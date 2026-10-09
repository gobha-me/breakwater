# Breakwater: concept

A deterministic, turn-based tactics game on an 8×8 coastal grid.

Sea creatures come ashore and **telegraph** their attacks a turn ahead. You
command three engineers whose tools mostly **push**: shove a crawler into
another crawler, into a building, or off the shelf. Every turn the **tide rises**
and floods the low tiles. Engineers can't swim; crawlers can. Hold the town
until the tide turns.

## Pillars

1. **Perfect information.** Enemy intents are visible before you act. Losses
   come from your choices, not from dice.
2. **Pushes, not hit points.** Positioning beats damage. Every ability moves
   something.
3. **The board changes under you.** The tide makes geography a timer: the safe
   ground at turn 1 is sea by turn 5.
4. **Small and finished.** One island, a handful of units, a handful of
   creatures, each rule polished. Breadth comes later, if ever.

## Why this shape

- **Deterministic core.** All rules are pure C++ in `core/`, driven by seeded,
  independent RNG streams. A mission is fully defined by its seed plus an
  input log, so replays double as golden tests.
- **Provable content.** Because the game has perfect information and a small
  state space, a solver can check that every generated mission is
  survivable. That turns "levels are fair" from a hope into a test.
- **Suited to Meshy.** Diorama-scale assets (crawlers, engineers, cottages,
  sea walls, rocks) are small, self-contained models, which is what
  text-to-3D generates best.

## Vertical slice: acceptance criteria

The slice is done when **all** of these hold, each with evidence:

1. Play one full mission (5 turns) on the generated island: move, push, end
   turn, see enemy intents, watch the tide rise. Win and lose states work.
2. Rules are 100% in `core/`. Godot only presents and forwards input; no rule
   is duplicated in GDScript.
3. Determinism: a recorded input log replays to an identical `state_hash` on
   clang and GCC, debug and release.
4. The solver proves every mission from seeds 0–999 is survivable. Unsolvable
   seeds are rejected at generation.
5. At least 4 Meshy-generated assets in the game (engineer, crawler, building,
   one prop), each with provenance in `assets/PROVENANCE.md` and an owner-approved
   look.
6. `tools/check.sh` passes, and a captured golden image of the opening board
   matches within tolerance.
