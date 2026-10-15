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
- **0.24** Live machine state (RFC-0013 amendment). No new grant. `objects.catalog`
  entries gain `machine`: `null`, or the object's declared machine inputs as authored
  `{primary, inputs: [{id, type, label, explain, default?, values?, min?, max?, maxLength?,
  repeat?, states?}]}`. New required state key `live` (`state.read`, published with the
  data, never per frame): per bound machine instance (at most 64) `{id, asset, ref, source,
  status, error, stale, inputs}`, each input `{source, key, value, raw, condition,
  publishedAt}` (`condition` fresh, stale, missing or error; `publishedAt` Unix ms of the
  publication core holds, or null). Older sessions see neither. Source metadata `_reset`
  (draft): a change restarts the running averages fed by that source. The source helper's
  control channel (`sources.sock` beside core's socket: status, test, restart, restart-all,
  reload; same user only) is local and not part of IPC.
- **0.25** (draft) Companions (RFC-0010 companions amendment). Methods
  (`objects.control`): `companion.set {ref}` (a companion-capable library or user catalog
  character; persisted in `$XDG_CONFIG_HOME/hyprune/companion.json`, or
  `HYPRUNE_COMPANION_FILE`), `companion.clear {}` and `companion.mode {mode: follow|stay}`,
  each returning `{revision}`. The reserved object id `companion` works with `npcs.bind`
  (a route or a standing spot), `paths.draw {bind: "companion"}` and `interaction.choose`
  (`talk`, `stay`, `follow`); `npcs.disable`/`remove` and `objects.*` refuse it (-32005).
  New required state key `companion {ref, name, state (none|loading|unavailable|active|
  waiting|hidden), reason, mode (follow|stay|route), path, activity, position, carried}`;
  `position` is published at most twice a second while it moves. `objects.catalog` entries
  gain `companion` (null for non-characters, else `{capable, name, reason}`); aimed
  interactions may have type `companion`; `paths.list` `bound` may list `companion`.
  Per-world route or spot bindings live in the editor overlay (`editor.json` `companions`).
  Notices: `companion.set`, `companion.cleared`, `companion.unavailable`,
  `companion.bound`, `companion.following`, `companion.staying`. Older sessions see no
  companion key, field or method, and never see the companion as the aimed character.
  Object format 0.10 (draft) gains the optional `companion` block (`name`,
  `followDistance` 1.5-2.5 m, `idles`, `say`).
- **0.26** Debug tool, companion pace, longer pocket list (RFC-0003 amendment). No new
  method, event or grant. Tool 9, the debug tool, is named: `tool` is 1-5 or 9 in
  `interaction`, `editor`, `hud.pose` and `tool.changed` (name `debug`); `editor.submode`
  is the shown layer (`windows`, `window-zones`, `collision`, `playable`, `paths`,
  `keep-clear`, `seats`, `visibility`, `steering`, `placement`); `editor.toolDefinitions`
  lists slot 9 after slots 1-5; the world action `tool9` is in the keymap, `input.get`,
  `input.preview` and `input.set`. Required `editor.debugTool`: null while another tool is
  out, else `{layer, label, index (0-based), count, legend [{label, colour (sRGB 0-1)}],
  card ([string] | null)}`; core still draws the banner, legend and card itself. Required
  `companion.pace` (`walk`|`run`, persisted in `companion.json`; it now also survives
  `companion.clear`); `companion.mode` takes an optional `pace`. `editor.unplaced.windows`
  and `mounts.instances` list up to 128 entries (the selected pocket item and the carried
  window first when cut), `editor.unplaced.count` at most 144. Older sessions keep the
  previous projection: tool 1 for tool 9, five tool definitions, no `tool9`, `debugTool`
  or `pace`, 16-item lists (the selected and carried windows kept) and a count of at most
  32, also in `mounts.changed`.
- **0.27** Workspace screens (RFC-0011 amendment; object format 0.11). No new
  method, event or grant. A user placement of an object whose `mount` trait has a
  `mounted-workspace` anchor is a workspace screen: while it is placed (active or still
  loading) and bound to a workspace, it is that workspace's home in the loaded world, with
  an area `screen-<placement id>` (its `workspaceSlot` is the workspace number), the
  anchor at the instance pose and scale (`mount.role` mounted-workspace, `interaction`
  focus-in) and an arrival spawn in front of it, all in `mounts` like an authored home; the
  binding is a derived default (never persisted in the layout), after the world's own
  homes. The wall, tiled home windows, F and workspace travel work as for authored walls;
  the screen shows the workspace at the monitor's aspect, centred in its 16:9 or 21:9
  frame. Required `objects.catalog` entry `screen` (null, or `{aspect 16:9|21:9, size,
  mountType}`) and object instance `screen` (null, or `{workspace|null, home|null, aspect,
  size}`, `home` the area id while it is a home). `objects.place` and the
  `objects.configure` patch take `workspace`: 1-9999, `"next-free"` (the lowest workspace
  above `workspace_base` that is no home of this world, no explicit home, not the room and
  no configured location) or null (no home); `objects.place` and tool 5 bind a screen to
  the next free workspace unless told otherwise, and a screen pocketed with tool 5 keeps its
  workspace for the next place. A taken workspace is refused with -32005 and rule
  `workspace.taken` (the room or a location: `workspace.reserved`). The placement record in
  `editor.json` keeps `workspace`. Notices `screen.bound`, `screen.unbound`. A screen's
  own approach zone never refuses moving it. Older sessions see no `screen` fields and
  can't send `workspace`; the injected homes appear in their `mounts` too.
  Imports (RFC-0014 amendment, same version): catalog entries also carry required `warnings`
  (at most 8: validation notes, sizes over their recommendation), `provenance` (null for
  package assets, else `{license, author, source}` from `import.json` or the `"."`
  attribution, `"unknown"` where unrecorded) and `imported` (null, or `{at, sourceName}` from
  `import.json`). The per-world user-placement totals become 2,000,000 triangles, a 512 MiB
  GPU pool and 100,000 proxy triangles (were 400,000, 192 MiB, 10,000) for every protocol.
  World folders (RFC-0014 builder world amendment, same version; world format 0.11): required
  state key `builder` (null, or `{projectId, worldId, name, folder, mode: "edit", revision,
  placements, sky, error}` while a world folder is loaded). `world.load` of `local.<slug>`
  with no installed package of that id opens `$HYPRUNE_PROJECTS_DIR/<slug>/` (default
  `$XDG_DATA_HOME/hyprune/projects`) from `builder/base.json`; the placements of
  `builder/project.json` are user placements there, and every objects edit in the folder is
  written back to that file (never `editor.json`; `revision` counts the writes). Placing a
  user or library object in a folder copies it into the folder's `objects/<id>/` first:
  object refs gain `world/<id>` and catalog entries the source `world` (notice
  `builder.copied`). Older sessions see no `builder` key, no `world/` instances and no
  `world` catalog entries. Git, saves and releases are the SDK CLI's (`hyprune builder`);
  core never runs git.
- **0.28** Shared clip library (RFC-0010 amendment). No new method, event or grant.
  Characters may have a style per role: required `objectInstance.style` and the companion's
  `style` (null, or `{walk?, run?, idle?}`, each a style name), `style` on `npcs.spawn` and
  `npcs.bind` (null clears it) and on `companion.mode` (kept in `companion.json`). A styled
  character plays `walk_<style>`, `run_<style>` or `idle_<style>` when it has that clip, else
  the plain one. The companion dances: the aimed interaction type `dance` with required,
  nullable `options` (one per dance it can play), `interaction.choose` takes `option` (a dance,
  or none for any), and its `activity` may be `dancing`; required `companion.dance` names the
  dance playing. Required `objects.catalog` entry `motion`: null for non-characters, else the
  character's skeleton, its styles per role, its dances and the motion packs that give them.
  Older sessions keep the 0.27 shapes (no `style`, `dance`, `motion` or `option`; `dancing`
  reads `idle`).
- **0.29** (draft) Motion feel (RFC-0008 and RFC-0009 amendments). New method
  `recording.label`; no new event or grant.
  - Input version 3: world mode binds `sprint` to `Control_L`, `down` to `Shift_L` and
    `record.toggle` to `Ctrl+r`. A version 2 `input.json` is upgraded once, each binding only
    where it still has its old default and the new key is free. `input.get`, `input.preview`
    and the keymap state report version 2 to older sessions, whose `input.set` sends version 2.
    Snapping follows the sprint key (Ctrl); tools 3 and 4 hold back Shift like Ctrl for chords.
  - `graphics.frame_pacing` (`even`, `max_rate`, null = preset; every preset `even`): even
    pacing of a rate-capped offload worker on a refresh divisor at a constant latency.
    Independently of the setting, the worker's frame budget now counts refresh intervals, not
    presents, and the compositor no longer re-paces worker frames on `target_fps`.
  - Debug recording format 2 (Ctrl+R): live dt and timing per frame, no fixed steps or forced
    frames while recording; per-draw views; a header with monitor, GPU, profile and versions;
    stats every 5 s. `recordingStatus` gains required `elapsedMs`, `stats` and `last`; `frame`
    may reach 432001. New required state key `recording`. `recording.label {label, meta?}`
    renames the last finished recording with a slug of the label and writes a sidecar.
    Format 1 recordings still replay on fixed steps.
  - Player settings (`player.get`/`set`) gain `look_inertia` (0-0.1 s), `move_inertia`
    (0-0.3 s), `view_bob_roll` (0-2 degrees), `carry_turn` (0-0.4 s) and `carry_follow`
    (0-0.3 s); older sessions never see them. A carried window's facing turn and grab point
    follow as critically damped springs; the walking bob adds a head tilt.
  - No wire change, unconditional: ticks that follow a presentation move by the interval
    between presentation timestamps (whole refreshes; the measured interval under VRR),
    and worker frames now include the view bob.
- **0.30** (draft) Settings presets (RFC-0008 amendment). New methods `settings.get`
  (`state.read`) and `settings.set` (`input.control`); no new event, grant or state key.
  - One global preset over every graphics and player setting: `auto`, `low`, `medium`,
    `high`, `ultra` (each sets `graphics.quality`) and `usb-c` (quality medium, offload on,
    frame pacing even). Saved in `settings.json` (`$HYPRUNE_SETTINGS_FILE`).
  - Overrides are the explicit values already in `graphics.json` (non-null keys) and
    `player.json` (any key); they survive preset switches. A setting resolves as preset,
    then the Lua config for keys it sets, then the override.
  - `settings.get` returns the preset, the presets with every setting's default under each
    on this GPU, `settings` (`graphics.<key>`/`player.<key>` to `{value, default, source}`,
    source `user`, `lua` or `preset`), the override count and the default keymap.
    `settings.set {preset?, set?, reset?, resetAll?}` applies reset all (every graphics and
    player override and the keymap), then the preset, resets and new values (null refused),
    validates the whole request, saves, applies and returns the new snapshot.
  - Once, with no preset saved, core picks one from `graphics.json` (`quality`, `intel` as
    `low`, else `auto`) and drops graphics and player values equal to its defaults.
    `graphics.set` and `player.set` keep working at every version.

### Changes without a protocol bump

These are wire-compatible behavior changes that integrators may notice:

- **Offload power cap and latency.** Power over `offload_power_cap_w` first throttles the
  worker (25 % fewer frames per sample, down to 30 FPS) and ends offload only if the draw is
  still over the cap 2 s later; `runtime.notice` codes `offload.power.throttled` and
  `offload.power.exceeded` say so, and `performance.reason` carries the numbers. Finished worker
  frames are read as soon as they arrive, and the worker no longer revalidates its graphics on
  every frame: about 10 ms less offload latency where the hold had drifted to an extra
  refresh. Recordings add `compositorGpuMs`.
- **Offload renderer reconfiguration and pacing.** `graphics.set` restarts the offload worker
  only for `offload` and `offload_device` (and on/off, world or output size changes);
  `target_fps`, `offload_power_cap_w` and rendering settings apply to the running worker.
  Even pacing uses the larger of the cap's refresh divisor and the one the worker measurably
  sustains (p90 of its per-frame service time, with hysteresis), and the worker's dynamic
  resolution targets that cap's grid rate instead of `target_fps`. The worker never skips a
  draw (its renderer's own target_fps pacing ended it, "reused frame"). Recordings' offload
  timing gains `serviceP90Ms`, `renderGpuMs`, `transferGpuMs` and `renderScale`.
- **World format 0.11** (RFC-0014 builder world amendment) loads: it follows 0.9 (there
  is no world format 0.10) and adds `atmosphere.panorama`, an equirectangular PNG or JPEG
  (2:1, 1,024 to 8,192 px wide; its centre looks along -Z, u grows towards +X) resampled on
  the loader to an sRGB cube of `maxResolution` per face, drawn as the sky at every sky
  setting but off and, without `atmosphere.environment`, lighting the world. Directory
  assets may be object format 0.11. `atmosphere.get`, `state.presentation` and
  `presentation.changed` never carry the panorama; `atmosphere.set` keeps the loaded one.

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

## Unreleased (no wire change)

- **Seat adaptation.** A character now takes a seat up to 25 cm lower than its own seat height
  (was 3 cm): the root sits lower and both legs bend more, a two-bone solve per leg that keeps
  each ankle where the clip put it relative to the floor (a little forward) with the foot flat;
  the plugin and the offload worker pose the same skeleton (`seatDrop` in the pose record).
  The official companions (seat heights 0.46-0.60 m) and Ash now sit on Lumen Reach's 0.40 m
  benches. Seats more than 45 cm too high or 25 cm too low are still refused.

## Core 0.10.0

A minor release with the contract round of @hyprune/schema 0.14.0: **IPC 0.28** (see its entry
above) and **motion packs** (`motion/<id>/motion.json`, motionVersion 0.1) are released. Older
protocols and formats are unchanged; object and world formats are unchanged.

- **Motion packs:** a world package, an installed library or the user folder may ship packs
  of clips on a named skeleton (`mixamo-v1`, the Mixamo humanoid; characters with its bones
  are `mixamo-v1` without declaring it). At load, a pack fills the clips a character lacks:
  retargeted through both rest poses (bones by canonical name, hips and speed scaled by hip
  height); a character's own clips win. A pack's sit seats a character by its hip joints when
  clothing skinned to the thighs hangs lower, and its sit and stand keep the source's travel
  (seat anchors are placed in world distances). Library packs are taken by id, the newest
  version first. The plugin and the offload worker bind the same packs.
- **Grounded standing clips:** a walk, run, idle, talk, wave, turn, look, nod or guide clip
  whose lowest foot never comes within 2 cm of the idle's is lowered by the difference at load
  (some exporters keep the hips at rest height while the knees bend). Sits, stands and dances
  are left alone.
- `objects.catalog` motion is known before a character is first spawned (a clips-only load,
  cached by content digest).
- `npcs.bind` with settings only (speed, accent, phase, style) now takes effect at once; it
  used to wait for the next world load.
- `hyprune-npc-check --motion DIR` binds packs and lists the clips they fill, with the planted
  speed and foot slip of walks and runs, and the clips grounded at load.

## Core 0.9.1 (no wire change)

Data sources phase 2 ([RFC-0013 amendment](https://hyprune.com/rfcs/0013-live-objects/#amendment-data-sources-phase-2-helper-and-shell)),
in the bundled source helper (`libexec/hyprune/source-helper`); core's IPC is unchanged.

- **Secrets by name:** `{"secret": "name"}` in a header or `{secret:name}` in the URL, a header
  string or the body, resolved at fetch time from `HYPRUNE_SOURCES_SECRET_<NAME>`, the secrets
  file (`--secrets`, default `$XDG_CONFIG_HOME/hyprune/secrets.json`, read only at mode 0600)
  or the desktop keyring (`secret-tool`; `--no-keyring` skips it). Never in `sources.json`,
  publications, logs, errors or control answers.
- **HTTP sources:** `GET`, `POST`, `PUT`, `PATCH`; a JSON or text `body`; `json`, `number`,
  `text` or `lines` output.
- **Selectors and the gauge map:** fields take JSONPath-like `path`s (members, indexes,
  slices, `*`, `..name`, `[?(@.a op value)]` filters) with `count`/`sum`/`avg`/`min`/`max`/
  `first`/`last`; a source's `map` publishes a gauge's `value` (scaled to 0-100), `level`
  (`ok`/`warn`/`fail` by thresholds or matched text), `display` (a template) and `label`, for
  command and HTTP sources.
- **Control channel:** `test` also answers the raw response (4 KiB), the HTTP status and type
  and where each secret was found; a new `secrets` op lists the configured secret names and
  their origin (never values). The reference shell's Director → Objects → Data sources uses
  both.

## Core 0.9.0

A minor release with the contract round of @hyprune/schema 0.13.0: **IPC 0.27** (workspace
screens, imports in the catalog, world folders), **world format 0.11** (panorama skies, builder
worlds) and **object format 0.11** (the `mount` trait, size recommendations, `import.json`) are
released; see their entries above. Older protocols and formats are unchanged. Also:


- A user character placed with a route (IPC 0.23 placement fields `path`, `phase`, `speed`,
  `accent`, `reverse`, `disabled`) made the next plugin load fail with "invalid placement":
  `editor.json` records were restored with the six base fields and `lock` only. The restore now
  takes those fields with their ranges, and the `workspace` field workspace screens add (1-9999).
  Unknown fields are still refused.
- `hyprune-walk-check PACKAGE PROBES.json` (new build tool): each probe walks (fixed heading, a
  walk or run speed, a frame time) or autodrives (`route.set` + `autodrive.start`, the player's
  walker) from an eye position towards a target, then stands still until it has stood for
  0.2 s. One JSON line per probe: `startGrounded`, `end`, `grounded`, `lastGrounded` (where it
  last stood), `minY`, `playable`, `leftPlayable` and `crossed` (the capsule axis passed
  through a collision triangle between two frames). World authors sweep walls and floor edges
  with it for holes a capsule falls through (Lumen Reach's `tools/check-fall-through.py`).

## Core 0.8.4 fixes (no wire change)

- Paths drawn with tool 3 are `generic`, so a user character carried and set down near one did
  not snap to it, and `npcs.bind` refused it ("is not a route"). A user's `generic` path is now
  taken wherever a character needs a route (carry snap within 0.75 m, `npcs.bind`, `npcs.spawn`
  with a path, the companion's binding) and becomes a route (`use: route`, one undo step) when
  it is walkable for that character; otherwise the usual -32007 refusal (RFC-0011 amendment).
  A drawn path left without points is dropped when the drawing ends.
- The companion stood idle wherever it was, in follow mode, when every spot near the player lay
  in a keep-clear volume or zone (the foot of Lumen Reach's Signal Rise ramp), until the player
  was 20 m away. Its follow slot and teleport target now fall back to spots by the walking rules
  alone; with no spot at all it walks towards the player, or teleports out of view when it
  can't (RFC-0010 amendment). `camera()` diagnostics gain `companion.slotLoose`, `stranded` and
  `slotMiss`, and `npcs.carry`.
- With offload, blended world materials (alpha haze, such as vent steam) behind a character
  were drawn over it: the compositor's depth pass under the worker's frame lacked characters.
  They are in it now; the in-process renderer was not affected.

## Core 0.8.3 fixes (no wire change)

- After a session lock the shell's HUD came back without a lease: core refused every method
  while locked, `shell.release` too, so the lease outlived the lock and the shell's next
  `shell.claim` was refused ("output already leased"). `shell.release` is now accepted while
  locked, and a claim from the connection that already holds the output renews its lease
  (RFC-0003 amendment). Display power off/on keeps the lease and the HUD.
- `npcs.spawn` without a pose looked at four spots straight ahead and refused any failure as
  rule `support`. It now takes the free spot nearest 2 m ahead within 6 m and 80 degrees of
  the view, past window placement zones and keep-clear volumes (which cover the open floor in
  front of Lumen Reach's screens); a refusal's `data.rule` and message say which rule failed
  (`support`, `walls`, `playable`, `approach`, `keep-clear` or `zone`, a zone by its id), also
  for given poses in `npcs.spawn`, `npcs.bind` and `companion.bind` (RFC-0011 amendment).
- The window prompt "E USE / F FOCUS" is hidden while the window is in use (E).
- Collision that is never drawn is static: a persisted move of a collision-only node is no
  longer applied (it shifted walk collision).
- A free room mount recorded with a world's first area no longer becomes that area's home
  after a relaunch.

## Core 0.8.2 fixes (no wire change)

- Offload power policy: a GPU that enforces less than 20 W no longer makes the configured
  cap's clamp range invalid (undefined behaviour; an abort in assert builds). The cap's
  floor is min(20 W, the GPU's limit).
- Characters upload to the GPU over several ticks (a 4 MiB budget per tick, like object
  assets) and draw once fully uploaded; spawning several no longer hitches one frame,
  and nothing uploads inside the render pass.
- The per-frame interaction-hint ray and the travel/route capsule probes use the world's
  collision index instead of testing every triangle.
- Characters exported with KHR_materials_pbrSpecularGlossiness (no metallic-roughness
  block) take their diffuse factor and texture as the base colour.
- X11 menus without a transient-for attach to a window of their own client (the one they
  open over), not to whichever window has focus.

## Input recordings take every current tool (no wire change)

- Session recordings (`hyprune.session` version 1, core's own file format) record and
  load with tool 5 or 9 out and the universal position mode; frames gain an optional
  `debugLayer` (tool 9's shown layer), which a replay restores. Recordings from before
  load unchanged. `replay.start` mark assertions keep `tool` 1-4 as IPC 0.26 specifies
  (schema `replayAssertions`); asserting tool 5 or 9 needs the next protocol version.

## Recall spreads windows out (no wire change)

- `surfaces.recall` gives every recalled window its own free spot ahead of the player,
  like a pocketed window coming out: a fan in the camera's vertical plane, rows above
  and below, then further out; in view first, clear of collision, never over another
  window. Recalled windows stay where they were put (they no longer follow the camera)
  and keep the `recalled` status until moved. A window with no free spot falls back to
  the old pose in front of the player.

## Characters and objects named by their display name (no wire change)

- `objects.instances[].name` for authored instances, NPC prompts ("Talk to …"), say
  captions and notices use the object's display name: a character's `companion.name`,
  else its object.json `name`, else (inline assets) the asset id. Authored residents read
  "Resident (blue)" where they read `blue` before; user-placed characters use the
  companion name over the catalog name when they have one.

## Waypoints: route line on the ground, routes round obstructions, cleared on arrival (no wire change)

- Arrival (RFC-0012): a waypoint the player reaches is cleared by core: within its
  marker's `range[0]` (2 m) across the ground, with the target between 1 m below the feet
  and 1 m above the eye. `location.waypoint` becomes null; a route ending at the same
  point (within 1 m) is cleared with it.
- `route.set` with one off-graph target (a position or zone that is no walking-graph
  place) whose straight leg from the camera is blocked by collision now follows the
  walking graph to the place nearest the target, then the target (RFC-0004 amendment).
  The last point keeps the caller's target; a clear straight leg stays as before.
- The drawn route line lies on the ground (a sample every 0.5 m on the floor below it,
  legs over a crest split) instead of straight from the feet through hills and floors.
- Marker labels at output scales above 1x: the fallback `+` and the label are laid out
  from the scaled glyph advance (the `+` no longer covers the label's first glyph at
  4K); overlay atlas icons scale with the output up to 2x (24 px at 1080p).

## Offload draws assets added mid-session (no wire change)

- The offload worker now draws characters and objects whose assets were added after
  the world loaded: the companion, residents spawned and objects placed from a library
  installed mid-session. Before, a still view kept the worker's first frame without
  them (the companion was missing in offload). Each new asset reaches the running
  worker by stable ref; until it is decoded and uploaded only that item is missing,
  and a decode the worker can't do no longer ends offload. `graphics.get`
  `performance.workerTimings` (a free-form object) gains `assetsReady` and
  `assetsFailed` (index, ref, error).

## Debug tool, tool 9 (no wire change)

- Key `9` (world action `tool9`) selects the debug tool. It shows one debug layer at a time;
  Cycle steps through them: window boxes and mounts, window zones, collision (floor, wall,
  invisible wall or backstop, object proxy; within 36 m), playable volumes and the fly
  ceiling, paths and stops, keepClear and approach zones, seats, visibility regions, NPC
  steering, and placement rules. Core draws a banner naming the layer with a legend of its
  colours. Lines are 3 px at 1080p (scaled with the output), anti-aliased in the gizmo pass,
  with light fills; the F3 debug view keeps its 1 px lines. Nothing is drawn, and no frames
  are forced, while another tool is out.
- Primary (`debug.inspect`) opens an info card for the aimed item: id, type, owner, area and
  the placement rule it enforces (for example "Inside a window zone (rule zone)"). In the
  placement layer, Primary on open ground checks a 0.5 m box there. Secondary
  (`debug.clear`) closes the card.
- IPC 0.26 names it (see the 0.26 entry). Before 0.26, peers see it as the normal tool: `editor.tool` and
  `interaction.tool` 1, `editor.submode` `normal`, `hud.pose` tool 1 and `tool.changed`
  slot 1 `pointer`. `editor.toolSlots` carries the debug tool's slots (free action and label
  strings), so hints stay right. `tool9` is left out of the published keymap, `input.get`
  and `input.preview` (with any conflict or Hyprland collision naming it), and `input.set`
  keeps its bindings, less any the peer has just given another action. An input file
  without `tool9` gains `9`, unless 9 is already bound in world mode.

## Characters steer, detour and keep their feet in step (no wire change)

RFC-0010 amendment "steering, detours and gait" (draft); closes open question 2.

- Characters (residents, user characters, the companion) avoid each other. Each scores a
  sideways offset from its route (up to ±0.9 m where the floor is walkable) and a speed
  (full, half, stop) against everyone's predicted positions over 2 s, keeping right; a step
  that would bring two bodies together is never taken. All decide from one snapshot per tick,
  so the update order changes nothing. Priorities (world order, the companion last) break the
  symmetry: in a way too narrow to pass, the lower one walks its route back, stands aside and
  goes on once the other has passed; nobody waits for good. The player keeps priority (the
  existing sidestep).
- A stretch of route where the body hits collision (a placed object, a pillar) gets a detour
  planned on the 0.5 m standing-spot grid and is walked round in both directions. Such a route
  is now walkable (`paths.list` `walkable` true, no `npc.suspended`), and stops inside the
  stretch are skipped; guides still refuse paths that need one. Other problems (no floor, a
  ledge, outside the playable volume, a screen approach) and a stretch with no way round
  leave the route unwalkable as before. User-placed objects without a collision proxy count
  as their bounds for characters.
- A stop reached while stepped aside, or with another character standing on it, is walked
  past that time instead of snapping onto it.
- Locomotion eases its speed and blends walk and run on one shared cycle phase advanced by
  ground speed over the blended stride: no foot sliding when speeding up or slowing down. A
  character goes at most its run clip at 1.8x.
- A stop facing an anchor (`face: {anchor}`) stands in front of the screen on its normal,
  just outside the approach zone, faces it and walks back; the way there is planned on the
  standing-spot grid round the zone (at most 10 m). Without such a spot it stops on the route
  point as before (`npc.suspended` notice names the anchor).
- The companion follows with the same gait at an eased pace, slows for characters crossing
  its way, and steps aside for a resident coming through where it stands. Its E menu adds
  **Run with me** / **Walk with me** (`interaction.choose` ids `run` and `walk`, type
  `companion`); `companion.json` keeps `pace: "run"`. The companion state does not report the
  pace (a `pace` field would be IPC 0.26).
- Diagnostics: agents report `speed`, `radius`, `stride`, `gait`, `detours`, `detour`,
  `stopSpots`, `stop`, `backingOff`, `blockedFor`; the companion `pace`, `speed`, `stride`,
  `gait`, `madeWay`, `makingWay`.

## Gizmo scale settles on the floor; machines start with a demo source (no wire change)

- Scaling an object that rests on a floor (tool 4) keeps it on that floor: scaled up its
  base rises out of the floor, scaled down it drops onto it, so a size is never refused for
  "Needs a floor under it". The other placement rules still apply.
- `hyprune-demo-source` (`dev/machines/scripts/demo-source.py`, installed in
  `share/hyprune/machines/` with a `bin/` link like `hyprune-system-gauges`) prints made-up
  but plausible values for a machine's inputs (`KEY=TYPE[:ARGS]` per input). The reference
  shell writes a `sources.json` entry running it for a newly placed machine that has none
  (RFC-0013). A checkout's build links both scripts into `build/bin`, and the live launcher
  puts that first on the checkout helper's `PATH`.

## Character motion: `auto` animates whenever the world is shown (no wire change)

RFC-0010 amendment "motion follows the user's settings":

- `npc_motion` `auto` no longer pauses residents or the companion for the power supply
  (battery, USB-C, unknown), after 30 s without input, while typing or in the free cursor.
  Characters animate whenever the world is shown; they pause only while it is inactive or
  loading (`reason` `inactive`) and in focus (`focus`). `auto` and `always` now behave the
  same (`always` also pauses in focus, where the world is covered); both values stay in the
  enum. Render on demand and the occlusion test keep the cost to characters in view: a
  still player with none in view gets no frames.
- `still`, `off` and reduced motion are how the user stops characters. They refuse a
  request that can't be carried out instead of accepting it and never doing it:
  `interaction.choose` (and E on the aimed character) for a `guide` returns -32005 with the
  reason in the message (`characters are paused (motion still)`, `… (reduced motion)`,
  `… (motion off)`; E reports it as `runtime.notice` `interaction.refused`). Under `off`
  every interaction is refused, including the companion's Talk, Stay and Follow. Talk,
  waypoints and panels still run under `still` and reduced motion. A guide in progress
  ends when the setting changes to `still`, reduced motion or `off`; the resident walks
  back once motion returns. The error codes are the ones already documented for
  `interaction.choose`.
- `npcs.reason` therefore only reports `inactive`, `focus` or `reduced motion`; `battery`,
  `usb-c power`, `power unknown`, `typing`, `free cursor` and `idle` are no longer sent.

## Carrying characters (no wire change)

Super+LMB (carry) picks up the companion or one of the user's own characters; authored
residents stay protected and the reticle shows `invalid`. While carried it hangs idling
from the grab point. Within 0.75 m of a walkable route of this world (with room for it) the
route is highlighted and it snaps on; releasing binds it there (as `npcs.bind` with the
phase at that point). On a free standing spot it stands there; anywhere else it goes back
where it was picked up. It is never thrown.

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

## Say captions wrap; occluded residents cost no frames (no wire change)

- A `say` line (up to 160 characters, the schema's limit) wraps over the resident's head
  in up to four lines, in a bubble that grows upward from the anchor; it was one label cut
  at about 40 characters with "...". Other labels (world labels, markers, prompts) are
  unchanged. The overlay diagnostics list a caption's lines.
- An animating character hidden behind opaque scene geometry from the eye neither keeps
  frames coming nor is drawn: five rays (centre, head, feet, both sides of its bounds
  grown for limbs) against the world's opaque meshes (glass, masked and blended surfaces
  never hide anything) must all be blocked; one clear ray draws it, and a hit within
  0.3 m of the eye or the character (or inside its bounds grown by 0.3 m) leaves the ray
  clear. The test re-runs when
  the eye or the character moves; the runtime decides, so the offload worker draws the
  same characters. A resident outside the view going behind or out from walls renders
  nothing (only drawn characters change the frame). With `visibility_culling` off nothing is hidden. The agent diagnostics
  report `occluded`. Measured in nested Lumen Reach 0.11.0 at 4K: facing the Rime Vault
  console 60.8 fps at rest becomes 0; facing the Sun Court wall the Array Hall resident is
  in view through the open vestibule and still renders (the resident behind walls no longer
  counts or draws).

## Path stops and guides from a seat (no wire change)

- A path stop that faces an anchor (`face: {anchor}`) and an interaction waypoint to an
  anchor load: anchor references are checked after the world's anchors are loaded (paths
  load with the objects, before the mounts). An unknown anchor is still refused.
- A seated resident asked to guide stands up (its stand clip, or the sit clip reversed),
  steps off to its seat's approach point and joins the guide path; the join leg is checked
  from there, not from the seat (it was refused with -32007 "can't walk there"). After the
  guide it walks back and sits down again; the seat stays reserved meanwhile. A guide
  cancelled before the character reaches the path turns back at once.

## Character rendering (no wire change)

RFC-0010 runtime rules ("region visibility follows its current position") and character spec:

- Characters are culled by visibility regions as well as the view frustum, with the rule
  placed objects use: a character shares the areas owning scene geometry near its current
  pose. A walking character in a region that is not visible from the camera's region is
  neither drawn nor keeps frames coming (`npcs.drawn`, `animationVisible` in the camera
  diagnostics). Before, any character inside the frustum counted, including ones in other
  areas behind walls.
- Characters use their normal, metallic-roughness and occlusion textures (uploaded as
  linear data; base colour and emissive stay sRGB). Before, only the base colour and
  emissive textures were loaded while the metallic factor (glTF default 1) stayed, so every
  character shaded as a mirror: black where its probe saw no sun (indoors, at night).
- Character KTX2 textures (KHR_texture_basisu) stay GPU-compressed, as world and object
  textures do: transcoded to the renderer's block format (BC7, else BC1/BC3, else ASTC 4x4)
  with the file's own mip chain, sRGB for base colour and emissive, linear for data maps.
  A 1024² map takes about a quarter of the memory of its RGBA8 copy. Before, character
  KTX2 images were expanded to RGBA. PNG and JPEG character images are unchanged (RGBA8,
  runtime mips). The offload renderer shares this path.

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

## Window records (no wire change)

- Every client on the room and home workspaces has a surface record. The former
  16-window capture limit and the 256 MiB texture gate are gone (records are bounded
  only by the 128 `apps`); clients past them used to open invisible and appear in
  `surfaces` as `desktop-window-*` ids that `surface.pocket` rejected with -32003.
- A client with no usable buffer for a moment keeps its record, id, pose and pocket
  slot. Records of closed clients are dropped at once, also while a window is focused
  full screen or a home workspace is worked natively.
- A client that reaches the room natively (moved from a home or another workspace)
  arrives ahead of the player like a new one, or in the pocket when no spot fits.
- `editor.unplaced.windows` and `mounts.instances` stay within their 16-item schema
  bounds: `count` covers every pocketed window and the selected one stays listed; the
  carried window is listed first.

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

Object manifests (`object.json`, `library.json`) are numbered like world formats; core
checks 0.9, 0.10 (an unreleased draft) and **0.11**: 0.11 switches on the `mount` trait for
workspace screens (1-4 anchors, at most one `mounted-workspace` anchor, 16:9 within 0.5 %
or 21:9 from 2.33 to 2.39, at least 0.9 m high, every rectangle inside the bounds grown by
0.1 m, `frameNode` a scene node; `tool-mount` anchors are inert). Such objects are
placeable; world packages can't author them yet. In 0.11 the per-asset budgets of ordinary
objects (not characters or items) are recommendations: over one is a warning, and only the
hard caps refuse (2,000,000 triangles, 8,192 px textures, 1 GiB GPU, 100,000 proxy
triangles, 256 files, 512 MiB per directory, 64 MiB per file); core reads object directories
up to those caps. `import.json` (RFC-0014, importVersion 0.1) beside `object.json` is
validated with the object.

Optional world extensions, at any format version: `com.hyprune.map` 0.1 (overview
map SVG and its affine frame) and, since IPC 0.20, `com.hyprune.blueprint` 0.1
(1–6 per-level blueprint SVGs in that frame, at most 64 KiB each and 256 KiB in
total, with drawing roles named by a `class` vocabulary). Older cores ignore the
blueprint extension.

## Machines prototype: placed gauges and script feeds (no wire change)

RFC-0013 draft, object manifest 0.10 draft (no version bump):
- Every placed instance of an object whose `object.json` has the `machine` trait now runs
  as a machine; `HYPRUNE_MACHINES_PROTOTYPE` is no longer needed and stays a development
  override. By default input `<id>` reads key `<id>` of source
  `com.hyprune.sources.<instance id>`, which the source helper publishes for a
  `sources.json` entry with that `id`. `$XDG_CONFIG_HOME/hyprune/machines.json`
  (`HYPRUNE_MACHINES_FILE`) can name another source, keys, maps and `averageWindowS`
  per instance; it is re-read on change. New `runtime.notice` codes:
  `machine.source` (once per newly placed machine: the source it reads) and
  `machine.invalid` (a binding or manifest problem; that instance is skipped).
- Mapping modifier `average` (`windowS` 1–3600, optional `override` input): the
  time-weighted running average of fresh data, before the mapping's value form.
- Slot kind `levels` (`fill` about the node origin, or `slide`; `axis`, `length`,
  `surface`, `settle` `ease`/`spring`) and drive target `level:<id>` (0–1).
- Stacks: `cap` up to 100, `rate` (copies per second), signed `spacing`, copies take
  their states slot's paint; at most 1024 stacked copies per world.
- Machines render in the offload worker too (their draw state travels with each pose);
  a placed gauge no longer forces the local renderer.
- Packaging: the release archive (`dev/release/build.sh`) ships the source helper as
  `libexec/hyprune/source-helper`, the gauge script as
  `share/hyprune/machines/system-gauges.py` and the command `bin/hyprune-system-gauges`
  (a link to it); all are standard-library Python that run from those paths. The
  archive's live launcher finds the helper there and, like the checkout's, now always
  starts it with a renewable `com.hyprune.sources` grant and
  `--config $XDG_CONFIG_HOME/hyprune/sources.json`, whether or not that file exists. A helper whose one-use token came
  over a descriptor that has since closed exits with status 3 when its connection is
  lost, instead of retrying without a grant.
