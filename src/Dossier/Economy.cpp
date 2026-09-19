#include "Dossier/Economy.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"

#include <HouseClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <map>

namespace
{
	std::map<int, int> g_lastSampleFrame; // house index -> frame of last sample
}

void Economy::Reset()
{
	g_lastSampleFrame.clear();
}

bool Economy::Sample(HouseClass* const pHouse)
{
	auto const& cfg = DossierConfig::Instance;
	int const window = cfg.EconWindow;
	if (window <= 0)
		return false;

	int const frame = Unsorted::CurrentFrame;
	int const idx = pHouse->ArrayIndex;

	auto const it = g_lastSampleFrame.find(idx);
	if (it != g_lastSampleFrame.end() && frame - it->second < window)
		return false;
	bool const first = (it == g_lastSampleFrame.end());
	g_lastSampleFrame[idx] = frame;

	auto& obs = Observatory::Get(idx);
	int const harvested = pHouse->HarvestedCredits;
	int const spent = pHouse->CreditsSpent;
	int const money = static_cast<int>(pHouse->Available_Money());

	if (first || !obs.EconInit)
	{
		// First sample only establishes the baseline; no rates yet.
		obs.LastHarvested = harvested;
		obs.LastSpent = spent;
		obs.FloatNow = money;
		obs.PrevFloat = money;
		obs.IncomeRate = 0;
		obs.SpendRate = 0;
		obs.FloatTrend = 0;
		obs.EconInit = true;
		return false;
	}

	obs.SpendRate = spent - obs.LastSpent;          // credits spent this window
	obs.PrevFloat = obs.FloatNow;
	obs.FloatNow = money;
	obs.FloatTrend = obs.FloatNow - obs.PrevFloat;

	// HarvestedCredits reads constant in this stack (verified in-game: money
	// demonstrably flowed in — float rose while spending continued — yet the
	// field delta stayed 0). So derive income from the accounting identity:
	//   money in = change in liquid worth + money out.
	// This also captures non-harvest income (cheat grants, crates, tech
	// buildings), which is exactly what an "economy strength" signal wants.
	int const harvestDelta = harvested - obs.LastHarvested; // reference only
	obs.IncomeRate = obs.FloatTrend + obs.SpendRate;
	obs.LastHarvested = harvested;
	obs.LastSpent = spent;

	if (cfg.DebugTicks)
		Debug::Log("[DossierExt] econ %s#%d f%d: float=%d (trend %+d/win) income=%d/win spend=%d/win net=%+d/win (harvestFld=%d)\n",
			pHouse->get_ID(), idx, frame, obs.FloatNow, obs.FloatTrend,
			obs.IncomeRate, obs.SpendRate, obs.IncomeRate - obs.SpendRate, harvestDelta);
	return true;
}
