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

	obs.IncomeRate = harvested - obs.LastHarvested; // credits gained this window
	obs.SpendRate = spent - obs.LastSpent;          // credits spent this window
	obs.PrevFloat = obs.FloatNow;
	obs.FloatNow = money;
	obs.FloatTrend = obs.FloatNow - obs.PrevFloat;
	obs.LastHarvested = harvested;
	obs.LastSpent = spent;

	if (cfg.DebugTicks)
		Debug::Log("[DossierExt] econ %s#%d f%d: float=%d (trend %+d/win) income=%d/win spend=%d/win net=%+d/win\n",
			pHouse->get_ID(), idx, frame, obs.FloatNow, obs.FloatTrend,
			obs.IncomeRate, obs.SpendRate, obs.IncomeRate - obs.SpendRate);
	return true;
}
