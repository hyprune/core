# Live tooling dry-run evidence

Nine pure selection/ABI/PID/recovery tests and 20 real nested checks pass.
Run: `python3 dev/live/test_live.py` and `./dev/nested/run.sh --live`.
The latter holds `flock /tmp/worldshell-nested.lock` for its entire lifetime.
All preflight/up/status calls use explicit nested `--instance`; TTY down tests
remove Wayland variables and recover the recorded nested socket. No real-session
command was issued.

The old plugin is an inert API-compatible fixture, not the owner's prototype.
Old HUD and `qs -c rick` are isolated minimal QML fixtures. Lumen Reach, core,
hypruned and the Hyprune shell are real. Proof includes healthy down, distinct
load paths across two ups, helper SIGKILL, restored HUD/plugin, preserved main
shell/native bind, and repeated down. The reserved N key causes selection of
**Super+Shift+F10**; live up chooses and reports its own free chord.

The supervised live cursor/real old-prototype behavior remains for the owner.
Logs contain no launch tokens or saved process environments.
