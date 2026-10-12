"""Machine data sources from the Director (RFC-0013 draft, IPC 0.24): dev/nested/run.sh --data-sources.

Nested only. Lumen Reach (HYPRUNE_GAUGES_WORLD, else HYPRUNE_CATALOG_WORLD, else the worlds
checkout's dist package) and a starter library archive with the gauges
(HYPRUNE_CATALOG_LIBRARY). The source helper starts the way the launcher starts it: with
its own com.hyprune.sources launch grant, no sources.json yet (it idles), and its control
socket beside core's. The checks drive it through the shell bridge's sources module, the
backend of Director > Objects > Placed > Data source (HYPRUNE_SHELL_DIR, default the
sibling shell checkout):
1. A placed gauge: catalog `machine` inputs, state `live` before any data.
2. Test runs the command once through the helper; nothing is published.
3. Save writes sources.json atomically; the idle helper picks it up and values arrive in
   `live` (no world reload, no restart).
4. Editing the interval changes the run cadence.
5. Restart re-runs the command at once; Reset average restarts the running average.
6. Remove deletes the entry; the source is withdrawn.
Evidence: docs/evidence/data-sources.json.
"""
import asyncio, hashlib, importlib.util, json, math, os, pathlib, secrets, shutil, signal, subprocess, sys, time
import session as s
from m2 import wait

WORLD = 'com.hyprune.lumen-reach'
LIBRARY = 'com.hyprune.starter'
ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_CATALOG_LIBRARY', ''))
HELPER_TOKEN = secrets.token_hex(32)


def grants():
    return [{'token': HELPER_TOKEN, 'client': 'com.hyprune.sources', 'capabilities': ['data.publish']}]


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_GAUGES_WORLD') or os.environ.get('HYPRUNE_CATALOG_WORLD') or
                          str(s.ROOT.parent / 'worlds/dist' / WORLD))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--data-sources needs Lumen Reach at {source} (HYPRUNE_GAUGES_WORLD)')
    if not ARCHIVE.is_file():
        raise SystemExit('--data-sources needs the starter library archive with the gauges (HYPRUNE_CATALOG_LIBRARY)')
    os.environ.pop('HYPRUNE_MACHINES_PROTOTYPE', None)
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir()
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'machines.json', 'sources.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)


def helper(socketpath):
    """As the launcher starts it: always, idle without a config, control socket beside core's.
    --dev lowers the command interval floor to 1 s so the cadence checks stay short."""
    token = s.STATE / 'helper.token'
    token.write_text(HELPER_TOKEN + '\n'); token.chmod(0o600)
    log = open(s.STATE / 'source-helper.log', 'w')
    return subprocess.Popen([sys.executable, str(s.ROOT / 'dev/machines/source-helper.py'), '--config', str(s.STATE / 'sources.json'),
                             '--socket', str(socketpath), '--token-file', str(token), '--dev', '-v'],
                            stdout=log, stderr=subprocess.STDOUT, start_new_session=True)


def bridge_sources():
    shell = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    spec = importlib.util.spec_from_file_location('shell_sources', shell / 'bridge/sources.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def acceptance(client, children, args, shell_token, socketpath):
    source = helper(socketpath)  # launch tokens expire 30 s after core starts
    try:
        checks(client, children, source, socketpath)
    finally:
        if source.poll() is None:
            source.send_signal(signal.SIGTERM)
            try:
                source.wait(timeout=5)
            except subprocess.TimeoutExpired:
                source.kill()
    client.call('runtime.exit', {})


def checks(client, children, source, socketpath):
    results = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS data sources:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    sources = bridge_sources()
    panel = sources.Sources(sources.control_path(str(socketpath)), str(s.STATE / 'sources.json'))
    loop = asyncio.new_event_loop()
    def ask(kind, **fields):  # one panel request, as QML sends it, and its answer event
        return loop.run_until_complete(panel.handle({'type': kind, **fields}))

    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60); time.sleep(1)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    def live(id_): return next((i for i in state()['live']['instances'] if i['id'] == id_), None)
    def inst(id_): return next((x for x in s.camera()['machines']['instances'] if x['id'] == id_), None)
    def world_loads():
        history = (s.camera().get('offloadRequests') or {}).get('history') or []
        return max([h['worldLoad'] for h in history if 'worldLoad' in h] or [0])

    archive = s.STATE / ARCHIVE.name
    version = client.call('libraries.install', {'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})['version']
    ref = f'library/{LIBRARY}@{version}/gauge-tube'
    entry = next(e for e in client.call('objects.catalog', {})['entries'] if e['ref'] == ref)
    placed = None
    for x in (-8, -6, -10, -4.5, -11, -12):
        for z in (5, 4, 6, 3):
            try:
                placed = client.call('objects.place', {'ref': ref, 'pose': {'position': [x, 0, z], 'rotation': [0, 0, 0, 1]}})['id']
                break
            except RuntimeError:
                continue
        if placed:
            break
    assert placed, 'no spot for the gauge'
    loads = wait(world_loads, 10)
    l0 = wait(lambda: live(placed), 20)
    check('IPC 0.24: the catalog declares the gauge inputs; live lists the placed gauge before any data',
          [i['id'] for i in entry['machine']['inputs']] == ['value', 'display', 'label', 'level', 'average'] and
          l0['source'] == f'com.hyprune.sources.{placed}' and l0['status'] is None and
          all(v['condition'] == 'missing' for v in l0['inputs'].values()),
          {'machine': entry['machine']['inputs'][0], 'live': {k: l0[k] for k in ('id', 'source', 'status')}})
    status = ask('sources.status')
    check('the helper runs idle without sources.json, connected, on its control socket',
          status['ok'] and status['status']['connected'] and status['status']['sources'] == {} and
          status['status']['config'] == str(s.STATE / 'sources.json') and not (s.STATE / 'sources.json').exists(),
          {'config': pathlib.Path(status['status']['config']).name, 'limits': status['status']['limits']['command']})

    # 2. Test: the command runs once; nothing is published.
    sim = str(s.ROOT / 'dev/machines/scripts/gauge-sim.py')
    argv = [sim, '--label', 'CPU', '--period', '2']
    test = ask('sources.test', id=placed, argv=argv)
    values = test['result']['values']
    time.sleep(1.5)
    check('Test runs the exact argv through the helper and shows its JSON; nothing is published',
          test['ok'] and test['result']['exitCode'] == 0 and set(values) >= {'value', 'display', 'label', 'level'} and
          values['label'] == 'CPU' and live(placed)['status'] is None,
          {'argv': argv[1:], 'values': values, 'durationMs': test['result']['durationMs']})

    # 3. Save: sources.json written atomically, the idle helper starts the source, values arrive.
    saved = ask('sources.save', id=placed, argv=argv, intervalS=1)
    written = json.loads((s.STATE / 'sources.json').read_text())
    check('Save writes the entry (0600, atomic) and the helper reloads at once',
          saved['ok'] and saved['reload']['ok'] and written == {'sources': [{'id': placed, 'kind': 'command', 'argv': argv, 'intervalS': 1}]} and
          oct((s.STATE / 'sources.json').stat().st_mode & 0o777) == '0o600' and
          not list(s.STATE.glob('.sources.json.*')),
          {'sources': [{**e, 'argv': [pathlib.Path(e['argv'][0]).name, *e['argv'][1:]]} for e in written['sources']]})
    arrived = wait(lambda: (lambda l: l if l['status'] == 'ok' and l['inputs']['value']['condition'] == 'fresh' else None)(live(placed)), 10)
    run = ask('sources.status')['status']['sources'][placed]['lastRun']
    check('values arrive in live without a world reload: the gauge shows the script',
          arrived['inputs']['label']['value'] == 'CPU' and arrived['inputs']['display']['raw'] == run['values']['display'] and
          inst(placed)['labels']['label'] == 'CPU' and world_loads() == loads,
          {'live': {k: v['value'] for k, v in arrived['inputs'].items()}, 'worldLoads': world_loads()})

    # 4. The interval: runs every 1 s, then every 4 s after the edit.
    def cadence(seconds):
        stamps, end = [], time.time() + seconds
        while time.time() < end:
            at = ask('sources.status')['status']['sources'][placed]['lastRun']['at']
            if not stamps or at != stamps[-1]:
                stamps.append(at)
            time.sleep(.2)
        gaps = [b - a for a, b in zip(stamps, stamps[1:])]
        return round(sum(gaps) / len(gaps) / 1000, 2) if gaps else None
    fast = cadence(5)
    edited = ask('sources.save', id=placed, argv=argv, intervalS=4)
    time.sleep(4.5)
    slow = cadence(13)
    check('editing the interval changes the run cadence (1 s -> 4 s), no restart',
          edited['ok'] and fast is not None and .8 <= fast <= 1.4 and slow is not None and 3.6 <= slow <= 4.6 and
          json.loads((s.STATE / 'sources.json').read_text())['sources'][0]['intervalS'] == 4,
          {'meanGapS': {'1': fast, '4': slow}})

    # 5. Restart: a fresh run now (well before the next 4 s run); Reset average restarts the ring.
    ring = next(d['target'].split(':', 1)[1] for d in json.loads(next((s.STATE / 'datahome').rglob('gauge-tube/object.json')).read_text())
                ['traits']['machine']['drives'] if (d.get('map') or {}).get('average'))
    time.sleep(2.5)  # past the window's first values: the average differs from the value
    before = ask('sources.status')['status']['sources'][placed]
    asked = time.time() * 1000
    restarted = ask('sources.restart', id=placed, resetAverage=True)
    after = wait(lambda: (lambda x: x if x['lastRun']['at'] > before['lastRun']['at'] else None)(ask('sources.status')['status']['sources'][placed]), 3)
    published = wait(lambda: (lambda l: l if l['inputs']['value']['publishedAt'] >= asked - 50 else None)(live(placed)), 3)
    time.sleep(.6)
    g = inst(placed)
    check('Restart re-runs the command at once; Reset average restarts the running average',
          restarted['ok'] and after['lastRun']['at'] - asked < 1500 and after['resets'] == 1 and
          after['lastRun']['values'].get('_reset') == 1 and
          abs(g['levels'][ring]['target'] - g['inputs']['value']['value'] / 100) < 1e-6 and world_loads() == loads,
          {'runAfterMs': round(after['lastRun']['at'] - asked), 'resets': after['resets'], 'ring': g['levels'][ring],
           'value': g['inputs']['value']['value']})

    # 6. Remove: the entry goes, the helper withdraws the source.
    removed = ask('sources.remove', id=placed)
    gone = wait(lambda: (lambda l: l if l['inputs']['value']['condition'] == 'missing' else None)(live(placed)), 5)
    check('Remove deletes the entry and the source is withdrawn (the gauge falls back to its defaults)',
          removed['ok'] and removed['removed'] and json.loads((s.STATE / 'sources.json').read_text()) == {'sources': []} and
          gone['status'] is None and placed not in ask('sources.status')['status']['sources'], {'live': gone['inputs']['value']})
    check('no world reload anywhere', world_loads() == loads, {'worldLoads': world_loads()})
    loop.close()
    (s.ROOT / 'docs/evidence/data-sources.json').write_text(json.dumps({'nestedOnly': True, 'world': WORLD, 'library': f'{LIBRARY}@{version}',
                                                                        'checks': results}, indent=2) + '\n')
    print('PASS data sources:', len(results), 'checks', flush=True)
