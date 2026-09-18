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
