# Graphics controls and profiling

Graphics settings live in `~/.config/hyprune/graphics.json` (or
`$XDG_CONFIG_HOME/hyprune/graphics.json`). This is a sparse JSON object, separate
from Hyprland configuration. Core watches atomic saves and applies valid changes
without a Hyprland reload. Invalid edits retain the last valid settings. Shell
Settings uses IPC 0.7 `graphics.get` / `graphics.set`; it never writes this file.
The API, ranges, permissions and schema handoff are documented in
[CHANGES-FOR-SDK-AND-SHELL.md](CHANGES-FOR-SDK-AND-SHELL.md#graphics-settings-integration-core-only-handoff-2026-10-08).

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

`sharpen` is 0–1 (default .35); zero disables the extra reconstruction taps.
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
The native HDR intermediate now uses alpha 1 for linear world pixels and alpha 0
for already encoded native-client pixels. `present()` tone-maps only the former.
When MSAA is off, the world draws directly to the resolve colour/depth textures;
an offload backend must not read the unused multisample target in that mode.
