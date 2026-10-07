# Hyprune core

“What if the computer itself were an explorable world?”

Hyprune core integrates a small 3D runtime with Hyprland. This is the M0 developer
implementation, pinned to **Hyprland 0.56.x** and checked against the exact running
compositor ABI hash. The shell, worlds, SDK, documentation and schemas live in
separate repositories. This repository contains no proprietary game assets.

## Build and test

Requires CMake 3.27+, a C++23 compiler, Ninja, pkg-config, matching Hyprland plugin
headers (including generated protocols), GLES3/EGL, Wayland client/server and
scanner, libdrm, pixman, xkbcommon, hyprgraphics, pangocairo, Lua, and Python 3.
The `hyprland.pc` include paths must point at the headers for the running build;
this machine uses the headers installed by hyprpm. cgltf, nlohmann/json and
stb_image are vendored with their licenses. Builds do not download dependencies.

```sh
python3 tests/fixtures/generate.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 3
ctest --test-dir build --output-on-failure
```

Outputs are `build/hyprune-core.so` and `build/hypruned`. Keep them together in the
build tree: the plugin currently embeds the helper's build path. `render-tests`
uses an independent surfaceless EGL context and software GL, not a compositor.
The generated atrium is original CC0 content; its generator is MIT source.

## Isolated nested session

Requires `Hyprland`, `hyprctl`, `kitty`, `wtype`, `grim`, `flock`; recording also
requires `wf-recorder`, `ffmpeg` and `ffprobe`.

```sh
dev/nested/run.sh                 # automated smoke and failure tests; then exits
dev/nested/run.sh --record        # also produces the gallery video
dev/nested/run.sh --serve         # interactive nested session, Ctrl+C to stop
```

`run.sh` wraps the **entire** session lifetime in
`flock /tmp/worldshell-nested.lock`. Do not bypass it. It starts a 1600×900 Wayland
nested output plus a 1920×1080 `HEADLESS-1` output for repeatable testing and
recording. The outer compositor controls whether its nested window floats and its
actual size; the harness deliberately never changes outer-session window rules
or dispatches outer-session commands. The headless output remains deterministic.

Only this controller may invoke hyprctl:

```sh
dev/nested/ctl.py hyprctl monitors -j
dev/nested/ctl.py hyprctl eval 'hl.plugin.hyprune.toggle("HEADLESS-1")'
dev/nested/ctl.py env kitty
dev/nested/ctl.py shot /tmp/hyprune-nested.png
```

The controller checks the saved PID, process start time, exact config path,
instance signature, Wayland socket and compositor lock file on **every call**. It
rejects instance overrides, missing/stale state and the outer signature/display.
Never run bare `hyprctl`, load into the owner's desktop, or manipulate that
session to make a test pass. The controller supports `env` only after verifying
the nested display; it never falls back to the caller's display.

The Lua entry point is `hl.plugin.hyprune.toggle([monitorName])`; the registered
native dispatcher is `hyprune.toggle`. Hyprland 0.56's CLI accepts Lua dispatch
expressions; use the `eval` form above. Inside world mode, WASD walks, Space jumps,
mouse motion looks, clicking a surface focuses its app, Escape returns to
navigation, and F12 exits the world independently of the daemon. Super/Alt
shortcuts retain compositor priority. Outside world mode, Super+F12 toggles it
when the nested Lua config is reloaded after plugin load.

The native cursor remains under Hyprland's control; a small rendered reticle marks
the picking ray. Core does not show/hide/replace hardware cursor images. M0 has
**not** been tested on a dedicated DRM/hardware-cursor session.

## Module boundaries

| Module | Owns | Does not own |
| --- | --- | --- |
| `platform/hyprland` | ABI gate, events, deferred work, weak window handles, capture leases, seat routing, plugin lifecycle | Package format or public wire protocol |
| `render` | GLES3 resources, mesh/material draw, 4× MSAA HDR target, resolve, output encoding, scoped GL state | Hyprland APIs, input or files |
| `world` | Offline schema checks, safe package paths, cgltf decode, images, explicit collision meshes, spawn checks | GL objects or compositor handles |
| `sim` | Fixed 60 Hz camera state, walk/fly strategies, static mesh capsule queries | Seat dispatch or window identity |
| `surfaces` | Stable surface descriptors and front-facing ray/quad math | Capturing pixels or granting focus |
| `ipc` | Bounded framed JSON-RPC, grants, snapshots/deltas; private build-pinned bridge | Pixels or raw public input injection |
| `daemon` | Public socket and launch grants, command acknowledgements, session revisions | GL or compositor calls |

`hypruned` is spawned with a private inherited `SOCK_SEQPACKET` socketpair. The
plugin commits authoritative changes; the daemon publishes them and answers only
after acknowledgement. Helper death/bridge EOF restores desktop input, normally
on the next tick and always subject to a one-second heartbeat watchdog. An active
render element never performs capture, cursor, focus or workspace operations.
Deferred work uses `doLater` and checks render nesting before running. Shutdown
removes queued work, listeners, hooks and retained pass elements before unloading;
owned GL resources are retired with the compositor EGL context current.

World JSON/glTF/image decoding runs in a joined CPU task. The prior world survives
failed loads. GPU upload and activation happen together on the owner thread.
For M0 the small upload is one transaction; incremental 2 ms upload budgeting and
a full daemon package registry are follow-up work.

## Worlds and graphics

`HYPRUNE_WORLD_DIR` names an **installed directory of packages**, each subdirectory
matching its world ID. It is trusted launcher configuration, never a public RPC
path. The nested harness installs its generated fixture there. `world.load` accepts
only an ID and optional spawn ID, never a path, URI or executable.

Supported: static glTF 2.0 triangles, glTF node world transforms and selected scene,
base color factors/textures, emissive factors, `KHR_materials_unlit`, explicit
static triangle collision, PNG lightmaps, linear or sRGB irradiance encoding and
independent UV0/UV1. Baked diffuse is albedo × irradiance × intensity + emission;
it receives no duplicate direct diffuse term. The world is tone mapped once.
Application pixels bypass the artistic tone curve; a GLES pixel regression test
checks that sRGB 128 remains 128. M0 supports SDR/untransformed outputs.

A 0.3 m radius, 1.8 m standing capsule supplies walking and jumping. The camera is
1.7 m above the feet. Substeps prevent thin-wall tunneling at supported speeds;
there is no stair stepping, dynamic rigid body simulation or navmesh following.
The small fixture uses a continuous collision floor distinct from tile visuals.
The brute-force static collider has a 20,000-triangle cap. It avoids a heavyweight
physics dependency for M0; Jolt/BVH acceleration fits behind the simulation
boundary when larger worlds, stairs or dynamic bodies justify it.

## IPC and current limitations

Protocol `0.1` uses the RFC-0003 socket location and 4-byte big-endian length framing.
It implements hello/ping, subscribe/snapshot/unsubscribe, world.load,
surface.focus, movement.set and runtime.exit. The returned method list is filtered
by grants; unadvertised methods return -32601. Snapshots expose camera and surfaces.
Built-in mode IDs are `com.hyprune.walk` and `com.hyprune.fly`.

Same-UID alone grants no state access. For the nested harness, the supervisor
passes one-use random credentials through an inherited pipe into the helper.
There is no persistent token file and no token in argv. The supervisor currently
seeds a finite set at launch; a user-facing grant/policy manager and supervised
shell launching are M1 work. This is not an extension sandbox.

RFC alignment and the precise acceptance evidence are recorded in
[docs/M0.md](docs/M0.md). In particular, the current public RFC does not specify
`camera.get/set`, `surfaces.list` or `ui.open`. Camera/surface reads use snapshots;
`hl.plugin.hyprune.camera(x,y,z,yaw,pitch)` is a local developer diagnostic/teleport
helper, not public IPC. There is no shell overlay/`ui.open` bridge yet.

Window capture currently leases the committed root surface's normal 2D texture;
it does not invoke snapshot FBO rendering. Up to 16 mapped windows on the selected
monitor are placed in an automatic development layout and kept live with
unsuspension and frame callbacks. Root-surface Wayland terminals work. Popups,
subsurface trees, external textures, transformed/cropped buffers, HDR color
management and arbitrary app compatibility are not M0 guarantees. Hidden
workspace textures remain eligible for capture, but hidden-workspace keyboard
focus is explicitly rejected. See M0.md for what was actually tested.

## Related repositories

Core excludes QML UI, official world art and SDK tooling. See the proposed
[RFCs](https://github.com/hyprune/docs/tree/main/src/content/docs/rfcs),
[schemas](https://github.com/hyprune/schema),
[roadmap](https://hyprune.com/roadmap/),
[organization contributing guide](https://github.com/hyprune/.github/blob/main/CONTRIBUTING.md)
and [Code of Conduct](https://github.com/hyprune/.github/blob/main/CODE_OF_CONDUCT.md).
