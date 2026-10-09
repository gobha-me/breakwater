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
6. `tools/godot_smoke.sh`: headless Godot loads the extension and checks the bridge.

Suppressions: a disabled check in `.clang-tidy` carries its reason there; an
inline suppression names the exact check, with the reason on the line above
(clang-format wraps long NOLINT lines, which breaks NOLINTNEXTLINE).

## Rules for changes

- **Determinism.** Randomness only through `breakwater::Rng` streams
  (`core/include/breakwater/rng.hpp`). Stream ids are save/replay format: append,
  never renumber. No `std::` distributions, no unordered iteration feeding the
  state, no floating point in rules.
- **State hash.** New state must be folded into `Board::state_hash()`. The test
  "state hash tracks every mutation" is the pattern to follow.
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

## Build quick reference

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
tools/check.sh
godot --path game        # run the game (needs a display)
```
