# Core M3A — hands-on desktop parity

Implementation and acceptance on Hyprland 0.56.2. Changes stay local; no agent
push and no owner-session compositor command. All nested lifetimes use
`dev/nested/run.sh` → `flock /tmp/worldshell-nested.lock`.

## Controls and architecture

Super+LMB carries at the hit point; wheel changes distance with exponential
smoothing. Super+middle rolls around the surface normal. Super+RMB configures
the client size with independently moving width/height, preserving pixel scale.
C holds 2× view zoom; wheel adjusts it, with a 30° lens floor. F focuses an aimed
workspace wall; Super+Alt or the typing button returns. A carried window released
while aiming a wall is tiled into that workspace. Dragging a client rectangle
on a workspace wall pulls it into 3D. Super+Alt switches movement/typing. The
virtual pointer routes hover, clicks and scroll to the real child surface.
V toggles noclip; F4 toggles flight. WASD moves, Space jumps/ascends, Ctrl descends
in flight, Shift runs. 1–5 select pointer, mount and three reserved path slots.
The mount tool clicks a window to tag it, then a surface to place it aligned with
the surface normal. Escape cancels; F12 always exits.

`interaction/Hands` owns pure hit/gesture math and collision sweeps;
`interaction/Config` owns validated patches. Layout owns live free poses and
durable app-class/title rules. The Hyprland adapter owns weak window and child
resources, seat serials and native configures. Native effects run through tracked
`doLater` work and the deferred tick. Rendering samples prepared root/child
texture leases and owned workspace FBOs under the GL state guard. No compositor
snapshot call or focus/cursor/workspace mutation occurs inside `Pass::draw`.

IPC 0.5 adds `interaction`, `apps` and user pose placements. Frozen 0.1–0.4
projections stay isolated. User mounts are saved by `hypruned` with atomic
replacement; world packages remain immutable. See the RFC-0002/0003/0005/0006/0008
amendments and [SDK/shell handoff](CHANGES-FOR-SDK-AND-SHELL.md).

## Configuration

```lua
hl.plugin.hyprune.config({
  input = { typing_button = 275, typing_cursor = true,
            bindings = { focus = "f", fly = "F4", noclip = "v" } },
  player = { walk_speed = 3.5, fly_speed = 5, step_height = 0.35 },
  windows = { spawn_distance = 5, scale = 0.002 },
  tools = { enabled = true, slots = {"pointer", "mount", "path", "path", "path"} },
  graphics = { quality = "auto", fov = 60, reduced_motion = false },
})
```

Calls are partial patches against current settings, applied after the frame.
An unchanged reload preserves poses, camera, velocity and GPU resources. Input
or slot edits cancel active grabs and clear held movement. Unknown sections or
options, invalid/duplicate keysyms, nonfinite values and out-of-range settings
reject the entire patch. Core-owned keys and duplicate nonzero mouse buttons are rejected. Reload does not reset window poses or respawn the player.
Emergency F12, Escape, Tab hold, Super gestures, Super+Alt and slot digits remain
core-owned controls. Keys in `input.bindings` are XKB symbol names.

| Option | Default | Valid values / meaning |
| --- | --- | --- |
| input.typing_button | 275 | Integer 0–767, evdev button; 0 disables; 275 is back |
| input.carry_button | 272 | Integer 0–767, Super+button carry |
| input.resize_button | 273 | Integer 0–767, Super+button native resize |
| input.roll_button | 274 | Integer 0–767, Super+button roll |
| input.typing_cursor | true | Boolean; virtual pointer in typing mode; false keeps look motion |
| input.bindings.forward | w | XKB symbol for forward |
| input.bindings.back | s | XKB symbol for backward |
| input.bindings.left | a | XKB symbol for strafe left |
| input.bindings.right | d | XKB symbol for strafe right |
| input.bindings.jump | space | XKB symbol for jump / fly up |
| input.bindings.down | Control_L | XKB symbol for fly down |
| input.bindings.focus | f | XKB symbol for wall fly-in |
| input.bindings.noclip | v | XKB symbol for noclip toggle |
| input.bindings.fly | F4 | XKB symbol for walk/flight toggle |
| input.bindings.zoom | c | XKB symbol for held view zoom |
| player.walk_speed | 3.5 | 0.1–20 metres/second |
| player.fly_speed | 5 | 0.1–20 metres/second |
| player.run_multiplier | 1.8 | 1–4, Shift speed multiplier |
| player.jump_speed | 5 | 0–15 metres/second initial jump speed |
| player.look_sensitivity | 0.0025 | 0.00001–0.05 radians/pointer count |
| player.look_inertia | 0.03 | 0–1 seconds exponential look easing; 0 immediate |
| player.move_inertia | 0.05 | 0–1 seconds exponential movement easing; 0 immediate |
| player.step_height | 0.35 | 0–0.5 metres; 0 disables discrete step-up |
| player.respawn_below | 30 | 0–100 metres below spawn eye; 0 disables |
| windows.spawn_distance | 5 | 0.2–50 metres in front of player, geometry-clamped |
| windows.scale | 0.002 | 0.0001–0.02 metres/logical pixel for new free windows |
| windows.wheel_factor | 1.06 | 1.001–1.5 multiplicative distance/zoom step |
| windows.transition_seconds | 0.35 | 0–2 seconds fullscreen and wall fly-in/out |
| tools.enabled | true | Boolean; false releases 1–5 to ordinary navigation policy |
| tools.slots | pointer,mount,path,path,path | Exactly five pointer/mount/path IDs; path is an M3B stub |
| graphics.quality | auto | auto/high/intel; safe deferred resource preparation when changed |
| graphics.fov | 60 | 30–110 vertical degrees |
| graphics.exposure | 1 | 0.01–16 world radiance override once explicitly set; otherwise authored atmosphere wins; app pixels bypass it |
| graphics.bloom_strength | 0.15 | 0–4 world bloom override once explicitly set; otherwise authored atmosphere wins; app pixels bypass it |
| graphics.reduced_motion | false | Boolean; instant fullscreen and focus handovers |

## Supervised live test checklist

Nested uses a software cursor and cannot test hardware-plane reentrancy. The
owner must supervise the first dedicated live DRM run. Every plugin cursor
transition below is queued after the frame, changes visibility only if needed,
and never installs a new native cursor image. Native shape requests stay native. While world input owns the output, a visibility hook coalesces compositor hide/show requests through `doLater` and enforces the hidden reticle/virtual policy. It never calls native cursor rendering in the frame.
Record cursor restoration and confirm no nested rendering / `CMonitor::useFP16`
crash for each transition:

- Desktop → movement reticle: save prior cursor visibility, hide native cursor.
- Movement → typing virtual pointer: native stays hidden; show the rendered pointer.
- Typing → movement (Super+Alt, button or Escape): native stays hidden; clear focus/grabs.
- Virtual pointer → neighbouring desktop output: show native cursor, defer warp.
- Desktop output → world monitor in typing: recover from the committed pointer position, defer monitor focus and hide native cursor, resume virtual pointer. Pre-warp events cannot reclaim the world cursor; events on the world stay swallowed during handover.
- Movement/typing → Director: show native cursor; after Director closes, hide it again.
- World → workspace fly-in: keep hidden during flight, show only after 2D handover.
- Workspace → world fly-out: hide after releasing native 2D, restore the saved view.
- World → fullscreen animation: keep hidden, show at native fullscreen handover.
- Fullscreen → world animation: hide, return to the saved plane and movement mode.
- Rapid fullscreen reversal or client closure during handover: settle to movement,
  release client buttons, and hide only while the world retains the output.
- Window/popup closure during a native drag: clear weak target, balance buttons;
  movement cursor stays hidden. Check menus and text/resize cursor shape requests.
- Output focus changes: release movement/grabs; the neighbouring desktop cursor
  stays native. Return to the world without a stuck hardware plane.
- F12, runtime exit, helper death, lock, output removal and plugin unload: restore
  the saved native visibility; cancel queued work and remove retained pass elements.
- Reload input/tools/graphics while carrying or typing: no duplicate cursor toggle,
  no lost release, no camera drift or frozen hardware cursor.

No live DRM session is operated by automated acceptance. The gallery watcher is
never paused, stopped, killed or signalled. Record to a temporary directory and
move only completed MP4/JSON files into gallery/videos.

## Verification and parity evidence

Final locked run: **43 automated checks passed**; the 44th entry explicitly
requires owner-supervised DRM hardware-cursor testing. The raw result is
[evidence/m3a-acceptance.json](evidence/m3a-acceptance.json). All 132 RPC responses
and 794 notifications from this run validate against frozen IPC 0.5, including
an actual authenticated Quickshell Director with five workspace apps. The
Workspaces screenshot was inspected for icons, titles and classes.

| Parity item | Status | Acceptance evidence |
| --- | --- | --- |
| Hand carry, wheel distance, roll, native resize, zoom and wall extraction | Done | carry_offset_distance, roll, client_resize (900×560 → 642×990), zoom, drag_off_wall; OBB tests |
| Parent-attached popup/menu/dialog/subsurface input and spawn placement | Done for supported textures | popup_click, x11_menu_click, dialog_keyboard_close, subsurface_click, spawn_in_front |
| Smooth fullscreen to native 2D and back | Done in nested | fullscreen_in/out; inspected native fullscreen at ~19 seconds |
| Movement/typing, virtual cursor and neighbouring output cursor | Done in nested | typing_toggle/button, virtual_cursor_typing, typing_neighbour_cursor/return |
| F wall fly-in, back, drop to native tiling | Done | focus_wall/back, drag_off_wall, drop_to_tile |
| Five slots and arbitrary surface mounting with persistence | Done; path slots are permitted M3B stubs | shell_mode_tools, mount_tag, arbitrary_mount, mount_persistence |
| Discrete stairs, respawn, noclip and fly toggle | Done | stairs (eye height ~2.45 m after 0.25 m risers), respawn, noclip_on/off, fly_toggle |
| Director Workspaces apps | Done | director_apps; actual rendered list of five clients inspected |
| Validated Lua configuration and live diff reload | Done | config_reload/rejected, pure invalid/atomic config tests |
| Hardware cursor on an owner DRM session | Pending supervised test | hardware_cursor_live is deliberately not marked passed |

Validation: core CTest **6/6**, schema **78/78**, shell **39/39**, SDK **54/54**
with core interoperability enabled and no skips; docs build and local link / five
schema mirror checks passed. Separate locked M1, M2 and first-session integration
regressions passed. M1 includes typing, helper-death desktop restoration (~0.057 s)
and three active unload/reload cycles. M2 validates 98 responses / 601 notifications
against IPC 0.4, including durable relaunch mounts, stock focus and travel.

The finished, visually inspected **57.9 s, 1920×1080, 30 fps H.264** recording is
`~/workspace/rick-the-alien/worldshell-gallery/videos/hyprune-core-m3a.mp4`, with
`hyprune-core-m3a.json`. [Video metadata](evidence/m3a-video.json) records the
SHA-256, timeline, reference-world attribution and tested implementation commit.
It shows all requested interactions, then both unchanged author packages. Encoding
and inspection happened under `/tmp`; only the finished pair was moved to the
gallery. Its watcher was never signalled. No owner-session command was issued.

Reproduce:

```sh
cmake --build build -j3
ctest --test-dir build --output-on-failure
./dev/nested/run.sh --m3a
./dev/nested/run.sh --m3a --record
```

The GTK fixture uses a real xdg popup and client fullscreen request. The persistent
nested virtual pointer preserves implicit/popup grabs across test commands.
Lumen Reach and Switchyard are copied unchanged from `../worlds/dist` into the
isolated session. Tests never select the owner's instance or Wayland socket.

## Limits retained from M2

Up to 16 mapped window roots on the selected world output and 128 attached child
planes are captured. The Workspaces app list describes those eligible root clients,
including hidden workspaces on that output. Unsupported external textures and
cropped/transformed client buffers remain unavailable; HDR client color conversion
is deferred. Path slots are deliberately stubs for M3B. Native cursor shape images
remain compositor owned; the world pointer is a rendered reticle. Hardware cursor
safety remains pending the supervised owner test, even after all nested checks pass.

## Prototype lessons carried forward

The behavioral reference is `hypr3d-rick-concept-oracle/src/main.cpp` and its crash,
focus, popup and cursor/input fixes. The implementation uses independent gesture,
config and layout modules. Carry retains the hit offset; roll uses a signed angular
sweep; resize clears native maximization before real configures. Full window focus
and raising are deferred. Menu children retain parent keyboard focus, dialog roots
receive their own focus, and subsurface coordinates are local to the exact resource.
Consumed presses own releases across handovers. Modified unbound world keys do not
reach hidden clients; compositor bindings retain priority. Direct texture leases
avoid snapshot recursion, GL state is guarded, and cursor visibility is deferred
and coalesced. Nested pointer devices persist across events so popup grabs survive.
