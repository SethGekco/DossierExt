#include "Dossier/Survey.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"
#include "Dossier/Production.h"
#include "Dossier/Profile.h"

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
	MapFingerprint g_fingerprint;

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
	g_fingerprint = MapFingerprint{};
}

MapFingerprint const& Survey::Fingerprint()
{
	return g_fingerprint;
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
		obs.TechListed = obs.TechCivilian = obs.TechCaptured = 0;
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

				// What counts as a tech building is MAP DESIGN, not a rules
				// technicality. Three signals, any one qualifies:
				//  1. declared in [AI] NeutralTechBuildings (+ our extras)
				//  2. civilian-shaped: buildable by nobody, ownable by anyone
				//     (TechLevel<0 && Capturable) — catches unlisted ones
				//  3. it was TAKEN — a fan map may offer a capturable ConYard
				//     or any faction structure; if this house didn't build it,
				//     it's a captured asset whatever its type.
				// NB Capturable ALONE means nothing: vanilla sets it on
				// ordinary buildings (GAPILE/GAREFN/GAPOWR), which is what
				// previously made this count the owner's entire base.
				auto const pId = pBld->Type->get_ID();
				bool const listed = pId && cfg.NeutralTechBuildings.count(pId) > 0;
				bool const civilian = pBld->Type->Capturable && pBld->Type->TechLevel < 0;
				bool const taken = cfg.CapturedCountsAsTech && pBld->HasBeenCaptured;
				if (listed) ++obs.TechListed;
				else if (civilian) ++obs.TechCivilian;
				else if (taken) ++obs.TechCaptured;
				if (listed || civilian || taken)
					++obs.TechBuildingsOwned;
			}
			// WHERE they hold ground: time-weighted building presence (a
			// structure standing for many surveys counts many times, so the
			// hot buckets are the places they really commit to).
			if (cfg.RecSpatial)
			{
				auto const cell = pTechno->GetMapCoords();
				++obs.BuildGrid[Observatory::BucketKey(cell.X, cell.Y)];
			}
		}
		else
		{
			obs.ArmyValue += cost;
			// Phase 2: time-weighted unit-mix histogram (this survey's snapshot
			// counts once; accumulated across the game).
			if (auto const pId = pType->get_ID())
			{
				++obs.UnitMix[pId];
				++obs.UnitMixSamples;
			}
		}
	}

	// Phase 2: peak army/building value over the game.
	for (int i = 0; i < HouseClass::Array.Count; ++i)
	{
		auto& obs = Observatory::Get(i);
		if (obs.ArmyValue > obs.PeakArmy) obs.PeakArmy = obs.ArmyValue;
		if (obs.BuildingValue > obs.PeakBuilding) obs.PeakBuilding = obs.BuildingValue;
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

		Debug::Log("[DossierExt] survey %s#%d f%d: army=%lld bld=%lld techBldgs=%d(listed=%d civ=%d taken=%d) oreNearest=%dcell oreReach=%lld(r%d) structs=%u\n",
			pHouse->get_ID(), i, frame, obs.ArmyValue, obs.BuildingValue,
			obs.TechBuildingsOwned, obs.TechListed, obs.TechCivilian, obs.TechCaptured,
			obs.OreNearest, obs.OreReachable, reach,
			obs.StructCounts.size());
	}

	if (cfg.DebugTicks)
		Debug::Log("[DossierExt] survey f%d: %u ore cells, %lld total ore value on map\n",
			frame, ore.size(), oreTotal);

	// Map fingerprint, taken once (starting ore is the meaningful measure, so
	// the first sweep wins — later sweeps see a depleted map).
	if (!g_fingerprint.Valid())
	{
		auto const& b = MapClass::Instance.MapCoordBounds;
		g_fingerprint.Width = b.Right - b.Left + 1;
		g_fingerprint.Height = b.Bottom - b.Top + 1;
		g_fingerprint.OreTotal = oreTotal;
		int spawns = 0;
		for (int i = 0; i < HouseClass::Array.Count; ++i)
		{
			auto const pH = HouseClass::Array.GetItem(i);
			if (pH && !pH->IsObserver() && !pH->IsNeutral() && pH->GetSpawnPosition() >= 0)
				++spawns;
		}
		g_fingerprint.Spawns = spawns;
		Debug::Log("[DossierExt] map fingerprint: %dx%d cells, %d spawns, %lld starting ore\n",
			g_fingerprint.Width, g_fingerprint.Height, g_fingerprint.Spawns, g_fingerprint.OreTotal);
	}

	// Structure build-order diff runs off the fresh snapshot.
	Production::DiffAfterSurvey();
}
