#include "Dossier/Signs.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"
#include "Dossier/Profile.h"

#include <CCINIClass.h>
#include <HouseClass.h>
#include <BuildingTypeClass.h>
#include <Unsorted.h>
#include <Utilities/Debug.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace
{
	std::vector<SignDef> g_signs;
	std::vector<StrategyDef> g_strategies;
	bool g_loaded = false;

	// Per-house, per-game state. Kept here rather than in HouseObs so the
	// inference layer stays self-contained.
	std::map<int, std::set<int>> g_fired;      // house -> sign indices
	std::map<int, std::set<int>> g_confirmed;  // house -> strategy indices
	std::set<int> const g_empty;

	// [Dossier.Inference]
	bool g_enabled = true;
	int g_minSamples = 2;        // games needed before a pair may predict
	double g_confidence = 0.6;   // confirmed/fired needed to call it

	std::vector<std::string> SplitList(const char* const value)
	{
		std::vector<std::string> out;
		std::string token;
		for (const char* p = value; ; ++p)
		{
			if (*p == ',' || *p == '\0')
			{
				while (!token.empty() && token.back() == ' ')
					token.pop_back();
				if (!token.empty())
					out.push_back(token);
				token.clear();
				if (*p == '\0')
					break;
			}
			else if (*p != ' ' || !token.empty())
				token += *p;
		}
		return out;
	}

	Predicate ParsePredicate(CCINIClass* const pINI, const char* const section)
	{
		Predicate p;
		if (!pINI->GetSection(section))
			return p;
		char buf[256] = { 0 };
		pINI->ReadString(section, "Observe", "", buf, sizeof(buf));
		p.Observe = buf;
		pINI->ReadString(section, "Subject", "", buf, sizeof(buf));
		p.Subject = SplitList(buf);
		pINI->ReadString(section, "Op", p.Op.c_str(), buf, sizeof(buf));
		p.Op = buf;
		p.Value = pINI->ReadDouble(section, "Value", p.Value);
		p.AfterFrame = pINI->ReadInteger(section, "AfterFrame", p.AfterFrame);
		p.BeforeFrame = pINI->ReadInteger(section, "BeforeFrame", p.BeforeFrame);
		p.Valid = !p.Observe.empty();
		return p;
	}

	// ── The single observable table ──────────────────────────────────────
	// Everything a sign or a strategy can measure. All of it is already in the
	// Observatory, so evaluation costs a few map lookups.
	bool Measure(Predicate const& p, HouseObs const& obs, int const frame, double& out)
	{
		auto const& o = p.Observe;
		if (o == "Frame")          { out = frame; return true; }
		if (o == "Float")          { out = obs.FloatNow; return true; }
		if (o == "MaxFloat")       { out = obs.MaxFloat; return true; }
		if (o == "Income")         { out = obs.SmoothedIncome; return true; }
		if (o == "Spend")          { out = obs.SpendRate; return true; }
		if (o == "Army")           { out = static_cast<double>(obs.ArmyValue); return true; }
		if (o == "PeakArmy")       { out = static_cast<double>(obs.PeakArmy); return true; }
		if (o == "Building")       { out = static_cast<double>(obs.BuildingValue); return true; }
		if (o == "Kills")          { out = obs.KillsDealt; return true; }
		if (o == "Losses")         { out = obs.LossesTaken; return true; }
		if (o == "TechBuildings")  { out = obs.TechBuildingsOwned; return true; }
		if (o == "Standing")       { out = obs.SmoothedStanding; return true; }
		if (o == "FirstKill")
		{
			// Not yet drawn blood reads as "very late", so "< N" stays false
			// until it actually happens.
			out = obs.FirstKillFrame >= 0 ? obs.FirstKillFrame : 1e9;
			return true;
		}
		if (o == "UnitFraction")
		{
			if (obs.UnitMixSamples <= 0)
				return false;
			long long sum = 0;
			for (auto const& id : p.Subject)
			{
				auto const it = obs.UnitMix.find(id);
				if (it != obs.UnitMix.end())
					sum += it->second;
			}
			out = static_cast<double>(sum) / obs.UnitMixSamples;
			return true;
		}
		if (o == "StructCount")
		{
			int sum = 0;
			for (auto const& id : p.Subject)
			{
				int const idx = BuildingTypeClass::FindIndex(id.c_str());
				if (idx < 0)
					continue;
				auto const it = obs.StructCounts.find(idx);
				if (it != obs.StructCounts.end())
					sum += it->second;
			}
			out = sum;
			return true;
		}
		if (o == "AttackBucket" || o == "RushBucket" || o == "BuildBucket")
		{
			if (p.Subject.empty())
				return false;
			auto const& grid = o == "AttackBucket" ? obs.AttackGrid
				: o == "RushBucket" ? obs.RushGrid : obs.BuildGrid;
			int sum = 0;
			for (auto const& key : p.Subject)
			{
				auto const it = grid.find(key);
				if (it != grid.end())
					sum += it->second;
			}
			out = sum;
			return true;
		}
		return false; // unknown observable
	}

	bool Compare(double const lhs, std::string const& op, double const rhs)
	{
		if (op == ">")  return lhs > rhs;
		if (op == ">=") return lhs >= rhs;
		if (op == "<")  return lhs < rhs;
		if (op == "<=") return lhs <= rhs;
		if (op == "=" || op == "==") return lhs == rhs;
		return false;
	}

	bool Eval(Predicate const& p, HouseObs const& obs, int const frame, double& measured)
	{
		if (!p.Valid)
			return false;
		if (frame < p.AfterFrame)
			return false;
		if (p.BeforeFrame > 0 && frame > p.BeforeFrame)
			return false;
		if (!Measure(p, obs, frame, measured))
			return false;
		return Compare(measured, p.Op, p.Value);
	}

	// Which record should answer "what has this sign meant before?" — the
	// per-country record if it has evidence, else the install-wide Overall
	// (Phase 2.1 blend: a fresh name inherits the install's memory).
	HabitRecord const* LookupRecord(PlayerProfile const& profile)
	{
		auto const it = profile.Countries.find(profile.CurrentCountry);
		if (it != profile.Countries.end() && !it->second.Assoc.empty())
			return &it->second;
		if (auto const pGlobal = Profile::Global())
		{
			auto const g = pGlobal->Countries.find(profile.CurrentCountry);
			if (g != pGlobal->Countries.end() && !g->second.Assoc.empty())
				return &g->second;
			if (!pGlobal->Overall.Assoc.empty())
				return &pGlobal->Overall;
		}
		return nullptr;
	}

	// A sign just fired — say what it has historically preceded for this player.
	void Predict(PlayerProfile const& profile, std::string const& signName, int const frame)
	{
		auto const pRec = LookupRecord(profile);
		if (!pRec)
		{
			Debug::Log("[DossierExt] no history for sign '%s' yet — recording it.\n",
				signName.c_str());
			return;
		}
		for (auto const& strat : g_strategies)
		{
			auto const key = signName + ">" + strat.Name;
			auto const it = pRec->Assoc.find(key);
			if (it == pRec->Assoc.end())
				continue;
			int const fired = it->second.first;
			int const confirmed = it->second.second;
			if (fired < g_minSamples)
				continue;
			double const conf = fired > 0 ? static_cast<double>(confirmed) / fired : 0.0;
			if (conf < g_confidence)
				continue;
			Debug::Log("[DossierExt] PREDICT f%d: '%s' fired -> expect %s "
				"(%d/%d = %.0f%% of past games for %s as %s)\n",
				frame, signName.c_str(), strat.Name.c_str(), confirmed, fired,
				conf * 100.0, profile.Name.c_str(), profile.CurrentCountry.c_str());
		}
	}
}

void Signs::Reset()
{
	g_signs.clear();
	g_strategies.clear();
	g_fired.clear();
	g_confirmed.clear();
	g_loaded = false;
}

std::vector<SignDef> const& Signs::SignCatalog() { return g_signs; }
std::vector<StrategyDef> const& Signs::StrategyCatalog() { return g_strategies; }

std::set<int> const& Signs::FiredSigns(int const houseIndex)
{
	auto const it = g_fired.find(houseIndex);
	return it != g_fired.end() ? it->second : g_empty;
}

std::set<int> const& Signs::ConfirmedStrategies(int const houseIndex)
{
	auto const it = g_confirmed.find(houseIndex);
	return it != g_confirmed.end() ? it->second : g_empty;
}

void Signs::LoadCatalogs()
{
	if (g_loaded)
		return;
	auto const pINI = CCINIClass::INI_Rules;
	if (!pINI)
		return;
	g_loaded = true;

	g_enabled = pINI->ReadBool("Dossier.Inference", "Enabled", g_enabled);
	g_minSamples = pINI->ReadInteger("Dossier.Inference", "MinSamples", g_minSamples);
	g_confidence = pINI->ReadDouble("Dossier.Inference", "Confidence", g_confidence);

	char buf[256] = { 0 };
	int const signCount = pINI->GetKeyCount("Dossier.Signs");
	for (int i = 0; i < signCount; ++i)
	{
		auto const pKey = pINI->GetKeyName("Dossier.Signs", i);
		if (!pKey)
			continue;
		pINI->ReadString("Dossier.Signs", pKey, "", buf, sizeof(buf));
		if (!*buf)
			continue;
		SignDef d;
		d.Name = buf;
		d.P = ParsePredicate(pINI, d.Name.c_str());
		if (!d.P.Valid)
		{
			Debug::Log("[DossierExt] WARNING: sign '%s' has no [%s] Observe=, dropped.\n",
				d.Name.c_str(), d.Name.c_str());
			continue;
		}
		Debug::Log("[DossierExt] sign '%s': %s %s %.2f (window f%d..f%d)\n",
			d.Name.c_str(), d.P.Observe.c_str(), d.P.Op.c_str(), d.P.Value,
			d.P.AfterFrame, d.P.BeforeFrame);
		g_signs.push_back(std::move(d));
	}

	int const stratCount = pINI->GetKeyCount("Dossier.Strategies");
	for (int i = 0; i < stratCount; ++i)
	{
		auto const pKey = pINI->GetKeyName("Dossier.Strategies", i);
		if (!pKey)
			continue;
		pINI->ReadString("Dossier.Strategies", pKey, "", buf, sizeof(buf));
		if (!*buf)
			continue;
		StrategyDef d;
		d.Name = buf;
		d.P = ParsePredicate(pINI, d.Name.c_str());
		if (!d.P.Valid)
		{
			Debug::Log("[DossierExt] WARNING: strategy '%s' has no [%s] Observe=, dropped.\n",
				d.Name.c_str(), d.Name.c_str());
			continue;
		}
		Debug::Log("[DossierExt] strategy '%s': %s %s %.2f\n", d.Name.c_str(),
			d.P.Observe.c_str(), d.P.Op.c_str(), d.P.Value);
		g_strategies.push_back(std::move(d));
	}

	Debug::Log("[DossierExt] [Dossier.Inference]: Enabled=%d MinSamples=%d Confidence=%.2f "
		"— %u sign(s), %u strategy(ies)\n",
		g_enabled, g_minSamples, g_confidence, g_signs.size(), g_strategies.size());
}

void Signs::Evaluate(HouseClass* const pHouse)
{
	if (!g_enabled || g_signs.empty())
		return;
	// Signs are about PLAYERS: only evaluate houses we keep a dossier on.
	auto const pProfile = Profile::FindByHouse(pHouse->ArrayIndex);
	if (!pProfile)
		return;
	auto const pObs = Observatory::Find(pHouse->ArrayIndex);
	if (!pObs)
		return;

	int const frame = Unsorted::CurrentFrame;
	int const idx = pHouse->ArrayIndex;
	auto& fired = g_fired[idx];
	auto& confirmed = g_confirmed[idx];

	// Signs latch: a tell that was true once stays true for the game, so a
	// later recovery can't erase the evidence it gave us.
	for (size_t i = 0; i < g_signs.size(); ++i)
	{
		if (fired.count(static_cast<int>(i)))
			continue;
		double measured = 0;
		if (!Eval(g_signs[i].P, *pObs, frame, measured))
			continue;
		fired.insert(static_cast<int>(i));
		Debug::Log("[DossierExt] SIGN f%d %s: '%s' (%s=%.2f %s %.2f)\n",
			frame, pProfile->Name.c_str(), g_signs[i].Name.c_str(),
			g_signs[i].P.Observe.c_str(), measured, g_signs[i].P.Op.c_str(),
			g_signs[i].P.Value);
		Predict(*pProfile, g_signs[i].Name, frame);
	}

	for (size_t i = 0; i < g_strategies.size(); ++i)
	{
		if (confirmed.count(static_cast<int>(i)))
			continue;
		double measured = 0;
		if (!Eval(g_strategies[i].P, *pObs, frame, measured))
			continue;
		confirmed.insert(static_cast<int>(i));
		Debug::Log("[DossierExt] CONFIRMED f%d %s: %s (%s=%.2f %s %.2f)\n",
			frame, pProfile->Name.c_str(), g_strategies[i].Name.c_str(),
			g_strategies[i].P.Observe.c_str(), measured,
			g_strategies[i].P.Op.c_str(), g_strategies[i].P.Value);
	}
}
