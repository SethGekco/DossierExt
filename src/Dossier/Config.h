#pragma once

#include <string>

// Parsed [Dossier.*] config + the per-difficulty DossierExt.* keys from the
// vanilla rules difficulty sections ([Easy]/[Normal]/[Difficult]).
//
// Difficulty mapping (DESIGN.md §5a): the engine's multiplier sections are
// INVERTED for AI houses (AIDifficulty 0=Hard reads [Easy]). Our DossierExt.*
// keys are OURS, not engine multipliers, so they use the INTUITIVE mapping:
// a Hard AI reads [Difficult]. Phase 0 logs both resolutions per house so the
// mapping is verified in-game, not assumed.
struct DossierDifficultyKeys
{
	bool Shrouded = false;   // Observatory may read shrouded/gapped cells
	bool Escalation = false; // Scoreboard may unlock dirty tiers
	bool AnySet = false;     // any DossierExt.* key present in the section
};

struct DossierConfig
{
	bool Parsed = false;

	// [Dossier.General]
	bool Enabled = true;
	bool DebugTicks = false;
	int CheckpointInterval = 3000;          // frames between profile flushes
	std::string ProfileDir = "DossierProfiles";

	// Phase 1 — Observatory
	int EconWindow = 450;                    // frames per economy sample
	double EconSmoothing = 0.3;              // EMA alpha for income (bursty unloads)
	int SurveyPeriod = 900;                  // frames between map/army scans
	int OreReachRadius = 30;                 // cells: "reachable ore" near base
	int ScoreboardPeriod = 450;              // frames between standing re-evals

	// Phase 2 — Dossier distillation
	int OpeningMaxEvents = 40;               // build-order events kept for the opening
	double RecencyWeight = 0.4;              // EMA weight of THIS game vs history

	// Phase 1 — [Dossier.Scoreboard] (tier thresholds; DESIGN §5c)
	double LosingBelow = 0.8;                // standing ratio -> LOSING
	double DesperateBelow = 0.5;             // -> DESPERATE
	double WinningAbove = 1.3;               // -> WINNING
	double Hysteresis = 0.1;                 // band to climb back out of a tier
	double StandingSmoothing = 0.3;          // EMA alpha: momentum vs responsiveness
	// Standing formula weights (open question #3: start equal thirds)
	double ArmyWeight = 1.0;
	double EconWeight = 1.0;
	double TerritoryWeight = 1.0;

	// DossierExt.* keys, indexed by AIDifficulty (0=Hard, 1=Normal, 2=Easy —
	// the engine's inverted enum; resolution helpers hide the confusion).
	DossierDifficultyKeys Diff[3];

	static DossierConfig Instance;

	static void Reset();
	static void EnsureParsed();

	// The INTUITIVE rules section this AIDifficulty reads DossierExt.* from.
	static const char* IntuitiveSection(int aiDifficulty);
	// The engine's (inverted) multiplier section for the same value, for the
	// Phase 0 verification log.
	static const char* EngineMultiplierSection(int aiDifficulty);
};
