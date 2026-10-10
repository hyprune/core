"""Static world geometry: dev/nested/run.sh --static-world.

Nested only (run.sh holds /tmp/worldshell-nested.lock). Lumen Reach from ../worlds/dist.
1. At the pose of the owner's report (Sun Court building, tool 1), every transform gesture
   (Super+Wheel, Super+Ctrl+Wheel, Super+Shift+Wheel, Super+LMB drag, tool 4 select/drag/
   wheel/carry) aimed at static geometry changes nothing: no object transform, collision
   checksum and editor objects unchanged, the frame identical.
"""
import json, math, os, shutil, subprocess, time
import session as s
from m2 import keys, wait

LUMEN = 'com.hyprune.lumen-reach'


def prepare(installed):
    shutil.copytree(s.ROOT.parent / 'worlds/dist' / LUMEN, installed / LUMEN, dirs_exist_ok=True)
    if os.environ.get('HYPRUNE_STATIC_SEED', '1') != '1':
        return
    # The owner's persisted state after the report: a static building (node 326) moved by Super+Wheel.
    (s.STATE / 'editor.json').write_text(json.dumps({'version': 1, 'paths': [], 'locks': [], 'objects': [
        {'areaId': '', 'objectId': 'node-326', 'position': [-4.18, 8.75, -21.47], 'rotation': [0, .2588, 0, .9659],
         'scale': [1, 1, 1], 'worldId': LUMEN}]}))


def acceptance(client, children, args, shell_token, socketpath):
    results = []
    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data}); print('PASS static world:', name, flush=True)
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True); children.append(device)
    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush(); assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)
    for _ in range(4):
        xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': LUMEN})
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active']); time.sleep(1)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    def camera(x, y, z, yaw, pitch): s.ctl('eval', f'hl.plugin.hyprune.camera({x},{y},{z},{yaw},{pitch})'); time.sleep(.4)
    def shot(name):
        path = s.STATE / name; subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True); return path
    def modified(mods, action):
        held = s.envrun('wtype', *sum((['-P', m] for m in mods), []), '-s', '700', *sum((['-p', m] for m in reversed(mods)), [])); children.append(held)
        time.sleep(.15); action(); held.wait(timeout=5); time.sleep(.2)
    def objects(): return [o for o in state()['editor']['objects'] if o['worldId'] == LUMEN]

    # 1. Static geometry at the reported pose (capture 1791644412909).
    keys('-k', '1'); camera(13.12, 1.7, -41.76, -3.03, .19)
    d0 = s.camera(); before = shot('static-before.png'); objects0 = objects()
    saved = json.loads((s.STATE / 'editor.json').read_text())['objects']
    if os.environ.get('HYPRUNE_STATIC_SEED', '1') == '1':
        check('a persisted move of static geometry is dropped on load', d0.get('objectTransforms', 0) == 0 and objects0 == [] and saved == [], {'saved': saved})
    check('reported pose aims at static world geometry', d0['aim'] == '' and d0.get('objectTransforms', 0) == 0, {'aim': d0['aim']})
    gestures = {
        'Super+Wheel': lambda: modified(['Super_L'], lambda: [pointer('scroll', n) for n in (-1, -1, -1, 1, -2)]),
        'Super+Ctrl+Wheel': lambda: modified(['Super_L', 'Control_L'], lambda: [pointer('scroll', n) for n in (-2, 3)]),
        'Super+Shift+Wheel': lambda: modified(['Super_L', 'Shift_L'], lambda: pointer('scroll', -2)),
        'Super+LMB drag': lambda: modified(['Super_L'], lambda: (pointer('button', 272, 1), pointer(120, 40), pointer('button', 272, 0))),
        'Super+RMB drag': lambda: modified(['Super_L'], lambda: (pointer('button', 273, 1), pointer(-80, 30), pointer('button', 273, 0))),
    }
    for name, gesture in gestures.items():
        gesture(); time.sleep(.3); d = s.camera()
        check(f'{name} on a building moves nothing', d.get('objectTransforms', 0) == 0 and d.get('collisionChecksum', 0) == d0.get('collisionChecksum', 0) and objects() == objects0,
              {'transforms': d.get('objectTransforms', 0), 'collision': [d0.get('collisionChecksum', 0), d.get('collisionChecksum', 0)]})
        camera(13.12, 1.7, -41.76, -3.03, .19)
    keys('-k', '4'); time.sleep(.3)
    pointer('button', 272); pointer('button', 272, 1); pointer(150, 0); pointer('button', 272, 0)
    pointer('scroll', -1); pointer('button', 272, 1); pointer(0, 120); pointer('button', 272, 0)
    modified(['Super_L'], lambda: (pointer('button', 272, 1), pointer(100, -30), pointer('button', 272, 0)))
    d = s.camera(); e = state()['editor']
    check('Position tool cannot select or move static geometry', d.get('objectTransforms', 0) == 0 and e['selectedKind'] != 'object' and objects() == objects0
          and d.get('collisionChecksum', 0) == d0.get('collisionChecksum', 0), {'selected': [e['selectedKind'], e['selectedId']]})
    keys('-k', '1'); camera(13.12, 1.7, -41.76, -3.03, .19); time.sleep(.5)
    after = shot('static-after.png')
    from PIL import Image, ImageChops
    a_img, b_img = Image.open(before).convert('RGB'), Image.open(after).convert('RGB')
    box = (0, 60, a_img.width, a_img.height)  # the top strip holds the timed 'ready' notification
    diff = ImageChops.difference(a_img.crop(box), b_img.crop(box)).convert('L').point(lambda v: 255 if v > 8 else 0)
    changed = sum(diff.histogram()[255:]) / (diff.width * diff.height)
    check('the frame is identical after every gesture', changed < .001, {'changedFraction': changed})

    (s.ROOT / 'docs/evidence/static-world.json').write_text(json.dumps({'nestedOnly': True, 'world': LUMEN, 'checks': results}, indent=2) + '\n')
    print('PASS static world:', len(results), 'checks', flush=True)
