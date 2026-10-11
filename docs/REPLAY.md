# Input and camera recording / replay

Start and stop with **record.toggle**, unbound by default. Bind it in input.json
(or Settings on an IPC 0.12 shell), for example `"record.toggle": ["F8"]` in the
world mode. All four modes have the action. Start from idle world control with
no held movement/gesture; subsequent mode/tool transitions are recorded. Stop
before changing worlds. World exit, a size limit or an I/O error stops capture.
A notice gives the private file path under `$XDG_STATE_HOME/hyprune/recordings/`
(default `~/.local/state/hyprune/recordings/`). F12 remains emergency world exit outside native free-cursor mode.

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
