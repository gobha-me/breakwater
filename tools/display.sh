# Sourced by tools that render. Sets `display` to the command prefix and
# arguments that give Godot a display to render into, preferring, in order:
# xvfb-run (works with the screen locked), the current DISPLAY or
# WAYLAND_DISPLAY, and the desktop session's Wayland socket (over SSH, while
# the session is unlocked). Exits 2 with advice when there is none.
# shellcheck shell=bash
session_socket="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/wayland-0"
if command -v xvfb-run >/dev/null; then
    display=(xvfb-run -a -s "-screen 0 1280x720x24")
    display_args=(--display-driver x11)
elif [[ -n "${WAYLAND_DISPLAY:-}" || -n "${DISPLAY:-}" ]]; then
    display=()
    display_args=()
elif [[ -S "$session_socket" ]]; then
    display=(env WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR="$(dirname "$session_socket")")
    display_args=(--display-driver wayland)
else
    echo "no display: unlock the desktop session or install xvfb-run (Arch: xorg-server-xvfb)" >&2
    exit 2
fi
