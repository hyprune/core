# Internal-resolution ink: IPC 0.14

Ink now reuses normals encoded into opaque HDR alpha and the existing resolved
depth. An internal RG32UI target packs the 14-bit octahedral normal code with full
float linear depth for a single neighbour fetch. Flat normals bypass decoding.
Sparse edge distances reconstruct thin logical-pixel coverage in a native
composite, which discards empty cells. There is no second geometry render,
extra multisample target or additional MSAA resolve. App surfaces/HUD stay native
and outside the detector. Offload workers include ink before transporting colour.

The first attempt fused composite into tone mapping; it increased native-pass
cost. The shipped sparse composite keeps the HDR/tone shader unchanged. All ink
programs and both quality modes warm behind loading and use the program cache.
No cursor transitions were added.

## Intel PTL measurements

3840×2160 output, fixed 0.5 internal world scale, Low, MSAA 1, offload off. Both
public worlds use identical camera poses and a 2 px authored style for before/after
captures. Standalone tests use an Intel-only EGL device and the nested lock.
The nested runs also exercise the actual compositor/plugin at the gameplay pose.

| World | Previous nested pass | New nested Fast | New nested Smooth |
| --- | ---: | ---: | ---: |
| Lumen Reach | 7.51 ms | 1.10 ms | 1.10 ms |
| Switchyard | 8.22 ms | 1.11 ms | 1.11 ms |

The dedicated ink passes meet the ≤1.5 ms target at this scale, with an 85–86%
reduction from the previous nested measurements. The repeated standalone
historical build measured 6.75/6.69 ms versus about 1.10 ms in the new build.

The normal encoding shares opaque shading, so the dedicated timer is not the
whole incremental cost. Alternating outer GPU queries (100 measured pairs per
tier, reversing order) measured Lumen added medians **1.18/1.17 ms** (Fast/Smooth).
Switchyard initially measured **1.83/1.04 ms**; a repeat without our concurrent
build jobs measured **1.53/1.19 ms**. P95 pair deltas remained 2.2–5.0 ms amid host
scheduling/clock variation. Thus a strict ≤1.5 ms *whole-frame* overhead is not
claimed, nor is this a sustained whole-desktop 60 FPS guarantee. Full-resolution
world rendering and wider/high-density lines cost more than this Low/0.5 case.

Raw timings: [ink-performance.json](evidence/ink-performance.json) and
[ink-nested.json](evidence/ink-nested.json). Matched original-resolution PNGs are
kept locally under `build/ink-evidence/{com.hyprune.lumen-reach,com.hyprune.switchyard}/{before,after}/`;
the evidence records their SHA-256 hashes. Smooth retains authored width, while
Fast is deliberately 1 logical px. Unit tests pin integrated one-pixel coverage,
width versus distance/output scale, coplanar normal seams at 1×/2×/4× MSAA,
offload worker ink, unchanged native app pixels, GL restoration and warmed shaders.

Reproduce (no live session access):

```bash
cmake --build build -j3
flock /tmp/worldshell-nested.lock python3 dev/benchmark-ink.py \
  ../worlds/dist/com.hyprune.lumen-reach ../worlds/dist/com.hyprune.switchyard \
  build/ink-evidence
HYPRUNE_NESTED_4K=1 dev/nested/run.sh --ink
# run.sh owns flock for the lifetime; do not wrap it a second time.
```

## Live settings and persistence

IPC 0.14 adds `outline_quality: "fast" | "smooth" | null`,
`outline_width_px: 1..4 | null`, and `outline_colour: [r,g,b] | null` (each 0..1)
to `graphics.set`. Low/Medium inherit Fast; High/Ultra Smooth. Explicit width
applies in Smooth; Fast's effective width is 1 px. Null clears an override.
User overrides persist in graphics.json across world loads and beat authored
style. Loading completion restores immutable package defaults before applying
overrides, preventing warm-up from accidentally baking them into reset defaults.
A world must still author enabled ink; width/colour settings do not invent it.

`graphics.get` provides requested/effective values and shared ink cost for the
Performance page. `atmosphere.set` remains a transient authoring preview. Shell
0ec998b must switch its width/colour writes to the graphics keys and add the tier
selector using the [handoff](CHANGES-FOR-SDK-AND-SHELL.md). Shell files were not edited.
The live launcher offers the C++ protocol helper's versions, and starts the shell
at the newest common supported version (currently core 0.14 / shell 0.13).

Validation: 13 core CTest groups; 103 schema tests; 59 SDK tests (2 optional daemon
tests skipped); 11 pure live-tooling tests; locked nested ink/persistence/wire
checks; all 51 live-tooling nested checks, including crash recovery and down twice.
Docs build and links pass. Nothing was run against the owner's live session.
