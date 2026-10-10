# Offload with an NVIDIA consumer (2026-10-10, remote host)

Host: 2x RTX 3090 (GPU0 65:00.0 = renderD128, GPU1 b3:00.0 = renderD129),
NVIDIA 615.78.08, Hyprland 0.56.2, Lumen Reach 0.9.0, nested isolated instances
(`dev/nested/isolated.py dev/nested/offload_measure.py`), scripted orbit at the
arrival, 12 s per phase, offload off → on → off.

## Failure found

On any NVIDIA consumer (the compositor on an NVIDIA GPU), offload never activated:
`glEGLImageTargetTexture2DOES(GL_TEXTURE_2D)` on the worker's LINEAR AB24 dmabuf
returns GL_INVALID_OPERATION. `eglQueryDmaBufModifiersEXT` reports LINEAR
AB24/AR24/XB24 `external_only` on both GPUs. Mesa (the laptop's Intel consumer)
imports LINEAR as a 2D texture, which hid this. The fix: when LINEAR is
external-only, the client binds the slot as GL_TEXTURE_EXTERNAL_OES and copies
it with a samplerExternalOES triangle instead of FBO + blit. The Mesa path is
unchanged.

## Not changed, but needed for these numbers

- This desktop has no `/sys/class/power_supply` entries, so the policy reports
  "power supply unknown" and fails closed. Runs bound a fake 300 W Mains `AC0`
  in a bwrap namespace.
- The renderer GPU idles at 37–45 W and draws 90–200 W while rendering, above the
  hard 80 W full-AC cap. Core's watchdog then stops the worker, and the client's
  "renderer transport EOF/truncation" masks the supervisor's real reason. The
  numbers below come from a local experiment build whose watchdog threshold was
  350 W (not committed); 75 °C stayed enforced.
- `discreteDevice()` takes the first NVIDIA render node, so the renderer is
  always GPU0. "Cross" means the compositor on GPU1 (the headless parent's
  default) and the renderer on GPU0. A GPU1-only instance can't offload.

## Results (medians; on-phase active fraction 0.99–1.0 everywhere)

| Output | Topology | Off: compositor GPU ms | On: compositor GPU ms | On: transfer ms | On: worker render GPU ms | On: producer ready ms (p95) | On: GPU util / W |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1080p | single GPU0 | 4.45 | 0.22 | 0.73 | 1.43 | 7.4 (10.3) | GPU0 25% / 92 W |
| 1080p | cross GPU1→GPU0 | 4.41 | 0.80 | 1.45 | 3.60 | 10.3 (11.8) | GPU0 35% / 56 W, GPU1 34% / 51 W |
| 4K | single GPU0 | 12.06 | 0.38 | 2.93 | 3.76 | 13.5 (18.0) | GPU0 71% / 198 W |
| 4K | cross GPU1→GPU0 | 12.03 | 0.37 | 2.93 | 3.74 | 11.4 (13.3) | GPU0 41% / 185 W, GPU1 54% / 167 W |

The compositor GPU ms is the sum of `gpuPassMs`. Off, the in-process renderer uses
`render_scale: auto` (0.5 here); the worker renders at scale 1.0. Delivered
60.0 FPS with offload on, in every row. With offload off, frames are damage-driven
(about 7.5 FPS at the orbit's IPC rate), so frame intervals aren't comparable;
use the GPU costs. Cross-GPU at 1080p doubles the transfer (0.73 → 1.45 ms) and
the worker render; at 4K both topologies cost the same, and cross-GPU halves the
p95 producer latency, because the compositor no longer shares GPU0.
