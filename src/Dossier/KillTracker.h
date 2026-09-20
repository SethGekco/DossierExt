#pragma once

// Phase 2 aggression sensing: hooks TechnoClass::RegisterDestruction (the same
// proven seat DoctrineExt uses; same-address hooks chain) to record, per house,
// the frame of its first kill (aggression onset), kills dealt, and losses
// taken. Distilled into the profile at game end. Log-only otherwise.
namespace KillTracker
{
	void Reset();
}
