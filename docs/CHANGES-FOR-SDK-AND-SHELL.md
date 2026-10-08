# Core M2 contract changes

Core 0.3.0 negotiates IPC **0.4**; 0.1, 0.2 and 0.3 keep their method/state
projections. Use `schema.validate('ipc', frame, responseMethod, '0.4')`. World
format **0.3** adds workspace homes, typed mounts, data objects, reserved placement
volumes and atmosphere. Format 0.1/0.2 packages remain accepted. Canonical schemas
are pinned to schema `9dde810`; RFC-0004/0005 M2 amendments are normative.

An area is one workspace home: its `workspaceSlot` is an author string such as
`"01"`, and core binds an actual compositor workspace ID to the area. The primary
anchor shows the workspace; focus-in returns to full stock Hyprland. Pulled tools
retain their owner's area and use its free mounts. Same-world workspace changes
travel to their home; cross-world changes use the existing warp lifecycle.
`workspaces.locations` also projects these bindings for the Director.

| Method | Params / result | Grant |
| --- | --- | --- |
| workspace.bind | `{id,worldId,areaId}` | world.control |
| workspace.focus / workspace.unfocus | `{id}` / `{}` | surface.control |
| surface.pull / surface.return | `{surfaceId}` | surface.control |
| surface.place | `{surfaceId,anchorId?}`; absent anchor selects a free owned mount | surface.control |
| surfaces.recall | `{}`; recalls live pulled tools on the selected output, including saved tools from other worlds | surface.control |
| placements.list | `{}` → `{placements}` | state.read |
| placements.remove | `{worldId,areaId,rule:{appClass,title}}` | surface.control |
| data.object.bind | `{objectId,sourceId}`; publisher's namespace only | data.publish |
| atmosphere.get | `{}` → `{profile,quality}` | state.read |
| atmosphere.set | `{profile}` | world.control |
| camera.fov | `{degrees}`; vertical FOV 30–110 | movement.control |

Mutations return `{revision}`. New state keys are complete atomic replacements:

- `mounts:{areas,anchors,objects,bindings,placements,placementZones,instances,carriedSurfaceId,desktopWorkspaceId}`.
- `presentation:{profile,fov,quality}`; quality is `high|intel`, profile is null before loading.

Areas contain `id,workspaceSlot,primaryAnchor,toolAnchors,dataAnchors` plus optional
`theme,spawnId,freePlacementZones`. Normalized anchors contain `areaId` and
`mount:{type,role,interaction,frameNode?,limits?}`. Objects publish
`id,areaId,pose,mountType,sourceId`. Placements use stable world/area/anchor IDs
and exact `{appClass,title}` rules; an empty title is a class-wide rule. Instances
resolve these to current `surfaceId,workspaceId,areaId,anchorId,status`.
Status is `carried|placed|recalled`. `desktopWorkspaceId` is null in exploration.
The daemon saves bindings/rules atomically to `$XDG_STATE_HOME/hyprune/layout.json`
(or `$HOME/.local/state/hyprune/layout.json`); `HYPRUNE_LAYOUT_FILE` isolates tests.
Recall is temporary and preserves the durable rule. Identical live rule matches
occupy distinct free mounts without rewriting each other's saved destination.

New hints `mounts.changed:{mounts}` and `presentation.changed:{presentation}` carry
`sessionId,revision`. Reconcile with snapshots/deltas after reconnect; hints are
not authoritative patches. Placement recovery uses existing `runtime.notice`
with **only** `level,code,message`: `placement.relocated` or `placement.recalled`.
That frozen notice shape does not gain session/revision fields.

Carried rectangles use support rays plus a full swept OBB against collision;
placed rectangles sweep the mount's approach and push outward with a margin.
Changed/blocked mounts relocate to a free owned mount, then a reserved area
volume, then safe recall. Mount quaternions drive rendering and pointer projection,
including tilted lecterns. In-place mouse motion, clicks, scroll and drag go to
native seat input; text uses the original keyboard path. No public input-injection
RPC exists. Focus/workspace/button mutations run outside rendering; pending
input callbacks are cancelled on unload. Apps retain their original workspaces. Clicking a pulled tool in a different
home activates its native workspace while preserving the camera at that mount;
keybind/Director workspace selection still travels to the home.

`atmosphere.set` changes fog, exposure, tone mapping and bloom immediately.
Changing sky/environment assets requires package reload and returns -32007 here;
core never opens paths supplied over this method. Per-area atmosphere remains a
future amendment; this contract is per-world. `camera.get` retains its older shape;
read FOV from `presentation`.

Lumen Reach format 0.2 adapters accept `com.hyprune.lumen-reach.mounts` version 1
(`areas,mounts,objects`) and `com.hyprune.lumen-reach.placement` version 1 (`zones`).
`freePlacementZones` contains string IDs; zone objects live in `placement.zones`.
Authoring `name` and approach annotations are stripped before canonical validation.
Roles `tool|data` normalize to `tool-mount|data-object`. Physical frame nodes are
re-resolved on load and never persistence identity. Names and area slots remain
Sun Court/01, Array Hall/02, Rime Vault/03, Signal Rise/04.

`com.hyprune.lumen-reach.routes` version 1 validates up to 64 feet-position nodes
and 256 bidirectional edges `{from,to,width}`. Places add the 1.7 m eye offset.
Nodes optionally declare `label` (1–256 characters) and boolean `waypoint`.
All nodes remain in pathfinding; only nodes with `waypoint` absent or true appear
in `spatial.world.places`. Their `name` uses `label` when supplied, otherwise the
stable node ID. Omitted fields retain the default visibility and ID label.
A single `route.set` destination at a declared node expands along the shortest
graph path from the nearest node to the camera. Explicit multi-point and off-graph
routes retain their behavior. Disconnected destinations fail; capsule collision
still stops blocked paths. Authored ramp bends/landings must be graph nodes.
Continuous ramps work; discrete stair stepping is not implemented.

See [M2.md](M2.md) for measurements, acceptance evidence, capture hooks and the
required supervised hardware-cursor test.

---

The following historical M1 notes describe their original negotiated versions;
the amendments above and FIRST-SESSION.md supersede their listed gaps.

# Core M1 contract changes

The IPC 0.3 first-session amendment supersedes the exclusive-input, map and
workspace gaps in this historical M1 report. See [FIRST-SESSION.md](FIRST-SESSION.md)
and RFC-0003/0004/0008. Frozen 0.1/0.2 behavior below is retained.

Negotiated IPC **0.2** is implemented. Core still serves exact **0.1**, with only
its original state keys/events/methods. `0.1-mock.1` remains mock-only. Select
`schema.validate('ipc', frame, responseMethod, '0.2')`; defaults remain 0.1.
Schemas: sibling schema `v0.2/ipc.schema.json`; rationale: RFC-0003 M1 amendment.

| Method | Params | Grant |
| --- | --- | --- |
| camera.get | `{}` → `{position,orientation}` | state.read |
| camera.set | `{position,orientation}` | movement.control |
| waypoint.set | `{id,label,target}` or mock-compatible `{id,label,position}` | world.control |
| waypoint.clear | `{}` | world.control |
| route.set / route.clear | `{points:Target[]}` / `{}` | world.control |
| travel.start / travel.cancel | `{to:Target,durationMs?:600}` / `{}` | movement.control |
| autodrive.start / autodrive.stop | `{speed?:3.5}` / `{}` | movement.control |
| ui.open / ui.close | `{leaseId,outputId,panel}` / `{leaseId,outputId}` | shell.control |
| data.publish / data.remove | `{sourceId,sequence,values,ttlMs?:5000}` / `{sourceId}` | **data.publish** |

Mutations return `{revision}`; shell.claim returns `{leaseId,revision}`. Existing
shell.claim/release/overlay are now implemented, with connection ownership and
output conflicts enforced. `exclusiveInput:true` is **unsupported (-32007)** until
the launcher supplies verified Wayland-client association. Do not map a Director
that assumes exclusive seat ownership. Nonexclusive overlays and passive warp
presentation work; full RFC-0008 interactive conformance is still pending.

`Target` is exactly `{zoneId}` or `{position:[x,y,z]}`. Point coordinates and
camera positions are world-space eye positions in metres; a zone resolves to
bounds X/Z centre and min Y + 1.7. Camera quaternions are `[x,y,z,w]`; roll and
near-vertical pitch are unsupported. Travel requires active world mode, validates
the destination, intentionally passes through geometry during the warp, and
returns when the transition starts. Zero duration is reduced-motion placement.
Autodrive follows up to 64 explicit route points using capsule collision and stops
at an obstruction; it does not find a route around obstacles.

State keeps the original five keys and adds:

- `location: {zones,waypoint,route,autodrive,transition}`. Waypoint is null or
  `{id,label,target,position}`; route is `[{target,position}]`; autodrive is boolean.
  Transition is null or `{id,outputId,phase,progress,to,monotonicMs}`.
- `integrations: {sources:[{sourceId,sequence,values,monotonicMs,ttlMs}]}`.

Replace these keys atomically in state.delta, exactly like existing keys. This
replaces the mock's replayed `world.metadata`; **there is no world.metadata event**.
Zones and waypoints are recoverable in the subscription baseline.

Additional events (all also carry `sessionId` and `revision`):

- `location.transition {id,outputId,phase,progress,to,monotonicMs}`. Stable ID per
  operation. Phases: loading/departing/travelling/arrived/failed/cancelled. Loading
  progress is null; other progress is 0..1. `to` is a Target, or `{worldId,spawnId?}`
  during world.load. outputId can be null before monitor selection. Loading
  events are flushed before the pending world.load response. Arrival follows
  committed activation; failed loads preserve the old world. A detected requester
  disconnect before activation cancels the load. Snapshots retain the last terminal
  transition; discard stale-session/older-revision events and reconcile on reconnect.
- `ui.opened {leaseId,outputId,panel}` / `ui.closed {leaseId,outputId}`. These are
  event names; ui.open/ui.close are methods. Core consumes Tab and owns the 350 ms
  hold. Short taps do nothing, release keeps overview open, Escape dismisses,
  F12 exits. Only the current lease receives the request logically; subscribers
  must match lease/output before displaying it. Lease disappearance hides UI.
- `data.updated {sourceId,sequence,values,monotonicMs,ttlMs}`. Presentation hint;
  revisioned sources are authoritative and disappear on expiry/disconnect/remove.

Publishing no longer piggybacks on state.read. Request **data.publish** in the
one-use token grant. sourceId must equal the authenticated client ID or be beneath
its dot namespace. One connection owns each source. Sequence must strictly
increase within its live lifetime. Values are complete replacement scalar maps:
≤32 properties, strings ≤256 characters, ≤16 KiB per map, ≤64 KiB aggregate. TTL
100–60000 ms; ≤32 sources; ≤10 publications/sec/source. No host `/proc`, Wayland,
network or filesystem permission is implied. Unknown/unadvertised methods return
-32601; ownership/stale-sequence conflicts -32004; limits -32008.

Bootstrap names ratified for supervised SDK clients: HYPRUNE_SOCKET,
HYPRUNE_CLIENT_ID, HYPRUNE_PROTOCOL, and one-shot HYPRUNE_TOKEN_FD containing
64 lowercase hex bytes plus newline. Core's HYPRUNE_LAUNCH_FD is a **different**
private JSON grant pipe. Fresh tokens on reconnect still require a supervisor;
no public issuance/reissue method or blanket persistent token was added.

Still deferred from shell notes: Wayland output descriptors (keep launcher mapping),
projection/marker data, world/map/catalog resource delivery, workspace.activate and
workspace/location mapping, settings/approval data, production renewal and trusted
Wayland association. `runtime.outputs` is M1's selected world monitor, retained
while inactive. UI panels remain bounded plain identifiers; no code/assets execute.

World format **0.2** has the same manifest fields as 0.1 and makes the new glTF
material profile explicit (RFC-0004); 0.1 packages remain accepted. UV0 tiles/trims;
UV1 stays independent baked irradiance. Switchyard can use standard glTF samplers,
KHR_texture_transform, normal, MR, occlusion and emissive slots. No mock names or
ad-hoc glTF extras are needed for its lightmaps.

## M3A — IPC 0.5

Negotiate exact `0.5` to receive `interaction:{mode,tool,tools,noclip,cursor,taggedSurfaceId}` and `apps:[{surfaceId,workspaceId,title,appClass,icon}]`. Mode is movement/typing/workspace/fullscreen; tool is 1–5, tools contains five pointer/mount/path slot names. The icon is a local theme name derived from class. Group apps by actual workspace ID in the Director. No title/path scraping or mock-only mode channel is needed. Older projections omit the two keys.

Arbitrary placements add a metre `pose` and `size`, with empty `anchorId`, to the existing world/area/class/title rule. Authored placements retain their original shape. 0.1–0.4 peers omit arbitrary rules from state/lists. Existing mutation methods and grants remain; there is no public raw-input or config RPC. Use the local validated Lua `hl.plugin.hyprune.config` API documented in [M3A.md](M3A.md). SDK types and offline validators support 0.4/0.5; the legacy mock still advertises only the versions it implements. Shell mode/slots/apps use authoritative state and method discovery.

## M3B / exact IPC 0.6

Core input.default.json is canonical; docs generates Keymap from it. Independent
input.json is watched and persisted only by core. New input.control grants
input.set/capture/capture.cancel; state.read grants get/preview. Capture requires
the verified Director lease. Preview returns canonical config, internal conflicts
and mode-scoped steals Hyprland descriptions. Internal conflicts reject save.
Snapshot/delta keymap includes config/path/conflicts/steals/capture/error; editor
includes four-mode state, tool/submode, selection, paths, objects, locked IDs and
debug toggle. FOV permits 3° for scope in 0.6; older projections remain ≥30°.
ui.opened.panel map selects Director Map. Existing exact versions remain frozen.

Shell opts into 0.6, requests input.control and provides Settings, mode/submode
HUD and capture feedback. It never writes config. SDK pins 0.6 and exposes typed
input methods/state; default 0.2/mock remains unchanged. RFC 0003/0006/0008 and
schema 0.6 are the contract. See M3B.md for behavior, tests and live cursor checks.

M3B local input uses keymap v2 for all IPC peers. Older protocols keep their
wire projections; they do not select a legacy physical keymap. Lua input key
names are superseded by the independent file. M3A interaction documentation and
recordings are historical; use the M3B acceptance harness for current defaults.

## Live follow-up: IPC 0.6 native desktops

Native workspaces are now published as workspace surfaces even without world
home bindings. Shell merges these with workspaces.locations; desktop-only cards
focus their surface instead of invoking workspace.activate. apps includes native
mapped toplevels on all outputs (capture-independent), with a 128-entry bound in
schema 0.6. 0.5 remains bounded to 16 through its compatibility projection.
Refresh the 0.6 schema pin in core, SDK and shell together. No new fields/methods
or input-file version are introduced. Native surface IDs are session-scoped.

Shell presentation restarts reconnect to the launcher-owned authenticated bridge
rather than reading inherited credential pipes again. Only descendant peers of
the same UID are accepted; the bridge binds the replacement Wayland PID. Director
uses anchored pages to avoid the observed Qt StackLayout crash. Back from world
control gives a real layer cursor; Back/Super+Tab returns. See M3B.md for the
added supervised cursor transitions and owner retry notes.

## Live retest round 2 / input v2 compatible amendment

IPC 0.6 input schema accepts optional `invert_y` (boolean, default false) and
optional debug/capture action arrays in interactive/focus/menu. Older keymaps
remain valid; core canonicalizes these defaults on read. Settings exposes invert
Y and the additional action slots. Bind interception stays mode scoped.
Core draws diagnostic FPS/timings/collision wires. World uses F3/Y and U;
interactive, focus and menu use F3 and Super+U so client text remains typeable.
U publishes private JSON/PNG pairs from the last completed core world frame,
including frame age, and posts a path notice. This adds no cursor transitions and
no compositor snapshot calls. Native focus captures explicitly retain the last
world view; they do not capture desktop/layer pixels.

## Amendment: world-locked overlays and screen UI

Core owns world-locked presentation: active waypoint marker/label, its route,
object/window/workspace interaction prompts and optional area/place labels.
These are drawn in the world render pass from **that frame's camera**, not from
shell IPC projection samples. The default displays no area/place markers; input
v2 `show_area_markers` is an optional boolean, default false, exposed in Settings.
Interaction prompts appear at aimed, unobstructed usable surfaces. Routes are
depth-tested world lines. Labels/icons are screen-space billboards anchored by
the same frame projection. Labels are intentionally readable through geometry;
route lines retain depth occlusion. Native fullscreen handover hides world-locked
presentation. Core diagnostics remain available as a screen overlay.

Shell owns Director, menus, notices and other screen UI, including an optional
compass/minimap. The official IPC 0.6 shell stops drawing IPC-projected world
markers. Projection data stays available for alternate/older shells, with its
original timestamp/coordinate contract, sampled at up to 10 Hz. Periodic state
publication is up to 30 Hz; explicit actions and UI events publish immediately.
World-locked rendering is independent of either publication rate.

IPC 0.6 adds `overlay.style` under `shell.control`: params `{leaseId, style}`
require the connection's currently owned shell lease; result `{style}` returns
the full canonical theme. Earlier protocols do not advertise or accept it.
`style` is a closed partial object with `fontFamily` (1–96 printable bytes),
`waypointColor`, `routeColor`, `labelColor`, `promptColor` (three finite sRGB
components each, 0–1), and optional `iconAtlas` (null means built-in symbols).
The atlas descriptor is `{path,cellSize,waypointIndex,promptIndex}`: absolute
local path, square cells 16–128 pixels, indices 0–255. Core accepts only a regular
same-UID non-symlink PNG, at most 4 MiB encoded and 1024×1024 decoded, with whole
cells and in-bounds indices. Icons use alpha coverage tinted by marker/prompt
colour and render at 24 output pixels. Font families resolve through Fontconfig;
the first implementation covers printable ASCII, with `?` fallback for other
label glyphs. Colours and fonts have defaults even without a shell.

Theme file I/O, decode, font rasterization and GL uploads run in deferred owner
work, never inside a compositor frame. Equal themes do no work; colour-only
changes reuse the existing atlas. Invalid themes leave the prior style active.
The trusted bridge inserts its owned lease; QML never receives credentials or
writes core configuration files. Themes are runtime state, not a package edit.
The official shell supplies its Tokens palette and font when it receives a lease.

### Live round 3 interaction corrections

Normal-tool clicks now remain in world mode; only E enters window-relative use.
Movement bindings also work with Super during owned gestures. Carry follows yaw
only, preserving pitch/roll. Free-window quads follow current logical client
size × windows.scale; authored mount fitting is unchanged. No IPC wire changes
or shell upgrade required. See M3B.md for evidence and supervised checks.

### Live round 3 rendering

Core now scales only 3D world rendering, preserving native windows and core
markers. Lua graphics options: render_scale="auto" (or 0.375..1), target_fps=60
(30..240 integer). Diagnostic captures include internal/native dimensions and
current scale. No public IPC/schema fields change; shell rendering and input
contracts are unchanged.

## Graphics Settings integration (core-only handoff, 2026-10-08)

Core adds IPC 0.7 `graphics.get` (`state.read`, empty params) and `graphics.set`
(`world.control`, `{patch:{...}}`). Both return `{config,effective,overrides,path,
error,presets,options,unsupported,timings}`. Settings should request `world.control`
in its existing grant. The core validates, atomically persists the sparse patch
in `$XDG_CONFIG_HOME/hyprune/graphics.json` (fallback `~/.config`, test override
`HYPRUNE_GRAPHICS_FILE`) and applies it without any Hyprland reload. Core watches
atomic replacements. Invalid reloads retain the last valid settings and expose
`error`. File values override Lua graphics setup; absent file keys inherit Lua.

`config` contains validated requested values; `effective` resolves the preset.
`quality`: `auto|low|medium|high|ultra` (`intel` remains a legacy Low alias).
Auto chooses **Low/iGPU on Intel, Medium otherwise**. The `presets` object gives
all exact effect defaults. Render controls from `options`, display requested
null as **Preset**, and provide an explicit override for every effect:

- `msaa`: null or 1 (off), 2, 4 samples; clamped to device support.
- `bloom`: null or `off|low|high` (low extraction only; high separable blur).
- `sky`: null or `off|gradient|environment` (cheap gradient or authored environment sky).
- `sharpen`: 0..1, default .35; zero disables reconstruction sharpening.
- `fog`: null or `off|linear|height` (distance-only or integrated height density).
- `reflections`: null/bool, environment-cubemap IBL, requires specular.
- `normal_maps`, `specular`, `ambient_occlusion`: null/bool, separate material features.
- `alpha_haze`: null/bool, blended world materials (including transparent glass).
  Masked cutouts and native application surfaces are retained at all settings.
- `anisotropy`: null or 1/2/4/8/16; explicit nearest samplers are preserved.
- `dynamic_resolution`: bool. With auto scale, adapt to GPU budget when enabled;
  disabled freezes the current scale (initially device/output-dependent).
- `render_scale`: `"auto"` or 0.375..1; fixed values disable adaptation. Native
  window surfaces, world markers and prompts always use output resolution.
- `target_fps`: integer 30..240, default 60; dynamic controller budget, not a cap.
- Existing `fov` 30..110, `exposure` .01..16, `bloom_strength` 0..4 and
  `reduced_motion` bool remain available via the same patch.

SSR, runtime shadow maps and contact shadows are **unsupported**, not pretend
quality switches. Authored baked shadows/contact darkening remain in lightmaps.
F3 exposes asynchronously measured GPU stage costs. Material effects share one
world shading pass; its cost is explicitly labelled shared, never duplicated or
misrepresented as a separately timed normal/fog/specular cost. `timings` includes
`gpuEffectsMs`, rendered/reused frame counts and native/internal dimensions.
Unavailable/disjoint queries give null. Poll diagnostics only while Settings is
open (e.g. 1 Hz); do not drive world marker positions from these responses.

**Contract handoff:** work is confined to core per lead coordination. The
companion `docs/graphics-ipc-amendment.json` contains the exact IPC 0.7 schema
addition for the schema/docs owner to apply to canonical schema and RFC-0005/0008;
shell/SDK must adopt that amendment before enabling these controls. No shell,
canonical schema or external docs checkout is edited by this core task. Existing
0.1–0.6 peers neither advertise nor accept these methods.

Renderer interface for the independent GPU-offload spike: `applyGraphics(config)`
requires the owner EGL context outside the render pass and updates scalar controls
and samplers only. `draw(target,width,height,camera,surfaces,typing)`, native-surface
composition, `snapshot()` and texture ownership stay unchanged. Seven disjoint
non-nested timer groups measure sky/world/resolve/bloom/upscale/native/output.

Native-composition detail for the offload spike: alpha 1 in the native intermediate
means linear world colour; alpha 0 now means already encoded client/overlay colour.
The output shader preserves the latter without a decode/encode round trip.
MSAA-off renders directly into resolve colour/depth textures; the multisample
renderbuffer target is unused in that mode. Target ownership and external draw /
snapshot signatures are unchanged.


### Landed shell reconciliation: graphics 0.7 and holds 0.8

Shell 36cb85f already consumes the exact graphics amendment. Canonical schema
commit e0739d1 publishes its byte-identical IPC 0.7 file; SDK types now include
`GraphicsMethodMap` (0.7) and `HoldMethodMap` (0.8). Live tooling opts into 0.7,
so World graphics appears with the shipped shell. No shell files were edited.

For animated HoldRing, explicitly negotiate 0.8 and consume `keymap.holds`.
Each pending hold has `{action,binding,startedAtMs,durationMs}`. For `menu`, use
`clamp((CLOCK_MONOTONIC_ms-startedAtMs)/durationMs,0,1)`; bridge can calculate
remaining milliseconds on receipt and let QML animate locally. Empty/removal
means cancelled or fired; also clear on disconnect. Duration-only reload keeps
start time. No per-frame progress IPC or shell keyboard interception is needed.
0.7 remains frozen and does not receive holds. The shell's 0.8 pin/bridge/ring
wiring is a small follow-up for its owner; do not enable 0.8 on its current build.

The old first-session smoke used the removed Lua camera setter, a 500 ms TAB
hold and click-to-type semantics. Current smoke uses camera.set, 1150 ms TAB,
E to use and explicit Super key events for exit; exact terminal contents remain
asserted. This does not reintroduce click-to-type into world mode.

### Rendering diagnostics / offload interface follow-up

`graphics.get.timings.upload` reports `pending`, `steps`, `bytes`, `cpuMs`,
`maxStepMs`, `maxJobMs`, `slowestJob`, `completedJobs`, `jobs`. This is within the
existing extensible 0.7 diagnostics object. Core keeps the established loading
transition active while upload jobs/fences advance; shells need no new event.
Budgets are 2 ms / 2 MiB per deferred tick, 256 KiB transfer chunks; individual
GL driver calls remain non-preemptible. The settings' effect timing groups stay
honest: material switches share a shading pass and are not independent timings.

Native composition now tone-maps the reconstructed opaque world before blending
premultiplied client pixels and depth-sorted world glass/haze. The native target
is entirely display-encoded; offload should provide HDR opaque colour and depth
before that boundary. Public target ownership is unchanged. New world-loading
entry points: `beginPrepare`, `continuePrepare`, `cancelPrepare`; synchronous
`prepare` remains for offline tests. No edits to the offload spike were made.


## IPC capability inheritance repair (0.8)

All core version gates now use `ipc/Protocol.hpp`: canonical numeric major/minor
parsing, `atLeast` feature floors, and one supported-version inventory. Hello
negotiates only published supported versions; unknown future offers are not
accepted merely because their number is larger. Advertisement and invocation
share the same method eligibility check. Older state projections remain frozen.
IPC 0.8 now inherits verified exclusive `shell.overlay` (0.3+) and custom
placement poses (0.5+). No schema or SDK type change is needed.

The supervised launcher now explicitly requests 0.8; update shell to 98b677a or
later for animated hold timing. The socket regression iterates every supported
version, testing inherited methods/events/grants, direct dispatch, snapshots,
mount events and hold timing. Adding a version automatically extends the matrix.

Validation: 9/9 CTests. Under the nested flock,
`python3 dev/nested/protocol_acceptance.py` passes with shell 98b677a: animated
HoldRing progress/cancel/fire, Director exclusive input, real Settings dropdowns
and persisted graphics, and native `placements.list` retaining a saved pose.
No cursor implementation changes; existing deferred Director transitions apply.


## Dedicated room workspace (supersedes monitor-wide adoption)

The world now enters native workspace 8 on its selected output, remembering and
restoring the prior workspace on deactivation. `windows.world_workspace` in the
Lua config selects another positive ID (1–999999), effective on next activation.
An active-only `window.openEarly` listener sets the initial floating flag after
native workspace redirects resolve, before insertion into a layout. There is no
runtime rule to leak to redirected clients or leave behind; unloading removes the
listener. No compositor config edits are needed.
Only room clients are implicitly adopted and spawned; other workspaces retain
their layouts. Explicit workspace-wall/mount clients remain supported. Exiting
E/F restores the room; toggling off from focus restores the pre-world workspace.
The Director's existing native workspace/app inventory naturally includes the
room. No new wire fields, SDK types or schema version are introduced: room
selection is compositor setup, not an input or graphics setting. Existing
`workspace.bind` still assigns real workspaces to authored area walls; it does
not choose the room. Do not bind a room as a workspace wall merely to adopt apps.
See M3B's updated lifecycle and supervised cursor checklist.


## Shell HUD concepts: core request (owner, overnight)

The owner requests that **5** (currently reserved tool slot 5) cycle shell HUD
concepts while testing. Please add a **rebindable `hud.cycle` input action**,
bound to `5` by default in world/navigation mode, consuming that key instead of
selecting reserved tool 5. Preserve ordinary application typing and menu input.
On each non-repeat press, publish a scoped event the associated shell can handle
once (suggested `ui.hud.cycle`, carrying leaseId/outputId plus session/revision or
an event sequence for duplicate suppression). Core owns key interception; shell
owns the concept list, persistence and brief concept-name banner. Please record
the exact event/negotiated version here when landed; do not widen frozen 0.8
without the matching schema/SDK publication. The shell will integrate the landed
contract. Until then, its development-only `preview cycleHud` IPC call invokes
the same switch/persistence/banner path for nested testing and video.

Concept order: Vanguard → Instrument → Minimal → Vanguard. This is presentation
only, not an editor tool mutation. The reference is private; no reference image,
third-party logo, game name or copied icon is being added to shell/core assets.

### Core response / in progress: HUD 0.9, Performance 0.10

Core is implementing exact IPC **0.9** for HUD data first. Planned event names:
`hud.frame` (opt-in, maximum 4 Hz), `hud.pose` (opt-in, maximum 30 Hz),
`zone.entered`, `zone.exited`, `tool.changed`, and **`ui.hud.cycle`**.
The last event is sent only to the verified associated shell, with
`leaseId`, `outputId`, `sequence`, `monotonicMs`, plus `sessionId`/`revision`.
`hud.cycle` replaces reserved `tool5` in the input action list and defaults to
`5` in world mode only. Core owns press/release and repeat suppression; the
shell owns concept order, persistence, and its banner. Canonical schema and SDK
publication will follow here before enabling the new protocol in live tooling.

Offload/performance is planned for **0.10**, preserving frozen 0.9 graphics:
requested/effective settings, preset versus Custom overrides, live off/auto/on,
active renderer, warming/fallback reason, power/profile/cap and FPS, dynamic
scale min/max, and existing effects with shared/independent GPU costs identified.
Hot switches keep the local renderer prepared and drawing throughout startup;
no reload/restart is part of a settings mutation. Offload remains default off.
Do not treat this paragraph as a landed schema; exact handoff follows validation.

### IPC 0.9 implementation available for integration

Canonical schema `schema/v0.9/ipc.schema.json` and `input.schema.json` are committed
in schema `d9da091`. SDK generation now includes `HudMethodMap`, `Ipc09_*` and
protocol `'0.9'` (tests green). Core unit tests are green; nested acceptance is
being run before its commit. Exact payloads are defined in that schema and the
RFC-0008 HUD amendment; no graphics fields changed in 0.9.

Call `hud.subscribe({topics:["hud.frame","hud.pose","zone.entered","zone.exited","tool.changed"]})`
with `state.read`; retain returned `subscriptionId` for `hud.unsubscribe`.
No state subscription is needed for these opt-in streams. Common event fields:
`subscriptionId/sessionId/revision/monotonicMs`. `hud.frame` adds
`fps/frameMs/cpuMs/gpuMs/renderScale/preset`; `hud.pose` adds
`outputId/worldId/worldName/position/heading/pitch/zone/tool/subtype`.
Heading is radians, zero -Z, positive right; +Y up. Zone is null or
`{id,homeName,worldId,worldName}`. Enter/exit events carry `zone`; tool change
carries `slot/name/subtype`. Initial zone/tool come from pose; transitions are
not replayed. Slow readers drop stale telemetry. No player resource stat exists.

**Bind the mapped HUD early** using `shell.bind`, before the first Director open:
5 emits `ui.hud.cycle` only to the verified associated lease owner with a state
subscription. Payload `leaseId/outputId/sequence/monotonicMs/sessionId/revision`.
Core does not change tools. Canonical world action is `hud.cycle` (not `tool5`);
input files migrate the reserved binding, and old peers receive projected names.
Please use numeric protocol floors in bridge code; its explicit version lists
currently stop at 0.8 and would otherwise silently disable 0.9 UI behavior.

Nested HUD acceptance passed: 42 pose and 8 frame samples over two seconds,
verified native 5 press/repeat suppression with scoped lease delivery, no tool
mutation, zone enter/exit, subtype notification, live rebind and unsubscribe.
Evidence: `docs/evidence/hud-09/acceptance.json`. Core CTest 9/9; schema 89/89;
SDK 55 passing / 2 intentional skips. HUD concepts themselves remain shell-owned.

### Shell request: authenticated active-world HUD style discovery

Owner requests optional `extensions["com.hyprune.hud-style"]` pointing at a
package-relative `hud/theme.json`. Shell will validate styles and sanitise SVGs,
load only package fonts into a private immutable cache, preserve layout/input,
and expose a saved “Use world HUD styles” opt-out. Please expose the **resolved
installed package root for the active world**, its world ID and content identity
(or a bounded validated asset-read method), to the authenticated associated shell.
Do not take a filesystem path supplied by a manifest as authority. Prefer a new
versioned metadata method/field; existing frozen IPC must remain unchanged.
Until this lands, shell can demonstrate styles only for launcher-authorised
`--world-package` paths, using the active IPC world ID to select among them.
No scanning arbitrary filesystem paths from QML. Proposal/final shell format:
`shell/docs/proposals/world-hud-style.md` (being reconciled with personal-world).
RAM usage and system-wide CPU utilisation are also unavailable in HUD 0.9;
current resource choices explicitly fall back to GPU/CPU render frame budget.

### Shell 0.9 integration blocker (strict published schema)

Actual nested `state.subscribe` response fails canonical 0.9 validation at
`result.snapshot.state.interaction.tools`: core sends
`["pointer","mount","path","path"]` (four entries), but published schema's
legacy `interaction.tools` requires five. Shell cannot obtain its first snapshot
or lease. Please preserve the legacy projection's fifth entry (editor/HUD tool
slots can remain 1–4) or coordinate a canonical contract correction. Shell has
pinned d9da091, implemented early bind, scoped/repeat-safe `ui.hud.cycle` and
opt-in telemetry; nested concept capture stays on 0.8 until this resolves.

Core correction for the 0.9 bootstrap blocker: `interaction.tools` retains its
five-entry legacy descriptor array in all existing protocols. That is a frozen
wire field, not the selectable tool inventory. Native actions and `hud.pose.tool`
remain slots 1–4; 5 is exclusively `hud.cycle`. The accidental four-entry
publication is removed in the current build (rebuilding now). No schema widening
or shell pin change is needed. Thank you for the strict end-to-end check.

Active-world HUD style discovery: request received. The approved installed root
and identity must come from core authority, never the manifest's arbitrary path.
Keeping the current launcher-authorised package fallback during M4 integration;
no unvalidated filesystem discovery field will be slipped into frozen 0.9.

### Performance 0.10: implementation and draft canonical schema ready for review

The current core build negotiates 0.10. `graphics.get/set` preserve their existing
fields and add `performance`; canonical schema is in `schema/v0.10/ipc.schema.json`
(under validation before commit/pinning). Old 0.7–0.9 peers still receive their
original config shape; new fields are rejected on writes from those versions.

New graphics keys: `offload: off|auto|on` (default off),
`offload_power_cap_w` (default 80, range 20–100; effective profile cap may be lower),
`scale_min` (0.375), `scale_max` (1.0), bounds 0.375–1, min ≤ max. Existing keys
cover all other controls on the owner's unified Performance page.
`performance` includes `mode/activeRenderer/status/reason/profile/powerW/capW/targetFPS`,
`preset` (`custom` when an explicit non-quality setting is overridden),
`requested/effective`, `controls`, `costsMs`, `workerTimings`, `pid/generation`.
Each control carries `requested/effective/gpuMs/sharedPass/passGpuMs`; null cost
means unavailable or not individually isolatable. Never label a shared world
shading pass as the exclusive cost of fog, normals, AO or reflections. MSAA's
resolve cost also excludes its shared scene shading cost. Poll graphics at up
to 4 Hz while the page is open; state-changing calls need no compositor reload.

Offload warms asynchronously with Intel still drawing. Only a complete, current
frame can replace opaque colour; native windows, glass, markers and prompts stay
local with reconstructed world depth. Turn-off/crash/lease expiry returns to
prepared Intel. Native nested on/off/move/carry/worker-crash tests passed the
first capped hardware run (41.13 W peak); smoothing transport polling and longer
stability samples are being checked before declaring M4 complete.

For the 0.9 shell recheck: core correction is committed as `8ce4f75`, current
`build/hyprune-core.so` includes it. The offload hardware regression is queued
on the shared flock; it will only use its own nested compositor, not the shell
agent's session. Performance 0.10's canonical get/set results share one schema,
including all explicit/effective controls and nullable costs; validation is green.

Performance contract is now committed: schema `011743a`, SDK `ab36efc` (55 pass,
2 intentional skips). Core hot-switch tests at 1080p and 4K observed 100% offload
through each steady interval, including movement/carry, with fallback after a
worker SIGKILL and explicit retry. 4K run peak: 32.12 W, USB-C profile 50 W /45 FPS.
The live launcher now pins **0.10** and preflight requires `hyprune-renderer`;
shell's 0.10 Performance integration must be present before the owner's retry.
Core's hardware runs are still bounded by the independent 50 W/75 °C watchdog.

Final native-resolution (scale 1.0) 4K result: 44.96–45.03 worker FPS,
96.7–100% active samples during repeated switches/carry, 32.78 W peak. Tests keep
50 ms freshness and fall back instead of presenting expired frames. Simultaneous
software recording of full 4K adds substantial load; its transient local fallback
is documented. The 1080p recording passed all intervals at 100%, peak 29.27 W.

Actual shell `0a0b770`/`cab82a9` menu regression passed under the power watchdog:
Performance's Offload dropdown, using real nested keyboard selection, switched
on/off with the same active world and no reload. Screenshot/evidence:
`docs/evidence/offload/menu-performance.png`, `menu.json`. `performance.targetFPS`
is the worker's admitted FPS (45 on USB-C), distinct from requested target and
native frame FPS; please show it alongside the power profile/cap. Costs can be
null for the first few warmup frames, then populate as timer results arrive.


### Shell integration ready: HUD concepts, Performance and package skins

Shell **508e95f** (and protocol **0a0b770**) negotiates exact 0.10, inherits 0.8
hold timing and 0.9 streams/cycle, and binds the passive HUD before the Director.
Physical 5 holds cycle once without tool mutation; Settings selection and preview
IPC share persisted concept/banner logic. Live Performance get/set and actual
Offload dropdowns tested Intel → NVIDIA → Intel; Auto on USB-C reports
“auto requires qualified full AC”. Shell watchdog run peaked at 26.55 W.
All three concepts × four homes × 1080p/1440p/4K × scales 1/2 passed. Gallery:
`screenshots/hud-concepts/` (including Performance and skin comparisons),
`videos/hud-concepts.mp4` (52.9 s, key 5 and real wheel subtypes).

Owner can hold TAB, then Settings → Appearance for concepts, curvature and
Use world HUD styles. Performance is its own Director tab. Package style format
was reconciled with the personal-world proposal; schema `extensions/hud-style*.json`
and proposed docs RFC-0004/RFC-0008 amendment are committed. Launcher-authorised
package paths work (including the current supervised launcher argument); dynamic
installed-package discovery is still requested for subsequent world changes.
No core source edits from shell, no pushes, no owner-session testing.

### Shader warmup and recording/replay: IPC 0.12 (after climbing 0.11)

Core now warms eight programs and driver draw-state variants behind loading;
`graphics.get().timings.shaderCache` adds hits/misses/rejections, compile time,
warmup CPU total/maximum and warming state. No Settings switch is needed.
The binary cache lives in XDG_CACHE_HOME/hyprune/programs, keyed by driver/build.
Intel PTL worst first-use submission: 26.40 ms before warmup, 0.90 ms after,
0.57 ms with eight cached programs. See docs/SHADER-WARMUP.md.

Climbing keeps **0.11**; recording/replay adds **0.12**, canonically extending
0.11. Schema commit `153d4d8`; SDK types include both versions and world 0.4.
`record.toggle` is unbound by default in all four input modes; it appears in
input.get for 0.12 Settings clients and uses the existing capture/clear boxes.
Older keymaps omit the action and old edits preserve its binding. Shell should
negotiate 0.12 to expose it; 0.10 Performance and 0.9 HUD remain supported.

The input.control grant gates recording.start({includeAppKeys?:false}), stop,
mark({name}), status and replay.start({path,mode?:"input"|"camera",
smoothingMs?:0..1000,assertions?:{recorded?,tolerance?,marks?}}), stop/status.
Results share {recording,playing,path,frame,mode,failures,error}; path/error may
be null. Runtime notices announce recording paths and replay completion/failure.
Default recordings redact unhandled/app keystrokes. Opt-in captures raw key
symbols (potential typed text); do not enable this silently. Camera replay
never sends application input; input replay never invokes Hyprland keybinds or
synthesizes application keyboard events. This is a developer/test API, not an
observer input-injection capability. See docs/REPLAY.md for file format/limits.
