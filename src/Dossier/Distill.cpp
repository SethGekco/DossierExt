#include "Dossier/Distill.h"
#include "Dossier/Profile.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"
#include "Dossier/Survey.h"

#include <BuildingTypeClass.h>
#include <Utilities/Debug.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace
{
	// EMA fold: first sample sets the value directly; later games blend with
	// RecencyWeight so recent behaviour dominates but history isn't erased.
	void Fold(double& field, double const sample, bool const first, double const w)
	{
		field = first ? sample : (1.0 - w) * field + w * sample;
	}

	const char* StructName(int const typeIndex)
	{
		if (typeIndex < 0 || typeIndex >= BuildingTypeClass::Array.Count)
			return "?";
		auto const pType = BuildingTypeClass::Array.GetItem(typeIndex);
		return pType ? pType->get_ID() : "?";
	}

	// Fold this game's habits into one record, at any scope.
	void FoldInto(HabitRecord& rec, HouseObs const& obs, double const w,
		std::vector<std::string> const& opening)
	{
		bool const first = (rec.HabitSamples == 0);

		double const incomeSample = obs.IncomeSamples > 0
			? static_cast<double>(obs.SumIncome) / obs.IncomeSamples : 0.0;
		Fold(rec.AvgIncome, incomeSample, first, w);
		Fold(rec.AvgPeakArmy, static_cast<double>(obs.PeakArmy), first, w);
		Fold(rec.AvgMaxFloat, static_cast<double>(obs.MaxFloat), first, w);
		if (obs.FirstKillFrame >= 0)
			Fold(rec.AvgFirstKill, static_cast<double>(obs.FirstKillFrame),
				rec.AvgFirstKill < 0, w);

		if (obs.UnitMixSamples > 0)
		{
			std::set<std::string> keys;
			for (auto const& [id, _] : rec.UnitMix) keys.insert(id);
			for (auto const& [id, _] : obs.UnitMix) keys.insert(id);
			for (auto const& id : keys)
			{
				auto const it = obs.UnitMix.find(id);
				double const sample = it != obs.UnitMix.end()
					? static_cast<double>(it->second) / obs.UnitMixSamples : 0.0;
				double cur = rec.UnitMix.count(id) ? rec.UnitMix.at(id) : 0.0;
				Fold(cur, sample, first, w);
				if (cur < 0.001)
					rec.UnitMix.erase(id); // prune negligible entries
				else
					rec.UnitMix[id] = cur;
			}
		}

		rec.Opening = opening;
		++rec.HabitSamples;
	}

	// Merge a game's grid into a stored grid, then keep only the hottest
	// SpatialTopN buckets so a profile can't grow without bound.
	void FoldGrid(std::map<std::string, int>& stored,
		std::map<std::string, int> const& fresh, int const topN)
	{
		for (auto const& [bucket, weight] : fresh)
			stored[bucket] += weight;
		if (topN <= 0 || static_cast<int>(stored.size()) <= topN)
			return;
		std::vector<std::pair<std::string, int>> all(stored.begin(), stored.end());
		std::sort(all.begin(), all.end(), [](auto const& a, auto const& b)
			{ return a.second != b.second ? a.second > b.second : a.first < b.first; });
		all.resize(topN);
		stored.clear();
		for (auto const& [bucket, weight] : all)
			stored[bucket] = weight;
	}

	// How different are two habit vectors? L1 distance over unit-mix (the
	// richest signal, range 0..2 -> halved to 0..1) blended with normalised
	// differences on aggression timing and cash-hoarding. Deterministic and
	// explainable — the log line names the biggest contributor.
	double Divergence(HabitRecord const& a, HabitRecord const& b, std::string& why)
	{
		std::set<std::string> keys;
		for (auto const& [id, _] : a.UnitMix) keys.insert(id);
		for (auto const& [id, _] : b.UnitMix) keys.insert(id);
		double mixL1 = 0.0;
		std::string worstId;
		double worstGap = 0.0;
		for (auto const& id : keys)
		{
			double const va = a.UnitMix.count(id) ? a.UnitMix.at(id) : 0.0;
			double const vb = b.UnitMix.count(id) ? b.UnitMix.at(id) : 0.0;
			double const gap = std::fabs(va - vb);
			mixL1 += gap;
			if (gap > worstGap) { worstGap = gap; worstId = id; }
		}
		double const mix = std::min(1.0, mixL1 * 0.5);

		auto relDiff = [](double const x, double const y)
		{
			double const denom = std::max(1.0, std::max(std::fabs(x), std::fabs(y)));
			return std::min(1.0, std::fabs(x - y) / denom);
		};
		double const aggro = (a.AvgFirstKill >= 0 && b.AvgFirstKill >= 0)
			? relDiff(a.AvgFirstKill, b.AvgFirstKill) : 0.0;
		double const cash = relDiff(a.AvgMaxFloat, b.AvgMaxFloat);

		double const score = 0.6 * mix + 0.2 * aggro + 0.2 * cash;
		char buf[192];
		std::snprintf(buf, sizeof(buf), "mix=%.2f (biggest gap %s %.2f) aggro=%.2f cash=%.2f",
			mix, worstId.empty() ? "-" : worstId.c_str(), worstGap, aggro, cash);
		why = buf;
		return score;
	}
}

namespace
{
	// How alike are two maps? Normalised differences on size, spawn count and
	// starting ore — enough to say "that 4-spawn ore-rich map plays like this
	// one" without pretending to understand terrain.
	double MapDistance(MapFingerprint const& a, MapFingerprint const& b)
	{
		auto rel = [](double x, double y)
		{
			double const denom = std::max(1.0, std::max(std::fabs(x), std::fabs(y)));
			return std::min(1.0, std::fabs(x - y) / denom);
		};
		double const size = 0.5 * (rel(a.Width, b.Width) + rel(a.Height, b.Height));
		double const spawns = rel(a.Spawns, b.Spawns);
		double const ore = rel(static_cast<double>(a.OreTotal), static_cast<double>(b.OreTotal));
		return 0.4 * size + 0.3 * spawns + 0.3 * ore;
	}
}

void Distill::ReportTransfer(PlayerProfile const& profile)
{
	auto const& cfg = DossierConfig::Instance;
	if (!cfg.TransferReport)
		return;
	if (profile.Overall.HabitSamples < 1)
		return;

	std::string why;

	// Per-country: does the playstyle shift with the country? (Rex's original
	// premise — now measured instead of assumed.)
	for (auto const& [country, rec] : profile.Countries)
	{
		if (rec.HabitSamples < 1)
			continue;
		double const d = Divergence(rec, profile.Overall, why);
		Debug::Log("[DossierExt] transfer %s country=%s games=%d divergence=%.2f [%s] -> %s\n",
			profile.Name.c_str(), country.c_str(), rec.HabitSamples, d, why.c_str(),
			d < cfg.TransferThreshold
			? "PORTABLE (plays the same as always)"
			: "COUNTRY-SPECIFIC (different game on this country)");
	}

	// Per-map: is the strategy map-invariant, or tailored to this map?
	for (auto const& [mapKey, rec] : profile.Maps)
	{
		if (rec.HabitSamples < 1)
			continue;
		double const d = Divergence(rec, profile.Overall, why);
		Debug::Log("[DossierExt] transfer %s map=%s games=%d divergence=%.2f [%s] -> %s\n",
			profile.Name.c_str(), mapKey.c_str(), rec.HabitSamples, d, why.c_str(),
			d < cfg.TransferThreshold
			? "PORTABLE (same plan he runs everywhere — predicts him on UNPLAYED maps)"
			: "MAP-SPECIFIC (tailored here; use this map's record, not the general one)");
	}

	// Nearest already-played map by fingerprint: what an unplayed map inherits.
	auto const cur = profile.MapInfo.find(profile.CurrentMapStem);
	if (cur == profile.MapInfo.end() || !cur->second.Valid())
		return;
	std::string bestKey;
	double bestDist = 1e9;
	for (auto const& [stem, fp] : profile.MapInfo)
	{
		if (stem == profile.CurrentMapStem || !fp.Valid())
			continue;
		double const d = MapDistance(cur->second, fp);
		if (d < bestDist) { bestDist = d; bestKey = stem; }
	}
	if (!bestKey.empty())
		Debug::Log("[DossierExt] map similarity: %s is closest to %s (distance=%.2f) -> %s\n",
			profile.CurrentMapStem.c_str(), bestKey.c_str(), bestDist,
			bestDist < cfg.MapSimilarThreshold
			? "SIMILAR: habits learned there apply here"
			: "different map character; lean on the Overall record instead");
}

void Distill::FoldHabits(PlayerProfile& profile, HouseObs& obs)
{
	auto const& cfg = DossierConfig::Instance;
	double const w = cfg.RecencyWeight;

	// The opening fingerprint is shared by every scope this game folds into.
	std::vector<std::string> opening;
	for (auto const& [frame, typeIdx] : obs.BuildOrder)
		opening.push_back(std::to_string(frame) + ":" + StructName(typeIdx));

	// Remember what this map looks like, so a future unplayed map can inherit
	// from the closest thing this player has actually been seen on.
	if (!profile.CurrentMapStem.empty())
	{
		auto const& fp = Survey::Fingerprint();
		if (fp.Valid())
			profile.MapInfo[profile.CurrentMapStem] = fp;
	}

	if (cfg.RecOverall)
		FoldInto(profile.Overall, obs, w, opening);
	if (cfg.RecPerCountry)
		FoldInto(profile.Countries[profile.CurrentCountry], obs, w, opening);
	if (cfg.RecPerMap && !profile.CurrentMapKey.empty())
		FoldInto(profile.Maps[profile.CurrentMapKey], obs, w, opening);

	if (cfg.RecSpatial && !profile.CurrentMapKey.empty())
	{
		auto& sp = profile.Spatial[profile.CurrentMapKey];
		FoldGrid(sp.AttackGrid, obs.AttackGrid, cfg.SpatialTopN);
		FoldGrid(sp.RushGrid, obs.RushGrid, cfg.SpatialTopN);
		FoldGrid(sp.BuildGrid, obs.BuildGrid, cfg.SpatialTopN);
		Debug::Log("[DossierExt] spatial %s @ %s: attack=%u rush=%u build=%u buckets kept\n",
			profile.Name.c_str(), profile.CurrentMapKey.c_str(),
			sp.AttackGrid.size(), sp.RushGrid.size(), sp.BuildGrid.size());
	}

	auto const& rec = profile.Countries.count(profile.CurrentCountry)
		? profile.Countries[profile.CurrentCountry] : profile.Overall;
	Debug::Log("[DossierExt] distilled %s as %s on %s: games=%d avgIncome=%.0f avgPeakArmy=%.0f "
		"avgMaxFloat=%.0f avgFirstKill=%.0f unitTypes=%u openingEvents=%u\n",
		profile.Name.c_str(), profile.CurrentCountry.c_str(),
		profile.CurrentMapKey.c_str(), rec.HabitSamples,
		rec.AvgIncome, rec.AvgPeakArmy, rec.AvgMaxFloat, rec.AvgFirstKill,
		rec.UnitMix.size(), rec.Opening.size());
}

void Distill::ReportIdentityTrust(PlayerProfile const& named)
{
	auto const& cfg = DossierConfig::Instance;
	if (cfg.IdentityMode == "NameOnly")
		return;
	auto const pGlobal = Profile::Global();
	if (!pGlobal)
		return;

	// Would this NAME be trusted on its own, or does the AI fall back to the
	// install-wide record? Phase 2 only reports it; Phase 3+ consults it.
	std::string why;
	double const d = Divergence(named.Overall, pGlobal->Overall, why);
	bool const enoughGames = named.Overall.HabitSamples >= cfg.TrustNameAfter;
	bool const distinct = d >= cfg.DivergenceThreshold;
	bool const standalone = (cfg.IdentityMode != "GlobalOnly") && enoughGames && distinct;

	Debug::Log("[DossierExt] identity '%s': games=%d/%d divergence=%.2f/%.2f [%s] -> %s\n",
		named.Name.c_str(), named.Overall.HabitSamples, cfg.TrustNameAfter,
		d, cfg.DivergenceThreshold, why.c_str(),
		standalone ? "TRUST THIS NAME (distinct persona)"
		: "USE INSTALL-WIDE RECORD (a new name buys no fresh start)");
}
