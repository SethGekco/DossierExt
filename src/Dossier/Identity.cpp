#include "Dossier/Identity.h"
#include "Dossier/Config.h"
#include "Dossier/Profile.h"

#include <HouseClass.h>
#include <ScenarioClass.h>
#include <Utilities/Debug.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
	bool g_resolved = false;
	int g_humans = 0;
	std::string g_localName;   // spawn.ini [Settings] Name — the local human
	std::string g_uiMapName;   // spawn.ini [Settings] UIMapName — the REAL map

	// Make a string safe for an INI section name AND free of '.' (the profile
	// parser splits section names on dots).
	void SanitizeKey(std::string& s)
	{
		for (auto& c : s)
		{
			bool const ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
				|| (c >= '0' && c <= '9') || c == '_' || c == '-';
			if (!ok)
				c = '_';
		}
		// Collapse runs of '_' and trim them off the ends, so "[8] Powder Keg"
		// reads back as "Powder_Keg".
		std::string out;
		for (char const c : s)
			if (c != '_' || (!out.empty() && out.back() != '_'))
				out += c;
		while (!out.empty() && out.back() == '_') out.pop_back();
		while (!out.empty() && out.front() == '_') out.erase(out.begin());
		s = out;
	}

	// The launcher rewrites spawn.ini with the real player names at launch; the
	// house PlainName is only the literal "<human player>" placeholder in
	// skirmish. Read [Settings] Name once so the local human gets a real
	// identity. (Reading spawn.ini is safe; only WRITING flags there is futile
	// — [[spawn-ini-rewritten-at-launch]].)
	void LoadSpawnName()
	{
		g_localName.clear();
		g_uiMapName.clear();
		FILE* const f = std::fopen("spawn.ini", "r");
		if (!f)
			return;
		char line[256];
		bool inSettings = false;
		while (std::fgets(line, sizeof(line), f))
		{
			char* s = line;
			while (*s == ' ' || *s == '\t') ++s;
			if (*s == '[')
			{
				inSettings = (std::strncmp(s, "[Settings]", 10) == 0);
				continue;
			}
			if (!inSettings)
				continue;
			auto trimmedValue = [](char* v)
			{
				char* e = v + std::strlen(v);
				while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) --e;
				*e = '\0';
				return v;
			};
			if (std::strncmp(s, "Name=", 5) == 0)
				g_localName = trimmedValue(s + 5);
			else if (std::strncmp(s, "UIMapName=", 10) == 0)
				g_uiMapName = trimmedValue(s + 10);
			if (!g_localName.empty() && !g_uiMapName.empty())
				break;
		}
		std::fclose(f);
	}

	// Map identity. TRAP: in skirmish/CnCNet the launcher copies the chosen map
	// to spawnmap.ini, so ScenarioClass::FileName is the literal "spawnmap.ini"
	// for EVERY game — using it collapses every map into one record. The real
	// name is spawn.ini's UIMapName ("[8] Powder Keg"). Campaign/other scenarios
	// do carry a real FileName, so prefer that when it isn't the spawn stub.
	std::string MapStem()
	{
		std::string fromFile;
		// DEFINE_REFERENCE gives a ScenarioClass* lvalue, not a function.
		if (auto const pScen = ScenarioClass::Instance)
		{
			fromFile = pScen->FileName;
			auto const slash = fromFile.find_last_of("\\/");
			if (slash != std::string::npos)
				fromFile = fromFile.substr(slash + 1);
			auto const dot = fromFile.find_last_of('.');
			if (dot != std::string::npos)
				fromFile = fromFile.substr(0, dot);
			SanitizeKey(fromFile);
		}

		bool const isSpawnStub = fromFile.empty()
			|| _stricmp(fromFile.c_str(), "spawnmap") == 0;

		if (isSpawnStub && !g_uiMapName.empty())
		{
			// Drop a leading "[8] " player-count tag, then sanitize.
			std::string ui = g_uiMapName;
			if (!ui.empty() && ui.front() == '[')
			{
				auto const close = ui.find(']');
				if (close != std::string::npos)
					ui = ui.substr(close + 1);
			}
			SanitizeKey(ui);
			if (!ui.empty())
				return ui;
		}
		return fromFile.empty() ? "UnknownMap" : fromFile;
	}

	// Per-map record key, optionally split by spawn point — "the player's
	// favourite things to do from THIS position on THIS map".
	std::string MapKeyFor(HouseClass* const pHouse)
	{
		auto const& cfg = DossierConfig::Instance;
		std::string key = MapStem();
		if (cfg.RecPerSpawn)
		{
			int const spawn = pHouse->GetSpawnPosition();
			if (spawn >= 0)
				key += "#spawn" + std::to_string(spawn);
		}
		return key;
	}

	// Pick the best available identity for a human house.
	std::string ResolveName(HouseClass* const pHouse)
	{
		char const* const plain = pHouse->PlainName;
		// A real MP lobby name is a normal string; the skirmish placeholder
		// starts with '<'. Prefer a real PlainName when present.
		if (plain && *plain && plain[0] != '<')
			return plain;
		// Local human in skirmish → spawn.ini name.
		if (pHouse == HouseClass::CurrentPlayer && !g_localName.empty())
			return g_localName;
		// Last resort: country + index, so distinct AIs/humans don't collide.
		std::string fallback = pHouse->Type ? pHouse->Type->get_ID() : "Unknown";
		fallback += "_";
		fallback += std::to_string(pHouse->ArrayIndex);
		return fallback;
	}
}

void Identity::Reset()
{
	g_resolved = false;
	g_humans = 0;
}

int Identity::HumanCount()
{
	return g_humans;
}

bool Identity::EnsureRoster()
{
	if (g_resolved)
		return true;
	g_resolved = true;

	auto const& cfg = DossierConfig::Instance;
	g_humans = 0;
	LoadSpawnName();
	Debug::Log("[DossierExt] spawn.ini local name = '%s', UIMapName = '%s' -> map key '%s'\n",
		g_localName.empty() ? "(none)" : g_localName.c_str(),
		g_uiMapName.empty() ? "(none)" : g_uiMapName.c_str(), MapStem().c_str());

	for (int i = 0; i < HouseClass::Array.Count; ++i)
	{
		auto const pHouse = HouseClass::Array.GetItem(i);
		if (!pHouse || !pHouse->Type)
			continue;

		int const diff = static_cast<int>(pHouse->AIDifficulty);
		bool const profiled = pHouse->IsHumanPlayer
			&& !pHouse->IsObserver() && !pHouse->IsNeutral();
		if (profiled)
			++g_humans;

		// One line per house: everything Phase 0 promises to verify, incl.
		// BOTH difficulty-section resolutions (intuitive = what DossierExt.*
		// uses; engine = the inverted multiplier section, for comparison).
		Debug::Log("[DossierExt] house %s#%d name='%s' side=%d human=%d observer=%d neutral=%d defeated=%d "
			"AIDifficulty=%d -> DossierExt keys from [%s] (engine multipliers: [%s]) Shrouded=%d Escalation=%d%s\n",
			pHouse->get_ID(), pHouse->ArrayIndex, pHouse->PlainName,
			pHouse->Type->SideIndex, pHouse->IsHumanPlayer, pHouse->IsObserver(),
			pHouse->IsNeutral(), pHouse->Defeated, diff,
			DossierConfig::IntuitiveSection(diff), DossierConfig::EngineMultiplierSection(diff),
			diff >= 0 && diff < 3 ? cfg.Diff[diff].Shrouded : 0,
			diff >= 0 && diff < 3 ? cfg.Diff[diff].Escalation : 0,
			profiled ? " [PROFILED]" : "");

		if (profiled)
		{
			std::string const name = ResolveName(pHouse);
			std::string const mapKey = MapKeyFor(pHouse);
			auto& prof = Profile::Open(name.c_str(), pHouse->ArrayIndex, pHouse->get_ID());
			prof.CurrentMapKey = mapKey;
			prof.CurrentMapStem = MapStem();
			Debug::Log("[DossierExt] profiling '%s' as %s on %s (spawn=%d)\n",
				name.c_str(), pHouse->get_ID(), mapKey.c_str(), pHouse->GetSpawnPosition());

			// The install-wide record follows the LOCAL human only — remote
			// lobby names are other people and must not pollute it.
			if (pHouse == HouseClass::CurrentPlayer
				&& DossierConfig::Instance.IdentityMode != "NameOnly")
				Profile::OpenGlobal(pHouse->get_ID(), mapKey.c_str());
		}
	}

	// MP policy (DESIGN.md §7): with 2+ humans the persistent dossier must not
	// influence sim. Phase 0 is log-only, so we just record the verdict.
	Debug::Log("[DossierExt] roster resolved: %d house(s), %d human(s)%s\n",
		HouseClass::Array.Count, g_humans,
		g_humans > 1 ? " — MP: dossier actuation would be DISABLED (log-only phase, no effect yet)" : "");
	return true;
}
