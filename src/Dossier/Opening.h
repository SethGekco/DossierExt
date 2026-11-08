#pragma once

class HouseClass;

// L1a — the opening book (DESIGN §10c). Every house, AI or human, has its
// first minutes recorded from the engine's lifetime build counters and graded
// at fixed marks, so a test game quit after 30 seconds still teaches. The book
// is a prefix tree of structure sequences per context: openings that share a
// start share the stored nodes. Record/log only — nothing acts on it yet.
namespace Opening
{
	void Reset();
	void Tick(HouseClass* pHouse);

	// Idempotent (baseline + this match, like the profile fold); writes only
	// when a mark was graded since the last save.
	void Save(const char* why);
}
