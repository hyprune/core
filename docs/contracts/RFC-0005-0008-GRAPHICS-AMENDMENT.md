# Proposed RFC-0005 / RFC-0008 amendment: IPC 0.7 graphics controls

Adopted in canonical docs RFC-0005/0008 and schema commit e0739d1.
The shell 36cb85f IPC 0.7 pin is byte-identical to the published schema.

IPC 0.7 inherits IPC 0.6 state, events, grants and methods. It adds:

| Method | Required grant | Parameters | Result |
| --- | --- | --- | --- |
| graphics.get | state.read | `{}` | graphics settings snapshot |
| graphics.set | world.control | `{patch: graphicsPatch}` | graphics settings snapshot |

Core negotiates 0.7 only when offered. Peers offering 0.6 or earlier retain their
frozen projections and method lists, including strict enum validation. Graphics
methods are unavailable to earlier versions even if called without advertisement.
Unauthorised requests receive method-not-advertised, as for existing controls.

A settings snapshot contains `config`, `effective`, `overrides`, `path`, `error`,
`presets`, `options`, `unsupported`, and `timings`. The first is the validated
requested graphics config; the second resolves nullable per-preset effects;
the third is the sparse persisted file layer. The presets and discrete options
are discoverable; all numeric ranges are specified in the companion schema.
Unknown effects, unsupported values and invalid types fail validation before any
write or renderer mutation. Files are private, atomically replaced, and watched
across editor renames. A bad edit retains the last valid config and sets `error`;
a repaired file clears it even when it restores exactly the previous value.

Settings writes go through core, never directly from the shell. No Hyprland
reload is involved. Lua can still provide compositor-level defaults; the user's
independent graphics file overrides them. A missing file key inherits Lua;
a null per-effect key inherits the selected preset. Preset changes retain
explicit effect overrides. No additional public state keys or events are added.
Settings may poll while open; normal HUD consumers need not subscribe.

Graphics controls cannot lower native application or world-overlay resolution.
Core owns world-locked markers/prompts and renders them with the current frame's
camera. Shell owns screen UI. The existing lease-owned `overlay.style` hook is
unchanged. File/IPC changes happen outside rendering; scalar/sampler changes
are diff-applied without world texture re-import. FBO allocation and own shader
rendering remain guarded GL operations; they never trigger compositor rendering.

Diagnostics are explicitly extensible and non-authoritative. GPU measurements
use nonblocking disjoint queries; unavailable values are null. The seven named
cost groups are sky, shared world shading, MSAA resolve, bloom, upscale/sharpen,
native surfaces/geometry, and output/markers. Fused material effects share the
world shading measurement; transparent world materials share the native
composition measurement. Neither group gives independent additive costs for
individual shader switches. `deliveredFPS` counts new frames, excluding repeated presentations.

No SSR, runtime shadow maps or runtime contact-shadow implementation is claimed.
These are reported in `unsupported`; attempts to set fictitious switches fail.
Baked lighting and shadows remain part of world lightmaps.
