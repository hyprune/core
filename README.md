# Hyprune core

“What if the computer itself were an explorable world?”

Hyprune core integrates a small 3D runtime with Hyprland. This is the M1 developer
implementation, pinned to **Hyprland 0.56.x** and checked against the exact running
compositor ABI hash. The shell, worlds, SDK, documentation and schemas live in
separate repositories. This repository contains no proprietary game assets.

## Build and test

Requires CMake 3.27+, a C++23 compiler, Ninja, pkg-config, matching Hyprland plugin
headers (including generated protocols), GLES3/EGL, Wayland client/server and
scanner, libdrm, pixman, xkbcommon, hyprgraphics, pangocairo, Lua, libxml2, OpenSSL,
libpng, FreeType, Fontconfig, Vulkan and GBM, and Python 3 (with `jsonschema` and
`referencing` for the wire tests).
The `hyprland.pc` include paths must point at the headers for the running build,
for example the headers installed by `hyprpm`. With Nix, `nix develop
github:hyprune/hyprune` provides a shell pinned to Hyprland 0.56.2 and every
dependency above. cgltf, nlohmann/json, stb_image, meshoptimizer, the Basis
Universal transcoder (with its zstd decoder) and FidelityFX FSR 1 are vendored with
their licenses; see [third_party/README.md](third_party/README.md). Builds do not
download dependencies.

```sh
python3 tests/fixtures/m2.py        # original CC0 atrium, materials and M2 fixtures
python3 tests/fixtures/climbing.py  # original CC0 climbing fixture
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
dev/nested/run.sh --m1            # also checks IPC 0.2 navigation/UI/data
dev/nested/run.sh --m1 --record   # records the material laboratory
dev/nested/run.sh --serve         # interactive nested session, Ctrl+C to stop
```

Recordings are encoded under `/tmp`; the finished MP4 and its JSON sidecar then
move to `$HYPRUNE_VIDEO_DIR` (default `~/.local/state/hyprune/videos`).

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

M3A defers and coalesces native cursor visibility changes after the frame; a
rendered reticle or virtual pointer marks the world picking ray. Native cursor
images stay with Hyprland. Hardware cursor behavior still requires the owner
supervised checklist in [docs/M3A.md](docs/M3A.md).


## First real session

```sh
dev/hyprune-dev up       # nested Hyprland + real core + real shell + Switchyard/apps
dev/hyprune-dev smoke    # automated end-to-end acceptance, then exit
dev/hyprune-dev record   # 45–75 second 1080p showcase and smoke acceptance
```

Build core first and consume `../worlds/dist/com.hyprune.switchyard` built per its
README (remote Blender only). `HYPRUNE_SWITCHYARD_PACKAGE` overrides the package
directory. The launcher copies that package unchanged and installs an original CC0
quiet-observatory fixture as workspace 11; workspace 10 is Switchyard. It starts
Quickshell over authenticated IPC 0.3, two live terminals (including btop) and a
private-profile Chromium page. All nested session lifetimes hold
`flock /tmp/worldshell-nested.lock`; the verified controller is the only compositor
control entry point. No owner-session command is used. `up` runs smoke acceptance
and remains interactive on the visible nested Wayland output; Ctrl+C stops it.
`smoke --visible` verifies that same output; recording uses 1920×1080 HEADLESS-1. See
[contracts and verification](docs/FIRST-SESSION.md).

This session uses `dev/nested/.integration-state/`, separate from the legacy
fixture harness. For interactive control, prefix each `dev/nested/ctl.py` command
with `HYPRUNE_NESTED_STATE_DIR="$PWD/dev/nested/.integration-state"`.
The integration additionally requires `btop`, Quickshell, and shell's Python
requirements; Chromium is optional.

## Module boundaries

| Module | Owns | Does not own |
| --- | --- | --- |
| `platform/hyprland` | ABI gate, events, deferred work, weak window handles, capture leases, seat routing, plugin lifecycle | Package format or public wire protocol |
| `render` | GLES3 resources, mesh/material draw, 4× MSAA HDR target, resolve, output encoding, scoped GL state | Hyprland APIs, input or files |
| `world` | Offline schema checks, safe package paths, cgltf decode, images, explicit collision meshes, spawn checks | GL objects or compositor handles |
| `sim` | Fixed 60 Hz camera state, walk/fly strategies, static mesh capsule queries | Seat dispatch or window identity |
| `surfaces` | Stable surface descriptors and front-facing ray/quad math | Capturing pixels or granting focus |
| `control` | Camera commands, route following, travel lifecycle, connection-owned leases/data, core UI hold timing | GL, compositor APIs or host metrics collection |
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
base color/emissive textures, all glTF samplers and mipmaps, up to 8× anisotropy,
`KHR_texture_transform` on all five material slots, derivative-frame normal maps,
metallic/roughness and lightmap-modulated key/sky specular, occlusion, `KHR_materials_unlit`, explicit
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

IPC `0.2` adds camera get/set, waypoint/route, timed travel, autodrive, lease-owned
UI open/close and integration publishing. It keeps `0.1` compatibility with its
original state shape. Discover methods/events through hello; grants filter both.
`location` and `integrations` are revisioned state. Warp notifications have stable
operation IDs, output, phase/progress, destination and monotonic timestamps.
Built-in mode IDs remain `com.hyprune.walk` and `com.hyprune.fly`.

Hold Tab for 350 ms in exploration mode to request the leased shell's overview;
release leaves it open. Escape dismisses it; F12 exits. IPC 0.3 adds verified Wayland-child binding and exclusive Director input; 0.1/0.2 retain their exact contracts. See [the first-session report](docs/FIRST-SESSION.md).

Same-UID alone grants no state access. Launch tokens are one-use and connection
bound. Publishing needs the separate `data.publish` capability and authenticated
source namespace, increasing sequence, bounded scalar values and TTL. No host
metrics collection or arbitrary filesystem access is granted. The nested launcher
injects grants through an inherited pipe; a production supervisor/policy manager,
renewal protocol and extension sandbox remain future work.

See [docs/M1.md](docs/M1.md) for measurements, validation and limitations, and
[docs/CHANGES-FOR-SDK-AND-SHELL.md](docs/CHANGES-FOR-SDK-AND-SHELL.md) for the exact
migration contract. [docs/M0.md](docs/M0.md) preserves the earlier acceptance report.
`hl.plugin.hyprune.camera(x,y,z,yaw,pitch)` remains a local diagnostic helper.

Window capture currently leases the committed root surface's normal 2D texture;
it does not invoke snapshot FBO rendering. Up to 16 mapped windows on the selected
monitor are kept live with unsuspension and frame callbacks. Existing clients use
initial or authored/durable placements; newly mapped windows spawn ahead of the
player. M3A adds up to 128 attached xdg popup/subsurface planes and parent-attached
X11 transient menus/dialogs.
External textures, transformed/cropped buffers and HDR client color management
remain unavailable. Hidden workspace textures remain eligible for capture;
keyboard focus requires a pulled tool with a valid workspace binding and deferred
native workspace activation. See M3A.md for current acceptance and limits.

## Related repositories

Core excludes QML UI, official world art and SDK tooling. See the proposed
[RFCs](https://github.com/hyprune/docs/tree/main/src/content/docs/rfcs),
[schemas](https://github.com/hyprune/schema),
[roadmap](https://hyprune.com/roadmap/),
[organization contributing guide](https://github.com/hyprune/.github/blob/main/CONTRIBUTING.md)
and [Code of Conduct](https://github.com/hyprune/.github/blob/main/CODE_OF_CONDUCT.md).


Core M2 adds workspace homes, pulled tools, collision recovery, persistent layouts,
IPC 0.4 and world format 0.3. See [docs/M2.md](docs/M2.md) and
[SDK/shell changes](docs/CHANGES-FOR-SDK-AND-SHELL.md). Run `dev/hyprune-dev m2-smoke`
for locked acceptance or `dev/hyprune-dev m2-record` for the gallery demo. Set
`HYPRUNE_M2_PACKAGE` to a built Lumen Reach directory to load it unchanged after
acceptance; `dev/hyprune-dev m2` keeps the isolated session available for captures.
`build/hyprune-world-check <package-directory>` validates packages without a compositor.

## M3A hands-on parity

Super+LMB carries at the hit point, wheel changes distance, Super+middle rolls,
Super+RMB configures the actual client, and C zooms. Super+Alt or the back button
switches movement/typing. F focuses the aimed workspace wall; the same mode toggle
backs out. Drop a carried window onto a wall to tile it. 1–5 selects tools; slot 2
mounts a tagged window on an arbitrary surface. V toggles noclip and F4 flight.
The Director HUD and Workspaces app list use frozen IPC 0.5 state.

Run `./dev/nested/run.sh --m3a` for locked real-client acceptance, or add `--record`
for a temporary 45–75 second demo. [docs/M3A.md](docs/M3A.md) documents every Lua
config option, parity evidence and the required supervised hardware cursor checks.

## M3B keymap v2

The current hands-on runtime uses `~/.config/hyprune/input.json`, independently
watched and validated without Hyprland reload. Defaults compile from
`config/input.default.json`; the docs Keymap page is generated from it. HJKL /
WASD / arrows move; E uses a window/workspace in place; F zooms into fullscreen;
Super+Tab or Back returns. TAB hold defaults to 1000 ms and opens Map. Q scopes,
Ctrl crouches, U captures debug state. Tools 1–5 show their wheel submodes in HUD.
Settings captures up to three bindings per action through authenticated IPC 0.6.
Matching actions steal colliding Hyprland bindings only in their active mode.

U writes `capture-<ms>.png` + `.json` to `$HYPRUNE_CAPTURE_DIR` (default
`~/.local/state/hyprune/captures`); photo mode stills use the same path. An
optional brand watermark, `hl.plugin.hyprune.config({capture={watermark=true}})`
(default off), stamps the Bold lockup bottom-right on the saved PNG only, in
white or navy chosen from the area underneath, and records it as
`frame.watermark` in the JSON. It is never drawn live. See
[assets/brand/BRAND.md](assets/brand/BRAND.md).

Run `dev/nested/run.sh --m3b` for isolated real seat acceptance, or add `--record`
for the demo. Both use the lifetime flock. See [M3B.md](docs/M3B.md), including the
complete supervised live cursor checklist, input options, contracts and evidence.

For the owner-supervised real-session check, see [dev/live/LIVE-TEST.md](dev/live/LIVE-TEST.md).
`dev/live/preflight`, `up`, `status`, and idempotent `down` swap only the old
hypr3d plugin/HUD and record recovery state. Agents must use explicit nested
instances; `./dev/nested/run.sh --live` provides the locked dry-run.

Graphics presets and individual effects use an independent, watched
`~/.config/hyprune/graphics.json`. Auto selects Low/iGPU on Intel. IPC 0.7 adds
validated `graphics.get`/`graphics.set` for Settings while older clients retain
their method lists. See [graphics controls](docs/GRAPHICS.md) for every option,
GPU cost attribution and the native-resolution composition contract.

M4 phase 1 adds optional runtime dGPU offload, **off by default**, and exact IPC
0.10 Performance controls. Build also requires Vulkan and GBM development packages
for `hyprune-renderer`; the local path continues to run without a discrete GPU.
See [graphics controls](docs/GRAPHICS.md) and [M4 acceptance](docs/M4.md).

### Playable bounds and backdrop checks

World format 0.6 optionally declares world/area `playable` boxes or convex hulls
and `maxFlyHeight` (absolute eye Y). Fly eases against the authored region plus
`player.fly_bounds_allowance` (live Lua config, default 0.5 m, range 0–20 m).
Walking keeps its collision behavior; noclip ignores bounds and respawn, with a
`world.bounds.left` notice on each outward crossing. Older worlds are unchanged.

`backdrop.farCards` declares glTF node subtrees for sky-card sampler checks.
`build/hyprune-world-check PACKAGE` emits CLAMP_TO_EDGE warnings on stderr and
in its JSON `warnings` array without rejecting intentional clamping. See the
[RFC-0004 amendment](https://hyprune.com/rfcs/0004-world-format/).

Regression commands: `ctest --test-dir build --output-on-failure` and
`dev/nested/run.sh --playable` (the runner acquires the nested lifetime flock).

## License

Source is MIT ([LICENSE](LICENSE)). Assets generated by `tests/fixtures/generate.py`
and the other fixture generators are dedicated under CC0-1.0; the generators
themselves are MIT. The Hyprune logo in `assets/brand/` is CC-BY-4.0 artwork
(see [BRAND.md](assets/brand/BRAND.md)). Vendored dependencies retain their own licenses (MIT,
Apache-2.0 with NOTICE, BSD-3-Clause); see [third_party/README.md](third_party/README.md).
