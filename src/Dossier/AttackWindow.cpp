#include "Dossier/AttackWindow.h"
#include "Dossier/Config.h"
#include "Dossier/Observatory.h"
#include "Dossier/Profile.h"
#include "Dossier/Actuator.h"

#include <HouseClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <map>

namespace
{
	std::map<int, int> g_lastEval;   // AI house -> frame
	std::map<int, bool> g_open;      // AI house -> window currently open

	// The human this AI is up against: a profiled (human) house that is hostile
	// to it. Picks the first match, which is the single-human case the dossier
	// is allowed to act on anyway.
	HouseClass* HostileHuman(HouseClass* const pAI, HouseObs const** outObs)
	{
		for (int i = 0; i < HouseClass::Array.Count; ++i)
		{
			auto const pH = HouseClass::Array.GetItem(i);
			if (!pH || pH == pAI || !pH->IsHumanPlayer) continue;
			if (pH->IsObserver() || pH->IsNeutral() || pH->Defeated) continue;
			if (pAI->IsAlliedWith(pH)) continue;
			if (!Profile::FindByHouse(i)) continue;   // not one we model
			if (auto const pObs = Observatory::Find(i))
			{
				*outObs = pObs;
				return pH;
			}
		}
		return nullptr;
	}
}

void AttackWindow::Reset()
{
	g_lastEval.clear();
	g_open.clear();
}

void AttackWindow::Evaluate(HouseClass* const pAI)
{
	auto const& cfg = DossierConfig::Instance;
	if (!cfg.WindowEnabled || cfg.WindowPeriod <= 0)
		return;
	if (pAI->IsHumanPlayer || pAI->IsObserver() || pAI->IsNeutral() || pAI->Defeated)
		return;

	int const frame = Unsorted::CurrentFrame;
	int const idx = pAI->ArrayIndex;
	auto const it = g_lastEval.find(idx);
	if (it != g_lastEval.end() && frame - it->second < cfg.WindowPeriod)
		return;
	g_lastEval[idx] = frame;

	auto const pOurs = Observatory::Find(idx);
	if (!pOurs || !pOurs->SurveyInit)
		return;

	HouseObs const* pTheirs = nullptr;
	auto const pHuman = HostileHuman(pAI, &pTheirs);
	if (!pHuman || !pTheirs)
		return;

	double const ourStrike = static_cast<double>(pOurs->ArmyValue);
	// Nothing to threaten with: a "window" we cannot exploit is not a window.
	if (ourStrike < cfg.WindowMinArmy)
	{
		if (g_open[idx])
		{
			g_open[idx] = false;
			Debug::Log("[DossierExt] attack window CLOSED for %s#%d: our army %.0f fell below "
				"WindowMinArmy=%d, nothing to strike with\n",
				pAI->get_ID(), idx, ourStrike, cfg.WindowMinArmy);
		}
		return;
	}

	// What the player can field against it inside the horizon.
	double const horizons = cfg.EconWindow > 0
		? static_cast<double>(cfg.WindowHorizon) / cfg.EconWindow : 0.0;
	double const incomeInHorizon = pTheirs->SmoothedIncome > 0
		? pTheirs->SmoothedIncome * horizons : 0.0;
	double const theirArmy = static_cast<double>(pTheirs->ArmyValue);
	double const theirCash = static_cast<double>(pTheirs->FloatNow);
	double const playerAnswer = theirArmy + theirCash + incomeInHorizon;

	double const need = ourStrike * cfg.WindowAffordRatio;
	bool const wasOpen = g_open[idx];
	// Hysteresis so a window doesn't flicker on a borderline economy: opening
	// needs the full margin, staying open is allowed a little slack.
	double const threshold = wasOpen ? need * (1.0 + cfg.WindowHysteresis) : need;
	bool const open = playerAnswer < threshold;

	if (open == wasOpen)
		return;   // no transition; the arithmetic is logged only on a change
	g_open[idx] = open;

	Debug::Log("[DossierExt] attack window %s for %s#%d vs %s: they can answer %.0f "
		"(army %.0f + cash %.0f + %.0f income over %df) vs our strike %.0f x %.2f = %.0f needed\n",
		open ? "OPEN" : "CLOSED", pAI->get_ID(), idx, pHuman->get_ID(),
		playerAnswer, theirArmy, theirCash, incomeInHorizon, cfg.WindowHorizon,
		ourStrike, cfg.WindowAffordRatio, threshold);

	// Acting on it is a lever like any other: the Actuator applies the master
	// switch, the multiplayer refusal and the escalation tier. Detection above
	// is free and always logs; only this line can change the game.
	if (open)
		Actuator::OnPredicted(pHuman->ArrayIndex, "AttackWindow", 1, 1);
}
