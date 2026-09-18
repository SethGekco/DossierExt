#pragma once

class HouseClass;

// Phase 0 engine: per-house tick (chained at the proven HouseClass::Update
// seat). Resolves the roster on first tick, polls human houses' outcome flags
// (IsWinner/IsLoser/Defeated — no dedicated game-end hook needed), and flushes
// dirty profiles on a frame-interval checkpoint.
namespace Engine
{
	void Reset();
	void TickHouse(HouseClass* pHouse);
}
