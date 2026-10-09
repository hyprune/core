# DRAFT (not committed): world format 0.7, compressed and optimised assets

Status: draft for the lead. Core support is on local branch
`perf/compressed-assets` (commit 4282659, on top of perf/render-standard);
nothing in docs/schema/sdk was changed.

## RFC-0004 amendment

Packages declaring `formatVersion >= 0.7` may use, in `scene.gltf`:

| Extension | Rules |
| --- | --- |
| `KHR_texture_basisu` | KTX2 images, 2D, LDR, ETC1S or UASTC 4x4, optional Zstandard supercompression. Include the full mip chain (core uploads the package's levels; it cannot generate mips for block formats). Colour textures are sRGB-encoded, data textures (normal, ORM) linear. A PNG `source` fallback is optional. **Lightmaps stay PNG** (v0 lightmap rule unchanged; HDR/KTX2 lightmaps need their own amendment). |
| `EXT_meshopt_compression` | Any mode/filter. The fallback buffer may be omitted (`fallback: true`). |
| `KHR_mesh_quantization` | Allowed. Position quantisation precision must keep collision within the walking tolerances; 16-bit positions and 16-bit texcoords are recommended (TEXCOORD_1 drives lightmap lookups). |

Runtime behaviour (normative for core): KTX2 is transcoded to a block format the
GPU advertises (BC7, BC1/BC3, ASTC 4x4) or RGBA. Budgets count the transcoded
size. Older cores reject these packages through `extensionsRequired`, so a 0.7
package must list the extensions it uses as required.

## Schema (`schema/world` 0.7)

- `formatVersion` enum gains `"0.7"`; world.json shape unchanged.
- Package validator: when 0.7, accept the three glTF extensions above in
  `extensionsRequired`; for < 0.7 keep rejecting them. Reject KTX2 lightmaps.
- Version helper + inheritance test (AGENTS.md rule 6).

## SDK / tooling

- No IPC change. `hyprune-world-check` estimates RGBA (worst case) because it has
  no GL context; the in-session `timings.upload.gpuBytes` reports the real size.
- Worlds build note: glTF-Transform drops textures that no material references
  (Hyprune lightmaps are referenced only by world.json), and reorders material
  textures. Keep lightmaps as the trailing textures and re-append them after
  `gltf-transform meshopt`, or encode textures with `basisu` and add
  `KHR_texture_basisu` in place (what the core test conversion does). gltfpack
  renumbers nodes and materials, which breaks world.json references.

## CHANGES-FOR-SDK-AND-SHELL.md

- No shell/SDK action; mention 0.7 packages and the Lumen size/memory numbers.
