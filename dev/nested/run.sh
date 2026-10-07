#!/usr/bin/env bash
set -euo pipefail
exec flock /tmp/worldshell-nested.lock python3 "$(dirname "$(realpath "$0")")/session.py" "$@"
