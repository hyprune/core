"""The HUD comes back after a session lock and a display power cycle: dev/nested/run.sh --lock.

Nested only. Lumen Reach (HYPRUNE_LUMEN_PACKAGE, else the worlds checkout's dist) and the real
shell at IPC 0.26 (HYPRUNE_SHELL_DIR, default the shell checkout). A session lock is a real
ext-session-lock-v1 client in the nested compositor (build/nested-lock-client); the display
power cycle is the compositor's DPMS dispatcher on the headless output. Checks:
 1. In world mode the shell holds one lease on the output and its HUD layer is mapped.
 2. Locking leaves world mode; the shell gives its lease back while locked (core 0.8.2
    refused every method while locked, shell.release too, so the lease outlived the lock).
 3. After unlocking and entering again: one lease, the HUD mapped, no "already leased"
    refusal in the shell log; the Director opens (exclusive input) and closes.
 4. DPMS off and on in world mode: the HUD is mapped again with one lease, the Director opens.
 5. Another connection claiming the shell's output is still refused (a renewal by the holder
    itself is a Control unit test).
Evidence: docs/evidence/lock.json (HYPRUNE_LOCK_SHOTS: elsewhere).
"""
import builtins, json, os, pathlib, secrets, shutil, subprocess, time
import session as s
from ctl import verified
from m2 import wait, keys

LUMEN = 'com.hyprune.lumen-reach'
SHELL_TOKEN = secrets.token_hex(32)
OUTPUT = 'HEADLESS-1'


def grants():
    return [{'token': SHELL_TOKEN, 'client': 'com.hyprune.shell',
             'capabilities': ['state.read', 'world.control', 'surface.control', 'movement.control', 'shell.control',
                              'runtime.control', 'input.control', 'quest.control', 'objects.control']}]


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--lock needs {LUMEN} at {source} (HYPRUNE_LUMEN_PACKAGE)')
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(source, installed / LUMEN)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'companion.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(os.environ.get('HYPRUNE_LOCK_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'lock-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS lock:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    def leases():
        return state()['shell']['leases']

    def hud_mapped():
        layers = json.loads(s.ctl('layers', '-j')).get(OUTPUT, {})
        return 'hyprune-hud' in json.dumps(layers)

    def shot(name):
        for _ in range(3):
            try:
                subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=10)
                return
            except subprocess.TimeoutExpired:  # nothing changing draws no frame: move a little
                c = client.call('camera.get', {})
                client.call('camera.set', {'position': [c['position'][0] + .01, *c['position'][1:]], 'orientation': c['orientation']})

    # The real shell (its launch token expires in 30 s: connect first).
    read, write = os.pipe(); os.write(write, (SHELL_TOKEN + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.26', HYPRUNE_SOCKET=str(socketpath),
                       HYPRUNE_OUTPUT_SCREEN=OUTPUT, HYPRUNE_DEV_AUTOMATION='1')
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell_log = s.STATE / 'shell.log'
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', '0.26'], env=environment,
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
    wait(lambda: status().get('protocol') == '0.26', 30)

    def shown(label):
        """One lease (the shell's) on the output, the shell holding it, its HUD mapped."""
        wait(lambda: status().get('lease') and hud_mapped() and len(leases()) == 1, 30)
        l = leases()
        data = {'leases': [{'id': x['id'], 'outputs': x['outputs']} for x in l], 'shellLease': status().get('lease'), 'hud': hud_mapped()}
        check(f'{label}: one lease, held by the shell, HUD mapped', len(l) == 1 and l[0]['id'] == status().get('lease') and data['hud'], data)

    def director(label):
        keys('-P', 'Tab', '-s', '1150', '-p', 'Tab')
        wait(lambda: status().get('director'), 10)
        exclusive = any(o['exclusiveInput'] for l in leases() for o in l['overlays'])
        shot('lock-' + label.replace(' ', '-') + '-director.png')
        check(f'{label}: the Director opens with exclusive input', status().get('director') and exclusive, {'exclusive': exclusive})
        keys('-k', 'Escape'); wait(lambda: not status().get('director'), 10)

    def enter():
        s.ctl('eval', f'hl.plugin.hyprune.toggle("{OUTPUT}")'); wait(lambda: s.camera()['active'], 60)

    s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{OUTPUT}" }}))')
    client.call('world.load', {'worldId': LUMEN})
    enter()
    wait(lambda: not s.camera().get('loading'), 60); time.sleep(1)
    # 1. The HUD in world mode.
    shown('before the lock')
    shot('lock-before.png')
    first_lease = status().get('lease')

    # 2. A real session lock.
    locker = s.envrun(str(s.ROOT / 'build/nested-lock-client'), stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(locker)
    line = locker.stdout.readline().strip()
    check('the nested lock client locks the session', line == 'locked', line)
    wait(lambda: not s.camera()['active'], 15)
    try:
        released = wait(lambda: not status().get('lease') and not leases(), 15)
    except AssertionError:
        released = False
    check('locked: world mode left, the lease given back', released, {'leases': leases(), 'shellLease': status().get('lease')})
    time.sleep(2)
    locker.stdin.write('unlock\n'); locker.stdin.flush()
    check('the lock client unlocks', locker.stdout.readline().strip() == 'unlocked', None)
    locker.wait(timeout=10); time.sleep(1)

    # 3. Entering again (the owner's Super+Shift+H): the HUD returns.
    enter(); time.sleep(1)
    shown('after unlock')
    log = shell_log.read_text(errors='replace')
    check('after unlock: no "already leased" refusal in the shell log', 'already leased' not in log and status().get('lease') != first_lease,
          {'lease': status().get('lease'), 'first': first_lease})
    shot('lock-after-unlock.png')
    director('after unlock')

    # 4. Display power off and on in world mode.
    def dpms(action):
        s.ctl('eval', f'hl.dispatch(hl.dsp.dpms({{ action = "{action}", monitor = "{OUTPUT}" }}))')
    dpms('off')
    off = wait(lambda: not next(m for m in json.loads(s.ctl('monitors', '-j')) if m['name'] == OUTPUT)['dpmsStatus'], 10)
    check('DPMS off', off, None)
    time.sleep(2)
    dpms('on')
    wait(lambda: next(m for m in json.loads(s.ctl('monitors', '-j')) if m['name'] == OUTPUT)['dpmsStatus'], 10)
    time.sleep(1)
    if not s.camera()['active']:
        enter()
    shown('after DPMS off and on')
    director('after DPMS')

    # 5. Another connection can't take the shell's output (the holder's own renewal: tests/control.cpp).
    output = state()['runtime']['outputs'][0]
    held = leases()[0]['id']
    try:
        client.call('shell.claim', {'outputs': [output]})
        other = 'accepted'
    except RuntimeError as e:
        other = e.args[0]
    check('another connection claiming a leased output is refused', other != 'accepted' and other.get('code') == -32004 and
          'already leased' in other.get('message', ''), other)
    check('the shell keeps its lease', [l['id'] for l in leases()] == [held], leases())

    if not os.environ.get('HYPRUNE_LOCK_SHOTS'):
        (s.ROOT / 'docs/evidence/lock.json').write_text(json.dumps({'nestedOnly': True, 'world': LUMEN, 'checks': checks}, indent=1) + '\n')
    print(f'PASS lock: {len(checks)} checks', flush=True)
