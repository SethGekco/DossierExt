#include "Dossier/Identity.h"
#include "Dossier/Config.h"
#include "Dossier/Profile.h"

#include <HouseClass.h>
#include <Utilities/Debug.h>

namespace
{
	bool g_resolved = false;
	int g_humans = 0;
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
			Profile::Open(pHouse->PlainName, pHouse->ArrayIndex, pHouse->get_ID());
	}

	// MP policy (DESIGN.md §7): with 2+ humans the persistent dossier must not
	// influence sim. Phase 0 is log-only, so we just record the verdict.
	Debug::Log("[DossierExt] roster resolved: %d house(s), %d human(s)%s\n",
		HouseClass::Array.Count, g_humans,
		g_humans > 1 ? " — MP: dossier actuation would be DISABLED (log-only phase, no effect yet)" : "");
	return true;
}
