#include "Dossier/Survey.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"
#include "Dossier/Production.h"

#include <HouseClass.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <MapClass.h>
#include <CellClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <vector>

namespace
{
	int g_lastSurveyFrame = -1;

	struct OreCell { CellStruct Loc; int Value; };

	// Sweep the map once, collecting every cell that currently holds ore.
	void CollectOre(std::vector<OreCell>& out)
	{
		auto const& map = MapClass::Instance;
		auto const& b = map.MapCoordBounds; // Left/Top/Right/Bottom in cells
		for (int y = b.Top; y <= b.Bottom; ++y)
		{
			for (int x = b.Left; x <= b.Right; ++x)
			{
				CellStruct cs{ static_cast<short>(x), static_cast<short>(y) };
				auto const pCell = map.TryGetCellAt(cs);
				if (!pCell)
					continue;
				int const val = pCell->GetContainedTiberiumValue();
				if (val > 0)
					out.push_back({ cs, val });
			}
		}
	}
}

void Survey::Reset()
{
	g_lastSurveyFrame = -1;
}

void Survey::MaybeRun()
{
	auto const& cfg = DossierConfig::Instance;
	if (cfg.SurveyPeriod <= 0)
		return;

	int const frame = Unsorted::CurrentFrame;
	if (g_lastSurveyFrame >= 0 && frame - g_lastSurveyFrame < cfg.SurveyPeriod)
		return;
	g_lastSurveyFrame = frame;

	// ── Reset the survey half of every house's observation, keeping the
	//    structure snapshot so Production can diff against last sweep ──────
	for (int i = 0; i < HouseClass::Array.Count; ++i)
	{
		auto const pHouse = HouseClass::Array.GetItem(i);
		if (!pHouse)
			continue;
		auto& obs = Observatory::Get(i);
		obs.ArmyValue = 0;
		obs.BuildingValue = 0;
		obs.TechBuildingsOwned = 0;
		obs.OreNearest = -1;
		obs.OreReachable = 0;
		obs.PrevStructCounts = obs.StructCounts;
		obs.StructCounts.clear();
	}

	// ── One pass over every techno: army value, building value, tech
	//    buildings, per-type structure counts ──────────────────────────────
	for (auto const pTechno : TechnoClass::Array)
	{
		if (!pTechno || !pTechno->IsAlive || pTechno->InLimbo)
			continue;
		auto const pOwner = pTechno->Owner;
		if (!pOwner)
			continue;
		auto const pType = pTechno->GetTechnoType(); // virtual dispatch -> real
		if (!pType)
			continue;

		auto& obs = Observatory::Get(pOwner->ArrayIndex);
		int const cost = pType->GetCost();

		if (pTechno->WhatAmI() == AbstractType::Building)
		{
			obs.BuildingValue += cost;
			auto const pBld = static_cast<BuildingClass*>(pTechno);
			if (pBld->Type)
			{
				++obs.StructCounts[pBld->Type->ArrayIndex];
				if (pBld->Type->Capturable)
					++obs.TechBuildingsOwned;
			}
		}
		else
		{
			obs.ArmyValue += cost;
		}
	}

	// ── Ore survey: collect ore cells once, then per-house nearest/reachable
	std::vector<OreCell> ore;
	CollectOre(ore);
	long long oreTotal = 0;
	for (auto const& oc : ore)
		oreTotal += oc.Value;

	int const reach = cfg.OreReachRadius;
	for (int i = 0; i < HouseClass::Array.Count; ++i)
	{
		auto const pHouse = HouseClass::Array.GetItem(i);
		if (!pHouse || pHouse->IsObserver() || pHouse->IsNeutral() || pHouse->Defeated)
			continue;
		auto const& base = pHouse->GetBaseCenter();
		if (base.X == 0 && base.Y == 0)
			continue;

		auto& obs = Observatory::Get(i);
		double nearest = 1e9;
		for (auto const& oc : ore)
		{
			double const d = base.DistanceFrom(oc.Loc); // in cells
			if (d < nearest)
				nearest = d;
			if (d <= reach)
				obs.OreReachable += oc.Value;
		}
		obs.OreNearest = (nearest < 1e9) ? static_cast<int>(nearest) : -1;
		obs.SurveyInit = true;

		Debug::Log("[DossierExt] survey %s#%d f%d: army=%lld bld=%lld techBldgs=%d oreNearest=%dcell oreReach=%lld(r%d) structs=%u\n",
			pHouse->get_ID(), i, frame, obs.ArmyValue, obs.BuildingValue,
			obs.TechBuildingsOwned, obs.OreNearest, obs.OreReachable, reach,
			obs.StructCounts.size());
	}

	if (cfg.DebugTicks)
		Debug::Log("[DossierExt] survey f%d: %u ore cells, %lld total ore value on map\n",
			frame, ore.size(), oreTotal);

	// Structure build-order diff runs off the fresh snapshot.
	Production::DiffAfterSurvey();
}
