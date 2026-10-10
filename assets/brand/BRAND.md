# Vendored brand assets (capture watermark)

Copied from the meta repo **hyprune/hyprune at `352e777`**
(`352e7777f7707aba1e990277ff630db1c20d72d0`, "Add the brand folder"). Rules
and the original files are in that repo's `brand/README.md`.

Licence: the artwork (SVGs, the derived mask PNGs and `src/render/BrandMasks.hpp`)
is **CC-BY-4.0**, not MIT. Attribution: "Hyprune logo by Hyprune contributors,
CC-BY-4.0". The licence grants no trademark rights; don't use the mark in a way
that suggests endorsement. `dev/brand-masks.py` is MIT source.

| File here | Source (meta repo) | SHA-256 (from `brand/SHA256SUMS`) |
|---|---|---|
| `lockup-bold-horizontal-mono.svg` | `brand/svg/bold/lockup-bold-horizontal-mono.svg` | `eee1094a0e513f90b67ac9eeea43236a0cce2939117f491b4269a32688aec034` |
| `lockup-bold-horizontal-small-mono.svg` | `brand/svg/bold/lockup-bold-horizontal-small-mono.svg` | `4a7d774146d779fbb0f2f87d543356e53f70da7b65ff02a8462dc6d09f56d968` |

The SVGs are byte-identical copies. Derived from them (not in the meta repo):

| File | What |
|---|---|
| `lockup-bold-horizontal-mono-mask.png` | 8-bit coverage (alpha) of the full Bold lockup, rasterised at 2x (1680 px viewBox width), cropped to the painted ink: 1529 x 370, origin (43.5, 35.5) source units |
| `lockup-bold-horizontal-small-mono-mask.png` | Same for the small Bold lockup at 1x (840 px), cropped: 765 x 186, origin (43, 35) |
| `../../src/render/BrandMasks.hpp` | The two mask PNGs as byte arrays plus crop metadata, compiled into core |

Core has no SVG renderer and doesn't get one: the masks are pre-rasterised and
`src/render/Watermark.cpp` area-filters (exact box filter) them down to the
drawn width at capture time. The full lockup is used at 240–1024 px, the small
one at 160–239 px (brand size rules), so the masters are always downscaled.

Regenerate (light, seconds) after a brand change:

```sh
python3 dev/brand-masks.py --meta ../hyprune --commit <meta-commit>
# then update the commit and hashes above
```

It verifies each SVG against that commit's `SHA256SUMS`, copies it here and
rasterises with `resvg` (0.48.1 was used) and Pillow.

Licence: "Hyprune logo by Hyprune contributors", **CC-BY-4.0**
(<https://creativecommons.org/licenses/by/4.0/>). No trademark rights are
granted. The generator script is MIT like the rest of core.

## Where it's used

Only `capture.watermark = true` (Lua config, default **false**) applies it,
and only to user captures written by `Runtime::debugCapture()` (U, and photo
mode stills, which go through the same path). It runs on the capture writer
thread, never in the render pass, and is never drawn on the live HUD or the
world. See `docs/M3A.md` (config table) and `README.md`.
