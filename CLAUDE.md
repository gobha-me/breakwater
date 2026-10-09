# Breakwater

Deterministic turn-based coastal tactics: push sea creatures, outlast the
tide. Pitch and slice acceptance criteria: `docs/CONCEPT.md`.

This project runs head-to-head against another agent's project. The owner
reviews periodically and has a high acceptance bar: show evidence (gate output,
captures, replay hashes), never just claim completion.

## Layout

- `core/`: all game rules, pure C++23. No Godot includes. Deterministic.
- `extension/`: GDExtension bridge (godot-cpp 10.0.0-stable, API 4.7). Converts
  types and validates input only. No rules here.
- `game/`: Godot 4.7 project. Presentation and input only; GDScript stays thin.
- `tests/`: Catch2 v3 unit tests for `core/`.
- `tools/`: gate scripts. `docs/`: design and known issues.

## Gates (run before saying anything is done)

`tools/check.sh` runs all of them and stops at the first failure:

1. `tools/format.sh --check`: clang-format (run `tools/format.sh` to fix).
2. ASan+UBSan build and tests (`asan` preset).
3. GCC build and tests (`gcc` preset).
4. Clang dev build and tests, including the extension (`dev` preset).
5. `tools/lint.sh`: clang-tidy, warnings as errors.
6. `tools/replay_check.sh`: every log in `tests/logs/` replays to its committed
   `.hashes` under Clang and GCC, debug and release.
7. `tools/godot_smoke.sh`: headless Godot loads the extension and checks the bridge.

Suppressions: a disabled check in `.clang-tidy` carries its reason there; an
inline suppression names the exact check, with the reason on the line above
(clang-format wraps long NOLINT lines, which breaks NOLINTNEXTLINE).

## Rules for changes

- **Determinism.** Randomness only through `breakwater::Rng` streams
  (`core/include/breakwater/rng.hpp`). Stream ids are save/replay format: append,
  never renumber. No `std::` distributions, no unordered iteration feeding the
  state, no floating point in rules.
- **State hash.** New state must be folded into `Board::state_hash()` or
  `Mission::state_hash()`. The tests "state hash tracks every mutation" and
  "mission hash covers seed, turn and action states, not events" are the
  pattern to follow.
- **Replay logs.** `tests/logs/*.log` with `.hashes` beside them are goldens. A
  rule change that moves a hash is re-recorded with
  `build/dev/tools/replay/bw_replay <log> > <log>.hashes`, and the PR says why.
- Test edge cases (bounds, dead units, blocked pushes) before visuals.
- Pin dependency versions to releases at least two weeks old.

## Visual checks

`tools/capture.sh [out.png]` renders the main scene through the game's own
viewport (`game/scripts/capture.gd`), so no desktop screenshot permission is
needed. It needs `xvfb-run` (package `xorg-server-xvfb`) to work over SSH or
with the screen locked: KWin suspends hidden Wayland windows, so they never
render. Read the PNG to review it. Later it becomes the golden-image gate.

## Assets

Meshy (MCP) for generation, Blender (MCP) for cleanup. Every asset gets an entry
in `assets/PROVENANCE.md`: prompt, Meshy task id, date, edits, license. Get the
owner's pick on a look before polishing it.

## Known issues

`docs/KNOWN_ISSUES.md`. Notably, Godot 4.7.2 crashes on a first-time headless
import that discovers the extension; `tools/godot_smoke.sh` works around it.

## Workflow

Work travels one issue at a time, and the loop never pauses for approval except
at a checkpoint named in `docs/ROADMAP.md`.

1. **Open an issue** before touching code: the outcome, the acceptance criteria,
   and the evidence that will prove them. Label it with a phase milestone and an
   `area:` label.
2. **Branch** from up-to-date `main`, named `<issue-number>-<slug>`. `main`
   never takes a direct commit.
3. **Work, test, fix** until `tools/check.sh` passes locally.
4. **Push and open a PR** that closes the issue (`Closes #N`) and carries the
   evidence in its body: gate output, replay hashes, capture images.
5. **Merge on green**: CI green plus the local-only gates (Godot smoke,
   captures) shown in the PR. Squash merge, delete the branch.
6. **Next issue.** No idling between issues.

CI runs the gates that need no display: format, ASan and UBSan, GCC, Clang,
clang-tidy, replay determinism. The Godot smoke test and captures need a Godot binary and a display,
so they stay local and their output belongs in the PR body.

A decision that comes up mid-issue gets made and recorded in the issue, not
escalated. Escalate only a checkpoint, or a discovery that invalidates the
roadmap.

## Build quick reference

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
tools/check.sh
godot --path game        # run the game (needs a display)
```
