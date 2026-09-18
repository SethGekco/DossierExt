#pragma once

// Phase 0 identity resolution (DESIGN.md §3): once per scenario, resolve every
// house → (human?, name, country, side, difficulty), log the roster, and open
// a dossier for each human. Also logs which rules section each house resolves
// its DossierExt.* keys from — the §5a inversion-trap verification.
namespace Identity
{
	void Reset();

	// Idempotent; called from the first house tick, when houses are fully set
	// up. Returns true once the roster has been resolved.
	bool EnsureRoster();

	int HumanCount();
}
