#pragma once

#include <string>
#include <vector>

// The match's STARTING CONDITIONS, read from spawn.ini [Settings].
//
// The point isn't to log lobby options — it's to answer "what changes how this
// player plays?". Each setting becomes a dimension/value pair like `Cash-High`
// or `Units-None`, and habits fold into a record per pair. Compare records
// ACROSS the values of one dimension and the divergence tells you whether that
// dimension actually matters: if `Cash-High` and `Cash-Low` produce very
// different openings and aggression, cash drives this player; if `SW-On` and
// `SW-Off` look identical, superweapons don't.
//
// Per-dimension-value (rather than per-combination) is deliberate: every match
// contributes to EVERY dimension, so this learns in a handful of games instead
// of needing one game per unique settings combination.
namespace MatchSettings
{
	void Reset();

	// Parse spawn.ini and build this match's dimension/value keys. Idempotent.
	void Load();

	// e.g. { "Cash-High", "Units-None", "SW-Off", "Crates-On", ... }
	std::vector<std::string> const& Keys();

	// Raw values worth showing in the log / profile meta.
	std::string const& Summary();
}
