#include "Dossier/Engine.h"
#include "Dossier/Config.h"
#include "Dossier/Identity.h"
#include "Dossier/Profile.h"
#include "Dossier/Observatory.h"
#include "Dossier/Economy.h"
#include "Dossier/Survey.h"
#include "Dossier/Scoreboard.h"

#include <HouseClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

namespace
{
	int g_lastCheckpointFrame = 0;

	void RecordOutcome(HouseClass* const pHouse, PlayerProfile& profile)
	{
		// Winner/loser flags are the engine's own verdict; Defeated covers
		// elimination paths that never set IsLoser.
		bool const won = pHouse->IsWinner;
		bool const lost = pHouse->IsLoser || pHouse->Defeated;
		if (!won && !lost)
			return;

		profile.OutcomeRecorded = true;
		auto& rec = profile.Countries[profile.CurrentCountry];
		if (won)
		{
			++profile.GamesWon;
			++rec.Won;
		}
		else
		{
			++profile.GamesLost;
			++rec.Lost;
		}
		Debug::Log("[DossierExt] OUTCOME %s#%d '%s' as %s: %s (IsWinner=%d IsLoser=%d Defeated=%d)\n",
			pHouse->get_ID(), pHouse->ArrayIndex, profile.RawName.c_str(),
			profile.CurrentCountry.c_str(), won ? "WON" : "LOST",
			pHouse->IsWinner, pHouse->IsLoser, pHouse->Defeated);
		Profile::Save(profile, won ? "Won" : "Lost");
	}
}

void Engine::Reset()
{
	g_lastCheckpointFrame = 0;
}

void Engine::TickHouse(HouseClass* const pHouse)
{
	auto const& cfg = DossierConfig::Instance;
	if (!cfg.Parsed || !cfg.Enabled || !pHouse)
		return;

	Identity::EnsureRoster();

	// ── Observatory (Phase 1) ────────────────────────────────────────────
	// Survey self-gates on frame, so calling it from every house tick runs it
	// once per SurveyPeriod. Economy + Scoreboard self-gate per house.
	Survey::MaybeRun();
	if (!pHouse->IsObserver() && !pHouse->IsNeutral())
	{
		Economy::Sample(pHouse);
		Scoreboard::Evaluate(pHouse);
	}

	// Outcome polling — only for houses we opened a dossier on.
	if (auto const pProfile = Profile::FindByHouse(pHouse->ArrayIndex))
		if (!pProfile->OutcomeRecorded)
			RecordOutcome(pHouse, *pProfile);

	// Periodic checkpoint so a crash or hard quit loses at most one interval.
	int const frame = Unsorted::CurrentFrame;
	if (cfg.CheckpointInterval > 0 && frame - g_lastCheckpointFrame >= cfg.CheckpointInterval)
	{
		g_lastCheckpointFrame = frame;
		if (cfg.DebugTicks)
			Debug::Log("[DossierExt] checkpoint at frame %d\n", frame);
		Profile::CheckpointAll();
	}
}

// HouseClass::Update — the proven staggered sense-tick seat (ECX = house;
// chains with Ares/Antares/DoctrineExt at the same address).
DEFINE_HOOK(0x4F8440, DossierExt_HouseClass_Update_Tick, 0x5)
{
	GET(HouseClass* const, pThis, ECX);
	Engine::TickHouse(pThis);
	return 0;
}
