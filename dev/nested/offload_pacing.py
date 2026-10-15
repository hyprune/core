#!/usr/bin/env python3
"""Live pacing of the offload renderer against the in-process one (nested only).

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/offload_pacing.py OUTDIR

World: HYPRUNE_PACING_WORLD (an extracted package; default the worlds checkout's dist Lumen
Reach). For each configuration in HYPRUNE_PACING_CONFIGS (default
"off:60,on:60,off:45,on:45": graphics.offload and graphics.target_fps, optionally a third
field graphics.frame_pacing, e.g. on:45:max_rate; the offload policy caps the worker at
target_fps, so 45 is the laptop-on-USB-C profile; HYPRUNE_NESTED_REFRESH sets the output's
refresh, e.g. 98 like the owner's external panel), a 4 s pan
is driven by core (cinema path, no recording: the live path) while the pacing probe
records every presented frame's draw time, content hash, offload front and age. Over
the steady middle of the pan it reports:
  repeats       frames whose content equals the previous frame's
  extra         repeats beyond what the target rate implies on this output (a 45 FPS
                worker on a 60 Hz output shows 1 frame in 4 twice)
  newHz         new frames per second; presentHz
  frameCV       CV of present-to-present intervals; newCV the same over new frames
  ageMs         mean age of the camera sample shown (latency inside core)
  newAgeStdMs   spread of that age over new frames: motion judder in ms (0 = every new
                frame shows the scene exactly the same time before it is drawn)
Gate (HYPRUNE_PACING_GATE=1): offload extra < 2 % in every round and the median round's
frameCV within 0.05 of in-process.
"""
import argparse, json, math, os, pathlib, shutil, statistics as st, time
import session as s
import npcs
import cinema
from m2 import wait

WORLD = 'com.hyprune.lumen-reach'
results = {}


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_PACING_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    target = installed / WORLD
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(source, target)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def worker_pid():
    """This instance's offload worker on the host (its sandbox reports another pid): a
    hyprune-renderer process with an ancestor in this instance's runtime dir."""
    runtime = os.environ.get('XDG_RUNTIME_DIR', '').encode()
    def env(pid):
        try:
            return pathlib.Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
        except OSError:
            return []
    for proc in pathlib.Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if b'hyprune-renderer' not in (proc / 'cmdline').read_bytes().split(b'\0')[0]:
                continue
        except OSError:
            continue
        pid = int(proc.name)
        while pid > 1:
            if b'XDG_RUNTIME_DIR=' + runtime in env(pid):
                return int(proc.name)
            try:
                pid = int(pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[1])
            except OSError:
                break
    raise RuntimeError('no offload worker for this instance')


def cv(xs):
    m = st.mean(xs)
    return st.pstdev(xs) / m if m else 0.


def even_divisor(refresh, fps):
    """Core's even pacing (offload/Pacing.hpp evenDivisor)."""
    n = 1
    while n < 8 and refresh / n > fps * 1.1:
        n += 1
    return n


def analyse(probe, target, refresh=60., even=False):
    frames = probe['frames']
    t0, t1 = frames[0][0], frames[-1][0]
    mid = [f for f in frames if t0 + 450 <= f[0] <= t1 - 450]
    repeats = sum(1 for a, b in zip(mid, mid[1:]) if a[2] == b[2])
    n = len(mid) - 1
    span = (mid[-1][0] - mid[0][0]) / 1000
    new = [b for a, b in zip(mid, mid[1:]) if a[2] != b[2]]
    # Inherent repeats at the target rate (even pacing: a steady refresh divisor; the offload
    # policy caps the worker at 60 on mains power).
    expected = 1 - 1 / even_divisor(refresh, min(target, 60)) if even else max(0., 1 - min(target, refresh) / refresh)
    presents = [p for p in probe['presents'] if mid[0][0] - 20 <= p <= mid[-1][0] + 20]
    gaps = [b - a for a, b in zip(presents, presents[1:])]
    newGaps = [b[0] - a[0] for a, b in zip(new, new[1:])]
    fronts = [f[1] for f in mid]
    steps = [b - a for a, b in zip(fronts, fronts[1:])] if fronts[0] else []
    return {'frames': n, 'repeats': repeats, 'repeatPct': round(100 * repeats / n, 2),
            'extraPct': round(100 * max(0., repeats / n - expected), 2), 'expectedPct': round(100 * expected, 1),
            'presentHz': round(n / span, 1), 'newHz': round(len(new) / span, 1),
            'frameCV': round(cv(gaps), 3) if gaps else None, 'newCV': round(cv(newGaps), 3) if len(newGaps) > 2 else None,
            'ageMs': round(st.mean(f[3] for f in mid), 1), 'ageP95Ms': round(sorted(f[3] for f in mid)[int(.95 * len(mid))], 1),
            'newAgeStdMs': round(st.pstdev(f[3] for f in new), 2) if len(new) > 2 else None,
            'frontSteps': {str(k): steps.count(k) for k in sorted(set(steps))} if steps else None}


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    cam = cinema.Cinema(s)
    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    perf = lambda: client.call('graphics.get', {})['performance']
    x, y, z = -8, 1.7, 12
    # A pan with a slow dolly: about 100 degrees in 4 s, from rest and back to rest.
    pan = [{'t': 0, 'eye': [x, y + .5, z], 'yaw': -.9, 'pitch': -.08, 'ease': 'in'},
           {'t': 1, 'eye': [x + .3, y + .5, z - .3], 'yaw': -.45, 'pitch': -.08},
           {'t': 3, 'eye': [x + .9, y + .5, z - .9], 'yaw': .45, 'pitch': -.08, 'ease': 'out'},
           {'t': 4, 'eye': [x + 1.2, y + .5, z - 1.2], 'yaw': .9, 'pitch': -.08}]
    configs = [c.split(':') for c in os.environ.get('HYPRUNE_PACING_CONFIGS', 'off:60,on:60,off:45,on:45').split(',')]
    refresh = float(os.environ.get('HYPRUNE_NESTED_REFRESH', '60'))
    for offload, fps, *pacing in configs:
        name = f'offload-{offload}-{fps}' + (f'-{pacing[0]}' if pacing else '')
        client.call('graphics.set', {'patch': {'offload': offload, 'target_fps': int(fps), 'npc_motion': 'always', 'quality': 'high',
                                               'dynamic_resolution': False, 'render_scale': 1,
                                               'frame_pacing': pacing[0] if pacing else None}})
        want = 'offload' if offload != 'off' else 'in-process'
        wait(lambda: perf()['activeRenderer'] == want, 60)
        cam.hold(pan[0]['eye'], yaw=pan[0]['yaw'], pitch=pan[0]['pitch']); time.sleep(2.5)
        rounds = []
        for k in range(int(os.environ.get('HYPRUNE_PACING_ROUNDS', '2'))):
            cam._call({'op': 'probe', 'on': True})
            cam.run(pan); time.sleep(.2)
            probe = cam._call({'op': 'probe', 'on': False})
            (out / f'{name}-{k}.json').write_text(json.dumps(probe))
            a = analyse(probe, int(fps), refresh, offload != 'off' and (pacing or ['even'])[0] == 'even')
            a['renderer'] = perf()['activeRenderer']
            w = perf().get('workerTimings') or {}
            if w:
                a['worker'] = {k: round(w[k], 2) for k in ('renderGpuMs', 'transferGpuMs', 'producerReadyMs') if isinstance(w.get(k), (int, float))}
                a['worker']['scale'] = w.get('renderScale'); a['worker']['msaa'] = w.get('msaaSamples')
            trips = (s.camera().get('offloadClient') or {}).get('trips') or []
            if trips:  # ms after the pose was sent: frame packet in, copy issued, promoted to the front
                a['tripMs'] = [round(st.median(t[i] for t in trips), 1) for i in range(3)]
                a['tripP90Ms'] = [round(sorted(t[i] for t in trips)[int(.9 * len(trips))], 1) for i in range(3)]
                (out / f'{name}-{k}-trips.json').write_text(json.dumps(trips))
                (out / f'{name}-{k}-ticks.json').write_text(json.dumps((s.camera().get('offloadClient') or {}).get('ticks') or []))
            rounds.append(a)
            print('PACING', name, k, json.dumps(a), flush=True)
            cam.hold(pan[0]['eye'], yaw=pan[0]['yaw'], pitch=pan[0]['pitch'], blend_ms=800); time.sleep(1.5)
        if offload != 'off':  # a still view of the floor: no new worker frames, no in-process stand-in
            cam.hold([x, y, z], yaw=0, pitch=-1.3, blend_ms=0); time.sleep(1.5)
            c0 = s.camera(); time.sleep(3); c1 = s.camera()
            ch0, ch1 = c0['offloadClient'].get('changed', {}), c1['offloadClient'].get('changed', {})
            sent = c1['offloadClient']['sequence'] - c0['offloadClient']['sequence']
            draws = {k: c1['offloadDraws'].get(k, 0) - c0['offloadDraws'].get(k, 0) for k in ('shown', 'stale', 'missing')}
            idle = {'posesPerSecond': round(sent / 3, 1), 'draws': draws, 'changed': {k: v - ch0.get(k, 0) for k, v in ch1.items() if v != ch0.get(k, 0)}}
            print('IDLE', name, json.dumps(idle), flush=True)
            rounds.append({'idle': idle})
        results[name] = rounds
    if os.environ.get('HYPRUNE_PACING_FALLBACK') == '1':
        # A worker that stops answering: frames keep coming from the in-process stand-in,
        # then the offload renderer comes back (or stays off with a reason); nothing hangs.
        import signal as sig
        client.call('graphics.set', {'patch': {'offload': 'on', 'target_fps': 60}})
        wait(lambda: perf()['activeRenderer'] == 'offload', 60)
        pid = worker_pid()
        cam._call({'op': 'probe', 'on': True})
        cam.path(pan); time.sleep(1)
        os.kill(pid, sig.SIGSTOP); time.sleep(2)
        try:
            os.kill(pid, sig.SIGCONT)
        except ProcessLookupError:
            pass  # the supervisor already ended the hung worker
        cam.wait(30); time.sleep(.3)
        probe = cam._call({'op': 'probe', 'on': False})
        fr = probe['frames']
        gaps = [b[0] - a[0] for a, b in zip(fr, fr[1:])]
        stood = sum(1 for f in fr if not f[1])
        recovered = wait(lambda: perf()['activeRenderer'] == 'offload' or perf()['reason'] not in ('admitted', 'warming'), 60) and perf()
        fb = {'frames': len(fr), 'maxGapMs': round(max(gaps), 1), 'inProcessFrames': stood,
              'after': {k: recovered.get(k) for k in ('activeRenderer', 'reason', 'status')}}
        results['fallback'] = fb
        print('FALLBACK', json.dumps(fb), flush=True)
    (out / 'pacing.json').write_text(json.dumps(results, indent=1) + '\n')
    cam.stop()
    failed = []
    if os.environ.get('HYPRUNE_PACING_GATE') == '1':
        for fps in sorted({c[1] for c in configs}):
            off, on = results.get(f'offload-off-{fps}'), results.get(f'offload-on-{fps}')
            if not (off and on):
                continue
            extra = max(r['extraPct'] for r in on if 'extraPct' in r)
            # Presentation on the nested headless output jitters with main-thread load (late
            # presents in pairs, in-process too): compare the median round.
            cvs = st.median(r['frameCV'] for r in on if 'frameCV' in r), st.median(r['frameCV'] for r in off)
            ok = extra < 2 and cvs[0] <= cvs[1] + .05
            print(('PASS' if ok else 'FAIL'), f'pacing at {fps}: offload extra repeats {extra} %, frame CV {cvs[0]} vs in-process {cvs[1]}', flush=True)
            if not ok:
                failed.append(fps)
    client.call('runtime.exit', {})
    if failed:
        raise AssertionError(('pacing', failed))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve())))
