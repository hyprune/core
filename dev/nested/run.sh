#!/usr/bin/env bash
set -euo pipefail
here="$(dirname "$(realpath "$0")")"
# No parent session (headless host, SSH) or explicit isolation: run inside a
# private headless parent with its own runtime dir, lock and state root.
if [[ -z "${WAYLAND_DISPLAY:-}" || "${HYPRUNE_NESTED_ISOLATE:-0}" == 1 ]]; then
    exec python3 "$here/isolated.py" "$@"
fi
exec flock /tmp/worldshell-nested.lock python3 "$here/session.py" "$@"
