#!/usr/bin/env bash
# Build the relocatable release archive for one Hyprland ABI.
#
#   HYPRLAND_VERSION=0.56.2 dev/release/build.sh OUT_DIR [VERSION]
#
# Two builds: the session plugin, and the supervised live plugin used by
# dev/live (HYPRUNE_LIVE_BUILD). Helper paths are compiled for the build tree,
# so callers name them at run time: HYPRUNE_DAEMON for the plugin and
# HYPRUNE_RENDERER for hypruned (dev/live sets both through bootstrap.json).
#
# Archive layout (hyprune-core/):
#   lib/hyprune-core.so          session plugin
#   bin/                         hypruned, hyprune-renderer, hyprune-world-check
#   share/hyprune/input.default.json
#   live/                        dev/live launcher, its build/live binaries and
#                                src/ipc/Protocol.hpp (the protocol inventory)
#   abi.json                     the Hyprland commit/tag/ABI hash built against
#   BUILDINFO.json, LICENSE, licenses/third_party
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=$(realpath -m "${1:?usage: build.sh OUT_DIR [VERSION]}")
want=${HYPRLAND_VERSION:?set HYPRLAND_VERSION, e.g. 0.56.2}
have=$(pkg-config --modversion hyprland)
[[ $have == "$want" ]] || { echo "Hyprland headers are $have, need $want" >&2; exit 1; }
rev=$(git -C "$root" rev-parse HEAD)
plugin=$(sed -n 's/.*return {"hyprune-core", .*"\([0-9.]*\)"};/\1/p' "$root/src/platform/hyprland/Plugin.cpp")
version=${2:-$plugin-g${rev:0:7}}
arch=$(uname -m)
name=hyprune-core-$version-hyprland-$want-$arch
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

build() { # build DIR [cmake args...]
    local dir=$1; shift
    cmake -S "$root" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "$@"
    cmake --build "$dir" -j "$(nproc)"
}
build "$work/session"
build "$work/live" -DHYPRUNE_LIVE_BUILD=ON

pkg=$work/hyprune-core
mkdir -p "$pkg"/{lib,bin,share/hyprune,licenses} "$pkg"/live/{dev/live,src/ipc,build/live}
install -m755 "$work/session/hyprune-core.so" "$pkg/lib/"
install -m755 "$work/session/"{hypruned,hyprune-renderer,hyprune-world-check} "$pkg/bin/"
install -m644 "$root/config/input.default.json" "$pkg/share/hyprune/"
install -m755 "$root"/dev/live/{live.py,credentials.py,daemon,preflight,up,down,status} "$pkg/live/dev/live/"
install -m644 "$root/dev/live/LIVE-TEST.md" "$pkg/live/dev/live/"
install -m644 "$root/dev/devenv.py" "$pkg/live/dev/"
install -m644 "$root/src/ipc/Protocol.hpp" "$pkg/live/src/ipc/"
install -m755 "$work/live/"{hyprune-core.so,hyprune-live-abi} "$pkg/live/build/live/"
for helper in hypruned hyprune-renderer hyprune-world-check; do
    ln -s "../../../bin/$helper" "$pkg/live/build/live/$helper"
done
strip --strip-unneeded "$pkg"/lib/*.so "$pkg"/bin/* "$pkg"/live/build/live/hyprune-core.so "$pkg"/live/build/live/hyprune-live-abi
"$pkg/live/build/live/hyprune-live-abi" > "$pkg/abi.json"
install -m644 "$root/LICENSE" "$pkg/"
cp -r "$root/third_party" "$pkg/licenses/"
find "$pkg/licenses/third_party" -type f ! -iname '*licen*' ! -iname '*copying*' ! -iname '*notice*' -delete
find "$pkg/licenses/third_party" -type d -empty -delete
python3 - "$pkg" "$version" "$rev" "$want" <<'EOF'
import json, subprocess, sys
pkg, version, rev, hyprland = sys.argv[1:]
query = lambda *names: subprocess.run(['pacman', '-Q', *names], capture_output=True, text=True).stdout.split('\n')
packages = dict(line.split() for line in query('hyprland', 'aquamarine', 'hyprutils', 'hyprgraphics', 'hyprcursor', 'hyprlang') if line)
info = dict(version=version, rev=rev, hyprland=hyprland, abi=json.load(open(f'{pkg}/abi.json')), packages=packages)
open(f'{pkg}/BUILDINFO.json', 'w').write(json.dumps(info, indent=2) + '\n')
EOF
mkdir -p "$out"
tar -C "$work" --owner=0 --group=0 --numeric-owner --sort=name -czf "$out/$name.tar.gz" hyprune-core
cp "$pkg/abi.json" "$out/$name.abi.json"
(cd "$out" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
cat "$out/$name.tar.gz.sha256"
