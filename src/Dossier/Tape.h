#pragma once

// L1b — the tactics tape (DESIGN §10d). Every order a human gives arrives as a
// network event and executes on every client at the same frame, so this is
// synced data, unlike a click hook. Each order is captured with the unit's
// situation (target, distance, both ranges, health, reload) — the raw material
// for micro cases. Log-only: every order is counted, the first LogLines per
// match are written out in full.
namespace Tape
{
	void Reset();

	// Per-house order counts, logged at checkpoints when they changed.
	void Summary();
}
