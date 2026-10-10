# Remote test runner

`dev/remote/run` runs core's automated checks on a headless work host and
prints **one JSON summary on stdout** (progress on stderr). It is meant to be
called over SSH from the lead's laptop, so the laptop can sleep while the host
tests.

```sh
# Synchronous (keep the SSH session open), default suites:
ssh 10.2.2.2 workspace/hyprune/core/dev/remote/run --ref origin/remote/headless-nested

# Detached: returns {runId, unit, summary, poll} at once; survives SSH drops
ssh 10.2.2.2 workspace/hyprune/core/dev/remote/run --detach --ref origin/main ctest placement
ssh 10.2.2.2 workspace/hyprune/core/dev/remote/run --status 20261010-213000-ab12
```

Default suites: `ctest workspace-homes placement playable static-world
live-findings ipc-warp`. Any `dev/nested/run.sh` mode works without its `--`
(`smoke` means plain `run.sh`).

| Option | Meaning |
| --- | --- |
| `--ref REF` | `git fetch origin`, then test `REF` in a detached worktree `../core-remote-<sha10>` (beside `core/`, so `../worlds/dist` and `../schema` resolve). Without it, the current checkout is tested. |
| `--lumen TAG` | Install that hyprune/worlds release (`lumen-reach-v0.9.0`) into `worlds/dist` after a SHA-256 check. Default: what is installed, else the pin in `hyprune/worlds/releases.json`. Switchyard always follows its pin. |
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
<suite>/stills/        live-findings stills (never written into the gallery)
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
