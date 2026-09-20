#pragma once

#include <map>
#include <string>
#include <vector>

// One dossier = one INI file per human player identity, kept on disk between
// games (DESIGN.md §2b/§3). Phase 2 distils each game's Observatory data into
// recency-weighted per-country habits: opening fingerprint, unit-mix, economy,
// aggression. Local file only; it does not feed sim decisions yet.
struct CountryRecord
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
};

struct PlayerProfile
{
	std::string Name;           // sanitized identity (file name stem)
	std::string RawName;        // as the house reported it
	int GamesSeen = 0;
	int GamesWon = 0;
	int GamesLost = 0;
	std::map<std::string, CountryRecord> Countries; // key = country ID (e.g. "Americans")

	// This game's session state (not persisted as-is).
	int HouseIndex = -1;
	std::string CurrentCountry;
	bool OutcomeRecorded = false;
	bool Dirty = false;
};

namespace Profile
{
	void Reset();

	// Load (or create empty) the profile for a raw player name; registers it
	// in the session table keyed by house index.
	PlayerProfile& Open(const char* rawName, int houseIndex, const char* countryId);

	PlayerProfile* FindByHouse(int houseIndex);

	// Write one profile to disk (creates ProfileDir if needed). Logs the path
	// and result; returns success.
	bool Save(PlayerProfile& profile, const char* lastOutcome);

	// Flush every dirty profile (periodic checkpoint).
	void CheckpointAll();
}
