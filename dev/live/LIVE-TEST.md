# Supervised M3B live test

**Owner + supervisor run live commands; agents use nested only. Keep a TTY ready.**
No installed config is edited. Preflight builds Release and queries the session
matching `WAYLAND_DISPLAY` in `hyprctl instances -j`, ignoring stale signatures.

```bash
cd /home/rick/workspace/hyprune/core
dev/live/preflight
dev/live/up                 # Note the printed toggle and Recovery state path.
dev/live/status
```

`up` records plugin paths and old HUD argv/environment/cwd/PID identities in
`$XDG_STATE_HOME/hyprune/live/state.json` (default `~/.local/state/hyprune/live/state.json`).
It unloads hypr3d, stops only `qs -p .../quickshell/hypr3d`, and preserves
`qs -c rick`/other instances. Core loads from a unique copy, starts its helper
and detached IPC 0.6 shell, then activates an unchanged copy of Lumen Reach.
Tokens stay in memory/pipes; logs are `launcher.log`, `hypruned.log`, `shell.log`.
Default input/editor/layout state belongs to this test's private runtime directory;
Settings edits only that copy. Bootstrap environment is restored before init returns.

Toggle: **Super+Shift+H**, otherwise N/F10/F11/F8 with those modifiers; up checks
binds after load and prints its choice. It conservatively reserves matching keys
across modifiers because Hyprland's bind JSON omits ignore-mods flags.
Hyprland itself **re-evaluates Lua on plugin load/unload**; scripts issue no reload
command. If hypr3d reappears, up rolls back. Avoid manual reload during testing:
it removes runtime binds.

Walking checklist ([full M3B detail](../../docs/M3B.md#supervised-live-test-checklist)):

- [ ] Desktop → world: native cursor hides, reticle appears; main shell stays alive.
- [ ] Super+Enter opens a terminal in view, clear of geometry; it survives toggling out/back and appears in Workspaces.
- [ ] Back in world → real cursor: click launcher/bar and its popups; over the world, hidden desktops receive no pointer input. Back/Super+Tab returns to reticle.
- [ ] E window: stationary camera, relative cursor, hover/click/scroll/typing reach root and popup.
- [ ] Cursor off world output: deferred warp/show gives free native cursor; returning hides it and restores relative pointer.
- [ ] E → Super+Tab/Back: release keys/buttons/focus, restore reticle. Repeat from neighbouring output.
- [ ] F window: cursor hidden through zoom, visible/centred at 2D handover; Super+Tab/Back restores view/reticle.
- [ ] E wall: stationary camera, deferred workspace/focus switch, relative clicks/typing. Test both exits.
- [ ] F wall: hidden through fly-in, visible at handover; exits fly back to saved camera.
- [ ] Super+Tab never opens native overview in E/F, including release; it does in world/inactive. Unrelated binds work in every mode.
- [ ] TAB hold → Map: release grabs, show cursor/focus Director after frame. Settings captures chords/buttons/wheel; Esc cancels/leaves menu, clears focus, restores reticle.
- [ ] Workspaces lists real desktops/apps even without configured homes. Click Help; alternate Help/Workspaces/Settings several times and keep the menu open.
- [ ] Q scope/vignette, tool/submodes, lock and gizmo: overlays only, no cursor shape/visibility changes. Try carry/resize and Shift snap.
- [ ] Reload test input during hold/grab: balanced releases; settings-only edits preserve hold. Esc leaves Settings.
- [ ] Lock/unlock: clear owned input, restore cursor policy, resume safely.
- [ ] Toggle off/on: restore focus/workspace/visibility; native binds work while inactive. U before/after suspect behavior; save notice path.
- [ ] If failed load/output loss/helper death occurs, check release/visibility recovery; do not induce a compositor crash.
- [ ] Down: deferred exit/panic visibility reset precedes unload; old plugin/HUD return, toggle disappears, overview works. Second down creates no HUD or cursor transition.

```bash
dev/live/status
dev/live/down
# Panic from TTY, without Wayland variables (safe to repeat):
/home/rick/workspace/hyprune/core/dev/live/down
```

**Crash:** switch to TTY, run down. It stops recorded test processes, keeps state
and reports recovery-pending when no compositor is available. Restart Hyprland
normally; run down again. It finds the recorded socket or its replacement with
the same Wayland socket. If ambiguous, inspect `hyprctl instances -j` and add
`--instance SIGNATURE`. Run as owner, never sudo. For custom XDG state locations,
add `--state-dir /absolute/path/to/hyprune/live` (Recovery state's parent).

**Capture:** U dumps in `$XDG_STATE_HOME/hyprune/captures/`, live logs/state, and
`~/.cache/hyprland/hyprlandCrashReport*.txt` (or `$XDG_CACHE_HOME/hyprland`). Status
also capture `~/.cache/quickshell/crashes/` and `coredumpctl info PID` for a shell crash. Status
shows each log's last 20 lines and new/updated reports since up. Include failed
tick, time, output, chord and preflight ABI output.

Nested proof: `./dev/nested/run.sh --live`, under lifetime
`flock /tmp/worldshell-nested.lock`, uses explicit overrides and inert old-plugin/
isolated HUD fixtures; checks healthy down, helper SIGKILL, TTY and repeated down.
