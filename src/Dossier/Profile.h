#pragma once

#include <map>
#include <string>

// One dossier = one INI file per human player identity, kept on disk between
// games (DESIGN.md §2b/§3). Phase 0 scope: the [Meta] counters and per-country
// played/won/lost stubs — just enough to prove the identity→read→play→write
// round trip. Local file only; it never feeds sim decisions in this phase.
struct CountryRecord
{
	int Played = 0;
	int Won = 0;
	int Lost = 0;
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
