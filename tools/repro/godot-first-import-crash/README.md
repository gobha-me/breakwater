# Godot 4.7.2: first headless import that discovers a GDExtension crashes at exit

A minimal reproduction for an engine bug that `docs/KNOWN_ISSUES.md` works
around. It is already reported upstream for Windows as
[godotengine/godot#123511](https://github.com/godotengine/godot/issues/123511),
which was closed as *not planned*. The reason given: the report didn't follow
the bug template, and Godot doesn't accept AI-generated reports. So nothing
here has been filed. The draft below is for a person to verify, rewrite in
their own words, and file using Godot's bug template.

## Measured (2026-10-13)

On the official 4.7.2 Linux build (`4.7.2.stable.official.ed1daf0bf`), CachyOS,
Linux 7.2.8, x86_64. `./run.sh <godot> 5`:

| `.godot/` before `--headless --import` | Exit |
|---|---|
| absent (clean first import) | **134 (SIGABRT), 5/5** |
| absent, but `extension_list.cfg` pre-seeded with the extension | 0, 3/3 |
| absent, and the project has no `.gdextension` at all | 0, 2/2 |

The CachyOS package build of 4.7.2 behaves the same. The extension is one
empty `RefCounted` subclass registered at `MODULE_INITIALIZATION_LEVEL_SCENE`
(godot-cpp `10.0.0-stable`), with nothing else in the project. The editor's
log reaches "Loading docks..." and then the process segfaults; Godot's handler
aborts with no backtrace. The main thread's frames from the core dump of the
stripped official binary (useful only with matching symbols):

```text
#3  0x4fa292  (crash handler -> abort)
#4  <signal handler called>
#5  0x1c7ae9f
#6  0x1c7c6f4
#7  0x1c84e75
#8  0x4e6c64d
#9  0x4963714
#10 0x42d046  (main)
```

## Draft for the upstream report

**Tested versions:** reproducible in 4.7.2.stable.official, and in a 4.7.2
Linux distribution build.

**System information:** Linux x86_64, `--headless`, so no renderer is
involved.

**Issue description:** a headless editor session whose project contains a
GDExtension not yet listed in `.godot/extension_list.cfg` crashes at shutdown.
Pre-seeding `extension_list.cfg` avoids it, and so does a second run.
Matches #123511 (Windows).

**Steps to reproduce:**

1. Build the extension: `cmake -S . -B out -G Ninja && cmake --build out`.
2. `rm -rf project/.godot`
3. `godot --headless --path project --import`. The exit code is 134.

**Minimal reproduction project:** this directory, `CMakeLists.txt`,
`src/repro.cpp` and `project/`.
