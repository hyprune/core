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
Phase 2 (Director > Objects > Data sources; an HTTP API on a local test server):
7. Test of an HTTP source whose bearer token is a secret not stored yet: the error names it
   and the page sees it as missing; nothing is published.
8. Storing the secret (the secrets file, 0600; never the keyring here): Test answers HTTP 200
   with the raw body and the mapped value, level, display and label.
9. Save (a whole entry: method, headers with the secret, map) on the gauge's source: the
   gauge shows the mapped values; the API's change moves its level to fail.
10. A command source with number output and a map feeds the gauge the same way.
Evidence: docs/evidence/data-sources.json.
"""
import asyncio, hashlib, http.server, importlib.util, json, math, os, pathlib, secrets, shutil, signal, subprocess, sys, threading, time
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
    for name in ['editor.json', 'layout.json', 'input.json', 'machines.json', 'sources.json', 'secrets.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)


def helper(socketpath):
    """As the launcher starts it: always, idle without a config, control socket beside core's.
    --dev lowers the command interval floor to 1 s so the cadence checks stay short."""
    token = s.STATE / 'helper.token'
    token.write_text(HELPER_TOKEN + '\n'); token.chmod(0o600)
    log = open(s.STATE / 'source-helper.log', 'w')
    return s.helper_connected(subprocess.Popen([sys.executable, str(s.ROOT / 'dev/machines/source-helper.py'), '--config', str(s.STATE / 'sources.json'),
                             '--socket', str(socketpath), '--token-file', str(token), '--dev', '-v',
                             '--secrets', str(s.STATE / 'secrets.json'), '--no-keyring'],
                            stdout=log, stderr=subprocess.STDOUT, start_new_session=True), s.STATE / 'source-helper.log')


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
    phase2(ask, check, live, inst, placed)
    check('no world reload anywhere', world_loads() == loads, {'worldLoads': world_loads()})
    loop.close()
    (s.ROOT / 'docs/evidence/data-sources.json').write_text(json.dumps({'nestedOnly': True, 'world': WORLD, 'library': f'{LIBRARY}@{version}',
                                                                        'checks': results}, indent=2) + '\n')
    print('PASS data sources:', len(results), 'checks', flush=True)


TOKEN = 'nested-' + secrets.token_hex(8)


class Api(http.server.BaseHTTPRequestHandler):
    """A local ingest API: GET /lag with Authorization: Bearer TOKEN answers the queues' lag."""
    lag = [3, 40]

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            body, status = b'{"error": "bad token"}', 401
        else:
            body = json.dumps({'name': 'ingest', 'items': [{'state': 'ok' if l < 30 else 'late', 'lag': l} for l in Api.lag]}).encode()
            status = 200
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def phase2(ask, check, live, inst, placed):
    """Data sources phase 2 through the bridge, as Director > Objects > Data sources sends it."""
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Api)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        url = f'http://127.0.0.1:{server.server_port}/lag'
        entry = {'id': placed, 'kind': 'http', 'url': url, 'headers': {'Authorization': {'secret': 'ingest-token', 'prefix': 'Bearer '}},
                 'intervalS': 1, 'map': {'value': {'path': '$.items[*].lag', 'reduce': 'max', 'max': 60},
                                         'level': {'warn': 50, 'fail': 90}, 'display': '{raw:d} s behind', 'label': {'path': '$.name'}}}
        definition = {k: v for k, v in entry.items() if k != 'id'}
        missing = ask('sources.testSource', source=definition)
        check('Test with a secret not stored: the error names it, the page sees it missing, nothing publishes',
              missing['ok'] and missing['result']['error'] == "secret 'ingest-token' is not set" and
              missing['result']['secrets'] == {'ingest-token': None} and live(placed)['status'] is None, missing['result'])
        stored = ask('sources.secretSet', name='ingest-token', value=TOKEN, store='file')
        path = s.STATE / 'secrets.json'
        test = ask('sources.testSource', source=definition)
        r = test['result']
        check('the secret goes to the secrets file (0600), never into an answer; Test answers HTTP 200, raw body and mapped values',
              stored['ok'] and TOKEN not in json.dumps(stored) and oct(path.stat().st_mode & 0o777) == '0o600' and
              json.loads(path.read_text()) == {'ingest-token': TOKEN} and r['httpStatus'] == 200 and '"ingest"' in r['raw'] and
              r['values'] == {'name': 'ingest', 'value': 66.67, 'level': 'warn', 'display': '40 s behind', 'label': 'ingest'} and
              r['secrets'] == {'ingest-token': 'file'} and TOKEN not in json.dumps(r),
              {k: r[k] for k in ('httpStatus', 'contentType', 'values', 'secrets', 'durationMs')})
        saved = ask('sources.put', entry=entry)
        written = json.loads((s.STATE / 'sources.json').read_text())
        arrived = wait(lambda: (lambda l: l if l['status'] == 'ok' and l['inputs']['level']['value'] == 'warn' else None)(live(placed)), 10)
        check('Save writes the whole entry (the secret by name only) and the gauge shows the mapped values',
              saved['ok'] and saved['reload']['ok'] and written['sources'] == [entry] and TOKEN not in json.dumps(written) and
              arrived['inputs']['value']['value'] == 66.67 and arrived['inputs']['display']['value'] == '40 s behind' and
              arrived['inputs']['label']['value'] == 'ingest' and inst(placed)['labels']['label'] == 'ingest',
              {'live': {k: v['value'] for k, v in arrived['inputs'].items()}})
        Api.lag = [3, 58]
        failing = wait(lambda: (lambda l: l if l['inputs']['level']['value'] == 'fail' else None)(live(placed)), 10)
        check('the API changes: the level moves to fail (58 s of 60 = 96.67 >= 90)',
              failing['inputs']['value']['value'] == 96.67 and failing['inputs']['display']['value'] == '58 s behind',
              {'live': {k: v['value'] for k, v in failing['inputs'].items()}})
        sim = str(s.ROOT / 'dev/machines/scripts/gauge-sim.py')
        command = {'id': placed, 'kind': 'command', 'argv': ['sh', '-c', 'echo 1500'], 'output': 'number', 'intervalS': 1,
                   'url': None, 'headers': None,
                   'map': {'value': {'field': 'value', 'max': 3000}, 'level': {'warn': 40}, 'display': '{raw:d} rpm', 'label': 'Fan'}}
        put = ask('sources.put', entry=command, previous=placed)
        fan = wait(lambda: (lambda l: l if l['inputs']['display']['value'] == '1500 rpm' else None)(live(placed)), 10)
        check('a command source with number output and a map feeds the gauge too (the entry replaced in place)',
              put['ok'] and fan['inputs']['value']['value'] == 50 and fan['inputs']['level']['value'] == 'warn' and
              fan['inputs']['label']['value'] == 'Fan' and
              json.loads((s.STATE / 'sources.json').read_text())['sources'] == [{k: v for k, v in command.items() if v is not None}],
              {'live': {k: v['value'] for k, v in fan['inputs'].items()}, 'sim': pathlib.Path(sim).name})
    finally:
        server.shutdown()
