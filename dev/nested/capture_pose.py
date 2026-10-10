"""Reproduce an owner capture in nested: dev/run.sh --capture-pose.

Nested only (run.sh holds /tmp/worldshell-nested.lock); use HYPRUNE_NESTED_4K=1 to match
a 4K capture. HYPRUNE_CAPTURE_JSON: the capture's JSON (camera pose, FOV, graphics
quality and render scale are taken from it). HYPRUNE_CAPTURE_WORLD: an unpacked world
package directory (default: Lumen Reach from ../worlds/dist). HYPRUNE_CAPTURE_OUT: the
PNG to write (default <state>/capture-pose.png).
"""
import json, os, pathlib, shutil, subprocess, time
import session as s
from m2 import wait


def package():
    return pathlib.Path(os.environ.get('HYPRUNE_CAPTURE_WORLD', str(s.ROOT.parent / 'worlds/dist/com.hyprune.lumen-reach')))


def prepare(installed):
    world = package()
    shutil.copytree(world, installed / json.loads((world / 'world.json').read_text())['id'], dirs_exist_ok=True)


def acceptance(client, children, args, token, socket):
    capture = json.loads(pathlib.Path(os.environ['HYPRUNE_CAPTURE_JSON']).read_text())
    world = json.loads((package() / 'world.json').read_text())
    camera, timings = capture['camera'], capture['frame'].get('timings', {})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': world['id']})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    patch = {'fov': camera['fov']}
    if timings.get('quality') in ('high', 'low'):
        patch['quality'] = timings['quality']
    if timings.get('renderScale'):
        patch.update({'dynamic_resolution': False, 'render_scale': timings['renderScale']})
    client.call('graphics.set', {'patch': patch})
    x, y, z = camera['position']
    s.ctl('eval', f'hl.plugin.hyprune.camera({x},{y},{z},{camera["yaw"]},{camera["pitch"]})'); time.sleep(3)
    out = pathlib.Path(os.environ.get('HYPRUNE_CAPTURE_OUT', str(s.STATE / 'capture-pose.png')))
    subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out)], check=True)
    print('PASS capture pose', world['id'], world.get('version'), json.dumps(patch), out, flush=True)
