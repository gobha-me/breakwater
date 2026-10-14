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
| Crawler | 2 at the start, then spawns | 2 | Up to 3 steps over any tile without a building or a unit, water included. |

Every unit and building has 2 HP. A bump deals 1 and a crawler's strike deals
2, so one bump never kills, two always do, and a strike that lands kills or
razes whatever it hits. That keeps the arithmetic readable at a glance, and it
makes every telegraphed strike a decision: stop it or lose what it hits.

Crawlers outreach engineers (3 steps to 2) so the threat arrives before the
tide forces the retreat; see "Balance" below.

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
   taken from where it stands *now*. A unit there takes 2 damage, crawlers
   included. A building there takes 2 damage. An empty tile absorbs the hit.
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

A crawler goes for the town. A plan is a destination within 3 steps (staying
put counts) together with a direction from that destination that strikes a
**standing building**. Plans are ranked by this key, lowest first, and the
first plan wins:

1. Steps taken.
2. Destination `y`, then `x`.
3. Direction, in the order North, East, South, West.

If no building is in reach, the crawler **marches on the town**: it moves to
the reachable tile closest to the nearest standing building by Manhattan
distance. Ties break by fewest steps, then destination `y`, then `x`. With no
building standing, it holds position. Wherever it stops, it strikes an engineer
standing next to it, if any, choosing the first in the order North, East,
South, West. Otherwise it declares **no attack**.

Engineers cannot lure crawlers. Under the earlier rule, a crawler with no
building in reach chased any engineer it could reach. Passive play exploited
that: an engineer would dangle out of reach of the town, step out of each
telegraphed strike, and drag every crawler away from the last cottage (#71).

The key is a total order, so no outcome can depend on iteration order or on a
random draw. The crawler AI uses no randomness at all, which keeps it readable
to the player: the same position always provokes the same move.

Targets are chosen against the board *at planning time*. If the player later
moves the target away, the attack still lands on the declared tile, whatever is
there.

## Balance

The numbers above are tuned with the solver (`bw_solve`), not by feel. The
question is whether the game's one verb is necessary: a mission a player can
win by walking inland, without ever shoving, fails pillar 2.

### How it is measured

Two sweeps over seeds 0-999. Every "winnable" verdict comes with a witness
that is replayed in-process, so it is a certificate. "Unwinnable" comes only
from a complete search. "Unproven" means the budget ran out and says nothing
either way.

- **With shoves, at the proof budget** (`kProofBudget`, 2M nodes): the share
  of missions that can be offered.
- **Without shoves, at 10M nodes**
  (`bw_solve --range 0-999 --no-shove --budget 10000000`, about 15 minutes on
  16 cores):
  - the winnable count is a **lower bound** on missions that need no shove;
  - the unwinnable count is a lower bound on missions that **need** one;
  - the true share lies between the two.

At 1M nodes the shove-free sweep is too loose to compare rule sets. Between
two of them, 108 seeds flipped one way and 66 the other. #71 read its 1M lower
bound (175) as "under 20%". The same rules at 10M certify at least 559. A lower
bound never establishes an upper limit (#96).

### Results

| Rules | With shoves | **No** shoves, 10M | Retreat policy | Idle |
|---|---|---|---|---|
| Bootstrap: crawlers prefer engineers, strike 1, reach 2 | 1000 / 1000 (1M) | 1000 winnable (1M) | 841 won | 0 |
| #39: crawlers prefer buildings, strike 2, reach 3 | 993 (1M) | ≥ 464 winnable (1M only) | 10 won | 0 |
| #71: crawlers march on the town, no luring | 981 (1M) | **≥ 559 winnable**, 50 unwinnable, 391 unproven | 10 won | 0 |
| **#96: the town falls when two of its three cottages are razed** | **975** (2M) | **≥ 121 winnable**, **383 unwinnable**, 496 unproven | **0** won | 0 |

Under #96, a random sample of 48 of the 496 unproven seeds was re-run at 50M
nodes: 38 came out unwinnable, 7 winnable and 3 still unproven. Scaled up, that
puts the true shove-free share near **19%**, with a sampling interval of
roughly 15-29%. That is an estimate. The certified statements are these:

- at least 12.1% of missions are winnable without a shove;
- at least 38.3% cannot be won without one.

Under #71, at least 55.9% were winnable without a shove, and 5% were proven
to need one.

### What the levers did

- **Bootstrap to #39:** the solver won all 1000 seeds without a single shove.
  Strikes then went for the town, and walking inland stopped working.
- **#39 to #71:** engineers could still bait crawlers away from the last
  cottage. Marching crawlers ended that.
- **#71 to #96:** passive play kept winning anyway. Three 2-HP cottages need
  three landed strikes, crawlers spend most of the five turns walking in from
  the waterline, and engineers on the high ground body-block approach tiles.
  Two of three cottages give the defence no slack for a crawler that gets
  through, so a crawler that will land has to be turned or pushed back.
- **Measured and not adopted (#96):** two more crawlers (spawns 2, 2, 2, 1).
  That gave 960 with shoves and 132 shove-free at 1M, against 175: a small
  effect for a lot more difficulty.
- **Measured for #72 and not adopted:** the Rammer, a crawler whose strike
  pushes. Its ram pushed the unit on the target tile by the shove rules, and a
  cottage there took a bump. Several targeting rules were tried: hunting
  engineers, or ramming wherever it did the most harm, including knocking
  engineers off guard posts and pushing crawlers into cottages. None made
  shoves more necessary:
  - **Under #71's rules**, as a sixth creature: ≥ 566 and ≥ 556 shove-free at
    10M, against ≥ 559 without it.
  - **Under #96's rules**, as a sixth creature (the guard-post rule):
    - with shoves: 965 winnable;
    - shove-free at 10M: ≥ 161 winnable, 205 unwinnable, 634 unproven;
    - a 48-seed spot check at 50M put the shove-free share near **31%**,
      against about 19% without it.
  - Replacing a crawler instead of adding one took pressure off the town
    (≥ 252 against ≥ 175 at 1M under #71).

  The extra body blocks crawler paths and shoves crawlers about. That helps
  passive play more than the ram hurts it. The prototype is kept on branch
  `72-rammer`, and its Meshy model is not shipped.

Seeds the solver cannot prove at the proof budget are rejected at generation
(`kRejectedSeeds`), so every mission offered is winnable. At 10M nodes, 997 of
1000 are: the proof budget, not the rules, decides most rejections.

## Outcome

- **Loss**, evaluated after every command and every enemy-phase step: every
  engineer is dead, or **the town has fallen**, which happens when two of its
  three cottages are razed (`kTownFallsAfter`). The mission ends on the spot:
  the rest of the enemy phase does not run, and the turn does not advance.
- **Win**: the enemy phase of turn 5 completes without a loss. The turn stays
  at 5; no sixth turn is announced.
- Once the mission has ended, every command is rejected and changes nothing,
  and no crawler holds an intent.

Why two and not three: with three cottages to lose, one crawler that got
through cost nothing, and passive play won at least 56% of missions (#96).
Losing one cottage is survivable; losing a second is not. So each strike that
will land on a cottage asks whether this is the one to let through.

A hand-built scenario with fewer buildings falls when all of them are razed,
and one with no buildings cannot be lost by razing them. One with no engineers
cannot be lost by losing them. Neither can be created during a mission, so a
generated one always has three of each. A razed building leaves no trace on
the board, so the town's starting size is part of the state hash.

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
