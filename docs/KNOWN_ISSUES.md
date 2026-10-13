# Known issues

## Godot segfaults at exit on a first-time headless import

- **Seen:** 2026-10-09, Godot 4.7.2 (`4.7.2.stable.arch_linux.ed1daf0bf`, CachyOS package), godot-cpp 10.0.0-stable.
- **Symptom:** `godot --headless --path game --import` with no `game/.godot/` exits 134 (SIGSEGV → abort) after the import finishes.
- **Bisected:**
  - clean import with the extension: crashes 3/3
  - clean import with `bin/breakwater.gdextension` moved away: passes
  - clean import of an empty project: passes
  - warm import (where `.godot/extension_list.cfg` already lists the extension): passes
  - clean import with `extension_list.cfg` pre-seeded: passes 3/3
  - `reloadable = false`: no change
- **Reading:** a shallow stack from `main()` with no extension frames suggests engine shutdown after the editor hot-discovered the extension mid-scan. Unconfirmed: the package ships without symbols (debuginfod had none).
- **Workaround:** `tools/godot_smoke.sh` pre-seeds `extension_list.cfg` before importing.
- **Retested 2026-10-13 (#74, #77)** on the official 4.7.2 build
  (`4.7.2.stable.official.ed1daf0bf`, from `tools/fetch_godot.sh`) as well as
  the CachyOS one:
  - **Bare project** (only `project.godot` plus the extension): a clean
    first-time import aborts at exit, both with the bootstrap-era extension and
    with today's.
  - **`game/`**: with today's extension a clean first-time import passes 3/3,
    while at #35 it aborted 3/3.

  So the trigger isn't a particular class of ours. It is the first import that
  discovers a GDExtension mid-scan, and whether it bites depends on the
  project's shape and scan timing. Godot builds aren't at fault one way or
  the other: the official build behaves the same. A minimal reproduction with
  a one-class extension is the next step (#77).
- **One more data point:** the first import after **switching** Godot builds
  over the same `.godot/` cache aborts once, in either direction, and every
  rerun passes. CI uses one build from a clean start. `tools/godot_smoke.sh`
  retries the import once, so a switch on a dev machine doesn't fail the gate.
- **Workaround:** kept, since it is harmless. `tools/godot_smoke.sh` still
  pre-seeds `extension_list.cfg`.
