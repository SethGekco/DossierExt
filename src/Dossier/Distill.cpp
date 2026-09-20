#include "Dossier/Distill.h"
#include "Dossier/Profile.h"
#include "Dossier/Observatory.h"
#include "Dossier/Config.h"

#include <BuildingTypeClass.h>
#include <Utilities/Debug.h>

#include <set>
#include <string>

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
}

void Distill::FoldHabits(PlayerProfile& profile, HouseObs& obs)
{
	auto const& cfg = DossierConfig::Instance;
	double const w = cfg.RecencyWeight;
	auto& rec = profile.Countries[profile.CurrentCountry];

	bool const first = (rec.HabitSamples == 0);

	double const incomeSample = obs.IncomeSamples > 0
		? static_cast<double>(obs.SumIncome) / obs.IncomeSamples : 0.0;
	Fold(rec.AvgIncome, incomeSample, first, w);
	Fold(rec.AvgPeakArmy, static_cast<double>(obs.PeakArmy), first, w);
	Fold(rec.AvgMaxFloat, static_cast<double>(obs.MaxFloat), first, w);

	// Aggression only folds when the house actually fought this game, and uses
	// its own first-sample flag (AvgFirstKill starts at -1 = never measured).
	if (obs.FirstKillFrame >= 0)
		Fold(rec.AvgFirstKill, static_cast<double>(obs.FirstKillFrame),
			rec.AvgFirstKill < 0, w);

	// Unit-mix: EMA over the union of previously-known and this-game types, so
	// abandoned units decay toward zero and new favourites rise.
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
			double cur = rec.UnitMix.count(id) ? rec.UnitMix[id] : 0.0;
			Fold(cur, sample, first, w);
			if (cur < 0.001)
				rec.UnitMix.erase(id); // prune negligible entries
			else
				rec.UnitMix[id] = cur;
		}
	}

	// Opening fingerprint: keep the most recent game's build order verbatim.
	rec.Opening.clear();
	for (auto const& [frame, typeIdx] : obs.BuildOrder)
		rec.Opening.push_back(std::to_string(frame) + ":" + StructName(typeIdx));

	++rec.HabitSamples;

	Debug::Log("[DossierExt] distilled %s as %s: games=%d avgIncome=%.0f avgPeakArmy=%.0f "
		"avgMaxFloat=%.0f avgFirstKill=%.0f unitTypes=%u openingEvents=%u\n",
		profile.Name.c_str(), profile.CurrentCountry.c_str(), rec.HabitSamples,
		rec.AvgIncome, rec.AvgPeakArmy, rec.AvgMaxFloat, rec.AvgFirstKill,
		rec.UnitMix.size(), rec.Opening.size());
}
