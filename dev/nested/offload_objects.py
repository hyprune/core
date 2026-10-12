#!/usr/bin/env python3
"""World objects in the offload renderer: pixel parity and frame times.

Nested only (isolated harness on headless hosts); needs two NVIDIA GPUs for the
cross-GPU topology and an admitted power profile. Lumen Reach (HYPRUNE_CATALOG_WORLD,
else the worlds checkout's dist package) with its animated armillary, the starter
library (HYPRUNE_CATALOG_LIBRARY) for catalog placements.

  HYPRUNE_NESTED_GPU=all:0 HYPRUNE_NESTED_ALLOW_DGPU=1 \\
    dev/nested/run.sh dev/nested/offload_objects.py OUT_DIR

OFFLOAD_OBJECTS_STAGE:
  parity (default)  the world-animation clock pinned at fixed times; stills with
                    offload off (compositor GPU) and on (the other GPU) compared per
                    pixel: the authored armillary, a placed catalog rack and armillary,
                    the rack moved with the tool 4 gizmo while offload runs, a hidden
                    authored object, and edited scene-node transforms (the M3B
                    fixture's cube). Writes parity.json, the stills and diff images.
  perf              frame times, offload off then on, with the clock running and the
                    armillaries in view, render scale 1 (HYPRUNE_NESTED_4K=1 for 4K).
                    Writes perf.json. HYPRUNE_MEASURE_DEVICE picks the offload GPU.
"""
import argparse, json, math, os, pathlib, statistics, subprocess, sys, time
import numpy as np
from PIL import Image
import session as s
import catalog
from m2 import wait, keys

WORLD = 'com.hyprune.lumen-reach'
RACK = f'library/{catalog.LIBRARY}/server-rack'
ARMILLARY = f'library/{catalog.LIBRARY}/armillary'
AUTHORED = 'sun-court-armillary'
TIMES = [0.0, 2.75, 7.4]
# A difference of more than this many 8-bit levels in any channel counts as a differing pixel.
LEVELS = 3


def compare(a, b, diff_path):
    x = np.asarray(Image.open(a).convert('RGB'), dtype=np.int16)
    y = np.asarray(Image.open(b).convert('RGB'), dtype=np.int16)
    if x.shape != y.shape:
        return {'error': 'size', 'a': list(x.shape), 'b': list(y.shape)}
    d = np.abs(x - y).max(axis=2)
    mse = float(np.mean((x - y).astype(np.float64) ** 2))
    Image.fromarray(np.clip(d * 16, 0, 255).astype(np.uint8)).save(diff_path)
    return {'maxLevels': int(d.max()), 'meanLevels': round(float(d.mean()), 4),
            'differingFraction': round(float((d > LEVELS).mean()), 6), 'differingPixels': int((d > LEVELS).sum()),
            'psnr': round(10 * math.log10(255 ** 2 / mse), 2) if mse > 0 else None}


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    stage = os.environ.get('OFFLOAD_OBJECTS_STAGE', 'parity')
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)

    def center():
        for _ in range(4):
            xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)

    def click():
        pointer('button', 272, 1); time.sleep(.12); pointer('button', 272, 0); time.sleep(.35)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    perf = lambda: client.call('graphics.get', {})['performance']
    diag = lambda: s.camera()
    instance = lambda id_: next((i for i in state()['objects']['instances'] if i['id'] == id_), None)
    results = {'checks': [], 'stage': stage, 'times': TIMES, 'levels': LEVELS}

    def check(name, ok, data=None):
        results['checks'].append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / f'{stage}.json').write_text(json.dumps(results, indent=2) + '\n')
        assert ok, (name, data)
        print('PASS offload-objects:', name, json.dumps(data)[:400] if data is not None else '', flush=True)

    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})')
        time.sleep(.3)

    def look(pos, target):
        d = [target[i] - pos[i] for i in range(3)]
        camera(pos, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def clock(t):
        s.ctl('eval', 'hl.plugin.hyprune.animation_clock(' + ('nil' if t is None else repr(float(t))) + ')')

    def offload(mode):
        client.call('graphics.set', {'patch': {'offload': mode}})
        if mode == 'off':
            return wait(lambda: perf()['activeRenderer'] == 'in-process', 15)

        def ready():
            p = perf()
            if p['reason'] not in ('off', 'inactive', 'warming', 'admitted', 'ready') and p['activeRenderer'] != 'offload':
                raise AssertionError(p)
            return p if p['activeRenderer'] == 'offload' else None
        try:
            return wait(ready, 30)
        except AssertionError:  # never displayed at a sample: report why
            p = perf()
            raise AssertionError(('offload never displayed', {k: p.get(k) for k in ('status', 'reason', 'pid', 'device')},
                                  diag().get('offloadRequests'), diag().get('offloadClient'), diag().get('offloadDraws')))

    def synced():
        """The displayed offload frame carries the plugin's current object state."""
        w = perf().get('workerTimings') or {}
        return w if w.get('objectsLoading') is False and 'objectsRevision' in w else None

    def still(name, settle=.6):
        time.sleep(settle)
        if name.startswith('offload-') and perf()['activeRenderer'] != 'offload':
            try:
                wait(lambda: perf()['activeRenderer'] == 'offload', 10)
            except AssertionError:
                check(f'{name}: offload is displayed', False, {'performance': {k: perf().get(k) for k in ('status', 'reason', 'pid')},
                                                               'requests': diag().get('offloadRequests'), 'client': diag().get('offloadClient')})
        if perf()['activeRenderer'] == 'offload':
            first = wait(synced, 15)['objectsRevision']
            time.sleep(.25)
            assert perf()['workerTimings']['objectsRevision'] >= first
        path = out / name
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        # Evidence of what was displayed: the renderer, and the compositor's world pass
        # (GPU ms; near zero when the offload frame replaces it).
        t = diag().get('timings', {})
        results.setdefault('stills', []).append({'name': name, 'renderer': perf()['activeRenderer'],
                                                 'compositorWorldPassGpuMs': (t.get('gpuPassMs') or [None])[0]})
        return path

    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: diag()['active'])
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    client.call('graphics.set', {'patch': {'offload': 'off', 'render_scale': 1, 'dynamic_resolution': False}})
    center()
    archive = s.STATE / catalog.ARCHIVE.name
    import hashlib
    client.call('libraries.install', {'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
    arm = instance(AUTHORED)
    check('Lumen Reach has its animated authored armillary', arm and arm['state'] == 'active',
          {'world': state()['world'].get('version') if isinstance(state().get('world'), dict) else None, 'armillary': arm})

    # Catalog placements (offload off): the ghost of tool 5 finds a valid floor spot, then
    # objects.place puts the object there (the same overlay record as a Primary click).
    def place(ref, candidates):
        client.call('objects.pocket', {'ref': ref})
        for pos, yaw, pitch in candidates:
            camera(pos, yaw, pitch)
            wait(lambda: diag()['objects']['ghost']['asset'] >= 0, 30); time.sleep(.3)
            g = diag()['objects']['ghost']
            if g['valid']:
                placed = client.call('objects.place', {'ref': ref, 'pose': {'position': g['position'], 'rotation': g['rotation']}})
                return placed['id'], g['position']
        raise AssertionError(('no spot', ref))
    rack_id, rack_at = place(RACK, [((-8, 1.7, 8), 0, -.35), ((-5, 1.7, 8), 0, -.35), ((-11, 1.7, 8), 0, -.35)])
    arm_id, arm_at = place(ARMILLARY, [((-4, 2.4, 12), math.pi / 2, -.38), ((0, 2.4, 15.2), 0, -.6), ((4, 2.4, 15.2), 0, -.6),
                                       ((-6, 2.4, 12.5), math.pi / 2, -.3), ((10, 2.4, 13), -math.pi / 2, -.38)])
    keys('-k', '1'); time.sleep(.3)
    wait(lambda: instance(rack_id) and instance(rack_id)['state'] == 'active' and instance(arm_id)['state'] == 'active', 20)
    check('a catalog rack and armillary placed', True, {'rack': instance(rack_id), 'armillary': instance(arm_id)})
    ap = arm['pose']['position']
    views = {
        'authored': ([ap[0] + 5.5, ap[1] + 2.5, ap[2] + 6.5], [ap[0], ap[1] + 1.2, ap[2]]),
        'placed': ([arm_at[0] - 7.5, 3.6, arm_at[2] + 3.0], [(arm_at[0] + rack_at[0]) / 2, 1.2, (arm_at[2] + rack_at[2]) / 2]),
        'rack': ([rack_at[0] + 1.2, 2.4, rack_at[2] + 3.4], [rack_at[0], 1.0, rack_at[2]]),
    }

    if stage == 'perf':
        return measure(client, out, results, check, camera, look, views, perf, offload, diag)
    if stage == 'ticks':
        # Tick cost per tool state, both renderers: a tick longer than the offload frame's
        # freshness budget makes the local renderer stand in.
        def sample(label):
            f0 = diag()['timings']['renderedFrames']; t0 = time.monotonic()
            ticks, renderers = [], []
            while time.monotonic() - t0 < 2:
                ticks.append(diag()['timings'].get('cpuUpdateMs')); renderers.append(perf()['activeRenderer']); time.sleep(.1)
            fps = (diag()['timings']['renderedFrames'] - f0) / (time.monotonic() - t0)
            return {'state': label, 'fps': round(fps, 1), 'tickMs': stats(ticks), 'offloadFraction': renderers.count('offload') / len(renderers)}
        rows = []
        def quiet(label, seconds=3):
            diag(); time.sleep(seconds)  # no calls in between: the present log alone
            log = diag().get('presentLog', [])
            t = [e[0] for e in log]
            gaps = [round(b - a, 1) for a, b in zip(t, t[1:])]
            return {'state': label + ' (no polling)', 'presents': len(t), 'gapsOver40ms': [g for g in gaps if g > 40],
                    'newFrames': sum(1 for e in log if e[1] == 0)}
        offload('off'); keys('-k', '1'); look(*views['placed']); time.sleep(.5)
        rows.append({'offload': 'off', **quiet('tool 1')})
        offload('on'); time.sleep(.5)
        rows.append({'offload': 'on', **quiet('tool 1')})
        for mode in ['off', 'on']:
            offload(mode)
            keys('-k', '1'); look(*views['placed']); time.sleep(.5)
            rows.append({'offload': mode, **sample('tool 1')})
            keys('-k', '4'); look(*views['rack']); center(); click(); time.sleep(.5)
            rows.append({'offload': mode, **sample('tool 4, rack selected')})
            keys('-k', '1'); time.sleep(.2)
            client.call('objects.pocket', {'ref': RACK})
            camera((-8, 1.7, 8), 0, -.35); time.sleep(.8)
            rows.append({'offload': mode, **sample('tool 5, rack ghost')})
            keys('-k', '1'); time.sleep(.3)
        results['ticks'] = rows
        (out / 'ticks.json').write_text(json.dumps(results, indent=2) + '\n')
        for r in rows:
            print('TICKS', json.dumps(r), flush=True)
        return

    # 1. Fixed animation times, three views: in-process, then offload.
    def sweep(tag):
        shots = {}
        for t in TIMES:
            clock(t)
            for view, (eye, target) in views.items():
                look(eye, target)
                shots[(view, t)] = still(f'{tag}-{view}-t{t:g}.png')
        return shots
    quick = os.environ.get('OFFLOAD_OBJECTS_SWEEP') == '0'  # development: skip the sweep
    if quick:
        TIMES[:] = TIMES[:2]
        views_all = dict(views); views.clear(); views['placed'] = views_all['placed']
    local = sweep('local')
    if quick:
        views.update(views_all)
    on = offload('on')
    check('offload runs with world objects (no local-renderer reason)', on['activeRenderer'] == 'offload',
          {k: on.get(k) for k in ('reason', 'device', 'capW', 'targetFPS', 'status')})
    remote = sweep('offload')
    worker = perf()['workerTimings']
    check('the worker draws the objects (instances, draw calls) at the pinned clock',
          worker['objectDraw']['instances'] >= 3 and worker['objectDraw']['animating'] and worker['clock'] == TIMES[-1],
          {'objectDraw': worker['objectDraw'], 'clock': worker['clock'], 'objectsRevision': worker['objectsRevision']})
    parity = []
    for key in local:
        view, t = key
        m = compare(local[key], remote[key], out / f'diff-{view}-t{t:g}.png')
        m.update(view=view, time=t)
        parity.append(m)
    results['parity'] = parity
    worst = max(p['differingFraction'] for p in parity)
    check('pixel parity in-process vs offload at fixed animation times', worst <= .001,
          [{k: p[k] for k in ('view', 'time', 'maxLevels', 'differingFraction', 'psnr')} for p in parity])
    # The armillary moves between the pinned times (the clock reaches the worker).
    moved = compare(remote[('placed', TIMES[0])], remote[('placed', TIMES[1])], out / 'diff-offload-placed-motion.png')
    check('offload frames follow the animation clock', moved['differingPixels'] > 500, moved)

    # 2. Tool 4 moves the rack while offload draws; its frames follow the drag.
    clock(TIMES[1])
    keys('-k', '4'); time.sleep(.3)
    look(*views['rack'])
    center(); click()
    wait(lambda: state()['editor']['selectedId'] == 'object:' + rack_id, 5)

    def aim(part):
        for _ in range(6):
            g = wait(lambda: diag()['gizmo'] if part in diag()['gizmo']['handles'] else None, 5)
            look(diag()['position'], g['handles'][part]['world'])
            time.sleep(.15)
            if diag()['gizmo']['hover'] == part:
                return diag()['gizmo']
        return wait(lambda: diag()['gizmo'] if diag()['gizmo']['hover'] == part else None, 3)
    g = aim('move-x')
    px, py = g['pivotScreen']
    dx, dy = (.5 - px) * 1920, (.5 - py) * 1080
    n = math.hypot(dx, dy) or 1
    start = instance(rack_id)['pose']['position']
    revisions = [perf()['workerTimings']['objectsRevision']]
    renderers, ticks = [], []
    pointer('button', 272, 1); time.sleep(.15)
    for _ in range(8):
        pointer(-dx / n * 24, -dy / n * 24); time.sleep(.08)
        p = perf(); t = diag()['timings']
        revisions.append(p['workerTimings'].get('objectsRevision')); renderers.append(p['activeRenderer'])
        ticks.append([round(t.get('cpuUpdateMs', 0), 2), round(t.get('frameMs', 0), 2), (diag().get('offloadClient') or {}).get('frontAgeMs')])
    time.sleep(.25)
    pointer('button', 272, 0)
    # The release commits (placement rules, collision, probe light): one long tick in which
    # the local renderer may stand in for a stale offload frame; offload then resumes.
    resumed = wait(lambda: perf()['activeRenderer'] == 'offload', 3)
    end = instance(rack_id)['pose']['position']
    f0 = diag()['timings']['renderedFrames']; time.sleep(1); rate = diag()['timings']['renderedFrames'] - f0
    check('a tool 4 drag moves the rack while offload is active; worker revisions follow the drag',
          abs(end[0] - start[0]) > .1 and resumed and len(set(revisions)) >= 4 and renderers.count('offload') >= len(renderers) - 1,
          {'from': start, 'to': end, 'revisions': revisions, 'renderers': renderers, 'ticks': ticks,
           'framesPerSecondAfter': rate, 'requests': diag().get('offloadRequests')})
    keys('-k', '1'); time.sleep(.3)
    look(*views['rack'])
    moved_remote = still('offload-rack-moved.png')
    look(*views['authored'])
    shown_remote = still('offload-authored-shown.png')
    client.call('objects.hide', {'id': AUTHORED, 'hidden': True})
    hidden_remote = still('offload-authored-hidden.png')
    offload('off')
    hidden_local = still('local-authored-hidden.png')
    client.call('objects.hide', {'id': AUTHORED, 'hidden': False})
    look(*views['rack'])
    moved_local = still('local-rack-moved.png')
    m1 = compare(moved_local, moved_remote, out / 'diff-rack-moved.png')
    m2 = compare(hidden_local, hidden_remote, out / 'diff-authored-hidden.png')
    gone = compare(shown_remote, hidden_remote, out / 'diff-authored-hide-effect.png')
    results['gizmo'] = {'rackMoved': m1, 'authoredHidden': m2, 'hideEffect': gone}
    check('the gizmo-moved rack and a hidden authored object match', m1['differingFraction'] <= .001 and
          m2['differingFraction'] <= .001 and gone['differingPixels'] > 500, results['gizmo'])

    # 3. Edited scene-node transforms (lifted restriction): tool 4 drags the independently
    # editable cube of the M3B fixture while offload draws; then offload on and off match.
    clock(None)
    client.call('world.load', {'worldId': 'org.hyprune.m3b'})
    wait(lambda: diag()['active'], 20)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    on = offload('on')
    keys('-k', '4'); time.sleep(.3)
    camera([2, 1.8, 3], 0, 0)
    center(); time.sleep(.3)
    click()
    try:
        selected = wait(lambda: state()['editor']['selectedId'] if state()['editor'].get('selectedKind') == 'node' else None, 5)
    except AssertionError:
        check('tool 4 selects the M3B cube (a scene node)', False, {'editor': state()['editor'], 'aim': diag().get('aim'), 'gizmo': diag().get('gizmo')})
    g = aim('move-x')
    px, py = g['pivotScreen']
    dx, dy = (.5 - px) * 1920, (.5 - py) * 1080
    n = math.hypot(dx, dy) or 1
    revisions = [perf()['workerTimings']['objectsRevision']]
    pointer('button', 272, 1); time.sleep(.15)
    for _ in range(6):
        pointer(-dx / n * 24, -dy / n * 24); time.sleep(.08)
        revisions.append(perf()['workerTimings']['objectsRevision'])
    pointer('button', 272, 0); time.sleep(.6)
    keys('-k', '1'); time.sleep(.3)
    camera([1.2, 2.2, 4.5], 0, -.15)
    edited_remote = still('offload-m3b-edited.png', 1.0)
    transforms = diag().get('objectTransforms', 0)
    offload('off')
    edited_local = still('local-m3b-edited.png', 1.0)
    m3 = compare(edited_local, edited_remote, out / 'diff-m3b-edited.png')
    results['editedTransforms'] = {'selected': selected, 'transforms': transforms, 'revisions': revisions, 'compare': m3,
                                   'reason': on.get('reason')}
    check('edited world transforms render in the offload worker', transforms > 0 and len(set(revisions)) >= 3 and
          m3['differingFraction'] <= .001, results['editedTransforms'])
    offload('off')
    (out / 'parity.json').write_text(json.dumps(results, indent=2) + '\n')
    print('PASS offload-objects parity', json.dumps({'worst': worst}), flush=True)


def gpu_sample():
    rows = subprocess.run(['nvidia-smi', '--query-gpu=pci.bus_id,utilization.gpu,power.draw', '--format=csv,noheader,nounits'],
                          capture_output=True, text=True).stdout
    return {l.split(',')[0].strip().lower()[-12:]: [float(v) for v in l.split(',')[1:]] for l in rows.splitlines() if l.count(',') == 2}


def stats(values):
    values = sorted(v for v in values if isinstance(v, (int, float)))
    if not values:
        return None
    return {'n': len(values), 'median': round(statistics.median(values), 3),
            'p95': round(values[min(len(values) - 1, int(len(values) * .95))], 3), 'mean': round(statistics.fmean(values), 3),
            'max': round(values[-1], 3)}


def measure(client, out, results, check, camera, look, views, perf, offload, diag):
    """Frame times with the clock running: a slow orbit that keeps both armillaries in view."""
    seconds = float(os.environ.get('HYPRUNE_MEASURE_SECONDS', '12'))
    eye, target = views['placed']
    radius = math.hypot(eye[0] - target[0], eye[2] - target[2])
    patch = {}
    if os.environ.get('HYPRUNE_MEASURE_DEVICE'):
        patch['offload_device'] = os.environ['HYPRUNE_MEASURE_DEVICE']
    if patch:
        client.call('graphics.set', {'patch': patch})
    phases = []
    from m2 import pose
    for mode in ['off', 'on', 'off']:
        p = offload(mode)
        time.sleep(1.5)
        draws0 = diag().get('offloadDraws') or {}
        # Cheap per-step calls only: camera.set over IPC at ~30 Hz, the renderer state at
        # ~10 Hz; the heavy diagnostics (timings, present log) once a second.
        samples, gpus, active, present = [], [], [], []
        diag()  # drains the present log
        start = time.monotonic(); last = 0; step = 0
        while (t := time.monotonic() - start) < seconds:
            a = math.atan2(eye[0] - target[0], eye[2] - target[2]) + .25 * math.sin(t * .4)
            pos = [target[0] + radius * math.sin(a), eye[1], target[2] + radius * math.cos(a)]
            d = [target[i] - pos[i] for i in range(3)]
            pose(client, pos, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))
            step += 1
            if step % 3 == 0:
                pf = perf(); active.append(pf['activeRenderer'])
            if time.monotonic() - last > 1:
                last = time.monotonic()
                dg = diag(); timing = dg.get('timings', {}); pf = perf()
                present += [e[0] for e in dg.get('presentLog', []) if e[1] == 0]  # steady ms of new frames
                samples.append({'frameMs': timing.get('frameMs'), 'cpuRenderMs': timing.get('cpuRenderMs'),
                                'gpuPassMs': sum(timing.get('gpuPassMs') or []) or None, 'deliveredFPS': timing.get('deliveredFPS'),
                                'worker': (pf.get('workerTimings') or {}) if pf['activeRenderer'] == 'offload' else {},
                                'animationFrames': dg.get('animationFrames')})
                gpus.append(gpu_sample())
            time.sleep(1 / 30)
        present += [e[0] for e in diag().get('presentLog', []) if e[1] == 0]
        per_gpu = {}
        for sample in gpus:
            for bus, (util, watts) in sample.items():
                per_gpu.setdefault(bus, {'util': [], 'watts': []})
                per_gpu[bus]['util'].append(util); per_gpu[bus]['watts'].append(watts)
        worker = {k: stats([x['worker'].get(k) for x in samples]) for k in ('renderGpuMs', 'transferGpuMs', 'producerReadyMs')}
        pf = perf()
        draws1 = diag().get('offloadDraws') or {}
        draws = {k: draws1.get(k, 0) - draws0.get(k, 0) for k in ('shown', 'stale', 'missing')}
        draws['ageMs'] = {b: n - (draws0.get('ageMs') or {}).get(b, 0) for b, n in (draws1.get('ageMs') or {}).items()}
        intervals = [round(b - a, 3) for a, b in zip(present, present[1:]) if 0 < b - a < 500]
        phases.append({'mode': mode, 'seconds': seconds, 'offloadDraws': draws, 'series': {'presentIntervalMs': intervals,
                                                                    'compositorGpuMs': [x['gpuPassMs'] for x in samples],
                                                                    'workerRenderGpuMs': [x['worker'].get('renderGpuMs') for x in samples]}, 'activeFraction': round(active.count('offload') / max(1, len(active)), 3),
                       'frameMs': stats([x['frameMs'] for x in samples]), 'cpuRenderMs': stats([x['cpuRenderMs'] for x in samples]),
                       'compositorGpuMs': stats([x['gpuPassMs'] for x in samples]),
                       'deliveredFPS': stats([x['deliveredFPS'] for x in samples]),
                       'presentIntervalMs': stats(intervals), 'worker': worker,
                       'workerObjects': next((x['worker']['objectDraw'] for x in reversed(samples) if x['worker'].get('objectDraw')), None),
                       'animationFrames': sum(1 for x in samples if x['animationFrames']) / max(1, len(samples)),
                       'gpus': {b: {k: stats(v) for k, v in d.items()} for b, d in per_gpu.items()},
                       'reason': pf.get('reason'), 'device': pf.get('device'), 'capW': pf.get('capW'),
                       'effective': {k: pf.get('effective', {}).get(k) for k in ('render_scale', 'dynamic_resolution', 'offload', 'target_fps')},
                       'renderer': diag().get('timings', {}).get('renderer')})
        results['phases'] = phases
        (out / 'perf.json').write_text(json.dumps(results, indent=2) + '\n')
    on = phases[1]
    # A measurement, not a gate: how often the worker's frame was fresh enough is reported.
    check('offload displayed during the measured orbit with objects animating', on['offloadDraws'].get('shown', 0) > 0 and
          (on['workerObjects'] or {}).get('animating'), {'activeFraction': on['activeFraction'], 'draws': on['offloadDraws'],
                                                         'objects': on['workerObjects']})
    info = json.loads((s.STATE_ROOT / 'instance.json').read_text()) if (s.STATE_ROOT / 'instance.json').is_file() else {}
    results['topology'] = {'instanceGpu': info.get('gpu'), '4k': os.environ.get('HYPRUNE_NESTED_4K') == '1'}
    (out / 'perf.json').write_text(json.dumps(results, indent=2) + '\n')
    print('MEASURE', json.dumps({f"{p['mode']}{i}": {'compositorGpuMs': (p['compositorGpuMs'] or {}).get('median'),
                                              'workerRenderGpuMs': (p['worker']['renderGpuMs'] or {}).get('median') if p['worker']['renderGpuMs'] else None,
                                              'frameMs': (p['frameMs'] or {}).get('median'),
                                              'presentIntervalMs': (p['presentIntervalMs'] or {}).get('median'),
                                              'presentP95': (p['presentIntervalMs'] or {}).get('p95')} for i, p in enumerate(phases)}), flush=True)


def prepare(installed):
    catalog_prepare(installed)
    import shutil
    subprocess.run([sys.executable, str(s.ROOT / 'tests/fixtures/m3b.py')], check=True, stdout=subprocess.DEVNULL)
    shutil.copytree(s.ROOT / 'tests/fixtures/m3b', installed / 'org.hyprune.m3b', dirs_exist_ok=True)


catalog_prepare = catalog.prepare
if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    catalog.acceptance = acceptance
    catalog.prepare = prepare
    s.run(argparse.Namespace(catalog=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, output=str(pathlib.Path(a.output).resolve())))
