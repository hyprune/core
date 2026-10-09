# Core lead handoff — 2026-10-09 (evening)

The IPC recovery acceptance gap is closed and the queued playable-bounds work is
committed. Nothing was pushed. All testing was nested under
`/tmp/worldshell-nested.lock`; the owner's live compositor was never loaded,
dispatched to or restarted. The gallery watcher was not touched. Private
packages were not used: every run below used generic public fixtures.

## Commits (local, unpushed)

| Repo | Commit | What |
| --- | --- | --- |
| core | `5298196` | Nested warp fixture with genuine unzoned mount instances |
| core | `6c1861d` | Deflake render test (GPU sync before checked overlay draws) |
| core | `d853d0c` | World 0.6 playable fly bounds + backdrop card warnings (restored stash) |
| core | `c474ef5` | `dev/live/down` frees per-run world/plugin copies in the runtime tmpfs |
| core | this commit | Handoff and CHANGES notes |
| shell | `e45dbe0` | Recovery harness: generic fixture, unzoned instances, evidence |

Earlier urgent fix: core `cc2dc8a`, schema `d44126a`, sdk `9763829`, docs
`7997a0b`, shell `72a75a9`. Playable contracts: schema `2c1be9b`, sdk `729634c`,
docs `ed06870`; core's pinned 0.6 world/common and 0.15 IPC bytes match schema
`d44126a`. The playable stash `d03bd34…` is now committed as `d853d0c`; the
stash entry is still in the list and can be dropped.

## What closed the acceptance gap

Unzoned instances come only from **saved free-space placements** (tool-5 LMB)
of clients on the **unbound** world workspace 8, located again after a world
load (`Layout::saved` + cleared `spawnAreas`). Recall and free spawn poses do not
survive a world load for unbound clients, so the old fixtures had no instances.
Pointer ownership was not the cause. The fixtures now launch two real clients on
workspace 8, enter, and mount one per world with tool 5 + LMB.

Observation, unchanged: right after a placement the instance reports the spawn
or first area as areaId; after the next world load the same instance omits it.
Both shapes are valid 0.15. Not changed (no new scope).

## Validation (all run green on the final tree)

```sh
cd /home/rick/workspace/hyprune/core
flock /tmp/worldshell-nested.lock ctest --test-dir build --output-on-failure   # 18/18
python3 dev/live/test_live.py           # 16 OK
python3 dev/live/test_credentials.py    # 3 OK
dev/nested/run.sh --playable            # 15 PASS, wire audit 276 frames
dev/nested/run.sh --ipc-warp            # 28 PASS; client 49 responses + 447
                                        # notifications valid 0.15; audit 519 frames
cd /home/rick/workspace/hyprune/shell
python3 -m unittest discover -s tests -q                                  # 100 OK
flock /tmp/worldshell-nested.lock python3 dev/recovery-core.py            # all PASS
```

`run.sh` already holds the flock. The shell recovery ran from a clean worktree of
shell `e45dbe0` against the rebuilt `build/live`: unzoned instances (areaId
omitted, none empty) after both warps, after invalid-frame snapshot recovery and
after both forced disconnects. It also covered renewed credentials and the
restored HUD lease. The injected delta was the only quarantined frame, with no
reconnect through the warps.
Evidence: core `docs/evidence/ipc-warp.json`, `docs/evidence/playable-bounds.json`;
shell `docs/evidence/bridge-recovery.json`.

Render flake: `render` failed about 5 of 12 runs before `6c1861d` (at HEAD
without playable too). Frame pacing re-presents the previous frame while two
fences are pending, so overlay checks read the prior draw. After the fix: 20/20.

`build` (RelWithDebInfo) and `build/live` (Release) are rebuilt at `d853d0c`.

## Runtime tmpfs incident (fixed in tooling)

`/run/user/1000` was 100% full: every `dev/live/up` copied ~140 MB of worlds into
`hyprune-live/<instance>/<run>/` and `down` never removed them. I deleted only
the directories of **dead nested compositors** (61 dirs, 5.6 GB freed). The
owner's live instance directory (`…_1791385985_…`, 346 MB of old runs, state
`down`) was left untouched; the owner may delete its old `*/worlds` dirs.

## Owner-only live retest

Do not run as automated work. The old supervisor lacks renewal; use a fresh
down/up.

```sh
cd /home/rick/workspace/hyprune/core
dev/live/down
dev/live/preflight
dev/live/up
# two-world variant: add --world /path/to/first --world /path/to/second to preflight and up
```

Enter with the printed chord (normally Super+Shift+H). To reproduce the original
bug: place a pocketed window with tool 5 + LMB in free space, warp via the Director
and back, then confirm the HUD stays connected. Panic: `dev/live/down` (works
from a TTY). Capture shell/helper logs if anything fails.

## Remaining gaps

- No live (owner) retest yet; nested cannot cover hardware cursor paths.
- Lumen Reach still uses its tall collision perimeter. To use core playable
  volumes, the world (owned by the worlds agent) needs format 0.6 with `playable`
  volumes. Its 13 footprint rects exceed the 8-volume world-level limit, so
  either merge them into ≤8 boxes/convex hulls, or move from the legacy mounts
  extension to canonical `areas[]` and declare per-area volumes. Keep only low
  walking backstops; walking is still collision-driven.
- Shell Placement sizing is another agent's work (`2eeb691`); not re-shot
  through real core here.
