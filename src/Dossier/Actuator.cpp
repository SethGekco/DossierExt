#include "Dossier/Actuator.h"
#include "Dossier/Config.h"
#include "Dossier/Observatory.h"
#include "Dossier/Identity.h"

#include <AITriggerTypeClass.h>
#include <HouseClass.h>
#include <CCINIClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <map>
#include <string>
#include <vector>

namespace
{
	// AIDifficulty is inverted (0=Hard, 1=Normal, 2=Easy) and the flag a given
	// difficulty READS is likewise reversed. Same mapping DoctrineExt uses.
	const int kReadFlag[3] = { 2, 1, 0 };

	bool GetFlag(AITriggerTypeClass* const pT, int const i)
	{
		return i == 0 ? pT->Enabled_Easy : i == 1 ? pT->Enabled_Normal : pT->Enabled_Hard;
	}
	void SetFlag(AITriggerTypeClass* const pT, int const i, bool const v)
	{
		if (i == 0) pT->Enabled_Easy = v;
		else if (i == 1) pT->Enabled_Normal = v;
		else pT->Enabled_Hard = v;
	}

	// One response: which triggers to force on/off when a strategy is predicted.
	struct Response
	{
		std::string Strategy;
		std::vector<std::string> Enable;
		std::vector<std::string> Disable;
		std::string Unlock = "Never";   // refuse by default, not permit
	};
	std::vector<Response> g_responses;
	bool g_loaded = false;

	// Saved original flag values so teardown can undo everything.
	struct Saved { int TypeIndex; int FlagIndex; bool Original; };
	std::vector<Saved> g_saved;
	// Strategies already acted on this match, so one prediction fires one action.
	std::map<int, std::vector<std::string>> g_actedPerHouse;

	std::vector<std::string> SplitList(const char* const v)
	{
		std::vector<std::string> out;
		std::string tok;
		for (const char* p = v; ; ++p)
		{
			if (*p == ',' || *p == '\0')
			{
				while (!tok.empty() && tok.back() == ' ') tok.pop_back();
				if (!tok.empty()) out.push_back(tok);
				tok.clear();
				if (!*p) break;
			}
			else if (*p != ' ' || !tok.empty()) tok += *p;
		}
		return out;
	}

	void Load()
	{
		if (g_loaded)
			return;
		auto const pINI = CCINIClass::INI_Rules;
		if (!pINI)
			return;
		g_loaded = true;

		char buf[512] = { 0 };
		int const n = pINI->GetKeyCount("Dossier.Responses");
		for (int i = 0; i < n; ++i)
		{
			auto const pKey = pINI->GetKeyName("Dossier.Responses", i);
			if (!pKey) continue;
			pINI->ReadString("Dossier.Responses", pKey, "", buf, sizeof(buf));
			if (!*buf) continue;
			Response r;
			r.Strategy = buf;
			std::string const sec = "Response." + r.Strategy;
			pINI->ReadString(sec.c_str(), "Enable", "", buf, sizeof(buf));
			r.Enable = SplitList(buf);
			pINI->ReadString(sec.c_str(), "Disable", "", buf, sizeof(buf));
			r.Disable = SplitList(buf);
			pINI->ReadString(sec.c_str(), "Unlock", r.Unlock.c_str(), buf, sizeof(buf));
			r.Unlock = buf;
			Debug::Log("[DossierExt] response '%s': enable=%u disable=%u Unlock=%s\n",
				r.Strategy.c_str(), r.Enable.size(), r.Disable.size(), r.Unlock.c_str());
			g_responses.push_back(std::move(r));
		}
		Debug::Log("[DossierExt] [Dossier.Actuation]: Enabled=%d — %u response(s) loaded\n",
			DossierConfig::Instance.ActuationEnabled, g_responses.size());
	}

	// Does the acting house's current standing satisfy this Unlock level?
	bool TierAllows(std::string const& unlock, Tier const tier, std::string& why)
	{
		if (unlock == "Always") { why = "Unlock=Always"; return true; }
		if (unlock == "Never") { why = "Unlock=Never (refused)"; return false; }
		// Ordered worst-last: acting is allowed once the house is AT or BELOW
		// the named standing, i.e. it only gets dirty when it needs to.
		int need = -1;
		if (unlock == "Even") need = static_cast<int>(Tier::Even);
		else if (unlock == "Losing") need = static_cast<int>(Tier::Losing);
		else if (unlock == "Desperate") need = static_cast<int>(Tier::Desperate);
		if (need < 0) { why = "Unlock=<unrecognised>, refused"; return false; }
		bool const ok = static_cast<int>(tier) >= need;
		char buf[96];
		std::snprintf(buf, sizeof(buf), "tier %s %s Unlock=%s", TierName(tier),
			ok ? ">=" : "<", unlock.c_str());
		why = buf;
		return ok;
	}

	// Flip one trigger's flag for the difficulty this house reads, remembering
	// the original so it can be put back.
	void Apply(AITriggerTypeClass* const pT, int const flag, bool const value,
		const char* const strategy, const char* const tierWhy, HouseClass* const pHouse)
	{
		bool const before = GetFlag(pT, flag);
		if (before == value)
			return;
		g_saved.push_back({ pT->ArrayIndex, flag, before });
		SetFlag(pT, flag, value);
		Debug::Log("[DossierExt] ACT f%d %s#%d: %s trigger '%s' (flag %d: %d->%d) "
			"because %s predicted [%s]\n",
			Unsorted::CurrentFrame, pHouse->get_ID(), pHouse->ArrayIndex,
			value ? "ENABLE" : "DISABLE", pT->get_ID(), flag, before, value,
			strategy, tierWhy);
	}
}

void Actuator::Reset()
{
	g_responses.clear();
	g_loaded = false;
	g_saved.clear();
	g_actedPerHouse.clear();
}

void Actuator::RestoreAll()
{
	if (g_saved.empty())
		return;
	int restored = 0;
	// Walk backwards so the earliest saved value wins if a flag was touched twice.
	for (auto it = g_saved.rbegin(); it != g_saved.rend(); ++it)
	{
		if (it->TypeIndex < 0 || it->TypeIndex >= AITriggerTypeClass::Array.Count)
			continue;
		if (auto const pT = AITriggerTypeClass::Array.GetItem(it->TypeIndex))
		{
			SetFlag(pT, it->FlagIndex, it->Original);
			++restored;
		}
	}
	Debug::Log("[DossierExt] restored %d trigger flag(s) to their pre-actuation state\n",
		restored);
	g_saved.clear();
}

void Actuator::OnPredicted(int const houseIndex, std::string const& strategy,
	int const fired, int const confirmed)
{
	auto const& cfg = DossierConfig::Instance;
	Load();
	if (!cfg.ActuationEnabled || g_responses.empty())
		return;

	// GATE 2 — multiplayer. Profiles are local files that differ per client, so
	// letting one drive an AI decision makes the clients diverge. Refuse loudly
	// rather than silently desync someone's match.
	if (Identity::HumanCount() > 1 && !cfg.ActOnProfilesInMultiplayer)
	{
		Debug::Log("[DossierExt] actuation REFUSED for %s: %d humans and "
			"ActOnProfilesInMultiplayer=no (local profiles differ per client -> desync)\n",
			strategy.c_str(), Identity::HumanCount());
		return;
	}

	auto const pHouse = houseIndex >= 0 && houseIndex < HouseClass::Array.Count
		? HouseClass::Array.GetItem(houseIndex) : nullptr;
	if (!pHouse)
		return;

	// The prediction is ABOUT the human; the actor is the AI that must answer
	// it. Apply to every hostile AI house, each gated on its OWN standing.
	for (int i = 0; i < HouseClass::Array.Count; ++i)
	{
		auto const pAI = HouseClass::Array.GetItem(i);
		if (!pAI || pAI == pHouse || pAI->IsHumanPlayer) continue;
		if (pAI->IsObserver() || pAI->IsNeutral() || pAI->Defeated) continue;
		if (pAI->IsAlliedWith(pHouse)) continue;

		int const diff = static_cast<int>(pAI->AIDifficulty);
		if (diff < 0 || diff > 2) continue;

		// GATE 3a — the per-difficulty dial from §5a.
		if (!cfg.Diff[diff].Escalation)
		{
			Debug::Log("[DossierExt] actuation skipped for %s#%d: DossierExt.Escalation=no "
				"in [%s]\n", pAI->get_ID(), i, DossierConfig::IntuitiveSection(diff));
			continue;
		}

		auto const pObs = Observatory::Find(i);
		Tier const tier = pObs && pObs->TierInit ? pObs->CurrentTier : Tier::Even;

		auto& acted = g_actedPerHouse[i];
		for (auto const& r : g_responses)
		{
			if (r.Strategy != strategy) continue;
			bool already = false;
			for (auto const& s : acted) if (s == strategy) { already = true; break; }
			if (already) continue;

			// GATE 3b — the escalation tier.
			std::string why;
			if (!TierAllows(r.Unlock, tier, why))
			{
				Debug::Log("[DossierExt] actuation withheld for %s#%d on %s: %s\n",
					pAI->get_ID(), i, strategy.c_str(), why.c_str());
				continue;
			}

			acted.push_back(strategy);
			Debug::Log("[DossierExt] ACTUATING %s#%d against predicted %s "
				"(%d/%d past games), authorised by %s\n",
				pAI->get_ID(), i, strategy.c_str(), confirmed, fired, why.c_str());

			for (auto const& id : r.Enable)
				if (auto const pT = AITriggerTypeClass::Find(id.c_str()))
					Apply(pT, kReadFlag[diff], true, strategy.c_str(), why.c_str(), pAI);
				else
					Debug::Log("[DossierExt] WARNING: response '%s' names unknown trigger '%s'\n",
						strategy.c_str(), id.c_str());
			for (auto const& id : r.Disable)
				if (auto const pT = AITriggerTypeClass::Find(id.c_str()))
					Apply(pT, kReadFlag[diff], false, strategy.c_str(), why.c_str(), pAI);
				else
					Debug::Log("[DossierExt] WARNING: response '%s' names unknown trigger '%s'\n",
						strategy.c_str(), id.c_str());
		}
	}
}
