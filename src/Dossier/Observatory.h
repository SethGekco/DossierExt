#pragma once

#include <map>

class HouseClass;

// Shared per-house observation table for the Phase 1 Observatory + Scoreboard.
// Survey.cpp fills the map/army aggregates; Economy.cpp fills the ledger;
// Production.cpp diffs the structure counts; Scoreboard.cpp reads it all.
// Everything here is derived from SYNCED sim state (MP-safe); it is only
// logged in Phase 1, never fed back into the sim.

enum class Tier { Winning = 0, Even = 1, Losing = 2, Desperate = 3 };
const char* TierName(Tier t);

struct HouseObs
{
	// ── Economy ledger (Economy.cpp), refreshed each EconWindow ──────────
	bool EconInit = false;
	int LastHarvested = 0;   // HarvestedCredits snapshot
	int LastSpent = 0;       // CreditsSpent snapshot
	int IncomeRate = 0;      // harvested delta over the last window
	int SpendRate = 0;       // spent delta over the last window
	int FloatNow = 0;        // Available_Money() at last sample
	int PrevFloat = 0;
	int FloatTrend = 0;      // FloatNow - PrevFloat (per window)

	// ── Survey aggregates (Survey.cpp), refreshed each SurveyPeriod ──────
	bool SurveyInit = false;
	long long ArmyValue = 0;       // Σ cost of owned units/inf/aircraft
	long long BuildingValue = 0;   // Σ cost of owned buildings
	int OreNearest = -1;           // cells to nearest ore (-1 = none found)
	long long OreReachable = 0;    // ore value within OreReachRadius of base
	int TechBuildingsOwned = 0;    // capturable tech structures under control

	// ── Structure build-order tape (Production.cpp) ─────────────────────
	bool StructInit = false;
	std::map<int, int> StructCounts;      // BuildingType array index -> count
	std::map<int, int> PrevStructCounts;

	// ── Scoreboard (Scoreboard.cpp) ─────────────────────────────────────
	bool TierInit = false;
	double Standing = 1.0;          // raw self/enemy strength this eval
	double SmoothedStanding = 1.0;  // EMA momentum score the tier is read from
	Tier CurrentTier = Tier::Even;
};

namespace Observatory
{
	void Reset();
	HouseObs& Get(int houseIndex);
	HouseObs* Find(int houseIndex);
}
