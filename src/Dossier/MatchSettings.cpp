#include "Dossier/MatchSettings.h"
#include "Dossier/Config.h"

#include <Utilities/Debug.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace
{
	bool g_loaded = false;
	std::vector<std::string> g_keys;
	std::string g_summary;

	// spawn.ini [Settings] as raw strings.
	std::map<std::string, std::string> g_raw;

	void ReadSpawnSettings()
	{
		g_raw.clear();
		FILE* const f = std::fopen("spawn.ini", "r");
		if (!f)
			return;
		char line[512];
		bool inSettings = false;
		while (std::fgets(line, sizeof(line), f))
		{
			char* s = line;
			while (*s == ' ' || *s == '\t') ++s;
			if (*s == '[')
			{
				inSettings = std::strncmp(s, "[Settings]", 10) == 0;
				continue;
			}
			if (!inSettings)
				continue;
			char* const eq = std::strchr(s, '=');
			if (!eq)
				continue;
			*eq = '\0';
			char* v = eq + 1;
			char* e = v + std::strlen(v);
			while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) --e;
			*e = '\0';
			g_raw[s] = v;
		}
		std::fclose(f);
	}

	std::string Get(const char* const key, const char* const fallback = "")
	{
		auto const it = g_raw.find(key);
		return it != g_raw.end() ? it->second : fallback;
	}

	int GetInt(const char* const key, int const fallback = 0)
	{
		auto const it = g_raw.find(key);
		return it != g_raw.end() ? std::atoi(it->second.c_str()) : fallback;
	}

	// spawn.ini uses True/False and Yes/No interchangeably.
	bool GetBool(const char* const key, bool const fallback = false)
	{
		auto const v = Get(key);
		if (v.empty())
			return fallback;
		char const c = static_cast<char>(std::tolower(v[0]));
		return c == 't' || c == 'y' || c == '1';
	}

	void Add(std::string const& dim, std::string const& value)
	{
		g_keys.push_back(dim + "-" + value);
	}

	void AddBool(const char* const dim, const char* const iniKey, bool const fallback = false)
	{
		Add(dim, GetBool(iniKey, fallback) ? "On" : "Off");
	}

	// Raw numbers are too granular to be a habit key — 99k and 100k credits are
	// the same game. Bucket them so records actually accumulate.
	std::string CashBucket(int const credits)
	{
		auto const& cfg = DossierConfig::Instance;
		if (credits <= cfg.CashLow)
			return "Low";
		return credits >= cfg.CashHigh ? "High" : "Normal";
	}

	std::string UnitsBucket(int const count)
	{
		auto const& cfg = DossierConfig::Instance;
		if (count <= 0)
			return "None";
		return count >= cfg.UnitsMany ? "Many" : "Few";
	}

	std::string AIsBucket(int const count)
	{
		if (count <= 0) return "None";
		return count == 1 ? "One" : "Many";
	}
}

void MatchSettings::Reset()
{
	g_loaded = false;
	g_keys.clear();
	g_summary.clear();
	g_raw.clear();
}

std::vector<std::string> const& MatchSettings::Keys() { return g_keys; }
std::string const& MatchSettings::Summary() { return g_summary; }

void MatchSettings::Load()
{
	if (g_loaded)
		return;
	g_loaded = true;
	ReadSpawnSettings();
	if (g_raw.empty())
	{
		Debug::Log("[DossierExt] no spawn.ini [Settings] — match-settings learning inactive.\n");
		return;
	}

	int const credits = GetInt("Credits");
	int const units = GetInt("UnitCount");
	int const ais = GetInt("AIPlayers");

	// The dimensions most likely to change HOW someone plays.
	Add("Cash", CashBucket(credits));
	Add("Units", UnitsBucket(units));
	Add("AIs", AIsBucket(ais));
	AddBool("SW", "Superweapons");
	AddBool("Crates", "Crates", true);
	AddBool("ShortGame", "ShortGame");
	AddBool("Bases", "Bases", true);
	AddBool("FogOfWar", "FogOfWar");
	AddBool("MCVRedeploy", "MCVRedeploy", true);
	AddBool("MultiEngineer", "MultiEngineer");
	AddBool("BuildOffAlly", "BuildOffAlly", true);
	auto const mode = Get("UIGameMode");
	if (!mode.empty())
	{
		std::string safe = mode;
		for (auto& c : safe)
			if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
		Add("Mode", safe);
	}

	char buf[256];
	std::snprintf(buf, sizeof(buf), "Credits=%d UnitCount=%d AIPlayers=%d SW=%d Crates=%d "
		"ShortGame=%d Bases=%d Fog=%d", credits, units, ais,
		GetBool("Superweapons"), GetBool("Crates", true), GetBool("ShortGame"),
		GetBool("Bases", true), GetBool("FogOfWar"));
	g_summary = buf;

	std::string joined;
	for (auto const& k : g_keys)
	{
		if (!joined.empty()) joined += " ";
		joined += k;
	}
	Debug::Log("[DossierExt] match settings: %s\n", g_summary.c_str());
	Debug::Log("[DossierExt] settings context (%u dimension(s)): %s\n",
		g_keys.size(), joined.c_str());
}
