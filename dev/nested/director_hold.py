#!/usr/bin/env python3
"""The Director opens and stays open with the real shell, at the newest protocol.

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/director_hold.py OUTDIR

Nested only. The shell from HYPRUNE_SHELL_DIR (default the shell checkout beside core) runs
against Lumen Reach. Tab is held in world mode: the shell must show the Director and core
must keep the overlay visible (with the keyboard) for three seconds; Escape closes it. Every
ui.closed core sends names its cause in the compositor log; they are kept in OUTDIR/results.json.
Then the same after state only hypruned adds (quests it keeps: its revisions then run ahead of
the compositor's), and once more while the world changes every frame.
"""
import argparse, builtins, json, os, pathlib, shutil, signal, subprocess, threading, time
import session as s
import npcs
from ctl import verified
from m2 import keys, wait

WORLD = 'com.hyprune.lumen-reach'
PROTOCOL = os.environ.get('HYPRUNE_DIRECTOR_PROTOCOL', '0.33')
# Extra headless outputs (the first is HEADLESS-1) and the one world mode runs on.
OUTPUTS = int(os.environ.get('HYPRUNE_DIRECTOR_OUTPUTS', '1'))
OUTPUT = os.environ.get('HYPRUNE_DIRECTOR_OUTPUT', 'HEADLESS-1')


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_SETTINGS_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'player.json', 'settings.json']:
        (s.STATE / name).unlink(missing_ok=True)
    # hypruned's content engine runs the shell's fake CLI: its jobs are state only hypruned adds.
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    os.environ.setdefault('HYPRUNE_CLI', f'python3 {shell_dir / "dev/fake-hyprune-cli.py"}')
    os.environ['HYPRUNE_FAKE_CLI_STATE'] = str(s.STATE / 'fake-cli')
    os.environ['HYPRUNE_FAKE_CLI_DELAY'] = '0.05'
    # No installs: the fake's install rows predate IPC 0.33's sourceState (sources and entries stay).
    subprocess.run([*os.environ['HYPRUNE_CLI'].split(), 'source', 'list', '--json'], check=True, stdout=subprocess.DEVNULL)
    fake = s.STATE / 'fake-cli/state.json'
    fake.write_text(json.dumps({**json.loads(fake.read_text()), 'installs': []}))


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    results = []

    def closes():
        log = s.STATE / 'hyprland.log'
        text = log.read_text(errors='replace') if log.exists() else ''
        return [line.split('[hyprune] ', 1)[1] for line in text.splitlines() if '[hyprune] ui.closed' in line]

    def check(name, ok, data=None):
        results.append({'name': name, 'pass': bool(ok), 'evidence': data})
        (out / 'results.json').write_text(json.dumps({'results': results, 'closes': closes()}, indent=2, default=str))
        print('PASS' if ok else 'FAIL', 'director-hold:', name, json.dumps(data, default=str)[:600] if data is not None else '', flush=True)

    for _ in range(OUTPUTS - 1):
        s.ctl('output', 'create', 'headless'); time.sleep(.5)

    read, write = os.pipe()
    os.write(write, (shell_token + '\n').encode())
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL=PROTOCOL, HYPRUNE_SOCKET=str(socketpath),
                       HYPRUNE_OUTPUT_SCREEN=OUTPUT, HYPRUNE_DEV_AUTOMATION='1',
                       HYPRUNE_SHELL_PREFERENCES=str(s.STATE / 'shell.json'))
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', PROTOCOL], env=environment,
                             pass_fds=(read,), stdout=open(s.STATE / 'shell.log', 'w'), stderr=subprocess.STDOUT)
    os.close(read); children.append(shell)

    def childpid():
        assert shell.poll() is None, 'shell exited; see shell.log'
        return builtins.next((int(q) for q in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
                              if pathlib.Path(f'/proc/{q}/comm').read_text().strip() == 'qs'), None)
    qpid = wait(childpid, 30)

    def ui(*a):
        return subprocess.check_output([str(s.HERE / 'ctl.py'), 'env', 'qs', 'ipc', '--pid', str(qpid), 'call', 'preview', *map(str, a)], text=True).strip()

    def status():
        return json.loads(ui('status'))

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def leases():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']['shell']['leases']

    def overlays():
        return [o for l in leases() for o in l.get('overlays', [])]

    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{OUTPUT}" }}))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', f'hl.plugin.hyprune.toggle("{OUTPUT}")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(4)
    wait(leases, timeout=30)

    def drift():
        # Replies to plugin methods and state deltas must count revisions alike (the shell
        # compares them): a reply's revision is never behind the state it produced.
        reply = client.call('autodrive.stop', {})['revision']
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['revision'] - reply

    sequence = [0]

    def hold(name, busy=False):
        before = len(closes())
        if not busy:
            keys('-P', 'Tab', '-s', '1400', '-p', 'Tab')
        else:
            # A world that changes every frame (companions, machines): state deltas are in flight
            # while the shell takes the keyboard, so a delta from before the grab is applied after it.
            held = threading.Thread(target=keys, args=('-P', 'Tab', '-s', '1400', '-p', 'Tab')); held.start()
            end = None
            while end is None or time.monotonic() < end:
                sequence[0] += 1
                client.call('data.publish', {'sourceId': f'org.hyprune.nested.busy{sequence[0] % 8}', 'sequence': sequence[0], 'values': {'n': sequence[0]}, 'ttlMs': 60000})
                time.sleep(.02)
                if end is None and not held.is_alive():
                    end = time.monotonic() + 1.5
        try:
            wait(lambda: status().get('director'), timeout=10)
        except AssertionError:
            pass
        time.sleep(3)
        st, ov = status(), overlays()
        check(f'{name}: the Director opens and stays open for 3 s', st.get('director') and any(o['visible'] and o['exclusiveInput'] for o in ov),
              {'director': st.get('director'), 'screen': st.get('screen'), 'overlays': ov, 'closes': closes()[before:], 'drift': drift()})
        keys('-k', 'Escape')
        try:
            wait(lambda: not status().get('director') and not any(o['visible'] for o in overlays()), timeout=10)
        except AssertionError:
            pass
        st, ov = status(), overlays()
        check(f'{name}: Escape closes it', not st.get('director') and not any(o['visible'] for o in ov),
              {'director': st.get('director'), 'overlays': ov, 'closes': closes()[before:]})

    hold('first hold')
    hold('second hold')
    # State only hypruned adds (content jobs, quests, connections) moves its revision without the
    # plugin's: the Director must still open.
    for n in range(3):
        client.call('quests.create', {'quest': {'title': f'Drift {n}', 'steps': [{'title': 'One'}]}})
        time.sleep(1)
    check('daemon-only changes: revision drift measured', True, {'drift': drift()})
    hold('after daemon-only state changes')
    hold('after daemon-only state changes, in a busy world', busy=True)
    failed = [r['name'] for r in results if not r['pass']]
    client.call('runtime.exit', {})
    assert not failed, failed
    print(f'PASS director-hold: {len(results)} checks', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve()),
                             protocol=PROTOCOL))
