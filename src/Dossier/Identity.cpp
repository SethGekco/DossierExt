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

	// The launcher rewrites spawn.ini with the real player names at launch; the
	// house PlainName is only the literal "<human player>" placeholder in
	// skirmish. Read [Settings] Name once so the local human gets a real
	// identity. (Reading spawn.ini is safe; only WRITING flags there is futile
	// — [[spawn-ini-rewritten-at-launch]].)
	void LoadSpawnName()
	{
		g_localName.clear();
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
			if (std::strncmp(s, "Name=", 5) == 0)
			{
				char* v = s + 5;
				char* e = v + std::strlen(v);
				while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) --e;
				*e = '\0';
				g_localName = v;
				break;
			}
		}
		std::fclose(f);
	}

	// Map identity: the scenario file name without path/extension, so
	// "Powder Keg.map" and a rehost of it share a record.
	std::string MapStem()
	{
		// DEFINE_REFERENCE gives a ScenarioClass* lvalue, not a function.
		auto const pScen = ScenarioClass::Instance;
		if (!pScen)
			return "UnknownMap";
		std::string s = pScen->FileName;
		auto const slash = s.find_last_of("\\/");
		if (slash != std::string::npos)
			s = s.substr(slash + 1);
		auto const dot = s.find_last_of('.');
		if (dot != std::string::npos)
			s = s.substr(0, dot);
		if (s.empty())
			return "UnknownMap";
		// Keep it INI-section-safe AND dot-free: the profile parser splits
		// section names on '.', so a "map.v2.map" stem must not keep its dot.
		for (auto& c : s)
		{
			bool const ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
				|| (c >= '0' && c <= '9') || c == '_' || c == '-';
			if (!ok)
				c = '_';
		}
		return s;
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
	Debug::Log("[DossierExt] spawn.ini local name = '%s'\n",
		g_localName.empty() ? "(none)" : g_localName.c_str());

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
