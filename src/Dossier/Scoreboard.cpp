#include "Dossier/Scoreboard.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"

#include <HouseClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <map>

namespace
{
	std::map<int, int> g_lastEvalFrame;

	bool IsEnemyOf(HouseClass* const pOwner, HouseClass* const pOther)
	{
		return pOther && pOther != pOwner
			&& !pOther->Defeated && !pOther->IsObserver() && !pOther->IsNeutral()
			&& !pOwner->IsAlliedWith(pOther);
	}

	// Ground worth holding: reachable ore PLUS captured tech buildings. Ore
	// alone goes dead on derrick-economy maps (verified: Powder Keg, nearest
	// ore 100+ cells away, 11 derricks held, steady income).
	double Territory(HouseObs const& obs)
	{
		return static_cast<double>(obs.OreReachable)
			+ static_cast<double>(obs.TechBuildingsOwned)
			* DossierConfig::Instance.TechBuildingValue;
	}

	// Component ratio self/enemyAvg, clamped so a nearly-dead enemy doesn't
	// blow the standing up to infinity.
	double Ratio(double const self, double const enemyAvg)
	{
		if (enemyAvg <= 0.0)
			return self > 0.0 ? 3.0 : 1.0;
		double r = self / enemyAvg;
		if (r > 3.0) r = 3.0;
		if (r < 0.0) r = 0.0;
		return r;
	}

	Tier BaseTier(double const s, DossierConfig const& cfg)
	{
		if (s < cfg.DesperateBelow) return Tier::Desperate;
		if (s < cfg.LosingBelow)    return Tier::Losing;
		if (s < cfg.WinningAbove)   return Tier::Even;
		return Tier::Winning;
	}

	// Worsening is immediate (the AI should notice danger at once); improving
	// requires clearing the boundary by Hysteresis so recovery doesn't flap.
	Tier DecideTier(double const s, Tier const cur, DossierConfig const& cfg)
	{
		Tier const plain = BaseTier(s, cfg);
		if (static_cast<int>(plain) < static_cast<int>(cur))
		{
			Tier const withHys = BaseTier(s - cfg.Hysteresis, cfg);
			return static_cast<int>(withHys) < static_cast<int>(cur) ? withHys : cur;
		}
		return plain;
	}
}

void Scoreboard::Reset()
{
	g_lastEvalFrame.clear();
}

void Scoreboard::Evaluate(HouseClass* const pHouse)
{
	auto const& cfg = DossierConfig::Instance;
	if (cfg.ScoreboardPeriod <= 0)
		return;
	if (pHouse->IsObserver() || pHouse->IsNeutral() || pHouse->Defeated)
		return;

	int const frame = Unsorted::CurrentFrame;
	int const idx = pHouse->ArrayIndex;
	auto const it = g_lastEvalFrame.find(idx);
	if (it != g_lastEvalFrame.end() && frame - it->second < cfg.ScoreboardPeriod)
		return;
	g_lastEvalFrame[idx] = frame;

	auto const pSelf = Observatory::Find(idx);
	if (!pSelf || !pSelf->SurveyInit)
		return; // wait for the first survey to fill the aggregates

	// Average the living enemies' strength components.
	double eArmy = 0, eEcon = 0, eTerr = 0;
	int enemies = 0;
	for (int i = 0; i < HouseClass::Array.Count; ++i)
	{
		auto const pOther = HouseClass::Array.GetItem(i);
		if (!IsEnemyOf(pHouse, pOther))
			continue;
		auto const pObs = Observatory::Find(i);
		if (!pObs)
			continue;
		eArmy += static_cast<double>(pObs->ArmyValue);
		eEcon += pObs->SmoothedIncome;
		eTerr += Territory(*pObs);
		++enemies;
	}

	Tier const prevTier = pSelf->CurrentTier;
	double raw;
	if (enemies == 0)
	{
		raw = 3.0;
	}
	else
	{
		eArmy /= enemies; eEcon /= enemies; eTerr /= enemies;
		double const rArmy = Ratio(static_cast<double>(pSelf->ArmyValue), eArmy);
		double const rEcon = Ratio(pSelf->SmoothedIncome, eEcon);
		double const rTerr = Ratio(Territory(*pSelf), eTerr);
		double const wsum = cfg.ArmyWeight + cfg.EconWeight + cfg.TerritoryWeight;
		raw = wsum > 0
			? (cfg.ArmyWeight * rArmy + cfg.EconWeight * rEcon + cfg.TerritoryWeight * rTerr) / wsum
			: 1.0;
	}
	pSelf->Standing = raw;

	// Smooth into a momentum score (EMA) so a single spiky window — income
	// arrives in harvester-sized lumps, army value dips while an MCV deploys —
	// can't flip the tier. The tier is read from the SMOOTHED value; hysteresis
	// then guards the recovery edge on top of that.
	bool const firstEval = !pSelf->TierInit;
	double const a = cfg.StandingSmoothing;
	pSelf->SmoothedStanding = firstEval ? raw : (a * raw + (1.0 - a) * pSelf->SmoothedStanding);
	double const s = pSelf->SmoothedStanding;
	pSelf->CurrentTier = DecideTier(s, firstEval ? BaseTier(s, cfg) : prevTier, cfg);
	pSelf->TierInit = true;

	// Tier transitions are the headline; log them unconditionally. Steady-tier
	// re-evals only under DebugTicks.
	if (firstEval || pSelf->CurrentTier != prevTier)
		Debug::Log("[DossierExt] SCOREBOARD %s#%d f%d: %s -> %s (standing=%.2f raw=%.2f; army=%lld econ=%d terr=%lld vs %d enemy)\n",
			pHouse->get_ID(), idx, frame, TierName(prevTier), TierName(pSelf->CurrentTier),
			s, raw, pSelf->ArmyValue, static_cast<int>(pSelf->SmoothedIncome), static_cast<long long>(Territory(*pSelf)), enemies);
	else if (cfg.DebugTicks)
		Debug::Log("[DossierExt] scoreboard %s#%d f%d: %s (standing=%.2f raw=%.2f)\n",
			pHouse->get_ID(), idx, frame, TierName(pSelf->CurrentTier), s, raw);
}
