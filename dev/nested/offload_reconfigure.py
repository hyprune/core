#!/usr/bin/env python3
"""Offload worker under live reconfiguration (nested only): a settings slider dragged.

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/offload_reconfigure.py OUTDIR

With the offload renderer on (two GPUs, like offload_pacing.py), while a pan runs:
  1. 80 graphics.set steps 25 ms apart (target_fps 30-110, the power cap above the GPU's draw
     (a cap below it trips the power watchdog, which ends offload by design), sharpening, the
     scale floor): the worker keeps running (same pid) and frames keep coming from it;
  2. offload off and on three times with target_fps changes between: it comes back each time;
  3. target_fps 33 (below half the refresh) for 8 s with the worker busy
     (HYPRUNE_DEV_WORKER_COST_MS, default here 25:3): no crash, frames from the worker.
Results in OUTDIR/reconfigure.json; any failure raises. HYPRUNE_RECONFIGURE_STEPS (default
"123") runs a subset, e.g. "3".
"""
import argparse, json, os, pathlib, random, shutil, time
import session as s
import npcs
import cinema
from m2 import wait

WORLD = 'com.hyprune.lumen-reach'


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_PACING_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    results = {}
    cam = cinema.Cinema(s)
    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    perf = lambda: client.call('graphics.get', {})['performance']
    x, y, z = -8, 1.7, 12
    pan = [{'t': 0, 'eye': [x, y + .5, z], 'yaw': -.9, 'pitch': -.08},
           {'t': 4, 'eye': [x + 1.2, y + .5, z - 1.2], 'yaw': .9, 'pitch': -.08},
           {'t': 8, 'eye': [x, y + .5, z], 'yaw': -.9, 'pitch': -.08}]

    def draws():
        return dict(s.camera().get('offloadDraws') or {})

    def shown_since(before):
        now = draws()
        return {k: now.get(k, 0) - before.get(k, 0) for k in ('shown', 'stale', 'missing')}

    client.call('graphics.set', {'patch': {'offload': 'on', 'target_fps': 60, 'npc_motion': 'always'}})
    wait(lambda: perf()['activeRenderer'] == 'offload', 90)
    pid = perf().get('pid')
    restarts = (s.camera().get('offloadRequests') or {}).get('count', 0)

    run = os.environ.get('HYPRUNE_RECONFIGURE_STEPS', '123')
    if '1' in run:
        # 1. A slider drag: many live changes, quickly.
        rnd = random.Random(7)
        cam.path(pan)
        before = draws()
        steps = []
        for k in range(80):
            patch = rnd.choice([{'target_fps': rnd.randint(30, 110)}, {'offload_power_cap_w': rnd.randint(200, 300)},
                                {'sharpen': round(rnd.random(), 2)}, {'scale_min': rnd.choice([.375, .5, .625])}])
            client.call('graphics.set', {'patch': patch}); steps.append(patch)
            time.sleep(.025)
        time.sleep(1.5)
        p = perf()
        d = shown_since(before)
        drag = {'steps': len(steps), 'pid': p.get('pid'), 'pidBefore': pid, 'activeRenderer': p['activeRenderer'], 'reason': p.get('reason'),
                'restarts': (s.camera().get('offloadRequests') or {}).get('count', 0) - restarts, 'draws': d}
        results['drag'] = drag
        print('DRAG', json.dumps(drag), flush=True)
        assert p['activeRenderer'] == 'offload' and p.get('pid') == pid and d['shown'] > d['stale'] + d['missing'], drag

    if '2' in run:
        # 2. Off and on, with rate changes between: it comes back every time.
        cycles = []
        for k in range(3):
            client.call('graphics.set', {'patch': {'offload': 'off'}})
            wait(lambda: perf()['activeRenderer'] == 'in-process', 30)
            for fps in (33, 47, 90):
                client.call('graphics.set', {'patch': {'target_fps': fps}}); time.sleep(.05)
            client.call('graphics.set', {'patch': {'offload': 'on'}})
            t0 = time.monotonic()
            ok = False
            try:
                ok = bool(wait(lambda: perf()['activeRenderer'] == 'offload', 60))
            except AssertionError:
                pass
            cycles.append({'back': ok, 'seconds': round(time.monotonic() - t0, 1), 'reason': perf().get('reason')})
        results['cycles'] = cycles
        print('CYCLES', json.dumps(cycles), flush=True)
        assert all(c['back'] for c in cycles), cycles

    if '3' in run:
        # 3. A low target with a busy worker (the renderer's own pacing used to skip a draw in
        # the worker, which ends it).
        client.call('graphics.set', {'patch': {'target_fps': 33}})
        time.sleep(.5)
        pid = perf().get('pid')
        cam.path(pan)
        before = draws()
        time.sleep(8)
        p = perf()
        d = shown_since(before)
        low = {'pid': p.get('pid'), 'pidBefore': pid, 'activeRenderer': p['activeRenderer'], 'reason': p.get('reason'), 'draws': d,
               'pacing': s.camera().get('offloadPacing')}
        results['low'] = low
        print('LOW', json.dumps(low), flush=True)
    (out / 'reconfigure.json').write_text(json.dumps(results, indent=1) + '\n')
    if '3' in run:
        assert p['activeRenderer'] == 'offload' and p.get('pid') == pid and d['shown'] > 0, low
    cam.stop()
    client.call('runtime.exit', {})
    print('PASS offload reconfigure', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    os.environ.setdefault('HYPRUNE_DEV_WORKER_COST_MS', '25:3')
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve()),
                             protocol='0.29'))
