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

## Policy fixes (IPC 0.18, same branch)

The first runs below needed workarounds that IPC 0.18 removes:

- **No battery = mains.** This desktop has no `/sys/class/power_supply`
  entries, which was "power supply unknown" (fail closed). Now it is `full-ac`.
- **Device-scaled cap.** The renderer GPU idles at 37–45 W and draws 90–200 W
  rendering, above the old hard 80 W cap, so the watchdog stopped the worker.
  The automatic cap without a battery is 75 % of the GPU's enforced limit:
  263 W for GPU0 (350 W limit), 293 W for GPU1 (390 W).
- **Device selection.** `offload_device: auto` prefers a GPU that isn't the
  compositor's, then the least utilised. Before, it was always the first
  NVIDIA node.
- **Real stop reason.** The supervisor's reason ("power/temperature watchdog")
  is reported instead of "renderer transport EOF/truncation".

The raw JSON in `experiment-350w/` comes from the first runs: fake 300 W mains
supply, uncommitted 350 W watchdog threshold, IPC 0.10 client.

## Results with the real 0.18 policy (no workarounds)

These are medians, at IPC 0.18. Every run was active 1.0 with offload on and
delivered 60.0 FPS. The JSON in this directory also checks the 0.15 projection
in each run (cap 100, no device), under the wire audit.

| Output | Topology (compositor → offload GPU) | Device / selection / cap | Off: compositor GPU ms | On: compositor GPU ms | On: transfer ms | On: worker render GPU ms | Producer ready ms (p95) | GPU util / W / max °C |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1080p | GPU0 → GPU0 (only GPU visible) | 65:00.0, auto, 263 W device | 4.40 | 0.23 | 0.73 | 1.43 | 7.4 (10.2) | GPU0 25% / 92 W / 35 |
| 1080p | GPU1 → GPU0 (auto avoids the compositor's GPU) | 65:00.0, auto, 263 W device | 4.35 | 0.76 | 1.45 | 3.62 | 10.6 (11.9) | GPU0 35% / 56 W, GPU1 34% / 52 W |
| 1080p | GPU1 → GPU1 (explicit `0000:b3:00.0`) | b3:00.0, explicit, 293 W device | 4.36 | 0.19 | 1.47 | 1.94 | 8.7 (9.8) | GPU1 38% / 102 W / 38 |
| 4K | GPU0 → GPU0 | 65:00.0, auto, 263 W device | 11.98 | 0.37 | 2.93 | 3.82 | 12.3 (16.7) | GPU0 72% / 197 W / 39 |
| 4K | GPU1 → GPU0 | 65:00.0, auto, 263 W device | 11.88 | 0.37 | 2.93 | 3.74 | 12.5 (14.2) | GPU0 41% / 184 W, GPU1 55% / 168 W |

The compositor GPU ms is the sum of `gpuPassMs`. With offload off, the
in-process renderer uses `render_scale: auto` (0.5 here); the worker renders
at 1.0. With offload off, frames are damage-driven (about 7.5 FPS at the
orbit's IPC rate), so compare GPU costs, not frame intervals.

What the numbers show:

- At 1080p, cross-GPU doubles the transfer (0.73 → 1.45 ms) and the worker's
  render time.
- At 4K, cross-GPU costs the same as one GPU and lowers the p95 producer
  latency (16.7 → 14.2 ms), because GPU0 no longer composites.
- The explicit same-GPU run on GPU1 keeps a 1.47 ms transfer even though
  compositor and worker share the GPU. So in the both-GPUs-visible topology,
  the transfer cost isn't simply cross-device.
- `discreteDevices()` still lists NVIDIA nodes only (NVML telemetry is required).
