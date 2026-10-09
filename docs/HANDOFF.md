# Core lead handoff — 2026-10-09 (kits, carry drop)

Nested only and nothing pushed. `build/live` was not rebuilt, the core-render
worktree was not touched, no heavy batch work ran locally (AGENTS.md rule 3).

| Repo | Commit | What |
| --- | --- | --- |
| core | `2c10abd` | Carry release drops into a workspace wall only when the window reached it |
| core | `e70c0a2` | workspace_homes: intentional wall drop still tiles natively (30 checks) |
| core | this commit | CHANGES note, handoff |
| hyprune | `591393e` | Kit compiler: apps/start may target world-defined homes; `[[homes]]` optional |
| hyprune | `6ef30c4` | Kits rely on the world: Lumen Reach/minimal on 21+, dev's Switchyard on 30 |
| hyprune | `80f260d` | README/kits/guide docs: the world defines homes, kits may add explicit ones |

**`--placement` failure: a real bug, fixed.** On release, a carried window
dropped into any workspace wall within 12 m along the pointer ray, even when
the wall was behind the held window. It was tiled natively there, lost its
placement and stopped being drawn. With default homes every area's main wall
is a workspace wall, so the owner would hit this live. The drop now needs the
wall within the held window's depth + 0.5 m. `release_before_wall_keeps_window`
(placement) and the intentional-drop check (workspace_homes) cover both sides.

**Kits** (meta repo, done by a subagent and reviewed):
- Lumen Reach and minimal have no `[[homes]]`. Apps launch on 21–24 with the
  same mounts and start on 21.
- The dev kit keeps one explicit home, Switchyard on 30 (it has no areas). Its
  debug terminal uses Minimal's world-defined 21.
- The standalone starter session binds Super+1…9 to 21…29, as it previously
  bound Super+N to kit homes.
- Meta repo checks: 20 unit tests OK; `nix flake check --no-build --offline`
  passes; all three rendered `session.lua` pass `Hyprland --verify-config`.
  Flake builds were skipped (heavy).

Owner calls still open (kits):
- From the dev kit's Switchyard, workspace 21 does nothing until Minimal is
  loaded. Either add an explicit Minimal home or start the kit in Minimal.
- With Home Manager, the kit's Super+1…9 binds sit alongside the owner's own
  grouped binds.
- Already generated profiles keep the old explicit homes until regenerated.

Validation (final tree):

```sh
flock /tmp/worldshell-nested.lock ctest --test-dir build   # 20/20
dev/nested/run.sh --placement        # 37 PASS lines incl. release_before_wall_keeps_window (was failing at :78)
dev/nested/run.sh --workspace-homes  # 30 checks
dev/nested/run.sh --long-titles      # PASS
dev/nested/run.sh --ipc-warp         # PASS
dev/nested/run.sh --playable         # 15 PASS (one batch run hit a 5 s client timeout on world.load; two reruns clean)
dev/nested/run.sh --m2               # PASS
flock /tmp/worldshell-nested.lock python3 dev/nested/world_workspace.py   # 23 PASS
shell unittest 100 OK; sdk npm test 63 pass / 2 skipped
```

Pre-existing, not investigated: `dev/nested/run.sh --m3a` fails at
`m3a.py:70` (roll check), identically with the drop fix reverted.

---

# Core lead handoff — 2026-10-09 (workspace numbering)

## Room 20, area homes 21+

Owner decision: `windows.world_workspace` defaults to **20** and
`windows.workspace_base` to **20**, so Lumen Reach is 21 sun-court,
22 array-hall, 23 rime-vault, 24 signal-rise. Their Hyprland binds are
group*10+N, so homes share the 21–29 group and the room is in 11–20, by
choice. Explicit Lua config (including the old 8) and explicit binds/kit
homes still win. Nested only, nothing pushed, `build/live` not rebuilt,
core-render untouched.

| Repo | Commit | What |
| --- | --- | --- |
| core | `3ec9bca` | Defaults + fallbacks, unit tests, live text, LIVE-TEST, M3A/M3B docs |
| core | `d905b50` | Nested harnesses on 20 / 21–24 (room test's unrelated ws stays 30) |
| core | this commit | CHANGES note, handoff |
| docs | `5339098` | RFC-0005 default-homes text; generated keymap gains a Workspaces section |
| shell | `7ffda21` | recovery-core / hud_placement_check / diagnostics fixture use room 20 |
| sdk | `2d4d926` | ipc-warp fixture uses room 20 |

The meta repo was not changed. Its kits bind explicit homes (the Lumen Reach
kit uses workspaces 1–4, dev uses 10/11) and never set `world_workspace`, so
they keep their numbers. Moving kits to 21+ is the owner's call.

Validation (final tree):

```sh
flock /tmp/worldshell-nested.lock ctest --test-dir build   # 20/20
dev/nested/run.sh --workspace-homes   # 29 checks (homes 21-24, room 20)
dev/nested/run.sh --long-titles       # PASS, audit 173 frames
dev/nested/run.sh --ipc-warp          # PASS, audit 523 frames
dev/nested/run.sh --playable          # 15 PASS
dev/nested/run.sh --m2                # PASS + IPC 0.4 wire (104/416)
flock /tmp/worldshell-nested.lock python3 dev/nested/world_workspace.py   # 23 PASS
python3 dev/live/test_live.py         # 16 OK (mocks)
shell: python3 -m unittest discover -s tests   # 100 OK
sdk: npm test                         # 63 pass, 2 skipped, 0 fail
docs: npm run build, check:links, check:keymap  # OK
```

Pre-existing, not caused by this change: `dev/nested/run.sh --placement`
fails at `placement.py:78` (aim at the released carried window times out
after 15 passes). The unchanged HEAD `e7d6013` fails identically. Not
investigated.

---

# Core lead handoff — 2026-10-09 (night)

## Follow-ups: schema-bounded strings, m2 harness

Nested only and nothing pushed. `build/live` was not rebuilt, nothing ran
from `dev/live/*`, the core-render worktree and gallery watcher were not
touched.

| Commit | What |
| --- | --- |
| `881e91c` | Clamp every client-supplied projected string to its schema limit (UTF-8 safe) |
| `7b03711` | `dev/nested/run.sh --long-titles` hostile-title wire audit + X11 title fixture |
| `8ee1e8d` | m2 harness fixed (E before typing, 1 s menu hold, OCR Recall) |
| this commit | CHANGES note, handoff |

**1. Invalid frame from a long title (real bug, fixed).** `ipc::boundedText`
(`src/ipc/Text.hpp`) returns valid UTF-8 of at most N code points, counted as
JSON Schema `maxLength` counts them, with U+FFFD for malformed bytes and a
trailing `…`. It is applied at Capture ingress (title 512, class 256) and at
every projection of outside strings (list in CHANGES). Placement rules store
and match the 256 code point form. Last resort: Bridge/Server/daemon/audit
serialization uses `error_handler_t::replace`. Limits are identical across
IPC 0.1–0.15.
The pre-fix plugin, run against the new nested case, reproduced the owner's
exact failure (`/params/changes/surfaces/0/title: maxLength`) and two more
bugs of the same class:
- `keymap/steals/N/description` over 1024. On the owner's hyprlang config
  that is any long `exec` bind colliding with a Hyprune key.
- `json type_error.316`: a malformed-UTF-8 X11 class made every state
  publish throw.
Grep sweep: IPC-client inputs were already byte-bounded and world-package
strings are bounded at load, so the remaining unbounded sources were the
Wayland/X11, Hyprland and file-error strings above.

**2. `--m2` typing failure: a stale harness, not a product bug.** The test
clicked the mounted terminal and typed in movement mode, where keys drive the
player by design: LMB clicks into the aimed window, E enters Use mode
(NOTES.md). With E, keys reach a window on a hidden bound workspace: core
selects its native workspace and routes the keyboard to it. The resulting
`typing` mode and exact typed text were verified nested. Two more stale bits
were fixed: the Director opens on a 1000 ms Tab hold (m2 used 450 ms), and the
Recall button is now found by OCR. `--m2` passes end to end.

Validation on the final tree:

```sh
flock /tmp/worldshell-nested.lock ctest --test-dir build --output-on-failure  # 20/20 (new: wire-text, wire-text-schema)
dev/nested/run.sh --long-titles      # 24 PASS; 0.15 + 0.1/0.3/0.5/0.8/0.11/0.14 clients valid; core audit 172 frames
dev/nested/run.sh --m2               # PASS M2 + IPC 0.4 wire conformance (105 responses, 417 notifications)
dev/nested/run.sh --workspace-homes  # 29 checks
dev/nested/run.sh --ipc-warp         # PASS; audit 525 frames
dev/nested/run.sh --playable         # 15 PASS
flock /tmp/worldshell-nested.lock python3 dev/nested/world_workspace.py   # 23 PASS
```

Owner live retest (after down → preflight → up): the shell log should show
no `protocol.invalid`. Long titles end in `…` in the Director. Typing into a
wall window is E then type; Back/Super+Tab leaves.

The earlier "Known gaps" note that `--m2` fails is superseded by item 2.

---

# Core lead handoff — 2026-10-09 (late)

## Default workspace homes (owner request: "workspaces 11, 12, 13… per area")

Done, nested only, nothing pushed. `build/live` was **not** rebuilt (the owner
was live-testing it); `dev/live/preflight` rebuilds it. No `dev/live/up/down`,
no real-session calls, gallery watcher untouched.

| Repo | Commit | What |
| --- | --- | --- |
| core | `fe29ae5` | Default area homes: `windows.workspace_base` (10) + `workspaceSlot` |
| core | `acd0afe` | `dev/nested/run.sh --workspace-homes` (29 checks); room test uses ws 30 |
| core | `45c7aaa` | m2 nested: two stale assertions fixed (suite still fails, see below) |
| core | this commit | CHANGES note, LIVE-TEST item, handoff |
| docs | `1c7c47c` | RFC-0005 M2 follow-up "world-derived default homes" |

No schema/SDK change: the IPC 0.15 wire shape is unchanged. Defaults look
exactly like explicit bindings in `mounts.bindings` / `workspaces.locations`.
The shell Director already lists `workspaces.locations`, so it needs no change.

How it works: `Layout::bindings` holds explicit (durable) homes, and
`Layout::defaults` is derived (`deriveDefaults`). Runtime calls `deriveHomes()`
on world load, `workspace.bind`, `_layout.restore` and Lua config change.
Explicit wins by area and by ID across worlds; the room (8), the configured
`world_workspace` and `HYPRUNE_WORKSPACE_LOCATIONS` IDs are reserved. The plugin
sends the derived IDs on the private bridge (`derivedBindings`), and hypruned
strips them before writing `layout.json`. Native keybind/dispatcher travel was
already the M2 tick (`workspaceLocation()`, outside the render pass); it did
nothing before only because no workspace was bound. New: an unbound active
workspace (e.g. room 8) clears `resolvedLocation`, so going to the same home
again travels again. No new hooks, cursor or focus calls were added.

Validation on the final tree:

```sh
flock /tmp/worldshell-nested.lock ctest --test-dir build --output-on-failure  # 18/18
dev/nested/run.sh --workspace-homes     # 29 PASS (evidence docs/evidence/workspace-homes.json)
dev/nested/run.sh --ipc-warp            # PASS; 50 responses + 442 notifications valid 0.15; audit 515 frames
dev/nested/run.sh --playable            # 15 PASS
flock /tmp/worldshell-nested.lock python3 dev/nested/world_workspace.py   # 23 PASS
python3 dev/live/test_live.py           # 16 OK (pure mocks)
```

Stills: gallery `screenshots/workspace-homes-2026-10-09/` (sun-court and
array-hall walls show the real `WORKSPACE 11/12` kitty clients).

Known gaps:

- `dev/nested/run.sh --m2` fails: typed input never reaches the mounted
  terminal. The unmodified HEAD plugin (`4c9bb27`) fails identically, so this
  predates this work (last green 2026-10-08 09:24). Not investigated.
- Lumen Reach array-hall arrival framing cuts off the top of its main wall.
  This is for the worlds agent (generous view of screens).
- Director labels homes by raw area ID (`sun-court`); a theme/name label is an
  optional shell tweak.

Owner live retest: down → preflight → up as below. In world, the normal
workspace keybind to 11/12/13/14 travels to sun-court/array-hall/rime-vault/
signal-rise. Windows opened there show on that area's main wall. 8 stays the
free room. Checklist item added to `dev/live/LIVE-TEST.md`.

---

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
