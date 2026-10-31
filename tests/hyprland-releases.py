#!/usr/bin/env python3
"""The release matrix (dev/release/hyprland.json) builds exactly one prebuilt per Hyprland
release CMake supports, and this build's Hyprland is one of them.

  hyprland-releases.py SOURCE_DIR "0.56;0.57" BUILD_HYPRLAND_VERSION
"""
import json, pathlib, re, sys

root, supported, built = pathlib.Path(sys.argv[1]), sys.argv[2].split(';'), sys.argv[3]
release = lambda version: '.'.join(version.split('.')[:2])
builds = json.loads((root / 'dev/release/hyprland.json').read_text())['builds']
minors = [release(b['hyprland']) for b in builds]
assert sorted(minors) == sorted(supported), f'release matrix {minors} != CMake supported {supported}'
assert len(set(minors)) == len(minors), f'two prebuilts for one release: {minors}'
for b in builds:
    assert re.fullmatch(r'\d+\.\d+\.\d+', b['hyprland']), b
    assert re.fullmatch(r'\d{4}/\d{2}/\d{2}', b['archSnapshot']), b
    assert b['archPackage'].startswith(f'hyprland-{b["hyprland"]}-'), b
assert release(built) in supported, f'built against Hyprland {built}, supported {supported}'
print('release matrix:', ', '.join(b['hyprland'] for b in builds), '| this build:', built)
