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
