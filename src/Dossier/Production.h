#pragma once

// Phase 1 structure build-order tape. Reads the per-house structure-count
// snapshots Survey produces and logs newly-appeared structures with the frame
// they showed up — the raw material for opening fingerprints (Phase 2). This
// is the poll-and-diff first cut; per-unit production waits on a factory hook
// in a later phase. Log-only.
namespace Production
{
	void Reset();

	// Called by Survey right after it refreshes StructCounts/PrevStructCounts.
	void DiffAfterSurvey();
}
