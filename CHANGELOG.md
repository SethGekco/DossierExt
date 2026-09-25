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
