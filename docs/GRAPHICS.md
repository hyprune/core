# Graphics controls and profiling

Graphics settings live in `~/.config/hyprune/graphics.json` (or
`$XDG_CONFIG_HOME/hyprune/graphics.json`). This is a sparse JSON object, separate
from Hyprland configuration. Core watches atomic saves and applies valid changes
without a Hyprland reload. Invalid edits retain the last valid settings. Shell
Settings uses IPC 0.7 `graphics.get` / `graphics.set`; it never writes this file.
The API, ranges and permissions are summarised below and in the
[changelog](CHANGELOG.md); the canonical schema is the contract.

```json
{"quality":"auto","render_scale":"auto","dynamic_resolution":true,"target_fps":60}
```

Auto selects Low/iGPU on Intel and Medium elsewhere. Choose `low`, `medium`,
`high` or `ultra` explicitly to override device selection. Effect overrides
survive preset changes; set an effect to null to return it to the preset.
Delete a file key to inherit compositor-level Lua setup, if supplied.
The existing `hl.plugin.hyprune.config({graphics={...}})` accepts the same keys.

| Effect | Low/iGPU | Medium | High | Ultra |
| --- | --- | --- | --- | --- |
| MSAA samples | 1 (off) | 1 | 2 | 4 |
| Bloom | off | extraction | blurred | blurred |
| Fog | distance | height | height | height |
| Sky | gradient | environment | environment | environment |
| Environment reflections | off | on | on | on |
| Normal maps / specular | off | on | on | on |
| Material AO | on | on | on | on |
| Blended haze/glass | off | on | on | on |
| Anisotropy | 1x | 2x | 8x | 16x |

The authoritative preset values and Settings metadata are in
`src/render/Graphics.hpp`. Dynamic resolution, target FPS and fixed scale are
independent of preset. Fixed scale is 0.375–1. Auto adapts in 1/16 steps using
nonblocking disjoint GPU timers. Intel 4K starts at 0.5; the controller reserves
40% of the frame interval for the compositor. Increases need six seconds below
60% of the core budget; reductions need .6 seconds above 108%. Reused
frames also signal insufficient headroom for the enclosing compositor; core
reduces resolution even when its own passes appear within budget. Disabling
dynamic resolution freezes the current automatic scale. Native client surfaces,
workspace screens, markers, labels and prompts retain output resolution.

Upscaling below output size uses AMD FidelityFX FSR 1 (EASU + RCAS) by default;
`sharpen` 0–1 maps to RCAS 2–0 stops. It needs GLSL ES 3.1; without it, or with
`HYPRUNE_UPSCALER=bilinear`, core uses the bilinear + bounded unsharp path, where
`sharpen` 0–1 (default .35) weights the extra taps and zero disables them.
`timings.upscaler` reports requested/active/error. IPC 0.16 exposes the choice as
the `upscaler` graphics key (`"fsr1"`, `"bilinear"`, null = preset; every preset
uses FSR 1), also settable in graphics.json and Lua; `HYPRUNE_UPSCALER` overrides
it for diagnostics. Costs and stills: [PERF-2026-10.md](PERF-2026-10.md).
`fov` is 30–110 degrees; `exposure` .01–16; `bloom_strength` 0–4;
`reduced_motion` boolean. Absent exposure/bloom strength overrides preserve the
world's authored atmosphere. SSR, runtime shadow maps and runtime contact
shadows are not implemented. Baked lighting/shadows stay in world lightmaps.

F3/Y opens a core-drawn panel at native resolution. FPS counts newly rendered
frames over a one-second window; repeated presentations do not inflate it.
GPU costs are asynchronous timer measurements, never CPU submission estimates.
Sky, world shading, MSAA resolve, bloom, upscale/sharpen, native surfaces and
output/markers have distinct non-nested queries. Material features share the
world shader, so F3 labels their aggregate as **shared cost**. It does not claim
individual fog/normal/specular costs or additive savings from switching them off.
Disjoint/unavailable measurements show N/A. CPU capture denotes texture leasing
and placement refresh, not PNG readback. U captures include the full diagnostics.

Cursor/focus/workspace transitions are unchanged. All graphics IPC and file
reloads apply outside the frame. Sampler changes avoid re-uploading the world;
FBO resize happens in the renderer under its GL state guard. MSAA-off draws
straight into the resolved colour/depth textures. Client pixels bypass material
shading and the sRGB decode/encode round trip; window borders are rings to avoid
shading the full client rectangle twice. Cached placement recovery is invalidated
on pose/size changes, world replacement and editor collision edits.

For the GPU-offload spike, the target/texture ownership interfaces are unchanged.
The native intermediate contains display-encoded pixels after reconstruction/
tone mapping; `present()` adds screen UI without tone-mapping those pixels again.
When MSAA is off, the world draws directly to the resolve colour/depth textures;
an offload backend must not read the unused multisample target in that mode.

Pointer and capsule broad phases now share a triangle BVH, built during CPU world
loading and rebuilt after object transforms. It preserves triangle order and
re-queries after capsule correction. Lumen Reach's 3,994 collision triangles:
ray 0.01361 → 0.000267 ms (51×), stationary walk step 0.2302 → 0.0900 ms (2.56×),
in the same process on this machine. Dense 10,002-triangle fixture: 323× ray,
5.54× walk. These are query benchmarks, not whole-frame FPS claims. Randomized
indexed/reference equivalence covers ray hits and capsule results; nine unit
suites pass. Raw measurements are under `evidence/review-audit/`.

Final October 8 nested measurement: native 3840×2160, Mesa Intel PTL, Lumen
Reach, automatic Low/iGPU, live terminal, 22.95 seconds: **60.01 newly rendered
frames/s, zero reused frames**. Internal scale ranged 0.375–0.5. CPU medians
update/render/capture/IPC: 0.219/0.366/0.027/0.0007 ms. GPU world/sky/native/
upscale/output: 1.868/0.149/1.543/1.536/0.473 ms; bloom 0.006 ms; MSAA resolve
zero. Encoded native composition now uses RGBA8, halving its colour bandwidth
from the former HDR target. The preceding HDR-native alpha run measured 59.56
FPS / 10 reused frames; both results are retained in evidence.

This is one static nested scene, not a guarantee for every camera, effect
preset, client or live-output arrangement. Reproduce:
`HYPRUNE_GRAPHICS_PROFILE=1 HYPRUNE_NESTED_4K=1 dev/nested/run.sh --live`.
The wrapper owns the nested lock for the whole run.

GPU uploads use `beginPrepare`, one `continuePrepare(2 ms, 2 MiB)` per deferred
tick, and `cancelPrepare`. The old world remains available until the final
upload fence signals. CPU loading/prefiltering remains on the loader worker;
loading UI stays active until publication. Texture rows and vertex data use
256 KiB chunks. GL allocation, mip generation and shader compilation are driver
calls that cannot be preempted: the time budget is soft, checked between jobs.
`timings.upload` records steps/bytes/CPU/max-step. Final nested cold load used
125 steps, 103.2 ms total CPU. The largest single job was 35.23 ms compiling
`scene_`; setup programs are separate jobs (the earlier combined setup took
57.3 ms). This remaining cold compile hitch is explicitly measured, not hidden. This removes the whole-world synchronous
upload; it does not promise that every driver operation fits 2 ms. Cancellation
releases partial resources; old-world rendering and all GL state are preserved.

Native transparency update: world tone mapping/bloom is applied during upscale,
then client buffers blend premultiplied at native resolution. World BLEND
materials join the native depth-sorted transparent list with windows; Low's
haze/glass switch skips those world materials. Intersecting transparent meshes
still have the usual per-batch sorting limit. Transparent materials do not feed
the opaque-world bloom extraction. Window buffers keep their original encoded
colours and opacity; zero-alpha corners/shadows do not erase the background.
The native intermediate is now entirely display-encoded (alpha reserved zero),
so the offload spike should export HDR opaque colour + depth and reconstruct/
tone-map before this native composition stage. Its branch was not edited.

## Runtime offload / Performance (IPC 0.10)

Additional sparse graphics keys:

| Key | Default | Meaning |
| --- | --- | --- |
| `offload` | `"off"` | `off`, `auto`, `on`; runtime handover, never a world/Hyprland reload |
| `offload_power_cap_w` | `null` | Automatic (IPC 0.18): the battery profile ceiling (50 W limited / 80 W qualified AC), or without a battery 75 % of the offload GPU's enforced power limit (80 W if unknown). A number (20–1000 W) only lowers the ceiling with a battery and is clamped to the GPU's limit without one. Before 0.18: 20–100 W, default 80 |
| `offload_device` | `"auto"` | IPC 0.18: `auto` prefers an NVIDIA GPU that isn't the compositor's, then the least utilised; or a PCI address such as `0000:65:00.0` |
| `scale_min` | `0.375` | Automatic resolution floor, 0.375–1 |
| `scale_max` | `1.0` | Automatic resolution ceiling, 0.375–1; cannot be below floor |

A fixed `render_scale` remains independent of the automatic bounds. `auto`
quality retains its device preset; the Performance UI displays `Custom` whenever
an explicit non-quality override is present. All fields, including current
requested/effective values and nullable measured costs, are available through
`graphics.get().performance.controls`. Shared-pass costs are labelled explicitly;
null is not zero. The active renderer and its power/profile/cap are reported
separately from the requested offload mode. The F3 panel shows worker pass costs,
transfer cost and local native-depth cost when offload is active.

USB-C, battery and limited AC keep automatic offload off. Explicit on uses the
50 W / 45 FPS profile. Full AC needs an advertised capacity of at least 150 W
and no battery discharge to admit 80 W / 60 FPS. A system without a battery is on
mains (`full-ac`, IPC 0.18); otherwise unknown supplies fail closed.
These are monitored software limits, not privileged hardware power-limit writes.
The worker's renewable 500 ms lease, 75 °C ceiling and fresh NVML samples are
mandatory. A fault stays on Intel until explicit off/on. Off stops the child and
releases its monitoring handle, permitting normal driver power management.

Offload is optional and defaults off. It needs the `hyprune-renderer` binary
(built with Vulkan/GBM development packages), compatible NVIDIA GLES/Vulkan
LINEAR DMA-BUF interop, and Linux x86-64 Landlock ABI 3 or newer. Unsupported
systems continue using the prepared in-process renderer. Edited world-node
transforms also select local rendering in phase 1. No cursor/focus/workspace
transition belongs to the renderer switch.
