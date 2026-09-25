#include "Dossier/Engine.h"
#include "Dossier/Config.h"
#include "Dossier/Identity.h"
#include "Dossier/Profile.h"
#include "Dossier/Observatory.h"
#include "Dossier/Economy.h"
#include "Dossier/Survey.h"
#include "Dossier/Scoreboard.h"
#include "Dossier/Distill.h"
#include "Dossier/Signs.h"
#include "Dossier/MatchSettings.h"

#include <HouseClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

namespace
{
	int g_lastCheckpointFrame = 0;

	// Restore the folded scopes to their pristine on-disk state, so a fold can be
	// re-run from scratch. Session fields and [Meta] totals are left alone.
	void RestoreFoldedScopes(PlayerProfile& profile, PlayerProfile const& baseline)
	{
		profile.Overall = baseline.Overall;
		profile.Countries = baseline.Countries;
		profile.Maps = baseline.Maps;
		profile.Spatial = baseline.Spatial;
		profile.MapInfo = baseline.MapInfo;
		profile.Settings = baseline.Settings;
		profile.Versus = baseline.Versus;
	}

	// Fold this match into a profile and write it. IDEMPOTENT: it rewinds to the
	// baseline first, so calling it every checkpoint yields exactly the same file
	// as calling it once. That is what lets an ABANDONED match still teach —
	// Rex's matches routinely end with no win/loss, and throwing away everything
	// observed in them was the single biggest brake on learning.
	void FoldAndSave(PlayerProfile& profile, PlayerProfile const* pBaseline,
		HouseObs& obs, int const houseIndex, int const outcome, const char* const tag)
	{
		if (pBaseline)
			RestoreFoldedScopes(profile, *pBaseline);
		Distill::FoldHabits(profile, obs, outcome);
		Distill::FoldAssociations(profile, houseIndex);
		Profile::Save(profile, tag);
	}

	void RecordOutcome(HouseClass* const pHouse, PlayerProfile& profile)
	{
		// Winner/loser flags are the engine's own verdict; Defeated covers
		// elimination paths that never set IsLoser.
		bool const won = pHouse->IsWinner;
		bool const lost = pHouse->IsLoser || pHouse->Defeated;
		if (!won && !lost)
			return;

		profile.OutcomeRecorded = true;
		// Per-scope Played/Won/Lost are counted inside the fold; [Meta] keeps
		// the profile-wide totals.
		if (won) ++profile.GamesWon; else ++profile.GamesLost;
		Debug::Log("[DossierExt] OUTCOME %s#%d '%s' as %s: %s (IsWinner=%d IsLoser=%d Defeated=%d)\n",
			pHouse->get_ID(), pHouse->ArrayIndex, profile.RawName.c_str(),
			profile.CurrentCountry.c_str(), won ? "WON" : "LOST",
			pHouse->IsWinner, pHouse->IsLoser, pHouse->Defeated);

		// Phase 2: fold this game's habits into the persistent dossier before
		// the final save — the named record, and (for the local human) the
		// install-wide record that renaming can't escape.
		// Rewind to the baseline first: checkpoints have very likely already
		// folded this match as "Abandoned", and folding again on top would
		// double-count it. FoldAndSave handles the rewind.
		int const outcome = won ? 1 : -1;
		if (auto const pObs = Observatory::Find(pHouse->ArrayIndex))
		{
			FoldAndSave(profile, Profile::BaselineByHouse(pHouse->ArrayIndex), *pObs,
				pHouse->ArrayIndex, outcome, won ? "Won" : "Lost");
			if (auto const pGlobal = Profile::Global())
			{
				if (pHouse == HouseClass::CurrentPlayer)
				{
					if (won) ++pGlobal->GamesWon; else ++pGlobal->GamesLost;
					pGlobal->Names.insert(profile.Name);
					FoldAndSave(*pGlobal, Profile::BaselineGlobal(), *pObs,
						pHouse->ArrayIndex, outcome, won ? "Won" : "Lost");
					Distill::ReportIdentityTrust(profile);
					// Transfer read is most meaningful on the install-wide
					// record: it spans every name, country and map played.
					Distill::ReportTransfer(*pGlobal);
					Distill::ReportSettingInfluence(*pGlobal);
					Distill::ReportMatchupBias(*pGlobal);
				}
			}
		}
		// (FoldAndSave already wrote both profiles.)
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

	MatchSettings::Load();   // starting conditions, before anything folds
	Identity::EnsureRoster();

	// ── Observatory (Phase 1) ────────────────────────────────────────────
	// Survey self-gates on frame, so calling it from every house tick runs it
	// once per SurveyPeriod. Economy + Scoreboard self-gate per house.
	Survey::MaybeRun();
	if (!pHouse->IsObserver() && !pHouse->IsNeutral())
	{
		Economy::Sample(pHouse);
		Scoreboard::Evaluate(pHouse);
		Signs::Evaluate(pHouse);   // Phase 3: tells + ground truth, log-only
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

		// Fold the match-so-far rather than only saving counters, so quitting
		// without a verdict still leaves the learning on disk. outcome=0 keeps
		// Won/Lost untouched; Played/HabitSamples count the match.
		if (auto const pProfile = Profile::FindByHouse(pHouse->ArrayIndex))
		{
			if (!pProfile->OutcomeRecorded)
				if (auto const pObs = Observatory::Find(pHouse->ArrayIndex))
				{
					FoldAndSave(*pProfile, Profile::BaselineByHouse(pHouse->ArrayIndex),
						*pObs, pHouse->ArrayIndex, 0, "Abandoned");
					if (pHouse == HouseClass::CurrentPlayer)
						if (auto const pGlobal = Profile::Global())
							FoldAndSave(*pGlobal, Profile::BaselineGlobal(), *pObs,
								pHouse->ArrayIndex, 0, "Abandoned");
				}
		}
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
