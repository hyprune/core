#!/usr/bin/env python3
"""Nested check of the demo camera driver (hl.plugin.hyprune.cinema) and the recording side.

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/cinema_check.py OUTDIR

World: HYPRUNE_CINEMA_WORLD (an extracted package directory; default the worlds checkout's
dist/com.hyprune.lumen-reach). Records, with the demos' wf-recorder settings at 1080p60:
  1. a 4 s camera path (dolly and pan, eased at both ends);
  2. a 4 s follow shot of the first resident, when the world has residents.
Each recording must pass the smoothness gate (cinema.gate: duplicate frames during
motion < 1 %, step CV < 0.15); the driver's own per-tick trace must show even steps too.
Then stop, toggling out and a world load must each release the camera.
"""
import argparse, json, math, os, pathlib, shutil, subprocess, sys, time
import session as s
import npcs
import cinema
from m2 import wait, keys

WORLD = 'com.hyprune.lumen-reach'
results = {}


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_CINEMA_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    world_id = json.loads((source / 'world.json').read_text())['id']
    results['world'] = world_id
    target = installed / world_id
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(source, target)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def trace_steps(samples, fields):
    """Per-tick steps (eye metres plus yaw/pitch radians at 3 m) and their CV against a 9-tick local mean."""
    i = {k: n for n, k in enumerate(fields)}
    st, dts = [], []
    for a, b in zip(samples, samples[1:]):
        d = math.dist([a[i['x']], a[i['y']], a[i['z']]], [b[i['x']], b[i['y']], b[i['z']]])
        turn = math.remainder(b[i['yaw']] - a[i['yaw']], math.tau)
        d += 3 * math.hypot(turn, b[i['pitch']] - a[i['pitch']])
        st.append(d); dts.append(b[i['dt']])
    moving = [k for k in range(len(st)) if st[k] > 1e-4]
    if len(moving) < 10:
        return {'ticks': len(st), 'movingTicks': len(moving)}
    ratios = []
    for k in moving[4:-4]:
        window = st[max(0, k - 4):k + 5]
        mean = sum(window) / len(window)
        ratios.append(st[k] / mean)
    m = sum(ratios) / len(ratios)
    cv = math.sqrt(sum((r - m) ** 2 for r in ratios) / len(ratios)) / m
    dm = sum(dts) / len(dts)
    dcv = math.sqrt(sum((d - dm) ** 2 for d in dts) / len(dts)) / dm
    held = sum(1 for k in moving[4:-4] if st[k] < .25 * sum(st[max(0, k - 4):k + 5]) / len(st[max(0, k - 4):k + 5]))
    return {'ticks': len(st), 'movingTicks': len(moving), 'stepCV': round(cv, 4), 'heldTicks': held,
            'dtMeanMs': round(dm * 1000, 3), 'dtCV': round(dcv, 4)}


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    cam = cinema.Cinema(s)
    checks = []

    def check(name, ok, detail=''):
        checks.append({'check': name, 'pass': bool(ok), 'detail': detail})
        print(('PASS ' if ok else 'FAIL ') + name + (f': {detail}' if detail else ''), flush=True)
    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    world = results['world']
    client.call('world.load', {'worldId': world})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    offload = os.environ.get('HYPRUNE_CINEMA_OFFLOAD', 'off')
    client.call('graphics.set', {'patch': {'offload': offload, 'npc_motion': 'always', 'quality': 'high'}})
    if offload != 'off':
        wait(lambda: client.call('graphics.get', {})['performance']['activeRenderer'] == 'offload', 60)
    results['offload'] = offload
    check('the hook is registered', cam.status()['mode'] == 'idle')
    manifest = json.loads((s.STATE / 'worlds' / world / 'world.json').read_text())
    spawns = {x['id']: x['pose']['position'] for x in manifest.get('spawns', [])}
    spawn = spawns.get(manifest.get('defaultSpawn'), next(iter(spawns.values()), [0, 0, 0]))
    x, y, z = spawn[0], spawn[1] + 1.7, spawn[2]
    # A dolly with a pan, from rest and back to rest: 7 m and about 70 degrees in 4 s.
    keys_ = [{'t': 0, 'eye': [x - 3, y + .6, z + 2], 'target': [x + 2, y - .5, z - 10], 'ease': 'in'},
             {'t': 2, 'eye': [x, y + 1.2, z + 1], 'target': [x + 6, y - .2, z - 7], 'ease': 'out'},
             {'t': 4, 'eye': [x + 3, y + 1.6, z - 1], 'target': [x + 10, y, z - 2]}]
    cam.hold(keys_[0]['eye'], keys_[0]['target']); time.sleep(3)  # uploads and shader warm-up
    c0 = s.camera(); frames0 = c0['frames']; draws0 = dict(c0.get('offloadDraws') or {})
    clip = out / 'path.mkv'
    with cam.recording(clip):
        cam.path(keys_, trace=True)
        t0 = time.monotonic(); st = cam.wait(30); took = time.monotonic() - t0
        time.sleep(.6)
    c1 = s.camera(); frames = c1['frames'] - frames0
    kinds = [int(p[1]) for p in c1.get('presentLog', [])]
    draws = {k: v - draws0.get(k, 0) for k, v in (c1.get('offloadDraws') or {}).items() if isinstance(v, (int, float))}
    results['presents'] = {'new': kinds.count(0), 'repeat': kinds.count(1), 'idle': kinds.count(2), 'offloadDraws': draws}
    print('INFO presents', json.dumps(results['presents']), flush=True)
    trace = cam.trace()
    (out / 'path-trace.json').write_text(json.dumps(trace))
    ts = trace_steps(trace['samples'], trace['fields'])
    g = cinema.gate(clip)
    results['path'] = {'gate': g, 'trace': ts, 'framesRendered': frames, 'seconds': round(took, 2)}
    check('the path finishes on time', st['done'] and 3.9 < took < 5.0, f'{took:.2f} s')
    check('a moving path forces a frame per present', frames >= 4 * 55, f'{frames} frames in {took:.1f} s')
    check('the driver steps evenly per tick', ts.get('stepCV', 9) < .15 and ts.get('heldTicks', 9) == 0, json.dumps(ts))
    check('the recording passes the smoothness gate', g.get('pass') is True, cinema.gate_summary(g))
    ex = cam.last_export
    results['path']['export'] = ex
    check('the export wrote every presented frame', ex['written'] == ex['grabbed'] == ex['passes'] and not ex['skipped'],
          json.dumps(ex))
    # For comparison only: the same path through the screen-capture client.
    cam.hold(keys_[0]['eye'], keys_[0]['target']); time.sleep(.5)
    clip = out / 'path-wf-recorder.mkv'
    with cam.recording(clip, method='wf-recorder'):
        cam.path(keys_); cam.wait(30); time.sleep(.6)
    results['path']['wfRecorderGate'] = cinema.gate(clip)
    print('INFO wf-recorder on the same path:', cinema.gate_summary(results['path']['wfRecorderGate']), flush=True)
    # A finished path holds: no further frames are forced by the driver.
    agents = s.camera()['npcs']['agents']
    if agents:
        who = agents[0]['id']
        cam.follow(who, blend_ms=0, trace=True); time.sleep(1.5)
        clip = out / 'follow.mkv'
        with cam.recording(clip):
            time.sleep(4)
        st = cam.status()
        trace = cam.trace()
        (out / 'follow-trace.json').write_text(json.dumps(trace))
        ts = trace_steps(trace['samples'], trace['fields'])
        g = cinema.gate(clip)
        results['follow'] = {'subject': who, 'gate': g, 'trace': ts}
        check('a follow shot tracks its subject', st['mode'] == 'follow' and not st['lost'], who)
        check('the follow camera steps evenly per tick', ts.get('stepCV', 9) < .15, json.dumps(ts))
        moved = g.get('pass') is not None
        check('the follow recording passes the gate', g.get('pass') is not False, cinema.gate_summary(g) + ('' if moved else ' (subject standing)'))
    else:
        print('SKIP follow: no residents in', world, flush=True)
    cam.stop()
    check('stop releases the camera', cam.status()['mode'] == 'idle')
    cam.path(keys_)
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: not s.camera()['active'], timeout=30)
    check('leaving the world releases the camera', cam.status()['mode'] == 'idle')
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(1)
    cam.path(keys_)
    client.call('world.load', {'worldId': world}); wait(lambda: cam.status()['mode'] == 'idle', timeout=90)
    check('a world load releases the camera', cam.status()['mode'] == 'idle')
    results['checks'] = checks
    (out / 'cinema-check.json').write_text(json.dumps(results, indent=1) + '\n')
    failed = [c['check'] for c in checks if not c['pass']]
    print(('PASS' if not failed else 'FAIL') + f' cinema check {len(checks) - len(failed)}/{len(checks)}', flush=True)
    client.call('runtime.exit', {})
    if failed:
        raise AssertionError(failed)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve())))
