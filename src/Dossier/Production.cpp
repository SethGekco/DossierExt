#include "Dossier/Production.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"

#include <HouseClass.h>
#include <BuildingTypeClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

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

		// Report every type whose count rose since the last sweep.
		for (auto const& [typeIdx, count] : obs.StructCounts)
		{
			auto const prevIt = obs.PrevStructCounts.find(typeIdx);
			int const prev = prevIt != obs.PrevStructCounts.end() ? prevIt->second : 0;
			int const added = count - prev;
			if (added > 0)
				Debug::Log("[DossierExt] build-order %s#%d f%d: +%d %s (now %d)\n",
					pHouse->get_ID(), i, frame, added, StructName(typeIdx), count);
		}
	}
}
