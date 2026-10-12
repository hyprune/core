# World objects in the offload worker (2026-10-12)

Test host: 2x RTX 3090 (GPU 0 65:00.0 on PCIe x16, GPU 1 b3:00.0 on PCIe x8),
NVIDIA 615.78.08, Hyprland 0.56.2, Lumen Reach 0.10.2 with the starter library
0.1.0. Nested isolated instances with every GPU visible and the compositor on
GPU 0 (`HYPRUNE_NESTED_GPU=all:0`), offload worker on GPU 1, 1920x1080 or
3840x2160, `render_scale` 1, dynamic resolution off.
Harness: `dev/nested/offload_objects.py`.

## Parity (`parity-1080p.json`)

The world-animation clock is pinned (`hl.plugin.hyprune.animation_clock`) and each
offload still must be shown by the offload renderer. Every comparison is identical,
pixel for pixel (largest channel difference 0):

| Case | Compared |
| --- | --- |
| Authored armillary, placed catalog rack and armillary, rack close-up | 3 views x t = 0, 2.75, 7.4 s |
| Rack moved with the tool 4 gizmo while offload runs (worker revisions follow the drag) | 1 view |
| Authored armillary hidden (`objects.hide`) | 1 view |
| M3B fixture cube (a scene node) moved with the tool 4 gizmo while offload runs | 1 view |

Releasing a gizmo drag commits the pose (placement rules, collision, probe light):
that tick is long enough for the worker's frame to go stale, so the local renderer
stands in for a frame; offload resumes within the next frames.

## Frame times (`perf-1080p.json`, `perf-4k.json`)

Slow orbit with the clock running and both armillaries and the rack in view;
15 s per phase, offload off, on, off. Medians:

| Output | Mode | Compositor GPU 0 ms/frame | Worker GPU 1 render ms | Transfer ms | Pose to frame ready ms (p95) | Worker frame shown | Present interval median / p95 ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1080p | in-process | 4.94 | | | | | 16.7 / 16.8 |
| 1080p | offload | 0.97 | 2.19 | 1.47 | 10.4 (16.4) | 96 % | 16.7 / 17.1 |
| 4K | in-process | 4.68 | | | | | 16.7 / 16.8 |
| 4K | offload | 0.64 | 6.51 | 5.87 | 18.9 (24.4) | 74 % | 16.7 / 17.5 |
| 4K, worker on GPU 0 (`perf-4k-worker-gpu0.json`) | offload | 0.43 (GPU 1) | 3.97 | 2.93 | 12.0 | 88 % | |

"Worker frame shown" counts drawn frames whose newest worker frame was younger
than 50 ms; otherwise the local renderer drew the world for that frame
(`offloadDraws` in the camera diagnostics, with the frame age in 10 ms bins). Two
3090s present 60 FPS either way; offload takes the world pass off the compositor
GPU. At 4K the worker on the x8 GPU takes twice as long to copy, so its frames
often reach the 50 ms limit.

Both modes show the same ~130 ms tick about once a second (1.6 % of frames over
40 ms). It is not offload related: a catalog rescan loop that is fixed separately.
`ticks-1080p.json` records it per tool state without polling (presentation gaps).
