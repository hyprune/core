#!/usr/bin/env python3
"""The stand-in workspace screens (tests/fixtures/screens.py) pass hyprune-object-check with the
anchor sizes and mount types the --screens suites expect."""
import json, pathlib, subprocess, sys, tempfile
check, make = sys.argv[1], pathlib.Path(__file__).resolve().parent / 'fixtures/screens.py'
expected = {'workspace-screen-wall-16x9': ([3.2, 1.8], 'hanging-wall', 'wall'),
            'workspace-screen-wall-21x9': ([4.2, 1.8], 'hanging-wall', 'wall'),
            'workspace-screen-stand-16x9': ([3.2, 1.8], 'kiosk-pillar', 'floor'),
            'workspace-screen-stand-21x9': ([4.2, 1.8], 'kiosk-pillar', 'floor')}
with tempfile.TemporaryDirectory() as tmp:
    subprocess.run([sys.executable, str(make), tmp], check=True, stdout=subprocess.DEVNULL)
    assert sorted(p.name for p in pathlib.Path(tmp).iterdir()) == sorted(expected)
    for oid, (size, mount, surface) in expected.items():
        report = json.loads(subprocess.run([check, str(pathlib.Path(tmp) / oid)], capture_output=True, text=True).stdout)
        assert report['state'] == 'ready' and not report['errors'] and report['surfaces'] == [surface], (oid, report)
        anchor = json.loads((pathlib.Path(tmp) / oid / 'object.json').read_text())['traits']['mount']['anchors'][0]
        assert anchor['size'] == size and anchor['mountType'] == mount, (oid, anchor)
print('screen fixtures OK')
