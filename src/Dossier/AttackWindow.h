#pragma once

class HouseClass;

// ─── Attack-window planner (DESIGN §6 actuator #2) ───────────────────────────
//
// The headline promise of the original brief: "plan an attack that's too
// expensive to counter." This is the piece that decides WHEN that is true.
//
// The test is affordability, and it counts BOTH ways a player can answer a
// strike — not just cash:
//
//   playerAnswer = their standing army value
//                + their liquid cash
//                + what their income will bring in over the horizon
//
//   window OPEN when  playerAnswer < ourStrike * AffordRatio
//
// Counting the standing army matters: a player sitting on a large force can
// answer a push without spending a credit, so a cash-only test would call a
// window open against someone perfectly able to defend. Income over the horizon
// matters for the opposite reason — a poor-looking player with a strong economy
// can simply buy the answer while the attack is still crossing the map.
//
// Detection always logs its arithmetic so the call can be graded. ACTING on it
// runs through the Actuator's three gates like any other lever, under the
// response name "AttackWindow".
namespace AttackWindow
{
	void Reset();

	// Self-gates on frame; safe to call from every AI house tick.
	void Evaluate(HouseClass* pAI);
}
