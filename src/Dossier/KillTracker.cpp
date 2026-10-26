#include "Dossier/KillTracker.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"
#include "Dossier/SellWatch.h"

#include <TechnoClass.h>
#include <HouseClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

void KillTracker::Reset()
{
	// State lives in the Observatory table, cleared by Observatory::Reset().
}

// TechnoClass::RegisterDestruction entry — ECX = dying object, [ESP+4] = killer.
// DoctrineExt hooks the same address for its own KillTracker/DeathZones; Syringe
// chains same-address hooks legally. We only READ, so order-independent.
DEFINE_HOOK(0x702D40, DossierExt_RegisterDestruction_Aggression, 0x5)
{
	GET(TechnoClass* const, pVictim, ECX);
	GET_STACK(TechnoClass* const, pKiller, 0x4);

	SellWatch::OnDestroyed(pVictim, pKiller);

	int const frame = Unsorted::CurrentFrame;

	auto const& cfg = DossierConfig::Instance;

	if (pKiller && pKiller->Owner)
	{
		auto& k = Observatory::Get(pKiller->Owner->ArrayIndex);
		++k.KillsDealt;
		if (k.FirstKillFrame < 0)
		{
			k.FirstKillFrame = frame;
			if (cfg.DebugTicks)
				Debug::Log("[DossierExt] first blood: %s#%d at f%d\n",
					pKiller->Owner->get_ID(), pKiller->Owner->ArrayIndex, frame);
		}

		// WHO they fight: attribute the kill to the victim's country, so a
		// target preference becomes measurable.
		if (pVictim && pVictim->Owner
			&& !pKiller->Owner->IsAlliedWith(pVictim->Owner)
			&& !pVictim->Owner->IsNeutral())
		{
			if (auto const pCountry = pVictim->Owner->get_ID())
			{
				++k.KillsByCountry[pCountry];
				++k.HostileKills;
			}
		}

		// WHERE they fight: bucket the victim's cell. Only count kills on
		// someone they're not allied with, so friendly-fire//civilian noise
		// doesn't paint a false attack route.
		if (cfg.RecSpatial && pVictim && pVictim->Owner
			&& !pKiller->Owner->IsAlliedWith(pVictim->Owner))
		{
			auto const cell = pVictim->GetMapCoords();
			auto const key = Observatory::BucketKey(cell.X, cell.Y);
			++k.AttackGrid[key];
			if (frame <= cfg.RushWindow)
				++k.RushGrid[key]; // early aggression = their rush path
		}
	}

	if (pVictim && pVictim->Owner)
	{
		auto& v = Observatory::Get(pVictim->Owner->ArrayIndex);
		++v.LossesTaken;
		// Weakness ledger: what type did the killing. Hostile kills only, so
		// friendly fire and civilian accidents don't read as a counter.
		if (pKiller && pKiller->Owner
			&& !pKiller->Owner->IsAlliedWith(pVictim->Owner))
		{
			if (auto const pType = pKiller->GetTechnoType())
				if (auto const pId = pType->get_ID())
				{
					++v.LostToType[pId];
					++v.HostileLosses;
				}
		}
	}

	return 0;
}
