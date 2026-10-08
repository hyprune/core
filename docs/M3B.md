# M3B: keymap v2

The owner's `../NOTES.md` is the spec and is unchanged. Defaults are compiled
from `config/input.default.json`; docs generates its Keymap page from those same
bytes (`python3 ../docs/scripts/generate-keymap.py --check`). HJKL is listed first.

Input owns an independent JSON file, `$XDG_CONFIG_HOME/hyprune/input.json` or
`~/.config/hyprune/input.json`. JSON uses core's existing nlohmann parser and IPC
representation. The parent-directory inotify watch survives atomic editor saves.
Invalid values or internal conflicts keep the last good config. Save stages a
0600 file, fsyncs and renames it; core never requests a Hyprland reload. Binding
changes cancel held actions/grabs; settings-only changes preserve held actions.
Lua graphics/speed/sensitivity/inertia remain compositor setup. Legacy Lua key
names no longer choose M3B actions. File bindings are authoritative.

Every action has zero to three bindings. Keys use the active layout’s base-level
symbol, lowercase letters and separate modifiers; the press identity survives
modifier/layout changes until release. Four modes: world, interactive (E),
focus (F), menu. E uses windows and workspace walls in place with a surface-relative
virtual cursor. F transitions windows or workspaces to native 2D. Super+Tab or
Back leaves either; TAB long hold opens Map; Esc leaves menu. Q holds scope with
wheel magnification and optional vignette. Ctrl crouches with fixed feet, reduced
eye height/capsule, and standing clearance checks. U writes a private timestamped
capture with camera, modes/tools, surfaces, placements, recent IPC/events and a
bounded recent input ring, and emits the file path as a notice.

Tool slots: normal; window place/lock; path open/closed line/curve/arc; position
move/rotate/scale; reserved. Shift snaps 0.1 m / 15° / 0.1 scale factors. Position
handles draw as axis arrows, rotation arcs, scale cubes and a uniform centre cube.
Object transforms alter rendered meshes and collision triangles consistently.
Window gestures preserve grabbed/corner offsets and send real client configure;
OBB swept placement remains required. Locked windows reject accidental hand
carry/resize and gizmo edits, and display a gold lock badge. Paths, transforms and
locks persist in `$XDG_STATE_HOME/hyprune/editor.json` by world/area; no authored
package is rewritten. Paths are limited to 128 paths, 256 points/path and 2048
points total. Editor state is bounded; debug capture allows 4 MiB. Z transport
summon and tool 5 remain explicitly reserved, as requested.

Settings is a real Director tab: mode selector, all actions, three capture boxes,
clear, conflict/stealing feedback, long-hold slider and vignette checkbox. Shell
requests capture using its verified lease, edits a draft, previews through core,
and saves through core. It never opens/writes the config file.

## Shortcut stealing

The cancellable keyboard/button/axis events run before native keybind handling.
A matching action cancels that event before Hyprland can dispatch a colliding
bind. Consumed presses own releases across mode changes and config reloads.
Native forwarded presses receive balanced manager releases using the originating
device. No native binding is rewritten. Deactivate, unload and daemon failure
remove world interception and preserve original native bindings. Uncolliding
shortcuts use native handling, including in world control; hidden clients never
receive swallowed world keys/buttons/scroll.

Native records supply collision descriptions. Lua binds without a description
show their dispatcher/reference. Native keycode and multi-key binds are stolen
when the triggering event matches an action, but their description matching is
currently incomplete; check those manually. The active mode’s exit binding also wins when the free cursor is on another
monitor; exit defers return to the world monitor. Uncolliding shortcuts stay native.

## Contract

Exact IPC 0.6 adds keymap/editor state, Map opening intent and input.get,
input.preview, input.set, input.capture, input.capture.cancel. Reads/preview use
state.read; edits/capture use input.control. Capture requires the active verified
Director lease. Schemas include the live follow-up amendment from schema commit 2d41df5; older exact
versions omit new fields/methods and retain prior projection bounds.
RFC 0003, 0006 and 0008 amendments and CHANGES-FOR-SDK-AND-SHELL.md describe this.

## Verification

Core CTest includes all default variants and ownership, hold boundary/repeat,
aggregate releases, wheel chords, file validation/watch/save, corner offset and
opposite-corner preservation, crouch, six path modes/persistence and gizmo picks.
Nested acceptance runs only through `./dev/nested/run.sh --m3b`, whose lifetime
is wrapped in `flock /tmp/worldshell-nested.lock`. Config/editor/capture paths
are isolated beneath `.m3b-state`; ctl verifies the nested PID/config/socket.
Nested fixture uses resolve_binds_by_sym for wtype's generated keymap, with actual
Super key events plus native modifier state. It defines colliding Super+Tab and
unrelated Super+F9 marker binds. Lumen Reach and Switchyard remain unchanged.

The final nested acceptance run passes **102 checks** (the reviewed recording
run passes 100). E tolerance selects a nearby window ahead of a farther wall
without bypassing solid geometry. Native Shift-mask tests verify boosted W
movement and held TAB cancellation as modifiers change. It covers every default
key variant, four modes, real window/workspace typing, workspace popup composition
and click/scroll landing, native fullscreen in/out, off-output virtual cursor exit,
mode-scoped Super+Tab stealing, unrelated native binds in every mode, config
watch/save/invalid-edit recovery, Settings capture and native-conflict feedback,
nearest-corner resize, carry/push/roll, arbitrary-wall placement, lock/unlock,
window/client and object gizmos, snapping, all six path types, jump/crouch/stairs/
respawn, both unchanged release worlds, helper shutdown and plugin unload. Three
successive scale gestures verify stable client configure acknowledgements.

| Parity item | Status | Evidence |
| --- | --- | --- |
| Independent validated input file, live diff-apply, three variants and 1000 ms hold | Done | `IPC_config_persist`, `watched_atomic_reload`, invalid-file recovery, hold-boundary unit/native checks |
| Settings capture/clear/conflicts, slider and scope toggle; core-only persistence | Done | real Director capture box click, `settings_core_save`, `settings_Hyprland_collision`; closed IPC and input-validation tests |
| HJKL-first defaults, E window/workspace, F window/workspace, exits, Q/U and movement | Done | real seat acceptance and generated Keymap drift check |
| Mode-scoped native stealing and balanced input ownership | Done | Super+Tab markers inactive/world/use/focus/off-output, unrelated binds in each mode, helper shutdown/unload restoration |
| Native collision descriptions for keycode/multi-key binds | Partial | actual interception works; description matching requires manual review as described above |
| Window place/lock, path segments and position gizmo | Done | arbitrary-wall mount, locked carry refusal, client resize, object transform/snap, six persisted path types |
| Render-pass safety / cursor transitions | Nested verified; hardware pending | render-depth guards, GL restore, exhaustive checklist below; software cursor cannot prove hardware behavior |
| Generated docs and 45–75 s nested demo | Done | `/keymap/`, default-file SHA/drift check, gallery MP4/JSON |

Core CTest passes **8/8**, with assertions enabled even in RelWithDebInfo; the
bridge suite starts a helper from a parent that blocks SIGTERM and verifies the
child's independent signal policy. Schema passes **80**, shell **42**, SDK **53**
with **2** environment-dependent real-core tests skipped. Docs builds **18 pages**;
link/schema hashes, generated keymap and browser/theme/search/mobile checks pass.

Evidence is checked in at [evidence/m3b](evidence/m3b/): acceptance JSON, gzip wire
transcripts, selected real screenshots, test logs and video metadata. Replay
validation with `node tests/validate-m3b-wire.mjs docs/evidence/m3b/`. The tested
runtime implementation is `5f1dd5c` (harness `8946876`); shell is `052b953`, SDK `ddc78c9`, schema
contract `a1e2f84` (test portability follow-up `6f095e9`), docs `bd6cfb8` and `ca07d0d` (layout-aware key identity amendment).
The final wire corpus validates **182 responses and 1436 notifications**.
`recorded-acceptance.json` and `video.json` retain the earlier recording’s
100 checks / implementation `8407595`, independently of the final corpus.

The inspected **59.9 s, 1920×1080, 30 fps** demo is at
`~/workspace/rick-the-alien/worldshell-gallery/videos/hyprune-core-m3b.mp4`, with
`hyprune-core-m3b.json`. It includes E window/workspace in place, F window/workspace
fullscreen, Super+Tab exits, Q scope, real Settings capture with a native conflict,
move/rotate/scale handles, window lock and six path types, Lumen Reach and Switchyard.
Transport Z and slot 5 remain the requested stubs. Hardware cursor validation is
pending the owner's supervised test below; the real session was never touched.

To use Settings, start the shell with exact `--protocol 0.6` (or
`HYPRUNE_PROTOCOL=0.6`) and grant `input.control` through the trusted launcher.
Existing exact protocol choices do not silently upgrade. Bindings live in core's
independent input file; no compositor reload is needed for edits.

## Supervised live test checklist

All cursor/focus/workspace changes run in doLater with a render-depth guard.
Visibility changes are coalesced, no custom native cursor image is installed,
and GL state is saved/restored. Test each transition under owner supervision;
confirm no reentrant render / CMonitor::useFP16 crash, stuck cursor or leaked input:

- Desktop → active world: save prior visibility; hide native cursor; draw reticle.
- World → E window use: native stays hidden; virtual pointer is relative to the window; focus correct root/child.
- World → E workspace use: native stays hidden; defer workspace switch/focus; camera remains fixed; virtual pointer is relative to wall.
- E window/workspace → world via Super+Tab or Back: clear app keys/buttons/focus; native stays hidden; reticle resumes.
- Virtual pointer → neighbouring desktop: defer warp/show; enable free native cursor only after committed warp.
- Neighbouring desktop → world: defer monitor focus/hide; recover window-relative cursor from committed pointer position.
- E/use or world → F window: native hidden through interpolation; show/warp to output centre only at 2D handover.
- F window → world via Super+Tab/Back: cancel exit chord before native overview bind; native hide/focus restore deferred; reticle resumes after return interpolation.
- World/use → F workspace: hidden through fly-in; deferred workspace switch/focus; show at 2D handover.
- F workspace → world via Super+Tab/Back: steal exit chord; defer native workspace/focus release and hide; restore saved camera during fly-out.
- World → TAB-held Director Map: release world grabs, show native cursor and focus verified Director after frame.
- Director → Settings capture: native stays visible; capture chord/button/wheel before native bindings; Esc cancels capture and leaves menu.
- Director/Settings → world via Esc: release Director focus/grabs, hide native cursor after frame, restore reticle.
- Scope Q/vignette, tool/submode selection, lock and gizmo dragging: native cursor stays hidden; overlays alone move/change; no cursor show/hide/shape mutation.
- World/use/focus/menu → session lock: restore native cursor policy and clear owned input; unlock resumes only after state is safe.
- World/use/focus/menu → exit, output loss, failed world load, daemon death or plugin unload: release keys/buttons, restore prior visibility/focus/workspace; native shortcuts work immediately.
- Super+Tab stealing in E, F window and F workspace: native overview must not trigger on press or release. In world (where default Super+Tab is unbound), inactive, and after unload it must trigger. Unrelated binds must work in every mode.
- Live tooling panic/down → desktop: `live_stop` queues exit and a visibility reset after the frame, changing no cursor shape; unload waits for inactive/visible state. A second down performs no cursor transition. See [LIVE-TEST.md](../dev/live/LIVE-TEST.md).
- Live binding reload during hold/grab and while Director is open: balanced releases; no cursor change inside render; settings-only reload preserves active hold.

Video is encoded and inspected outside gallery, then moved as a completed MP4/JSON
pair into gallery/videos. The gallery autocommit watcher is never signalled.

## Supervised live follow-up (2026-10-08)

The world adopts newly mapped toplevels on its output while active. Their native
workspace stays unchanged; there is no owned room workspace or monitor switch.
Adopted clients keep their identity/pose across toggles while they remain mapped.
Existing desktop windows are baselined at activation rather than silently
imported. New windows receive an ephemeral free pose, the nearest/current home
(or their explicit workspace home), full-rectangle free-zone clearance and M2
OBB recovery. If no home zone can fit the window safely in the current view (including arrival
outside a home), it falls back to safe
camera placement with a notice. Existing authored placements take precedence.
The texture budget applies to actual world clients, not unrelated old desktops.

Back in world mode enters the real layer cursor (the Lua `input.typing_button`,
default 275, when no file binding consumes that button). Back or Super+Tab leaves
this interactive mode. Mouse look and movement stop; top/overlay layer surfaces
and their popups on the world output receive ordinary pointer input. Clicks,
scroll and hover over the 3D scene remain masked from underlying desktop clients.
Other outputs retain ordinary native input. This complements E's window-relative
virtual cursor; it does not replace it.

Director Workspaces comes from native workspace surfaces, merged with optional
world homes. Desktop-only entries use surface.focus, while configured homes use
workspace.activate. The IPC 0.6 apps inventory includes mapped native toplevels
on all outputs, up to 128, independently of the 16 world texture leases. The
schema 0.6 amendment and all validators are updated together; 0.5 stays at 16.

Both owner's Quickshell crash reports (`kcex3galmt`, `kcexelalmt`) and coredumpctl
backtraces identify Qt 6.11 StackLayout size-hint traversal, including
qmlAttachedPropertiesObject/QQuickLayout/QQuickStackLayout on page change.
Director pages now have explicit anchored geometry. The trusted launcher keeps
the authenticated core bridge alive across presentation restarts, accepts only
same-UID descendant replacements, replaces stale presentation sockets, and
binds the new Wayland PID before opening an overlay. No token is put in QML,
argv or files. A nonzero top-level QML exit is relaunched through that bridge.

`hypruned.log` formerly recorded errors only. It now records session startup,
IPC readiness, shutdown and bridge errors in the existing live log; secrets and
raw input are not logged. Unspecified graphical glitches remain pending owner
observations; this follow-up makes no claim to have diagnosed those.

### Supervised live test checklist additions

- [ ] World control → Back: real cursor becomes visible; virtual cursor disappears;
      camera stops moving. Click the owner's bar/launcher, type into its search,
      open its popup and scroll it.
- [ ] Move from a layer over the world: hidden desktop windows get no hover,
      click or scroll. Move to another output: normal desktop pointer still works.
- [ ] Layer cursor → Back and → Super+Tab: cursor hide and return/warp are deferred;
      movement/reticle resume; the owner's colliding overview bind is stolen.
- [ ] Layer cursor → Super+Tab → TAB-held Director → Esc: deferred cursor/focus
      transitions remain balanced and restore world control.
- [ ] Layer cursor → toggle out, helper loss or unload: saved cursor visibility
      returns through the deferred path, and native binds resume.
- [ ] Launch kitty with Super+Enter while active: it appears safely in the current
      home, is usable with E, and appears with title/class/icon in Workspaces.
- [ ] Alternate Help/Workspaces/Settings/Map several times. Help stays open.
- [ ] After a presentation restart, the menu can open again with a fresh Wayland
      child association; the bridge retains its lease and credentials.

Nested regressions run through `dev/nested/run.sh --live`, always under its
lifetime `flock /tmp/worldshell-nested.lock`. They use the live bootstrap's exact
0.6 grants and empty workspace mapping; include native launch, real layer click,
menu page changes, forced presentation crash/reconnect, helper death and down
twice. Nested software cursors still cannot validate hardware-cursor behavior.

Live follow-up evidence: [35 nested checks](evidence/live-followup/acceptance.json),
[spawn in view](evidence/live-followup/spawn.json),
[recovered Director](evidence/live-followup/director.json), and the unit/schema/SDK/shell
logs under `docs/evidence/live-followup/`. The test intentionally crashes only the
nested presentation, limits its core dump size, and leaves the real session alone.

## Live retest round 2: input conventions and diagnostics

World +Y is up; zero camera heading looks along -Z. Positive camera heading
turns right; positive pitch looks up. Screen pointer Y grows down. Camera
quaternions use right-handed axis angles, so the camera quaternion is
`Y(-heading) * X(pitch)`. Surface/editor yaw remains a right-handed +Y angle.
Carry applies the camera quaternion delta to both the original window facing
and the hit-point offset, preserving the grab point as the camera turns or tilts.
Camera ray/projection, mount normals, editor rotation and carry are pinned by
unit tests. Focus interpolation takes the shortest heading across the ±pi seam.
The virtual reticle now uses Hyprland's target convention (native row zero is
the logical top), matching input rays and projection.

`invert_y` is an optional boolean in input.json, defaults to false, and has a
Settings checkbox. Older keymaps are normalized without overwriting the file.
Missing debug/capture actions in interactive/focus/menu use their mode defaults;
explicit empty arrays disable them. World uses F3/Y and U. Interactive, focus
and menu use F3 and Super+U so ordinary U and Y remain typeable in clients.
Their Hyprland collisions are intercepted only while the mode is active.

F3/Y draws core's same-frame diagnostic HUD: FPS/frame interval, CPU update,
render, capture refresh and IPC, asynchronous disjoint-aware GPU query timings,
and CPU scene/windows/bloom/output timings. Collision triangles, player capsule,
window OBBs, placement-zone bounds and walking graph are native wireframes.
Collision VBOs are rebuilt outside the render pass when editor transforms change.
GPU query reads never wait for unfinished results or call glFinish; unavailable
or disjoint timings show N/A. Debug HUD remains visible over native focus views;
its world timing sample refers to the last world frame there.

U stages a core-owned RGBA8 tonemap of the last completed world resolve FBO
**after** the compositor frame, restores GL state including pack/unpack/PBO
state, and reads that own FBO. It never asks Hyprland to render or snapshot.
PNG encoding and atomic private file persistence run in a worker; the owner loop
posts the success/error notice. The bootstrap pins the state directory at load:
`$XDG_STATE_HOME/hyprune/captures/`, or `~/.local/state/hyprune/captures/`.
A JSON/PNG pair has the same timestamp stem. JSON records the frame camera,
number, dimensions, age, source and timings, plus mode/tools/surfaces/placements,
IPC/events and bounded input history. During native 2D handover the screenshot is
explicitly the last completed **world** frame, with its age, rather than a native
desktop snapshot. Plugin unload waits for any active writer to finish.

### Supervised live test checklist additions

- [ ] World: mouse up looks up; Settings → Invert mouse look Y → Save reverses
      only the vertical axis. Clear the option and save to restore the default.
- [ ] Carry an off-centre point of a terminal; turn right 30°, left, then tilt.
      The window faces the player, the grabbed point stays under the reticle,
      and collision recovery still prevents geometry penetration.
- [ ] F3 then Y toggles core diagnostics; verify CPU/GPU labels and all collision
      wires. No cursor state change occurs for either action.
- [ ] U in world and Super+U in E-interactive, F-focus and Director: JSON and PNG appear,
      the notice names their path, and PNG orientation matches the world output.
      Native focus captures clearly report last-world-frame age. U never changes
      the cursor or triggers a compositor snapshot inside the frame.
- [ ] Interactive/focus → Super+Tab: existing deferred cursor hide/show and focus
      exit remain balanced after diagnostics. F3/Super+U stealing ends on deactivate.

Nested evidence is under `docs/evidence/live-round2/`. The nested compositor has
only a software cursor; these checks do not validate hardware-cursor behavior.

## Live retest round 2: core world overlays and profiling

Core draws waypoint labels/icons, the active route and E/F prompts with the
camera used for its current frame. `show_area_markers` defaults to false; Settings
can opt in. Shell IPC 0.6 HUD removes its projected marker repeater while keeping
Director/notices/compass/minimap. Projection data remains available at up to 10 Hz.
Periodic state publication is capped at 30 Hz; explicit mutations and UI events
publish immediately. This removes repeated per-frame marker raycasts and JSON
publication from the 3D feedback loop. See the RFC-0005/0008 amendments and
CHANGES-FOR-SDK-AND-SHELL.md for the lease-owned colour/font/icon-atlas style hook.

Diagnostic timings distinguish CPU frame submission from asynchronous GPU elapsed
time. CPU update includes movement and native texture refresh; capture refresh is
also shown as its own substage. IPC and renderer CPU timings are separate. GPU
scene/windows/resolve+bloom/output queries and deferred capture/composition queries
never wait for unfinished results. Texture capture uses native imported leases,
not CPU screenshot uploads. U's one-shot readback and worker PNG persistence are
reported separately in its JSON. GPU timings share the machine with the owner's
still-active session and can include scheduling contention; they are not an
exclusive-GPU benchmark. Bloom was already half resolution; disabled/zero-strength
bloom now clears its target and skips all three extraction/blur draws.

### Supervised live test checklist additions

- [ ] With no waypoint selected, area/place markers are absent. Select a waypoint:
      its label and route remain locked to the world while turning rapidly.
- [ ] Aim at a usable window or workspace: E/F prompt follows that surface without
      IPC lag. Focus fullscreen: world-locked overlays disappear; exit restores them.
- [ ] Settings → Show area/place markers → Save enables them; clearing disables
      them while preserving the active waypoint. No cursor change occurs on apply.
- [ ] Shell lease/theme changes update core colours/font/icons through deferred
      uploads. No cursor show/hide/shape change occurs during styling.
- [ ] U during a rapid turn records the rendered frame camera and marker positions.
      Capture readback is deferred until input delivery and the frame finish;
      unload cancels queued captures and joins an active PNG writer.

Nested tests use `HYPRUNE_NESTED_4K=1 dev/nested/run.sh --live`, whose lifetime
flock isolates the compositor. The harness checks actual output dimensions before
profiling, tests a live theme update and marker option reload, and keeps earlier
carry/capture/layer/Director/reconnect/helper-crash recovery checks.

The final 80-sample Lumen Reach run used a 3840×2160 headless output on Mesa
Intel(R) Graphics (PTL), with an active waypoint route. Times below are medians
in milliseconds except IPC, where the mean captures scheduled publications:

| Stage | Before | After |
| --- | ---: | ---: |
| CPU update | 0.539 | 0.561 |
| CPU native texture refresh | 0.036 | 0.053 |
| CPU renderer submission | 0.362 | 0.346 |
| CPU IPC publication | 1.416 mean | 0.355 mean |
| GPU scene | 25.640 | 25.228 |
| GPU windows | 2.737 | 2.772 |
| GPU resolve and bloom | 6.747 | 6.942 |
| GPU output | 0.839 | 0.835 |

Native texture refresh is part of CPU update. GPU capture upload was 0 ms in
this world because surfaces used imported native leases. The previous 16.7 ms
"frame" value measured CPU submissions even while GPU work queued; the bounded
run measured an 83.2 ms median interval between new world renders and at
most two frames in flight. The owner session shared the GPU during nested tests,
so these numbers cannot establish exclusive 4K throughput. The scene pass is the
largest measured cost. IPC publication work fell roughly 75% on the sampled
mean; per-frame CPU update and render work remained below 1 ms combined.

The one-shot U capture at 4K read back its own FBO in 78.7 ms and encoded/wrote
the PNG on a worker in 627 ms; its selected world frame was 165 ms old. That
readback can cause a single visible pause when U is pressed. The PNG worker and
the normal world frame path do not wait for encoding. Raw before/after samples,
the paired capture and acceptance results are in
`docs/evidence/live-round2-overlays/`.

GPU submissions are capped at two in-flight core frames using zero-timeout GL
fence checks. When busy, presentation reuses the matching world resolve and
frame-local labels; it does not enqueue another expensive scene. U temporarily
holds new world submissions until the selected frame's GPU fence completes,
then reads its own tonemap FBO after input delivery. No glFinish or blocking fence
wait occurs in production; PNG compression/persistence stays off the event loop.
This bounds GPU backlog and capture latency rather than masking it with a CPU
submission FPS counter. Diagnostics count reused frames and in-flight work.

The default auto quality now detects Intel on first world load. Its intended
2-sample MSAA tier is applied without requiring an initial Lua config change.
Changing quality at runtime reallocates same-sized targets when sample tier
changes; high remains explicitly selectable. Nested cleanup now closes native
clients before destroying Hyprland's monitor/workspace controllers: 0.56's
shutdown-only fullscreen cleanup otherwise produced SIGSEGV coredumps, unrelated
to a render-pass crash. Those backtraces were inspected from nested PID 1677404;
no debugger or mutation targeted the owner's session.

## Live round 3: direct clicks, carry, and logical client size

Normal-tool LMB/RMB sends a client button at the crosshair without entering use
mode. Pointer motion follows the moving ray hit while dragging. The pressed
client owns the release, including after leaving its quad; leaving the quad
retains its last valid local position. E remains explicit window-relative use.
During a Super gesture, configured movement bindings ignore Super and take
priority over native Hyprland chords. Their releases remain owned after ending
the gesture. Other native bindings retain their previous scope.

Carry applies only the camera's change in yaw to the window and the grabbed
point offset. Initial pitch/roll stay unchanged; looking vertically changes the
position along the aim ray. Free windows and user mounts use `windows.scale`
metres per current logical client unit on both axes. A configure acknowledgement
replaces stale pending dimensions; buffer/output scale is not a geometry factor.
Authored tool mounts retain their explicit fit-to-mount sizing contract.

The owner's three round-3 captures were inspected read-only. The first shows
surface-15 at logical 576×670 but a 2.35262×1.34842 m quad, and surface-17 at
573×548 with a 2.39409×1.09586 m quad: stale resize width was stretching content.
The second confirms the Intel PTL device and GPU-bound frame times. The third's
floating amber strip is `Entry light` at Blender (2.8,23.07,1.55), authored in
`worlds/lumen-reach/build.py:223`. Export: `Visual-sun-court`, mesh 199, primitive
5, material `sun-court--amber`, world bounds approximately
(2.7625,0.2,-23.095)–(2.8375,2.9,-23.045). It stands in the open entrance gap;
worlds should move it onto a pier or remove it. Its 60 exported triangles contain
no exact duplicates, and its material is opaque. A still capture cannot establish
the cause of temporal flicker; that remains unconfirmed. Worlds was not edited.

### Supervised live test checklist additions

- [ ] Plain LMB press/drag/release selects text at the moving crosshair and keeps
  the reticle; it must not transition to a virtual or desktop cursor.
- [ ] Release after looking off the window stops selection; a subsequent click
  works. Close a pressed client and verify no stuck button or leaked click.
- [ ] Hold Super+LMB and move with HJKL / WASD / arrows. No native Super movement
  chord fires. Release the gesture and check native chords work again.
- [ ] Turn horizontally while carrying, then look up/down: yaw follows, original
  pitch/roll remain fixed, grab offset is retained.
- [ ] Resize terminals on differently scaled outputs: equal fonts retain equal
  world density in free windows; no horizontal stretching after configure ACK.
- [ ] E still changes reticle → virtual cursor; Super+Tab/Back returns to reticle.
  No new cursor shape/visibility calls were introduced in this fix.

Validation: 8/8 CTests and 114/114 nested M3B checks, including actual balanced
client press/moving drag/off-window release at GDK_SCALE=1 and 2, carry with a
colliding native Super+W, pitch preservation, and native chord restoration.
Nested also loaded Lumen Reach and Switchyard. Evidence:
`docs/evidence/live-round3/acceptance.json` and `unit.txt`.

4K spawn follow-up: a safe placement smaller than the initial desktop-sized
client now queues a matching logical client configure. Floating/configure actions
run on a later event-loop turn, after capture iteration, so layout callbacks
cannot invalidate the compositor window list being traversed. The 4K live-like
harness verified the arriving terminal is visible, collision-free and in view.

### Live round 3 render resolution

The world render scale is independent of client logical/buffer scale. See M3A.md
for `graphics.render_scale` and `graphics.target_fps`. No new cursor transitions
are introduced. For the supervised follow-up:

- [ ] F3 shows internal scale, native output size and frame/GPU timings; allow
  several seconds for auto resolution to settle while turning in Sun Court.
- [ ] Text, workspace walls, prompts and markers stay sharp as scale changes;
  a window behind a world wall remains occluded.
- [ ] U saves a native-resolution PNG with the reported scale in its JSON.
- [ ] Open a desktop-sized terminal at 4K: the client receives a safe logical
  configure size rather than a stretched quad or an invisible oversized window.

4K profile (`evidence/live-round3/render-scale/summary.json`): Intel PTL, Lumen
Reach, native 3840×2160. Auto settled at 0.375 in this nested run. Median GPU
scene/windows/bloom+upscale/output: 5.74/3.38/1.51/0.83 ms, versus
22.38/3.25/7.20/0.84 ms at fixed native world resolution. Median frame interval
16.67 ms versus 66.62 ms. Auto p95 was 33.40 ms, with 91 reused presentations
during the 80-sample interval; sustained ≥60 FPS is **not yet demonstrated**.
The owner session was not altered to obtain exclusive GPU access.
CPU render/update/capture/IPC medians: 0.30/3.82/3.32/0.001 ms. The capture metric
covers texture leasing and placement refresh, not PNG capture. Snapshot readback
remains an explicit U-only operation. GPU tests preserve native client stripe
pixels exactly across 1.0 and 0.5 world scales, verify world colour/depth
orientation, native window occlusion, capture dimensions and GL state, and
exercise fixed/auto bounds and hysteresis. Live-like 4K acceptance: 55/55,
including scale diff reload, new-window spawn, captures and recovery. CTests: 8/8.
