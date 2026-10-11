# Climbing acceptance

Core adds climbable walking activities. World metadata is
format 0.4; shell movement activity and prompts require IPC 0.11. Existing
walk/fly mode IDs and earlier IPC projections remain unchanged. IPC 0.11 inherits
the 0.10 contract and adds only movement activity fields.

Switch the mechanic through the existing local Lua configuration entry point:

```lua
hl.plugin.hyprune.config({movement={climb_mode="grab"}})
-- Alternatives: "look-to-climb", "auto-traverse"
hl.plugin.hyprune.config({graphics={reduced_motion=true}})
```

Grab is the default. Move into the climb face, then W/S, K/J or arrows climb
up/down. Release to hold, strafe across a wide wall, Ctrl to slide, Space to jump
outward. Look-to-climb multiplies forward/back by sine of pitch (level view
holds). Auto-traverse uses E at either end to reach the other end; Space cancels.
All modes catch falling through the prism; automatic mode catches and holds
until E. Top exit and bottom back-off follow collision-checked connectors.
Climbing is disabled in fly/noclip. Input handover, camera teleport, world change,
respawn and desktop exit clear its state. The small visual bob respects reduced
motion. No hand mesh, animation assets or renderer changes are involved.

The RFC amendments (`fa8f192`) and versioned contracts (`569e92d`) are on
docs/schema main. IPC 0.12 inherits climbing 0.11 and adds recording/replay.
Recordings preserve the climbing mechanic configuration; replay start/stop
clears transient climb state.

## Reproduce

```sh
python3 tests/fixtures/climbing.py
cmake --build build -j 3
ctest --test-dir build --output-on-failure
dev/nested/run.sh --climbing
dev/nested/run.sh --climbing --record
(cd ../schema && npm ci)
node tests/validate-climbing-wire.mjs ../schema
```

Both nested commands hold `flock /tmp/worldshell-nested.lock` for the complete
lifetime and use the existing identity-verifying controller. No live-session
commands are issued. The fixture is original procedural CC0: a hatch with a
high drop-in spawn, 10 m rope ladder, 4 m wide chain-link wall and inclined vines.
The immutable world format describes an oriented segment with width/depth,
outward face normal, feet-position exits and optional speed. Endpoints are
capsule validated at load; every runtime motion is swept in 8 mm substeps.

The acceptance suite exercises all three mechanics through real nested seat
input, including short Space taps, W/S, K/J and arrows, fall catch, automatic top
exit, bottom detach, immediate E reversal from the landing, and V/F4 cancellation.
Unit tests additionally cover angled clearance, lateral clamps, reduced motion,
blocked exit, thin-wall sweep, malformed metadata and config, and old IPC
projection. Schema tests include invalid normals, duplicate IDs, degeneracy,
speed/volume bounds, nonfinite values and IPC version isolation.

Recording produces three labelled 36-second sections (about 109 seconds total)
with real nested footage. Encoding and labelling happen in `/tmp`; finished MP4
and string-only JSON sidecar move into
`$HYPRUNE_VIDEO_DIR/hyprune-climbing.*`.

Results and the exact checks are saved under `tests/evidence/climbing/`.

Verified on core `93fa9c0`: 11/11 core suites, 95/95 schema
tests, 46/46 nested climbing checks, docs build/link checks, and full IPC 0.11
response/notification validation. See the evidence logs for exact counts.

Combined with shader warm-up and replay: 13/13 core suites, 46/46 climbing
checks; 260 IPC responses and 758 notifications validated against 0.11.
The combined 0.12 scripted input replay passed twice with per-frame assertions,
camera smoothing, and real-client app-key privacy checks.
