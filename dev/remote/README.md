# Remote test runner

`dev/remote/run` runs core's automated checks on a headless work host and
prints **one JSON summary on stdout** (progress on stderr). It is meant to be
called over SSH from a workstation or laptop, so that machine can sleep while
the host tests.

The host is yours to choose: set `HYPRUNE_BUILD_HOST` in the environment or in
`~/.config/hyprune/dev.env` (see CONTRIBUTING.md), then:

```sh
host=$(dev/devenv.py HYPRUNE_BUILD_HOST | cut -d= -f2-)
# Synchronous (keep the SSH session open), default suites:
ssh "$host" workspace/hyprune/core/dev/remote/run --ref origin/main

# Detached: returns {runId, unit, summary, poll} at once; survives SSH drops
ssh "$host" workspace/hyprune/core/dev/remote/run --detach --ref origin/main ctest placement
ssh "$host" workspace/hyprune/core/dev/remote/run --status 20261010-213000-ab12
```

Default suites: `ctest workspace-homes placement playable static-world
live-findings ipc-warp`. Any `dev/nested/run.sh` mode works without its `--`
(`smoke` means plain `run.sh`).

| Option | Meaning |
| --- | --- |
| `--ref REF` | `git fetch origin`, then test `REF` in a detached worktree `../core-remote-<sha10>` (beside `core/`, so `../worlds/dist` and `../schema` resolve). Without it, the current checkout is tested. |
| `--lumen TAG` | Install that hyprune/worlds release (`lumen-reach-v0.9.0`) into `worlds/dist` after a SHA-256 check. Default: what is installed (perhaps a local build), else the pin in `hyprune/worlds/releases.json`. |
| `--pinned` | Replace installed Lumen Reach and Switchyard packages with the published releases pinned in `hyprune/worlds/releases.json` (an unpublished pin keeps the install). The nightly passes it. |
| `--parallel N` | Concurrent suites (default 4). |
| `--no-build` | Skip CMake/Ninja and fixture generation. |
| `--detach` | Run under a transient `systemd-run --user` unit. |
| `--status ID` | Print a run's summary. Exit 0 pass, 1 fail/error, 2 unknown, 3 still running. |

Each nested suite runs as an **isolated instance** (`dev/nested/isolated.py`):

- its own headless parent compositor (`kwin_wayland --virtual`, 3840x2160);
- a short private runtime dir;
- a per-instance lock and state root;
- one GPU, the least-loaded render node, in a `bwrap` device view.

Nothing touches a desktop session, DRM/KMS output, or
`/tmp/worldshell-nested.lock`. Refs that predate the isolated harness (core
`354f29e`) can't run nested suites headless.

## Output

Artifacts go to `~/hyprune-remote-runs/<runId>/` (override with
`HYPRUNE_REMOTE_RUNS`):

```
summary.json           the JSON printed on stdout
<suite>/log.txt        full suite output
<suite>/state/         instance state: instance.json (GPU, load), parent.log,
                       hyprland-*.log, .*-state/ screenshots and transcripts
<suite>/stills/        live-findings stills
evidence/              docs/evidence/*.json the suites rewrote (restored in the tree)
```

`summary.json` fields:

- `runId`, `host`, `ref`, `sha`, `tree`, `started`, `finished`;
- `status`: `pass`, `fail` or `error` (with `error`);
- `worlds`, `gpus`, `passed`, `failed`;
- `results[]`, one per suite: `suite`, `status`, `exitCode`, `durationS`, `log`, `failure` (last error line), `artifacts[]`. CTest results add `tests` and `failedTests`; nested suites add `passLines`, `gpu`, `gpuLoad` and `parent`.

Example (shortened):

```json
{"runId": "20261010-213000-ab12", "status": "fail", "sha": "be91ab1…", "passed": 6, "failed": 1,
 "results": [{"suite": "placement", "status": "pass", "passLines": 37, "durationS": 61.2,
              "gpu": {"node": "renderD129", "pci": "0000:b3:00.0", "name": "NVIDIA GeForce RTX 3090"},
              "gpuLoad": {"00000000:b3:00.0": {"utilMeanPct": 14.1, "memMaxMiB": 1550, "powerMeanW": 76}}}]}
```

## Host requirements

Hyprland/aquamarine matching core's pin, `kwin_wayland` (or a sway that offers
xdg_wm_base v6), `bwrap`, `grim`, `wtype`, `wf-recorder`, `kitty`, `qs`,
`node` and `npm` (`schema/` deps install automatically), `gh` (authenticated,
for world releases), CMake/Ninja. On NVIDIA-only hosts the harness skips the
Mesa EGL pin and preloads `build/nested-nvidia-headless.so` into nested
Hyprland so HEADLESS-1 gets renderable buffers.

## Nightly

`dev/remote/nightly install` copies `nightly` and `run` to
`~/.local/share/hyprune-nightly/` and enables a systemd `--user` timer:
`hyprune-nightly.timer`, daily at 02:00. It's `Persistent=true`, so a missed
run starts after downtime. User lingering must be on (`loginctl enable-linger`)
so the timer fires with nobody logged in.
Re-run `install` after changes merge.

Each run (`nightly run`, or `systemctl --user start hyprune-nightly`):

1. Updates dedicated clones of core, shell, sdk, schema, worlds and hyprune
   to `origin/main` in `~/hyprune-nightly/`. A second worlds clone,
   `worlds-build`, takes the Blender builds.
2. Runs the core runner against that core:
   - CTest first;
   - then every automatable `run.sh` mode in parallel isolated instances
     across the GPUs. live, serve, record and visible are left out. That pass
     uses `--pinned`, so every suite tests the published worlds the meta repo
     pins; `--gizmo` gets that Lumen Reach through `HYPRUNE_GIZMO_WORLD`.
   - then, one at a time, the harness scripts that drive `session.run`.
     The offload power-wrapped scripts and the walkthrough recorder are left out.
3. Runs `npm test` in schema and sdk, and the shell unittests.
4. Worlds:
   - `navmesh-test`;
   - `lumen-build`, `lumen-validate`, `visibility-validate`;
   - `build`, `validate`, `budgets` and `gltf-check` (Switchyard);
   - schema and `hyprune-world-check` against the published Lumen Reach
     release.

   The make targets are expanded with `make -n`. A recursive `$(MAKE)`
   contributes the commands it prints, run in its directory; its own line and
   the `make[N]: Entering/Leaving directory` lines are dropped. Their ssh hop
   (`tools/remote-*.sh`) becomes the same local Blender call, because the runner
   host is expected to be the Blender host.
5. Writes `pages/nightly/<date>.json`, `<date>.html` and `index.html` into a
   static results site (`HYPRUNE_GALLERY_DIR` from the environment or the host's
   `~/.config/hyprune/dev.env`; `HYPRUNE_NIGHTLY_GALLERY` overrides it), plus
   a top-level `pages/nightly.html` link. Unset, it warns and writes under
   `~/.local/state/hyprune/gallery`. It never commits. It keeps the
   last 14 runs: pages, runner artifacts (`~/hyprune-remote-runs/nightly-*`)
   and logs (`~/hyprune-nightly/logs/`).

`nightly status` prints the latest result.
