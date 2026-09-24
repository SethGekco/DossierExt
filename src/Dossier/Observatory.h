#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

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
	int IncomeRate = 0;      // raw money-in this window (float trend + spend)
	double SmoothedIncome = 0; // EMA of IncomeRate — the scoreboard's econ input
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
	int TechBuildingsOwned = 0;    // neutral/captured assets under control
	int TechListed = 0;            // ...declared in [AI] NeutralTechBuildings
	int TechCivilian = 0;          // ...unlisted but civilian-shaped
	int TechCaptured = 0;          // ...taken from someone (fan-map ConYards)

	// ── Structure build-order tape (Production.cpp) ─────────────────────
	bool StructInit = false;
	std::map<int, int> StructCounts;      // BuildingType array index -> count
	std::map<int, int> PrevStructCounts;

	// ── Scoreboard (Scoreboard.cpp) ─────────────────────────────────────
	bool TierInit = false;
	double Standing = 1.0;          // raw self/enemy strength this eval
	double SmoothedStanding = 1.0;  // EMA momentum score the tier is read from
	Tier CurrentTier = Tier::Even;

	// ── Phase 2 per-game accumulation (distilled into the profile at end) ─
	// Opening build order: (frame, BuildingType array index), in order, capped.
	std::vector<std::pair<int, int>> BuildOrder;
	// Time-weighted unit-mix: type ID (e.g. "HTNK") -> Σ (count seen per
	// survey). Normalise to fractions at distill time. Keyed by ID string to
	// avoid index collisions across Unit/Infantry/Aircraft type arrays.
	std::map<std::string, long long> UnitMix;
	long long UnitMixSamples = 0;  // Σ units counted across surveys (denominator)
	// Economy habits.
	int MaxFloat = 0;
	long long SumIncome = 0;       // Σ smoothed income over samples
	int IncomeSamples = 0;
	double PeakIncome = 0;
	// Peaks.
	long long PeakArmy = 0;
	long long PeakBuilding = 0;
	// Aggression (KillTracker hook).
	int FirstKillFrame = -1;       // frame this house scored its first kill
	int KillsDealt = 0;
	int LossesTaken = 0;

	// Spatial habits — "where do they do things", bucketed "bx,by".
	std::map<std::string, int> AttackGrid; // where this house kills enemies
	std::map<std::string, int> RushGrid;   // ...inside RushWindow = the rush path
	std::map<std::string, int> BuildGrid;  // time-weighted building presence
};

namespace Observatory
{
	void Reset();
	HouseObs& Get(int houseIndex);
	HouseObs* Find(int houseIndex);

	// "bx,by" key for a cell, at the configured SpatialBucket resolution.
	std::string BucketKey(int cellX, int cellY);
}
