"""World 0.8 placed world objects: dev/nested/run.sh --objects.

Nested only (isolated harness on headless hosts). Loads the original CC0
fixture tests/fixtures/objects (one armillary-like asset placed three times
with different phases on a lightmapped floor) and checks:
1. Looking at the objects with no input they move between two stills, and
   core keeps rendering new frames (render on demand).
2. Looking away (swept bounds out of the frustum), no frames are forced.
3. graphics.reduced_motion freezes them at the t = 0 pose and forces nothing;
   the instances are drawn (instances with different phases differ in pixels)
   and the ink outline is drawn on them; switching it off resumes motion.
4. Static-world gestures (Position tool select/drag, Super+LMB drag,
   Super+Wheel) aimed at an object move nothing.
5. Leaving the world view (desktop) forces nothing.
Evidence goes to docs/evidence/world-objects.json and the state dir (stills).
"""
import json, math, shutil, subprocess, time
import session as s
from m2 import keys, wait

WORLD = 'org.hyprune.objects'


def prepare(installed):
    subprocess.run([s.sys.executable, str(s.ROOT / 'tests/fixtures/objects.py')], check=True, stdout=subprocess.DEVNULL)
    shutil.copytree(s.ROOT / 'tests/fixtures/objects', installed / WORLD, dirs_exist_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    results = []
    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data}); print('PASS objects:', name, data if data is not None else '', flush=True)
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True); children.append(device)
    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush(); assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)
    for _ in range(4):
        xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active']); time.sleep(1)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    def camera(yaw, pitch=0):
        s.ctl('eval', f'hl.plugin.hyprune.camera(0,1.7,0,{yaw},{pitch})')
    def shot(name):
        path = s.STATE / name; subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True); return path
    def frames(seconds):
        """New world frames rendered while idle (no input) for `seconds`."""
        a = s.camera(); t0 = time.monotonic(); time.sleep(seconds); b = s.camera(); dt = time.monotonic() - t0
        return {'perSecond': round((b['frames'] - a['frames']) / dt, 1), 'frames': b['frames'] - a['frames'],
                'seconds': round(dt, 2), 'animationFrames': b['animationFrames'], 'objects': b['timings']['objects']}
    from PIL import Image, ImageChops
    def fraction(a, b, box):
        x, y = Image.open(a).convert('RGB').crop(box), Image.open(b).convert('RGB').crop(box)
        diff = ImageChops.difference(x, y).convert('L').point(lambda v: 255 if v > 24 else 0)
        return round(sum(diff.histogram()[255:]) / (diff.width * diff.height), 5)
    def centre(path):  # the three objects, above the horizon strip of timed notices
        w, h = Image.open(path).size; return (w // 8, h // 6, 7 * w // 8, int(h * .55))
    def graphics(patch):
        client.call('graphics.set', {'patch': patch}); time.sleep(.5)

    t = s.camera()['timings']['objects']
    check('the fixture places three instances of one asset', t['instances'] == 3 and t['animating'], t)
    # 1. In view: motion without input, frames keep coming.
    camera(0); time.sleep(2.5)  # let load notices and camera motion settle
    a = shot('objects-a.png'); time.sleep(1); b = shot('objects-b.png')
    moved = fraction(a, b, centre(a))
    check('objects move between two stills while visible', moved > .002, {'changedFraction': moved})
    visible = frames(2)
    check('frames keep coming while an animated object is in view', visible['perSecond'] >= 10 and visible['animationFrames'], visible)
    o = visible['objects']
    check('one instanced draw per asset batch covers all three instances', o['visible'] == 3 and o['instancesDrawn'] == 3 * o['drawCalls'] > 0, o)
    # 2. Looking away: swept bounds out of the frustum force nothing.
    camera(math.pi); time.sleep(1.5)
    away = frames(3)
    check('no frames are forced while objects are out of view', away['frames'] <= 3 and not away['animationFrames']
          and away['objects']['visible'] == 0, away)
    # 3. Reduced motion: frozen at t = 0, nothing forced; instances and ink drawn.
    camera(0); graphics({'reduced_motion': True}); time.sleep(1)
    c = shot('objects-reduced-a.png'); still = frames(2); d = shot('objects-reduced-b.png')
    frozen = fraction(c, d, centre(c))
    check('reduced_motion freezes the objects', frozen < .0005, {'changedFraction': frozen})
    check('reduced_motion forces no frames', still['frames'] <= 3 and not still['animationFrames'], still)
    # West (phase 0) and centre (phase .33) at their t = 0 poses: same asset, different pixels.
    fov = client.call('camera.get', {})['fov']; image = Image.open(c); W, H = image.size
    def box(x, y=1.5, z=-6, half=1.2):
        f = 1 / math.tan(math.radians(fov) / 2); aspect = W / H
        px = lambda X: W / 2 + (X / -z) * f / aspect * W / 2; py = lambda Y: H / 2 - ((Y - 1.7) / -z) * f * H / 2
        return tuple(int(v) for v in (px(x - half), py(y + half), px(x + half), py(y - half)))
    west, mid = image.crop(box(-3)), image.crop(box(0))
    differ = ImageChops.difference(west.convert('RGB'), mid.convert('RGB')).convert('L').point(lambda v: 255 if v > 24 else 0)
    phases = round(sum(differ.histogram()[255:]) / (differ.width * differ.height), 5)
    check('instances with different phases are drawn differently', phases > .01, {'changedFraction': phases, 'boxes': [box(-3), box(0)]})
    graphics({'outline': False}); plain = shot('objects-no-ink.png')
    graphics({'outline': True}); inked = shot('objects-ink.png')
    ink = fraction(plain, inked, centre(plain))
    check('the ink outline is drawn on the objects', ink > .001, {'inkFraction': ink})
    graphics({'outline': None})
    # 4. Static-world gestures aimed at the centre object (its upper ring and buoy, sky
    # behind it) move nothing: objects are render-only. (The fixture's floor and pillar
    # own scene collision, so a ray reaching them could legitimately select them.)
    camera(0, .1); time.sleep(.5)
    d0 = s.camera(); before = shot('objects-gestures-before.png')
    def modified(mods, action):
        held = s.envrun('wtype', *sum((['-P', m] for m in mods), []), '-s', '700', *sum((['-p', m] for m in reversed(mods)), [])); children.append(held)
        time.sleep(.15); action(); held.wait(timeout=5); time.sleep(.2)
    keys('-k', '4'); time.sleep(.3)
    pointer('button', 272); pointer('button', 272, 1); pointer(150, 0); pointer('button', 272, 0)
    modified(['Super_L'], lambda: (pointer('button', 272, 1), pointer(120, 40), pointer('button', 272, 0)))
    modified(['Super_L'], lambda: [pointer('scroll', n) for n in (-1, -2, 1)])
    keys('-k', '1'); camera(0, .1); time.sleep(.6)
    d1 = s.camera(); after = shot('objects-gestures-after.png')
    editor = client.call('state.snapshot', {'subscriptionId': client.call('state.subscribe', {'topics': ['state']})['subscriptionId']})['snapshot']['state'].get('editor', {})
    same = fraction(before, after, (0, 60, Image.open(before).width, Image.open(before).height))
    check('static-world gestures on an object move nothing', d1.get('objectTransforms', 0) == 0 and d1.get('collisionChecksum') == d0.get('collisionChecksum')
          and not editor.get('objects') and same < .001,
          {'aim': [d0['aim'], d1['aim']], 'transforms': d1.get('objectTransforms', 0), 'editorObjects': editor.get('objects'), 'changedFraction': same})
    graphics({'reduced_motion': False}); time.sleep(.5)
    resumed = frames(2)
    check('motion resumes when reduced_motion is switched off', resumed['perSecond'] >= 10 and resumed['animationFrames'], resumed)
    # 5. Desktop: the world view is inactive, animation forces nothing.
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: not s.camera()['active']); time.sleep(1)
    desktop = frames(2)
    check('the desktop forces no world frames', desktop['frames'] == 0 and not desktop['animationFrames'], desktop)
    (s.ROOT / 'docs/evidence/world-objects.json').write_text(json.dumps({'nestedOnly': True, 'world': WORLD, 'checks': results}, indent=2) + '\n')
    print('PASS world objects:', len(results), 'checks', flush=True)
    client.call('runtime.exit', {})
