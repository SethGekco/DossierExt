#pragma once

#include <set>
#include <string>
#include <vector>

class HouseClass;

// Phase 3 — inference (DESIGN §4). Two INI catalogs over ONE predicate grammar:
//
//   [Dossier.Signs]      cheap tells, evaluated live      ("hoards cash")
//   [Dossier.Strategies] ground truth, also predicates    ("pushed the west lane")
//
// A Sign is a hint; a Strategy is proof something happened. Both are the same
// predicate type, so there is a single evaluator and the catalogs grow in INI
// without touching the DLL.
//
// Learning is plain counting: at game end, for every sign that fired and every
// strategy confirmed, bump (fired, confirmed) for that pair in the player's
// profile. Prediction = when a sign fires live, any strategy it has historically
// preceded FOR THIS PLAYER gets confidence = confirmed/fired, logged with its
// evidence. Deterministic, explainable, log-only in this phase.
struct Predicate
{
	std::string Observe;               // what to measure (see Signs.cpp)
	std::vector<std::string> Subject;  // type IDs / bucket key, per observable
	std::string Op = ">=";
	double Value = 0;
	int AfterFrame = 0;                // ignore before this frame
	int BeforeFrame = 0;               // 0 = no upper bound
	bool Valid = false;
};

struct SignDef
{
	std::string Name;
	Predicate P;
};

struct StrategyDef
{
	std::string Name;
	Predicate P;
};

namespace Signs
{
	void Reset();

	// Parse [Dossier.Signs] / [Dossier.Strategies] / [Dossier.Inference].
	void LoadCatalogs();

	// Evaluate every sign and strategy for this house; log first-time firings
	// and any prediction they license. Reads only the Observatory table.
	void Evaluate(HouseClass* pHouse);

	std::vector<SignDef> const& SignCatalog();
	std::vector<StrategyDef> const& StrategyCatalog();

	// What fired / was confirmed this game, for the game-end fold.
	std::set<int> const& FiredSigns(int houseIndex);
	std::set<int> const& ConfirmedStrategies(int houseIndex);
}
