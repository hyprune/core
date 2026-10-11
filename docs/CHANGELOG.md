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

### Changes without a protocol bump

These are wire-compatible behavior changes that integrators may notice:

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
  pose under `graphics.reduced_motion`. Worlds with objects use the in-process
  renderer (offload reports "world objects use local renderer"). No IPC change.

Older packages remain accepted by core at every format version above.

Optional world extensions, at any format version: `com.hyprune.map` 0.1 (overview
map SVG and its affine frame) and, since IPC 0.20, `com.hyprune.blueprint` 0.1
(1–6 per-level blueprint SVGs in that frame, at most 64 KiB each and 256 KiB in
total, with drawing roles named by a `class` vocabulary). Older cores ignore the
blueprint extension.
