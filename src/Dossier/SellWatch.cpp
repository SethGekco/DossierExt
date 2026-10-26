#include "Dossier/SellWatch.h"
#include "Dossier/Config.h"

#include <HouseClass.h>
#include <TechnoClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

#include <windows.h>
#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace
{
	// Read at the offsets gamemd's AI update itself uses (0x4FD7A0-0x4FD848)
	// rather than trusting header layout, since this is the thing in question:
	//   +0x250  AIMode: 0 General, 1 LowOnCash (<25 credits), 3 = the 900-frame
	//           window after the +0x54D8 stamp (last-legs check skipped),
	//           4 SellAll (Fire_Sale + All_To_Hunt every AI tick).
	//   +0x54D8 the frame stamp that opens that 900-frame window.
	constexpr int kAIModeOffset = 0x250;
	constexpr int kStampOffset = 0x54D8;

	constexpr int kMaxStateLines = 40;     // AIMode / ConYard changes, per house
	constexpr int kMaxSaleLines = 60;      // sell starts, per house
	constexpr int kMaxFireSaleLines = 12;  // Fire_Sale calls, per house
	constexpr int kFireSaleRepeatFrames = 900;

	struct HouseWatch
	{
		int AIMode = -99;
		int ConYards = -1;
		std::set<BuildingClass const*> Selling;
		int StateLines = 0;
		int SaleLines = 0;
		int FireSaleLines = 0;
		DWORD LastCaller = 0;
		int LastFireSaleFrame = -1000000;
	};

	std::map<int, HouseWatch> g_watch;

	int RawInt(HouseClass const* const pHouse, int const offset)
	{
		return *reinterpret_cast<int const*>(reinterpret_cast<char const*>(pHouse) + offset);
	}

	bool Watched(HouseClass* const pHouse)
	{
		auto const& cfg = DossierConfig::Instance;
		return cfg.Parsed && cfg.Enabled && cfg.SellWatch && pHouse
			&& !pHouse->IsHumanPlayer && !pHouse->IsNeutral() && !pHouse->IsObserver();
	}

	// The engine's own last-legs test (0x4FD879-0x4FD8C5): a building keeps the
	// house off the fire sale if it is alive, out of limbo, and its type has
	// Factory= set. Names are listed so a surprise zero is explainable.
	std::string FactoryCensus(HouseClass* const pHouse, int& count)
	{
		count = 0;
		std::string names;
		for (auto const pBld : pHouse->Buildings)
		{
			if (!pBld || !pBld->IsAlive || pBld->InLimbo || !pBld->Type)
				continue;
			if (pBld->Type->Factory == AbstractType::None)
				continue;
			++count;
			if (names.size() < 120)
			{
				if (!names.empty())
					names += ',';
				names += pBld->Type->ID;
				if (pBld->CurrentMission == Mission::Selling)
					names += "(selling)";
			}
		}
		return names;
	}

	// One line of house state, shared by every event so they read the same.
	std::string StateOf(HouseClass* const pHouse)
	{
		int factories = 0;
		auto const census = FactoryCensus(pHouse, factories);
		int const stamp = RawInt(pHouse, kStampOffset);
		char buf[320];
		std::snprintf(buf, sizeof(buf),
			"AIMode=%d money=%d conyards=%d buildings=%d stamp54D8=f%d(%+d) factories=%d[%s]",
			RawInt(pHouse, kAIModeOffset), static_cast<int>(pHouse->Available_Money()),
			pHouse->ConYards.Count, pHouse->Buildings.Count,
			stamp, Unsorted::CurrentFrame - stamp, factories, census.c_str());
		return buf;
	}

	// gamemd's own two Fire_Sale call sites (vtable calls at 0x4FD7B7 and
	// 0x4FD8FE). Phobos replaces the second with its AIFireSale hook, so a
	// last-legs sale normally shows up as called from Phobos.dll instead.
	const char* KnownCaller(DWORD const ret)
	{
		switch (ret)
		{
		case 0x4FD7BA: return " = engine AIMode=SellAll tick (script action 30 / trigger action)";
		case 0x4FD901: return " = engine last-legs check (no live factory building)";
		default: return "";
		}
	}

	const char* ModuleOf(DWORD const addr, DWORD& rva)
	{
		static char path[MAX_PATH];
		HMODULE hMod = nullptr;
		rva = addr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
				| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(addr), &hMod) || !hMod)
			return "?";
		if (!GetModuleFileNameA(hMod, path, MAX_PATH))
			return "?";
		rva = addr - reinterpret_cast<DWORD>(hMod);
		const char* base = path;
		for (const char* p = path; *p; ++p)
			if (*p == '\\' || *p == '/')
				base = p + 1;
		return base;
	}
}

void SellWatch::Reset()
{
	g_watch.clear();
}

void SellWatch::Tick(HouseClass* const pHouse)
{
	if (!Watched(pHouse))
		return;

	auto& w = g_watch[pHouse->ArrayIndex];
	int const frame = Unsorted::CurrentFrame;
	int const mode = RawInt(pHouse, kAIModeOffset);
	int const conYards = pHouse->ConYards.Count;

	if ((mode != w.AIMode || conYards != w.ConYards) && w.StateLines < kMaxStateLines)
	{
		++w.StateLines;
		Debug::Log("[DossierExt] sellwatch %s#%d f%d: AIMode %d->%d ConYards %d->%d | %s\n",
			pHouse->get_ID(), pHouse->ArrayIndex, frame, w.AIMode, mode,
			w.ConYards, conYards, StateOf(pHouse).c_str());
	}
	w.AIMode = mode;
	w.ConYards = conYards;

	// Sell starts: diff this house's buildings in Mission::Selling against
	// the last tick. Rebuilding the set each tick also forgets torn-down ones.
	std::set<BuildingClass const*> selling;
	for (auto const pBld : pHouse->Buildings)
	{
		if (!pBld || !pBld->IsAlive || pBld->CurrentMission != Mission::Selling)
			continue;
		selling.insert(pBld);
		if (w.Selling.count(pBld) || w.SaleLines >= kMaxSaleLines)
			continue;
		++w.SaleLines;
		Debug::Log("[DossierExt] sellwatch %s#%d f%d: SELL START %s hp=%d/%d | %s\n",
			pHouse->get_ID(), pHouse->ArrayIndex, frame,
			pBld->Type ? pBld->Type->ID : "?", pBld->Health,
			pBld->Type ? pBld->Type->Strength : 0, StateOf(pHouse).c_str());
	}
	w.Selling.swap(selling);
}

void SellWatch::OnDestroyed(TechnoClass* const pVictim, TechnoClass* const pKiller)
{
	if (!pVictim || pVictim->WhatAmI() != AbstractType::Building)
		return;
	auto const pBld = static_cast<BuildingClass*>(pVictim);
	if (!pBld->Type || !pBld->Type->ConstructionYard || !Watched(pBld->Owner))
		return;

	auto const pKillerType = pKiller ? pKiller->GetTechnoType() : nullptr;
	Debug::Log("[DossierExt] sellwatch %s#%d f%d: CONYARD DESTROYED %s by %s (%s#%d)\n",
		pBld->Owner->get_ID(), pBld->Owner->ArrayIndex, Unsorted::CurrentFrame,
		pBld->Type->ID, pKillerType ? pKillerType->ID : "nobody",
		pKiller && pKiller->Owner ? pKiller->Owner->get_ID() : "-",
		pKiller && pKiller->Owner ? pKiller->Owner->ArrayIndex : -1);
}

// HouseClass::Fire_Sale — IHouse vtable +0x34 -> 0x5013A0, __stdcall with
// [ESP+4] = IHouse*. It sells every non-limbo building with health > 0 in one
// pass (ConYard included), and every road into a base liquidation ends here:
// gamemd's SellAll tick, its last-legs check (via Phobos's AIFireSale hook),
// and any DLL. So the return address names the culprit. Read-only; the site
// is `push edi` + `mov edi,[esp+8]` = exactly 5 bytes, no relative operands.
DEFINE_HOOK(0x5013A0, DossierExt_HouseClass_FireSale_Watch, 0x5)
{
	GET_STACK(DWORD const, retAddr, 0x0);
	GET_STACK(IHouse* const, pIHouse, 0x4);

	if (!pIHouse)
		return 0;
	auto const pHouse = static_cast<HouseClass*>(pIHouse);
	if (!Watched(pHouse))
		return 0;

	// SellAll mode calls this every AI tick, so log the first call, any change
	// of caller, and a reminder every kFireSaleRepeatFrames — within a budget.
	auto& w = g_watch[pHouse->ArrayIndex];
	int const frame = Unsorted::CurrentFrame;
	bool const fresh = retAddr != w.LastCaller
		|| frame - w.LastFireSaleFrame >= kFireSaleRepeatFrames;
	if (!fresh || w.FireSaleLines >= kMaxFireSaleLines)
		return 0;
	++w.FireSaleLines;
	w.LastCaller = retAddr;
	w.LastFireSaleFrame = frame;

	DWORD rva = 0;
	auto const module = ModuleOf(retAddr, rva);
	Debug::Log("[DossierExt] FIRESALE %s#%d f%d: called from %s+0x%X (%08X)%s | %s\n",
		pHouse->get_ID(), pHouse->ArrayIndex, frame, module, rva, retAddr,
		KnownCaller(retAddr), StateOf(pHouse).c_str());
	return 0;
}
