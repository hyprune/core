# Supervised M3B live test

**Owner + supervisor run live commands; agents use nested only. Keep a TTY ready.**
No installed config is edited. Preflight builds Release and queries the session
matching `WAYLAND_DISPLAY` in `hyprctl instances -j`, ignoring stale signatures.
The launcher derives its offers from core’s protocol helper; the shell uses the
newest common installed protocol. Preflight and launcher.log report both versions.

```bash
cd path/to/core   # your core checkout
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
It replaces only what you configure, from the environment or
`~/.config/hyprune/dev.env`: `HYPRUNE_LIVE_REPLACE_PLUGIN` (a substring of a loaded
plugin's name to unload) and `HYPRUNE_LIVE_REPLACE_HUD` (the Quickshell config path
suffix of a HUD to stop, e.g. `quickshell/myhud` for `qs -p ~/.config/quickshell/myhud`).
Unset, nothing is unloaded or stopped; other Quickshell instances always keep
running. `down` restores exactly what `up` replaced. Core loads from a unique copy, starts its helper
and detached shell at the newest common IPC version, then loads the first selected world (Lumen Reach by default)
without activating it. A Hyprland notification names the actual toggle chord;
press it to enter when ready.
When `$XDG_CONFIG_HOME/hyprune/sources.json` exists, `up` also grants `data.publish` to
`com.hyprune.sources` and starts `dev/machines/source-helper.py` right after the shell
(within the launch token's 30 s), renewing its grant like the shell's; it feeds placed
gauges (see `dev/machines/README.md`, "Gauges: wire a script") and `down` stops it.
Tokens stay in memory/pipes; logs are `launcher.log`, `hypruned.log`, `shell.log` and,
with sources, `source-helper.log`.
Default input/editor/layout state belongs to this test's private runtime directory;
Settings edits only that copy. Bootstrap environment is restored before init returns.

Toggle: **Super+Shift+H**, otherwise N/F10/F11/F8 with those modifiers; up checks
binds after load and prints its choice. It conservatively reserves matching keys
across modifiers because Hyprland's bind JSON omits ignore-mods flags.
Hyprland itself **re-evaluates Lua on plugin load/unload**; scripts issue no reload
command. If the replaced plugin reappears, up rolls back. Avoid manual reload during testing:
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
- [ ] Failed frames: with the world active, turn a second monitor off/on (DPMS) or replug it. The HUD keeps updating and the Director still opens; `journalctl`/stderr shows at most one "render pass ended without RENDER_POST" line.
- [ ] Walls and mounts: an area wall shows your wallpaper behind its windows (empty workspace: the Hyprune placeholder). With a pocketed window (tool 5) aim at a lectern/kiosk screen: it lights up and the ghost sits in the frame; place: the window fills the frame flush. Carry a window onto a screen and release: same. No cursor flash.
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
/path/to/core/dev/live/down   # absolute path to your checkout
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

## Motion feel: presentation-driven tick, pacing, native walls

The tick now runs right after each presented frame (deferred with doLater, never in the
render pass) and moves by the frame's real time; the 16 ms timer only covers idle. Nested
tests cannot show hardware-cursor or real-DRM presentation behaviour:

- [ ] Walk and turn in Lumen Reach: motion is even, nothing stalls when you stop and start
  again (idle switches to the timer and back), the hardware cursor never flickers.
- [ ] Turn quickly at the default settings and with `target_fps` 30 (Settings, Performance):
  30 looks steady (no stutter), 60 stays smooth.
- [ ] Turn the monitor off (DPMS) and on, and unplug/replug a second monitor while the
  world is active: no freeze or crash; walking still works afterwards.
- [ ] Walls with windows (e.g. a terminal on the Sun Court wall) read as crisp up close as
  on the desktop; moving across the court does not shimmer.

## Player settings, view bob, reticle (IPC 0.19)

The reticle is now its own small pass at the end of `present()` (scissored, blend and
scissor state restored, no cursor/focus/workspace calls); the bob is a view-only offset.
Nested uses a software cursor and a headless output, so check on the real monitor:

- [ ] The reticle is crisp at your monitor scale (no blur, no shimmer while turning) in
  every style (Settings, Reticle); the outline keeps it visible on sky and on white windows.
- [ ] Walk in Lumen Reach: a subtle bob, calm when running (Shift), gone in flight, while
  climbing and with Reduced motion; stopping never snaps. Try Settings, Movement: off /
  subtle / normal, FOV 50-120, walk and run speeds apply immediately.
- [ ] Tool 5 / Super on windows near and far: green with corner brackets only within reach,
  dim with a distance below beyond it (and the click does nothing but a notice), an x on
  an occupied spot. The hardware cursor never appears or flickers while the reticle changes.
- [ ] E into a window and back, Super+Tab free cursor and back: the reticle hides and returns
  with no cursor flicker.

## Windows into the world, use-mode gestures, Too far (2026-10-11)

Windows brought into the world are floated, resized and moved by core (Hyprland float
/ resize / move actions from the tick, never inside a hook or the render pass). Nested
has no DRM or hardware cursor, so check on the real session:

- [ ] Drag a tiled Chrome window from the other monitor into the world (Super+LMB native
  drag): it appears in front of you at a sensible size; Super+RMB / Super+LMB on its edge
  resizes it and it stays; nothing flickers (cursor, other windows) while it floats.
- [ ] Recall (Director) and pull windows from a home wall, pocket and place one with tool
  5, carry one off its wall: each stays visible through resizes; return one to its wall
  and it is tiled there again.
- [ ] E into a window, Super+LMB on its edge resizes, Super+LMB in the middle carries,
  Super+Wheel pushes/pulls; afterwards you are typing in the same window again. The HUD
  is hidden while in E and F and comes back on exit.
- [ ] Super+click a window far away: the reticle turns the warning colour with "Too far:
  N m" and a notice says so; same for E beyond the use distance.

## Spawns, home arrivals, hidden-home windows, world switches (2026-10-11)

Workspace switches touching world workspaces now finish Hyprland's slide/fade from the
workspace event (deferred if a render pass is active). Hidden home windows keep receiving
frames. Nested can't show hardware cursor or real-output behaviour, so check on the real
session:

- [ ] Super+Enter in the Sun Court plaza and in a hall: the terminal appears ahead of you,
  over floor you can reach, never in mid-air or off a terrace edge.
- [ ] Launch Chrome normally (into your running browser, no flags) and move it to an area
  home: it paints live on that wall, with one output and with two outputs.
- [ ] Move two or three windows onto one home workspace: the first takes the wall, the next
  ones take the area's tool anchors in order. Focus into the wall and back: nothing swaps,
  no "Relocated" notice.
- [ ] Warp between areas, Super+1..9 between homes, focus into a wall and back, enter and
  leave the world: no slide or fade, and no cursor flicker or crash. A normal 1 -> 2 switch
  on your desktop still animates with your own settings.

## Position tool: Gizmo 2.0 (tool 4)

Handles, hover highlight, ghost and readouts are core overlays drawn inside the world
frame; drags move a virtual pointer, never the native one, and every key effect (typed
values, undo, drop, pivot) runs deferred from input, not in the render pass. No step
should change cursor visibility or shape. Nested runs use a software cursor, so check on
the real session (`HYPRUNE_GIZMO_OBJECTS=1 HYPRUNE_GIZMO_UNIVERSAL=1` in the compositor
environment enables world objects and the universal mode):

- [ ] Tool 4, aim at a window and click: handles appear at a constant size; turn and walk
  around it. Hovering a handle brightens it and the reticle tightens; the native cursor
  stays hidden and does not flicker.
- [ ] Press on an arrow and drag: the reticle follows the virtual pointer off-centre, the
  guide line and readout appear; release: the reticle snaps back to the centre with no
  native cursor flash. Repeat with a ring, a plane square, the centre disc and the outer
  view ring, with Shift held (ticks) and without.
- [ ] Drag until the pointer reaches each screen edge and release there: no cursor appears
  on the other monitor, no warp, no focus change.
- [ ] Start a drag, type `1.5`, release the button, press Enter: exact value applied. Start
  another, press Escape: cancelled. Neither moves the native cursor or changes focus.
- [ ] Scale a window from its X box: the client resizes during the drag without flicker;
  Ctrl+Z restores its size, Ctrl+Shift+Z re-applies it.
- [ ] Wheel through move / rotate / scale (/ all), toggle local axes (RMB) and the pivot
  (period): overlays only.
- [ ] With world objects enabled, select the Sun Court armillary; drag it down into the
  pedestal: it stays put, a red ghost and BLOCKED readout show, release refuses with a
  notice. End drops it, Shift+End aligns it to the aimed surface.
- [ ] Super+LMB carry the armillary into a wall and release: it stays at its last valid
  place. During every drag above, open the Director (TAB hold): the drag is cancelled
  cleanly and the cursor policy is the Director's.

## TAB tap and hold, markers, focus sessions (IPC 0.21)

A short TAB tap now reveals markers; the hold still opens the Director. Markers draw in
the present pass under the GL guard; no cursor, focus or workspace call was added. Focus
credit reads only the focused window's app class and workspace. Nested has a software
cursor and a headless output, so check on the real session:

- [ ] Tap TAB while walking (and while sprinting with Shift): markers fade in, hold about
  4 s and fade out; the hold ring never flashes on a tap; the hardware cursor never shows.
- [ ] Hold TAB: the ring fills after a short delay and the Director opens on the Map; no
  markers flash first. Release before the ring completes: only the markers.
- [ ] Tap TAB, then press E (use) or F (focus) on a window, or Super+Tab (free cursor)
  while markers show: they disappear at once, the cursor transition is clean (no flicker,
  no crash), and coming back shows no stale markers.
- [ ] Track a personal quest with a target behind you: a small arrow at the screen edge;
  turn towards it: a small diamond. No marker ever draws over the window you aim at.
- [ ] Start a short focus session on a workspace from the Quests page: the HUD timer
  counts down; typing in that workspace's window on the desktop credits; pause, skip and
  stop work from the HUD/Director.

## Deferred input runs at the start of the world tick (replay determinism)

Input that the compositor's handlers defer (button actions, free-cursor and Use
transitions, captures) now also runs at the start of the next world tick, still from an
idle callback outside the render pass, in arrival order; before, a tick queued earlier
could run first. Nested has a software cursor, so check on the real session:

- [ ] Super+Tab (free cursor) in and out several times while walking, and Back to return:
  the hardware cursor shows and hides once per toggle, no flicker, no crash.
- [ ] E (use) into a window and Super+Tab out, quickly and repeatedly: typing lands in the
  window, the cursor transition is clean.
- [ ] Hold W, tap D, release both: movement starts and stops on the frame you expect (no
  lag or extra step), and LMB/RMB tool actions respond on the press.

## Path tool: point drag, hover, visibility (tool 3)

Paths now draw only in tool 3 (period keeps them shown elsewhere). In tool 3 the aimed
point highlights; LMB pressed on it and dragged moves it with the reticle (no cursor is
shown or hidden; the drag is the reticle), release commits; Ctrl+Z / Ctrl+Shift+Z undo
and redo. Nested has a software cursor, so check on the real session:

- [ ] Tool 1: no paths drawn. Tool 3: every path with points; aim at a point: it grows
  and turns white.
- [ ] LMB on a point, drag across the floor and onto a step, release: the point follows,
  sits on the floor, the hardware cursor never appears; Shift while dragging snaps.
- [ ] Ctrl+Z while in tool 3 undoes the move without crouching or flying down; Ctrl+Shift+Z
  redoes it. Escape mid-drag puts the point back.
- [ ] RMB on a middle point removes that point; LMB on a segment inserts one there.
- [ ] Switch tools (1, 4, 5) in the middle of a drag: the drag cancels cleanly, no stuck
  button, no crash.

## Session lock and display power: the HUD comes back (core 0.8.3)

Locking left world mode, but core refused the shell's lease release while locked (every
method was refused), so after the unlock the shell's new claim was refused ("output already
leased") and it had no HUD. Core now takes `shell.release` while locked and treats a repeat
claim from the lease holder as a renewal; the shell retries its claim on the next entry. The
lock transition itself adds no cursor or surface work (world mode exits as before, from the
tick). Nested runs a real ext-session-lock client and the DPMS dispatcher
(`dev/nested/run.sh --lock`) but has a software cursor and no real display, so check on the
real session:

- [ ] In world mode, lock with your lock combo; the screen may turn off. Unlock. On the
  desktop the cursor is normal (shown, the right shape) and native binds work; no crash.
- [ ] Super+Shift+H: the world shows with the HUD (tool deck, compass, hints). Hold TAB: the
  Director opens and draws; Esc closes it and the reticle returns. The shell log has no
  "already leased".
- [ ] Lock again, wait until the display powers off (or `hyprctl dispatch dpms off` from a
  TTY-safe binding), wake it, unlock: the same checks.
- [ ] Without locking, in world mode: `dpms off`, wait 5 s, `dpms on`. The world and the
  HUD come back without re-entering; the Director opens; the cursor stays hidden in world
  control and shows/hides once for Super+Tab (free cursor) and back.
- [ ] Lock while the Director is open, and while using a window (E): after unlock and
  re-entry, no stuck input grab, the Director opens and closes normally.

## Drawn routes, the companion at the ramp foot, steam with offload (core 0.8.4)

Three fixes from a live session, each with a nested check (`--route-carry`,
`--follow-corridor`, `--haze-depth`). The offload change adds the characters to the
compositor's depth pass under the worker's frame (inside the render pass, GL state saved as
before); no cursor or focus work. Check on the real session, with Lumen Reach and a character
you placed from Director → Objects:

- [ ] Tool 3: draw a closed square on open floor (Alternate closes it), then key 1. Super+LMB
  on your placed character, hold the aim just outside the square: the square lights up and
  the ring is valid. Release: it walks the square; no crash, no stuck Super or button.
- [ ] Director → Objects → Characters → Rebind route on that character: your drawn path is
  listed ("Drawn path"); choosing it binds it and it walks it.
- [ ] With the companion following, walk from Signal Rise down the ramp into the Rime Vault
  and stop at its foot; turn round: the companion comes down to you (or, when it can't, appears
  near you out of view), never standing far up the ramp.
- [ ] Offload on (Settings → Performance): in the Rime Vault, stand so a resident walks or
  sits in front of the purge vent steam: the steam stays behind them. Toggle offload off and on
  while looking: the picture matches.

## Workspace screens (IPC 0.27, object format 0.11)

A placed screen is a home like an authored wall, so F, workspace travel and pulling windows
off it use the same deferred focus, cursor and workspace paths as authored walls; the new code
only adds the home (in the tick) and the screen's wall plane. Nested `--screens` and
`--screens-shell` cover the logic; these cover the hardware cursor and the real monitor. With
the starter screens in the user folder (or the starter kit 0.3.0 installed) and Lumen Reach:

- [ ] Director → Objects → Catalog → a stand screen → Add to pocket; tool 5 Primary on open
  floor: the screen stands there and a toast says which workspace it shows (25).
- [ ] Open a terminal on that workspace (`hyprctl dispatch workspace 25`, open it, go back to
  20): the screen shows it, at the monitor's aspect; the window is tiled on 25, not floating.
- [ ] Aim at the screen and press F: the camera flies in and the cursor is the desktop's;
  Super+Tab flies out with the cursor hidden again. No crash, no stuck cursor shape.
- [ ] Super+LMB on the terminal's rectangle on the screen pulls it into the world (it moves to
  the room and floats); dropping it back on the screen returns it to 25, tiled.
- [ ] Switch to workspace 25 with your normal keybind from the room: you travel to the
  screen's arrival spot facing it. Back to 20.
- [ ] Director → Objects → Placed → the screen → Workspace: type 26 and Enter, then None,
  then Next free: the wall follows each time (empty with None). Carry the screen a few metres
  with Super+LMB: the wall moves with it on release.
- [ ] A 21:9 wall screen on a wall: the workspace shows centred with dark bars on a 16:9
  monitor, full width on a 21:9 one.

## Placed characters across a restart (core 0.9.0)

Loader fix only (`editor.json` restore); no render, cursor or focus change. With Lumen Reach 0.11.3:

- [ ] Director → Objects: place a character and bind it to a route (Rebind route), then
  `core/dev/live/down` and `up` again (or log out and in): the plugin loads, the character is
  back on its route and walking. No "invalid placement" notice.
- [ ] Signal Rise: walk, run (Shift) and autodrive from the foot of the snow-service stairs
  towards the ramp foot, along the passage wall: you stop at the wall, never drop through.

## Data sources page, HTTP sources and secrets (core 0.9.1)

Helper and shell only (no render, cursor or focus change). With a gauge placed (Director →
Objects → Catalog → a gauge, tool 5):

- [ ] Director → Objects → **Data sources**: the gauge's demo source is listed with its state;
  typing in the editor (V, L, H, R included) never switches the view or locks anything.
- [ ] **New HTTP** with the gauge's id, a public JSON API you use (or a local one) and its
  token as a secret header: **Test** names the missing secret; paste it and **Store in file**:
  `~/.config/hyprune/secrets.json` is mode 0600 and `sources.json` holds the name only.
- [ ] **Test** again: the raw response, the mapped values and the gauge preview agree with the
  API; **Save**: the gauge in the world shows the value, colour (level) and display within one
  interval.
- [ ] If the desktop keyring is unlocked and `secret-tool` is installed: **Keyring** stores a
  second secret; `secret-tool lookup application hyprune secret <name>` prints it.
