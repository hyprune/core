# Core rendering and interaction review audit

All compositor tests use the verified nested instance under
`flock /tmp/worldshell-nested.lock`. The real session and gallery watcher are
untouched. Failures captured before fixes are in `evidence/review-audit/`.

| Hazard | Confirmation and result |
| --- | --- |
| Tiny resize intervals | 640×480 passes unchanged; logical 80×60 (scaled tiny output) aborts with `_GLIBCXX_ASSERTIONS`. Ordered bounds now respect smaller outputs; pure tests cover both. Nested 640×480 also passes a real Super+RMB client resize. |
| Whole-world GPU upload | Lumen's 54 images / 84 meshes took 211 ms synchronously on the isolated NVIDIA EGL device. Runtime now stages transfer jobs across deferred ticks with nonblocking fences; cancellation and old-world preservation tested. Driver-call time remains a soft bound. |
| Pointer rays | 10,002-triangle dense fixture exceeded the reference cost by 2,063×. Shared BVH now narrows aim, world-surface and collision rays; randomized reference equivalence passes. |
| Capsule scans | Lumen same-process benchmark improves walk step 0.230 → 0.090 ms. BVH candidates preserve original contact order and re-query after correction. |
| Fullscreen owner changes | Nested A → B fullscreen stuck on A before the fix. Owner replacement/closure now clears stale state and preserves the original placement. |
| X11 owner fallback | Two-app X11 test passes before changes: explicit transient owner wins over focused distractor. No speculative change to fallback priority. |
| Attached placement ordering | Nested inventory reproduced dialogs/menus entering root placement first. Classification now precedes placement/configure, with a second transient check before deferred resize; two-app X11 owner, original size and child inventory tests pass. |
| Spawn stacking | Unit test reproduced identical centres after zone clamping. Bounded in-zone fan offsets recheck collision recovery and occupancy. Later surface hits cannot overwrite a nearer obstacle bound. |
| Surface alpha | Pixel test reproduced a transparent client replacing background with black. Native premultiplied composition now passes zero/half-alpha checks and world-glass front/behind ordering. |
| Specular/glossiness diffuse | Fixture reproduced lost diffuse factor/texture. Fallback preserves colour/alpha/texture/UV transforms and approximates roughness from glossiness. Required unsupported extensions still fail explicitly. |

Commands:

```sh
cmake --build build -j4
ctest --test-dir build --output-on-failure
build/spatial-performance ../worlds/dist/com.hyprune.lumen-reach
flock /tmp/worldshell-nested.lock python3 dev/nested/review_audit.py
flock /tmp/worldshell-nested.lock env HYPRUNE_AUDIT_CASE=x11 python3 dev/nested/review_audit.py
HYPRUNE_GRAPHICS_PROFILE=1 HYPRUNE_NESTED_4K=1 dev/nested/run.sh --live
```

The legacy terminal smoke failure was stale fixture behavior: obsolete Lua camera
setter, 500 ms TAB and click-to-type. Current IPC positioning, 1150 ms TAB and
E/Super+Tab preserve the exact typing assertion and pass first-session acceptance.
IPC 0.7 matches shell 36cb85f exactly. IPC 0.8 hold timing is core-ready; shell
0.8 negotiation/bridge/ring wiring remains with the shell owner.


Final validation: nine core CTest suites; 156 nested M3B checks, 262 responses
and 1,010 notifications validated against canonical IPC 0.8; first-session smoke;
fullscreen replacement/closure, two-app X11 ownership and attachment ordering;
640×480 client resize; 26 nested live-tooling checks including helper crash and
double teardown. Canonical schema: 86 tests. SDK: 54 passed, 2 skipped. Docs
site builds. Raw frame-rate and GPU/CPU measurements are linked from GRAPHICS.md.
No hardware cursor or real-session behavior is claimed by these nested checks.
