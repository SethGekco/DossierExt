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

	// Does this player's strategy TRANSFER across maps/countries, or is it
	// tailored to each? Compares every per-map and per-country record against
	// the player's cross-everything Overall record: a small divergence means
	// the habit is portable (and so predicts them on an unplayed map), a large
	// one means that map/country is a special case. Also names the closest
	// already-played map by fingerprint, which is what a brand-new map should
	// inherit from. Phase 2.2 reports; Phase 3+ consults.
	void ReportTransfer(PlayerProfile const& profile);
}
