#include "Dossier/Profile.h"
#include "Dossier/Config.h"

#include <Utilities/Debug.h>

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>

namespace
{
	std::map<int, PlayerProfile> g_byHouse; // house index -> profile
	PlayerProfile g_global;                 // install-wide human record
	bool g_globalOpen = false;

	// File-name-safe identity: [A-Za-z0-9_-], everything else becomes '_'.
	std::string Sanitize(const char* raw)
	{
		std::string out;
		for (const char* p = raw; *p; ++p)
		{
			char const c = *p;
			bool const ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
				|| (c >= '0' && c <= '9') || c == '_' || c == '-';
			out += ok ? c : '_';
		}
		if (out.empty())
			out = "unnamed";
		return out;
	}

	std::string PathFor(const std::string& name)
	{
		return DossierConfig::Instance.ProfileDir + "\\" + name + ".ini";
	}

	std::string Timestamp()
	{
		char buf[32] = { 0 };
		std::time_t const now = std::time(nullptr);
		std::tm tmv;
		if (localtime_s(&tmv, &now) == 0)
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
		return buf;
	}

	// "a,b,c" -> set
	void SplitNames(const char* const value, std::set<std::string>& out)
	{
		std::string tok;
		for (const char* p = value; ; ++p)
		{
			if (*p == ',' || *p == '\0')
			{
				if (!tok.empty()) out.insert(tok);
				tok.clear();
				if (*p == '\0') break;
			}
			else if (*p != ' ' || !tok.empty())
				tok += *p;
		}
	}

	// Apply one key=value to a habit record.
	void AssignHabit(HabitRecord& rec, const char* const key, const char* const value)
	{
		if (!std::strcmp(key, "Played")) rec.Played = std::atoi(value);
		else if (!std::strcmp(key, "Won")) rec.Won = std::atoi(value);
		else if (!std::strcmp(key, "Lost")) rec.Lost = std::atoi(value);
		else if (!std::strcmp(key, "HabitSamples")) rec.HabitSamples = std::atoi(value);
		else if (!std::strcmp(key, "AvgIncome")) rec.AvgIncome = std::atof(value);
		else if (!std::strcmp(key, "AvgPeakArmy")) rec.AvgPeakArmy = std::atof(value);
		else if (!std::strcmp(key, "AvgMaxFloat")) rec.AvgMaxFloat = std::atof(value);
		else if (!std::strcmp(key, "AvgFirstKill")) rec.AvgFirstKill = std::atof(value);
	}

	// Route "<Scope>[.<Name>][.<Sub>]" — Sub is UnitMix/Opening for habit
	// scopes, or Attack/Rush/Build for Spatial.
	void ApplyLine(PlayerProfile& profile, std::string const& section,
		const char* const key, const char* const value)
	{
		auto const d1 = section.find('.');
		std::string const scope = section.substr(0, d1);
		std::string rest = d1 == std::string::npos ? "" : section.substr(d1 + 1);

		if (scope == "Meta")
		{
			if (!std::strcmp(key, "Names")) SplitNames(value, profile.Names);
			else if (!std::strcmp(key, "GamesSeen")) profile.GamesSeen = std::atoi(value);
			else if (!std::strcmp(key, "GamesWon")) profile.GamesWon = std::atoi(value);
			else if (!std::strcmp(key, "GamesLost")) profile.GamesLost = std::atoi(value);
			return;
		}

		// Split rest into name + sub (habit scopes other than Overall carry a
		// name; Overall's "rest" IS the sub).
		std::string name, sub;
		if (scope == "Overall")
			sub = rest;
		else
		{
			auto const d2 = rest.find('.');
			name = rest.substr(0, d2);
			sub = d2 == std::string::npos ? "" : rest.substr(d2 + 1);
		}

		if (scope == "MapInfo")
		{
			if (name.empty())
				return;
			auto& fp = profile.MapInfo[name];
			if (!std::strcmp(key, "Width")) fp.Width = std::atoi(value);
			else if (!std::strcmp(key, "Height")) fp.Height = std::atoi(value);
			else if (!std::strcmp(key, "Spawns")) fp.Spawns = std::atoi(value);
			else if (!std::strcmp(key, "OreTotal")) fp.OreTotal = std::atoll(value);
			return;
		}

		if (scope == "Spatial")
		{
			if (name.empty())
				return;
			auto& sp = profile.Spatial[name];
			int const v = std::atoi(value);
			if (sub == "Attack") sp.AttackGrid[key] = v;
			else if (sub == "Rush") sp.RushGrid[key] = v;
			else if (sub == "Build") sp.BuildGrid[key] = v;
			return;
		}

		HabitRecord* pRec = nullptr;
		if (scope == "Overall") pRec = &profile.Overall;
		else if (scope == "Country" && !name.empty()) pRec = &profile.Countries[name];
		else if (scope == "Map" && !name.empty()) pRec = &profile.Maps[name];
		if (!pRec)
			return;

		if (sub.empty()) AssignHabit(*pRec, key, value);
		else if (sub == "UnitMix") pRec->UnitMix[key] = std::atof(value);
		else if (sub == "Opening") pRec->Opening.push_back(value);
	}

	// Minimal INI reader for our own files: sections + key=value, no quoting.
	void LoadFromDisk(PlayerProfile& profile)
	{
		auto const path = PathFor(profile.Name);
		FILE* const f = std::fopen(path.c_str(), "r");
		if (!f)
		{
			Debug::Log("[DossierExt] profile %s: no file yet (%s), starting fresh.\n",
				profile.Name.c_str(), path.c_str());
			return;
		}

		char line[512];
		std::string section;
		while (std::fgets(line, sizeof(line), f))
		{
			char* s = line;
			while (*s == ' ' || *s == '\t')
				++s;
			char* const end = s + std::strlen(s);
			char* e = end;
			while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
				--e;
			*e = '\0';
			if (!*s || *s == ';')
				continue;
			if (*s == '[')
			{
				char* const close = std::strchr(s, ']');
				if (close)
				{
					*close = '\0';
					section = s + 1;
				}
				continue;
			}
			char* const eq = std::strchr(s, '=');
			if (!eq)
				continue;
			*eq = '\0';
			char const* const key = s;
			char const* const value = eq + 1;

			ApplyLine(profile, section, key, value);
		}
		std::fclose(f);
		Debug::Log("[DossierExt] profile %s loaded: seen=%d won=%d lost=%d countries=%u\n",
			profile.Name.c_str(), profile.GamesSeen, profile.GamesWon, profile.GamesLost,
			profile.Countries.size());
	}
}

void Profile::Reset()
{
	g_byHouse.clear();
	g_global = PlayerProfile{};
	g_globalOpen = false;
}

PlayerProfile* Profile::Global()
{
	return g_globalOpen ? &g_global : nullptr;
}

void Profile::OpenGlobal(const char* const countryId, const char* const mapKey)
{
	if (g_globalOpen)
		return;
	g_globalOpen = true;
	g_global.RawName = DossierConfig::Instance.GlobalProfileName;
	g_global.Name = g_global.RawName;
	g_global.IsGlobal = true;
	g_global.CurrentCountry = countryId;
	g_global.CurrentMapKey = mapKey;
	// Fingerprints key off the bare stem (everything before "#spawnN").
	g_global.CurrentMapStem = std::string(mapKey).substr(0, std::string(mapKey).find('#'));
	LoadFromDisk(g_global);
	++g_global.GamesSeen;
	// Per-scope Played is counted at fold time (completed games only).
	g_global.Dirty = true;
	Save(g_global, "InProgress");
}

PlayerProfile& Profile::Open(const char* const rawName, int const houseIndex, const char* const countryId)
{
	auto& profile = g_byHouse[houseIndex];
	profile.RawName = rawName;
	profile.Name = Sanitize(rawName);
	profile.HouseIndex = houseIndex;
	profile.CurrentCountry = countryId;
	LoadFromDisk(profile);

	// Seeing the player at all counts, and writing immediately proves the IO
	// path at game START rather than discovering a broken path at game end.
	++profile.GamesSeen;
	// Per-scope Played is counted at fold time (completed games only)...
	profile.Dirty = true;
	Save(profile, "InProgress");
	return profile;
}

PlayerProfile* Profile::FindByHouse(int const houseIndex)
{
	auto const it = g_byHouse.find(houseIndex);
	return it != g_byHouse.end() ? &it->second : nullptr;
}

bool Profile::Save(PlayerProfile& profile, const char* const lastOutcome)
{
	auto const& dir = DossierConfig::Instance.ProfileDir;
	CreateDirectoryA(dir.c_str(), nullptr); // no-op if it already exists

	auto const path = PathFor(profile.Name);
	FILE* const f = std::fopen(path.c_str(), "w");
	if (!f)
	{
		Debug::Log("[DossierExt] WARNING: cannot write profile %s\n", path.c_str());
		return false;
	}

	std::fprintf(f, "; DossierExt player profile — generated, hand-editable.\n");
	std::fprintf(f, "[Meta]\nName=%s\nGamesSeen=%d\nGamesWon=%d\nGamesLost=%d\n",
		profile.RawName.c_str(), profile.GamesSeen, profile.GamesWon, profile.GamesLost);
	std::fprintf(f, "LastSeen=%s\nLastCountry=%s\nLastOutcome=%s\n",
		Timestamp().c_str(), profile.CurrentCountry.c_str(), lastOutcome);
	if (!profile.Names.empty())
	{
		std::string joined;
		for (auto const& n : profile.Names)
		{
			if (!joined.empty()) joined += ",";
			joined += n;
		}
		std::fprintf(f, "Names=%s\n", joined.c_str());
	}
	// One habit record, under an arbitrary scope prefix.
	auto writeHabit = [&f](std::string const& prefix, HabitRecord const& rec)
	{
		std::fprintf(f, "\n[%s]\nPlayed=%d\nWon=%d\nLost=%d\nHabitSamples=%d\n",
			prefix.c_str(), rec.Played, rec.Won, rec.Lost, rec.HabitSamples);
		std::fprintf(f, "AvgIncome=%.1f\nAvgPeakArmy=%.1f\nAvgMaxFloat=%.1f\nAvgFirstKill=%.1f\n",
			rec.AvgIncome, rec.AvgPeakArmy, rec.AvgMaxFloat, rec.AvgFirstKill);
		if (!rec.UnitMix.empty())
		{
			std::fprintf(f, "[%s.UnitMix]\n", prefix.c_str());
			for (auto const& [id, frac] : rec.UnitMix)
				std::fprintf(f, "%s=%.3f\n", id.c_str(), frac);
		}
		if (!rec.Opening.empty())
		{
			std::fprintf(f, "[%s.Opening]\n", prefix.c_str());
			int n = 0;
			for (auto const& ev : rec.Opening)
				std::fprintf(f, "%d=%s\n", n++, ev.c_str());
		}
	};

	auto writeGrid = [&f](std::string const& prefix, const char* const sub,
		std::map<std::string, int> const& grid)
	{
		if (grid.empty())
			return;
		std::fprintf(f, "[%s.%s]\n", prefix.c_str(), sub);
		for (auto const& [bucket, weight] : grid)
			std::fprintf(f, "%s=%d\n", bucket.c_str(), weight);
	};

	if (profile.Overall.HabitSamples > 0)
		writeHabit("Overall", profile.Overall);
	for (auto const& [country, rec] : profile.Countries)
		writeHabit("Country." + country, rec);
	for (auto const& [mapKey, rec] : profile.Maps)
		writeHabit("Map." + mapKey, rec);
	for (auto const& [stem, fp] : profile.MapInfo)
	{
		if (!fp.Valid())
			continue;
		std::fprintf(f, "\n[MapInfo.%s]\nWidth=%d\nHeight=%d\nSpawns=%d\nOreTotal=%lld\n",
			stem.c_str(), fp.Width, fp.Height, fp.Spawns, fp.OreTotal);
	}
	for (auto const& [mapKey, sp] : profile.Spatial)
	{
		std::string const prefix = "Spatial." + mapKey;
		writeGrid(prefix, "Attack", sp.AttackGrid);
		writeGrid(prefix, "Rush", sp.RushGrid);
		writeGrid(prefix, "Build", sp.BuildGrid);
	}
	std::fclose(f);

	profile.Dirty = false;
	Debug::Log("[DossierExt] profile %s saved (%s): seen=%d won=%d lost=%d outcome=%s\n",
		profile.Name.c_str(), path.c_str(), profile.GamesSeen, profile.GamesWon,
		profile.GamesLost, lastOutcome);
	return true;
}

void Profile::CheckpointAll()
{
	for (auto& [idx, profile] : g_byHouse)
		if (profile.Dirty)
			Save(profile, profile.OutcomeRecorded ? "Recorded" : "InProgress");
}
