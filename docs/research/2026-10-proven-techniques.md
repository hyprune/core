# Proven techniques we should adopt (2026-10)

Research note, 2026-10-09. Question from the owner: we went from about 20 to 60 FPS
by optimising by hand, but every engine has already solved these problems, so why
aren't we using the same tricks? And world plans made from fixed-angle stills looked
right but walked wrong. How do established pipelines prevent that?

Scope: the core renderer (`core/src/render`, `core/src/world`, `core/src/offload`),
world format 0.5/0.6, and the Lumen Reach build (`worlds/tools`, `worlds/lumen-reach`).
The constraints are fixed. Core draws with GL inside Hyprland's render pass (AGENTS.md
rule 2) on a laptop iGPU that is often on USB-C power. Bakes run only on the bake host.
The product goal is **playable**, not glossy. Checks must be **deterministic and cheap**,
not LLM vision review of stills.

## Executive summary

**What the measurements say.** The 20→60 FPS jump came mostly from **dynamic
resolution plus bandwidth cuts**. Round 3 dropped the world pass from 22.38 to 5.74 ms
by rendering at 0.375 scale (`docs/M3B.md:472-477`). The final 60.01 FPS runs at
0.375–0.5 scale on Low (`docs/GRAPHICS.md:83-90`). On 4K Intel the frame cost
therefore tracks **pixels and memory traffic**, not draw count. Hand-authored
visibility regions confirm this from the other side: they cut submissions 87→68 at
best, with "no reliable FPS improvement" (`worlds/lumen-reach/VISIBILITY.md:47-61`).

The standard fixes we lack are mostly about bytes per pixel: compressed textures,
mipmapped lightmaps, indexed and optimised meshes, and not rendering when nothing
changed. The world-side failures (layout and perspective wrong once walked) are the
problem that **blockout-first level design** exists to solve. We skipped that stage:
geometry is born as final procedural Blender code, and the layout is only checked
after a remote bake.

Top 5, ranked by playable impact against effort:

| # | Recommendation | Playable impact | Effort |
|---|---|---|---|
| 1 | **Metrics-locked plan → greybox stage before any art or bake** (§C stages 1–2). The plan JSON is authored in Python or Onshape DXF and auto-extruded into an unlit greybox GLB that core loads immediately. Deterministic gates run in milliseconds: connectivity, door/stair/corridor metrics, screen viewing depth, adventure metrics. Visibility cells and portals are derived from the plan instead of hand-written. | Highest. It catches the "looked right, walks wrong" class before a bake exists, and turns hand-authored `visibility.json` into generated data. | Medium. Mostly Python in `worlds/tools`; one schema field for plan provenance. |
| 2 | **Render on demand plus cheap GPU wins.** (a) Stop damaging the whole monitor every 16 ms when the camera, windows and animations are idle (`Plugin.cpp:3615-3616`). (b) Use mipmaps for lightmaps; they are generated but sampled through a no-mip sampler (`Renderer.cpp:314-316,986`). (c) Drop the duplicate RGBA8+sRGB upload of colour textures (`Renderer.cpp:438-448`) using `EXT_texture_sRGB_decode` or a single sRGB copy. (d) Move `transpose(inverse(model))` out of the vertex shader (`Renderer.cpp:63`). | High on a USB-C-powered laptop: idle world frames cost power and thermal headroom; (b)–(d) free bandwidth that the resolution controller turns into higher scale. | Low. Each item is days, not weeks; no format change. |
| 3 | **Adopt the standard glTF asset optimiser instead of shipping raw exports:** meshoptimizer/gltfpack (MIT) or glTF-Transform (MIT) in the build, plus KTX2 + Basis Universal (Apache-2.0) textures transcoded at load to BC7/BC1 (or whatever the driver advertises). Core gains indexed draws (today everything is flat `glDrawArrays`, `World.cpp:361-384`) and a KTX2 loader (libktx, Apache-2.0). | High for iGPU bandwidth and load time; the package (72 MB, ~372 MiB conservative GPU memory) shrinks severalfold. | Medium. One build step; core needs index buffers and a KTX2 path behind the existing upload budget. |
| 4 | **Managed object library with instancing and per-instance lightmap rectangles.** Objects are authored once (Blender asset library or Onshape → glTF) with metadata: footprint, clearance, mounts, budgets. Worlds place them by reference. The bake host bakes everything together, writing a per-instance `scale/offset` into one shared atlas, the way Unity's `lightmapScaleOffset`, Godot's per-instance lightmaps and the `MOZ_lightmap` glTF extension do. Small props use light probes plus pre-baked AO instead of atlas space. | High for pipeline speed and reuse across worlds and themes; ends "props are inline code" (`lumen-reach/build.py:209-280`). | Medium–high. World format 0.7: placements, object refs, `EXT_mesh_gpu_instancing`, lightmap rect. |
| 5 | **Replace hand-made culling with standard, derived culling.** Per-batch AABB frustum culling (absent today) plus cell/portal visibility generated from the stage-1 plan (Teller & Séquin PVS, Luebke & Georges portals). Keep the current runtime format and generate it. | Medium today, because frames are fill-bound. It matters more as worlds grow and once batches are split per room or object. Mostly removes authoring work. | Low (frustum) to medium (portal generator). |

**Not recommended now.** Embedding a third-party renderer in-process: they want
their own context, thread and swapchain, which collides with rule 2. If we ever want
an engine renderer, the place for it is behind the existing RFC-0009 process
boundary (dmabuf hand-off), not inside Hyprland's pass (§A.4). GPU-driven,
meshlet, Hi-Z and HLOD techniques are standard too, but they target scenes 100× our
size; skip them.

---

## A. Rendering and performance

### A.1 Inventory: what core already does like everyone else

Much of what we built is standard, and we should keep it. It is "reinvented" only in
the sense that it is in-house code, not that it is novel.

| Technique | Core today | Standard equivalent | Verdict |
|---|---|---|---|
| Baked lightmaps (Cycles DIFFUSE direct+indirect, OIDN `RTLightmap`) | `worlds/tools/bake_pipeline.py:83-105`; TEXCOORD_1, PNG, intensity scale (`World.cpp:195-207,297-304`) | Same in every engine | Keep |
| Dynamic resolution (EMA of GPU time, hysteresis, 0.375–1 scale) | `render/Resolution.hpp:15-36` | Unreal Dynamic Resolution heuristic: GPU-time budget, sliding window | Keep; the logic matches the standard |
| GPU timer query ring (never waits) | `render/GPUTimer.hpp` | Standard | Keep |
| ≤2 frames in flight, non-blocking fences, reuse last frame | `Renderer.cpp:859-879` | Standard frame pacing | Keep |
| Program binary cache plus warmup draws | `render/ProgramCache.hpp`, `docs/SHADER-WARMUP.md` (26.4→0.9 ms hitch) | PSO/shader precaching | Keep |
| Budgeted chunked uploads (2 ms / 2 MiB per tick) | `docs/GRAPHICS.md:97-108` | Streaming budgets | Keep |
| Runtime mip generation plus per-sampler anisotropy | `Renderer.cpp:460-487` | Standard (but mips belong in the asset, see KTX2) | Keep; move offline |
| Back-to-front sort of blended draws | `Renderer.cpp:993-999,1230-1241` | Standard | Keep |
| Triangle BVH for pointer and collision queries | `src/sim/TriangleIndex.hpp` | Standard | Keep |
| Client windows sampled directly from Hyprland textures (no copies) | `platform/hyprland/Capture.cpp:82-95` | Zero-copy compositing | Keep |

### A.2 What we lack

| Missing technique | Evidence | Why it matters here | Adopt |
|---|---|---|---|
| **On-demand rendering / idle skip** | Every 16 ms tick calls `damageMonitor` and `scheduleFrame` while active (`Plugin.cpp:3615-3616`, `4449-4460`) | A static world with a static window still costs a full frame (about 10 full-screen passes) 60 times a second on battery or USB-C. Editors, CAD tools and compositors all render on change. | Keep a dirty flag set by camera motion, window damage, animated materials and HUD changes. Without one, present nothing new and let Hyprland's damage tracking do its job. |
| **Lightmap mipmaps** | Lightmaps are bound to `clamp_` with `GL_LINEAR` minification (`Renderer.cpp:314-316,986`), although mips are generated and the bake report declares `supportedMipLevels: 2` | Distant lightmap texels alias (shimmer while walking) and thrash the texture cache | `GL_LINEAR_MIPMAP_LINEAR` limited to the declared safe levels (`GL_TEXTURE_MAX_LEVEL`) |
| **Texture compression** | Every texture is RGBA8; colour and emissive textures are uploaded twice, as RGBA8 and SRGB8_ALPHA8 (`Renderer.cpp:438-448`, budget ×2 at `:351`) | 4–8× less memory and bandwidth per texel on a shared-memory iGPU | KTX2 + Basis Universal: UASTC for normals and lightmaps, ETC1S for albedo. Transcode at load to BC7/BC5/BC1 if the driver lists them (check extension strings; do not assume). One sRGB copy, or `EXT_texture_sRGB_decode` per sampler. |
| **Index buffers and mesh optimisation** | Indices are expanded into a flat list (`World.cpp:361-384`), drawn with `glDrawArrays`, 40-byte float vertices (`World.hpp:11-14`) | No post-transform cache reuse, and roughly 3× vertex memory. Small in GPU ms today, but free to fix. | meshoptimizer: `meshopt_optimizeVertexCache`, `optimizeOverdraw`, `optimizeVertexFetch`, quantisation. Via gltfpack offline, so core only needs `glDrawElements`. |
| **Frustum culling** | None; every batch that passes visibility is drawn (`Renderer.cpp:987-1071`) | Cheap CPU win, and it scales once worlds are chunked per room or object | Per-batch world AABB vs 6 planes; the BVH code already computes bounds |
| **Uniform blocks / cached locations** | About 25 `glGetUniformLocation` string lookups per batch (`Renderer.cpp:1002-1069`) | CPU cost per draw; it grows with instancing and objects | Cache locations at link time; one std140 UBO per material |
| **Instancing** | None; scene has 295 nodes and 295 meshes, 1:1 | Needed by the object library (§B) | `EXT_mesh_gpu_instancing` → `glDrawElementsInstanced` with a per-instance transform and lightmap-rect attribute |
| **Light probes for non-lightmapped objects** | None (one sun, 0.24 ambient floor, `Renderer.cpp:117-128`) | Small props and the player's held objects would match baked lighting | A baked L1 SH probe grid (3D texture) from the bake host; Godot LightmapGI and Unity Light Probes do exactly this |
| **Standard upscaler** | Custom 4-tap sharpen in the upscale pass (`Renderer.cpp:180-182,1177-1200`) | We always run at 0.375–0.5 scale on Intel 4K, so upscale quality *is* the image | AMD FidelityFX **FSR 1** (EASU + RCAS; MIT, plain fragment shaders, GLES-portable). Spatial-only: no motion vectors or history, so it fits our pass structure. |
| **Engine-style debug view modes** | None. Image checks rely on beauty renders and human contact sheets | They make image-space checks deterministic (see §C) | Debug outputs: node/object ID, lightmap texel density, overdraw, collision. Unreal "buffer visualization" and Godot debug draw modes are the precedent. |

### A.3 What we reinvented, and the standard to replace or derive it from

| In-house system | Standard | Recommendation |
|---|---|---|
| **Visibility regions**: hand-written AABB areas, node lists and directed `visibleFrom` links in `lumen-reach/visibility.json`; validated by paired screenshots (`tools/check-visibility*.py`) | Cells and portals with a precomputed potentially visible set (Teller & Séquin 1991), or runtime portal traversal (Luebke & Georges 1995). Engines bake occluders instead (Godot: Embree CPU raster of baked occluders). | Keep the runtime format, **stop authoring it by hand**. Rooms and doorways in the stage-1 plan *are* cells and portals; generate `visibility.areas` from them (conservative, with a portal-frustum or sampled-ray PVS). The manual 11-area file and its paired-screenshot validation become a generator plus a unit test. |
| **Route ribbons as "navmesh"** (`lumen-reach/build.py:704-711`) and capsule sampling along edges (`check-walking.py`) | Recast/Detour (zlib): voxelise the collision mesh with agent radius/height/climb/slope → navmesh, connectivity, clearances | Generate a Recast navmesh from the collision export in the build (Recast via `recast-navigation-js` in the Node tooling, or a small C++ tool). Use it for connectivity, reachable-volume sampling and "is every screen approach reachable". Keep the route graph as *intent*, and check it against the navmesh. |
| **Custom upscale sharpen** | FSR 1 EASU+RCAS | Replace (A.2) |
| **Program binary cache on top of Mesa's own disk cache** | Driver caches | Harmless; keep, because it also covers warmup and the offload worker |
| **Merged-per-area meshes with per-group material copies** (`bake_pipeline.py:63-68`) | Static batching / HLOD per cell, but with shared materials | Fine for the shell; objects go to instancing (§B) |

### A.4 Embedding an existing renderer: realistic?

| Option | Licence | Fit under the compositor constraints |
|---|---|---|
| Filament | Apache-2.0 | Supports a shared GL context plus a headless swapchain, rendering into an FBO texture. It still wants its own context and render thread, and gltfio has no lightmap path (custom materials needed). Possible **only out of process**. |
| bgfx | BSD-2 | Can be driven single-threaded, but owns GL state. Inside Hyprland's pass we would still need GLState save/restore around a foreign renderer that we don't control. |
| Godot as a library (libgodot) | MIT | Has everything (lightmaps, probes, occlusion, navmesh), but is a whole engine, input loop and scene system. Only viable behind the offload boundary, and doubles the stack. |

**Conclusion.** In-process, no. Rule 2 (render-pass safety, deferred cursor and
workspace changes, GL state restore) and two past SIGSEGVs make a foreign GL renderer
inside Hyprland's pass the riskiest change we could make. Out of process, an engine
could live in the RFC-0009 `hyprune-renderer` slot, which already hands a dmabuf to
core. But RFC-0009 itself found offload "is not needed merely to reach 60 FPS on this
Intel device" (RFC-0009 `:92-93`), and our renderer is about 2k lines doing a small,
well-understood job. The industry-standard move at our scale is to **adopt libraries,
not engines**: meshoptimizer, Basis/libktx, FSR 1, Recast, and optionally xatlas
for lightmap UVs.

### A.5 Libraries to adopt (licences)

| Library | Use | Licence |
|---|---|---|
| [meshoptimizer / gltfpack](https://github.com/zeux/meshoptimizer) | Offline mesh optimisation, quantisation, `EXT_meshopt_compression`, simplification for vista LODs | MIT |
| [glTF-Transform](https://gltf-transform.dev/cli) | Node CLI/library: `dedup`, `instance` (→ `EXT_mesh_gpu_instancing`), `flatten`, `join`, `uastc`/`etc1s`, `inspect` (stats for budget gates) | MIT |
| [Basis Universal](https://github.com/BinomialLLC/basis_universal) | UASTC/ETC1S encoder and transcoder (KTX2) | Apache-2.0 |
| [KTX-Software / libktx](https://github.com/KhronosGroup/KTX-Software) | KTX2 loading/transcoding in core | Apache-2.0 |
| [FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | Upscale plus sharpen | MIT |
| [Recast & Detour](https://recastnav.com/) / [recast-navigation-js](https://github.com/isaac-mason/recast-navigation-js) | Navmesh generation and queries for build checks | zlib / MIT |
| [xatlas](https://github.com/jpcy/xatlas) | Lightmap UV charting and packing (Blender `smart_project` alternative when tiny parts exhaust gutters, `RENDER-REVIEW.md:40-42`) | MIT |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (`CharacterVirtual`) | *Optional, later*: a standard character controller if walking and climbing outgrow the custom BVH. Not now; ours works and is tested. | MIT |
| cgltf (already used) | glTF loading | MIT |

---

## B. World pipeline: standard practice and our gaps

### B.1 What established pipelines do

1. **Metrics first.** Player height and radius, eye height, door, corridor and stair
   sizes are fixed *before* geometry, and everything is built as multiples of them
   ([The Level Design Book: metrics](https://book.leveldesignbook.com/process/blockout/metrics)).
   A reference capsule and a door frame sit in every scene.
2. **Blockout / greybox.** Simple untextured geometry is walked in-engine, iterated
   cheaply, and only then handed to art
   ([The Level Design Book: blockout](https://book.leveldesignbook.com/process/blockout)).
   Hero stills come *after* the space works, and are taken from the walked camera.
3. **Modular kits on a grid.** Wall, floor, stair, door and pillar pieces sit on a
   fixed module (for example 0.5/1/2/4 m), with pivots at the floor and snapping.
4. **Prefabs / asset libraries.** Props are authored once and placed by reference.
   In Blender this is the **Asset Browser** with catalogs, plus **linked collections
   with library overrides**: per-instance edits stay in sync with the source file
   ([Library Overrides](https://docs.blender.org/manual/en/latest/files/linked_libraries/library_overrides.html),
   [Asset Browser](https://docs.blender.org/manual/en/latest/editors/asset_browser.html)).
5. **Instancing in the interchange format.** glTF `EXT_mesh_gpu_instancing` (the
   Blender exporter's "GPU instances" option). Limits: instances must be child-less
   meshes under one parent, with no per-instance material variation
   ([Blender glTF exporter](https://docs.blender.org/manual/en/latest/addons/import_export/scene_gltf2.html)).
   The extension allows custom per-instance attributes prefixed with `_`
   ([spec](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Vendor/EXT_mesh_gpu_instancing)),
   which is where a lightmap rect can go.
6. **Baking lightmaps for instanced objects.** The mesh has one unique UV2 layout
   in 0–1. Each *instance* gets its own rectangle in a shared atlas via a
   **scale/offset** pair:
   - Unity: [`lightmapScaleOffset`](https://docs.unity3d.com/ScriptReference/CombineInstance-lightmapScaleOffset.html).
   - glTF: [`MOZ_lightmap`](https://hackmd.io/@XR/lightmapping-web) has per-node scale/offset.
   - Godot: "a sub-scene can be instanced several times… each instance will be
     assigned a lightmap of its own", with a per-instance lightmap scale
     ([Godot LightmapGI](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_lightmap_gi.html)).

   **Dynamic and small objects get light probes** instead of atlas space (same page).
7. **Cheap automated validation** is routine in production pipelines:
   - navmesh connectivity and coverage
   - clearance and capsule tests
   - scale sanity against category ranges
   - top-down maps
   - leak checks (ray or flood fill from the outside)
   - budget reports
   - scripted fly-throughs with frame timing

### B.2 Hyprune today vs that practice

| Practice | We have | We lack | Adopt |
|---|---|---|---|
| Metrics | Capsule 0.3/1.8 m, eye +1.7 m, slope ≤0.4, window sizes 1.6×0.9 and 3.2×1.8, approaches 4/16 m (`RFC-0004:132-186`, `check-walking.py:47-54`) | Door width, corridor width, ceiling, stair riser/tread, screen viewing depth as **named constants checked before geometry** | A single `metrics.json` shared by plan checks, the greybox generator and `check-*` |
| Blockout | None. Layout is coordinates inside `court()`/`hall()`/`vault()`/`rise()` (`build.py:300-680`), first seen after a remote bake (about 5 min) | A plan stage and an unbaked greybox that core can load | Stages 1–2 below |
| Modular kits | `polish.py` "detail kit" and `screen()`/`lamp()`/`crate()` as Python functions | Reusable assets with metadata, usable across worlds | Object library (stage 3) |
| Prefabs and instancing | None: 295 nodes / 295 meshes; geometry joined per area (`bake_pipeline.py:63-75`) | Object references, `EXT_mesh_gpu_instancing`, per-instance lightmap rects | World format 0.7 |
| Lightmapping | Good (Cycles + OIDN, texel-density gate, UV1 overlap gate) | Per-instance rects; probes for small objects; mipped sampling at runtime | Stage 3/4 |
| Navmesh | Route ribbons (Lumen), 0.5 m grid (Switchyard) | A navmesh from the real collision | Recast |
| Automated checks | Strong geometric set: walking, placement SAT sweeps, coplanar, emissive, package, texel density, visibility | Plan-level checks; leak checks; inside-geometry checks; reachable-volume sampling; perf gate on a scripted route | §C gates |
| Top-down plan | `map.svg` drawn from the route graph after the build | A plan as the *source*, not a by-product | Stage 1 |

### B.3 Onshape as a front end for stages 1–3

Onshape exports sketches as **DXF/DWG**. Parts, Part Studios and assemblies export as
**glTF/GLB, OBJ, STEP, STL, 3MF and others**
([Onshape export formats](https://cad.onshape.com/help/Content/exporting-files.htm)).
Exports can also be triggered through Onshape's REST API, so no manual clicks are
needed.

- **Stage 1 (plan):** one Top-plane sketch per layer, named `walls`, `openings`,
  `stairs`, `screens`, `paths`, `vistas`, and so on, exported as DXF.
  Onshape variables and configurations hold the metrics (door width, eye height),
  so the owner designs against the same numbers the checks use. A small importer
  converts DXF to `plan.json` (closed polylines become rooms; lines on
  `openings` become portals).
- **Stage 2 (massing):** a Part Studio of extruded massing, with floor slabs,
  levels, ramps and terraces, exported as GLB. Part names map to the cells and
  surfaces in the plan.
- **Stage 3 (objects):** terminals, benches, consoles and lamps modelled
  parametrically and exported as glTF. CAD tessellation produces long, thin
  triangles that bake poorly. Library ingest therefore always runs weld/simplify
  (meshoptimizer) and generates UV2 (xatlas or Blender) instead of trusting CAD
  UVs.

---

## C. Proposal: staged world pipeline with cheap gates

Principle: **every stage produces an artifact that core can load or a script can
check in seconds, before the next, more expensive stage runs.** No stage needs a
vision model. Humans walk the greybox; machines check numbers.

Shared input: `worlds/metrics.json`. Proposed starting values, to tune with the
owner by walking a stage-1 test plan:

| Metric | Proposed default | Already in code |
|---|---|---|
| Capsule | radius 0.3 m, height 1.8 m | yes |
| Eye height | 1.7 m | yes |
| Door clear width × height | ≥1.2 × 2.4 m (capsule is 0.6 m; camera comfort needs more) | no |
| Corridor width | ≥2.0 m | no |
| Ceiling over walkable area | ≥2.8 m, or open sky | no |
| Stair riser / tread | 0.15–0.18 m / ≥0.28 m; ramps ≤0.4 slope | slope only |
| Screen viewing depth | Clear floor in front of each anchor ≥ `w / (2·tan 20°)` + 1 m (about 3.2 m for 1.6 m windows, 5.4 m for 3.2 m), the screen spanning ≤40° horizontally from the eye | approach 4/16 m |
| Fly/walk volumes | `playable` (format 0.6) | yes |

### Stage 1: plan (layout only)

- **Artifact:** `plan.json`, either hand-written, generated by Python, or converted
  from Onshape DXF. It describes:
  - rooms and terraces as polygons with a floor height, plus a ceiling height or
    "open sky"
  - openings as portals between rooms, with width and height
  - stairs and ramps, with rise and run
  - screen slots: anchor pose, size, required viewing zone
  - spawn, intended routes, landmarks and vista lines
  - area theme tags
- **Automated gate** (pure Python, under 1 s, no Blender, no GPU):
  - The room/portal graph is connected from spawn. Every screen slot and landmark
    is reachable.
  - Metrics: every door, corridor, stair and ceiling meets `metrics.json`.
  - Each screen's viewing zone lies inside walkable floor and is not crossed by a
    portal or stair.
  - **Adventure metrics**, so a plan can't regress to "all grid, no world":
    - number of distinct areas
    - elevation range and level count
    - loop count (cycle rank of the graph)
    - max straight-corridor length
    - fraction of rooms that are axis-aligned and same-size (a simple
      regularity score, warning only)
    - vista lines: 2D line of sight from route points to each landmark, checked
      against wall polygons
  - Visibility cells and portals are generated here (§A.3), with conservative
    room-to-room PVS by portal-frustum sampling in 2.5D.
- **Human-cheap output:** a top-down SVG with rooms, portals, routes, viewing zones,
  vista lines and failing items in red. One glance, no render.

### Stage 2: greybox and shell (rooms, environment, paths)

- **2a, greybox (minutes, local, no bake):** auto-extrude `plan.json` into a GLB
  with:
  - walls with door holes, floors, stair treads and ramps
  - an unlit checker or grid material (1 m module visible)
  - a 1.8 m reference capsule at each screen slot
  - placeholder screen quads

  The greybox is a valid world package (`lightmaps: []`). Core loads it in the
  nested session at once, and the owner walks it in a live session. Most layout
  rejections should happen here.
- **2b, shell:** real architecture replaces the greybox: procedural Blender code
  as today, or Onshape massing GLB, or kit pieces. The plan stays the source of
  truth.
- **Automated gate** (offline, numpy plus Recast, seconds):
  1. **Plan conformance.** Build a Recast navmesh from the collision export
     (agent 0.3/1.8 m, slope 0.4). Compare it to the plan's walkable polygons
     (IoU per room ≥ threshold). Every planned portal must still be traversable,
     and nothing should be walkable that the plan didn't intend.
  2. **Existing checks, moved earlier:** `check-walking` (route graph against the
     navmesh), `check-placement` (window sweeps along approaches), `check-coplanar`.
  3. **Sky-leak ray test.** From sample points inside every room marked indoor,
     cast about 256 Fibonacci-sphere rays against the visual and collision mesh.
     A ray that escapes beyond the far-clip envelope without passing through a
     planned opening is a leak; report the point and the direction.
  4. **Camera-inside-geometry test.** For eye points sampled across the *reachable
     volume* (navmesh positions plus 1.7 m, plus fly samples inside `playable`),
     cast hemisphere rays. If more than X% hit back faces, or the nearest surface
     is closer than the near plane plus a margin, the point is inside or clipping
     geometry. This is the deterministic version of the any-angle review, and
     catches problems like a camera place sitting inside a building shell.
  5. **Window-in-wall.** For every anchor and placement zone, check the SAT sweep
     already in `check-placement`, run at plan time.
- **Route capture** (nested, deterministic, no LLM): generate a pose list from
  navmesh paths spawn → each area → each screen, at eye height and walking speed.
  Render at low resolution with the **object-ID debug view** (A.2) and log
  per-pose numbers:
  - sky-ID pixel fraction in indoor rooms (target 0)
  - near-plane clip pixels
  - screen-anchor coverage, i.e. how much of the target screen is visible and
    unoccluded from its approach
  - GPU pass times

  The output is JSON thresholds plus one video for the gallery. People watch the
  video; the gate is the JSON.

### Stage 3: objects (managed library, placed by reference)

- **Library layout** (proposal: `worlds/library/<objectId>/`), one folder per object:
  - `object.glb`: geometry, UV0, a unique non-overlapping UV2, LOD0
    (plus an optional simplified LOD made with meshoptimizer)
  - `object.json`:
    - id, version, tags and themes
    - category, so scale checks have ranges
    - footprint and clearance box, i.e. its use zone
    - collision proxy (box, convex or mesh)
    - mount points: screen anchors, tool mounts, light emitters
    - lightmap mode, `atlas` or `probe`, and a texel budget
    - triangle and texture budgets
    - provenance and licence
  - The source `.blend` is marked as an asset in a Blender asset library, or an
    Onshape document link.
- **World placements:** `placements.json` holds one entry per placed instance:
  `{objectId, version, transform, variant, theme}`.
  - The bake host instantiates placements as linked collections. For baking it
    makes instances real, packs each instance's UV2 rectangle into the area atlas
    (rectangle packing), bakes the shell and objects together in one Cycles pass
    (the owner's "baked together"), then writes back a shared mesh plus a
    per-instance `lightmapScaleOffset`.
  - `probe` objects (small props) are excluded from the atlas. They receive an
    object-space AO bake once, in the library, and use a world SH probe grid
    baked on the same run.
- **Format and runtime:** world format 0.7 adds:
  - `objects[]`: library id and version, with glTF mesh refs
  - per-node or per-instance lightmap rect: a manifest field, or an
    `EXT_mesh_gpu_instancing` attribute `_LIGHTMAP_ST`
  - an optional probe-grid texture

  Core draws one `glDrawElementsInstanced` per object mesh and material. Like any
  contract change, this goes through an RFC-0004 amendment, a schema bump, SDK
  types and a [changelog](../CHANGELOG.md) entry.
- **Automated gates:**
  - **Per object, in library CI** (seconds):
    - glTF validator (`validate-gltf.mjs`)
    - pivot on the floor
    - bounding box inside the category's scale range (a bench seat at 0.4–0.5 m;
      a terminal screen centre near eye height)
    - UV2 inside 0–1 and overlap-free (reuse the `check-package` raster)
    - triangle and texture budgets
    - collision proxy watertight
    - licence field present
  - **Per placement** (seconds):
    - support ray hits the floor within 2 cm
    - no intersection with walls or other objects (OBB/SAT, existing code)
    - clearance box doesn't cut route capsules or screen viewing zones
    - mount points face open floor
    - per-area instance and draw budget

### Stage 4: bake, optimise, package

- **Bake** on the bake host as today: Cycles + OIDN, with the current texel-density
  and UV1-coverage guards.
- **Optimise** with gltfpack or glTF-Transform:
  - dedup, instance, weld, vertex-cache and overdraw order, quantise
  - KTX2 textures: UASTC for lightmaps and normals, ETC1S or UASTC for albedo
- **Gates:**
  - existing `check-package`, `check-visibility` (generated input now),
    `check-any-angle` (offline part)
  - `gltf-transform inspect` budgets: triangles, draws, GPU MiB including mips
  - a **perf gate** on the stage-2 route capture: p50/p95 GPU frame time and
    settled resolution scale on the reference Intel device stay within
    thresholds against the previous release
  - pixel-diff regression (SSIM/ΔE) of fixed route poses against the previous
    build, used only to *flag* changes for a human

### What a cheap check looks like

Every check follows the existing `check-*.py` pattern: numpy, `gltf_io.py`, a JSON
report and a non-zero exit on failure. It runs in seconds on the laptop without
Blender or a GPU, so it can run on every edit and every agent iteration. Only route
capture and the perf gate need the nested session, and they produce numbers, not
pictures for a model to judge. The expensive human or LLM attention goes to the
greybox walk (stage 2a) and the final art review, where only people can judge.

### Suggested order of work

1. `metrics.json`, plan schema, plan checks plus SVG, greybox extruder
   (worlds/tools). Pilot it on the next Lumen Reach area or a new small test
   world.
2. Core quick wins, no format change: idle skip, lightmap mips, single sRGB
   upload, uniform caching, frustum culling, index buffers. Measure each with the
   existing GPU timers at a fixed scale.
3. Recast navmesh, sky-leak and inside-geometry checks.
4. gltfpack/KTX2 in the build, plus the core KTX2 loader.
5. World format 0.7: objects, placements, instancing, lightmap rects, probes. Then
   the object library.
6. FSR 1 upscale (A/B against the current sharpen on the same route capture).

## Sources

- Teller & Séquin, *Visibility Preprocessing for Interactive Walkthroughs*, SIGGRAPH 1991: https://www.cs.princeton.edu/courses/archive/spr01/cs598b/papers/teller91.pdf
- Luebke & Georges, *Portals and Mirrors: Simple, Fast Evaluation of Potentially Visible Sets*, I3D 1995: https://cs.unc.edu/people/david-luebke/
- Godot occlusion culling (Embree-baked occluders): https://docs.godotengine.org/en/stable/tutorials/3d/occlusion_culling.html
- Godot LightmapGI (per-instance lightmaps, probes, texel size): https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_lightmap_gi.html
- Unity `lightmapScaleOffset`: https://docs.unity3d.com/ScriptReference/CombineInstance-lightmapScaleOffset.html
- `MOZ_lightmap` and web lightmapping notes: https://hackmd.io/@XR/lightmapping-web
- Unreal dynamic resolution: https://dev.epicgames.com/documentation/unreal-engine/dynamic-resolution-in-unreal-engine
- Hi-Z / GPU culling overview (not recommended at our scale): https://vkguide.dev/docs/gpudriven/compute_culling/
- meshoptimizer / gltfpack: https://github.com/zeux/meshoptimizer
- glTF-Transform CLI: https://gltf-transform.dev/cli
- Basis Universal: https://github.com/BinomialLLC/basis_universal
- KTX-Software: https://github.com/KhronosGroup/KTX-Software
- `KHR_texture_basisu`: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_basisu
- `EXT_mesh_gpu_instancing`: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Vendor/EXT_mesh_gpu_instancing
- `EXT_texture_sRGB_decode`: https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_sRGB_decode.txt
- FidelityFX FSR 1: https://github.com/GPUOpen-Effects/FidelityFX-FSR
- Recast & Detour: https://recastnav.com/ ; recast-navigation-js: https://github.com/isaac-mason/recast-navigation-js
- xatlas: https://github.com/jpcy/xatlas
- Jolt Physics: https://github.com/jrouwe/JoltPhysics
- Filament shared-context integration: https://github.com/google/filament/issues/1921
- Blender library overrides: https://docs.blender.org/manual/en/latest/files/linked_libraries/library_overrides.html
- Blender asset browser: https://docs.blender.org/manual/en/latest/editors/asset_browser.html
- Blender glTF exporter (GPU instances): https://docs.blender.org/manual/en/latest/addons/import_export/scene_gltf2.html
- The Level Design Book, blockout and metrics: https://book.leveldesignbook.com/process/blockout , https://book.leveldesignbook.com/process/blockout/metrics
- Onshape export formats: https://cad.onshape.com/help/Content/exporting-files.htm

Internal evidence cited: `core/docs/{GRAPHICS,M3B,AUTHORING,INK-PERFORMANCE,SHADER-WARMUP}.md`,
`core/src/render/Renderer.cpp`, `core/src/world/World.cpp`,
`core/src/platform/hyprland/Plugin.cpp`, RFC-0004, RFC-0009,
`worlds/tools/bake_pipeline.py`, `worlds/lumen-reach/{build.py,VISIBILITY.md,RENDER-REVIEW.md}`.
