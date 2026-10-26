# DossierExt — remaining goals

Ordered by value. Audited against the source on 2026-10-25, not from memory:
every "not built" below was confirmed by grepping `src/`.

Status of the phases in `DESIGN.md`: 0, 1, 2 (+2.1/2.2/2.3) and 3 are built and
verified in-game. Phase 4 is **half** built. Phase 5 is untouched.

---

## 1. ~~Honest-mode shroud filter~~ — DONE 2026-10-25 (hypothesis needs log check)
`DossierExt.Shrouded=` is parsed and logged, and **nothing consults it**. The
Observatory surveys the whole map omnisciently on every difficulty, so
`Shrouded=no` under `[Easy]` promises honesty and does not deliver it, and Gap
Generators do not blind the dossier AI the way DESIGN §5b says they must.

A dial that does nothing is worse than no dial — this is the most important
unbuilt item.

Blocked on research, not effort: needs a **per-house, synced** visibility read.
`CellClass::IsShrouded()` is current-player oriented and `Foggedness` is a render
field; neither is a per-house answer. Resolve via the hook encyclopedia /
registry before writing code, and confirm the read is sync-safe.

## 2. ~~Attack-window planner~~ — DONE 2026-10-25 (untested in-game)
The headline promise of the original brief: *"plan an attack that's too
expensive to counter."* Not built. The inputs all exist already — the economy
ledger knows the player's float, income and spend; the Scoreboard knows standing;
`Versus` knows where attention is. Declare a window when the player cannot afford
to answer force X within T seconds, and weight offensive triggers into it.
Gate it on `[Dossier.Escalation] AttackWindow.Unlock=` like any other lever.

## 3. ~~Weakness ledger~~ — LEDGER DONE 2026-10-25 (untested); TARGETING still open (needs trigger IDs)
Persist what actually beat this player (and what they never counter) per country,
then bias trigger selection game-over-game. The `Versus` "LARGELY IGNORES"
finding is already a weakness signal with nothing consuming it.

## 4. Trigger track-record learning (DESIGN §6a)
Per-player × country × trigger outcome record → weight **priors** at scenario
start, `[Dossier.TriggerFamilies]` for cross-game generalisation ("aerial fails
vs this player"), habit-based force-disable. ATTExt owns the within-game deltas;
this is the across-game half. Not built.

## 5. Phase 5 refinement
Sign decay (a tell that never predicts should fade and say so), cross-game
tuning, and whatever the first real dossiers suggest.

## 6. Per-unit production capture
The build-order tape is structures only, by polling. Units need a factory hook
for a true "what did they queue, when" tape. (Captured-vs-built separation for
structures already landed.)

## 7. Calibrate the Scoreboard weights
`ArmyWeight`/`EconWeight`/`TerritoryWeight` are still the initial equal thirds
and have never been calibrated against real logs.

## 8. Contribute our hooks to the encyclopedia
5 seats, all shared/already-proven, still not contributed back.
Per `[[hook-encyclopedia-workflow]]` this is owed after live verification.

---

## 9. LAST — LLM companion tool (Rex: revisit when the above is done)
**A tool, deliberately not an extension**, because nothing about it belongs
in-process. An LLM must never sit in the match loop: it is nondeterministic and
YR is lockstep, so a decision source that can differ between clients desyncs the
match outright — the exact hazard the rest of this project is engineered around.

Offline it is a good fit as the analyst that *writes the AI's rules*: read
`DossierProfiles/*.ini` and `WarZones/*.ini` between sessions and propose new
`[Dossier.Signs]` / `[Dossier.Strategies]` / `[Dossier.Responses]` entries as INI
text. Nothing it emits executes — the deterministic engine just follows rules.

The reason this works rather than merely sounding clever: **the fitness function
already exists.** The association counter grades every sign by fired/confirmed,
so the proposer can be speculative and the existing grader keeps only what
predicts (exactly as `HoardsCash` honestly decayed to 28% and stopped being
trusted). Proposer + grader is a closed loop with no LLM in the critical path.

Build the **mechanical sign search first**: the predicate grammar is small and
enumerable (observable × operator × threshold), so thousands of candidates can be
brute-forced against profile history deterministically and for free. The LLM's
edge is only in proposing semantically interesting composites a brute force
wouldn't reach ("pillbox before refinery while floating cash") — real, but
narrower than it first appears. Add it as a second proposer into the same grader.
