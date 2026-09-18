#include "Dossier/Config.h"
#include "Dossier/Engine.h"
#include "Dossier/Identity.h"
#include "Dossier/Profile.h"

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
	Debug::Log("[DossierExt] [Dossier.General]: Enabled=%d DebugTicks=%d CheckpointInterval=%d ProfileDir=%s\n",
		cfg.Enabled, cfg.DebugTicks, cfg.CheckpointInterval, cfg.ProfileDir.c_str());

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
	DossierConfig::Reset();
	DossierConfig::EnsureParsed();
	return 0;
}
