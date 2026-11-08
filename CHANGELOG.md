# Changelog — DossierExt

> Convention (Rex, 2026-09-24): **every change gets an entry here, in the same
> commit.** Several sessions work across this project and its siblings without
> seeing each other's context; the changelog is the shared channel. Newest first,
> and always say *why* — the reason is what saves the next agent.
>
> **Dependency:** DossierExt will consume **WarZoneExt** (`~/Claude/WarZoneExt`),
> the shared spatial-memory DLL, as an *optional* runtime dependency once its API
> lands. Log changes that touch that boundary in **both** changelogs. WarZoneExt
> treats its exported API, its zone **names**, and its on-disk format as
> contracts — read its CHANGELOG before assuming any of them.

## [unreleased] — 2026-11-08

### Fixed — tape crash on a building placement (live 14:41-14:43, rolled back)
- Rex's game crashed (`C0000005` in `strnlen`, DossierExt+0x2d630) on a PLACE
  right after queuing dogs. The linker map put it in the tape's PRODUCE/PLACE
  line: `TechnoTypeClass::GetByTypeAndIndex` (0x48DCD0) indexes the type array
  with no bounds check, and a PLACE can carry HeapID -1. `HouseClass::Place`
  (0x4FB0E0) guards `heap < 0` itself, because it takes the building from the
  house's factory, not from the event. We read `array[-1]` and printed it as a
  name. Now our own `TypeOf()` uses the bounds-checked `GetItemOrDefault` per
  type array, and an unresolved type logs `?(rtti N heap M)`.
- Lesson: an engine lookup the engine itself guards before calling is not
  safe to call raw from a hook. Check how the engine calls it first.

### Added — L1a opening book + L1b tactics tape (DESIGN §10c/§10d; record/log only)
- Why: Rex wants the AI to learn from everything the player does, keep what
  works for similar situations, keep what fails as things to avoid and
  counter, merge overlapping records, and learn **without full games**. Rex
  tests DLLs all day, so even short test games should teach the AI to be in
  peak form in its first 60 seconds.
- Found while designing it: nothing learned in a game survived a quit before
  the first checkpoint (default f3000; rulesmd has 1500). `0x685659` only
  resets, and under the spawner each match is its own process, so it fires at
  the next game's start, after ClearScenario has destroyed the objects. The
  structure tape also polled only every SurveyPeriod (900 frames), far too
  coarse for an opening.
- **Opening book** (`Opening.cpp`, `[Dossier.Openings]`): every house, AI
  and human alike. Build tape = diff of `FactoryProducedBuildingTypes` every
  `Tick`=15 frames (a lifetime counter that never decreases, so captures
  don't count; read through the const overload so we never grow the engine's
  vector; walls skipped). Graded at `Marks`=600..9000: tempo = spent so far +
  income rate × `Horizon` − `LowPowerPenalty`. Each mark is logged
  (`opening <house> [<ctx>] f600: tempo ...`) and saved on the next tick, so a
  game quit at f1300 still keeps marks 600 and 1200.
  `DossierProfiles/_Openings.ini` is a prefix tree per context (side × start
  credits × Human/AIHard/AINormal/AIEasy). Openings that share a start share
  stored nodes, a mark only counts for nodes built before it, and leaves with
  the least evidence are evicted past `MaxNodes`. A house that built no
  structure is not graded (an idle slot in an unattended test, or a player
  only watching, is not an opening). Saves rewind to the loaded
  baseline (no double count) and write through `.tmp` + rename. At game start
  `BOOK <ctx>: ... Best line: ...` gives the best-evidenced line from history.
- **Tactics tape** (`Tape.cpp`, `[Dossier.Tape]`): new hook **0x4C6CB0**
  `EventClass::RespondToEvent` entry (6 bytes `sub esp,0x370`, unhooked by
  the registry and every ~/Claude/*/src). Human orders only (move, attack,
  stop, deploy, produce, place, sell, ...). MegaMission lines carry the unit,
  mission, target (own/ally/foe), destination, distance, both weapon ranges,
  health and `RearmTimer`, the raw material for kiting cases. Every order is
  counted, and per-house totals are logged at checkpoints. The first
  `LogLines`=150 per match are written out.
- Nothing acts on either yet. Hooks: 7 (overlap + bounds checks clean against
  the registry and gamemd-spawn.exe).


### Designed — learning from the player (DESIGN §10, TODO 9)
- Why: Rex asked for the AI to copy the player's strategies that work, learn
  micro (kiting) and the exploits humans use, and write a post-mortem when a
  strategy it trusted gets beaten (how it was beaten, its shortcomings) so it
  can come back better.
- Recorder seat chosen and checked: `EventClass::RespondToEvent` entry
  **0x4C6CB0** (`sub esp,0x370`, 6 bytes). Not hooked by anything in the
  registry or any ~/Claude/*/src; the neighbours are 0x4C6CC8 (CnCNet-Spawner,
  Phobos, AggressiveStance), 0x4C6CCD (Ares/Antares) and 0x4C6CF0 (Phobos). It
  runs on every client at the same frame, so the tape is synced data.
- Honest limit recorded in the design: the AI learns *which* named technique
  works, where and with what parameters; it cannot invent new micro from
  clicks. Unnamed lopsided exchanges are logged by a free-damage catch-all so
  they can be named later.
- Boundary kept: micro executors belong in DoctrineExt (handoff, not a
  cross-repo edit), macro playbooks go through the Actuator. Old TODO 9 (LLM
  tool) is now 10 and still LAST.

### Note — SellWatch
- Rex 2026-11-08: DoctrineExt does fire-sale-related things on purpose. A
  `FIRESALE ... called from DoctrineExt.dll+...` line is expected, not a bug.
- Through the 11-08 13:29 log there were **no** Fire_Sale calls in the last 40
  logs. The AI sales are one building at a time (1-9 per house, GACNST
  included, full HP), mostly with money near 0 (AIMode 1), but one ConYard
  went at 4,566 credits in AIMode 0, so low cash is not the whole story.
  Sales via a Sell mission bypass Fire_Sale, so SellWatch cannot name who
  ordered those. `[Dossier.General] SellWatch=no` silences it.

## [unreleased] — 2026-10-26

### Added — SellWatch (diagnostic, log-only, `[Dossier.General] SellWatch=yes`)
- Why: Rex's Easy AIs liquidated their bases ~f2100 (GAPOWR+GAPILE torn down in
  the same frame, GAREFN ~110 frames later) and Short Game then defeated them
  with ~39 live units. No DLL logged a fire sale, and the ConYard left
  invisibly: PrerequisiteExt's `SOLD by house` line skips `UndeploysInto`
  types, so "GACNST never sold" in that log means nothing.
- New hook **0x5013A0 `HouseClass::Fire_Sale`** (IHouse vtable +0x34; `push
  edi; mov edi,[esp+8]` = 5 bytes, no rel32; nobody else hooks it). Logs
  `FIRESALE <house> called from <module>+<rva>` — the return address names
  the culprit. gamemd's own sites decode by name: 0x4FD7BA = AIMode SellAll
  tick, 0x4FD901 = last-legs (normally seen as Phobos.dll, whose
  `HouseClass_UpdateAI_OnLastLegs` replaces that call).
- Per AI house tick: `sellwatch ... AIMode a->b ConYards c->d` and `SELL
  START <type>`, each with money, ConYard/building counts, the +0x54D8 stamp,
  and the engine's exact last-legs factory census (alive, !InLimbo,
  `Type->Factory != None`). `CONYARD DESTROYED ... by` from the kill seat
  separates combat deaths from sales.
- Decoded from gamemd-spawn.exe so the log reads against ground truth:
  HouseClass +0x250 AIMode (0 General, 1 LowOnCash <25 credits, 3 = 900-frame
  window after the +0x54D8 stamp which skips last-legs, 4 SellAll); script
  action 30 (jump table 0x6E9F74) and two TAction handlers are the ONLY
  writers of SellAll; Fire_Sale has no direct callers, only the vtable.
  Ruled out for the 2026-10-25 game: DoctrineExt's two Fire_Sale calls (need
  crates and !ShortGame; game was Crates-Off/ShortGame-On), aimd.ini and map
  scripts (no action 30 anywhere; map has no triggers), Phobos AutoDeath
  (only E1DroneLauncher), BaseUnitGuard (not installed), SquadExt (members
  are infantry). Remove or default-off once the cause is found.

## [unreleased] — 2026-09-24

### Added — Phase 3: inference
- `[Dossier.Signs]` / `[Dossier.Strategies]` catalogs over one predicate
  grammar, so signs (tells) and strategies (ground truth) share a single
  evaluator and grow in INI without DLL changes. Observables all read the
  existing Observatory; `AttackBucket`/`RushBucket`/`BuildBucket` let a strategy
  confirm "he pushed that lane again" straight off the spatial grids.
- Learning by counting: at game end each fired sign is paired with each
  strategy and `(fired, confirmed)` is stored as
  `[<scope>.Assoc] Sign>Strategy=fired,confirmed`, per-country and Overall.
- Prediction on a sign firing, gated by `MinSamples`/`Confidence`; prefers the
  per-country record, falls back to the install-wide one so a fresh name
  inherits install memory. Verified in-game (4 signs fired, associations
  written). Still log-only — nothing acts on a prediction.

### Added — Phase 2.1/2.2/2.3: identity, layers, honest verdicts
- Install-wide `_AllHumans.ini` record so renaming buys no fresh start; a name
  only stands alone with enough games AND measurable divergence.
- Record layers: Overall / per-country / per-map / per-spawn, each toggleable
  for modders watching file size; spatial Attack/Rush/Build grids bucketed and
  pruned to `SpatialTopN`.
- Transfer analysis: is a habit portable or map/country-specific, plus map
  fingerprints so an unplayed map inherits from the closest known one.
- `TransferMinGames` and single-name/single-country guards, because one game's
  noise IS the whole sample and divergence of 0.00 was being printed as a
  confident "PORTABLE".

### Fixed
- **Map identity:** every map was recorded as `spawnmap` —
  `ScenarioClass::FileName` is always `spawnmap.ini` in skirmish/CnCNet. Use
  spawn.ini `[Settings] UIMapName`. This had collapsed all maps into one record.
- **Tech buildings:** `Capturable` is set on ordinary buildings
  (GAPILE/GAREFN/GAPOWR), so testing it counted the owner's whole base. Now uses
  `[AI] NeutralTechBuildings` + `ExtraTechBuildings` (stock list lacks CASLAB),
  with the `TechLevel<0` shape heuristic demoted to opt-in after it
  false-positived on the Construction Yard (GACNST is `TechLevel=-1
  Capturable=true` because ConYards deploy rather than build).
- **"Changed hands"** is `Owner != InitialOwner`, not `HasBeenCaptured`.
- **Build-order purity:** captured structures were entering the opening
  fingerprint as if built (a spy-targeting bug in another extension had Rex
  capturing a NABNKR, which then appeared as `+1 NABNKR` in his build order).
  Captured counts are tracked separately and excluded; acquisitions log as
  `acquired ... (changed hands, not built)`.
- Income derived from `float + spend` because `HarvestedCredits` reads constant
  in this stack; EMA-smoothed at source, since harvester unloads arrive in
  bursts and were whipsawing the Scoreboard tier.
- Scoreboard territory counts tech structures as well as reachable ore, after a
  derrick-free ore-poor map left the dimension dead all game.
- Per-scope `Played/Won/Lost` (only the Country scope was counting).

### Notes
- Known open: an AI showed `taken=14` that remains unexplained; the
  `tech=<ID>(<rule>)` breakdown log is deployed to name them next run. An
  earlier garrison explanation for it was **unverified speculation and wrong** —
  do not repeat it.
- Strategy thresholds were initially calibrated from cross-game totals while the
  spatial grids reset per game, so nothing ever confirmed; retuned in rulesmd.
