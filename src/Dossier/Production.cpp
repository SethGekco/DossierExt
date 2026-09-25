#include "Dossier/Production.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"

#include <HouseClass.h>
#include <BuildingTypeClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <map>

namespace
{
	const char* StructName(int const typeIndex)
	{
		if (typeIndex < 0 || typeIndex >= BuildingTypeClass::Array.Count)
			return "?";
		auto const pType = BuildingTypeClass::Array.GetItem(typeIndex);
		return pType ? pType->get_ID() : "?";
	}
}

void Production::Reset()
{
	// State lives in the Observatory table, cleared by Observatory::Reset().
}

void Production::DiffAfterSurvey()
{
	auto const& cfg = DossierConfig::Instance;
	int const frame = Unsorted::CurrentFrame;

	for (int i = 0; i < HouseClass::Array.Count; ++i)
	{
		auto const pHouse = HouseClass::Array.GetItem(i);
		if (!pHouse || pHouse->IsObserver() || pHouse->IsNeutral())
			continue;
		auto& obs = Observatory::Get(i);

		// First observed snapshot = baseline; don't report the starting base
		// as if every building was just built.
		if (!obs.StructInit)
		{
			obs.StructInit = true;
			if (cfg.DebugTicks)
				Debug::Log("[DossierExt] build-order %s#%d f%d: baseline %u structure type(s)\n",
					pHouse->get_ID(), i, frame, obs.StructCounts.size());
			continue;
		}

		auto const lookup = [](std::map<int, int> const& m, int const k)
		{
			auto const it = m.find(k);
			return it != m.end() ? it->second : 0;
		};

		// Report every type whose count rose since the last sweep — but only
		// the part that was BUILT. A structure that changed hands (captured,
		// spied, engineered) is an acquisition, not an opening-build decision,
		// and letting it through corrupts the fingerprint.
		for (auto const& [typeIdx, count] : obs.StructCounts)
		{
			int const capturedNow = lookup(obs.StructCaptured, typeIdx);
			int const capturedPrev = lookup(obs.PrevStructCaptured, typeIdx);
			int const gained = capturedNow - capturedPrev;
			if (gained > 0)
				Debug::Log("[DossierExt] acquired %s#%d f%d: +%d %s (changed hands, not built)\n",
					pHouse->get_ID(), i, frame, gained, StructName(typeIdx));

			int const prev = lookup(obs.PrevStructCounts, typeIdx);
			int const added = (count - capturedNow) - (prev - capturedPrev);
			if (added > 0)
			{
				Debug::Log("[DossierExt] build-order %s#%d f%d: +%d %s (now %d)\n",
					pHouse->get_ID(), i, frame, added, StructName(typeIdx), count);
				// Phase 2: record the opening build order (capped) for the
				// profile's opening fingerprint.
				if (static_cast<int>(obs.BuildOrder.size()) < cfg.OpeningMaxEvents)
					for (int n = 0; n < added && static_cast<int>(obs.BuildOrder.size()) < cfg.OpeningMaxEvents; ++n)
						obs.BuildOrder.emplace_back(frame, typeIdx);
			}
		}
	}
}
