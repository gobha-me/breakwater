# Breakwater

A deterministic, turn-based coastal tactics game. Push sea creatures around,
protect the town, and outlast the rising tide. See [the concept](docs/CONCEPT.md).

Rules are pure C++23 (`core/`), exposed to Godot 4.7 through a GDExtension
(`extension/`) and presented by the Godot project in `game/`.

## Requirements

- CMake 3.28+, Ninja, Clang and GCC with C++23 support
- Godot 4.7 (`godot` on `PATH`, or set `GODOT_BIN`)
- clang-tidy and clang-format
- Optional: `xvfb-run` for headless visual captures

The first configure fetches Catch2 v3.16.0 and godot-cpp 10.0.0-stable.

## Build and run

```sh
cmake --preset dev && cmake --build --preset dev   # core, tests, extension
ctest --preset dev
godot --path game
```

## Replays

A mission is a seed plus a command log (`tests/logs/*.log`).
`build/dev/tools/replay/bw_replay <log>` prints the state hash after setup and
after every turn; the same log prints the same hashes on every compiler and
build type.

## Quality gates

```sh
tools/check.sh   # format, ASan+UBSan, GCC, clang, clang-tidy, replay, Godot smoke
```

## License

To be decided.
