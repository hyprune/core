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
Director lease. Schemas are frozen from schema commit a1e2f84; older exact
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
