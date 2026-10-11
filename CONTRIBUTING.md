# Contributing

Read the current sibling `hyprune/docs` RFCs 0002, 0003 and 0004 and the
`hyprune/schema/v0` and `v0.2` contracts before changing boundaries. Record the commits used
for interface work. Public negotiated methods must agree with their schema and be
advertised only when implemented. Do not silently extend the version.

Use C++23, the repository clang-format configuration, and small commits. Keep
Hyprland headers inside `platform/hyprland`; renderer, simulation, world and public
IPC tests must run without a compositor. Keep dependencies pinned with provenance
and license text. Original procedural fixture content is CC0; source is MIT. Contribute only assets, screenshots, textures, meshes or branding you have the
right to license here. The old Hypr3D repository is an API/lessons reference, not a source tree to
copy.

Before submitting:

```sh
python3 tests/fixtures/m2.py
python3 tests/fixtures/climbing.py
cmake -S . -B build -G Ninja
cmake --build build -j 3
ctest --test-dir build --output-on-failure
dev/nested/run.sh --m1
node tests/validate-wire.mjs ../schema 0.2
```

For render/input changes, inspect `.state/world.png` from the nested harness.
Use `--m1 --record` when updating demo evidence. Tests must exercise behavior (state
restoration, framing failures, real Wayland input), not duplicate implementation.
Do not add a test suite for prose-only changes.

All nested runs must hold `/tmp/worldshell-nested.lock` for their entire lifetime;
`dev/nested/run.sh` does this with flock. **Never run bare hyprctl or touch the
owner's real compositor.** `dev/nested/ctl.py` is the sole control/client-launch
entry point. Do not bypass its identity checks to fix a failing test. A nested
software cursor does not establish hardware-cursor safety.

Inside `IPassElement::draw`, draw only from prepared state. No cursor changes,
workspace switching, focus calls, snapshot FBOs, window mutation, I/O, package
parsing, worker waits or callbacks that can trigger compositor rendering. Queue
those through `doLater`, verify the render phase has unwound, and make the GL
context current before resource work. Save and restore every touched GL state.
Hold capture texture/geometry together; unsuspend leased hidden windows and pace
frame callbacks; release leases, keys and buttons on all cancellation paths.

On unload, cancel idle callbacks and timers, remove retained pass elements,
unregister listeners/hooks, join workers, then retire resources. Preserve
`-fno-gnu-unique`: GNU-unique symbols can keep a plugin mapped after unload.

Commit locally; the owner reviews and pushes. Requested collaboration commits end
with this trailer:

```
Co-Authored-By: Krombopulos Michael <Krombopulos.Michael@mail99.me>
```

For material costs, compare both the M1 diffuse baseline and the historical M0 renderer in one EGL context. `flock /tmp/worldshell-nested.lock python3 dev/benchmark-materials.py 0` builds the historical renderer in a temporary directory and reports paired GPU timings; the EGL device index is machine-specific. Record GL_RENDERER, resolution, sample count and percentiles. Do not infer Intel or desktop-GPU performance from a laptop GPU measurement.
