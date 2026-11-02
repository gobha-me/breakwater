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

## Play

```sh
tools/play.sh            # build what's missing and play (default mission 20)
tools/play.sh 42         # a specific mission; unproven seeds move on to the next
tools/play.sh --campaign 3  # campaign 3: five missions in a row, scored
tools/export.sh          # standalone Linux release build in build/export/
tools/play.sh --exported # play the exported build
```

Mouse: click an engineer, then a green tile to move, an orange unit to shove,
or a teal unit two tiles away to harpoon it one tile closer (hover first to
preview the outcome). Keys: WASD shove, Shift+WASD pull, Space end turn,
Z undo a move, Tab next engineer, R restart, N next mission, F5 save a replay,
M sound on or off, - and = volume.

Campaigns: C starts the campaign this mission belongs to (five missions in a
row, scored), N moves on after a win, R retries a mission, F5 saves the whole
campaign, Shift+C goes back to free play. `tools/play.sh --campaign 3` starts
campaign 3.

## Build

```sh
cmake --preset dev && cmake --build --preset dev   # core, tests, extension
ctest --preset dev
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

The golden harness pins both `VK_ICD_FILENAMES` and `VK_DRIVER_FILES` to
`/usr/share/vulkan/icd.d/lvp_icd.json` for a software Vulkan baseline (Mesa
lavapipe), overriding inherited loader settings. If that file is missing or
unreadable, the harness stops before rendering and asks Infra Lead to
reinstall `mesa-vulkan-drivers`. Infra Lead also supplies `vulkan-tools` for
renderer diagnostics. Run:

```sh
export LP_NUM_THREADS=2
GODOT_BIN="$(tools/fetch_godot.sh)" tools/golden.sh
```

The same pin applies to `tools/golden.sh --update` and `tools/check.sh`.
The software adapter reports itself as `llvmpipe`; the selected lvp ICD makes
this Vulkan (lavapipe), rather than the OpenGL llvmpipe fallback. Record
`vulkaninfo --summary` and the adapter line in `build/golden/capture.log`
when updating the baseline. The recorded baseline uses official Godot 4.7.2,
Mesa 25.2.8 and LLVM 20.1.2; changing those versions can change pixels.

## License

BSD 3-Clause; see [LICENSE](LICENSE). This covers the code and the assets in
`game/assets/` (provenance in [assets/PROVENANCE.md](assets/PROVENANCE.md)).

Fetched at build time, under their own licences: godot-cpp (MIT) and
Catch2 (BSL-1.0). Godot itself is MIT.
