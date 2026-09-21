#pragma once

struct PlayerProfile;
struct HouseObs;

// Phase 2: fold one game's Observatory accumulation into the persistent
// per-country habit aggregates of a player's profile, recency-weighted (recent
// games count more). Called once at game end, before the profile is saved.
namespace Distill
{
	void FoldHabits(PlayerProfile& profile, HouseObs& obs);

	// Log whether this NAME has earned standalone trust or whether the AI
	// would fall back to the install-wide record (so renaming buys nothing).
	// Phase 2 reports only; Phase 3+ consults the same test.
	void ReportIdentityTrust(PlayerProfile const& named);
}
