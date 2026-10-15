#!/usr/bin/env python3
"""Translucent windows in the world (nested only): a terminal with a see-through background.

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/translucent_windows.py OUTDIR

Two kitty windows with background_opacity 0.5 (a dark background, a prompt line): one in the
room (workspace 20, a free world window) and one on home 21's wall. Each is shot from 3 m in
front, in-process and with the offload renderer (two GPUs, as offload_pacing.py), with
the windows' capture records. Inside each window the image should show the world through the
background (brighter than the opaque dark background would be): OUTDIR/translucent.json
reports the mean brightness inside each window against the same spot with the window hidden.
"""
import argparse, json, os, pathlib, shutil, subprocess, time
import session as s
import npcs
from m2 import wait, pose
from PIL import Image, ImageStat

WORLD = 'com.hyprune.lumen-reach'


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_PACING_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    perf = lambda: client.call('graphics.get', {})['performance']
    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    client.call('graphics.set', {'patch': {'offload': 'off', 'dynamic_resolution': False, 'render_scale': 1}})

    def spawn(title, ws=None):
        if ws:
            s.ctl('eval', 'hl.window_rule({name="' + title + '",match={title="^' + title + '$"},workspace="' + str(ws) + ' silent"})')
        children.append(s.envrun('kitty', '--title', title, '--override', 'background=#101418', '--override', 'background_opacity=0.5',
                                 '--override', 'cursor_blink_interval=0', 'bash', '--noprofile', '--norc',
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        return wait(lambda: next((v for v in state()['surfaces'] if v['title'] == title), None), 30)

    windows = {'room': spawn('Glass room'), 'home21': spawn('Glass home', 21)}
    time.sleep(2)
    results = {}

    def capture(surface):
        return wait(lambda: next((c for c in s.camera()['captures'] if c['id'] == surface['id']), None), 20)

    def look(surface):
        c = capture(surface)
        p, n = c['position'], c.get('normal') or [0, 0, 1]
        pose(client, [p[0] + 3 * n[0], p[1] + 3 * n[1], p[2] + 3 * n[2]])
        # Face the window: yaw from the normal (the camera looks along -normal).
        import math
        yaw = math.atan2(-n[0], n[2]) if abs(n[0]) + abs(n[2]) > 1e-6 else 0
        client.call('camera.set', {'position': [p[0] + 3 * n[0], p[1] + 3 * n[1], p[2] + 3 * n[2]],
                                   'orientation': [0, math.sin(-yaw / 2), 0, math.cos(-yaw / 2)]})
        time.sleep(1.2)
        return c

    def shot(name):
        path = out / (name + '.png')
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        return path

    for mode in ('off', 'on'):
        client.call('graphics.set', {'patch': {'offload': mode}})
        wait(lambda: perf()['activeRenderer'] == ('offload' if mode == 'on' else 'in-process'), 90)
        time.sleep(1)
        for key, surface in windows.items():
            c = look(surface)
            path = shot(f'{key}-offload-{mode}')
            img = Image.open(path).convert('L')
            w, h = img.size
            centre = img.crop((int(w * .4), int(h * .4), int(w * .6), int(h * .6)))
            results[f'{key}-offload-{mode}'] = {'renderer': perf()['activeRenderer'], 'brightness': round(ImageStat.Stat(centre).mean[0], 1),
                                                'capture': {k: c.get(k) for k in ('id', 'workspaceId', 'visible', 'wall', 'floating', 'pixels', 'size') if k in c}}
            print('GLASS', key, mode, json.dumps(results[f'{key}-offload-{mode}']), flush=True)
    (out / 'translucent.json').write_text(json.dumps(results, indent=1) + '\n')
    client.call('runtime.exit', {})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve()),
                             protocol='0.29'))
