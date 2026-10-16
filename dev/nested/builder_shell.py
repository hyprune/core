"""Build a world from the Director (RFC-0014 as amended, IPC 0.31, world format 0.11):
dev/nested/run.sh --builder-shell.

Nested only. Lumen Reach to start from, the real shell at IPC 0.31 (HYPRUNE_SHELL_DIR, default
the shell checkout), the SDK's hyprune CLI (HYPRUNE_CLI, default node <sdk>/dist/src/cli.js),
an imported object in the user folder (core's tests/fixtures/imported/barrel), a workspace
screen (HYPRUNE_SCREENS_DIR, default core's stand-ins as for --screens) and a library
(tests/fixtures/catalog/libraries/com.example.kit@1.0.0). World folders go to
$XDG_DATA_HOME/hyprune/projects in the state dir.
Checks:
 1. Director > Builder > New world: the wizard (name, the sunset sky, the grass floor every 3 m,
    facing the sun) makes a git repository with one commit (the Open Plain starter), and the shell
    warps into it in editor mode (state.builder, the panorama sky).
 2. Placing an imported object, a workspace screen and a library object copies each into the
    folder's objects/ (refs world/<id>); the placements go to builder/project.json, never to
    editor.json; the screen is a home for workspace 21; the page counts unsaved changes.
 3. Save with a message makes one commit; world.json holds the three objects and the home.
 4. A move, then Save with no message: a second commit with the default message.
 5. Release: an annotated tag v0.1.0 and nothing pending (no remote: tag only).
 6. Reload from the folder (Lumen Reach, then Builder > My worlds > Edit): the same placements,
    poses and screen binding.
 7. The released package (git archive v0.1.0) passes the SDK's validator and hyprune-world-check.
 8. The page's sky and floor tiles (paving, night) apply, reload the world, keep the placements.
    8b. The checklist (Phase D): the blocking items done; a spawn moved into the air (CLI) opens
    spawn.floor; after Save, Release is refused (release.check) and Release anyway tags v0.1.1;
    the spawn back closes it; Placed > Model turns the imported barrel a quarter.
 9. Shapes (IPC 0.31): a glowing box made from Objects > Shapes is pocketed, placed with a key
    (dry run first; a taken key is refused with its id; ifExists update moves it); stairs from
    the CLI stand on the floor; a cube inside the box is refused (its proxy collides); a material
    made in the editor and changed rebuilds its box and swaps the placement (same id and key);
    the Placed panel resizes a shape and renames its key; removing a missing key succeeds;
    Save prunes the unplaced shapes and keeps the keys.
10. Behaviours: a textured cube grows with CPU use (command source, behaviour added from Placed),
    a glowing bar follows a JSON API (HTTP source with a map); the bar's height on screen follows.
11. No QML runtime errors.
Stills: builder-*.png in the state dir (builder-wizard-*.png the wizard's steps, builder-sky.png looks
towards the sunset); evidence docs/evidence/builder-shell.json.
"""
import builtins, http.server, json, os, pathlib, secrets, shutil, signal, socket, subprocess, sys, tempfile, threading, time
import session as s
from ctl import verified
from m2 import wait, keys
from screens import screens_dir

LUMEN = 'com.hyprune.lumen-reach'
SHELL_TOKEN = secrets.token_hex(32)
HELPER_TOKEN = secrets.token_hex(32)
OUTPUT = 'HEADLESS-1'
SDK = pathlib.Path(os.environ.get('HYPRUNE_SDK_DIR', str(s.ROOT.parent / 'sdk')))
CLI = os.environ.get('HYPRUNE_CLI', f'node {SDK}/dist/src/cli.js')
SCREEN = 'workspace-screen-stand-16x9'
LIBRARY = 'com.example.kit@1.0.0'
NAME = 'Harbour Test'
SLUG = 'harbour-test'


def grants():
    return [{'token': SHELL_TOKEN, 'client': 'com.hyprune.shell',
             'capabilities': ['state.read', 'world.control', 'surface.control', 'movement.control', 'shell.control',
                              'runtime.control', 'input.control', 'quest.control', 'objects.control']},
            {'token': HELPER_TOKEN, 'client': 'com.hyprune.sources', 'capabilities': ['data.publish']}]


def helper(socketpath):
    """The source helper on STATE/sources.json (empty until step 10), with its own launch grant."""
    (s.STATE / 'sources.json').write_text(json.dumps({'devFastIntervals': True, 'sources': []}, indent=1) + '\n')
    token = s.STATE / 'helper.token'
    token.write_text(HELPER_TOKEN + '\n'); token.chmod(0o600)
    log = open(s.STATE / 'source-helper.log', 'w')
    return s.helper_connected(subprocess.Popen([sys.executable, str(s.ROOT / 'dev/machines/source-helper.py'), '--config', str(s.STATE / 'sources.json'),
                             '--socket', str(socketpath), '--token-file', str(token), '-v'],
                            stdout=log, stderr=subprocess.STDOUT, start_new_session=True), s.STATE / 'source-helper.log')


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--builder-shell needs {LUMEN} at {source}')
    screens = screens_dir()
    if not (screens / SCREEN / 'object.json').is_file():
        raise SystemExit(f'--builder-shell needs the screen object {SCREEN} at {screens} (HYPRUNE_SCREENS_DIR)')
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(source, installed / LUMEN)
    data = s.STATE / 'datahome'
    shutil.rmtree(data, ignore_errors=True)
    shutil.copytree(s.ROOT / 'tests/fixtures/imported/barrel', data / 'hyprune/objects/barrel')
    shutil.copytree(screens / SCREEN, data / 'hyprune/objects' / SCREEN)
    shutil.copytree(s.ROOT / 'tests/fixtures/catalog/libraries' / LIBRARY, data / 'hyprune/libraries' / LIBRARY)
    os.environ['XDG_DATA_HOME'] = str(data)
    os.environ.pop('HYPRUNE_PROJECTS_DIR', None)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    # Launch tokens are one-use and expire soon after core starts: the helper connects first.
    children.append(helper(socketpath))
    out = s.STATE
    checks = []
    folder = pathlib.Path(os.environ['XDG_DATA_HOME']) / 'hyprune/projects' / SLUG

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'builder-shell-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS builder-shell:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    def shot(name):
        time.sleep(.8)
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=20)

    def git(*a):
        return subprocess.check_output(['git', '-C', str(folder), *a], text=True).strip()

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    read, write = os.pipe(); os.write(write, (SHELL_TOKEN + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.31', HYPRUNE_SOCKET=str(socketpath), HYPRUNE_CLI=CLI,
                       XDG_DATA_HOME=os.environ['XDG_DATA_HOME'], HYPRUNE_OUTPUT_SCREEN=OUTPUT, HYPRUNE_DEV_AUTOMATION='1',
                       HYPRUNE_SHELL_PREFERENCES=str(s.STATE / 'shell-preferences.json'))
    environment.pop('HYPRUNE_PROJECTS_DIR', None)
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell_log = s.STATE / 'shell.log'
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', '0.31'], env=environment,
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

    def builder():
        return status().get('builder') or {}

    def job():
        return builder().get('job') or {}

    def press(name):
        return ui('press', name) == 'true'

    def director():
        # After a world load the hold can land in the warp transition: try again.
        wait(lambda: s.camera()['active'], 30)
        for attempt in range(4):
            if status().get('director'):
                break
            keys('-P', 'Tab', '-s', '1150', '-p', 'Tab')
            try:
                wait(lambda: status().get('director'), 6)
            except AssertionError:
                time.sleep(1.5)
        if not status().get('director'):
            st = status()
            core = state()
            (out / 'builder-director-status.json').write_text(json.dumps({'shell': {k: st.get(k) for k in st if k not in ('objects', 'npcs', 'quests')},
                                                                           'core': {k: core.get(k) for k in ('shell', 'keymap', 'location', 'runtime', 'world', 'interaction')}}, indent=1, default=str))
            shot('builder-director-failed.png')
        assert status().get('director'), 'the Director did not open'
        s.ctl('eval', 'hl.dispatch(hl.dsp.cursor.move({x=4,y=4}))')

    def close_director():
        if status().get('director'):
            keys('-k', 'Escape'); wait(lambda: not status().get('director'), 10)

    def run_job(kind, button, seconds=60):
        assert press(button), button
        wait(lambda: job().get('kind') == kind and job().get('phase') in ('done', 'failed'), seconds)
        return job()

    s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{OUTPUT}" }}))')
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', f'hl.plugin.hyprune.toggle("{OUTPUT}")'); wait(lambda: s.camera()['active'], 60)
    wait(lambda: status().get('protocol') == '0.31' and status().get('lease'), 30)

    # 1. New world: the wizard (name, sky, floor, facing), then create and warp in.
    director()
    ui('tabNamed', 'builder'); wait(lambda: status().get('tabName') == 'builder')
    assert press('builder-new')
    wait(lambda: (builder().get('wizard') or {}).get('step') == 0, 10)
    assert ui('fieldText', 'builderName', NAME) == 'true'
    time.sleep(.3)
    shot('builder-wizard-name.png')
    assert press('builder-wizard-next')
    wait(lambda: len(builder().get('skies') or []) == 4, 60)
    assert press('builder-sky-preset:sunset')
    time.sleep(1)
    shot('builder-wizard-sky.png')
    assert press('builder-wizard-next')
    wait(lambda: len(builder().get('floors') or []) == 5, 60)
    assert press('builder-floor-preset:grass')
    assert ui('fieldText', 'builderTile', '3') == 'true'
    time.sleep(1)
    shot('builder-wizard-floor.png')
    assert press('builder-wizard-next')
    assert press('builder-facing:sun')
    time.sleep(.3)
    shot('builder-wizard-start.png')
    wizard = builder().get('wizard')
    start = time.monotonic()
    j = run_job('create', 'builder-create')
    wait(lambda: (state().get('builder') or {}).get('projectId') == SLUG and (state().get('world') or {}).get('id') == f'local.{SLUG}', 60)
    b = state()['builder']
    commits = git('log', '--format=%s').split('\n')
    project = json.loads((folder / 'builder/project.json').read_text())
    check('New world makes a git repository from Open Plain with the chosen sky, floor and facing, and warps into it in editor mode',
          j['phase'] == 'done' and commits == [f'Create {NAME} from the Open Plain starter'] and b['mode'] == 'edit' and
          b['folder'] == str(folder) and b['placements'] == 0 and b['sky'] == 'sky/panorama.jpg' and
          project['starter'] == 'open-plain@0.2' and project['sky']['preset'] == 'sunset' and
          project['floor']['preset'] == 'grass' and project['floor']['tile'] == 3,
          {'seconds': round(time.monotonic() - start, 1), 'commits': commits, 'builder': b, 'wizard': wizard,
           'sky': project['sky'], 'floor': project['floor'], 'files': git('ls-files').split('\n')})
    close_director()
    wait(lambda: not status().get('warp') and status().get('phase') == 'arrived', 30)
    spawn = json.loads((folder / 'builder/base.json').read_text())['spawns'][0]['pose']
    yaw = -2 * __import__('math').atan2(spawn['rotation'][1], spawn['rotation'][3])  # the camera's heading (+yaw turns right)
    s.ctl('eval', f'hl.plugin.hyprune.camera(0,1.7,0,{yaw:.3f},.02)')
    shot('builder-world.png')
    # Towards the sunset (the spawn faces the sun) and down at the grass.
    s.ctl('eval', f'hl.plugin.hyprune.camera(0,1.7,0,{yaw:.3f},.12)')
    shot('builder-sky.png')
    s.ctl('eval', f'hl.plugin.hyprune.camera(0,1.7,0,{yaw + 3.14:.3f},-.5)')
    shot('builder-floor.png')

    # 2. Place an imported object, a workspace screen (bound to 21) and a library object.
    def place(ref, position, **extra):
        # A user or library object decodes on its first use: objects.place says retryable.
        for _ in range(50):
            try:
                return client.call('objects.place', {'ref': ref, 'pose': {'position': position, 'rotation': [0, 0, 0, 1]}, **extra})['id']
            except RuntimeError as e:
                if not (e.args[0].get('data') or {}).get('retryable'):
                    raise
                time.sleep(.2)
        raise AssertionError(f'{ref} never loaded')
    ids = {'barrel': place('user/barrel', [1.6, 0, -5]),
           'screen': place(f'user/{SCREEN}', [-1.5, 0, -7], workspace=21),
           'crate': place(f'library/{LIBRARY}/crate', [3.4, 0, -6.5])}

    def instances():
        return {i['id']: i for i in state()['objects']['instances'] if i['id'] in ids.values()}
    wait(lambda: len(instances()) == 3 and all(i['state'] == 'active' for i in instances().values()), 30)
    inst = instances()
    project = json.loads((folder / 'builder/project.json').read_text())
    editor = json.loads((s.STATE / 'editor.json').read_text()) if (s.STATE / 'editor.json').exists() else {'placements': []}
    copies = sorted(p.name for p in (folder / 'objects').iterdir())
    check('placing copies each object into the folder and writes builder/project.json, not editor.json',
          sorted(i['ref'] for i in inst.values()) == ['world/barrel', 'world/crate', f'world/{SCREEN}'] and
          copies == ['barrel', 'crate', SCREEN] and len(project['placements']) == 3 and
          not any(r['worldId'] == f'local.{SLUG}' for r in editor['placements']) and state()['builder']['placements'] == 3,
          {'refs': {k: inst[v]['ref'] for k, v in ids.items()}, 'copies': copies, 'project': [p['ref'] for p in project['placements']]})
    screen = inst[ids['screen']]
    check('the copied screen is a home for workspace 21', (screen.get('screen') or {}).get('workspace') == 21 and
          any(b['id'] == 21 for b in state()['mounts']['bindings']), screen.get('screen'))
    director()
    ui('tabNamed', 'builder')
    wait(lambda: (builder().get('status') or {}).get('pending', {}).get('total', 0) > 0, 15)
    check('the Builder page shows the unsaved changes', (builder().get('pending') or '').startswith('Unsaved: placed 3 objects'),
          {'pending': builder().get('pending'), 'counts': builder()['status']['pending']})
    shot('builder-pending.png')

    # 3. Save with a message.
    ui('fieldText', 'builderMessage', 'Place a barrel, a screen and a crate')
    j = run_job('save', 'builder-save')
    world = json.loads((folder / 'world.json').read_text())
    check('Save makes one commit; world.json holds the objects and the home',
          j['phase'] == 'done' and git('log', '-1', '--format=%s') == 'Place a barrel, a screen and a crate' and
          git('rev-list', '--count', 'HEAD') == '2' and len(world['objects']) == 3 and
          [a['id'] for a in world.get('anchors', [])] == ['screen-01'] and world['areas'][0]['workspaceSlot'] == '01' and
          git('status', '--porcelain') == '',
          {'message': j.get('result', {}).get('message'), 'error': j.get('error'), 'objects': [o['asset'] for o in world['objects']],
           'license': world['license'], 'homes': j.get('result', {}).get('homes')})

    # 4. Move the barrel; Save with no message.
    b0 = inst[ids['barrel']]['pose']
    client.call('objects.transform', {'id': ids['barrel'], 'pose': {'position': [b0['position'][0] + .8, b0['position'][1], b0['position'][2]], 'rotation': b0['rotation']}})
    wait(lambda: (builder().get('status') or {}).get('pending', {}).get('total', 0) > 0, 15)
    j = run_job('save', 'builder-save')
    message = git('log', '-1', '--format=%s')
    check('a second Save commits the move with the default message', j['phase'] == 'done' and git('rev-list', '--count', 'HEAD') == '3' and
          message.startswith('Save ') and message.endswith('· moved 1'), {'message': message})

    # 5. Release (no remote): a tag.
    j = run_job('release', 'builder-release')
    check('Release without a remote makes the annotated tag v0.1.0', j['phase'] == 'done' and git('tag', '-l') == 'v0.1.0' and
          git('cat-file', '-t', 'v0.1.0') == 'tag' and git('status', '--porcelain') == '' and
          (builder().get('status') or {}).get('lastVersion', {}).get('tag') == 'v0.1.0',
          {'result': {k: j.get('result', {}).get(k) for k in ('tag', 'pushed', 'release')}, 'error': j.get('error')})
    shot('builder-released.png')
    before = {i: {'ref': x['ref'], 'pose': x['pose'], 'screen': x.get('screen')} for i, x in instances().items()}

    # 6. Reload from the folder: Lumen Reach, then Builder > My worlds > Edit.
    close_director()
    client.call('world.load', {'worldId': LUMEN})
    wait(lambda: (state().get('world') or {}).get('id') == LUMEN and state().get('builder') is None, 60)
    editor_file = s.STATE / 'editor.json'
    kept = json.loads(editor_file.read_text())['placements'] if editor_file.exists() else []
    check('leaving the folder keeps no placements for it in editor.json', not any(r['worldId'] == f'local.{SLUG}' for r in kept),
          {'editor.json': editor_file.exists(), 'placements': len(kept)})
    director()
    ui('tabNamed', 'builder')
    wait(lambda: SLUG in (builder().get('projects') or []), 15)
    assert press(f'builder-edit:{SLUG}')
    wait(lambda: (state().get('builder') or {}).get('projectId') == SLUG, 60)
    wait(lambda: len(instances()) == 3 and all(i['state'] == 'active' for i in instances().values()), 30)
    after = {i: {'ref': x['ref'], 'pose': x['pose'], 'screen': x.get('screen')} for i, x in instances().items()}

    def close(a, b):
        return all(abs(p - q) < 1e-4 for p, q in zip(a['pose']['position'] + a['pose']['rotation'], b['pose']['position'] + b['pose']['rotation']))
    same = after.keys() == before.keys() and all(after[k]['ref'] == before[k]['ref'] and close(after[k], before[k]) and
                                                 after[k]['screen'] == before[k]['screen'] for k in after)
    try:
        wait(lambda: ((builder().get('status') or {}).get('pending') or {}).get('total') == 0, 10)
    except AssertionError:
        pass
    check('reloading the folder restores the same placements, poses and screen binding, with nothing pending', same and
          ((builder().get('status') or {}).get('pending') or {}).get('total') == 0 and git('status', '--porcelain') == '',
          {'before': before, 'after': after, 'pending': (builder().get('status') or {}).get('pending'), 'git': git('status', '--porcelain')})
    close_director()
    s.ctl('eval', 'hl.plugin.hyprune.camera(1,1.7,0,0,-.08)')
    shot('builder-reloaded.png')

    # 7. The released package validates.
    with tempfile.TemporaryDirectory(dir=os.path.expanduser('~/.cache')) as tmp:
        pkg = pathlib.Path(tmp) / 'package'
        pkg.mkdir()
        subprocess.run(f'git -C {folder} archive v0.1.0 | tar -x -C {pkg}', shell=True, check=True)
        sdk = subprocess.run([*CLI.split(), 'world', 'validate', str(pkg)], capture_output=True, text=True)
        core = subprocess.run([str(s.ROOT / 'build/hyprune-world-check'), str(pkg)], capture_output=True, text=True)
        files = sorted(str(p.relative_to(pkg)) for p in pkg.rglob('*') if p.is_file())
    check('the released package passes the SDK validator and hyprune-world-check', sdk.returncode == 0 and core.returncode == 0 and
          'builder/project.json' in files and not any(f.startswith('.git') for f in files),
          {'sdk': sdk.returncode, 'core': core.returncode, 'stderr': (sdk.stderr + core.stderr)[-400:], 'files': len(files)})

    # 8. The sky and floor from the page: each tile applies, the world reloads with the placements.
    director()
    ui('tabNamed', 'builder')
    wait(lambda: len(builder().get('floors') or []) == 5 and len(builder().get('skies') or []) == 4, 60)
    loads = []
    for kind, button in (('floor', 'builder-set-floor:paving'), ('sky', 'builder-set-sky:night')):
        director()
        ui('tabNamed', 'builder')
        j = run_job(kind, button)
        loads.append({'kind': kind, 'phase': j.get('phase'), 'error': j.get('error')})
        wait(lambda: (state().get('builder') or {}).get('projectId') == SLUG and (state().get('world') or {}).get('id') == f'local.{SLUG}', 60)
        wait(lambda: len(instances()) == 3 and all(i['state'] == 'active' for i in instances().values()), 60)
    project = json.loads((folder / 'builder/project.json').read_text())
    base = json.loads((folder / 'builder/base.json').read_text())
    check('the page sets the floor and the sky; the world reloads with them and keeps its placements',
          all(l['phase'] == 'done' for l in loads) and project['floor']['preset'] == 'paving' and project['sky']['preset'] == 'night' and
          base['atmosphere']['exposure'] > 1 and len(project['placements']) == 3,
          {'loads': loads, 'floor': project['floor'], 'sky': project['sky'], 'git': git('status', '--porcelain').split('\n')})
    close_director()
    wait(lambda: not status().get('warp') and status().get('phase') == 'arrived', 30)
    s.ctl('eval', 'hl.plugin.hyprune.camera(1,1.7,0,0,.25)')
    shot('builder-night.png')

    # 8b. The checklist (RFC-0014 Phase D): every blocking item done; a spawn moved into the air
    # opens spawn.floor; after Save, Release is refused (release.check) and Release anyway tags
    # v0.1.1; the spawn back closes the item. Then the barrel (an import)
    # is turned a quarter from Placed > Model (object adjust on world/barrel; the folder reloads).
    up = [0, 0, 0, 1]

    def checklist():
        return builder().get('check') or {}

    def approach():
        return builtins.next((i for i in checklist().get('items') or [] if i['id'] == 'screen.approach'), None)

    def generated(*args):
        r = subprocess.run([*CLI.split(), 'builder', 'primitive', SLUG, *args, '--json'], capture_output=True, text=True,
                           env={**os.environ, 'HYPRUNE_PROJECTS_DIR': str(folder.parent)})
        return json.loads(r.stdout.strip().split('\n')[-1])['result']['ref']

    def placing(params):
        for _ in range(60):
            try:
                return client.call('objects.place', params)
            except RuntimeError as e:
                if not (e.args[0].get('data') or {}).get('retryable'):
                    raise
                time.sleep(.2)
        raise AssertionError(f"{params['ref']} never loaded")

    def job_again(button, seconds=90):
        before = json.dumps(job(), sort_keys=True)
        assert press(button), button
        wait(lambda: job().get('phase') in ('done', 'failed') and json.dumps(job(), sort_keys=True) != before, seconds)
        return job()
    director()
    ui('tabNamed', 'builder')
    wait(lambda: approach() is not None, 60)
    first = checklist()
    check('the Builder page lists the checklist; the spawn, the screen approach and the walkable area are done',
          first.get('ready') and approach()['done'] and approach()['target'] == ids['screen'] and
          all(i['done'] for i in first['items'] if i['id'] in ('spawn.floor', 'walk.area', 'budgets')),
          {'items': [{k: i[k] for k in ('id', 'done', 'blocks')} for i in first['items']]})
    shot('builder-checklist.png')
    # Core refuses objects in a screen's approach (rule approach), so the open item comes from the
    # CLI: the spawn moved 3 m up (no floor under it), saved from the page.
    def spawn_at(position):
        r = subprocess.run([*CLI.split(), 'builder', 'spawn', SLUG, '--at', position, '--json'], capture_output=True, text=True,
                           env={**os.environ, 'HYPRUNE_PROJECTS_DIR': str(folder.parent)})
        return json.loads(r.stdout.strip().split('\n')[-1])

    def spawn_item():
        return builtins.next((i for i in checklist().get('items') or [] if i['id'] == 'spawn.floor'), None)
    moved = spawn_at('0,3,0')
    ui('fieldText', 'builderMessage', 'A spawn in the air')
    saved = job_again('builder-save')
    wait(lambda: spawn_item() is not None and not spawn_item()['done'], 30)
    opened = spawn_item()
    time.sleep(.5)
    shot('builder-checklist-open.png')
    refused = job_again('builder-release')
    time.sleep(.5)
    shot('builder-release-refused.png')
    anyway = job_again('builder-release-anyway')
    check('a spawn in the air opens spawn.floor; Release is refused with release.check, Release anyway tags v0.1.1',
          moved['event'] == 'done' and saved['phase'] == 'done' and 'no floor' in opened['message'] and refused['phase'] == 'failed' and
          (refused.get('error') or {}).get('code') == 'release.check' and anyway['phase'] == 'done' and 'v0.1.1' in git('tag', '-l').split('\n'),
          {'item': opened, 'refused': refused.get('error'), 'anyway': {k: (anyway.get('result') or {}).get(k) for k in ('tag', 'warnings', 'publishing')}})
    spawn_at('0,0,0')
    ui('fieldText', 'builderMessage', 'The spawn back on the ground')
    job_again('builder-save')
    wait(lambda: spawn_item() is not None and spawn_item()['done'] and checklist().get('ready'), 30)
    check('the spawn back on the ground closes the item; the checklist is ready again', spawn_item()['done'] and checklist().get('ready'), {'item': spawn_item()})
    # Placed > Model: the barrel's fit, then a quarter turn (object adjust on the folder's copy).
    barrel_before = json.loads((folder / 'objects/barrel/object.json').read_text())['bounds']
    ui('tabNamed', 'objects')
    ui('objectsView', 'placed')
    ui('selectObject', ids['barrel'])
    wait(lambda: ((builder().get('edit') or {}).get('fit') or {}).get('ref') == 'world/barrel', 60)
    fit = builder()['edit']['fit']
    time.sleep(.5)
    shot('builder-model-section.png')
    assert press('segment:90')
    assert press('object-adjust')
    wait(lambda: ((builder().get('edit') or {}).get('last') or {}).get('phase') in ('done', 'failed'), 120)
    last = builder()['edit']['last']
    wait(lambda: (state().get('builder') or {}).get('projectId') == SLUG and len(instances()) == 3 and all(i['state'] == 'active' for i in instances().values()), 60)
    barrel_after = json.loads((folder / 'objects/barrel/object.json').read_text())['bounds']
    record = json.loads((folder / 'objects/barrel/import.json').read_text())
    check('Placed > Model reads the import\'s fit and turns it a quarter (absolute yaw 90, a backup kept, the folder reloads)',
          fit['adjustable'] and last['phase'] == 'done' and record['options']['yaw'] == 90 and bool(last['result'].get('backup')) and
          abs((barrel_after['max'][0] - barrel_after['min'][0]) - (barrel_before['max'][2] - barrel_before['min'][2])) < 2e-3,
          {'fit': fit['fit'], 'result': {k: last.get('result', {}).get(k) for k in ('steps', 'backup')}, 'before': barrel_before, 'after': barrel_after})
    close_director()

    # 9. Shapes (IPC 0.31): made in the Director, placed with keys, swapped, renamed, pruned.
    def shapes():
        return status().get('shapes') or {}

    def shape_job(kind, button, seconds=90):
        assert press(button), button
        wait(lambda: (shapes().get('job') or {}).get('kind') == kind and (shapes().get('job') or {}).get('phase') in ('done', 'failed'), seconds)
        return shapes()['job']

    def refused(method, params):
        try:
            client.call(method, params)
        except RuntimeError as e:
            return e.args[0]
        return None

    def placing(params):
        # A just-made shape decodes on its first use: objects.place says retryable.
        for _ in range(60):
            try:
                return client.call('objects.place', params)
            except RuntimeError as e:
                if not (e.args[0].get('data') or {}).get('retryable'):
                    raise
                time.sleep(.2)
        raise AssertionError(f"{params['ref']} never loaded")

    def by_key(key):
        return builtins.next((i for i in state()['objects']['instances'] if i.get('key') == key), None)
    up = [0, 0, 0, 1]
    director()
    ui('tabNamed', 'objects')
    ui('objectsView', 'shapes')
    wait(lambda: shapes().get('supported') and status().get('tabName') == 'objects', 15)
    glow = {'shape': 'box', 'size': ['0.6', '1.6', '0.6'], 'steps': None,
            'material': {'kind': 'colour', 'colour': '#202020', 'key': '', 'glow': {'colour': '#30e0ff', 'strength': '4'}}}
    assert ui('shapeForm', json.dumps(glow)) == 'true'
    time.sleep(.5)
    shot('builder-shapes.png')
    j = shape_job('make', 'shape-make')
    beacon_ref = (j.get('result') or {}).get('ref')
    wait(lambda: any(o.get('ref') == beacon_ref for o in (state()['editor'].get('unplaced') or {}).get('objects', [])), 20)
    dry = placing({'ref': beacon_ref, 'pose': {'position': [0, 0, -11], 'rotation': up}, 'key': 'beacon', 'dryRun': True})
    count = len(state()['objects']['instances'])
    beacon = placing({'ref': beacon_ref, 'pose': {'position': [0, 0, -11], 'rotation': up}, 'key': 'beacon'})['id']
    taken = refused('objects.place', {'ref': beacon_ref, 'pose': {'position': [1, 0, -11], 'rotation': up}, 'key': 'beacon'})
    moved = client.call('objects.place', {'ref': beacon_ref, 'pose': {'position': [0.5, 0, -11], 'rotation': up}, 'key': 'beacon', 'ifExists': 'update'})
    check('a shape made in Objects > Shapes is pocketed; placing with a key: dry run, taken key refused with its id, ifExists update moves it',
          j['phase'] == 'done' and beacon_ref.startswith('world/box-') and dry.get('dryRun') is True and count == len(state()['objects']['instances']) - 1 and
          (taken or {}).get('data', {}).get('rule') == 'key.taken' and (taken or {}).get('data', {}).get('id') == beacon and
          moved.get('updated') is True and moved['id'] == beacon and abs(by_key('beacon')['pose']['position'][0] - 0.5) < 1e-3,
          {'job': j.get('result'), 'dry': dry, 'taken': taken, 'moved': moved})
    stairs = json.loads(subprocess.run([*CLI.split(), 'builder', 'primitive', SLUG, '--shape', 'stairs', '--size', '1.2,1.8,3', '--material', 'paving', '--json'],
                                       capture_output=True, text=True, env={**os.environ, 'HYPRUNE_PROJECTS_DIR': str(folder.parent)}).stdout.strip().split('\n')[-1])['result']
    steps = placing({'ref': stairs['ref'], 'pose': {'position': [-3, 0, -12], 'rotation': up}, 'key': 'steps'})['id']
    blocked = refused('objects.place', {'ref': stairs['ref'], 'pose': {'position': [0.5, 0, -11], 'rotation': up}})
    check('stairs from the CLI place on the floor; a shape inside the box is refused by its collision',
          bool(steps) and by_key('steps')['state'] == 'active' and blocked is not None, {'stairs': stairs.get('name'), 'blocked': blocked})
    # A material from the editor; a box in it; the material changed: the box is rebuilt and swapped.
    assert ui('materialForm', json.dumps({'key': 'brick', 'name': 'Brick', 'colour': '#d08060', 'texture': {'kind': 'preset', 'preset': 'paving'},
                                          'tile': '2', 'glow': None, 'roughness': '', 'metallic': '', 'fresh': True})) == 'true'
    time.sleep(.4)
    shot('builder-materials.png')
    j = shape_job('material', 'material-save')
    assert ui('shapeForm', json.dumps({'shape': 'box', 'size': ['3', '1.2', '0.3'], 'steps': None, 'material': {'kind': 'key', 'key': 'brick', 'colour': '#d08060', 'glow': None}})) == 'true'
    wall_ref = shape_job('make', 'shape-make')['result']['ref']
    wall = placing({'ref': wall_ref, 'pose': {'position': [3, 0, -12], 'rotation': up}, 'key': 'wall'})['id']
    wait(lambda: 'brick' in (shapes().get('materials') or []), 20)
    assert ui('materialForm', json.dumps({'key': 'brick', 'name': 'Brick', 'colour': '#806040', 'texture': {'kind': 'keep'}, 'tile': '', 'glow': None,
                                          'roughness': '', 'metallic': '', 'fresh': False, 'textured': True})) == 'true'
    assert press('material-save')  # saved, then (stale shapes) rebuilt: the job becomes the rebuild
    wait(lambda: (shapes().get('job') or {}).get('kind') == 'rebuild' and shapes()['job'].get('phase') == 'done', 90)
    wait(lambda: by_key('wall')['ref'] != wall_ref, 20)
    swapped = by_key('wall')
    check('a material saved in the editor and changed rebuilds its shapes and swaps their placements (same id and key)',
          j['phase'] == 'done' and swapped['id'] == wall and swapped['ref'].startswith('world/box-') and swapped['state'] == 'active',
          {'before': wall_ref, 'after': swapped['ref']})
    # The Placed panel: resize the beacon, then rename its key.
    ui('objectsView', 'placed')
    ui('selectObject', beacon)
    wait(lambda: status().get('director'), 5)
    time.sleep(.8)
    assert ui('fieldText', 'shapeEditSizeY', '2.4') == 'true'
    old_ref = by_key('beacon')['ref']
    shot('builder-shape-edit.png')
    shape_job('apply', 'shape-apply')
    wait(lambda: by_key('beacon')['ref'] != old_ref, 20)
    assert ui('fieldText', 'objectKey', 'tower') == 'true'
    assert press('object-key-set')
    wait(lambda: by_key('tower') is not None, 10)
    tower = by_key('tower')
    missing = client.call('objects.remove', {'key': 'nothing-here'})
    cat = client.call('objects.catalog', {})['entries']
    prim = builtins.next((e['primitive'] for e in cat if e['ref'] == tower['ref']), None)
    check('the Placed panel resizes a shape (a new folder, the same id, its glow kept) and renames its key; removing a missing key succeeds',
          tower['id'] == beacon and tower['ref'] != old_ref and (prim or {}).get('size') == [0.6, 2.4, 0.6] and missing.get('removed') is False and
          ((prim or {}).get('material') or {}).get('glow') == {'colour': '#30e0ff', 'strength': 4},
          {'tower': {k: tower[k] for k in ('id', 'ref', 'key')}, 'primitive': prim, 'missing': missing})
    close_director()
    keys('-k', '1')  # tool 1: no placement ghost for the made shapes still in the pocket
    time.sleep(.5)
    # From behind the shapes (beyond them the screen and the barrel): the glowing tower, the brick
    # wall and the stairs rising away from the camera's side.
    s.ctl('eval', 'hl.plugin.hyprune.camera(0.5,2.2,-18.5,3.14,-.1)')
    shot('builder-shapes-world.png')
    s.ctl('eval', 'hl.plugin.hyprune.camera(-6.5,1.9,-10.5,1.17,-.12)')
    shot('builder-shapes-stairs.png')
    director()
    ui('tabNamed', 'builder')
    ui('fieldText', 'builderMessage', 'Shapes: a beacon, stairs and a brick wall')
    j = run_job('save', 'builder-save')
    project = json.loads((folder / 'builder/project.json').read_text())
    world = json.loads((folder / 'world.json').read_text())
    placed_keys = sorted(p["key"] for p in project["placements"] if "key" in p)
    left = sorted(p.name for p in (folder / 'objects').iterdir() if (p / 'primitive.json').exists())
    used = sorted({p['ref'][6:] for p in project['placements'] if (folder / 'objects' / p['ref'][6:] / 'primitive.json').exists()})
    check('Save keeps the keys, prunes the shapes nobody places and credits the materials',
          j["phase"] == "done" and placed_keys == ["steps", "tower", "wall"] and left == used and len(left) == 3 and
          any(a['path'].startswith('materials/brick/') for a in world['attribution']),
          {"keys": placed_keys, "shapes": left, 'warnings': (j.get('result') or {}).get('warnings')})
    close_director()

    # 10. Behaviours: a textured cube grows with CPU use (a command source), a glowing bar follows
    # a JSON API (an HTTP source with a map); the cube's behaviour is added from Placed.
    def generated(*args):
        r = subprocess.run([*CLI.split(), 'builder', 'primitive', SLUG, *args, '--json'], capture_output=True, text=True,
                           env={**os.environ, 'HYPRUNE_PROJECTS_DIR': str(folder.parent)})
        return json.loads(r.stdout.strip().split('\n')[-1])['result']['ref']
    cube_ref = generated('--shape', 'cube', '--size', '1', '--material', 'paving')
    bar_ref = generated('--shape', 'box', '--size', '0.5,2.5,0.5', '--colour', '#101820')
    cube = placing({'ref': cube_ref, 'pose': {'position': [-1.2, 0, -22], 'rotation': up}, 'key': 'cpu-cube'})['id']
    bar = placing({'ref': bar_ref, 'pose': {'position': [1.2, 0, -22], 'rotation': up}, 'key': 'queue-bar'})['id']
    bad = refused('objects.configure', {'id': bar, 'patch': {'behaviours': [{'key': 'x', 'property': 'scale', 'from': [1, 0, 1], 'to': [1, 1, 1]}]}, 'dryRun': True})
    # The cube from the Placed panel: + Grow, B's height 3 (A keeps 0.05), Save.
    director()
    ui('tabNamed', 'objects')
    ui('objectsView', 'placed')
    ui('selectObject', cube)
    time.sleep(.8)
    assert press('behaviour-add:scale')
    assert ui('fieldText', 'behaviour-to:1', '3') == 'true'
    time.sleep(.6)
    shot('builder-behaviour-editor.png')
    assert press('behaviour-save')
    wait(lambda: (by_key('cpu-cube') or {}).get('behaviours'), 15)
    # The bar over IPC: its height and glow follow the queue depth (0..50 jobs as 0..100).
    client.call('objects.configure', {'key': 'queue-bar', 'patch': {'behaviours': [
        {'key': 'depth', 'property': 'scale', 'from': [1, .05, 1], 'to': [1, 1, 1], 'smoothing': .2},
        {'key': 'glow', 'property': 'glow', 'from': 1, 'to': 6, 'colour': '#30e0ff', 'smoothing': .2}]}})
    queue = {'queue': {'depth': 37}}

    class Api(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = json.dumps(queue).encode()
            self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(body))); self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass
    api = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Api)
    threading.Thread(target=api.serve_forever, daemon=True).start()
    gauges = str(s.ROOT / 'dev/machines/scripts/system-gauges.py')
    (s.STATE / 'sources.json').write_text(json.dumps({'devFastIntervals': True, 'sources': [
        {'id': cube, 'kind': 'command', 'argv': [gauges, 'cpu'], 'intervalS': 1, 'timeoutS': 5},
        {'id': bar, 'kind': 'http', 'url': f'http://127.0.0.1:{api.server_address[1]}/queue.json', 'intervalS': 1,
         'map': {'value': {'path': '$.queue.depth', 'min': 0, 'max': 50}, 'display': '{raw:.0f} queued'}}]}, indent=1) + '\n')
    states = lambda key: {b['key']: b for b in (by_key(key) or {}).get('behaviours') or []}
    wait(lambda: states('cpu-cube').get('grow', {}).get('condition') == 'fresh' and states('queue-bar').get('depth', {}).get('condition') == 'fresh', 30)
    grow, depth = states('cpu-cube')['grow'], states('queue-bar')['depth']
    check('a behaviour added from Placed and one over IPC read their data sources (command and HTTP with a map); a bad one is refused',
          grow['behaviour']['to'] == [1, 3, 1] and abs(grow['t'] - min(1, max(0, float(grow['value']) / 100))) <= .011 and
          depth['value'] == 74 and depth['t'] == .74 and (bad or {}).get('code') == -32602,
          {'cube': grow, 'bar': depth, 'refused': (bad or {}).get('message')})
    close_director()
    keys('-k', '1')

    def bar_rows(path):
        # Rows of the bar's glow (cyan: blue and green high, red well below) in the middle column band.
        from PIL import Image
        im = Image.open(path).convert('RGB')
        w, h = im.size
        px = im.load()
        rows = 0
        for y in range(h):
            if any(px[x, y][2] > 200 and px[x, y][1] > 200 and px[x, y][0] < px[x, y][2] - 60 for x in range(w // 2 - 40, w // 2 + 40, 4)):
                rows += 1
        return rows
    s.ctl('eval', 'hl.plugin.hyprune.camera(1.2,1.4,-17.5,0,0)')
    time.sleep(1.2)
    shot('builder-bar-high.png')
    queue['queue']['depth'] = 8
    wait(lambda: states('queue-bar')['depth']['t'] == .16, 15)
    time.sleep(1.5)
    shot('builder-bar-low.png')
    high, low = bar_rows(out / 'builder-bar-high.png'), bar_rows(out / 'builder-bar-low.png')
    check('the bar grows and shrinks with the API value on screen (display only: its collision stays at rest)',
          high > low * 2 and low > 0 and all(abs(a - b) < 1e-3 for a, b in zip(by_key('queue-bar')['pose']['position'], [1.2, 0, -22])),
          {'rowsAt74': high, 'rowsAt16': low})
    s.ctl('eval', 'hl.plugin.hyprune.camera(0,1.6,-16.5,0,-.05)')
    time.sleep(1)
    shot('builder-behaviours.png')
    # The bar's panel with its two behaviours and their live values.
    director()
    ui('tabNamed', 'objects')
    ui('objectsView', 'placed')
    ui('selectObject', bar)
    time.sleep(.8)
    assert press('behaviour-add:visible') and press('behaviour-cancel')  # opens, scrolls down, closes: the list stays in view
    time.sleep(.5)
    shot('builder-behaviours-panel.png')
    close_director()
    if os.environ.get('HYPRUNE_BEHAVIOURS_CLIP') == '1':
        # The gallery clip: the camera slides past while the API value swings and a CPU burst
        # (busy processes for 4 s) makes the cube grow; core exports every presented frame.
        import cinema, math as m
        cam = cinema.Cinema(s)
        cam.hold([-3.5, 1.7, -16.5], target=[0, 1, -22])
        time.sleep(1)
        raw = out / 'behaviours-clip.mkv'
        with cam.recording(raw):
            cam.path([{'t': 0, 'eye': [-3.5, 1.7, -16.5], 'target': [0, 1, -22]}, {'t': 14, 'eye': [3.5, 1.9, -16.0], 'target': [0, 1, -22]}])
            burners = []
            for i in range(28):
                queue['queue']['depth'] = round(26 + 22 * m.sin(i / 3.5))
                if i == 8:
                    burners = [subprocess.Popen([sys.executable, '-c', 'import time\nt=time.time()+4\nwhile time.time()<t: pass'])
                               for _ in range(os.cpu_count() or 4)]
                time.sleep(.5)
            for b in burners:
                b.wait()
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(raw), *cinema.ENCODE, str(out / 'behaviours-clip.mp4')], check=True)
        print('clip:', out / 'behaviours-clip.mp4', flush=True)
    api.shutdown()
    log = shell_log.read_text()

    bad = [x for x in ['ReferenceError', 'TypeError', 'Binding loop', 'Unable to assign', 'is not defined', 'does not match the negotiated protocol'] if x in log]
    check('no QML runtime errors in the shell log', not bad, bad)
    evidence = s.ROOT / 'docs/evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'builder-shell.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
    print(f'PASS builder-shell: {len(checks)} checks', flush=True)
