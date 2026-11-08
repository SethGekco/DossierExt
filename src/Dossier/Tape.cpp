#include "Dossier/Tape.h"
#include "Dossier/Config.h"

#include <EventClass.h>
#include <HouseClass.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <BuildingTypeClass.h>
#include <UnitTypeClass.h>
#include <InfantryTypeClass.h>
#include <AircraftTypeClass.h>
#include <CellClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

#include <cstdio>
#include <map>
#include <string>

namespace
{
	int g_logged = 0;
	std::map<int, std::map<int, int>> g_counts;   // house -> event type -> orders
	bool g_changed = false;

	const char* MissionName(int const m)
	{
		static const char* const names[] = {
			"Sleep", "Attack", "Move", "QMove", "Retreat", "Guard", "Sticky", "Enter",
			"Capture", "Eaten", "Harvest", "AreaGuard", "Return", "Stop", "Ambush", "Hunt",
			"Unload", "Sabotage", "Construction", "Selling", "Repair", "Rescue", "Missile",
			"Harmless", "Open", "Patrol", "ParadropApproach", "ParadropOverfly", "Wait",
			"AttackMove", "SpyplaneApproach", "SpyplaneOverfly" };
		return m >= 0 && m < static_cast<int>(sizeof(names) / sizeof(names[0])) ? names[m] : "?";
	}

	const char* EventName(int const type)
	{
		return type >= 0 && type < static_cast<int>(EventType::LAST_EVENT)
			? EventClass::EventNames[type] : "?";
	}

	// Player decisions only. FrameInfo/Timing/ProcessTime and friends are
	// network chatter that would bury the orders.
	bool IsOrder(EventType const type)
	{
		switch (type)
		{
		case EventType::MegaMission:
		case EventType::MegaMissionF:
		case EventType::Idle:
		case EventType::Scatter:
		case EventType::Deploy:
		case EventType::Place:
		case EventType::Produce:
		case EventType::Suspend:
		case EventType::Abandon:
		case EventType::Primary:
		case EventType::SpecialPlace:
		case EventType::Repair:
		case EventType::Sell:
		case EventType::SellCell:
		case EventType::PowerOn:
		case EventType::PowerOff:
			return true;
		default:
			return false;
		}
	}

	// Bounds-checked type lookup. The engine's GetByTypeAndIndex (0x48DCD0)
	// indexes the type array blindly, and a PLACE can carry HeapID -1:
	// HouseClass::Place (0x4FB0E0) guards `heap < 0` itself because it takes
	// the building from the house's factory, not from the event. Trusting the
	// engine helper read array[-1], printed a garbage name and crashed Rex's
	// game (2026-11-08, a placement right after queuing dogs).
	TechnoTypeClass* TypeOf(AbstractType const rtti, int const heap)
	{
		switch (rtti)
		{
		case AbstractType::Unit:
		case AbstractType::UnitType:
			return UnitTypeClass::Array.GetItemOrDefault(heap);
		case AbstractType::Aircraft:
		case AbstractType::AircraftType:
			return AircraftTypeClass::Array.GetItemOrDefault(heap);
		case AbstractType::Building:
		case AbstractType::BuildingType:
			return BuildingTypeClass::Array.GetItemOrDefault(heap);
		case AbstractType::Infantry:
		case AbstractType::InfantryType:
			return InfantryTypeClass::Array.GetItemOrDefault(heap);
		default:
			return nullptr;
		}
	}

	// "HTNK#1234(foe)" for an object, "cell 112,87" for a cell, "-" for nothing.
	std::string Describe(TargetClass target, HouseClass* const pOrderer, TechnoClass*& pTechno)
	{
		pTechno = nullptr;
		if (!target.m_RTTI)
			return "-";
		char buf[64];
		if (target.m_RTTI == static_cast<unsigned char>(AbstractType::Cell))
		{
			std::snprintf(buf, sizeof(buf), "cell %d,%d", target.m_ID % 1000, target.m_ID / 1000);
			return buf;
		}
		pTechno = target.As_Techno();
		if (pTechno)
		{
			auto const pType = pTechno->GetTechnoType();
			auto const pOwner = pTechno->Owner;
			char const* const side = !pOwner || pOwner == pOrderer ? ""
				: pOrderer->IsAlliedWith(pOwner) ? "(ally)" : "(foe)";
			std::snprintf(buf, sizeof(buf), "%s#%u%s", pType ? pType->get_ID() : "?",
				pTechno->UniqueID, side);
			return buf;
		}
		return "obj";
	}

	// The situation that makes an order a case: how far, who outranges whom,
	// how hurt, and whether the gun is reloading (the kiting window).
	void LogMegaMission(HouseClass* const pHouse, EventClass* const pEvent, int const mission,
		TargetClass const whom, TargetClass const target, TargetClass const dest)
	{
		TechnoClass* pUnit = nullptr;
		TechnoClass* pTarget = nullptr;
		TechnoClass* pIgnored = nullptr;
		auto const unit = Describe(whom, pHouse, pUnit);
		auto const tgt = Describe(target, pHouse, pTarget);
		auto const dst = Describe(dest, pHouse, pIgnored);

		char ctx[160] = "";
		if (pUnit)
		{
			auto const pType = pUnit->GetTechnoType();
			int const hp = pType && pType->Strength > 0 ? pUnit->Health * 100 / pType->Strength : -1;
			int const rearm = pUnit->RearmTimer.GetTimeLeft();
			int const ownRange = pUnit->GetWeaponRange(0) / 256;
			if (pTarget)
			{
				int const dist = static_cast<int>(pUnit->GetCoords().DistanceFrom(pTarget->GetCoords()) / 256);
				std::snprintf(ctx, sizeof(ctx), " | dist %d range %d vs %d hp %d%% rearm %d",
					dist, ownRange, pTarget->GetWeaponRange(0) / 256, hp, rearm);
			}
			else
			{
				std::snprintf(ctx, sizeof(ctx), " | range %d hp %d%% rearm %d", ownRange, hp, rearm);
			}
		}
		Debug::Log("[DossierExt] tape %s#%d f%u: %s %s tgt=%s dest=%s%s\n",
			pHouse->get_ID(), pHouse->ArrayIndex, pEvent->Frame, unit.c_str(),
			MissionName(mission), tgt.c_str(), dst.c_str(), ctx);
	}

	void LogOrder(HouseClass* const pHouse, EventClass* const pEvent)
	{
		switch (pEvent->Type)
		{
		case EventType::MegaMission:
		{
			auto const& mm = pEvent->MegaMission;
			LogMegaMission(pHouse, pEvent, mm.Mission, mm.Whom, mm.Target, mm.Destination);
			return;
		}
		case EventType::MegaMissionF:
		{
			auto const& mm = pEvent->MegaMissionF;
			LogMegaMission(pHouse, pEvent, mm.Mission, mm.Whom, mm.Target, mm.Destination);
			return;
		}
		case EventType::Produce:
		case EventType::Place:
		{
			AbstractType const rtti = pEvent->Type == EventType::Produce
				? pEvent->Produce.RTTIType : pEvent->Place.RTTIType;
			int const heap = pEvent->Type == EventType::Produce
				? pEvent->Produce.HeapID : pEvent->Place.HeapID;
			auto const pType = TypeOf(rtti, heap);
			char what[48];
			if (pType)
				std::snprintf(what, sizeof(what), "%s", pType->get_ID());
			else
				std::snprintf(what, sizeof(what), "?(rtti %d heap %d)", static_cast<int>(rtti), heap);
			char where[32] = "";
			if (pEvent->Type == EventType::Place)
				std::snprintf(where, sizeof(where), " at %d,%d",
					pEvent->Place.Location.X, pEvent->Place.Location.Y);
			Debug::Log("[DossierExt] tape %s#%d f%u: %s %s%s\n",
				pHouse->get_ID(), pHouse->ArrayIndex, pEvent->Frame, EventName(static_cast<int>(pEvent->Type)),
				what, where);
			return;
		}
		default:
			Debug::Log("[DossierExt] tape %s#%d f%u: %s\n",
				pHouse->get_ID(), pHouse->ArrayIndex, pEvent->Frame, EventName(static_cast<int>(pEvent->Type)));
			return;
		}
	}
}

void Tape::Reset()
{
	g_logged = 0;
	g_counts.clear();
	g_changed = false;
}

void Tape::Summary()
{
	if (!g_changed)
		return;
	g_changed = false;
	for (auto const& [houseIndex, counts] : g_counts)
	{
		auto const pHouse = HouseClass::Array.GetItemOrDefault(houseIndex);
		std::string line;
		int total = 0;
		for (auto const& [type, n] : counts)
		{
			char item[48];
			std::snprintf(item, sizeof(item), "%s%s=%d", line.empty() ? "" : " ", EventName(type), n);
			line += item;
			total += n;
		}
		Debug::Log("[DossierExt] tape %s#%d f%d: %d orders so far: %s\n",
			pHouse ? pHouse->get_ID() : "?", houseIndex, Unsorted::CurrentFrame, total, line.c_str());
	}
}

// EventClass::RespondToEvent entry: `sub esp,0x370` (6 bytes, no rel32). ECX =
// the event about to execute. Nobody else hooks the entry (CnCNet-Spawner,
// Phobos and AggressiveStance chain at 0x4C6CC8, Ares/Antares at 0x4C6CCD,
// Phobos at 0x4C6CF0). Read-only: we look before the engine acts.
DEFINE_HOOK(0x4C6CB0, DossierExt_EventClass_RespondToEvent_Tape, 0x6)
{
	GET(EventClass* const, pEvent, ECX);

	auto const& cfg = DossierConfig::Instance;
	if (!pEvent || !cfg.Parsed || !cfg.Enabled || !cfg.TapeEnabled || !IsOrder(pEvent->Type))
		return 0;
	auto const pHouse = HouseClass::Array.GetItemOrDefault(pEvent->HouseIndex);
	if (!pHouse || !pHouse->IsControlledByHuman())
		return 0;

	++g_counts[pHouse->ArrayIndex][static_cast<int>(pEvent->Type)];
	g_changed = true;
	if (g_logged < cfg.TapeLogLines)
	{
		++g_logged;
		LogOrder(pHouse, pEvent);
	}
	return 0;
}
