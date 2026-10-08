# IPC 0.12 launcher and free-cursor hints

Tested core follow-up against shell `d901cdd`. The live launcher offers IPC 0.12
both for its control connection and the detached shell. No real-session command
was issued. Both nested runs used the lifetime `flock /tmp/worldshell-nested.lock`
provided by `dev/nested/run.sh`.

| Command | Result |
| --- | --- |
| `python3 dev/live/test_live.py` | 11 passed, including launcher hello version |
| `cmake --build build -j3` | Built successfully |
| `ctest --test-dir build --output-on-failure` | 13 passed |
| `dev/nested/run.sh --climbing` | 49 passed |
| `dev/nested/run.sh --live` | 50 passed; preflight also built Release |

The renderer regression hides tool and aimed-window hints while retaining the
waypoint and route. The nested climbing check verifies the actual ladder prompt
before Back, only the waypoint while in free cursor, and the restored ladder hint
after returning. Cursor visibility and input routing are unchanged by this patch;
the supervised checklist adds the visual check.

The live-tooling run proves IPC 0.12 negotiation and cursor state at the real
shell bridge, launcher layer clicks, empty-world swallowing, Help/Workspaces,
Quickshell crash recovery, healthy shutdown, unique plugin paths on a second up,
TTY recovery after helper death, restoration of the isolated old plugin/HUD,
preservation of the isolated main shell, and idempotent down. The legacy suite
now follows free-cursor round trips and aims at the carried window's actual pose.

Run-local detailed evidence is in `dev/nested/.state/live/acceptance.json` and
`dev/nested/.climbing-state/acceptance.json`; these scratch paths are replaced by
subsequent runs. The nested collision fixture selected Super+Shift+F10; the live
launcher still prefers Super+Shift+H and reports its available chord.
