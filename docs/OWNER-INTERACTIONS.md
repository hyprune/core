# Owner interaction follow-up — IPC 0.15

The input pipeline resolves globally configured physical bindings to four tool
slots, then resolves the selected tool's declaration to an action handler.
Neither handlers nor the HUD need to interpret physical mouse buttons. Null
slots are inert. Super gestures win before tool dispatch; the originating
slot/action owns its release across tool changes. Wheel-bound button slots emit
balanced press/release pairs; key/button-bound Cycle advances once on press.

| Tool | Primary | Secondary | Alternate | Cycle |
| --- | --- | --- | --- | --- |
| Normal | Click/interact | Right-click | Middle-click | Scroll |
| Window | Tag/mount or lock | Lock/unlock | Unassigned | Place/lock |
| Path | Add point | Remove last point | Open/close | Line/curve/arc |
| Position | Select/drag gizmo | World/local axes | Snap toggle | Move/rotate/scale |
| Placement | Place ahead | Mount on surface | Pocket aimed window | Queue selection |

Defaults are LMB/RMB/MMB/Wheel, globally rebindable in input Settings. Tool 5 is
Placement; HUD cycling has no default key. `editor.toolSlots`, `toolDefinitions`
and `toolInput` provide generic labels, actual eligible gesture bindings and
held modifiers. This is a declaration/action boundary for future trusted
extensions, not a new permission for external code to register handlers.

Existing room clients without placements are queued invisibly. Newly opened
clients keep automatic full-size, collision-safe placement when a vacant spot
exists; otherwise they join the queue. Pockets are newest-first, keep clients
alive and keep content sizing. The selected item alone previews as a translucent
non-interactive ghost. Entry selects tool 5 for a nonempty queue, else tool 1.

Carry faces the player by default (`windows.carry_face_player`); R toggles facing
with Super still held. Yaw follows, pitch does not tilt the window. Content scale
(Super+Shift+Wheel) configures real logical client dimensions without changing
quad size, bounded by 320×200 and the output's logical extent (smaller outputs
remain valid). It persists with each placement. Rotation is Super+Ctrl+Wheel;
Super+Shift+MMB resets client sizing, facing and spawn distance when a safe spot
exists. Reset feedback has a new sequence even at 100%.

Back/Super+Tab from use/focus exits directly to world. From world it enters free
cursor; another press returns. Escape with no app keyboard focus returns to
world. The detailed hardware-cursor checklist is the **IPC 0.15** section of
[M3B](M3B.md); it supersedes the older 0.12 entry/return semantics.

`dev/live/up` loads the world and shell while staying on the desktop, then emits
a Hyprland readiness notification with its actual chosen toggle chord. Inactive
shell readiness checks authentication/presentation, not an active-output lease.
Normal plugin load/world readiness also notifies after the frame. For opaque
Lua callbacks without a descriptive `Hyprune toggle` bind, core gives a truthful
“use your configured toggle” hint rather than guessing a chord.

Contracts: RFC-0005 placement content; RFC-0006 input/modes/tools; RFC-0008
core-owned tool labels and modifier context. IPC 0.15 schema and SDK are versioned;
older clients get frozen keymap/tool/placement projections. World format is
unchanged. The generated keymap page is synchronized with input defaults.

## Automated evidence

All compositor runs used `/tmp/worldshell-nested.lock`; no live-session commands
were executed. Main Release/development targets build. Validation:

- Core CTest: 14/14 groups, including newest-protocol inheritance and placement.
- Schema: 107 tests; SDK: 60 passed, 2 skipped; docs keymap/link/build checks pass.
- Live tooling pure tests: 13; nested lifecycle: 53 checks, including inactive
  startup, actual fallback chord, Help/Workspaces, shell restart/reconnect,
  helper crash, TTY recovery and repeated down.
- [Placement evidence](evidence/placement.json): every tool's declared dispatch,
  key-bound Cycle, cross-tool releases, queue/pocket/mount, held-Super facing,
  content sizing/reset/persistence, mode exits, IPC conformance and workspace return.
- [Free-cursor evidence](evidence/free-cursor.json): 20 checks, including both
  outputs, native layers, projected typing, balanced cross-output drag release,
  carry finish, E/F window/workspace exits and restored inactive Super+Tab.

These are software-cursor nested tests; hardware cursor behavior still requires
supervised validation. No gallery watcher was changed and no commits were pushed.
