#include "Dossier/Config.h"
#include "Dossier/Engine.h"
#include "Dossier/Identity.h"
#include "Dossier/Profile.h"
#include "Dossier/Observatory.h"
#include "Dossier/Economy.h"
#include "Dossier/Survey.h"
#include "Dossier/Production.h"
#include "Dossier/Scoreboard.h"

#include <CCINIClass.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

DossierConfig DossierConfig::Instance;

namespace
{
	// AIDifficulty is inverted: 0=Hard, 1=Normal, 2=Easy.
	const char* const kIntuitive[3] = { "Difficult", "Normal", "Easy" };
	const char* const kEngine[3] = { "Easy", "Normal", "Difficult" };
}

const char* DossierConfig::IntuitiveSection(int const aiDifficulty)
{
	return aiDifficulty >= 0 && aiDifficulty < 3 ? kIntuitive[aiDifficulty] : "?";
}

const char* DossierConfig::EngineMultiplierSection(int const aiDifficulty)
{
	return aiDifficulty >= 0 && aiDifficulty < 3 ? kEngine[aiDifficulty] : "?";
}

void DossierConfig::Reset()
{
	Instance = DossierConfig{};
}

void DossierConfig::EnsureParsed()
{
	auto& cfg = Instance;
	if (cfg.Parsed)
		return;

	auto const pINI = CCINIClass::INI_Rules;
	if (!pINI)
		return;

	cfg.Parsed = true;
	char buf[256] = { 0 };

	// ─── [Dossier.General] ──────────────────────────────────────────────
	cfg.Enabled = pINI->ReadBool("Dossier.General", "Enabled", cfg.Enabled);
	cfg.DebugTicks = pINI->ReadBool("Dossier.General", "DebugTicks", cfg.DebugTicks);
	cfg.CheckpointInterval = pINI->ReadInteger("Dossier.General", "CheckpointInterval", cfg.CheckpointInterval);
	pINI->ReadString("Dossier.General", "ProfileDir", cfg.ProfileDir.c_str(), buf, sizeof(buf));
	cfg.ProfileDir = buf;
	cfg.EconWindow = pINI->ReadInteger("Dossier.General", "EconWindow", cfg.EconWindow);
	cfg.SurveyPeriod = pINI->ReadInteger("Dossier.General", "SurveyPeriod", cfg.SurveyPeriod);
	cfg.OreReachRadius = pINI->ReadInteger("Dossier.General", "OreReachRadius", cfg.OreReachRadius);
	cfg.ScoreboardPeriod = pINI->ReadInteger("Dossier.General", "ScoreboardPeriod", cfg.ScoreboardPeriod);
	Debug::Log("[DossierExt] [Dossier.General]: Enabled=%d DebugTicks=%d CheckpointInterval=%d ProfileDir=%s "
		"EconWindow=%d SurveyPeriod=%d OreReachRadius=%d ScoreboardPeriod=%d\n",
		cfg.Enabled, cfg.DebugTicks, cfg.CheckpointInterval, cfg.ProfileDir.c_str(),
		cfg.EconWindow, cfg.SurveyPeriod, cfg.OreReachRadius, cfg.ScoreboardPeriod);

	// ─── [Dossier.Scoreboard] — tier thresholds + standing weights ──────
	cfg.LosingBelow = pINI->ReadDouble("Dossier.Scoreboard", "Losing.Below", cfg.LosingBelow);
	cfg.DesperateBelow = pINI->ReadDouble("Dossier.Scoreboard", "Desperate.Below", cfg.DesperateBelow);
	cfg.WinningAbove = pINI->ReadDouble("Dossier.Scoreboard", "Winning.Above", cfg.WinningAbove);
	cfg.Hysteresis = pINI->ReadDouble("Dossier.Scoreboard", "Hysteresis", cfg.Hysteresis);
	cfg.StandingSmoothing = pINI->ReadDouble("Dossier.Scoreboard", "StandingSmoothing", cfg.StandingSmoothing);
	cfg.ArmyWeight = pINI->ReadDouble("Dossier.Scoreboard", "ArmyWeight", cfg.ArmyWeight);
	cfg.EconWeight = pINI->ReadDouble("Dossier.Scoreboard", "EconWeight", cfg.EconWeight);
	cfg.TerritoryWeight = pINI->ReadDouble("Dossier.Scoreboard", "TerritoryWeight", cfg.TerritoryWeight);
	Debug::Log("[DossierExt] [Dossier.Scoreboard]: Losing.Below=%.2f Desperate.Below=%.2f Winning.Above=%.2f "
		"Hysteresis=%.2f StandingSmoothing=%.2f weights(army/econ/territory)=%.2f/%.2f/%.2f\n",
		cfg.LosingBelow, cfg.DesperateBelow, cfg.WinningAbove, cfg.Hysteresis, cfg.StandingSmoothing,
		cfg.ArmyWeight, cfg.EconWeight, cfg.TerritoryWeight);

	// ─── DossierExt.* per-difficulty keys (intuitive section names) ─────
	// Keys are read from the section a house of that difficulty resolves to:
	// Hard AI ⇒ [Difficult], Easy AI ⇒ [Easy]. Presence is tracked so the
	// echo distinguishes "explicitly no" from "defaulted no".
	for (int d = 0; d < 3; ++d)
	{
		auto& keys = cfg.Diff[d];
		auto const section = kIntuitive[d];

		pINI->ReadString(section, "DossierExt.Shrouded", "", buf, sizeof(buf));
		if (*buf)
			keys.AnySet = true;
		keys.Shrouded = pINI->ReadBool(section, "DossierExt.Shrouded", keys.Shrouded);

		pINI->ReadString(section, "DossierExt.Escalation", "", buf, sizeof(buf));
		if (*buf)
			keys.AnySet = true;
		keys.Escalation = pINI->ReadBool(section, "DossierExt.Escalation", keys.Escalation);

		Debug::Log("[DossierExt] [%s] (AIDifficulty=%d): DossierExt.Shrouded=%d DossierExt.Escalation=%d%s\n",
			section, d, keys.Shrouded, keys.Escalation,
			keys.AnySet ? "" : " (no keys set, defaults)");
	}
}

// Scenario::ClearClasses — game-mode INIs merge into the rules INI per
// scenario, so drop everything and re-read fresh each match. Same site
// DoctrineExt/IntelExt clear at (same-address hooks chain; the Phase 0 echo
// lines double as the liveness proof).
DEFINE_HOOK(0x685659, DossierExt_Scenario_ClearClasses, 0xA)
{
	Engine::Reset();
	Identity::Reset();
	Profile::Reset();
	Observatory::Reset();
	Economy::Reset();
	Survey::Reset();
	Production::Reset();
	Scoreboard::Reset();
	DossierConfig::Reset();
	DossierConfig::EnsureParsed();
	return 0;
}
