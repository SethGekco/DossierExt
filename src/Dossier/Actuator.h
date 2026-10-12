#pragma once

#include <string>

// ─── Phase 4: acting on what we learned ──────────────────────────────────────
//
// Everything before this phase only wrote to the log. This is the first code in
// DossierExt that changes what the AI actually does, so it is deliberately
// wrapped in three independent gates — ALL must pass:
//
//   1. master switch          [Dossier.Actuation] Enabled=  (default NO)
//   2. multiplayer policy     profile-driven action is refused with 2+ humans,
//                             because each client holds a DIFFERENT profile and
//                             a divergent AI decision desyncs the match
//   3. escalation tier        [Dossier.Escalation] <Lever>.Unlock=
//                             Always | Never | Even | Losing | Desperate,
//                             checked against the acting house's live Scoreboard
//                             tier, and the per-difficulty DossierExt.Escalation
//                             dial from §5a
//
// What it can do: force AITriggerTypes on or off in response to a prediction.
//
// HONEST LIMIT on granularity: the engine's enable flags live on the
// AITriggerType itself (Enabled_Easy/Normal/Hard), NOT per house. The engine
// picks one flag by the house's AIDifficulty, so the finest control available is
// "triggers readable by houses of difficulty D". A trigger is further scoped by
// its own OwnerHouseType/SideIndex, which is what keeps this from being a
// blunt global switch. There is no per-house override without a deeper hook.
//
// Everything is restored at scenario teardown, and every flip is logged with the
// tier that authorised it.
namespace Actuator
{
	void Reset();

	// A prediction crossed its confidence bar for this house. May flip triggers.
	void OnPredicted(int houseIndex, std::string const& strategy,
		int fired, int confirmed);

	// Put every flag we touched back. Called per scenario.
	void RestoreAll();
}
