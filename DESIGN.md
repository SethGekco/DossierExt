# DossierExt — the AI's third brain

> Name settled (Rex 2026-09-18: "keep the name for now").
> "Dossier" = the file an intelligence agency keeps on a person: habits,
> weaknesses, patterns. That is literally what this DLL maintains.

## 1. The three brains

| Brain | Philosophy | Timescale |
|---|---|---|
| **AITriggerTypeExt** (aimd) | *Spam what works.* Know which combinations win and stick with them. | Per-wave, scripted playbook |
| **DoctrineExt** | *Play the game well.* Analyze the battlefield, react with the right tool at the right size. | Per-battle, seconds-to-minutes |
| **DossierExt** | *Know the player.* Learn their habits per country and faction, watch their economy, and abuse every weakness — including timing attacks they cannot afford to answer. | Per-game and **across games** |

The first two brains have no memory of the opponent as a *person*. DossierExt is
deliberately "a tad dirtier": it profiles the human, keeps that profile on disk
between games, and plays the player, not just the board.

It also sits *above* brain #1: DossierExt gets full authority to enable,
disable, and re-weight AITriggerTypes at runtime when it believes it knows what
the player is doing.

## 2. The two halves

DossierExt is really two cooperating subsystems:

### 2a. The Observatory (in-game, per-game)
Live, synced-data sensing. Everything here reads sim state that every peer has,
so it is MP-safe by construction:

- **Ore survey** — where ore/gems are on the map, how much, and the distance
  from each house's base center. Derived: who is ore-starved, whose expansion
  is contested, when the player's local patch will run dry.
- **Tech building survey** — neutral tech structures (derricks, airports,
  hospitals, labs), distance from each base, who has captured what.
- **Economy ledger per house** — credits, income rate, spending rate,
  growth/shrink trend over sliding windows. AITriggerTypeExt already proved the
  CreditsRate math; reuse the approach, not the DLL.
- **Spending habit capture** — *what* the player builds and *when*: production
  events logged as a timestamped build order (buildings, units, defenses,
  superweapons). This is the raw material for fingerprinting.
- **Sustainability analysis** — spending rate vs income rate vs remaining
  reachable ore ⇒ "this army composition is not sustainable; the mistake is
  coming." The AI plans around the player's inevitable overextension.

### 2b. The Dossier (across games, per player × country/faction)
A persistent profile file per player identity, with per-faction and per-country
sub-sections, because Rex's core insight is that **playstyle shifts with the
country played**:

- Opening fingerprint: typical build order for the first N minutes.
- Aggression profile: when they first attack, with what, how often.
- Composition habits: unit-mix histograms (armor-heavy? air spam? navy?).
- Economy habits: refinery count curve, expansion timing, typical float
  (unspent credits), crash frequency.
- Cliché strategies observed, with confirmation counts (see §4).
- Weaknesses: what killed them before, what they never counter well, which
  defenses they neglect.
- Recency-weighted: new games count more than old ones, so the profile tracks
  a player who changes, and habits decay if abandoned.

## 3. Data model

### Profile store (on disk, INI — Rex's INI-everything rule)
```
Profiles/<PlayerName>.ini            ; one file per identity

[Meta]                               ; games seen, last seen, version
[Overall]                            ; cross-faction aggregates
[Faction.Soviet]                     ; per-faction habits
[Country.Russians]                   ; per-country overrides (finer grain)
[Country.Russians.BuildOrder]        ; opening fingerprint, timestamped
[Country.Russians.Signs]             ; sign→strategy confirmation counters
[Country.Russians.Weaknesses]
```
All values are simple counters, rates, and timestamped lists — greppable,
hand-editable, diffable. A generic key-scanning parser, no bespoke binary blob.

### Identity
- Player name from the house (CnCNet lobby name in MP; skirmish human name in
  single player). Country + side recorded per game.
- AI houses are never profiled — only humans.

### Config (rulesmd, per Rex's INI-heavy rule)
```
[Dossier.General]      ; master switches, tick period, MP policy, debug
[Dossier.Signs]        ; the sign catalog (see §4)
[Dossier.Strategies]   ; strategy definitions + confirmation predicates
[Dossier.Responses]    ; strategy → AITrigger enable/disable/weight actions
```
Plus per-difficulty overrides in the vanilla difficulty sections (see §5a):
```
[Easy]                 ; DossierExt.Shrouded=no  DossierExt.Escalation=no  …
[Normal]
[Difficult]            ; DossierExt.Shrouded=yes DossierExt.Escalation=yes …
```

## 4. Sign → Strategy inference (the "constantly learn" loop)

The cliché-strategy detector is **not** hardcoded. It is a catalog:

- A **Sign** is a cheap observable predicate over Observatory data:
  `TwoRefineriesBefore=3000` (frames), `NavalYardExists`, `AirUnitRatio>0.4`,
  `EngineerCount>=3`, `BarracksOnly.NoWarFactoryBy=5000`, `FloatOver=8000`, …
- A **Strategy** is a hypothesis with a *confirmation predicate* — the ground
  truth event that proves it happened: `Rush = attacked with >=K units before
  frame N`, `EngiRush = engineer entered our building`, `AirBlitz = >=K air
  DPS on our base in one window`, `TechRush = tier-3 unit seen before frame N`.
- Learning = counting: every game, for every (sign fired, strategy confirmed)
  pair, bump the association counter in the player's dossier. Prediction =
  when signs fire in a live game, the strategies they historically preceded
  *for this player, on this country* get a confidence score.
- **Refinement is automatic**: signs that never predict anything decay to
  useless (and get logged as such); signs that reliably precede a strategy for
  this player become that player's tells. The catalog can grow in INI without
  touching the DLL.

This is dumb Bayesian counting, on purpose: deterministic, explainable in a
debug.log line ("EngiRush predicted: sign ThreeEngineers historically 7/8 for
Rex-as-France"), and cheap.

## 5. The dirtiness dial

Rex's guardrail (2026-09-18): don't go overboard. Gap Generators must stay a
*usable, meaningful* player tool — an always-omniscient AI would silently
delete them from the game. The dirtiness is therefore a **dial**, set two ways:
statically per difficulty, and dynamically by the Scoreboard.

### 5a. Per-difficulty toggles
Every dirty feature is individually toggleable, and the toggles live in the
vanilla difficulty sections so difficulty selection *is* the dial:
```
[Easy]
DossierExt.Shrouded=no        ; Observatory may NOT read shrouded/gapped cells
DossierExt.Escalation=no      ; Scoreboard never unlocks dirty tiers

[Difficult]
DossierExt.Shrouded=yes       ; Observatory sees through shroud + gap
DossierExt.Escalation=yes
```
Semantics pinned: `DossierExt.Shrouded=yes` means "the Observatory may read
cells the AI house has not legitimately revealed" (Rex's key name, verbatim).
More per-difficulty knobs as features land: prediction confidence thresholds,
attack-window aggressiveness, dossier-vs-observatory authority, etc.
`[Dossier.General]` holds the defaults; difficulty sections override.

**TRAP:** YR applies the difficulty multiplier sections *inverted* for AI
houses (a hard AI reads the buffed [Easy] section). Resolve per-house via the
house's actual difficulty field (the OwnerDifficulty read is already proven in
AITriggerTypeExt) and decide the section-name mapping there; Phase 0 must LOG
which section each house resolved so the mapping is verified, not assumed.

### 5b. Honest mode = Gap Generators genuinely work
With `Shrouded=no` the Observatory only reads cells some AI house has
explored/can currently see, and **gap-generator-cloaked cells are excluded**
— a gap field genuinely blinds the dossier AI's ore survey, economy watch,
and sign evaluation inside it. That makes gap coverage a real counter-intel
play for the player. (Exact per-house visibility + gap flags on CellClass:
resolve via encyclopedia/registry before hooking; must be a synced read.)
Persistent dossier habits learned in *previous* games remain fair game in
honest mode — that's memory, not wallhack.

### 5c. The Scoreboard — cheat when losing
Rex's rubber-band idea: the AI keeps score on itself and only reaches for the
dirty tools when it needs them. A per-AI-house **standing estimate** from
synced data: own vs enemy army value, building/base value, economy trend
(Observatory ledger), kill/loss exchange rate (0x702D40 kill facts), territory
/ ore control. Smoothed into a momentum score with hysteresis, bucketed:

```
WINNING → EVEN → LOSING → DESPERATE
```

Every dirty feature declares the condition that unlocks it (INI, per
difficulty), and the vocabulary covers the whole dial (Rex 2026-09-18 — the
switching terms themselves are user-customizable):
```
[Dossier.Escalation]
Shrouded.Unlock=Desperate     ; peek through shroud only when desperate
AttackWindow.Unlock=Losing
TriggerOverride.Unlock=Always ; on at all times, Scoreboard ignored
SomeFeature.Unlock=Never      ; hard-off, regardless of standing
```
`Unlock=` accepts `Always | Never | Even | Losing | Desperate` — so a modder
can pin any feature permanently on, permanently off, or standing-gated.
(`Always`/`Never` here override the per-difficulty on/off keys of §5a; the
§5a keys remain the coarse switch for users who don't touch escalation.)

The *standard* for "vulnerable" is customizable too — the tier boundaries are
data, not code:
```
[Dossier.Scoreboard]
Window=450                    ; frames per momentum sample window
Losing.Below=0.8              ; standing ratio (self vs enemies) → LOSING
Desperate.Below=0.5           ; → DESPERATE
Winning.Above=1.3             ; → WINNING
Hysteresis=0.1                ; band to climb back out of a tier
```
A user who thinks the AI should panic early sets `Losing.Below=1.0`; one who
wants a stoic AI sets `Desperate.Below=0.2`. Per-difficulty overridable like
everything else (`DossierExt.Losing.Below=` in [Easy]/[Difficult]).

So on [Difficult] a losing AI starts playing dirty and a winning AI plays
honest — the player only meets the wallhack when they're already ahead, which
is exactly when "punish predictability" feels fair instead of cheap.
De-escalation on recovery (with hysteresis so it doesn't flap). Every tier
change and every unlock/lock gets a log line.

The Scoreboard doubles as a *sensor*: "player is losing and floating cash" is
itself a sign for §4, and game-end standing curves go into the dossier
(comeback habits, tilt patterns).

## 6. Actuators — how the knowledge gets abused

1. **AITriggerType master control.** Direct manipulation of the engine's
   AITriggerType tables: force-enable, force-disable, and re-weight per house
   at runtime. When confidence in a predicted strategy crosses a threshold,
   [Dossier.Responses] maps it to trigger actions: "EngiRush predicted ⇒
   enable the anti-infantry defense triggers NOW, disable the eco-boom
   triggers." This is the supervisor seat over brain #1.
   (AITriggerTypeExt already does weight self-delta and cascades internally —
   DossierExt stays a separate DLL and acts on the same engine tables from
   above; no compile-time coupling. Verify the enable/weight fields + picker
   read-path via the hook registry / encyclopedia before hooking.)
2. **Attack-window planner.** The Observatory knows the player's credits,
   income, float, and army value; the Dossier knows their typical reaction.
   When `player cannot afford to counter force X within T seconds` — economy
   shrinking, float low, army committed elsewhere — declare an attack window
   and dump weight onto the offensive triggers that match. This is the "attack
   that's too expensive to counter" from the project brief.
3. **Weakness targeting.** Persistent weaknesses (never builds AA as Korea,
   always leaves the west flank open) bias trigger selection game-over-game.
4. *(Deliberately NOT an actuator: team dispatch.)* DossierExt does not build
   or steer teams — that is DoctrineExt's and aimd's job. It only pulls their
   levers. Keeps the brains separate, per the established one-primitive-per-DLL
   rule.

### 6a. Trigger track-record learning (moved from the tool TODO, 2026-09-18)

The cross-game half of the weight system. AITriggerTypeExt already ships the
WITHIN-game half (per-trigger Success/FailureWeightDelta overrides +
cross-trigger cascades, both live and verified) — everything below is what
that system deliberately does NOT do, and lands here instead.

- **Per-player trigger track record.** Record each trigger dispatch outcome
  (success/failure, from the same engine outcome path ATTExt's
  RegisterSuccess/Failure hooks observe — resolve our own seat via the
  registry, no compile coupling) into the profile, keyed player × country ×
  trigger, recency-weighted like every other dossier fact.
- **Weight priors at game start.** On scenario open, convert the track record
  into starting weights via the master-control lever (actuator #1): triggers
  that historically land against THIS player start heavy, proven losers start
  light — instead of every game re-learning from the static INI weights.
  `[Dossier.WeightLearning]` PriorDelta= / PriorClamp= / MinGames= keys.
- **Trigger families.** Cross-game generalization needs grouping ("aerial
  approaches fail vs this player", not just "trigger 0BB2A99C failed") — an
  INI catalog `[Dossier.TriggerFamilies]` Aerial=trigID,trigID,… (the wave
  tool can emit it at generation time since it knows each wave's role).
  Family-level priors are the cross-game analog of ATTExt's cascades.
- **Habit-based enable/disable.** A family ≥X% failed over the last N games
  vs this player gets force-disabled at start (re-evaluated mid-game);
  a proven opener gets its window boosted. This is actuator #1 driven by
  memory instead of by live prediction.
- **Honesty tier:** this is memory, not wallhack — per §5 decisions,
  cross-game habits are fair game, so these levers default `Always`; each
  still gets its own [Dossier.Escalation] key for modders who disagree.
- **Boundary (restate):** ATTExt owns within-game deltas/cascades; DossierExt
  owns across-game priors + habit gating, acting only through the engine
  tables from above. MP: profile-driven, so single-human-only per §7.

## 7. Hard constraints (learned the hard way elsewhere)

- **Desync.** Live Observatory sensing reads synced sim state → MP-safe. But
  the *persistent dossier* is a local file; in MP every client simulates every
  AI, and a profile that exists on one machine and not another = instant
  desync. Policy: `ProfilesInMultiplayer=no` (default) — dossier-driven
  actuation is active only with exactly one human player; Observatory-only
  mode (current-game data, identical on all peers) can stay on in MP.
  All randomness via ScenarioClass::Random, never hashed/local RNG, in any
  path that touches sim state.
- **Savegames.** Learned in-game state (sign firings, confidence) not
  serialized in early phases — document as known limitation, same as
  DoctrineExt Phase 1 cooldowns.
- **File IO timing.** Profile read at scenario start; profile write at
  game-end (find the one true game-over/score path via the encyclopedia — the
  score screen has the 8-entry overflow minefield, stay clear of it) plus a
  periodic checkpoint so a crash doesn't lose the game's learning.
- **Hook hygiene.** All addresses resolved via registry/hooks.csv + PDB symbol
  map first; overlap-check in CI; new hooks contributed back to the
  encyclopedia after live verification. Reuse proven seats where possible:
  HouseClass::Update 0x4F8440 (Doctrine's staggered sense tick pattern),
  Scenario_ClearClasses 0x685659 (per-scenario reset), ExeRun 0x7CD810 +
  CmdLineParse 0x52F639 (startup), RegisterDestruction 0x702D40 (kill facts).
- **Performance.** Ore survey is a whole-map scan → do it incrementally,
  staggered across frames (the big-map O(N²) lesson); cache and delta, never
  rescan from scratch per tick.

## 8. Phases

- **Phase 0 — scaffold + identity + persistence round-trip.** AcademyExt
  template, submodules pinned to the known-good commits. Parse [Dossier.*]
  AND the `DossierExt.*` per-difficulty keys — log which difficulty section
  each house resolves to (the §5a inversion trap check). At scenario start:
  resolve every house → (human?, name, country, faction, difficulty), log it.
  Load `Profiles/<name>.ini` if present; at game end write a stub profile
  (games-seen counter). Proves the full loop: identity → read → play →
  write. **Log-only.**
- **Phase 1 — the Observatory + Scoreboard.** Ore survey + tech building
  survey + per-house economy ledger (credits/income/spend/float, sliding
  windows) + production-event capture (the build-order tape) + the standing
  estimate with tier transitions (WINNING/EVEN/LOSING/DESPERATE) logged so
  Rex can grade whether "losing" matches what he sees on the field.
  Everything to debug.log, DebugTicks-gated. **Log-only; no behavior
  change.**
- **Phase 2 — the Dossier.** At game end, distill the Observatory tape into
  the profile: opening fingerprint, aggression timings, unit-mix histogram,
  economy habits, per-country sections, recency weighting. Verify across
  multiple real games that the file grows sanely and reads back.
- **Phase 3 — inference.** [Dossier.Signs] + [Dossier.Strategies] catalogs,
  live sign evaluation, confirmation detection, association counting into the
  profile, and live predictions in the log ("predicting AirBlitz 6/7").
  **Still log-only** — Rex can grade the predictions against what he actually
  did before any of it acts.
- **Phase 4 — actuation.** AITriggerType enable/disable/re-weight from
  [Dossier.Responses]; then the attack-window planner. Each actuator behind
  its own master switch AND its [Dossier.Escalation] unlock tier; log line
  for every lever pulled, including the tier that authorized it.
- **Phase 5 — refinement.** Sign decay, weakness ledger, cross-game tuning,
  and whatever the first real dossiers on Rex reveal.

## 9. Decisions log + open questions

**Decided (Rex, 2026-09-18):**
- Name stays DossierExt.
- Shroud honesty is per-difficulty INI (`DossierExt.Shrouded=` in
  [Easy]/[Difficult] etc.), not a global; Gap Generators must remain a real
  player tool in honest mode (§5b).
- Dirty features individually toggleable; the AI tracks winning/losing and
  escalates dirtiness only when it needs to (§5c Scoreboard).

**Still open:**
1. Profile directory location — game folder `Profiles/` next to debug.log?
2. In MP with 2+ humans, Observatory-only mode OK, or hard-off entirely?
3. Scoreboard formula weights (army vs economy vs territory) — start with
   equal thirds, calibrate from Phase 1 logs.
