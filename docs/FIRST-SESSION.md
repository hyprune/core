# First real Hyprune session — 2026-10-07

Core 0.2.0, shell 0.2.0 and SDK/schema 0.3.0 run the first session over
authenticated IPC 0.3. Neither the SDK mock nor shell association/workspace
emulators participate. Switchyard is copied unchanged from the worlds build;
the second world is the original CC0 atrium fixture named Quiet observatory.

## Reproduce

Build core using the README, install shell's Python requirements, and build
Switchyard according to `../worlds/README.md`. This machine consumed the existing
remote-Blender-built `../worlds/dist/com.hyprune.switchyard` package. Require
Quickshell, kitty, btop, wtype and grim; Chromium is optional. For recording,
install wf-recorder and FFmpeg.

```sh
dev/hyprune-dev up       # smoke acceptance followed by an interactive session
dev/hyprune-dev smoke    # acceptance and clean shutdown
dev/hyprune-dev smoke --visible # acceptance on the interactive nested output
dev/hyprune-dev record   # acceptance plus the 61-second real-session showcase
node tests/validate-session.mjs
```

The entire nested lifetime holds `flock /tmp/worldshell-nested.lock`. All
compositor/Quickshell/Wayland operations pass through a controller that verifies
the nested PID, start time, exact config, compositor lock, instance signature
and display. It rejects the owner's session. IPC grants enter through inherited
FDs; tokens never enter recordings, logs or files. `up` uses the visible nested
Wayland output; smoke/record default to 1080p HEADLESS-1. Artifacts stay in ignored
`dev/nested/.integration-state/`, separate from the older fixture harness. Set `HYPRUNE_NESTED_STATE_DIR="$PWD/dev/nested/.integration-state"`
before interactive `dev/nested/ctl.py` commands. Workspace 10 maps to Switchyard
arrival; workspace 11 maps to Quiet observatory. F12 returns to the desktop.

## Contracts and implementation

- RFC-0008: `shell.bind` associates the authenticated bridge's direct Quickshell
  child with a mapped HUD on the leased output. Core verifies SO_PEERCRED,
  parentage/start time and actual Wayland client PID/UID. Exclusive Director
  focus reaches only that bound client's mapped Director. Namespace or supplied
  PID alone cannot authorize input. Core owns the 350 ms Tab hold; ordinary Tab
  reaches the Director after it opens. Escape closes; lock/exit/disconnect/client
  loss revoke association. A brief layer-mapping interval blocks input leakage.
- RFC-0003 / schema IPC 0.3: `shell.bind`, `workspace.activate`, and revisioned
  `spatial`/`workspaces` descriptors. Frozen 0.1/0.2 clients keep their exact
  state shapes. Socket permissions are now 0600 at creation.
- RFC-0004: optional safe SVG/affine map metadata. A documented adapter reads
  Switchyard's existing map/routes without editing its package. Core validates
  a bounded non-executable SVG and publishes exact bytes/hash/transform; shell
  uses the same fitted rectangle for image and markers. Seven authored route
  places supply eye-height waypoint/travel destinations.
- Core publishes normalized marker projections using its renderer's view,
  60-degree perspective and static collision. Shell interpolates briefly,
  filters occlusion and expires stale samples. This keeps camera and collision
  knowledge in core rather than duplicating it in QML.
- Trusted launcher workspace mappings use actual compositor workspace changes
  and core's existing asynchronous loading/warp lifecycle. Failed loads retain
  the previous world; rapid switches cancel and resolve the latest location.
- Live root surfaces use Switchyard's authored station window anchors, with
  aspect-preserving placement. Layer surfaces stay out of capture. Input picking
  routes real clicks and keyboard input to the docked application's surface.

No focus, workspace, capture or cursor mutation runs inside a render pass.
Hardware cursor lifecycle is unchanged. Nested tests are not evidence of DRM
hardware-cursor correctness. This remains a development supervisor with one-use
tokens, SDR/untransformed outputs, static collision and session-local anchor
assignment; production token renewal, full hotplug/lock acceptance, persistent
docking and navigation around arbitrary obstacles remain follow-up work.

## Acceptance and review

The automated real session verifies package loading and map delivery, HUD layers
on only HEADLESS-1, actual W walking, a core-owned hold and verified exclusive
Director, ordinary Director Tab and Escape, waypoint/route/autodrive, Map travel
and passive warp, two live terminals including btop, Chromium when installed,
station anchoring, real pointer focus and a typed file marker, IPC workspace
activation, compositor workspace return, rapid workspace switching, exit and
live compositor/shell responses without a crash. It also rejects an ordinary
Wayland terminal impersonating the shell, an unassociated exclusive overlay,
and caller-supplied private PID context.

Verification: core CTest 4/4; shell unittest 36/36; schema Node tests 73/73;
SDK Node tests 53/53 including the actual hypruned executable, with no skips;
SDK Bun 50/50; docs build, browser and schema/link checks; SDK validation of the built Switchyard
package. The final visible session has 106 replies and 778 notifications validated
method-by-method against frozen IPC 0.3. The legacy locked M1 nested run also
passes, including daemon-death desktop restoration in 53 ms and three active
unload/reload cycles.
See `evidence/first-session.json` for captured counts and artifact hashes.

Video: `/home/rick/workspace/rick-the-alien/worldshell-gallery/videos/hyprune-first-session.mp4`,
61.433333 seconds, 22,226,000 bytes, H.264/yuv420p, 1920×1080 at 30 fps, with
`moov` before `mdat` for fast start. Adjacent JSON supplies title/tag/notes.
Frames were inspected at full resolution and in a contact sheet. Multiple takes
corrected btop's terminal size and made Map selection a real pointer click.
It shows arrival, a real walk, live btop/browser/terminal docks, actual typing,
Tab hold, package Map waypoint/travel, workspace warp out and back. Camera
glides use authenticated core camera commands to frame the real rendered world;
the walking, hold, Map click and application typing use nested seat input.

Worlds content was not edited. All integration commits remain local for review.
