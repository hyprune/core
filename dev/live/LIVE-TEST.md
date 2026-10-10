# Supervised M3B live test

**Owner + supervisor run live commands; agents use nested only. Keep a TTY ready.**
No installed config is edited. Preflight builds Release and queries the session
matching `WAYLAND_DISPLAY` in `hyprctl instances -j`, ignoring stale signatures.
The launcher derives its offers from core’s protocol helper; the shell uses the
newest common installed protocol. Preflight and launcher.log report both versions.

```bash
cd /home/rick/workspace/hyprune/core
dev/live/preflight
dev/live/up                 # Note the printed toggle and Recovery state path.
dev/live/status
```

To install multiple packages for this test, repeat `--world PACKAGE_DIR` on both
preflight and up. The first starts; every selected world appears in Director →
Places and can be loaded through its destination button. Directories need not
match their manifest IDs; duplicate IDs and invalid packages are rejected before
the session swap. Preflight only validates/builds/queries; it does not install
packages or change the session. `status` shows the recorded selection.

```bash
dev/live/preflight --world ../worlds/dist/com.hyprune.lumen-reach --world ../worlds/dist/com.hyprune.switchyard
dev/live/up --world ../worlds/dist/com.hyprune.lumen-reach --world ../worlds/dist/com.hyprune.switchyard
```

Packages are copied into the private runtime, and all copied manifests are passed
to the shell. Each world's optional HUD style follows the active world when the
shell's World Styles preference is enabled. `down` needs no world arguments.

`up` records plugin paths and old HUD argv/environment/cwd/PID identities in
`$XDG_STATE_HOME/hyprune/live/state.json` (default `~/.local/state/hyprune/live/state.json`).
It unloads hypr3d, stops only `qs -p .../quickshell/hypr3d`, and preserves
`qs -c rick`/other instances. Core loads from a unique copy, starts its helper
and detached shell at the newest common IPC version, then loads the first selected world (Lumen Reach by default)
without activating it. A Hyprland notification names the actual toggle chord;
press it to enter when ready.
Tokens stay in memory/pipes; logs are `launcher.log`, `hypruned.log`, `shell.log`.
Default input/editor/layout state belongs to this test's private runtime directory;
Settings edits only that copy. Bootstrap environment is restored before init returns.

Toggle: **Super+Shift+H**, otherwise N/F10/F11/F8 with those modifiers; up checks
binds after load and prints its choice. It conservatively reserves matching keys
across modifiers because Hyprland's bind JSON omits ignore-mods flags.
Hyprland itself **re-evaluates Lua on plugin load/unload**; scripts issue no reload
command. If hypr3d reappears, up rolls back. Avoid manual reload during testing:
it removes runtime binds.

Walking checklist ([full M3B detail](../../docs/M3B.md#supervised-live-test-checklist)):

- [ ] Desktop → world: native cursor hides, reticle appears; main shell stays alive.
- [ ] Super+Enter opens a terminal in view, clear of geometry; it survives toggling out/back and appears in Workspaces.
- [ ] Back or Super+Tab in world → real cursor: click launcher/bar and projected world windows; empty world swallows clicks. Either toggle returns to reticle.
- [ ] Free cursor hides core ladder/window/tool interaction hints; the active waypoint and route remain visible. Returning restores the applicable hints.
- [ ] E window: stationary camera, relative cursor, hover/click/scroll/typing reach root and popup.
- [ ] Cursor off world output: deferred warp/show gives free native cursor; returning hides it and restores relative pointer.
- [ ] E → Super+Tab/Back: direct return to world control/reticle. Press again to enter free cursor; press once more to return.
- [ ] F window: cursor hidden through zoom, visible/centred at 2D handover; Super+Tab/Back exits fullscreen to world/reticle.
- [ ] E wall: stationary camera, deferred workspace/focus switch, relative clicks/typing. Test both exits.
- [ ] F wall: hidden through fly-in, visible at handover; Back/Super+Tab flies out to world/reticle.
- [ ] Carry release near a main wall: carry a window (Super+LMB) a few metres in front of an area's main wall and release; it stays placed where you left it. Push it right up to the wall (or pull a client off a wall and release at once) and it drops into that workspace, tiled on the wall. No cursor flash either way.
- [ ] Default homes (Lumen Reach: 21 sun-court, 22 array-hall, 23 rime-vault, 24 signal-rise; room is 20): in world, your normal workspace keybind to 21–24 travels there with the reticle kept and no cursor flash; a window opened there shows on that area's main wall. Back to 20 (room) and to the same home again travels again. From an E/F wall the keybind also travels (leaves 2D); from free cursor, travel waits until you return to the reticle. Watch for any cursor flash.
- [ ] Super+Tab never opens native overview while world/use/focus/free cursor is active, including release; it does when inactive. Other native binds keep working; free cursor intercepts Back/Super+Tab and Escape when no app has keyboard focus.
- [ ] TAB hold → Map: release grabs, show cursor/focus Director after frame. Settings captures chords/buttons/wheel; Esc cancels/leaves menu, clears focus, restores reticle.
- [ ] Workspaces lists real desktops/apps even without configured homes. Click Help; alternate Help/Workspaces/Settings several times and keep the menu open.
- [ ] Placement (5): wheel selects, LMB places ahead, RMB mounts, MMB pockets. Queue remains invisible except the selected ghost. Generic HUD hints follow slot rebindings.
- [ ] R during carry toggles facing, including with Super held; Super+Shift+Wheel changes client resolution while quad size stays fixed; Super+Ctrl+Wheel rotates; Super+Shift+MMB resets.
- [ ] Q scope/vignette, tool/submodes, lock and gizmo: overlays only, no cursor shape/visibility changes. Try carry/resize and Shift snap.
- [ ] Reload test input during hold/grab: balanced releases; settings-only edits preserve hold. Esc leaves Settings.
- [ ] Lock/unlock: clear owned input, restore cursor policy, resume safely.
- [ ] Toggle off/on: restore focus/workspace/visibility; native binds work while inactive. U before/after suspect behavior; save notice path.
- [ ] If failed load/output loss/helper death occurs, check release/visibility recovery; do not induce a compositor crash.
- [ ] Down: deferred exit/panic visibility reset precedes unload; old plugin/HUD return, toggle disappears, overview works. Second down creates no HUD or cursor transition.

```bash
dev/live/status
dev/live/down
# Panic from TTY, without Wayland variables (safe to repeat):
/home/rick/workspace/hyprune/core/dev/live/down
```

**Crash:** switch to TTY, run down. It stops recorded test processes, keeps state
and reports recovery-pending when no compositor is available. Restart Hyprland
normally; run down again. It finds the recorded socket or its replacement with
the same Wayland socket. If ambiguous, inspect `hyprctl instances -j` and add
`--instance SIGNATURE`. Run as owner, never sudo. For custom XDG state locations,
add `--state-dir /absolute/path/to/hyprune/live` (Recovery state's parent).

**Capture:** U dumps in `$XDG_STATE_HOME/hyprune/captures/`, live logs/state, and
`~/.cache/hyprland/hyprlandCrashReport*.txt` (or `$XDG_CACHE_HOME/hyprland`). Status
also capture `~/.cache/quickshell/crashes/` and `coredumpctl info PID` for a shell crash. Status
shows each log's last 20 lines and new/updated reports since up. Include failed
tick, time, output, chord and preflight ABI output.

Nested proof: `./dev/nested/run.sh --live`, under lifetime
`flock /tmp/worldshell-nested.lock`, uses explicit overrides and inert old-plugin/
isolated HUD fixtures; checks healthy down, helper SIGKILL, TTY and repeated down.


Workspace/IPC retry additions: `up` now uses IPC 0.8 (shell 98b677a or later).
World entry switches the selected output to dedicated workspace 20 and enables
the scoped pre-layout floating policy. No Hyprland config edit is required. Check that
Super+Enter opens a floating terminal there, and toggle out restores the previous
workspace without changing its tiled layout. TAB hold should animate and open the
exclusive Director; test Graphics Settings and Escape. `down` disables that policy
and restores the workspace before unloading. To choose a different room manually,
use `hl.plugin.hyprune.config({windows={world_workspace=18}})`; the change applies
on the next activation. Follow the cursor checks in `docs/M3B.md`.

Multiworld regression: `python3 dev/live/test_live.py` and
`HYPRUNE_LIVE_WORLDS=1 dev/nested/run.sh --live` (the wrapper owns the flock).
The nested test uses temporary Lumen Reach/Switchyard copies with distinct HUD
styles, starts with the non-default world, and clicks the actual Places buttons
in both directions. It uses grim/tesseract to locate the visible controls.
Evidence: [live-worlds.json](../../docs/evidence/live-worlds.json).

The bootstrap supervisor now remains running to renew the shell bridge's
credentials after a transient disconnect. The inherited duplex channel accepts
`renew` lines only from the exact launcher child (kernel PID/UID credentials).
Each fresh token is scoped to that launch's capabilities, PID-bound, one-use,
and expires after 30 seconds. Limits are 8 renewals/second, 4096 per launch and
64 outstanding grants. Tokens never enter argv, QML, state files or logs.
`down` also stops this supervisor; no config-file changes are needed.

For a nested debug run, `HYPRUNE_VALIDATE_IPC=1` enables full negotiated-schema
checks on every outgoing frame in hypruned. Validation runs on a separate
worker, away from the compositor/render pass. A bad frame aborts the test helper
and reports only method, field path and schema keyword. Ordinary Release live
runs leave this off. `dev/nested/run.sh` enables it automatically.

## Render on demand (perf/render-standard)

Core now renders a world frame only when its inputs change (camera, windows'
commits, overlays, settings); Hyprland repaints in between re-present the cached
frame. No cursor/focus/workspace calls were added. Supervised checks:

- [ ] Stand still in Lumen Reach with F3 closed: the world image stays correct and
  stable; nothing freezes when you start moving or turning again.
- [ ] A video (or `watch -n0.1 date`) on a room window and on a mounted workspace
  wall keeps updating while the camera is still.
- [ ] HUD animations, the TAB hold ring and the Director still animate while still.
- [ ] Typing in a window (E): the virtual cursor and the text update while still.
- [ ] U while idle saves the current frame; F3 shows live numbers (it forces
  rendering) and closing it returns to idle.
- [ ] Optional: `HYPRUNE_CONTINUOUS_RENDER=1` restores per-tick frames if
  anything above fails; `HYPRUNE_UPSCALER=fsr1` tries the FSR 1 upscaler.

## Capture notes (IPC 0.17)

U saves the capture immediately; a 0.17 shell then shows a one-line "Why?" prompt
holding exclusive keyboard input, like the Director. Core adds no cursor, focus or
workspace calls of its own: the prompt reuses the Director's deferred keyboard
focus and menu-mode cursor path. Nested runs use a software cursor, so check the
hardware cursor here:

- [ ] Walking in Lumen Reach, press U: the prompt appears at the bottom; no crash,
  no flicker. Note what the hardware cursor does while the prompt is open (menu
  mode, like the Director) and that it is gone again after Enter.
- [ ] Type a note including w/a/s/d and space: the world does not move. Enter: the
  reticle is back and W walks immediately (no lost or stuck keys).
- [ ] U, type, Esc: the prompt closes and nothing is written; walking resumes.
- [ ] U, then press and hold W while the prompt is still open, Enter with W held:
  walking starts without re-pressing W. Then U again and type at once: the first
  character appears (nested wtype drops it after this sequence; check a real
  keyboard).
- [ ] U, type, Super+U, type, Enter: two captures, both notes in
  `~/.local/state/hyprune/captures/notes.md` in order.
- [ ] Super+U while typing in a window (E), in F focus and in the free cursor:
  capture only, no prompt, the mode is unchanged.
- [ ] Hold Tab while the prompt is open: the Director replaces it (typed text is
  kept as the note); Esc closes the Director as usual.

## Static world and carrying from behind

World package geometry no longer moves with gestures; a window can be carried
from behind. No new cursor, focus or workspace calls (the carry press already
runs in the deferred input path).

- [ ] In Lumen Reach aim at a building (e.g. the Sun Court hall) and try
  Super+Wheel, Super+Ctrl+Wheel, Super+Shift+Wheel, Super+LMB/RMB drags and the
  Position tool (4): nothing moves, and walking into the building still collides.
- [ ] After loading, a building moved before this fix is back in place.
- [ ] Walk behind a window and Super+click it: it turns to face you smoothly
  (immediately with reduced motion), the spot you grabbed stays under the
  reticle, and it drags like a front grab. The hardware cursor does not appear
  or flicker during the turn; no crash.
- [ ] A plain click on the back of a window does nothing.
