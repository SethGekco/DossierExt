#include "Dossier/Config.h"
#include "Dossier/Engine.h"
#include "Dossier/Identity.h"
#include "Dossier/Profile.h"
#include "Dossier/Observatory.h"
#include "Dossier/Economy.h"
#include "Dossier/Survey.h"
#include "Dossier/Production.h"
#include "Dossier/Scoreboard.h"
#include "Dossier/KillTracker.h"

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
	cfg.EconSmoothing = pINI->ReadDouble("Dossier.General", "EconSmoothing", cfg.EconSmoothing);
	cfg.SurveyPeriod = pINI->ReadInteger("Dossier.General", "SurveyPeriod", cfg.SurveyPeriod);
	cfg.OreReachRadius = pINI->ReadInteger("Dossier.General", "OreReachRadius", cfg.OreReachRadius);
	cfg.ScoreboardPeriod = pINI->ReadInteger("Dossier.General", "ScoreboardPeriod", cfg.ScoreboardPeriod);
	cfg.OpeningMaxEvents = pINI->ReadInteger("Dossier.General", "OpeningMaxEvents", cfg.OpeningMaxEvents);
	cfg.RecencyWeight = pINI->ReadDouble("Dossier.General", "RecencyWeight", cfg.RecencyWeight);
	Debug::Log("[DossierExt] [Dossier.General]: Enabled=%d DebugTicks=%d CheckpointInterval=%d ProfileDir=%s "
		"EconWindow=%d EconSmoothing=%.2f SurveyPeriod=%d OreReachRadius=%d ScoreboardPeriod=%d\n",
		cfg.Enabled, cfg.DebugTicks, cfg.CheckpointInterval, cfg.ProfileDir.c_str(),
		cfg.EconWindow, cfg.EconSmoothing, cfg.SurveyPeriod, cfg.OreReachRadius, cfg.ScoreboardPeriod);

	// ─── [Dossier.Identity] — name-vs-install identity policy ───────────
	pINI->ReadString("Dossier.Identity", "Mode", cfg.IdentityMode.c_str(), buf, sizeof(buf));
	cfg.IdentityMode = buf;
	pINI->ReadString("Dossier.Identity", "GlobalProfile", cfg.GlobalProfileName.c_str(), buf, sizeof(buf));
	cfg.GlobalProfileName = buf;
	cfg.TrustNameAfter = pINI->ReadInteger("Dossier.Identity", "TrustNameAfter", cfg.TrustNameAfter);
	cfg.DivergenceThreshold = pINI->ReadDouble("Dossier.Identity", "DivergenceThreshold", cfg.DivergenceThreshold);
	cfg.MPNameTrust = pINI->ReadBool("Dossier.Identity", "MPNameTrust", cfg.MPNameTrust);
	cfg.RecordInMultiplayer = pINI->ReadBool("Dossier.Identity", "RecordInMultiplayer", cfg.RecordInMultiplayer);
	cfg.ActOnProfilesInMultiplayer = pINI->ReadBool("Dossier.Identity", "ActOnProfilesInMultiplayer", cfg.ActOnProfilesInMultiplayer);
	if (cfg.ActOnProfilesInMultiplayer)
		Debug::Log("[DossierExt] WARNING: ActOnProfilesInMultiplayer=yes — local profiles would "
			"influence the sim with 2+ humans. Clients hold DIFFERENT profiles, so this WILL desync. "
			"Only safe if every client is guaranteed identical dossier data.\n");
	Debug::Log("[DossierExt] [Dossier.Identity]: Mode=%s GlobalProfile=%s TrustNameAfter=%d "
		"DivergenceThreshold=%.2f MPNameTrust=%d\n",
		cfg.IdentityMode.c_str(), cfg.GlobalProfileName.c_str(), cfg.TrustNameAfter,
		cfg.DivergenceThreshold, cfg.MPNameTrust);

	// ─── [Dossier.Records] — which layers to record ─────────────────────
	cfg.RecOverall = pINI->ReadBool("Dossier.Records", "Overall", cfg.RecOverall);
	cfg.RecPerCountry = pINI->ReadBool("Dossier.Records", "PerCountry", cfg.RecPerCountry);
	cfg.RecPerMap = pINI->ReadBool("Dossier.Records", "PerMap", cfg.RecPerMap);
	cfg.RecPerSpawn = pINI->ReadBool("Dossier.Records", "PerSpawn", cfg.RecPerSpawn);
	cfg.RecSpatial = pINI->ReadBool("Dossier.Records", "Spatial", cfg.RecSpatial);
	cfg.SpatialBucket = pINI->ReadInteger("Dossier.Records", "SpatialBucket", cfg.SpatialBucket);
	cfg.SpatialTopN = pINI->ReadInteger("Dossier.Records", "SpatialTopN", cfg.SpatialTopN);
	cfg.RushWindow = pINI->ReadInteger("Dossier.Records", "RushWindow", cfg.RushWindow);
	cfg.TransferReport = pINI->ReadBool("Dossier.Records", "TransferReport", cfg.TransferReport);
	cfg.TransferThreshold = pINI->ReadDouble("Dossier.Records", "TransferThreshold", cfg.TransferThreshold);
	cfg.MapSimilarThreshold = pINI->ReadDouble("Dossier.Records", "MapSimilarThreshold", cfg.MapSimilarThreshold);
	Debug::Log("[DossierExt] [Dossier.Records]: Overall=%d PerCountry=%d PerMap=%d PerSpawn=%d "
		"Spatial=%d SpatialBucket=%d SpatialTopN=%d RushWindow=%d\n",
		cfg.RecOverall, cfg.RecPerCountry, cfg.RecPerMap, cfg.RecPerSpawn,
		cfg.RecSpatial, cfg.SpatialBucket, cfg.SpatialTopN, cfg.RushWindow);

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
	KillTracker::Reset();
	DossierConfig::Reset();
	DossierConfig::EnsureParsed();
	return 0;
}
