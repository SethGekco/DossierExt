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

			if (section == "Meta")
			{
				if (!std::strcmp(key, "GamesSeen")) profile.GamesSeen = std::atoi(value);
				else if (!std::strcmp(key, "GamesWon")) profile.GamesWon = std::atoi(value);
				else if (!std::strcmp(key, "GamesLost")) profile.GamesLost = std::atoi(value);
			}
			else if (!section.compare(0, 8, "Country."))
			{
				auto& rec = profile.Countries[section.substr(8)];
				if (!std::strcmp(key, "Played")) rec.Played = std::atoi(value);
				else if (!std::strcmp(key, "Won")) rec.Won = std::atoi(value);
				else if (!std::strcmp(key, "Lost")) rec.Lost = std::atoi(value);
			}
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
	++profile.Countries[profile.CurrentCountry].Played;
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
	for (auto const& [country, rec] : profile.Countries)
		std::fprintf(f, "\n[Country.%s]\nPlayed=%d\nWon=%d\nLost=%d\n",
			country.c_str(), rec.Played, rec.Won, rec.Lost);
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
