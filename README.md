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

## Quality gates

```sh
tools/check.sh   # format, ASan+UBSan, GCC, clang, clang-tidy, Godot smoke
```

## License

To be decided.
