# Roadmap: bootstrap to vertical slice

Phases run in order. Each ends with evidence, not a claim. The acceptance
criteria being chased are in [the concept](CONCEPT.md); the numbers in
parentheses say which criterion a phase closes.

## Status, 2026-10-14: slice complete, depth, sound and campaigns added

| Phase | State | Concept criterion |
|---|---|---|
| 0 Rules | Done: `RULES.md` (#13), rebalanced in #39 | n/a |
| 1 Core loop | Done (#15, #17, #18, #23) | 2: rules only in `core/` |
| 2 Replay | Done (#24, #31, #32) | 3: `tools/replay_check.sh`, 16/16 identical (three mission logs and a campaign, four builds) |
| 3 Playable | Done (#35, #40, #41, #46); captures in `docs/captures/` | 1: playable by input; 6: golden gate in place |
| 4 Solver | Done (#36, #42); proof budget 2M since #96 | 4: `tools/seeds_check.sh`, 990 proven, 10 rejected |
| 5 Assets | Done (#49): four Meshy assets, look A, provenance in `assets/PROVENANCE.md` | 5; 6: golden re-recorded |

All six concept criteria are met and gated by `tools/check.sh`. The owner
approved the look and confirmed the Meshy licence (#49).

Since the slice, as GitHub milestones:

| Milestone | Issues | Tag |
|---|---|---|
| 6 Consequences visible | Previews, undo, readable strikes, hints | v0.2.0 |
| 7 Terrain art | Textured terrain, water, toppers | v0.3.0 |
| 8 Depth | Marching crawlers (#71), the town falls at two (#96), Rammer measured and dropped (#72), harpoon pull (#73) | v0.4.0 |
| Engineering hygiene | Godot gates in CI, GDScript lint, leaks, crash repro (#74-#77) | n/a |
| Later | Sound (#78, approved by the owner), campaigns (#79) | v0.5.0 |

Open for the owner: **#80**, publishing a build to itch.io. That is
outward-facing, so it waits for the owner's go.

Revised since the plan: crawlers now go for the town, strike for 2 and move 3
(#19, #39), and march on it rather than chase engineers (#71). The town falls
when two of its three cottages are razed (#96). Engineers carry a harpoon that
pulls a unit two tiles away one tile closer (#73). The solver showed the original
rules never needed a shove, and a 10M-node sweep showed #71's still mostly
didn't: at least 559 of 1000 missions were winnable without one. Under #96 at
least 383 provably need one. The method and numbers are in `RULES.md`,
"Balance".

## Decisions taken

These were open questions at the end of bootstrap. Answered here so work does
not stall; the owner can veto any of them in an issue comment.

- **Engineer verbs for the slice: Move and Shove only.** A third tool (harpoon
  pull, portable sea wall) is post-slice. Reason: every verb multiplies the
  solver's branching factor, and criterion 4 is a proof obligation, not a
  nice-to-have. An engineer may move and then shove in the same turn; see
  `docs/RULES.md` for why.
- **Solver rigor: full proof with rejection.** A seed is accepted only when the
  search proves a surviving line. A seed whose search exhausts its node budget
  is reported `unproven` and rejected, never counted as fair.
- **Order: playable first.** Phases 1-3 produce something to click; the solver
  and the assets follow. The head-to-head is better served by a playable slice
  than by a provable empty board.

## Phase 0: freeze the rules

[`docs/RULES.md`](RULES.md) holds the exact numbers the tests assert
against. It supersedes the starting values sketched here at bootstrap.

*Evidence:* the document, and Phase 1's tests citing it.

## Phase 1: the game loop in `core/` (1, 2)

- `Mission` owns `Board`, turn counter and phase. `Command` in, `Event` out:
  the only way to drive or observe a mission.
- Commands: `Move`, `Shove`, `EndTurn`. Events are what the presentation layer
  animates.
- Turn order: player actions, resolve the intents telegraphed last turn, tide
  rises, spawns, then compute and publish next turn's intents. Perfect
  information falls out of publishing intents before the player acts.
- Crawler intent: fixed priority (attack an adjacent engineer, else an adjacent
  building, else step toward the nearest building), ties broken by a stated
  ordering, never by unordered iteration.
- New RNG streams, appended: `Deploy = 3`, `Spawns = 4`. The crawler AI uses none.
- Per-turn RNG is re-derived from `seed` and the turn number rather than stored,
  so `state_hash()` stays small and the solver can hash a position cheaply.
- Tests lead with edges: shoving a unit into water it cannot survive, intents
  aimed at something that died first, spawns onto occupied tiles, a building
  destroyed mid-resolution, the last engineer drowning on the final turn.

*Evidence:* `tools/check.sh` green; scripted 5-turn missions that reach a win
and a loss.

## Phase 2: replay and cross-compiler determinism (3)

- Serialize the command log; `seed` plus commands fully defines a mission.
- `bw_replay`: replay a log, print the state hash per turn.
- Gate: a committed log replays to identical hashes under Clang and GCC, debug
  and release. Criterion 3 becomes a test rather than an assertion.
- Soak: thousands of missions of legal random commands under ASan and UBSan.

*Evidence:* `tools/replay_check.sh` in `check.sh`; the per-turn hash table.

## Phase 3: playable in Godot (1, 2)

- Bridge grows `apply_command`, legal-action queries, unit and intent lists, an
  event drain, phase and outcome. Conversion and validation only; no rule
  leaves `core/`.
- Input: select an engineer, see reachable tiles and shove targets, act, end the
  turn. Intent arrows, tide readout, turn counter, win and loss banners.
- `game/tests/smoke.gd` drives a whole mission headless through the bridge.

*Evidence:* captures at turns 1, 3 and 5, reviewed; recorded replays of a win
and a loss. **The slice is playable here**; phases 4 and 5 are the acceptance
bar above playable.

## Phase 4: the solver (4)

- Depth-limited exhaustive search over turn sequences, transposition table keyed
  on `state_hash`, move ordering and dominance pruning, node budget per seed.
- Generation rejects any seed not proved survivable.
- Sweep seeds 0-999 in CI; two cheap seeds in `check.sh`.

*Evidence:* seed, verdict and nodes searched per seed, plus the rejection count.
If branching proves too wide, the fix is tightening Phase 0's numbers and saying
so, not weakening the claim.

## Phase 5: assets and the golden image (5, 6)

- Look first and cheaply: Meshy text-to-image concepts for the owner's pick
  before any 3D spend. Credit cost quoted before every paid call.
- Then four models (engineer, crawler, cottage, prop), Blender cleanup,
  `assets/PROVENANCE.md` entries.
- `tools/golden.sh` compares the opening board against a committed golden within
  tolerance, and joins `check.sh`.

## Checkpoints

Work does not stop anywhere else.

- **C1, art look.** Needs the owner's eye and spends credits. Concepts are
  surfaced during Phase 1 so the answer is in hand before Phase 5 needs it;
  everything else continues meanwhile.
- **C2, slice review.** Offered when Phase 3 lands. Not blocking: Phase 4
  starts unless the owner says otherwise.
