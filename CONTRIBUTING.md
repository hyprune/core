# Contributing to Hyprune core

Thanks for helping. Core is the Hyprland plugin (`hyprune-core.so`), the
`hypruned` daemon and the renderer. Organisation-wide guidance (code of conduct,
security reports, licensing of world content) is at
[hyprune.com/contributing](https://hyprune.com/contributing/).

## Workflow

1. Fork `hyprune/core` and clone your fork next to its siblings, so relative paths
   resolve: `schema/`, `worlds/` (for packaged worlds) and `docs/` beside `core/`.
2. Create a topic branch. Keep commits small and logical, with messages that say
   what changed and why.
3. Build and test (below), then open a pull request against `main`. Describe the
   user-visible problem, how you tested it (CTest, which nested modes, any live
   session) and what you could not test.

There is no CLA; contributions are licensed under the repository's terms (MIT for
source, CC0 for original procedural fixtures). Contribute only assets,
screenshots, textures, meshes or branding you have the right to license here.
Add a `Co-Authored-By:` trailer only for people who actually co-wrote the change
and agreed to it.

## Contracts

Read the `hyprune/docs` RFCs (start with 0002–0004) and the versioned
`hyprune/schema` contracts before changing a boundary. Public negotiated methods
must agree with their schema and be advertised only when implemented; never
silently extend a protocol version. A contract change goes through an RFC
amendment and a schema version first; record the schema commit you built
against. `docs/CHANGELOG.md` summarises protocol versions for integrators.

## Code

Use C++23 and the repository clang-format configuration. Keep Hyprland headers
inside `src/platform/hyprland`; renderer, simulation, world and public IPC tests
must run without a compositor. Keep dependencies pinned, with provenance and
licence text (see `third_party/`).

## Build and test

```sh
for f in materials climbing m2 m3a m3b visibility generate; do python3 tests/fixtures/$f.py; done
cmake -S . -B build -G Ninja
cmake --build build -j 3
ctest --test-dir build --output-on-failure
dev/nested/run.sh            # smoke test in a nested compositor
dev/nested/run.sh --m1       # one of many modes; see dev/nested/session.py
```

Tests must exercise behaviour (state restoration, framing failures, real Wayland
input), not duplicate the implementation. For render or input changes, inspect
the nested harness's `.state/world.png`. Prose-only changes need no new tests.

## Nested sessions, never your desktop

All automated testing runs in a nested Hyprland. Every nested run holds
`/tmp/worldshell-nested.lock` for its whole lifetime; `dev/nested/run.sh` does
this with `flock`. **Never run bare `hyprctl` against, or load the plugin into,
your real session from a test.** `dev/nested/ctl.py` is the only control and
client-launch entry point; don't bypass its identity checks to make a test pass.
A nested session uses a software cursor, so it cannot establish hardware-cursor
safety: changes to cursor, workspace or focus transitions need the supervised
checklist in `dev/live/LIVE-TEST.md`.

## Render-pass rules

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

## Performance measurements

For material costs, compare the M1 diffuse baseline and the historical M0
renderer in one EGL context: `flock /tmp/worldshell-nested.lock python3
dev/benchmark-materials.py 0` builds the historical renderer in a temporary
directory and reports paired GPU timings (the EGL device index is
machine-specific). Record GL_RENDERER, resolution, sample count and percentiles,
and don't infer Intel or desktop-GPU performance from a laptop GPU measurement.

## Developer settings (`dev.env`)

Machine-specific values never go into the repository. Tools read them from the
environment, or from an optional `~/.config/hyprune/dev.env` (`$XDG_CONFIG_HOME`
if set): one `KEY=VALUE` per line, `#` comments, `~` expanded. An environment
variable wins over the file.

```sh
# ~/.config/hyprune/dev.env
HYPRUNE_GALLERY_DIR=~/review/gallery        # stills and pages for review
HYPRUNE_VIDEO_DIR=~/review/gallery/videos   # finished demo videos
HYPRUNE_BUILD_HOST=buildbox                 # SSH host for dev/remote runs
HYPRUNE_BAKE_HOST=buildbox                  # worlds Blender bakes ("local" on that host)
```

When a gallery or video directory is not configured, the tool prints one warning
naming where its output went instead (under `~/.local/state/hyprune/`).
`dev/devenv.py` prints the effective values. `dev/live` reads its own optional
settings from the same file (see `dev/live/LIVE-TEST.md`).
