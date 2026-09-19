#pragma once

class HouseClass;

// Phase 1 economy ledger: per-house income/spend/float/trend over sliding
// EconWindow-frame windows, from HarvestedCredits / CreditsSpent / balance.
// Cheap field reads; runs per house on its staggered tick. Log-only.
namespace Economy
{
	void Reset();

	// Sample this house if its window has elapsed. Returns true if a new
	// sample was taken (so the caller can log it).
	bool Sample(HouseClass* pHouse);
}
