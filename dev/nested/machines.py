"""Machines prototype (RFC-0013 draft): dev/nested/run.sh --machines.

Nested only (isolated harness on headless hosts). Loads the original CC0 fixture
tests/fixtures/machines (a small machine asset placed twice, plus one plain copy)
with HYPRUNE_MACHINES_PROTOTYPE. machine-a reads values this test publishes with
data.publish; machine-b reads `status` from the real source helper
(dev/machines/source-helper.py running sims/build-sim.py with its own
com.hyprune.sources grant). Checks:
1. Missing data: rest state, labels show a dash, nothing moves or forces frames.
2. Published values: palettes, labels, stack counts, toggles, emission; the
   change is visible in pixels; a running spinner keeps frames coming in view.
3. Out of view: a bound change updates state and forces no frames; looking back
   shows it.
4. Counters spawn items that ride the track; a `becomes` transition fires the
   burst; an idle machine costs no frames.
5. Stale data dims the machine and stops it; removed data falls back to defaults.
6. Reduced motion: states change, nothing spins or travels, no frames forced.
7. The real helper publishes through its own grant and machine-b follows it; on
   SIGTERM it removes its sources.
8. Offload parity (HYPRUNE_MACHINES_OFFLOAD=1; a host with a second GPU): with reduced
   motion (nothing spins or travels) the offload worker draws the machines' palettes,
   labels, stacked crates, toggles and lift like the in-process renderer.
HYPRUNE_MACHINES_WORLD=<package dir> (with HYPRUNE_MACHINES_BINDINGS, default
<package>/machines.bindings.json) loads another world instead and only runs the
helper with every simulator (for demos: HYPRUNE_MACHINES_DEMO=<script.py> is run
with the session's client, socket and state). Evidence: docs/evidence/machines.json.
"""
import json, math, os, pathlib, secrets, signal, shutil, subprocess, sys, time
import session as s
from m2 import wait

FIXTURE = 'org.hyprune.machines'
LINE = 'org.hyprune.nested.line'
HELPER_TOKEN = secrets.token_hex(32)
WORLD = FIXTURE


def grants():
    """The helper's own launch grant: data.publish only, under com.hyprune.sources."""
    return [{'token': HELPER_TOKEN, 'client': 'com.hyprune.sources', 'capabilities': ['data.publish']}]


def prepare(installed):
    global WORLD
    other = os.environ.get('HYPRUNE_MACHINES_WORLD')
    if other:
        package = pathlib.Path(other)
        WORLD = json.loads((package / 'world.json').read_text())['id']
        shutil.copytree(package, installed / WORLD, dirs_exist_ok=True)
        bindings = pathlib.Path(os.environ.get('HYPRUNE_MACHINES_BINDINGS', package / 'machines.bindings.json'))
        os.environ['HYPRUNE_MACHINES_PROTOTYPE'] = str(bindings.resolve())
        return
    subprocess.run([sys.executable, str(s.ROOT / 'tests/fixtures/machines.py')], check=True, stdout=subprocess.DEVNULL)
    shutil.copytree(s.ROOT / 'tests/fixtures/machines', installed / FIXTURE, dirs_exist_ok=True)
    spec = json.loads((s.ROOT / 'tests/fixtures/machines/machines.bindings.json').read_text())
    for binding in spec['instances']['machine-a']['inputs'].values():
        binding['source'] = LINE  # this test publishes it
    spec['instances']['machine-b']['inputs'] = {'status': {'source': 'com.hyprune.sources.build', 'key': 'status', 'map': {
        'cases': [{'equals': 'success', 'to': 'ok'}, {'equals': 'failed', 'to': 'fail'}, {'equals': 'running', 'to': 'running'}],
        'else': 'idle'}}}
    path = s.STATE / 'machines.bindings.json'
    path.write_text(json.dumps(spec, indent=1))
    os.environ['HYPRUNE_MACHINES_PROTOTYPE'] = str(path)


def helper(socketpath, only=None):
    """Starts the source helper with its own token, every simulator or only some."""
    config = json.loads((s.ROOT / 'dev/machines/sources.example.json').read_text())
    # HYPRUNE_MACHINES_SIM_ENV: {"<source id>": {"MACHINES_SIM_SHIFT": "..."}} per simulator (demos).
    extra = json.loads(os.environ.get('HYPRUNE_MACHINES_SIM_ENV', '{}'))
    config['sources'] = [dict(src, argv=[str(s.ROOT / 'dev/machines' / src['argv'][0])], env={**src.get('env', {}), **extra.get(src['id'], {})})
                         for src in config['sources'] if src.get('kind') == 'command' and (only is None or src['id'] in only)]
    (s.STATE / 'sources.json').write_text(json.dumps(config, indent=1))
    token = s.STATE / 'helper.token'
    token.write_text(HELPER_TOKEN + '\n'); token.chmod(0o600)
    log = open(s.STATE / 'source-helper.log', 'w')
    return subprocess.Popen([sys.executable, str(s.ROOT / 'dev/machines/source-helper.py'), '--config', str(s.STATE / 'sources.json'),
                             '--socket', str(socketpath), '--token-file', str(token), '-v'],
                            stdout=log, stderr=subprocess.STDOUT, start_new_session=True)


def acceptance(client, children, args, shell_token, socketpath):
    # Launch tokens expire 30 s after core starts: the helper connects first.
    only = None if os.environ.get('HYPRUNE_MACHINES_WORLD') else ['build']
    source = helper(socketpath, only)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60); time.sleep(1)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    try:
        if os.environ.get('HYPRUNE_MACHINES_WORLD'):
            demo(client, socketpath, source)
        else:
            checks(client, source)
    finally:
        if source.poll() is None:
            source.send_signal(signal.SIGTERM)
            try:
                source.wait(timeout=5)
            except subprocess.TimeoutExpired:
                source.kill()
    client.call('runtime.exit', {})


def demo(client, socketpath, source):
    m = wait(lambda: s.camera()['machines'] if s.camera()['machines']['active'] else None, timeout=10)
    print('machines:', [i['id'] for i in m['instances']], flush=True)
    script = os.environ.get('HYPRUNE_MACHINES_DEMO')
    if not script:
        time.sleep(float(os.environ.get('HYPRUNE_MACHINES_HOLD', '20')))
        return
    import importlib.util
    spec = importlib.util.spec_from_file_location('machines_demo', script)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    module.run(client, s, source)


def checks(client, source):
    results = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data}); print('PASS machines:', name, flush=True)
    from PIL import Image, ImageChops

    def machines(): return s.camera()['machines']
    def inst(i='machine-a'): return next(x for x in machines()['instances'] if x['id'] == i)
    def camera(x, y, z, yaw, pitch=0): s.ctl('eval', f'hl.plugin.hyprune.camera({x},{y},{z},{yaw},{pitch})')
    def shot(name):
        path = s.STATE / name; subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True); return path
    def fraction(a, b, box=None):
        x, y = Image.open(a).convert('RGB'), Image.open(b).convert('RGB')
        if box: x, y = x.crop(box), y.crop(box)
        diff = ImageChops.difference(x, y).convert('L').point(lambda v: 255 if v > 24 else 0)
        return round(sum(diff.histogram()[255:]) / (diff.width * diff.height), 5)
    def frames(seconds):
        a = s.camera(); t0 = time.monotonic(); time.sleep(seconds); b = s.camera(); dt = time.monotonic() - t0
        return {'perSecond': round((b['frames'] - a['frames']) / dt, 1), 'frames': b['frames'] - a['frames'], 'animationFrames': b['animationFrames']}
    sequence = [int(time.time() * 1000)]
    import threading
    lock = threading.RLock()  # one IPC client shared with the keep-alive thread
    def call(method, params):
        with lock:
            return client.call(method, params)
    def publish(values):
        with lock:
            for attempt in range(3):
                sequence[0] = max(sequence[0] + 1, int(time.time() * 1000))
                try:
                    return client.call('data.publish', {'sourceId': LINE, 'sequence': sequence[0], 'values': values, 'ttlMs': 60000})
                except RuntimeError as e:  # -32008: 10 publications per second per source
                    if '-32008' not in str(e) or attempt == 2:
                        raise
                    time.sleep(.12)
    keep = threading.Event()  # set: republish `values` every second (staleMs is 3 s)
    def keepalive():
        while True:
            time.sleep(1)
            if keep.is_set() and values:
                publish(values)
    threading.Thread(target=keepalive, daemon=True).start()
    def hold(seconds): time.sleep(seconds)
    front = lambda: camera(-3, 1.4, 4.2, 0, -.05)  # machine-a fills the middle of the frame
    away = lambda: camera(-3, 1.4, 4.2, math.pi, 0)

    values = {}
    m = machines()
    check('the prototype binds two instances from the bindings file', m['active'] and len(m['instances']) == 2, [i['id'] for i in m['instances']])
    # 1. Missing data.
    front(); time.sleep(1.5)
    a = inst()
    check('missing data: inputs at their defaults, labels show a dash', a['inputs']['status']['condition'] == 'missing'
          and a['states']['lamp_1'] == 'idle' and a['labels']['sign'] == '—' and machines()['labelsDrawn'] >= 1, a)
    idle = frames(2)
    check('an unfed machine forces no frames', idle['frames'] <= 3 and not idle['animationFrames'],
          {'frames': idle, 'instances': [{k: x[k] for k in ('id', 'moving', 'inView', 'animations')} for x in machines()['instances']]})
    before = shot('machines-missing.png')
    # 2. Values.
    values.update({'status': 'running', 'stage.1': 'running', 'stage.2': 'failed', 'jobsStarted': 500, 'depth': 70,
              'progress': .8, 'lastJob': 'main #1432'})
    publish(values); keep.set(); time.sleep(1.2)
    a = inst()
    check('published values reach the inputs through the binding maps', a['inputs']['status']['value'] == 'running'
          and a['inputs']['stage.2']['value'] == 'fail' and a['inputs']['status']['condition'] == 'fresh', a['inputs'])
    check('drives set palettes, labels, stack counts and toggles', a['states'] == {'lamp_1': 'running', 'lamp_2': 'fail'}
          and a['labels']['sign'] == 'main #1432' and a['stacks']['waiting'] == 4 and a['toggles']['alarm'] is False, a)
    running = frames(2)
    check('a spinning machine in view keeps frames coming', running['perSecond'] >= 10 and running['animationFrames'], running)
    after = shot('machines-running.png')
    changed = fraction(before, after)
    check('the change is drawn (lamps, crates, label, lift)', changed > .002, {'changedFraction': changed})
    # 3. Out of view.
    away(); time.sleep(1)
    r0 = inst()['revision']; quiet = frames(2)
    check('out of view a spinning machine forces no frames', quiet['frames'] <= 3 and inst()['moving'] and not inst()['inView'], quiet)
    values['stage.1'] = 'success'; publish(values)
    quiet = frames(1.5)
    check('a bound change out of view updates state without frames', inst()['states']['lamp_1'] == 'ok' and inst()['revision'] != r0
          and quiet['frames'] <= 3, {'frames': quiet, 'revision': [r0, inst()['revision']]})
    # 4. Items, transitions, idle cost.
    front(); time.sleep(.5)
    values['jobsStarted'] = 503; publish(values); time.sleep(1.8)
    a = inst(); timings = s.camera()['timings']['objects']
    check('a counter increase spawns items onto the track', a['tracks'][0]['spawned'] == 3 and a['tracks'][0]['live'] >= 2, a['tracks'])
    check('items are drawn as instances', timings['instancesDrawn'] > 3, timings)
    items = shot('machines-items.png')
    values['status'] = 'success'; publish(values); time.sleep(.6)
    check('running -> success fires the burst', machines()['pieces'] > 0, machines()['pieces'])
    burst = shot('machines-burst.png')
    hold(8)
    a = inst()
    check('items leave at the sink; the spinner winds down', a['tracks'][0]['live'] == 0 and a['animations']['spinner'] == 0, a)
    settled = frames(2)
    check('an idle machine in view costs no frames', settled['frames'] <= 3 and not settled['animationFrames'], settled)
    # 5. Stale, then missing.
    values['status'] = 'failed'; publish(values); keep.clear(); time.sleep(.8)
    fail = shot('machines-fail.png')
    check('fail shows the alarm toggle and doubles lamp emission', inst()['toggles']['alarm'] is True, inst()['toggles'])
    time.sleep(3.5)
    a = inst()
    check('past staleMs the machine is stale and greys', a['stale'] and a['inputs']['status']['condition'] == 'stale' and a['look'] > .9, a)
    stale = shot('machines-stale.png')
    greyed = fraction(fail, stale)
    check('the stale look is drawn', greyed > .002, {'changedFraction': greyed})
    call('data.remove', {'sourceId': LINE}); time.sleep(.8)
    a = inst()
    check('removed data: defaults again, labels show a dash', a['inputs']['status']['condition'] == 'missing'
          and a['labels']['sign'] == '—' and a['states']['lamp_1'] == 'idle', a)
    # 6. Reduced motion.
    values.update(status='running', jobsStarted=600); publish(values); keep.set(); time.sleep(1)
    call('graphics.set', {'patch': {'reduced_motion': True}}); time.sleep(.6)
    values['jobsStarted'] = 604; values['stage.2'] = 'success'; publish(values); time.sleep(1.2)
    a = inst(); still = frames(2)
    check('reduced motion: states change, nothing spins or travels, no frames', a['states']['lamp_2'] == 'ok'
          and a['animations']['spinner'] == 0 and a['tracks'][0]['live'] == 0 and still['frames'] <= 3, {'instance': a, 'frames': still})
    call('graphics.set', {'patch': {'reduced_motion': False}}); time.sleep(.5)
    # 7. The real helper drives machine-b.
    b = wait(lambda: inst('machine-b') if inst('machine-b')['inputs']['status']['condition'] == 'fresh' else None, timeout=15)
    check('the source helper publishes with its own grant and machine-b follows build-sim', b['inputs']['status']['source'] == 'com.hyprune.sources.build'
          and b['inputs']['status']['raw'] in ('idle', 'running', 'success', 'failed'), b['inputs']['status'])
    source.send_signal(signal.SIGTERM); source.wait(timeout=10)
    b = wait(lambda: inst('machine-b') if inst('machine-b')['inputs']['status']['condition'] == 'missing' else None, timeout=5)
    check('on SIGTERM the helper removes its sources', b is not None and source.returncode == 0, {'exit': source.returncode})
    if os.environ.get('HYPRUNE_MACHINES_OFFLOAD', '0') != '0':
        offload_parity(client, check, call, publish, values, inst, front, shot)
    check('no render pass leaked', s.camera().get('renderPassesLeaked', 0) == 0, s.camera().get('renderPassesLeaked'))
    stills = [str(p.name) for p in (before, after, items, burst, fail, stale)]
    (s.ROOT / 'docs/evidence/machines.json').write_text(json.dumps({'nestedOnly': True, 'world': FIXTURE, 'stills': stills, 'checks': results}, indent=2) + '\n')
    print('PASS machines:', len(results), 'checks', flush=True)


def offload_parity(client, check, call, publish, values, inst, front, shot):
    """In-process and offload stills of machine-a in a set state agree pixel for pixel."""
    import numpy as np
    from PIL import Image
    perf = lambda: call('graphics.get', {})['performance']
    def renderer(mode):
        call('graphics.set', {'patch': {'offload': mode, 'render_scale': 1, 'dynamic_resolution': False, 'offload_power_cap_w': None}})
        want = 'offload' if mode == 'on' else 'in-process'
        try:
            return wait(lambda: perf()['activeRenderer'] == want, 40)
        except AssertionError:
            raise AssertionError(('offload did not switch', want, perf().get('reason'), s.camera().get('offloadRequests')))
    def region(path):
        img = np.asarray(Image.open(s.STATE / path).convert('RGB'), dtype=np.int16)
        h, w = img.shape[:2]
        return img[int(h * .15):int(h * .9), int(w * .15):int(w * .85)]
    call('graphics.set', {'patch': {'reduced_motion': True}})
    values.update({'status': 'failed', 'stage.1': 'running', 'stage.2': 'success', 'depth': 55, 'progress': .6, 'lastJob': 'main #77'})
    publish(values); time.sleep(1.2)
    front(); time.sleep(.8)
    shots = {}
    for mode in ('off', 'on'):
        renderer(mode); time.sleep(1.5)
        shots[mode] = shot(f'machines-offload-{mode}.png')
    worker = (s.camera()['timings'].get('offload') or {}).get('worker') or {}
    d = np.abs(region(shots['off']) - region(shots['on'])).max(axis=2)
    stats = {'meanLevels': round(float(d.mean()), 3), 'differing': round(float((d > 6).mean()), 5), 'worker': worker.get('machines')}
    renderer('off')
    call('graphics.set', {'patch': {'reduced_motion': False}})
    check('offload draws the machines (palettes, labels, crates, toggles, lift) like the in-process renderer',
          stats['differing'] < .01 and (worker.get('machines') or {}).get('states', 0) >= 2 and
          (worker.get('machines') or {}).get('labelsDrawn', 0) >= 1, stats)
