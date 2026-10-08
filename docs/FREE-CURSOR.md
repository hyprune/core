# Suspended free cursor

With the Director closed, **Back** or **Super+Tab** toggles the real Hyprland
cursor. It works from walking, E-use, F-window/workspace focus and a carry.
Carry/resize finishes first. The camera (including gravity) freezes. A second
press restores the saved camera and world/use/focus target. From F focus the
first press shows the world; the second focuses the saved target again. A closed
target safely returns to world control.

The cursor crosses physical monitor boundaries without recapture. Layers,
notifications, bars, launchers and other outputs retain ordinary native pointer
input. World windows receive projected clicks/drags/scroll; empty world swallows
pointer input. Press ownership follows a drag across output and layer boundaries,
so its release reaches the original owner. Keyboard goes to the focused client;
only the two toggle gestures are intercepted. Native Hyprland binds still work.
The world reticle is hidden while the native cursor is visible.

Projected windows need their selected keyboard focus retained when Hyprland
switches keyboard devices: otherwise the first typed character was lost in the
nested virtual-keyboard test. The deferred forwarding path preserves that focus,
balances releases and calls the native keybind manager. Native layers and other
outputs keep ordinary routing. An inactive-world comparison typed the complete
command without this path; the regression now verifies the complete command in
free-cursor mode too.

IPC 0.12 publishes `interaction.mode` and `editor.mode` as `cursor`. Older
protocols receive their frozen `typing` / `interactive` equivalents. The shell
handoff requests **CURSOR · Back/Super+Tab to return**; shell implementation is
owned by the separate shell agent. See `CHANGES-FOR-SDK-AND-SHELL.md`. Canonical
schema, generated SDK and generated keymap have been updated.

## Verification

```sh
cmake --build build -j 3
ctest --test-dir build --output-on-failure
flock /tmp/worldshell-nested.lock python3 dev/nested/free_cursor.py
node tests/validate-replay-wire.mjs ../schema
```

The 20 nested checks cover entry/return paths, frozen camera, native keyboard,
projected drag across outputs, native layer drag onto the world, empty-world
swallowing, a closed return target and restoration of a colliding native bind on
deactivation. IPC 0.12 responses and notifications validate against the canonical
schema. `docs/evidence/free-cursor.json` records the checks. The shader/replay and
climbing suites are rerun on this combined core.

Cursor/focus/workspace mutations run after the frame. These automated checks use
a software cursor; hardware-cursor verification is still supervised. Every new
transition is listed in `docs/M3B.md` and the walking order in
`dev/live/LIVE-TEST.md` is updated.
