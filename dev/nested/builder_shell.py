"""Build a world from the Director (RFC-0014 phase 1 as amended, IPC 0.27, world format 0.11):
dev/nested/run.sh --builder-shell.

Nested only. Lumen Reach to start from, the real shell at IPC 0.27 (HYPRUNE_SHELL_DIR, default
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
 9. No QML runtime errors.
Stills: builder-*.png in the state dir (builder-wizard-*.png the wizard's steps, builder-sky.png looks
towards the sunset); evidence docs/evidence/builder-shell.json.
"""
import builtins, json, os, pathlib, secrets, shutil, subprocess, tempfile, time
import session as s
from ctl import verified
from m2 import wait, keys
from screens import screens_dir

LUMEN = 'com.hyprune.lumen-reach'
SHELL_TOKEN = secrets.token_hex(32)
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
                              'runtime.control', 'input.control', 'quest.control', 'objects.control']}]


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
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.27', HYPRUNE_SOCKET=str(socketpath), HYPRUNE_CLI=CLI,
                       XDG_DATA_HOME=os.environ['XDG_DATA_HOME'], HYPRUNE_OUTPUT_SCREEN=OUTPUT, HYPRUNE_DEV_AUTOMATION='1',
                       HYPRUNE_SHELL_PREFERENCES=str(s.STATE / 'shell-preferences.json'))
    environment.pop('HYPRUNE_PROJECTS_DIR', None)
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
    wait(lambda: status().get('protocol') == '0.27' and status().get('lease'), 30)

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
    log = shell_log.read_text()
    bad = [x for x in ['ReferenceError', 'TypeError', 'Binding loop', 'Unable to assign', 'is not defined', 'does not match the negotiated protocol'] if x in log]
    check('no QML runtime errors in the shell log', not bad, bad)
    evidence = s.ROOT / 'docs/evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'builder-shell.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
    print(f'PASS builder-shell: {len(checks)} checks', flush=True)
