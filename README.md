# DossierExt

The AI's **third brain** for Yuri's Revenge (Syringe DLL):

- **AITriggerTypeExt** — spam what works (the scripted playbook)
- **DoctrineExt** — analyze the battlefield and react (procedural doctrine)
- **DossierExt** — *know the player*: learn their habits per country and
  faction across games, watch their economy live, and abuse the knowledge —
  including runtime authority over AITriggerTypes and attacks timed to be too
  expensive to counter.

Dirtiness is a dial: per-difficulty INI toggles (`DossierExt.Shrouded=` in
[Easy]/[Normal]/[Difficult]) plus a Scoreboard that only unlocks the dirty
tools when the AI is actually losing. Gap Generators stay a real player
counter-intel tool in honest mode.

See [DESIGN.md](DESIGN.md) for the full design.

## Status

**Phase 0** — scaffold, identity resolution, per-difficulty key parsing, and
the profile read→play→write round trip (`DossierProfiles/<player>.ini`).
Log-only; no behavior change.

## Build

CI builds `DevBuild` via MSBuild (see `.github/workflows/build.yml`).
Submodules: YRpp + Phobos (utility headers only; this is not a Phobos fork),
pinned to the same commits as DoctrineExt.
