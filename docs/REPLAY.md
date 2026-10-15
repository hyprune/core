# Input and camera recording / replay

Start and stop with **record.toggle**: **Ctrl+R** in world mode (input version 3; in an
application Ctrl+R stays the application's). Rebind it in input.json or Settings; all four
modes have the action. A recording may start while keys are held (they are recorded as
presses in its first frame), not during a gesture, carry, climb or scope; subsequent
mode/tool transitions are recorded. Stop before changing worlds. World exit, a size or
duration limit or an I/O error stops capture. A notice gives the private file path under
`$XDG_STATE_HOME/hyprune/recordings/` (default `~/.local/state/hyprune/recordings/`),
named `rec-<local time>.jsonl`. The reference shell shows a red REC dot with the elapsed
time while it runs and asks for a label on stop (`recording.label`, IPC 0.29): the file
becomes `rec-<time>-<slug>.jsonl` with a sidecar `.json` holding the label; Enter on an
empty label or Esc keeps the time name. A notice reminds after 5 minutes, then every 10.
F12 remains emergency world exit outside native free-cursor mode.

**Recording does not change the feel** (format 2): motion keeps the live per-frame dt,
the input clock stays live and nothing forces frames, so live motion is identical with
recording on or off. Replay feeds the recorded dt sequence back. Format 1 files (fixed
1/60 s steps) still replay as before. Analysis: `dev/feel/analyze.py` (below).

## Privacy and scope

Files are created **0600**. Unhandled/application key events are replaced with
redacted entries **before** reaching the asynchronous writer queue. Default
recordings cannot type that text back. `includeAppKeys: true` is an explicit
per-recording opt-in: raw key symbols and modifiers can reconstruct typed text.
Movement, configuration, timing and world identity can also be private. Inspect
before sharing. No token, screenshot, app content or app title is recorded.
Input replay never invokes Hyprland keybinds or injects application keyboard
input, even from an opted-in file. Pointer actions can reach the aimed live
application. External application, launcher/layer and compositor timing are not
deterministic fixtures; regression recordings should use known world/app state.
Camera replay dispatches no recorded input, making it suitable for clean tours.

## IPC and CLI

IPC **0.12** inherits climbing's 0.11. All seven methods require `input.control`:

| Method | Parameters |
| --- | --- |
| recording.start | `{includeAppKeys?: false}` |
| recording.stop / recording.status | `{}` |
| recording.mark | `{name: "landing"}` (1–64 characters) |
| replay.start | `{path, mode?: "input" or "camera", smoothingMs?: 0..1000, assertions?: spec}` |
| replay.stop / replay.status | `{}` |

Each returns `{recording, playing, path, frame, mode, failures, error}`; path and
error may be null. Stop is idempotent. Unknown parameters/assertion fields fail.
Existing input files migrate automatically; pre-0.12 clients see their frozen
action list and their edits preserve the hidden recording binding.

The default developer command launches **its own verified nested compositor**,
loads the recorded world and holds `/tmp/worldshell-nested.lock` throughout:

```sh
dev/hyprune-dev replay /path/session.jsonl --assert /path/assertions.json
dev/hyprune-dev replay /path/session.jsonl --camera --smooth-ms 100 \
  --video /tmp/hyprune-tour.mp4
```

An attached, explicitly selected IPC endpoint requires a fresh launch grant
passed through an inherited descriptor, never a token on the command line:

```sh
dev/hyprune-dev record-input start --socket "$HYPRUNE_SOCKET" --token-fd 3
dev/hyprune-dev record-input mark landing --socket "$HYPRUNE_SOCKET" --token-fd 3
dev/hyprune-dev record-input stop --socket "$HYPRUNE_SOCKET" --token-fd 3
```

Each invocation needs its own fresh grant for client ID `org.hyprune.dev` (or
`--client-id` matching the grant), with `input.control` and `state.read`.
Observer connections are refused. No command discovers or mutates a live
session implicitly. Default nested replay packages include Lumen Reach,
Switchyard and the test fixtures; install additional packages in the harness
before replaying their recordings. Video capture is nested-only, encoded in
`/tmp` before publication.

Example assertion file:

```json
{
  "recorded": true,
  "tolerance": 0.002,
  "marks": {
    "landing": {
      "camera": {"position": [1, 2, 3], "yaw": 0},
      "mode": "world",
      "tool": 2
    }
  }
}
```

`recorded` compares every simulation frame with the original state. Named marks
can assert camera position/yaw/pitch/fov, mode, tool, subtype, scope, noclip or
movement, with a per-mark tolerance override. Missing marks and unknown fields
are errors. Differences produce bounded failure messages and a nonzero CLI exit.

## Version 2 JSONL format (IPC 0.29 core)

As version 1 below, with these differences:

- `header`: `version: 2`, `timing: "live"` (no `timestepNs`), and `system`: `monitor`
  (name, width, height, refreshHz, vrr, scale), `gpu` (GL vendor, renderer, version),
  `profile` (effective graphics, renderer, offload policy, worker capabilities) and
  `versions` (core, protocol, world id and version).
- `frame`: one per tick, with `tickNs` (tick start since recording start), `dt` (seconds the
  tick moved by: the live dt) and `timing`: `sections` (CPU ms per tick section), `tickMs`,
  `presents` (backend presentation times since the previous frame, ns since start),
  `refreshMs`, `vrr`, `renderedFrames`, `gpuMs` (last measured GPU time, null if none) and,
  `compositorGpuMs` (the compositor's whole frame on this output, GPU, the latest measured);
  with the offload renderer, `offload` (front sequence, frontAgeMs, readyP90Ms,
  latencyRefreshes, divisor, serviceP90Ms (the worker's sustained per-frame time), the front's
  renderGpuMs, transferGpuMs and renderScale, shown, draw counts).
- `view`: per completed 3D draw, `camera` (the pose shown), `drawNs`, `drawMs` (CPU), `worker`,
  `front` and `ageMs` (from the camera sample to the draw).
- `input`: as before; `ns` is the compositor's receive time. Presses synthesized for keys
  held at the start carry `held: true`.
- `stats`: every ~5 s and on stop (`final: true`): `t`, `windowS`, `presents`, `presentHz`,
  `fps` (new frames per second), `frameMs` (present interval p50/p95/p99/max), `dtMs`
  (mean/std/max), `refreshMs`, `missedVblanks`, `hitches` ([{t, ms}], intervals over 2.5x the
  median and over two refreshes).

Bounds: 128 MiB, 30 minutes (432,000 frames); floats are rounded (poses to 1e-6, timing to
1e-3 ms), about 100 KB/s at 98 Hz with a 500 Hz mouse (about 20 minutes).

## Version 1 JSONL format

Each line is a JSON object with `kind` and monotonic `ns` since recording start.
The file is bounded to 64 MiB / 36,000 simulation frames (ten minutes); lines are
bounded to 256 KiB, and the background writer queue to 4 MiB. Queue/disk failure
is reported; an incomplete file is not accepted for replay.

- `header`: `format: "hyprune.session"`, `version: 1`, `timestepNs: 16666667`,
  `worldId`, SHA-256 `worldDigest` of manifest/collision/visual geometry,
  deterministic `seed`, input/player configuration, initial camera/tool/editor
  state, and `includeAppKeys`. A different installed world is rejected.
- `input`: next simulation `frame` and `event`. Types are `key`, `button`, `axis`
  and `motion`. Key/button/axis carry normalized key, modifiers, down state,
  signed value and input mode; motion carries raw relative dx/dy and mode.
  Redacted entries contain only `{type: "key", redacted: true}`. Motion belongs
  to core's world/virtual-cursor handler; native layer/menu pointer motion is not
  an OS input recording.
- `frame`: contiguous zero-based simulation frame and `state`: full camera pose,
  vertical speed/grounded/eye height, mode, tool/subtype indices, movement,
  noclip and scope. The fixed timestep is exactly 1/60 s; `timestepNs` is its
  integer nanosecond representation.
- `view`: simulation frame, renderer frame counter and the actual rendered
  camera/state for each completed 3D draw (including reused simulation poses).
- `mark`: name, preceding simulation frame and state at that boundary.
- `end`: final simulation-frame count. Missing/truncated ends are errors.

Recording and input replay share the same fixed 60 Hz simulation and hold clock;
inputs are assigned to the next simulation boundary and pass through the normal
keymap/action code. The editor's time-derived path IDs use the recorded seed.
Physics has no random sampling. Config and editor persistence are protected
while replay is active; the original input/player settings are restored on stop.
Input matching never reads the physical cursor during event injection. Normal
focus transitions retain their deferred cursor policy; virtual motion cannot
warp onto another physical output. Real input is suppressed while replay owns
world control, with F12 still available. No hardware-cursor testing was performed.

Camera tours use recorded simulation poses, with optional symmetric triangular
smoothing; yaw interpolation unwraps at ±π. They do not reproduce app contents,
native 2D handovers, or external process/window launches. Input regression uses
the original core/world/config and known client topology; OS and application
responses remain external. Numeric tolerances account for floating-point drift
between compiler/CPU builds.

## Evidence

`tests/replay.cpp` covers private files, default masking/opt-in, corrupted files,
smoothing and assertions. `dev/nested/replay_acceptance.py` records physical
key/button/axis/motion events, tool/scope/menu changes and rendered poses, replays
twice with per-frame assertions, checks camera smoothing and idempotent stop,
and verifies masking at a real terminal's typing boundary. Evidence and a benign
scripted recording live in `docs/evidence/replay/`. The CLI reruns the recording
in a fresh nested compositor. Shader warmup is checked in the same nested run:
effect/preset toggles load or compile no new programs.

## Demo videos

`dev/nested/session_demo.py` turns a recording into a demo video, nested only:

```
dev/nested/run.sh dev/nested/session_demo.py --recording REC.jsonl --package WORLD_PKG --output DIR \
    [--mode input|camera] [--smooth-look MS] [--hud on|off] [--trim START:END] \
    [--speed START:END=FACTOR ...] [--name demo] [--encode-only]
```

The recording's world (the same package: replay checks its digest) loads with
the real shell, and core replays it at frame-exact pacing, one recorded tick per
presented frame, while the cinema export (`cinema.recording`) hands every
presented frame to the encoder. Input mode runs every recorded input again, so
the HUD, tool switches and the Director appear as they did; camera mode replays
the view only (`--smooth-ms` smooths it). A hand-held mouse delivers look motion
unevenly per tick; `--smooth-look MS` spreads it over an MS window first (the
total turn and every other input are kept). Times are seconds from the first
frame or `recording.mark` names. `--trim` keeps a part; `--speed` plays a part
faster (frames dropped evenly) or slower (frames repeated). Output:
`DIR/<name>-1080p60.mp4`, `DIR/gate.json` (the cinema smoothness gate over the
recorded camera's turns: its image measure can't see a walk straight ahead or a
resting hand's drift, so the whole video's figures are kept for reference only) and
`DIR/cut.json` (the frame-to-video mapping); `--encode-only` cuts the kept
`DIR/replay.mkv` again.

`dev/nested/human.py` is a synthetic driver for scripted demos that should look
operated by a person rather than a spline: camera looks with a minimum-jerk
profile, a small overshoot and a correction; walks with gentle starts and stops,
speed variation, path wobble, head bob and glances; idle drift; Fitts-law pointer
moves on curved paths with overshoot and micro-corrections; natural clicks and
typing rhythm. Every take is seeded, so it repeats exactly. `tests/human-driver.py`
(CTest `human-driver`) checks determinism and the motion properties.

`dev/nested/showcase.py` drives a demo through core's real input instead of a camera path,
in two nested stages. `--stage take` plays a scripted story with human timing: keys through
wtype, mouse look from `human.LookHand` (minimum-jerk turns a few percent past the target
with a short settle and a corrective sub-movement, hand tremor and a slow wander, smooth
steering while walking, turns that start with the walk) played by `nested-pointer play HZ
FILE` at 250 Hz with absolute deadlines; each action is aimed from the camera's pose and
core records the take (`recording.start`, a mark where the harness launches a window).
`--stage render` starts from the same state: the home workspace's desktop as a still, the
exported pull-out into the world, then `replay.start` of the take (long ticks split into
1/60 s ones so one tick per presented frame moves evenly, look smoothed over 70 ms) with the
window launched again at its mark. Output: `<name>-1080p60.mp4`, `gate.json` (the cinema gate
over the recorded turns), `cut.json`.
