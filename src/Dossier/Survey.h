#pragma once

// Phase 1 Observatory scan: once per SurveyPeriod, sweep the map + object
// arrays and refresh every real house's survey aggregates (ore nearest/
// reachable, tech buildings, army value, building value, per-type structure
// counts). Self-gates on frame; safe to call from every house tick. Log-only.
struct MapFingerprint;

namespace Survey
{
	void Reset();

	// Runs the full sweep if a SurveyPeriod has elapsed since the last one.
	void MaybeRun();

	// What this map looks like (dimensions, spawn count, starting ore), taken
	// at the first sweep — the basis for "is this map like one I've played?".
	MapFingerprint const& Fingerprint();
}
