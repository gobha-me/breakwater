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
- **To do:** retest with an official Godot build or one with symbols, and report upstream if it reproduces there.
