"""Import a model from the Director (RFC-0014 as amended, IPC 0.27): dev/nested/run.sh --import-shell.

Nested only. Lumen Reach, the real shell at IPC 0.27 (HYPRUNE_SHELL_DIR, default the shell
checkout) and the SDK's hyprune CLI (HYPRUNE_CLI, default node <sdk>/dist/src/cli.js) with
its CC0 import fixture (HYPRUNE_IMPORT_FIXTURE, default <sdk>/test/fixtures/import/barrel).
Checks:
 1. Objects > Import… opens the file browser; picking barrel.gltf by keyboard measures it
    (12,544 triangles, centimetres guessed).
 2. In the review form, Z up and a 5,000-triangle target typed into the field; Import runs
    the stages and writes user/barrel (decimated, KTX2, convex proxy, import.json).
 3. Core's catalog lists it within seconds, without a reload, with provenance and imported.
 4. Add to pocket, then tool 5 places it; it stands upright, 1.2 m tall, on the floor.
 5. Its collision proxy is world collision: a second barrel at the same pose is refused
    (rule walls); one beside it is placed.
 6. No QML runtime errors.
Stills: import-*.png in the state dir; evidence docs/evidence/import-shell.json.
"""
import builtins, json, math, os, pathlib, secrets, shutil, subprocess, time
import session as s
from ctl import verified
from m2 import wait, keys

LUMEN = 'com.hyprune.lumen-reach'
SHELL_TOKEN = secrets.token_hex(32)
OUTPUT = 'HEADLESS-1'
SDK = pathlib.Path(os.environ.get('HYPRUNE_SDK_DIR', str(s.ROOT.parent / 'sdk')))
FIXTURE = pathlib.Path(os.environ.get('HYPRUNE_IMPORT_FIXTURE', str(SDK / 'test/fixtures/import/barrel')))
CLI = os.environ.get('HYPRUNE_CLI', f'node {SDK}/dist/src/cli.js')


def grants():
    return [{'token': SHELL_TOKEN, 'client': 'com.hyprune.shell',
             'capabilities': ['state.read', 'world.control', 'surface.control', 'movement.control', 'shell.control',
                              'runtime.control', 'input.control', 'quest.control', 'objects.control']}]


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--import-shell needs {LUMEN} at {source}')
    if not (FIXTURE / 'barrel.gltf').is_file():
        raise SystemExit(f'--import-shell needs the SDK import fixture at {FIXTURE} (HYPRUNE_IMPORT_FIXTURE)')
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(source, installed / LUMEN)
    data = s.STATE / 'datahome'
    shutil.rmtree(data, ignore_errors=True)
    (data / 'hyprune/objects').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(data)
    models = s.STATE / 'models'
    shutil.rmtree(models, ignore_errors=True)
    shutil.copytree(FIXTURE, models / 'barrel')
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    out = s.STATE
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'import-shell-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS import-shell:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    def shot(name):
        time.sleep(.8)
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=20)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    read, write = os.pipe(); os.write(write, (SHELL_TOKEN + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.27', HYPRUNE_SOCKET=str(socketpath), HYPRUNE_CLI=CLI,
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

    def job():
        return (status().get('objects') or {}).get('importJob') or {}

    def press(name):
        return ui('press', name) == 'true'

    s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{OUTPUT}" }}))')
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', f'hl.plugin.hyprune.toggle("{OUTPUT}")'); wait(lambda: s.camera()['active'], 60)
    wait(lambda: status().get('protocol') == '0.27' and status().get('lease'), 30)

    def director():
        keys('-P', 'Tab', '-s', '1150', '-p', 'Tab'); wait(lambda: status().get('director'), 10)
        s.ctl('eval', 'hl.dispatch(hl.dsp.cursor.move({x=4,y=4}))')

    # 1. Import… and the file browser.
    director()
    ui('tabNamed', 'objects'); wait(lambda: status().get('tabName') == 'objects')
    assert press('object-import')
    wait(lambda: job().get('phase') == 'browse', 10)
    # The folder lists asynchronously ("Loading…" first, longer under load): keys sent before
    # it lists go nowhere. Open it again and repeat until the pick leaves the browser.
    for attempt in range(6):
        assert ui('importBrowse', str(s.STATE / 'models/barrel')) == 'true'
        time.sleep(1 + attempt)
        if attempt == 0:
            shot('import-browse.png')
        keys('-k', 'Down', '-k', 'Return')  # barrel.bin, then barrel.gltf (bin can't be picked)
        try:
            wait(lambda: job().get('phase') in ('inspecting', 'review', 'failed'), 5)
            break
        except AssertionError:
            pass
    # A failed inspection (e.g. an SDK CLI without `object import --inspect`) ends the wait with its error.
    wait(lambda: job().get('phase') in ('review', 'failed'), 30)
    assert job().get('phase') == 'review', ('the inspection failed', CLI, job().get('error'))
    j = job()
    check('the browser picks barrel.gltf and the review shows its measures', j['path'].endswith('barrel.gltf') and
          j['inspection'] == {'triangles': 12544, 'unitsGuess': 'cm'}, {'path': j['path'], 'inspection': j['inspection']})

    # 2. Review: Z up and a typed 5,000-triangle target; Import.
    # The author starts as the user's git name (or empty), never the project's name.
    expected = subprocess.run(['git', 'config', '--get', 'user.name'], capture_output=True, text=True, cwd=os.environ.get('HOME', '/')).stdout.strip()
    check('the author field starts as the user\'s git name or empty', job()['form']['author'] == expected and expected != 'Hyprune contributors',
          {'author': job()['form']['author']})
    ui('importPatch', json.dumps({'up': 'z', 'licence': 'CC0-1.0'}))
    assert ui('focusNamed', 'import-triangles') == 'true'
    keys('-M', 'ctrl', '-k', 'a', '-m', 'ctrl'); keys('5000')
    wait(lambda: job()['form']['maxTriangles'] == '5000', 5)
    shot('import-review.png')
    start = time.monotonic()
    assert press('import-run')
    wait(lambda: job().get('phase') in ('done', 'failed'), 120)
    j = job()
    check('Import runs its stages and writes user/barrel', j['phase'] == 'done' and j['ref'] == 'user/barrel' and
          {'read', 'fit', 'simplify', 'ktx2', 'compress', 'check'} <= set(j['stages']),
          {'phase': j['phase'], 'stages': j['stages'], 'error': j.get('error'), 'seconds': round(time.monotonic() - start, 1)})
    folder = pathlib.Path(os.environ['XDG_DATA_HOME']) / 'hyprune/objects/barrel'
    manifest = json.loads((folder / 'object.json').read_text())
    record = json.loads((folder / 'import.json').read_text())
    check('the object folder has object.json (0.11), import.json and a thumbnail', manifest['formatVersion'] == '0.11' and
          record['options']['up'] == 'z' and record['license']['spdx'] == 'CC0-1.0' and (folder / 'thumbnail.png').is_file(),
          {'files': sorted(p.name for p in folder.iterdir()), 'bounds': manifest['bounds'], 'steps': record['steps']})

    # 3. Core lists it without a reload.
    done_at = time.monotonic()
    wait(lambda: job().get('listed'), 20)
    entry = next(e for e in client.call('objects.catalog', {})['entries'] if e['ref'] == 'user/barrel')
    check('core\'s catalog lists it within seconds, placeable, with provenance and imported',
          entry['placeable'] and entry['state'] == 'ready' and entry['provenance']['license'] == 'CC0-1.0' and
          entry['provenance']['author'] == (expected or 'unknown') and
          entry['imported']['sourceName'] == 'barrel.gltf' and entry['triangles'] <= 5000 and entry['collisionTriangles'] > 12,
          {'seconds': round(time.monotonic() - done_at, 1), 'triangles': entry['triangles'], 'collision': entry['collisionTriangles'],
           'warnings': entry['warnings'], 'provenance': entry['provenance'], 'imported': entry['imported']})
    shot('import-done.png')

    # 4. Pocket, then tool 5.
    assert press('import-pocket')
    wait(lambda: any(p['ref'] == 'user/barrel' for p in state()['objects']['pocket']), 10)
    keys('-k', 'Escape'); wait(lambda: not status().get('director'), 10)
    placed = None
    position = s.camera()['position']
    for yaw in [0, .7, -.7, 1.4, -1.4, 3.1]:
        s.ctl('eval', f'hl.plugin.hyprune.camera({position[0]},{position[1]},{position[2]},{yaw},-.35)'); time.sleep(.6)
        ghost = s.camera().get('objects', {}).get('ghost', {})
        if ghost.get('active') and ghost.get('valid'):
            for state_ in ('1', '0'):
                subprocess.run([str(s.HERE / 'ctl.py'), 'env', str(s.ROOT / 'build/nested-pointer'), 'button', '272', state_], check=True)
                time.sleep(.12)
            try:
                placed = wait(lambda: next((i for i in state()['objects']['instances'] if i['ref'] == 'user/barrel'), None), 5)
                break
            except AssertionError:
                continue
    check('tool 5 places the imported barrel', placed is not None, placed and placed['pose'])
    instance = wait(lambda: (lambda i: i if i and i['state'] == 'active' else None)(
        next((i for i in state()['objects']['instances'] if i['id'] == placed['id']), None)), 20)
    check('it is active and upright', instance['state'] == 'active' and abs(instance['pose']['rotation'][0]) < 1e-3 and abs(instance['pose']['rotation'][2]) < 1e-3,
          instance['pose'])
    p = instance['pose']['position']
    d = [p[0] - position[0], p[2] - position[2]]
    n = max(1e-6, (d[0] ** 2 + d[1] ** 2) ** .5)
    eye = [p[0] - d[0] / n * 2.4, 1.3 + p[1], p[2] - d[1] / n * 2.4]
    yaw = math.atan2(d[0] / n, -d[1] / n)  # looking along the player-to-barrel direction
    s.ctl('eval', f'hl.plugin.hyprune.camera({eye[0]},{eye[1]},{eye[2]},{yaw},-.25)')
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    time.sleep(1)
    shot('import-placed.png')

    # 5. Collision: the proxy is world collision.
    try:
        client.call('objects.place', {'ref': 'user/barrel', 'pose': instance['pose']})
        refused = None
    except RuntimeError as e:
        refused = e.args[0]
    check('a second barrel at the same pose is refused by the first one\'s proxy', refused and refused['code'] == -32007 and refused['data']['rule'] == 'walls', refused)
    beside = None
    for dx, dz in [(1.2, 0), (-1.2, 0), (0, 1.2), (0, -1.2)]:
        try:
            beside = client.call('objects.place', {'ref': 'user/barrel', 'pose': {'position': [p[0] + dx, p[1], p[2] + dz], 'rotation': instance['pose']['rotation']}})['id']
            break
        except RuntimeError:
            continue
    check('one beside it is placed', beside is not None, beside)
    time.sleep(1)
    shot('import-two-barrels.png')
    log = shell_log.read_text()
    bad = [x for x in ['ReferenceError', 'TypeError', 'Binding loop', 'Unable to assign', 'does not match the negotiated protocol'] if x in log]
    check('no QML runtime errors in the shell log', not bad, bad)
    evidence = s.ROOT / 'docs/evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'import-shell.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
    print(f'PASS import-shell: {len(checks)} checks', flush=True)
