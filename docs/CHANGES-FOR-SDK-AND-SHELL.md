# Core M1 contract changes

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
