#pragma once

// Phase 1 Observatory scan: once per SurveyPeriod, sweep the map + object
// arrays and refresh every real house's survey aggregates (ore nearest/
// reachable, tech buildings, army value, building value, per-type structure
// counts). Self-gates on frame; safe to call from every house tick. Log-only.
namespace Survey
{
	void Reset();

	// Runs the full sweep if a SurveyPeriod has elapsed since the last one.
	void MaybeRun();
}
