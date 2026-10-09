# Core lead handoff — 2026-10-09

Checkpoint requested by the owner. Stop here; no live retry was performed.
The live compositor is running. All further engineering tests must be nested
under `/tmp/worldshell-nested.lock`. Never load, unload, dispatch to or otherwise
change the owner's live session. Never signal the gallery watcher. Private
packages are local test inputs only: do not publish their identities or content.

## Committed urgent fix

Core **cc2dc8a** checkpoints post-warp schema correction, negotiated outgoing-frame
validation and private credential renewal. The preceding multiworld live launcher
is **0735e4d**. Nothing was pushed.

Live captures identified empty `mounts.instances[].areaId` strings for unzoned
windows. The rejected notification itself was not retained, so its precise
original envelope is unknown. Core now omits absent area IDs. Corrected IPC 0.15
permits omission but still requires nonempty IDs when present. Older protocols
omit unzoned mount-instance metadata rather than violating their frozen schema;
ordinary surface/app lists are unchanged. HUD-style and visibility data were not
the identified malformed fields.

Every outgoing helper IPC frame can be checked against its negotiated canonical
schema, including the originating method for successful replies. Debug enables
this by default; nested sets `HYPRUNE_VALIDATE_IPC=1`; Release leaves it off unless
explicitly enabled. A bounded worker validates off the compositor thread and
aborts the test helper on failure with a redacted method/path diagnostic. Schema
files are pinned locally for offline validation.

The live bootstrap now stays alive and brokers fresh, expiring one-use tokens
over inherited private sockets. `SCM_CREDENTIALS` authenticates the exact launcher
PID/UID/GID; grants are PID-bound and cannot exceed original capabilities. Limits
are 8 requests/s, 4096 per launch and 64 outstanding grants, with a 30 s expiry.
The helper services renewal even while a plugin command is pending. No public
credential method, token files, config edits or cursor transitions were added.
Already-running old supervisors require an owner-run down/up to gain renewal.

Related commits, all local and committed:

| Repo | Urgent fix | Earlier queued bounds contract |
| --- | --- | --- |
| schema | `d44126a` | `2c1be9b` |
| sdk | `9763829` | `729634c` |
| docs | `7997a0b` | `ed06870` |
| shell (separate agent) | `72a75a9`, handoff `5803c57` | — |

Shell independently quarantines malformed complete frames, resnapshots with
bounded backoff, and reconnects using the renewed credential channel. Its schema
pin includes the corrected 0.15 definition. See the shell's `docs/HANDOFF.md` and
`docs/bridge-recovery.md`; core did not edit shell files.

## Validation and remaining acceptance gap

Passed: core CTest **16/16**, live-tooling **15**, credential-broker **3**,
canonical schema **109**, SDK **63 passed / 2 skipped**, docs build/link checks.
Core and `build/live` Release were rebuilt. Python compilation and diff checks
passed at checkpoint. Shell reports **100** unit tests passing.

The initial nested styled/visibility warp passed **310 audited outgoing frames**
and retained authentication. However, its mount-instance array was empty: that
run does **not** prove the unzoned-instance regression. The stronger core harness
now fails `live instances present after warp`. An attempted stronger shell run
also timed out waiting for unzoned instances. These are open acceptance gaps,
not green results. The earlier shell run did pass injected-invalid-delta
resnapshot plus two forced disconnects, fresh credentials and restored HUD lease;
that result predates the final schema repin. Evidence records preserve these
limitations. Local diagnostic logs:

- `/tmp/hyprune-ipc-ctest.log`
- `/tmp/hyprune-ipc-warp-installed.log`
- `/tmp/hyprune-recovery-test.log`

## Exact next steps

1. Complete the stronger nested fixture in `dev/nested/ipc_warp.py`. First verify
   input ownership and a real window on the dedicated world workspace (default
   8). The current harness focuses HEADLESS-1 but does not first move the persistent
   virtual pointer onto it; compare established nested input helpers. This is a
   hypothesis, not a confirmed cause. Explicitly place/pull a pocketed window if
   necessary, and assert a nonempty unzoned instance before and after each warp.
2. Re-run the complete wire-audited regression and shell recovery with the final
   schema pin. Use generic fixtures by default; any external styled package
   remains local. The originally supplied source package had changed and failed
   existing UV validation; its installed snapshot validated. Do not weaken world
   validation to accommodate that unrelated issue.

   ```sh
   cd /home/rick/workspace/hyprune/core
   flock /tmp/worldshell-nested.lock ctest --test-dir build --output-on-failure
   python3 dev/live/test_live.py
   python3 dev/live/test_credentials.py
   dev/nested/run.sh --ipc-warp
   # run.sh already holds the lifetime flock; do not wrap it in another flock.
   # Optional local package: HYPRUNE_WARP_PACKAGE=/path/to/package dev/nested/run.sh --ipc-warp

   cd /home/rick/workspace/hyprune/shell
   HYPRUNE_RECOVERY_WORLD=/path/to/styled-visibility-package \
     flock /tmp/worldshell-nested.lock python3 dev/recovery-core.py
   ```

3. Record actual nonempty-instance acceptance evidence, then finish the queued
   bounds work below. No additional feature work was started after the wrap-up.

## Queued playable bounds and backdrop warning

Core implementation is preserved, built and previously tested, but **not committed
or applied to main**. Stable stash object:

`d03bd3421bd532f2b5e99061de942bcfc1b6343f`

Current label: `stash@{0}`, `playable-bounds: validated implementation paused for
live IPC blocker`. Preserve it until the implementation has been restored and
committed. Restore after the urgent acceptance checks with:

```sh
cd /home/rick/workspace/hyprune/core
git stash apply d03bd3421bd532f2b5e99061de942bcfc1b6343f
```

It implements world-format 0.6 optional per-world/per-area playable volumes
(boxes and convex hulls), max fly height, live `player.fly_bounds_allowance`,
soft fly boundary braking/sliding and recovery, unbounded noclip with one-shot
leaving-bounds notices, and declared backdrop-card CLAMP_TO_EDGE warnings from
`hyprune-world-check`. Walking/climbing behavior remains unchanged. It also
prevents noclip fall-respawn and strips authoring-only bounds metadata from IPC
area records. Schema/SDK/docs contracts are already committed in the table above.

Before stashing: 16 core tests and 15 nested playable checks passed, covering side
and ceiling limits, allowance reload, noclip exit/notices/re-entry, recovery,
walking behavior and legacy Lumen Reach/Switchyard behavior. Re-run after merge;
these results do not cover integration with the urgent fixes.

Expected merge points: CMake (preserve both Playable and WireAudit/tests),
`dev/nested/session.py` (both `--playable` and `--ipc-warp`, validation env),
`src/surfaces/Layout.cpp` (both stripped playable metadata and absent area IDs),
and shared `CHANGES-FOR-SDK-AND-SHELL.md` (keep both sets of notes). Then rebuild,
run full CTest and `dev/nested/run.sh --playable`, review contracts and commit.
No other explicit feature backlog remains from this session.

## Owner-only retry commands

Do not run these as part of automated work. Finish the acceptance gap first.
The shared shell main already contains the recovery commits; no shell fast-forward
is needed here. Other checkouts need those commits and the corrected schema pin
before retrying. Do not push without the owner's instruction.

For the default Lumen Reach session:

```sh
cd /home/rick/workspace/hyprune/core
dev/live/down
dev/live/preflight
dev/live/up
```

For the two-world reproduction, replace the package directory placeholders with
validated local directories (the first is the starting world):

```sh
cd /home/rick/workspace/hyprune/core
dev/live/down
dev/live/preflight --world /path/to/first-package --world /path/to/second-package
dev/live/up --world /path/to/first-package --world /path/to/second-package
```

`up` loads on the desktop and prints/notifies the actual toggle chord (normally
Super+Shift+H); use that chord to enter, then warp through the Director. Capture
shell/helper logs and U dumps if anything fails. Panic/recovery remains
`dev/live/down`, including from a TTY. Never reuse old one-shot token pipes.
