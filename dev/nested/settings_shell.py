#!/usr/bin/env python3
"""Director Settings with the real shell (IPC 0.30): every category, the preset and overrides.

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/settings_shell.py OUTDIR

Nested only. The shell from HYPRUNE_SHELL_DIR (default the shell checkout beside core) runs at
0.30 against Lumen Reach with a few overrides set. The Director opens (Tab held), a still is
taken of each Settings category at 1920x1080 (the layout audit must pass) and of three at 2x
scale; then a clip (OUTDIR/settings-flow.mp4) shows a preset switch keeping an override, a
return to the default, a search and the reset-all confirmation. Results in OUTDIR/results.json.
"""
import argparse, builtins, json, os, pathlib, shutil, signal, subprocess, time
import session as s
import npcs
from ctl import verified
from m2 import keys, wait

WORLD = 'com.hyprune.lumen-reach'
CATEGORIES = ['display', 'movement', 'controls', 'hud', 'world', 'guidance', 'data', 'recording', 'accessibility', 'advanced']


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_SETTINGS_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'player.json', 'settings.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    results = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS settings-shell:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    read, write = os.pipe(); os.write(write, (shell_token + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.30', HYPRUNE_SOCKET=str(socketpath),
                       HYPRUNE_OUTPUT_SCREEN='HEADLESS-1', HYPRUNE_DEV_AUTOMATION='1',
                       HYPRUNE_SHELL_PREFERENCES=str(s.STATE / 'shell.json'))
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', '0.30'], env=environment,
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

    def shot(name):
        time.sleep(.9)
        audit = json.loads(ui('layoutAudit'))
        assert audit.get('labels', 0) > 0 and not audit.get('failures'), (name, audit)
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / (name + '.png'))], check=True)
        return audit

    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(4)
    get = lambda: client.call('settings.get', {})
    st = client.call('settings.set', {'preset': 'medium', 'set': {'graphics.msaa': 2, 'graphics.target_fps': 120, 'player.carry_turn': .2, 'player.walk_speed': 5}})
    check('overrides set', st['overrides'] == 4, {'overrides': st['overrides']})

    def open_director():
        keys('-P', 'Tab', '-s', '1400', '-p', 'Tab')
        wait(lambda: status().get('director'), timeout=15)
    open_director()
    ui('tabNamed', 'settings')
    for cat in CATEGORIES:
        ui('settingsCategory', cat)
        audit = shot(f'settings-{cat}')
        check(f'{cat}: drawn, no clipped or overlapping labels', True, {'labels': audit.get('labels')})
    for cat in ['display', 'controls', 'hud']:
        s.ctl('eval', 'hl.monitor({output="HEADLESS-1",mode="1920x1080@60",position="1600x0",scale=2})'); time.sleep(1.5)
        ui('settingsCategory', cat)
        shot(f'settings-{cat}-s2')
    s.ctl('eval', 'hl.monitor({output="HEADLESS-1",mode="1920x1080@60",position="1600x0",scale=1})'); time.sleep(1.5)
    check('2x scale: three categories drawn')

    # The flow, on video.
    ui('settingsCategory', 'display'); time.sleep(.8)
    rec = s.envrun('wf-recorder', '-o', 'HEADLESS-1', '-r', '30', '-c', 'libx264', '-p', 'preset=veryfast', '-p', 'crf=22', '-x', 'yuv420p',
                   '-f', str(s.STATE / 'flow.mkv'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    children.append(rec); time.sleep(2)
    ui('settingsPreset', 'ultra'); time.sleep(2.5)
    st = get()
    check('a preset switch keeps the override (dot stays) and moves the rest',
          st['preset'] == 'ultra' and st['settings']['graphics.msaa'] == {'value': 2, 'default': 4, 'source': 'user'} and
          st['settings']['graphics.anisotropy']['value'] == 16, {'msaa': st['settings']['graphics.msaa']})
    ui('settingsReset', 'graphics.msaa'); time.sleep(2.5)
    check('return to default', get()['settings']['graphics.msaa'] == {'value': 4, 'default': 4, 'source': 'preset'})
    ui('settingsSearch', 'smooth'); time.sleep(2.5)
    ui('settingsSearch', ''); time.sleep(1)
    ui('settingsConfirmReset', 'true'); time.sleep(2.5)
    shot('settings-reset-confirm')
    client.call('settings.set', {'resetAll': True}); ui('settingsConfirmReset', 'false'); time.sleep(2.5)
    st = get()
    check('reset all: no overrides, the preset stays', st['overrides'] == 0 and st['preset'] == 'ultra', {'overrides': st['overrides']})
    rec.send_signal(signal.SIGINT)
    for n in range(300):
        if rec.poll() is not None:
            break
        s.ctl('eval', f'hl.plugin.hyprune.camera(-8,1.7,12,{1e-4 * (n % 2)},0)'); time.sleep(.1)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(s.STATE / 'flow.mkv'), '-c:v', 'libx264', '-preset', 'slow', '-crf', '20',
                    '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(out / 'settings-flow.mp4')], check=True)
    (out / 'results.json').write_text(json.dumps(results, indent=2))
    client.call('runtime.exit', {})
    print(f'PASS settings-shell: {len(results)} checks', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve()),
                             protocol='0.30'))
