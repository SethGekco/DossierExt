#pragma once

class HouseClass;

// Phase 1 Scoreboard (DESIGN §5c): a per-house standing estimate from synced
// data — army value, economy trend, and territory (ore control) as ratios vs
// the living enemies' average — smoothed into WINNING/EVEN/LOSING/DESPERATE
// tiers with recovery hysteresis. Log-only: tier transitions are logged so Rex
// can grade whether "LOSING" matches the field. Drives nothing yet.
namespace Scoreboard
{
	void Reset();

	// Re-evaluate this house's standing if its ScoreboardPeriod has elapsed.
	void Evaluate(HouseClass* pHouse);
}
