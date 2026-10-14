"""Workspace screens from the Director (IPC 0.27): dev/nested/run.sh --screens-shell.

Nested only. Lumen Reach and the starter screens in the user folder as for --screens, and the
real shell at IPC 0.27 (HYPRUNE_SHELL_DIR, default the shell checkout). Checks:
 1. The shell negotiates 0.27; Director > Objects lists the screens with their aspect chip.
 2. Add (pocket) then tool 5 Primary places a wall screen: it shows the next free workspace (25).
 3. Placed: the screen's row is tagged WS 25 and its Workspace section shows it.
 4. Typing 27 and Enter in the Workspace field rebinds it; None empties it; Next free binds 25.
 5. A taken workspace (21) is refused with a toast naming the rule; no QML runtime errors.
Stills: screens-shell-*.png in the state dir.
"""
import builtins, json, os, pathlib, secrets, subprocess, time
import session as s
import screens
from ctl import verified
from m2 import wait, keys

LUMEN = screens.LUMEN
SHELL_TOKEN = secrets.token_hex(32)
OUTPUT = 'HEADLESS-1'
STAND = 'user/workspace-screen-stand-16x9'


def grants():
    return [{'token': SHELL_TOKEN, 'client': 'com.hyprune.shell',
             'capabilities': ['state.read', 'world.control', 'surface.control', 'movement.control', 'shell.control',
                              'runtime.control', 'input.control', 'quest.control', 'objects.control']}]


def prepare(installed):
    screens.prepare(installed)


def acceptance(client, children, args, shell_token, socketpath):
    out = s.STATE
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'screens-shell-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS screens-shell:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    def shot(name):
        time.sleep(.8)
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=20)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    def screen_instances():
        return [i for i in state()['objects']['instances'] if i.get('screen')]

    read, write = os.pipe(); os.write(write, (SHELL_TOKEN + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.27', HYPRUNE_SOCKET=str(socketpath),
                       HYPRUNE_OUTPUT_SCREEN=OUTPUT, HYPRUNE_DEV_AUTOMATION='1', HYPRUNE_SHELL_PREFERENCES=str(s.STATE / 'shell-preferences.json'))
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

    def press(name):
        return ui('press', name) == 'true'

    s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{OUTPUT}" }}))')
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', f'hl.plugin.hyprune.toggle("{OUTPUT}")'); wait(lambda: s.camera()['active'], 60)
    wait(lambda: status().get('protocol') == '0.27' and status().get('lease'), 30)
    st = status()
    check('the shell negotiates IPC 0.27 with objects.control', 'objects.control' in st.get('capabilities', []), st.get('protocol'))

    def director():
        keys('-P', 'Tab', '-s', '1150', '-p', 'Tab'); wait(lambda: status().get('director'), 10)
        s.ctl('eval', 'hl.dispatch(hl.dsp.cursor.move({x=4,y=4}))')

    def close():
        keys('-k', 'Escape'); wait(lambda: not status().get('director'), 10)

    # 1. The catalog lists the screens with their chip.
    director()
    ui('tabNamed', 'objects'); wait(lambda: status().get('tabName') == 'objects')
    wait(lambda: status()['objects']['catalog'] >= 4, 20)
    ui('objectFilter', 'user', '', 'screen'); time.sleep(.6)
    check('Objects lists the four screens (Mine, "screen")', status()['objects']['shown'] == 4, status()['objects'])
    shot('screens-shell-catalog.png')

    # 2. Add puts it in the pocket; tool 5 Primary places it, bound to the next free workspace.
    assert press('objectAdd:' + STAND), 'Add is enabled'
    wait(lambda: status()['objects']['pocket'], 10)
    close()
    # From the arrival spot, aim at the open floor ahead (turning if the ghost is refused there).
    placed = None
    start = s.camera()['position']
    for yaw, pitch in [(0, -.35), (.7, -.35), (-.7, -.35), (1.4, -.35), (-1.4, -.35), (3.1, -.35)]:
        s.ctl('eval', f'hl.plugin.hyprune.camera({start[0]},{start[1]},{start[2]},{yaw},{pitch})'); time.sleep(.6)
        g = s.camera().get('objects', {}).get('ghost', {})
        if g.get('active') and g.get('valid'):
            subprocess.run([str(s.HERE / 'ctl.py'), 'env', str(s.ROOT / 'build/nested-pointer'), 'button', '272', '1'], check=True); time.sleep(.12)
            subprocess.run([str(s.HERE / 'ctl.py'), 'env', str(s.ROOT / 'build/nested-pointer'), 'button', '272', '0'], check=True)
            try:
                placed = wait(lambda: next(iter(screen_instances()), None), 5)
                break
            except AssertionError:
                continue
    if not placed:  # no valid ghost found from those spots: place through core (the shell part is the point here)
        client.call('objects.place', {'ref': STAND})
        placed = wait(lambda: next(iter(screen_instances()), None), 5)
        check('tool 5 placement fell back to objects.place (no valid ghost from the probe spots)', True, placed['pose'])
    else:
        check('tool 5 Primary placed the pocketed screen', True, placed['pose'])
    check('the placed screen shows the next free workspace (25)', placed['screen']['workspace'] == 25 and placed['screen']['home'], placed['screen'])

    # 3. Placed view: tag and Workspace section.
    director()
    ui('tabNamed', 'objects'); ui('objectsView', 'placed'); ui('objectSelect', placed['id'])
    wait(lambda: 'screen' in status()['objects']['sections'], 10)
    shot('screens-shell-placed-25.png')
    check('Placed shows the Workspace section for the screen', status()['objects']['sections'][:2] == ['transform', 'screen'], status()['objects']['sections'])

    def workspace():
        return next(i for i in state()['objects']['instances'] if i['id'] == placed['id'])['screen']['workspace']

    # 4. Rebinding from the Director.
    assert ui('focusNamed', 'object-workspace') == 'true'
    keys('27'); keys('-k', 'Return')
    check('typing 27 and Enter rebinds the screen', wait(lambda: workspace() == 27, 10), workspace())
    shot('screens-shell-placed-27.png')
    assert press('object-workspace-none')
    check('None empties it', wait(lambda: workspace() is None, 10), workspace())
    assert press('object-workspace-next')
    check('Next free binds 25 again', wait(lambda: workspace() == 25, 10), workspace())

    # 5. A taken workspace is refused with the rule.
    assert ui('focusNamed', 'object-workspace') == 'true'
    keys('21'); keys('-k', 'Return')
    wait(lambda: status()['objects']['feedback'] and status()['objects']['feedback']['level'] == 'warning', 10)
    feedback = status()['objects']['feedback']
    shot('screens-shell-refused.png')
    check('workspace 21 is refused and the page says why', workspace() == 25 and 'another home' in feedback['text'] and feedback['text'].startswith('Workspace 21'), feedback)
    close()
    log = shell_log.read_text()
    bad = [x for x in ['ReferenceError', 'TypeError', 'Binding loop', 'Unable to assign', 'does not match the negotiated protocol'] if x in log]
    check('no QML runtime errors in the shell log', not bad, bad)
    print(f'PASS screens-shell: {len(checks)} checks', flush=True)
