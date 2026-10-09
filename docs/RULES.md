# Rules of the vertical slice

The numbers tests assert against. Each rule says why it is what it is, so a
balance change is a deliberate edit here first and in code second.

Coordinates: `x` is the column, `y` the row. Row 0 is the sea edge and `y`
grows inland. "Adjacent" always means orthogonally adjacent.

## Island

| Rule | Value | Why |
|---|---|---|
| Board | 8 × 8 | Small enough to read at a glance and to search. |
| Elevation of tile (x, y) | `y + j`, with `j` in {0, 1} from the `Terrain` stream | The tide rises one level a turn, so the shore retreats about one row a turn, unevenly. |
| Buildings | 3, each 2 HP, in rows 5-7, from the `Buildings` stream | The town is what you defend; it sits on the high ground. |
| Flooded | `elevation < tide` | One comparison, so there are no partial states. |

Elevation was `y/2 + j` at bootstrap. Its maximum of 4 meant a tide rising
once per turn drowned the whole island by turn 5, which made every mission a
loss.

## Tide

- The tide is **1** when the mission starts, and rises by **1** in every enemy
  phase. It is 6 after the last turn.
- At tide 6, rows 0-4 are sea, row 5 is half-flooded, and rows 6-7 stand dry.
  The tide does not turn back within the slice: holding out means ending on the
  high ground.
- When the tide rises, an engineer standing on a newly flooded tile drowns.
  Crawlers swim. Buildings are not affected by water.

## Units

| Unit | Count | HP | Movement |
|---|---|---|---|
| Engineer | 3 | 2 | Up to 2 steps over dry, empty tiles. |
| Crawler | 2 at the start, then spawns | 2 | Up to 2 steps over any tile without a building or a unit, water included. |

Every unit has the same 2 HP so that one bump never kills, and two always do.
That keeps the arithmetic of a shove readable at a glance.

## Deployment

Engineers start on row 4, which is dry at tide 1 and floods from tide 5. Their
three distinct columns are drawn from the `Deploy` stream by sampling without
replacement. Starting low gives the player a reason to move and a deadline to
retreat by.

## Spawns

| When | Crawlers |
|---|---|
| Mission start | 2 |
| Enemy phase of turns 1, 2, 3 | 1 each |
| Enemy phase of turns 4, 5 | 0 |

Five crawlers in total. To choose a spawn tile, list the flooded tiles with no
unit and no building. If that list is empty, list the empty tiles of row 0
instead. Keep only
the tiles in the most inland row of that list, sort them by `x`, and pick one
with the `Spawns` stream. If no tile qualifies, the spawn is skipped.

Crawlers come ashore at the waterline, so the front advances with the tide.
When one turn spawns several crawlers, each draw comes from that turn's
`Spawns` stream in sequence, and the list is rebuilt after every placement.

## A turn

The mission lasts **5 turns**. Each turn has a player phase and then an enemy
phase.

### Player phase

Each engineer acts once per turn, and the player chooses the order:

1. **Move** (optional): to any tile within 2 steps over dry tiles with no
   building and no unit. Moving into water is never legal; drowning happens
   only by being shoved or by the tide.
2. **Shove** (optional, after or instead of the move): push the unit on an
   adjacent tile one tile further away. It may be a crawler or an engineer.
   A shove ends the engineer's turn.

Shove resolution is `Board::push`:

| The shoved unit hits | Result |
|---|---|
| Open ground | It moves. An engineer that lands on water drowns. |
| The board edge | It takes 1 damage and stays put. |
| Another unit | Each takes 1 damage. Neither moves. |
| A building | The unit and the building each take 1 damage. The unit stays put. |

**End turn** is legal at any point in the player phase. Engineers that have not
acted simply forgo their action.

Why move-then-shove rather than move *or* shove: crawlers move after the
player, so an engineer almost never starts its turn adjacent to one. With move
*or* shove, a shove would be all but unusable.

### Enemy phase

Each step runs in this order. Within a step, crawlers act in ascending id,
which is spawn order.

1. **Attacks resolve.** Each living crawler hits the tile its intent points at,
   taken from where it stands *now*. A unit there takes 1 damage, crawlers
   included. A building there takes 1 damage. An empty tile absorbs the hit.
   A crawler that died earlier in this step does not attack.
2. **The tide rises** by 1. Engineers on flooded tiles drown.
3. **Spawns**, by the table above.
4. **Crawlers move and declare.** Each living crawler moves, then publishes an
   intent for the next enemy phase. Spawned crawlers included.
5. **The turn advances**, and the outcome is evaluated.

After turn 5, steps 3 and 4 are skipped: no crawler plans for a turn that will
never come.

Because an intent is a **direction**, not a target, shoving a crawler retargets
its attack. That is the central tactic: turn crawlers into each other, into
empty water, away from the town.

## Crawler intent

A crawler picks the best of the plans it can reach this turn. A plan is a
destination within 2 steps (staying put counts) together with an attack
direction from that destination. Plans are ranked by this key, lowest first,
and the first plan wins:

1. Target class: an engineer is 0, a building is 1. A plan aimed at anything
   else is not a plan.
2. Steps taken.
3. Destination `y`, then `x`.
4. Direction, in the order North, East, South, West.

If no plan exists, the crawler moves to the reachable tile closest to the
nearest standing building by Manhattan distance. Ties break by fewest steps,
then destination `y`, then `x`. It then declares **no attack**.

The key is a total order, so no outcome can depend on iteration order or on a
random draw. The crawler AI uses no randomness at all, which keeps it readable
to the player: the same position always provokes the same move.

Targets are chosen against the board *at planning time*. If the player later
moves the target away, the attack still lands on the declared tile, whatever is
there.

## Outcome

- **Loss**, evaluated after every command and every enemy-phase step: every
  engineer is dead, or every building is razed.
- **Win**: the enemy phase of turn 5 completes without a loss.
- Once the mission has ended, every command is rejected and changes nothing.

## RNG streams

Stream ids are part of the save and replay format: append, never renumber.

| Id | Stream | Used for |
|---|---|---|
| 1 | `Terrain` | Elevation jitter |
| 2 | `Buildings` | Building placement |
| 3 | `Deploy` | Engineer columns |
| 4 | `Spawns` | Spawn tile choice |

A per-turn draw comes from a generator derived from `(seed, stream, turn)`,
not from a generator carried in the mission state. A position is then fully
described by the board, the turn and the intents, so the solver can hash it
cheaply.

The roadmap reserved id 4 for an `EnemyAi` stream. The AI turned out to need
no randomness, so the id goes to `Spawns`, and `Deploy` takes 3.

## Solver cost

Per engineer per turn there are at most 13 destinations (staying, 4 one-step
tiles, 8 two-step tiles), and each can be followed by one of at most 4 shoves or
by none: 65 options. Three engineers in any order give at most 65³ × 3! ≈ 1.6
million action sequences per turn. Many of them reach the same position.

That is too many to enumerate over five turns. It does not have to be
enumerated. Proving a mission survivable means finding **one** winning line,
so the solver is an existential depth-first search. It tries promising moves
first, prunes positions it has already seen by `state_hash`, and stops at the
first win. Only a mission it cannot prove pays for the whole search. Such a
seed hits the node budget, is reported `unproven`, and is rejected.

Fallback, if the sweep over seeds 0-999 is too slow: restrict move-then-shove
to move-*or*-shove. That cuts the options to 17 per engineer, and the change
would be made here first.
