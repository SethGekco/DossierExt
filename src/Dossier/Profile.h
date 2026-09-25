#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// One dossier = one INI file per human player identity, kept on disk between
// games (DESIGN.md §2b/§3). Phase 2 distils each game's Observatory data into
// recency-weighted per-country habits: opening fingerprint, unit-mix, economy,
// aggression. Local file only; it does not feed sim decisions yet.
// One aggregate of habits. The SAME record type serves every scope: the
// cross-country "Overall" layer, each [Country.X], and each [Map.Y] /
// [Map.Y.SpawnN] — so a habit can be asked at whatever grain has evidence.
struct HabitRecord
{
	int Played = 0;
	int Won = 0;
	int Lost = 0;

	// Recency-weighted habit aggregates (EMA across games; see Distill).
	int HabitSamples = 0;           // how many games have folded in
	double AvgIncome = 0;           // sustained income per econ window
	double AvgPeakArmy = 0;         // typical peak army value
	double AvgMaxFloat = 0;         // typical peak unspent cash (floats money?)
	double AvgFirstKill = -1;       // aggression onset (frame of first kill)
	std::map<std::string, double> UnitMix; // folded composition fractions
	std::vector<std::string> Opening;      // "frame:TypeID", most recent game
	// Phase 3 learning: "Sign>Strategy" -> (times the sign fired, times the
	// strategy was ALSO confirmed that game). Confidence = second/first.
	std::map<std::string, std::pair<int, int>> Assoc;
};
using CountryRecord = HabitRecord; // back-compat alias

// What a map IS, independent of any player — so habits learned on one map can
// be transferred to an unplayed map that looks similar.
struct MapFingerprint
{
	int Width = 0;
	int Height = 0;
	int Spawns = 0;
	long long OreTotal = 0;
	bool Valid() const { return Width > 0 && Height > 0; }
};

// Where this player does things on a given map+spawn. Keys are "bx,by" bucket
// coordinates (SpatialBucket cells per bucket) so the INI stays readable.
struct SpatialRecord
{
	std::map<std::string, int> AttackGrid; // where they kill enemies (all game)
	std::map<std::string, int> RushGrid;   // ...within RushWindow = their rush path
	std::map<std::string, int> BuildGrid;  // time-weighted: where they hold/build
};

struct PlayerProfile
{
	std::string Name;           // sanitized identity (file name stem)
	std::string RawName;        // as the house reported it
	int GamesSeen = 0;
	int GamesWon = 0;
	int GamesLost = 0;
	// Which player names have fed this record. Only the install-wide profile
	// uses it: with a single name its habits are IDENTICAL to that name's own
	// record, so a divergence of 0.00 means "no evidence yet", NOT "consistent".
	std::set<std::string> Names;
	HabitRecord Overall;                              // cross-country layer
	std::map<std::string, HabitRecord> Countries;     // key = country ID
	std::map<std::string, HabitRecord> Maps;          // key = map, or "map#spawnN"
	std::map<std::string, SpatialRecord> Spatial;     // key = same as Maps
	std::map<std::string, MapFingerprint> MapInfo;    // key = bare map stem

	// This game's session state (not persisted as-is).
	int HouseIndex = -1;
	std::string CurrentCountry;
	std::string CurrentMapKey;  // "MapName" or "MapName#spawnN"
	std::string CurrentMapStem; // bare map name (fingerprint key)
	bool OutcomeRecorded = false;
	bool Dirty = false;
	bool IsGlobal = false;      // the install-wide "_AllHumans" record
};

namespace Profile
{
	void Reset();

	// Load (or create empty) the profile for a raw player name; registers it
	// in the session table keyed by house index.
	PlayerProfile& Open(const char* rawName, int houseIndex, const char* countryId);

	PlayerProfile* FindByHouse(int houseIndex);

	// The install-wide human record: fed by every LOCAL human game whatever
	// name was typed, so renaming can't shake the dossier (DESIGN §3a).
	PlayerProfile* Global();
	void OpenGlobal(const char* countryId, const char* mapKey);

	// The record exactly as it was loaded, before this match contributed.
	// Folding always restarts from here, which makes the fold IDEMPOTENT: it can
	// run at every checkpoint without double-counting, so an abandoned match
	// still teaches (Rex's matches routinely end with no win/loss).
	PlayerProfile* BaselineByHouse(int houseIndex);
	PlayerProfile* BaselineGlobal();

	// Write one profile to disk (creates ProfileDir if needed). Logs the path
	// and result; returns success.
	bool Save(PlayerProfile& profile, const char* lastOutcome);

	// Flush every dirty profile (periodic checkpoint).
	void CheckpointAll();
}
