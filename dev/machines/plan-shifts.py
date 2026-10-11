#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pick per-simulator MACHINES_SIM_SHIFT values so demo events land at given seconds after a
recording start (Unix seconds). Prints the HYPRUNE_MACHINES_SIM_ENV JSON for dev/nested/machines.py.

  dev/machines/plan-shifts.py RECORD_AT '{"queue": 20, "deploy": 66, "build": 6, "ci": 55}'
"""
import json, subprocess, sys, pathlib
sims = pathlib.Path(__file__).resolve().parent / 'sims'
record_at = float(sys.argv[1]); want = json.loads(sys.argv[2])  # {"queue": 38, "deploy": 66, "build": 6, "ci": 55}
def timeline(name, start, seconds):
    out = subprocess.check_output([sys.executable, str(sims / f'{name}-sim.py'), '--timeline', str(start), str(start + seconds), '1', '--with-time'], text=True)
    return [json.loads(l) for l in out.splitlines()]
start = record_at + 600
env, notes = {}, {}
def find(name, test, lead):
    rows = timeline(name, start, 7200)
    for i in range(1, len(rows)):
        if test(rows[i - 1], rows[i], rows, i):
            t = rows[i]['_t']; env[name] = {'MACHINES_SIM_SHIFT': str(round(t - (record_at + lead), 1))}; notes[name] = t; return
    raise SystemExit(f'no window for {name}')
if 'queue' in want:   # backlog rising through 150 (peaks ~1-2 min later)
    find('queue', lambda a, b, r, i: a['depth'] < 150 <= b['depth'], want['queue'])
if 'deploy' in want:  # rolling begins and reaches live (not failed)
    find('deploy', lambda a, b, r, i: a['phase'] != 'rolling' and b['phase'] == 'rolling' and any(x['phase'] == 'live' for x in r[i:i + 40]) and not any(x['phase'] == 'failed' for x in r[i:i + 40]), want['deploy'])
if 'build' in want:   # a success burst
    find('build', lambda a, b, r, i: b['successes'] > a['successes'] and r[min(i + 20, len(r) - 1)]['successes'] > b['successes'], want['build'])
if 'ci' in want:      # a failure, then a pass within 20 s
    # a failure after a busy minute (tokens on the belt), then a pass within 20 s
    find('ci', lambda a, b, r, i: i > 60 and b['jobsFailed'] > a['jobsFailed'] and r[min(i + 20, len(r) - 1)]['jobsSucceeded'] > b['jobsSucceeded']
         and b['jobsStarted'] - r[i - 60]['jobsStarted'] >= 8, want['ci'])
print(json.dumps(env))
