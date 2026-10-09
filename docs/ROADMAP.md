# Roadmap: bootstrap to vertical slice

Phases run in order. Each ends with evidence, not a claim. The acceptance
criteria being chased are in [the concept](CONCEPT.md); the numbers in
parentheses say which criterion a phase closes.

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
