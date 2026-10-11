#!/usr/bin/env python3
"""hyprune-npc-check against the generated mannequin and its spec violations."""
import json, pathlib, subprocess, sys, tempfile
check, make = sys.argv[1], pathlib.Path(__file__).resolve().parent / 'fixtures/make-mannequin.py'
cases = {'': [], 'root-motion': ['animates `root`'], 'centimetres': ['centimetres'], 'no-idle': ['no `idle` clip'],
         'extra-bones 50': ['the limit is 65'], 'three-materials': ['4 materials'], 'rotate-x': ['stand along +Y'],
         'five-weights': ['more than 4 weights'], 'mixamo --no-root': ['run tools/npc-import']}
warnings = {'mixamo --fingers': ['performance target'], 'extra-bones 25': ['performance target'], 'one-node': []}
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
    for flags, expected in warnings.items():  # accepted, with warnings
        out = pathlib.Path(tmp) / 'w.glb'; args = flags.split()
        subprocess.run([sys.executable, str(make), str(out)] + ['--' + args[0]] + args[1:],
                       check=True, stdout=subprocess.DEVNULL)
        r = subprocess.run([check, str(out), '--json'], capture_output=True, text=True); report = json.loads(r.stdout)
        found = [p['message'] for p in report['problems'] if p['level'] == 'warning']
        assert r.returncode == 0 and all(any(e in m for m in found) for e in expected), (flags, report)
    bad = pathlib.Path(tmp) / 'not.glb'; bad.write_bytes(b'nope')
    assert subprocess.run([check, str(bad)], capture_output=True).returncode == 1
print('PASS hyprune-npc-check: spec mannequin and Mixamo-named rigs pass; root motion, centimetres, missing idle, >65 bones, 4 materials, wrong axis, >4 weights, Mixamo without root fail; >40 bones warn')
