# DRAFT (not committed): IPC 0.16 graphics `upscaler`

Status: draft for the lead. Core implements FSR 1 behind `HYPRUNE_UPSCALER`
today; no contract field exists yet.

## RFC-0003 / RFC-0008 amendment (graphics settings)

New sparse graphics key, settable through `graphics.set` (0.16+), the graphics
file and Lua config:

| Key | Values | Default | Meaning |
| --- | --- | --- | --- |
| `upscaler` | `"fsr1"`, `"bilinear"`, `null` | preset | Reconstruction when the world renders below output size. `fsr1` = AMD FidelityFX FSR 1 (EASU + RCAS) on display-encoded colour; `bilinear` = bilinear + bounded unsharp mask (pre-0.16 behaviour). `null` inherits the preset. |

Presets (proposal): all presets `fsr1`. Fallback: when the GPU lacks GLSL ES 3.1,
core reports `bilinear` as active; `graphics.get().timings.upscaler` already
carries `{requested, active, error}` (timings is extensible, no change needed).

`sharpen` (0..1) keeps its key and range. With `fsr1` it maps to RCAS strength:
`stops = 2 * (1 - sharpen)`, 0 stops = RCAS maximum; with `bilinear` it is the
existing unsharp weight. Performance grouping: `upscale_sharpen` cost now covers
the internal-resolution tone pass + EASU + RCAS when `fsr1` is active.

## Schema (`schema/ipc` 0.16)

- `$defs/graphicsConfig.properties.upscaler`: `{"enum": ["fsr1", "bilinear", null]}`
- `$defs/graphicsPatch.properties.upscaler`: same.
- `graphics.get` `options.upscaler`: `["fsr1", "bilinear"]`; presets carry it.
- Frozen 0.15 and older: unchanged; core omits `upscaler` from responses to
  sessions that negotiated < 0.16 (same mechanism as `outline_quality` in 0.14).
- Version helper + inheritance test (AGENTS.md rule 6).

## SDK

- `GraphicsConfig.upscaler?: 'fsr1' | 'bilinear' | null` in the TS types; mock
  core accepts it for 0.16 sessions.

## CHANGES-FOR-SDK-AND-SHELL.md

- Shell Performance page: add an "Upscaler" row (FSR 1 / Bilinear) to the
  graphics controls, shown for 0.16 sessions; label the cost row "Upscale".
