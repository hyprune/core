#!/usr/bin/env python3
"""The debug recording with the real shell (IPC 0.29): the HUD's REC dot, the label prompt.

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/feel_shell.py OUTDIR

Nested only. The shell from HYPRUNE_SHELL_DIR (default the shell checkout beside core) runs at
0.29 against Lumen Reach. Ctrl+R starts a recording; a still shows the REC dot and its time.
Ctrl+R stops it; the shell opens the label prompt (a still), a label is typed and Enter sends
recording.label: the file is renamed rec-<time>-<slug>.jsonl with its sidecar. Then a second
recording is stopped and Esc keeps its time-only name. Stills and results go to OUTDIR.
"""
import argparse, builtins, json, os, pathlib, shutil, subprocess, time
import session as s
import npcs
from ctl import verified
from m2 import keys, wait

WORLD = 'com.hyprune.lumen-reach'


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_FEEL_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    results = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS feel-shell:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    read, write = os.pipe(); os.write(write, (shell_token + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.29', HYPRUNE_SOCKET=str(socketpath),
                       HYPRUNE_OUTPUT_SCREEN='HEADLESS-1', HYPRUNE_DEV_AUTOMATION='1')
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', '0.29'], env=environment,
                             pass_fds=(read,), stdout=open(s.STATE / 'shell.log', 'w'), stderr=subprocess.STDOUT)
    os.close(read); children.append(shell)

    def childpid():
        assert shell.poll() is None, 'shell exited; see shell.log'
        return builtins.next((int(q) for q in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
                              if pathlib.Path(f'/proc/{q}/comm').read_text().strip() == 'qs'), None)
    wait(childpid, 30)

    def shot(name):
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True)

    def status():
        return client.call('recording.status', {})

    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(4)
    s.ctl('eval', 'hl.plugin.hyprune.camera(-8,1.7,12,0,0)'); time.sleep(1.5)
    ctrl_r = lambda: keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'r', '-p', 'Control_L', '-m', 'ctrl')

    ctrl_r(); wait(lambda: status()['recording'], timeout=10)
    keys('-P', 'w', '-s', '1500', '-p', 'w'); time.sleep(1.5)
    shot('feel-shell-rec.png')
    check('Ctrl+R records; the status carries the elapsed time', status()['elapsedMs'] >= 2500, {'elapsedMs': status()['elapsedMs']})
    ctrl_r(); wait(lambda: not status()['recording'], timeout=10)
    path = status()['last']['path']
    time.sleep(1.2)
    shot('feel-shell-label-prompt.png')
    keys('walk past the court'); time.sleep(.3)
    shot('feel-shell-label-typed.png')
    keys('-k', 'Return')
    labelled = wait(lambda: (lambda v: v if v['last'] and v['last']['label'] else None)(status()), timeout=10)
    new = pathlib.Path(labelled['last']['path'])
    check('Enter labels it: renamed with a slug, sidecar written',
          new.name.endswith('-walk-past-the-court.jsonl') and new.exists() and not pathlib.Path(path).exists() and
          json.loads(new.with_suffix('.json').read_text())['label'] == 'walk past the court',
          {'from': pathlib.Path(path).name, 'to': new.name, 'sidecar': json.loads(new.with_suffix('.json').read_text())})
    time.sleep(.8)
    shot('feel-shell-after.png')
    check('the world has the keyboard again', s.camera()['active'] and not status()['recording'])

    ctrl_r(); wait(lambda: status()['recording'], timeout=10); time.sleep(1)
    ctrl_r(); wait(lambda: not status()['recording'], timeout=10); time.sleep(1.2)
    second = status()['last']['path']
    keys('-k', 'Escape'); time.sleep(1)
    check('Esc keeps the time-only name', pathlib.Path(second).exists() and status()['last']['label'] is None,
          {'path': pathlib.Path(second).name})
    (out / 'results.json').write_text(json.dumps(results, indent=2))
    client.call('runtime.exit', {})
    print(f'PASS feel-shell: {len(results)} checks', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve()),
                             protocol='0.29'))
