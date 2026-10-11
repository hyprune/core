#!/usr/bin/env python3
"""hyprune-npc-check against the generated mannequin and its spec violations."""
import json, pathlib, subprocess, sys, tempfile
check, make = sys.argv[1], pathlib.Path(__file__).resolve().parent / 'fixtures/make-mannequin.py'
cases = {'': [], 'root-motion': ['animates `root`'], 'centimetres': ['centimetres'], 'no-idle': ['no `idle` clip'],
         'extra-bones 25': ['the limit is 40'], 'three-materials': ['3 materials'], 'rotate-x': ['stand along +Y'],
         'five-weights': ['more than 4 weights']}
with tempfile.TemporaryDirectory() as tmp:
    base = pathlib.Path(tmp) / 'base.glb'
    subprocess.run([sys.executable, str(make), str(base)], check=True, stdout=subprocess.DEVNULL)
    for flags, expected in cases.items():
        out = pathlib.Path(tmp) / f'{flags.split()[0] if flags else "ok"}.glb'
        subprocess.run([sys.executable, str(make), str(out)] + (['--' + flags.split()[0]] + flags.split()[1:] if flags else []),
                       check=True, stdout=subprocess.DEVNULL)
        r = subprocess.run([check, str(out), '--json', '--against', str(base)], capture_output=True, text=True)
        report = json.loads(r.stdout)
        errors = [p['message'] for p in report['problems'] if p['level'] == 'error']
        if not expected:
            assert r.returncode == 0 and not errors and report['bones'] == 20 and report['triangles'] == 5400, report
        else:
            assert r.returncode == 1, (flags, report)
            for e in expected:
                assert any(e in m for m in errors), (flags, e, errors)
    bad = pathlib.Path(tmp) / 'not.glb'; bad.write_bytes(b'nope')
    assert subprocess.run([check, str(bad)], capture_output=True).returncode == 1
print('PASS hyprune-npc-check: spec mannequin passes; root motion, centimetres, missing idle, >40 bones, 3 materials, wrong axis, >4 weights fail')
