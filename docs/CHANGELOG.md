# IPC and world format changelog

What SDK and shell authors need to know about each IPC protocol version and each
world format version that core accepts. The canonical contract is the RFC set at
<https://hyprune.com/rfcs/> and the versioned JSON Schemas at
<https://github.com/hyprune/schema>; this file is a summary, not the contract.

Every version is negotiated exactly in `hello`. Core serves every version listed in
`src/ipc/Protocol.hpp` (`Supported`); a newer version inherits all earlier methods,
events and state through numeric feature floors (`atLeast`), and older sessions keep
their frozen state projections and enums. Unknown or unadvertised methods return
-32601. Mutations return `{revision}`. Validate frames with
`schema.validate('ipc', frame, responseMethod, '<version>')`.

## IPC versions

- **0.1** Framed, authenticated JSON-RPC: `hello`/`ping`, state subscribe/resync,
  `world.load` (installed world ID and optional spawn ID, never a path),
  `movement.set`, `surface.focus`, `runtime.exit`, `shell.claim`/`release`/`overlay`.
  Launch tokens are one-use, connection bound and delivered through an inherited FD.
- **0.2** Adds `camera.get`/`set`, `waypoint.set`/`clear`, `route.set`/`clear`,
  `travel.start`/`cancel`, `autodrive.start`/`stop`, lease-scoped `ui.open`/`close`
  and `data.publish`/`remove` (new `data.publish` grant, namespaced sources, bounded
  scalar maps with TTL). New state keys `location` and `integrations`; events
  `location.transition`, `ui.opened`/`ui.closed`, `data.updated`. There is no
  `world.metadata` event.
- **0.3** Adds `shell.bind` (verified Wayland-child association, enabling exclusive
  Director input) and `workspace.activate`; new revisioned state keys `spatial` and
  `workspaces`. Socket permissions are 0600.
- **0.4** Workspace homes and pulled tools: `workspace.bind`, `workspace.focus`/
  `unfocus`, `surface.pull`/`place`/`return`, `surfaces.recall`, `placements.list`/
  `remove`, `data.object.bind`, `atmosphere.get`/`set`, `camera.fov`. New state keys
  `mounts` and `presentation`; hints `mounts.changed` and `presentation.changed`.
  Placement recovery uses `runtime.notice` codes `placement.relocated`/`recalled`.
- **0.5** State gains `interaction {mode,tool,tools,noclip,cursor,taggedSurfaceId}`
  and `apps` (bounded to 16 entries). Placements may carry a free `pose` and `size`
  with an empty `anchorId`; older sessions do not see these rules.
- **0.6** Keymap v2: `input.get`/`preview`/`set`/`capture`/`capture.cancel` (new
  `input.control` grant; capture requires the verified Director lease) and state keys
  `keymap` and `editor`. `overlay.style` (`shell.control`) themes core-drawn
  world-locked markers, prompts and labels. FOV may drop to 3° for scope. `apps`
  includes native toplevels on all outputs, bounded to 128.
- **0.7** `graphics.get` (`state.read`) and `graphics.set {patch}` (`world.control`):
  presets, per-effect overrides, render scale, target FPS and GPU timings, persisted
  in `graphics.json`. See [GRAPHICS.md](GRAPHICS.md).
- **0.8** `keymap.holds` lists pending holds `{action,binding,startedAtMs,durationMs}`
  so a shell can animate hold progress locally. Also inherits exclusive
  `shell.overlay` and free placement poses through the shared version helper.
- **0.9** HUD streams: `hud.subscribe`/`unsubscribe` for opt-in `hud.frame` (≤4 Hz),
  `hud.pose` (≤30 Hz), `zone.entered`, `zone.exited`, `tool.changed`. The input action
  `hud.cycle` (originally bound to `5`; unbound by default since 0.15) emits `ui.hud.cycle` only to the bound
  shell lease. Legacy `interaction.tools` keeps its five-entry descriptor array.
- **0.10** Performance: `graphics.get`/`set` add `performance` (active renderer,
  status/reason, power profile and cap, per-control requested/effective values and
  GPU cost with shared-pass labels) and the keys `offload` (`off|auto|on`, default
  off), `offload_power_cap_w`, `scale_min`, `scale_max`. See [M4.md](M4.md).
- **0.11** Climbing: `movement` state gains `state`, `ladderId` and `prompt` for
  climb activity and prompts. Requires world format 0.4 metadata. See
  [CLIMBING.md](CLIMBING.md).
- **0.12** Recording and replay (`recording.*`, `replay.*`, `input.control` grant)
  and the unbound-by-default `record.toggle` action; see [REPLAY.md](REPLAY.md).
  `interaction.mode`/`editor.mode` may be `cursor` (free cursor, world `exit` defaults
  `Super+Tab`/`Back`); older sessions see `typing`/`interactive`. See
  [FREE-CURSOR.md](FREE-CURSOR.md). `graphics.get().timings.shaderCache` is added.
- **0.13** `graphics.outline` (nullable permit for the world's authored ink) and
  `graphics.visibility_culling`; `timings.visibility`. `camera.photo`/`camera.release`
  for photo lenses and dev-only `camera.author`; `camera.get` reports lens state.
  See [AUTHORING.md](AUTHORING.md).
- **0.14** Ink controls in `graphics.set`: `outline_quality` (`fast|smooth|null`),
  `outline_width_px` (1–4 or null) and `outline_colour` (RGB or null), persisted in
  `graphics.json`. `outline` and `outline_quality` share one GPU timer.
- **0.15** Placement and tool slots: tool 5 (Placement), `editor.unplaced`,
  `carryFacing`, `contentScale`, `toolSlots`, `toolDefinitions` and `toolInput`
  (modifier/gesture hints). World actions `primary`/`secondary`/`alternate`/`cycle`
  (old `wheel` migrates to `cycle`); `carry.facing`, `content.scale`, `window.reset`.
  `interaction.tools` admits `position`/`placement`; `hud.pose.tool` and
  `tool.changed.slot` admit 5. Placements gain optional `content`; mount instances
  may omit `areaId` when unzoned. `runtime.notice` code `placement.pocketed`.
- **0.16** `graphics.upscaler` (`fsr1|bilinear|null`); `timings.upscaler
  {requested,active,error}` reports the live reconstruction.
- **0.17** `capture.taken {captureId,timestampMs,mode}` to shells holding
  `shell.control`, and `capture.annotate {captureId,note}` to attach a note to a
  capture taken during this plugin load.
- **0.18** `graphics.offload_device` (`auto` or a PCI address); `offload_power_cap_w`
  accepts 20–1000 or `null` (automatic, scaled to the device on mains power).
  `performance` gains `capSource` and `device`. Older sessions keep the 20–100 cap.
- **0.19** `player.get`/`player.set {patch}` (`movement.control`) for walk/run
  speed, view bob, reticle style and reach limits, persisted in `player.json`.
  `graphics.fov` is 30–120 (default 65). `overlayStyle` gains `reticleColor`,
  `validColor`, `invalidColor`; state gains `interaction.reticle {state,action}`.
  Out-of-reach actions are refused with `runtime.notice` `placement.out_of_range`.
  See [VIEW-BOB.md](VIEW-BOB.md).
- **0.20** `surface.pocket {surfaceId}` puts a world window into the pocket (the
  Placement queue; idempotent, placement kept). `surface.unpocket {surfaceId}`
  takes it out: back to its saved placement or a free spot in front of the player
  (`placed: true`), else it stays pocketed, selected, with the Placement tool on
  (`placed: false`). Pocket membership stays `editor.unplaced.windows`.
  `spatial.map.levels` carries the world's blueprint levels
  (`com.hyprune.blueprint` 0.1) as `{id,name,minY,maxY,svg,sha256}`.
- **0.21** Markers, personal quests and focus sessions (RFC-0012 P1). New grant
  `quest.control` (requestable from 0.21). New required state keys:
  `markers {reveal,persistent,calm,coreDraw,list}` (at most 64 resolved markers: tier,
  kind, icon, target, position, range, occlusion, source), `quests {enabled,tracked,
  active[<=16],counts,world:null}` and `focus {state,pausedState,sessionId,
  phaseStartedAtMs,phaseEndsAtMs,phaseMs,remainingMs,creditedMs,crediting,cycle,cycles,
  breakKind,countMode,target,calm}` (monotonic milliseconds, like `keymap.holds`;
  `creditedMs` is sampled at most every 10 s and on phase changes). Methods:
  `markers.reveal {durationMs?}` (`shell.control`); `guidance.get`/`guidance.set
  {patch}` (the `player.get` pattern, flat dotted keys); `quests.list`/`get`
  (`state.read`); `quests.track`/`accept`/`abandon`/`resume`/`create`/`update`/
  `delete`/`mark` (`quest.control`; personal quests only, -32007 for others);
  `focus.start {target?,questRef?,objectiveId?,overrides?}`/`pause`/`resume`/`skip`/
  `stop` (`quest.control`) and `focus.history {days?}` (`state.read`). Events
  `quest.progress` and `focus.phase` (`focusSessionId`; the IPC `sessionId` is the
  usual one). World mode gains the action `markers.reveal` (default `Tab`): a TAB
  tap/hold pair with `menu` on `Tab[L]`; files without it are migrated. `overlay.style`
  gains `questColor`; `spatial.projection` markers gain `markerId` when
  `markers.core_draw` is off. Quests and focus run in hypruned and persist privately
  under `$XDG_STATE_HOME/hyprune/{quests,focus}`; guidance is
  `$XDG_CONFIG_HOME/hyprune/guidance.json`. Older sessions see none of this.
- **0.23** Characters, paths and interactions (RFC-0011 P2, RFC-0010; world format
  0.9). No new grant: changes need `objects.control`, `paths.list` needs `state.read`,
  `interaction.choose` needs `world.control`. New required state key `npcs {motion,
  state (none|animating|paused|still|off), reason, usage {authored,user}, limits
  {authored:16,user:8}, drawing, aimed, interaction}`: `aimed` is the character with
  interactions under the reticle within the use distance (`{objectId,name,prompt,
  interactions[{id,label,type}]}`), `interaction` the running one (`type`
  choose|say|waypoint|guide|panel, `choices[{id,label}]`, `line`, `lineIndex`,
  `lineCount`, `panel`), `drawing` a `paths.draw` in progress. `objects.instances[]`
  gain `character` and `accent`; `path` (`package/<id>` or `user/<id>`), `activity`
  (idle|walking|running|stopped|sitting|guiding|interacting|suspended) and `seat` are
  set for characters; `state` may be `suspended` (route not walkable). Moving
  characters' positions publish at most twice a second. `editor.paths[]` gain `use`,
  `name`, `speed`, `stops`. `interaction.reticle.action` gains `interact`.
  `graphics.get/set` gain `npc_motion` (auto|always|still|off; persisted in
  `graphics.json`, wins over Lua `npc.motion`; `auto` animates only on a full or
  limited AC power profile). Methods: `npcs.spawn {ref, path?|pose?, phase?, speed?,
  accent?, reverse?}` → `{revision,id}`, `npcs.bind {id, ...}` (user characters:
  route or standing spot and settings), `npcs.disable {id,disabled}` (authored and
  user), `npcs.remove {id}|{all}` (user), `paths.list` (package and user paths with
  `walkable`, `problems`, `bound`), `paths.set {path}` → `{revision,id}` (a route must
  be walkable), `paths.remove {id,unbind?}` (removing the route being drawn cancels the
  draw), `paths.draw {use?,name?,spawn?|bind?}` (tool 3 with a fresh path; finishing it
  spawns or rebinds), `paths.finish` → `{revision,id,bound}`, `interaction.choose
  {objectId, interactionId|null}`. Errors: -32004 unknown id/ref/path, -32005
  protected/authored/not a character, -32007 walkability or placement rule (`rule`
  `walkable` or `support`), -32008 budget (8 user characters per world, 8 per path,
  the 0.22 object budgets). `objects.transform`, `objects.pocket`, `objects.place` and
  `objects.configure` refuse characters (-32005). Notices: `npc.spawned`, `npc.removed`,
  `npc.disabled`, `npc.enabled`, `npc.suspended`, `path.saved`, `path.removed`,
  `path.unwalkable`, `world.library.missing`, `object.protected`. Older sessions keep
  the 0.22 shapes (no `npcs`, character fields stripped, `path`/`activity`/`seat`
  null, `interact` reads `click`).

### Changes without a protocol bump

These are wire-compatible behavior changes that integrators may notice:

- **0.22** Object catalog and user placements (RFC-0011 P1). New grant
  `objects.control` (requestable from 0.22). New required state key `objects
  {catalogRevision,instances[<=512],pocket[<=16],usage,limits}`: this world's authored
  instances and user placements (`id`, `source` authored/user, `ref`, `name`, `areaId`,
  `pose`, `scale`, `state` active/hidden/missing/blocked/loading, `movable`, `locked`,
  the blocking `rule`; `path`, `activity` and `seat` stay null until P2), the object
  pocket items, and user-object totals against their limits. Methods (`state.read`):
  `objects.catalog` (`{revision: catalogRevision, entries[<=1024]}`: ref, source, id,
  version, formatVersion, name, category, tags, traits, surfaces, scale range, bounds,
  triangles, gpuBytes, collisionTriangles, thumbnail path, licence, library, other
  versions, state, placeable, reason, errors) and `libraries.list`. Methods
  (`objects.control`): `objects.pocket {ref}` (`{revision,itemId}`; tool 5 selected),
  `objects.place {ref,pose?,scale?}` (`{revision,id}`), `objects.transform
  {id,pose,scale?}`, `objects.remove {id}|{all}`, `objects.hide {id,hidden}`,
  `objects.reset {id}|{all}`, `objects.configure {id,patch:{locked}}`,
  `libraries.install {archive,sha256}` (a local `.tar.zst`; hashed before extraction;
  completes when installed) and `libraries.remove {id,version}`. Errors: -32004
  unknown id or ref, -32005 locked/protected/not movable, -32007 a placement rule
  (`error.data {retryable:false, rule}`), -32008 a budget (rule `budget`).
  `editor.selectedKind` gains `node` (scene nodes; world objects are `object` with
  `object:<id>`), `editor.submode` gains `universal`, `editor.unplaced` gains `objects`
  (`count` covers windows and objects) and `editor.gizmo {space,pivot,snap}` is new.
  Older sessions see none of these: nodes read `object`, universal reads `move`, the
  pocket lists windows only. `runtime.notice` codes: `object.placed`,
  `object.removed`, `object.budget`, `object.blocked`, `object.missing`,
  `object.substituted`, `object.locked`, `object.protected`, `library.installed`,
  `library.conflict`, `library.invalid`, `catalog.invalid` (plus the gizmo's
  `placement.blocked`, `editor.undo`/`redo`, `gizmo.drop`/`align`/`pivot`).
  `editor.json` becomes version 2 (`placements`, `overrides`); version 1 files migrate
  (a copy is kept as `editor.json.v1`). The movable trait now decides object targets;
  `HYPRUNE_GIZMO_OBJECTS=1` only stands in for movable world 0.8 objects.
- Client strings projected from outside core (titles, classes, icons, rules,
  workspace and output names, errors, notices) are clamped to the schema
  `maxLength` in code points and end with `…` when clamped.
- Areas without an explicit `workspace.bind` get default homes
  (`windows.workspace_base + workspaceSlot`, base 20); they appear in
  `mounts.bindings` and `workspaces.locations` like explicit homes. The world room is
  native workspace 20 (`windows.world_workspace`).
- Render on demand: while idle, `graphics.get` timings report `idleFrames` and
  delivered FPS falls toward zero.
- Shell credential renewal for supervised launches uses `HYPRUNE_TOKEN_REFRESH=1`
  and a duplex `HYPRUNE_TOKEN_FD` (`renew\n` request, fresh one-use token reply).
  No public IPC method is involved.
- Windows brought into the world (opened in the room, moved or dragged in from another
  output, pulled, recalled, placed, pocketed, carried off a home wall) become floating
  clients at their own size (over half the output: scaled to 35% of it), centred on
  the world output; returned to a home wall they are tiled again. Clients with a
  `wp_viewporter` source crop are captured. Recall brings room clients to the player.
- The out-of-range reticle uses `overlayStyle.invalidColor` with a "Too far: N m"
  label. A refused reach-limited action always sends a notice: `placement.out_of_range`
  (place, pocket, mount, path) or `interaction.out_of_range` (Super grab beyond
  `reach_grab`, use beyond `useDistance`; `interaction.reticle` shows `out_of_range`
  with action `""` for a refused use). Use mode (E) accepts the Super window gestures;
  Super+LMB on a window edge resizes. `interaction.mode` `workspace`, `fullscreen` and
  `typing` hide the shell HUD.
- Position tool (tool 4, [GIZMO.md](GIZMO.md)): constant-size 3D handles with plane,
  screen, view-ring, trackball and uniform-scale handles. Object edits are previewed
  and committed on release, so `editor.objects` changes once per gesture (not every
  tick) and collision follows commits only. A pose breaking a placement rule is
  refused with `runtime.notice` `placement.blocked`; in tool 4, `interaction.reticle`
  shows `target`/`grab` over a handle, `holding`/`grab` while dragging and
  `invalid`/`place` for a refused pose. Undo/redo send `editor.undo`/`editor.redo`
  notices, drop/align `gizmo.drop`/`gizmo.align` on failure, the pivot toggle
  `gizmo.pivot`. Development switches (`HYPRUNE_GIZMO_OBJECTS`, `HYPRUNE_GIZMO_UNIVERSAL`)
  add world 0.8 objects as targets (`selectedKind` `object`, `objectId`
  `object:<instance id>`) and a universal mode that `editor.submode` reports as `move`.

## Path tool editing (no wire change)

RFC-0006 tool table, Path row:

- Paths draw only in the path tool (tool 3), with their points and curve handles; the
  point under the reticle highlights. Period in tool 3 keeps all paths shown in the other
  tools; the debug view shows them too. Paths are still saved in `editor.json` and listed
  in `state.editor.paths` unchanged.
- Primary pressed on a point and dragged moves it along the aimed surface (Shift snaps to
  0.1 m; a point on a floor stays on it); release commits. Primary on a segment inserts a
  point there; elsewhere it adds a point to the active path as before.
- Secondary removes the aimed point, else the active path's last point.
- Path edits (add, insert, move, remove) are undo steps: Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y in
  tools 3 and 4. Escape cancels a point drag. The Primary hint reads "Add / drag point".

## Window placement behaviour (no wire change)

RFC-0005 "Amendment: reachable spawns, home windows and world switches":

- Only the room workspace (`windows.world_workspace`, default 20) floats. Windows on a
  home workspace stay tiled windows of that workspace and appear on its wall, however
  they got there (opened there, opened while focused into the wall, or moved there).
  The earlier rule that later home arrivals take tool mounts is withdrawn.
- A window taken into the world (pull, carry off a wall, place, mount, pocket) moves
  natively to the room workspace and floats there. `surface.return` and dropping it on
  a wall move it back to that wall's workspace and tile it.
- Durable placement rules restore windows on the room workspace only; a home
  workspace's own windows are never pulled out by a rule.
- New room windows spawn ahead of the player on reachable floor (unchanged).
- `mounts.instances` lists a taken window with `workspaceId` 20 and its home area.

## World format versions

- **0.1** Initial manifest, glTF scene, explicit collision, spawns, PNG lightmaps.
- **0.2** Same manifest shape; makes the glTF material profile explicit (UV0
  tiles/trims, UV1 baked irradiance, standard samplers, `KHR_texture_transform`).
- **0.3** Workspace homes (areas with `workspaceSlot`), typed mounts, data objects,
  reserved placement volumes and per-world atmosphere.
- **0.4** Climbable segments (oriented, with width/depth, face normal, exits and
  optional speed).
- **0.5** Optional ink (outline) style and independent visibility regions.
- **0.6** `playable` boxes/convex hulls and `maxFlyHeight` on the world and areas;
  `backdrop.farCards` for sky-card sampler checks. Noclip emits
  `runtime.notice` `world.bounds.left`.
- **0.7** Packages may require `KHR_texture_basisu` (KTX2), `EXT_meshopt_compression`
  and `KHR_mesh_quantization`. Lightmaps stay PNG.
- **0.8** Placed world objects: `objectAssets` (≤ 32 package glTFs, the format's
  extension rules, never lightmapped) with optional node animation (≤ 16; `spin`,
  `orbit`, `bob`; nodes by unique name), a static collision proxy and `probe`/`unlit`
  lighting; `objects` (≤ 256 instances: pose, uniform scale, speed, phase, optional
  ambient). `library` references are reserved and rejected. Objects are render-only
  (never picked, walked on or moved by gestures; proxies collide statically and count
  toward the collision budget). Core uploads each asset once, draws instances
  instanced, culls by swept bounds, lights `probe` assets with the runtime sun plus a
  per-instance ambient probe from the world's baked light, renders on demand only
  while an animated object is visible in the active world view, and holds the t = 0
  pose under `graphics.reduced_motion`. Until 2026-10-12, worlds with objects used
  the in-process renderer (offload reported "world objects use local renderer").
  No IPC change.
  Correction (2026-10-11): an object shares the visibility of the static geometry
  around it (the areas owning scene meshes its swept sphere overlaps; none: frustum
  only), not the first area containing its centre, which in derived regions is a
  camera cell visible from nowhere. `hyprune-world-check` reports `regions`.
  Offload (2026-10-12): the optional offload renderer (RFC-0009) draws world objects
  too: authored instances, catalog placements, hidden ones and gizmo edits, at the
  same animation clock with the same instanced path and lighting, and edited
  scene-node transforms. `performance.reason` no longer reports "world objects use
  local renderer" or "edited world transforms use local renderer"; the
  environment-gated machines and NPC prototypes keep the local renderer ("machines
  prototype uses local renderer", "NPC prototype uses local renderer"). No IPC
  change: `reason` is free text.
- **0.9** Characters, paths, seats and keepClear (RFC-0011 P2, RFC-0004 amendment).
  `objectAssets[]` take one source: an inline `scene` (the 0.8 `animations`/`collision`
  shorthand or `traits`, not both), a package object directory `dir` (with
  `object.json`) or an installed library object (`library` `id@version` + `object`);
  a missing library fails the load with `runtime.notice` `world.library.missing` and
  the previous scene stays. Assets with the `character` trait load as skinned
  characters (RFC-0010 spec). `objects[]` gain `path` (a `route`; no `pose`; starts at
  `phase` along it), `reverse`, `seat` (unbound characters sit there), `accent`,
  `interactions` (`say`, `waypoint`, `guide`, `panel`; `travel` and `quest` reserved)
  and `locked`; at most 16 character instances, 8 per path, characters unscaled. New
  `paths[]` (≤ 64, 1,024 points; `use` route|track|generic; tool 3's segment geometry;
  `stops` with seconds and a clip, a facing or a seat), `seats[]` (≤ 64) and
  `keepClear[]` (≤ 64). Routes are checked for walkability every 0.25 m (floor within
  0.35 m of the previous sample, body capsule clear, playable union, no screen
  approach); characters on a route that fails stand still (`suspended`).
  `hyprune-world-check` reports characters and routes and fails on an unwalkable
  authored route. Characters render through the offload renderer too (the worker
  poses them from the same pose inputs); the NPC prototype (`HYPRUNE_NPC_PROTOTYPE`)
  is withdrawn.

Older packages remain accepted by core at every format version above.

Optional world extensions, at any format version: `com.hyprune.map` 0.1 (overview
map SVG and its affine frame) and, since IPC 0.20, `com.hyprune.blueprint` 0.1
(1–6 per-level blueprint SVGs in that frame, at most 64 KiB each and 256 KiB in
total, with drawing roles named by a `class` vocabulary). Older cores ignore the
blueprint extension.
