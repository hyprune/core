"""Director > Objects > Data sources with the real shell (data sources phase 2, RFC-0013 draft):
dev/nested/run.sh --data-sources-shell.

Nested only. Lumen Reach and the gauges library as --data-sources; the real shell
(HYPRUNE_SHELL_DIR, default the sibling shell checkout) at IPC 0.27 and the source helper as
the launcher starts it, with a secrets file in the state dir (never the keyring). A local test
API answers the queues' lag behind a bearer token. Checks, driven through the page:
1. The page lists no sources yet; New HTTP opens an editor.
2. Typed in: URL, an Authorization header from the secret ingest-token, the map (value from
   $.items[1].lag scaled 0..60, thresholds warn 50 / fail 90, a display template, the label
   from $.name). The page lists the secret as one it names.
3. Test before the secret is stored: the error names it.
4. The secret typed into its password field and stored in the file: 0600, the value in no
   answer or page state.
5. Test: HTTP 200, the raw body beside the mapped values and the gauge preview.
6. Save: sources.json has the entry (the secret by name only); the helper runs it.
7. No QML runtime errors.
Stills: data-sources-*.png in the state dir; evidence docs/evidence/data-sources-shell.json.
"""
import builtins, http.server, json, os, pathlib, secrets, subprocess, threading, time
import session as s
import datasources
from ctl import verified
from m2 import wait, keys

SHELL_TOKEN = secrets.token_hex(32)
OUTPUT = 'HEADLESS-1'


def grants():
    return datasources.grants() + [{'token': SHELL_TOKEN, 'client': 'com.hyprune.shell',
                                    'capabilities': ['state.read', 'world.control', 'surface.control', 'movement.control', 'shell.control',
                                                     'runtime.control', 'input.control', 'quest.control', 'objects.control']}]


def prepare(installed):
    datasources.prepare(installed)


def acceptance(client, children, args, shell_token, socketpath):
    source = datasources.helper(socketpath)  # launch tokens expire 30 s after core starts
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), datasources.Api)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        run(client, children, socketpath, f'http://127.0.0.1:{server.server_port}/lag')
    finally:
        server.shutdown()
        if source.poll() is None:
            source.terminate()
            source.wait(timeout=5)
    client.call('runtime.exit', {})


def run(client, children, socketpath, url):
    out = s.STATE
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'data-sources-shell-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS data-sources-shell:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    def shot(name):
        time.sleep(.8)
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=20)

    read, write = os.pipe(); os.write(write, (SHELL_TOKEN + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.27', HYPRUNE_SOCKET=str(socketpath),
                       XDG_DATA_HOME=os.environ['XDG_DATA_HOME'], HYPRUNE_OUTPUT_SCREEN=OUTPUT, HYPRUNE_DEV_AUTOMATION='1',
                       HYPRUNE_SHELL_PREFERENCES=str(s.STATE / 'shell-preferences.json'))
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell_log = s.STATE / 'shell.log'
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', '0.27'], env=environment,
                             pass_fds=(read,), stdout=open(shell_log, 'w'), stderr=subprocess.STDOUT)
    os.close(read); children.append(shell)

    def childpid():
        assert shell.poll() is None, 'shell exited; see shell.log'
        return builtins.next((int(q) for q in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
                              if pathlib.Path(f'/proc/{q}/comm').read_text().strip() == 'qs'), None)
    qpid = wait(childpid, 30)

    def ui(*a):
        return subprocess.check_output([str(s.HERE / 'ctl.py'), 'env', 'qs', 'ipc', '--pid', str(qpid), 'call', 'preview', *map(str, a)],
                                       text=True).strip()

    def status():
        try:
            return json.loads(ui('status'))
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            return {}

    def page():
        return status().get('dataSources') or {}

    def press(name):
        assert ui('press', name) == 'true', name

    def field(name, text):
        assert ui('fieldText', name, text) == 'true', name

    s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{OUTPUT}" }}))')
    client.call('world.load', {'worldId': datasources.WORLD})
    s.ctl('eval', f'hl.plugin.hyprune.toggle("{OUTPUT}")'); wait(lambda: s.camera()['active'], 60)
    wait(lambda: status().get('protocol') == '0.27' and status().get('lease'), 30)
    keys('-P', 'Tab', '-s', '1150', '-p', 'Tab'); wait(lambda: status().get('director'), 10)
    s.ctl('eval', 'hl.dispatch(hl.dsp.cursor.move({x=4,y=4}))')
    ui('tabNamed', 'objects'); wait(lambda: status().get('tabName') == 'objects')
    ui('objectsView', 'sources'); wait(lambda: page().get('visible'), 10)

    # 1. Empty, then New HTTP.
    time.sleep(1.5)
    check('the page lists no sources yet', page()['rows'] == [] and page()['selected'] == '', page())
    shot('data-sources-empty.png')
    press('sources-new-http'); wait(lambda: page().get('selected') == 'http-1', 5)
    check('New HTTP opens an editor for a new source', page()['fresh'] == 'http' and page()['dirty'], {k: page()[k] for k in ('selected', 'fresh')})

    # 2. The definition and the map, typed in.
    for name, text in [('source-id', 'ingest'), ('source-url', url)]:
        field(name, text)
    press('source-add-header')
    wait(lambda: ui('fieldText', 'header-name:0', 'Authorization') == 'true', 5)
    field('header-prefix:0', 'Bearer ')
    field('header-secret:0', 'ingest-token')
    for name, text in [('map-value', '$.items[1].lag'), ('map-max', '60'), ('map-display', '{raw:d} s behind'), ('map-label', '$.name')]:
        field(name, text)
    press('segment:thresholds')
    wait(lambda: ui('fieldText', 'map-warn', '50') == 'true', 5)
    field('map-fail', '90')
    wait(lambda: page().get('names') == ['ingest-token'], 5)
    check('the editor names the secret it needs', page()['names'] == ['ingest-token'], page()['names'])

    # 3. Test before the secret exists.
    press('source-test')
    missing = wait(lambda: (page().get('test') or None), 20)
    check('Test before the secret is stored: the error names it', missing['error'] == "secret 'ingest-token' is not set" and not missing['ok'],
          {'error': missing['error']})
    shot('data-sources-missing-secret.png')

    # 4. Store the secret from its password field.
    field('secret-value:ingest-token', datasources.TOKEN)
    press('secret-file:ingest-token')
    stored = wait(lambda: (lambda f: f if f and 'Stored secret' in f.get('text', '') else None)(page().get('feedback')), 10)
    path = s.STATE / 'secrets.json'
    check('the secret goes to the secrets file (0600); no page state or answer holds it',
          oct(path.stat().st_mode & 0o777) == '0o600' and json.loads(path.read_text()) == {'ingest-token': datasources.TOKEN} and
          datasources.TOKEN not in json.dumps(status()), {'feedback': stored['text']})

    # 5. Test: raw response and mapped values.
    press('source-test')
    test = wait(lambda: (lambda t: t if t and t.get('ok') else None)(page().get('test')), 20)
    rows = {r['key']: r['value'] for r in test['rows']}
    check('Test answers HTTP 200: the raw body beside the mapped value, level, display, label and the gauge preview',
          test['line'].startswith('HTTP 200') and '"ingest"' in test['raw'] and rows['value'] == '66.67' and rows['level'] == 'warn' and
          rows['display'] == '40 s behind' and rows['label'] == 'ingest' and test['preview']['level'] == 'warn',
          {'line': test['line'], 'rows': rows})
    s.ctl('eval', 'hl.dispatch(hl.dsp.cursor.move({x=4,y=4}))')
    shot('data-sources-test.png')

    # 6. Save.
    press('source-save')
    wait(lambda: (lambda f: f if f and f.get('text', '').startswith('Saved') else None)(page().get('feedback')), 10)
    written = json.loads((s.STATE / 'sources.json').read_text())
    entry = written['sources'][0]
    ran = wait(lambda: (lambda r: r if r and r['state'] == 'ok' else None)(next((x for x in page()['rows'] if x['id'] == 'ingest'), None)), 15)
    check('Save writes the entry (the secret by name only) and the helper runs it',
          entry['id'] == 'ingest' and entry['url'] == url and entry['headers'] == {'Authorization': {'secret': 'ingest-token', 'prefix': 'Bearer '}} and
          entry['map']['level'] == {'warn': 50, 'fail': 90} and datasources.TOKEN not in json.dumps(written) and ran['state'] == 'ok',
          {'entry': {k: v for k, v in entry.items() if k != 'url'}})
    shot('data-sources-saved.png')

    # 7. QML runtime errors.
    log = shell_log.read_text(errors='replace')
    problems = [l for l in log.splitlines() if ('qml' in l.lower() and ('error' in l.lower() or 'typeerror' in l.lower() or 'referenceerror' in l.lower()))]
    check('no QML runtime errors', not problems, problems[:5])
    (s.ROOT / 'docs/evidence/data-sources-shell.json').write_text(json.dumps({'nestedOnly': True, 'checks': checks}, indent=1) + '\n')
    print('PASS data-sources-shell:', len(checks), 'checks', flush=True)
