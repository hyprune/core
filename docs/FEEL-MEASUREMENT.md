# Measuring motion feel (October 2026, IPC 0.29 draft)

Live captures on a 3840x2160 external display at 97.97 Hz, with the offload renderer on USB-C
power (worker capped at 45 FPS), felt jagged next to a reference setup that showed fewer frames
(a steady third of the refresh). This note describes the tools that measure motion feel, what
they found and what changed. Nested measurements unless stated otherwise.

## Tools

- **Debug recording (Ctrl+R, recording format 2).** Records input, camera and timing without
  changing motion: every tick's live dt and the presentation times around it, CPU section
  times, GPU time, offload front, age, latency and divisor, every draw's camera and pose age,
  every input event with its receive time, the monitor mode, GPU, profile and versions, and a
  statistics line every 5 s. See [REPLAY.md](REPLAY.md#version-2-jsonl-format-ipc-029-core).
  Recording on and off measured alike (nested 98 Hz, in-process: 97.6 vs 97.9 presents/s,
  present interval deviation 1.27 vs 1.21 ms, yaw step variation 0.290 vs 0.283).
- **`dev/feel/analyze.py REC.jsonl [...] --out DIR`** (pure Python): presents in refresh units
  and missed refreshes, tick dt against the presents around it, the latency from each shown
  frame's content time (the presentation its motion is locked to, or the pose's send time) to
  its display, each shown frame's step relative to its display interval under steady pan and
  walk (stalled, backward, overshoot), the yaw the mouse asked for against the yaw shown (look
  lag), hitches with the CPU sections around them. HTML and JSON, side by side.
- **`dev/feel/video.py CAPTURE.mp4 [...] --out DIR`**: the same motion questions for screen
  captures that keep one video frame per new compositor frame (gpu-screen-recorder content
  mode): phase correlation over a tile grid in the frame centre, fitted as translation plus
  expansion, then each frame's motion relative to its interval and judder against a smooth
  path of time. Decoding 4K is heavy: run it on a work machine.
- **`dev/feel/replay-input REC.jsonl`**: plays a recording's world input (pointer, buttons,
  wheel, world keys) back through a uinput device at its recorded times, with a countdown and
  an abort key, so the identical movement can be captured in two setups. It injects into
  whatever has focus.
- **`dev/nested/feel_record.py`** and **`dev/nested/offload_pacing.py`**: nested harnesses
  (a fixed walk, steady 500 Hz pan and sprint turn recorded with Ctrl+R; the offload pacing
  probe). `HYPRUNE_NESTED_REFRESH=98` runs the headless output at 98 Hz.

## Findings

Live capture, steady pans (video.py): a third of Hyprune's new frames did not move the scene
(34% stalled) and a fifth moved more than twice as far as their display time warranted (20%);
only 16% carried the motion their interval should (within ±25%), against 52% for the
reference. Frame rate was not the problem. Causes, all objective and fixed without a setting:

1. **Offload frame budget counted presents, not refresh intervals.** A compositor presenting
   every second refresh earned half a worker frame per present (a 45 FPS cap at 98 Hz: 0.46),
   so about every other present repeated the worker frame and the next one carried two
   intervals of motion. The budget now counts refresh intervals from presentation times.
2. **Worker frames on an uneven grid with varying latency.** With even pacing (below) every
   worker frame is shown a whole number of refreshes after the present its pose followed.
3. **The compositor re-paced worker frames** on its own `target_fps` divisor, beating against
   the worker's grid (every other new frame shown a refresh late). Worker frames are no longer
   paced twice.
4. **Tick dt came from the event loop's wall clock.** Each frame's motion carried the
   scheduling jitter of the tick (nested 98 Hz: dt deviation 1.9 ms, correlated 0.99 with the
   previous present interval, -0.44 with its own display interval). Ticks that follow a
   presentation now move by the interval between presentation timestamps: whole refreshes on a
   fixed-refresh output, the measured interval under VRR (present-locked dt).
5. **The view bob was missing from worker frames** (the pose carried only the climbing bob).

Nested results at 98 Hz (Lumen Reach, medium, a 500 Hz pointer pan and a walk):

| | Shown frames/s | Pan frames within ±25% of their interval | Walk within ±25% | Overshoot (>2x) | Spread of content-to-display latency |
| --- | --- | --- | --- | --- | --- |
| In-process, before (wall-clock dt) | 96.4 | 64% | 68% | 2.0% | 2.0 ms |
| In-process, present-locked dt | 96.8 | 84% | 85% | 0.2% | 2.0 ms |
| Offload 60, max_rate | 59.3 | 74% | 68% | 4.5% | 5.4 ms |
| Offload 60, even | 48.7 | 92% | 89% | 1.0% | 3.8 ms |

Offload pacing probe at 98 Hz (4 rounds each): even pacing at a 60 FPS cap gives a new-frame
interval variation of 0.06-0.10 (max_rate: 0.44-0.47) and a pose-age spread of 0.6-0.9 ms
(4.4-6.1 ms); at the 45 FPS USB-C cap 0.03-0.13 and 0.4-2.1 ms, at 32.7 FPS. Even pacing costs
about 9 ms of latency (pose age 45 vs 36 ms at 60).

The nested headless output presents every refresh; the live capture showed the compositor
presenting every 2-4 refreshes. That needs a live recording to explain (its per-tick section
times and GPU time say where the time goes).

## Settings (IPC 0.29)

Subjective behaviour is a setting with a default per preset; the objective fixes above are
unconditional. All presets currently share these defaults.

| Setting | Values | Low | Medium | High | Ultra | USB-C offload | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| graphics.frame_pacing | even, max_rate | even | even | even | even | even | null inherits the preset |
| player.look_inertia | 0-0.1 s | 0.03 | 0.03 | 0.03 | 0.03 | 0.03 | 0 = raw mouse (lowest latency); measured look lag 30-32 ms |
| player.move_inertia | 0-0.3 s | 0.05 | 0.05 | 0.05 | 0.05 | 0.05 | acceleration/deceleration of walking |
| player.view_bob_roll | 0-2 degrees | 0.4 | 0.4 | 0.4 | 0.4 | 0.4 | head tilt per stride, on the view bob's envelope |
| player.carry_turn | 0-0.4 s | 0.11 | 0.11 | 0.11 | 0.11 | 0.11 | a carried window turns toward you (critically damped) |
| player.carry_follow | 0-0.3 s | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | the grab point follows the centre ray |

Reduced motion turns the bob, its tilt and the carry easing off.

## Techniques considered

| Technique | Applies? | Status |
| --- | --- | --- |
| Render interpolation of a fixed simulation step | No: motion is a variable step locked to presentation, which needs no interpolation. Interpolation was tried in an earlier round and stalled the view when the step lagged the render. | Present-locked dt instead |
| Variable step matched to the display time | Yes | Done: present-locked dt; with steady presents the step equals the display interval and latency stays constant; a missed refresh is caught up next frame |
| Frame pacing at a refresh divisor | Yes | In-process: `target_fps` below the refresh already paces on a divisor. Offload: even pacing (new, a setting) |
| Low-latency look smoothing | Yes | Exponential look smoothing exists (30 ms); now a setting, 0 = raw |
| Acceleration/deceleration curves on movement | Yes | Exponential intent smoothing exists (50 ms); now a setting |
| Camera spring/lag and roll | Partly | Head tilt on the view bob (setting); the carried window's easing is the "physical object" feel; no camera lag beyond look smoothing (it adds latency) |
| Camera motion blur | Not now | Needs depth and the previous frame's matrices in both renderers and a full-screen pass (about 1-2 ms at 4K on an integrated GPU); motion blur hides low frame rates more than it fixes judder. Candidate setting later |
| Hitch removal | Yes | Shader warm-up and asynchronous uploads exist; recordings now attribute hitches to tick sections; none in the nested runs |
