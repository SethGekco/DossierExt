#pragma once

class HouseClass;
class TechnoClass;

// Diagnostic: when an AI house liquidates its base, record WHO ordered it and
// what the house looked like at that moment. Written for Rex's Easy AIs that
// fire-sold ~2 minutes in (2026-10-25) with no DLL owning up to it — the log
// could not say, because the ConYard leaves invisibly (PrerequisiteExt's SOLD
// line skips UndeploysInto types) and nothing logs a Fire_Sale call.
//
// Static analysis of gamemd found three live roads into HouseClass::Fire_Sale:
// the AIMode=SellAll tick (script action 30 / trigger action), the last-legs
// check (no live factory building; Phobos [General]AIFireSale), and DLLs. None
// should have been open in that game, so this watches the real call. Log-only.
namespace SellWatch
{
	void Reset();

	// Per AI house: log AIMode / ConYard-count changes and each building that
	// starts selling, with the engine's own last-legs factory census.
	void Tick(HouseClass* pHouse);

	// From the RegisterDestruction seat: a ConYard dying in combat (selling
	// never passes through there, so this separates "killed" from "sold").
	void OnDestroyed(TechnoClass* pVictim, TechnoClass* pKiller);
}
