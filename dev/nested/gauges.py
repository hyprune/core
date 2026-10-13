"""Gauges (RFC-0013 draft machines): dev/nested/run.sh --gauges.

Nested only. Lumen Reach (HYPRUNE_GAUGES_WORLD, else HYPRUNE_CATALOG_WORLD, else the
worlds checkout's dist package) and a starter library archive with the three gauges
(HYPRUNE_CATALOG_LIBRARY, com.hyprune.starter 0.2.0 or later). No bindings file and no
HYPRUNE_MACHINES_PROTOTYPE: the gauges bind by default to the source named after their
instance ID. The source helper runs with its own com.hyprune.sources launch grant from
the start, on a sources file that only gains the gauges once they are placed (the helper
re-reads it), each running dev/machines/scripts/gauge-sim.py. Checks:
1. Placement: one gauge from the pocket with tool 5 (Primary), two with objects.place; each
   is bound to com.hyprune.sources.<id> and a notice names that source.
2. Values: fill level, pushed and resting beads and voxel counts follow each published
   value through the asset's drives; label and display show on the label slots.
3. The running average: the ring's level moves between the published values, a
   machines.json averageWindowS (re-read on change) sets its window.
4. Frames: once the scripts hold their last value and the average has caught up, nothing
   moves and no frames are forced.
5. Offload parity (HYPRUNE_GAUGES_OFFLOAD=1; a host with a second GPU): the offload worker draws the gauges like the
   in-process renderer, before and after a change made while it renders (a machines.json
   rebind to this test's own source, then a published value); power-capped and short.
Stills (close and 3 m) and, with HYPRUNE_GAUGES_RECORD=1, a 1080p60 clip in the state dir
(gauges.mp4 and its smoothness gate): a slow arc round the three gauges moved by core per
presented frame (cinema.py), every presented frame exported. Evidence: docs/evidence/gauges.json.
"""
import hashlib, json, math, os, pathlib, secrets, shutil, signal, subprocess, sys, time
import session as s
import cinema
from m2 import wait

WORLD = 'com.hyprune.lumen-reach'
LIBRARY = 'com.hyprune.starter'
ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_CATALOG_LIBRARY', ''))
GAUGES = [('gauge-tube', 'CPU', '%'), ('gauge-abacus', 'RAM', 'GB'), ('gauge-cube', 'VRAM', 'GB')]
HELPER_TOKEN = secrets.token_hex(32)
PERIOD, STEPS, WINDOW = 5, 5, 12  # sim step seconds, steps before the hold, average window


def grants():
    return [{'token': HELPER_TOKEN, 'client': 'com.hyprune.sources', 'capabilities': ['data.publish']}]


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_GAUGES_WORLD') or os.environ.get('HYPRUNE_CATALOG_WORLD') or
                          str(s.ROOT.parent / 'worlds/dist' / WORLD))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--gauges needs Lumen Reach at {source} (HYPRUNE_GAUGES_WORLD)')
    if not ARCHIVE.is_file():
        raise SystemExit('--gauges needs the starter library archive with the gauges (HYPRUNE_CATALOG_LIBRARY)')
    os.environ.pop('HYPRUNE_MACHINES_PROTOTYPE', None)
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir()
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'machines.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)
    (s.STATE / 'sources.json').write_text(json.dumps({'devFastIntervals': True, 'sources': []}, indent=1) + '\n')


def helper(socketpath):
    token = s.STATE / 'helper.token'
    token.write_text(HELPER_TOKEN + '\n'); token.chmod(0o600)
    log = open(s.STATE / 'source-helper.log', 'w')
    return subprocess.Popen([sys.executable, str(s.ROOT / 'dev/machines/source-helper.py'), '--config', str(s.STATE / 'sources.json'),
                             '--socket', str(socketpath), '--token-file', str(token), '-v'],
                            stdout=log, stderr=subprocess.STDOUT, start_new_session=True)


def mapped(map_, v):
    """The drive mapping forms a gauge uses on its value (scale with clamp and round)."""
    if not map_ or 'scale' not in map_:
        return v
    (a, b), (c, d) = map_['scale']['from'], map_['scale']['to']
    out = c + (v - a) / (b - a) * (d - c)
    if map_.get('clamp', True):
        out = min(max(out, min(c, d)), max(c, d))
    return {'floor': math.floor, 'ceil': math.ceil, 'nearest': lambda x: math.floor(x + .5)}.get(map_.get('round'), lambda x: x)(out)


def acceptance(client, children, args, shell_token, socketpath):
    # Launch tokens are one-use and expire 30 s after core starts: the helper connects first.
    source = helper(socketpath)
    try:
        checks(client, children, source)
    finally:
        if source.poll() is None:
            source.send_signal(signal.SIGTERM)
            try:
                source.wait(timeout=5)
            except subprocess.TimeoutExpired:
                source.kill()
    client.call('runtime.exit', {})


def checks(client, children, source):
    results = []
    out = s.STATE / 'gauges'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir()

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS gauges:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60); time.sleep(1)
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)

    def click(button=272):
        pointer('button', button, 1); time.sleep(.12); pointer('button', button, 0); time.sleep(.35)

    for _ in range(4):
        xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    def user(): return [i for i in state()['objects']['instances'] if i['source'] == 'user']
    def machines(): return s.camera()['machines']
    def inst(id_): return next((x for x in machines()['instances'] if x['id'] == id_), None)
    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})'); time.sleep(.3)
    def look(pos, target):
        d = [target[i] - pos[i] for i in range(3)]
        camera(pos, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))
    def shot(name):
        path = out / name; subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True); return path.name
    def frames(seconds):
        a = s.camera(); t0 = time.monotonic(); time.sleep(seconds); b = s.camera(); dt = time.monotonic() - t0
        return {'perSecond': round((b['frames'] - a['frames']) / dt, 1), 'frames': b['frames'] - a['frames'], 'animationFrames': b['animationFrames']}
    def notices(code): return [e['params'] for e in client.events if e.get('method') == 'runtime.notice' and e['params']['code'] == code]
    def error(method, params):
        try:
            client.call(method, params)
        except RuntimeError as e:
            return e.args[0]
        return None

    archive = s.STATE / ARCHIVE.name
    installed = client.call('libraries.install', {'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
    version = installed['version']
    entries = {e['ref']: e for e in client.call('objects.catalog', {})['entries']}
    refs = {g: f'library/{LIBRARY}@{version}/{g}' for g, _, _ in GAUGES}
    check('the library brings three placeable gauges with the machine trait',
          all(r in entries and entries[r]['placeable'] and 'machine' in entries[r]['traits'] for r in refs.values()),
          {r: {k: entries.get(r, {}).get(k) for k in ('state', 'placeable', 'traits', 'triangles')} for r in refs.values()})
    library = next(p for p in (s.STATE / 'datahome').rglob('object.json') if json.loads(p.read_text()).get('id') == 'gauge-tube').parents[1]
    manifests = {g: json.loads(next(p for p in library.rglob('object.json') if json.loads(p.read_text()).get('id') == g).read_text())
                 for g, _, _ in GAUGES}

    # 1. Placement: the tube from the pocket with tool 5, the others beside it.
    client.events.clear()
    spot = os.environ.get('HYPRUNE_GAUGES_SPOT')  # development: "x,z" places the tube there instead
    if spot:
        x, z = map(float, spot.split(','))
        tube_id = None
        for dz in (0, -1, 1, -2, 2):
            try:
                tube_id = client.call('objects.place', {'ref': refs['gauge-tube'], 'pose': {'position': [x, 0, z + dz], 'rotation': [0, 0, 0, 1]}})['id']
                break
            except RuntimeError:
                continue
        assert tube_id, ('no spot for the tube near', spot)
    else:
        client.call('objects.pocket', {'ref': refs['gauge-tube']})
    floor = [] if spot else [((-8, 1.7, 8), 0, -.35), ((-4.5, 1.7, 8), 0, -.35), ((-11, 1.7, 8), 0, -.35), ((-6, 1.7, 6), -.3, -.4)]
    g = None
    for pos, yaw, pitch in floor:
        camera(pos, yaw, pitch)
        g = wait(lambda: s.camera()['objects']['ghost'] if s.camera()['objects']['ghost']['asset'] >= 0 else None, 20); time.sleep(.3)
        g = s.camera()['objects']['ghost']
        if g['valid']:
            break
    if not spot:
        assert g['valid'], g
        click()
    tube = wait(lambda: user() or None, 10)[0]
    ids = {'gauge-tube': tube['id']}
    p0 = tube['pose']['position']
    for gauge, offsets in (('gauge-abacus', (-1.6, -1.9, -2.2, -2.6)), ('gauge-cube', (1.6, 1.9, 2.2, 2.6))):
        for dx in offsets:
            try:
                r = client.call('objects.place', {'ref': refs[gauge], 'pose': {'position': [p0[0] + dx, p0[1], p0[2]], 'rotation': [0, 0, 0, 1]}})
                ids[gauge] = r['id']
                break
            except RuntimeError:
                continue
        assert gauge in ids, ('no spot beside the tube for', gauge)
    m = wait(lambda: machines() if machines()['active'] and len(machines()['instances']) == 3 else None, 20)
    bound = {g: inst(i) for g, i in ids.items()}
    check('three placed gauges run as machines without a bindings file, each on com.hyprune.sources.<id>',
          all(b and b['inputs']['value']['source'] == f'com.hyprune.sources.{ids[g]}' and b['inputs']['value']['key'] == 'value'
              for g, b in bound.items()), {g: {'id': ids[g], 'source': b['inputs']['value']['source']} for g, b in bound.items()})
    def named():  # events arrive with responses: a ping collects them
        client.call('session.ping', {})
        hints = notices('machine.source')
        return hints if all(any(i in h['message'] for h in hints) for i in ids.values()) else None
    try:
        hints = wait(named, 5)
    except AssertionError:
        hints = notices('machine.source')
    check('a notice names the data source each new gauge reads',
          all(any(i in h['message'] for h in hints) for i in ids.values()), [h['message'] for h in hints])
    # A new abacus counts its resting beads in (the stack rate), then everything rests.
    try:
        bound = wait(lambda: (lambda b: b if all(not x['moving'] for x in b.values()) else None)({g: inst(i) for g, i in ids.items()}), 10)
    except AssertionError:
        bound = {g: inst(i) for g, i in ids.items()}
    check('unfed gauges: labels show a dash and, once placed, nothing moves', all(b['labels']['display'] == '—' and not b['moving'] for b in bound.values()),
          {g: {'labels': b['labels'], 'moving': b['moving'], 'stacks': b['stacksShown']} for g, b in bound.items()})

    if os.environ.get('HYPRUNE_GAUGES_LOOK'):  # development: fixed values, stills, nothing else
        look_stills(client, ids, user, look, shot, os.environ['HYPRUNE_GAUGES_LOOK'])
        return
    if os.environ.get('HYPRUNE_GAUGES_OFFLOAD_FIRST') == '1':  # development: the offload stage on unfed gauges
        offload_parity(client, check, ids, p0, look, inst, out, [])
        return
    # 2. Sources: the scripts start now; the helper picks the new entries up.
    epoch = int(time.time()) + 2
    (s.STATE / 'machines.json').write_text(json.dumps({'instances': {i: {'averageWindowS': WINDOW} for i in ids.values()}}, indent=1) + '\n')
    sim = str(s.ROOT / 'dev/machines/scripts/gauge-sim.py')
    config = {'devFastIntervals': True, 'sources': [
        {'id': ids[g], 'kind': 'command', 'argv': [sim, '--label', label, '--unit', unit, '--steps', str(STEPS), '--period', str(PERIOD), '--hold'],
         'intervalS': 1, 'timeoutS': 2, 'env': {'MACHINES_SIM_EPOCH': str(epoch), 'MACHINES_SIM_SEED': g}} for g, label, unit in GAUGES]}
    (s.STATE / 'sources.json').write_text(json.dumps(config, indent=1) + '\n')
    timeline = {}
    for g, label, unit in GAUGES:
        env = {**os.environ, 'MACHINES_SIM_EPOCH': str(epoch), 'MACHINES_SIM_SEED': g}
        lines = subprocess.run([sys.executable, sim, '--label', label, '--unit', unit, '--steps', str(STEPS), '--period', str(PERIOD), '--hold',
                                '--timeline', str(epoch), str(epoch + PERIOD * (STEPS + 1)), '1'],
                               env=env, capture_output=True, text=True, check=True).stdout.split('\n')
        timeline[g] = [json.loads(l) for l in lines if l.strip()]
    finals = {g: t[-1] for g, t in timeline.items()}
    wait(lambda: all(inst(i)['inputs']['value']['condition'] == 'fresh' for i in ids.values()) or None, 20)
    recorder = None
    if os.environ.get('HYPRUNE_GAUGES_RECORD') == '1':
        # A slow arc round the gauges, pausing on the front view (the 3 m still) at 2.5 periods.
        cam = cinema.Cinema(s)
        aim = [p0[0], .75, p0[2]]
        arc = lambda deg, r, h: [p0[0] + r * math.sin(math.radians(deg)), h, p0[2] + r * math.cos(math.radians(deg))]
        cam.hold(arc(-28, 4.0, 1.6), aim); time.sleep(.8)
        recorder = cam.recording(out / 'gauges.capture.mkv', warmup=.5)
        began = recorder.__enter__()
        front = max(6., epoch + PERIOD * 2.5 - time.time())  # path time of the still
        cam.path([{'t': 0, 'eye': arc(-28, 4.0, 1.6), 'target': aim, 'ease': 'in'},
                  {'t': front * .5, 'eye': arc(-15, 3.6, 1.52), 'target': aim},
                  {'t': front - 1.0, 'eye': arc(0, 3.2, 1.45), 'target': aim, 'ease': 'out'},
                  {'t': front + 1.5, 'eye': arc(0, 3.2, 1.45), 'target': aim, 'ease': 'in'},
                  {'t': front + 10, 'eye': arc(14, 3.3, 1.5), 'target': aim},
                  {'t': front + 22, 'eye': arc(26, 3.7, 1.62), 'target': aim, 'ease': 'out'},
                  {'t': front + 24, 'eye': arc(26, 3.7, 1.62), 'target': aim}])
    ring = {g: next(d['target'].split(':', 1)[1] for d in manifests[g]['traits']['machine']['drives'] if (d.get('map') or {}).get('average'))
            for g, _, _ in GAUGES}
    samples = {g: [] for g, _, _ in GAUGES}
    seen = {g: set() for g, _, _ in GAUGES}
    deadline = epoch + PERIOD * STEPS + 2
    stills = []
    while time.time() < deadline:
        for g, _, _ in GAUGES:
            b = inst(ids[g])
            v = b['inputs']['value']['raw']
            if b['inputs']['value']['condition'] != 'fresh' or v is None:
                continue
            seen[g].add(v)
            samples[g].append({'t': round(time.time() - epoch, 2), 'value': v, 'ring': b['levels'][ring[g]]['target'],
                               'counts': b['stacks'], 'levels': {k: x['target'] for k, x in b['levels'].items()}, 'labels': b['labels']})
            for d in manifests[g]['traits']['machine']['drives']:
                if d['input'] != 'value' or (d.get('map') or {}).get('average'):
                    continue
                kind, slot = d['target'].split(':', 1)
                expected = mapped(d.get('map'), v)
                got = b['stacks'].get(slot) if kind == 'count' else b['levels'].get(slot, {}).get('target') if kind == 'level' else None
                if got is not None and b['inputs']['value']['raw'] == inst(ids[g])['inputs']['value']['raw']:
                    assert abs(got - expected) < 1e-6, (g, d['target'], v, got, expected)
        if not stills and time.time() > epoch + PERIOD * 2.5:
            if recorder is None:
                look([p0[0], 1.45, p0[2] + 3.2], [p0[0], .75, p0[2]]); time.sleep(.6)
            stills.append(shot('gauges-3m.png'))
        time.sleep(.5)
    published = {g: sorted({t['value'] for t in timeline[g]}) for g in timeline}
    check('every gauge followed the script: fill level, pushed/rest beads and voxels match each published value',
          all(seen[g] and seen[g] <= set(published[g]) and len(seen[g]) >= 3 for g in seen),
          {g: {'seen': sorted(seen[g]), 'published': published[g]} for g in seen})
    labels = {g: inst(ids[g])['labels'] for g, _, _ in GAUGES}
    check('label and display show the script\'s text', all(labels[g]['label'] == finals[g]['label'] and labels[g]['display'] == finals[g]['display']
                                                         for g in labels) and machines()['labelsDrawn'] >= 6, {'labels': labels, 'finals': finals})
    moved = {g: sorted({round(x['ring'], 3) for x in samples[g]}) for g in samples}
    between = all(min(x['value'] for x in samples[g]) / 100 - 1e-6 <= x['ring'] <= max(x['value'] for x in samples[g]) / 100 + 1e-6
                  for g in samples for x in samples[g][3:])
    check('the average ring moves, within the range of the published values', all(len(moved[g]) >= 5 for g in moved) and between,
          {g: moved[g][:12] for g in moved})
    if recorder is not None:
        time.sleep(WINDOW + 3)
        recorder.__exit__(None, None, None)
        cam.stop()
        cinema.encode([out / 'gauges.capture.mkv'], out / 'gauges.mp4', trim=.5)
        gate = cinema.gate(out / 'gauges.mp4')
        (out / 'gauges.gate.json').write_text(json.dumps(gate, indent=1) + '\n')
        print('gauges recording:', cinema.gate_summary(gate), 'export', json.dumps(cam.last_export), flush=True)
    # 3. Hold: the average catches up with the last value, then nothing moves.
    hold = wait(lambda: all(abs(inst(ids[g])['levels'][ring[g]]['level'] - finals[g]['value'] / 100) < 1e-6 for g in ids) or None,
                WINDOW + 10)
    check('after the window the ring sits exactly at the held value', bool(hold),
          {g: inst(ids[g])['levels'][ring[g]] for g in ids})
    look([p0[0], 1.45, p0[2] + 3.2], [p0[0], .75, p0[2]]); time.sleep(1.5)
    # Lumen Reach animates other objects in this view: the gauges' own share is no motion
    # and no new revision (a revision or motion in view is what makes a machine render).
    r0 = {i: inst(i)['revision'] for i in ids.values()}
    idle = frames(3)
    held = {i: {k: inst(i)[k] for k in ('moving', 'inView', 'revision')} for i in ids.values()}
    check('values held: the gauges stop (no motion, no new revision in view), so they force no frames',
          all(not h['moving'] and h['inView'] and h['revision'] == r0[i] for i, h in held.items()),
          {'gauges': held, 'frames in this view (other world animation included)': idle,
           'npcsForceFrames': s.camera()['npcs'].get('forcesFrames')})
    stills.append(shot('gauges-3m-held.png'))
    for g, _, _ in GAUGES:
        p = next(i for i in user() if i['id'] == ids[g])['pose']['position']
        look([p[0], 1.15, p[2] + 1.3], [p[0], .7, p[2]]); time.sleep(.8)
        stills.append(shot(f'{g}-close.png'))
    if os.environ.get('HYPRUNE_GAUGES_OFFLOAD', '0') != '0':
        offload_parity(client, check, ids, p0, look, inst, out, stills)
    check('no render pass leaked', s.camera().get('renderPassesLeaked', 0) == 0, s.camera().get('renderPassesLeaked'))
    (s.ROOT / 'docs/evidence/gauges.json').write_text(json.dumps({'nestedOnly': True, 'world': WORLD, 'library': f'{LIBRARY}@{version}',
                                                                  'stills': stills, 'checks': results}, indent=2) + '\n')
    print('PASS gauges:', len(results), 'checks', flush=True)


def offload_parity(client, check, ids, p0, look, inst, out, stills):
    """In-process and offload stills of the held gauges agree; a change while offloaded
    (rebind through machines.json, publish) reaches the worker and agrees again."""
    import numpy as np
    from PIL import Image
    perf = lambda: client.call('graphics.get', {})['performance']

    def renderer(mode):
        client.call('graphics.set', {'patch': {'offload': mode, 'render_scale': 1, 'dynamic_resolution': False,
                                               'offload_power_cap_w': None}})
        want = 'offload' if mode == 'on' else 'in-process'
        try:
            return wait(lambda: perf()['activeRenderer'] == want, 40)
        except AssertionError:
            c = s.camera()
            raise AssertionError(('offload did not switch', want, perf(), c.get('offloadRequests'), c.get('offloadClient'),
                                  (c['timings'].get('offload') or {})))

    def shot(name):
        path = out / f'gauges-offload-{name}.png'
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        stills.append(path.name)
        return path

    def region(path):
        img = np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)
        h, w = img.shape[:2]
        return img[int(h * .4):int(h * .82), int(w * .1):int(w * .9)]  # the three gauges, not the world's animation

    def compare(a, b):
        d = np.abs(region(a) - region(b)).max(axis=2)
        return {'meanLevels': round(float(d.mean()), 3), 'differing': round(float((d > 6).mean()), 5)}
    look([p0[0], 1.45, p0[2] + 3.2], [p0[0], .75, p0[2]]); time.sleep(.6)
    before = {}
    for mode in ('off', 'on'):
        renderer(mode); time.sleep(1.5)
        before[mode] = shot('held-' + mode)
    worker = (s.camera()['timings'].get('offload') or {}).get('worker') or {}
    held = compare(before['off'], before['on'])
    check('offload: no local fallback with gauges placed, the worker draws their state and labels',
          perf()['activeRenderer'] == 'offload' and (worker.get('machines') or {}).get('states') == 3 and
          (worker.get('machines') or {}).get('labelsDrawn', 0) >= 6, {'worker': worker.get('machines'), 'renderer': perf()['activeRenderer']})
    check('offload draws the held gauges like the in-process renderer', held['differing'] < .01, held)
    # A change while the worker renders: the tube reads this test's source (machines.json, re-read).
    tube = ids['gauge-tube']
    path = s.STATE / 'machines.json'
    bindings = json.loads(path.read_text()) if path.exists() else {'instances': {}}
    bindings['instances'].setdefault(tube, {})['source'] = 'org.hyprune.nested.gauge'
    (s.STATE / 'machines.json').write_text(json.dumps(bindings, indent=1) + '\n')
    values = {'value': 90, 'display': '90%', 'label': 'GPU', 'level': 'fail', 'average': 90}
    client.call('data.publish', {'sourceId': 'org.hyprune.nested.gauge', 'sequence': int(time.time() * 1000), 'values': values, 'ttlMs': 60000})
    wait(lambda: (lambda t: t if t['labels']['display'] == '90%' and not t['moving'] else None)(inst(tube)), 15)
    time.sleep(.6)
    after = {'on': shot('changed-on')}
    renderer('off'); time.sleep(1.5)
    after['off'] = shot('changed-off')
    changed, moved = compare(after['off'], after['on']), compare(before['on'], after['on'])
    check('a change made while offloaded reaches the worker and matches the in-process draw',
          changed['differing'] < .01 and moved['differing'] > .002, {'parity': changed, 'changedVsHeld': moved})


def look_stills(client, ids, user, look, shot, name):
    """Fixed values on every gauge (bound to this test's own source through machines.json),
    then a 3 m still and a close still of each: for colour and readability reviews."""
    (s.STATE / 'machines.json').write_text(json.dumps({'instances': {i: {'source': 'org.hyprune.nested.look'} for i in ids.values()}}) + '\n')
    import threading
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})  # the camera stays where it is put
    current = {'value': float(os.environ.get('HYPRUNE_GAUGES_LOOK_VALUE', '64')), 'display': '64%', 'label': 'LOOK',
               'level': os.environ.get('HYPRUNE_GAUGES_LOOK_LEVEL', 'ok'), 'average': 50}
    lock, running = threading.Lock(), threading.Event()
    running.set()
    def keepalive():  # fresh data the whole time (staleMs is 8 s)
        while running.is_set():
            with lock:
                client.call('data.publish', {'sourceId': 'org.hyprune.nested.look', 'sequence': int(time.time() * 1000),
                                             'values': dict(current), 'ttlMs': 60000})
            time.sleep(1)
    threading.Thread(target=keepalive, daemon=True).start()
    time.sleep(5)
    def views(suffix):
        with lock:
            placed = {x['id']: x['pose']['position'] for x in user()}
        p0 = placed[ids['gauge-tube']]
        look([p0[0], 1.45, p0[2] + 3.2], [p0[0], .75, p0[2]]); time.sleep(1)
        shot(f'look-{name}-3m{suffix}.png')
        for g, i in ids.items():
            p = placed[i]
            look([p[0], 1.25, p[2] + 2.0], [p[0], .75, p[2]]); time.sleep(1)
            shot(f'look-{name}-{g}{suffix}.png')
    views('')
    # The same views empty: pixels that change are the fill (a mask for colour measurements).
    with lock:
        current.update(value=0, average=0)
    time.sleep(5)
    views('-empty')
    running.clear()
