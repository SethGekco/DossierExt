#pragma once

struct PlayerProfile;
struct HouseObs;

// Phase 2: fold one game's Observatory accumulation into the persistent
// per-country habit aggregates of a player's profile, recency-weighted (recent
// games count more). Called once at game end, before the profile is saved.
namespace Distill
{
	void FoldHabits(PlayerProfile& profile, HouseObs& obs);
}
