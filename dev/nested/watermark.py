"""Capture watermark acceptance (nested only; run.sh holds the flock).

Holds photo-mode poses in Lumen Reach, takes a U capture with
capture.watermark off and on at each, then checks: off has no mark and
records {enabled:false}; on differs from off only inside frame.watermark.rect
(bottom-right, clear space); ink follows the corner (navy on bright Sun Court
stone, white in dark areas); the live output never shows the mark.
Writes .watermark-state/captures/ and docs/evidence/capture-watermark.json.
"""
import json, math, os, shutil, subprocess, time
import session as s
from m2 import wait, keys

POSES = [  # name, position, yaw, pitch (radians); pitch < 0 looks down at the floor
    ('sun-court-stone-a', [-8, 1.7, 12], 0.0, -0.75),
    ('sun-court-stone-b', [-8, 1.7, 12], 1.57, -0.8),
    ('sun-court-stone-c', [-8, 1.7, 12], 3.14, -0.7),
    ('sun-court-stone-d', [-8, 1.7, 12], -1.57, -0.8),
    ('sun-court-view', [-8, 1.7, 12], 0.6, -0.15),
    ('array-hall-a', [8, 1.7, -34], 0.0, -0.1),
    ('array-hall-b', [8, 1.7, -34], 3.14, -0.1),
    ('rime-vault-a', [38, -4.3, -79], 0.0, -0.2),
    ('rime-vault-b', [38, -4.3, -79], 1.57, -0.3),
    ('rime-vault-c', [38, -4.3, -79], 3.14, -0.3),
]


def quat(yaw, pitch):
    sy, cy, sp, cp = math.sin(-yaw / 2), math.cos(-yaw / 2), math.sin(pitch / 2), math.cos(pitch / 2)
    return [cy * sp, sy * cp, -sy * sp, cy * cp]


def png(path):
    from PIL import Image
    return Image.open(path).convert('RGBA')


def acceptance(client, children, args, shell_token, socketpath):
    report = {'nestedOnly': True, 'protocol': '0.15', 'checks': [], 'captures': []}

    def check(name, ok, detail=None):
        assert ok, (name, detail)
        report['checks'].append(name)
        print('PASS watermark:', name, flush=True)

    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    m = next(m for m in json.loads(s.ctl('monitors', '-j')) if m['name'] == 'HEADLESS-1')
    xy = list(map(float, s.ctl('cursorpos').split(',')))
    p = s.envrun(str(s.ROOT / 'build/nested-pointer'), str(m['x'] + m['width'] / m['scale'] / 2 - xy[0]),
                 str(m['y'] + m['height'] / m['scale'] / 2 - xy[1])); p.wait(timeout=5)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': 'com.hyprune.lumen-reach'})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); time.sleep(.7)
    client.call('graphics.set', {'patch': {'offload': 'off', 'dynamic_resolution': False}})
    check('default_off', s.camera()['config']['capture']['watermark'] is False)
    captures = s.STATE / 'statehome/hyprune/captures'
    out = s.STATE / 'captures'; shutil.rmtree(out, ignore_errors=True); out.mkdir()

    def capture(name):
        before = set(captures.glob('*.png'))
        keys('-k', 'u')
        new = wait(lambda: set(captures.glob('*.png')) - before, timeout=20)
        png_path = next(iter(new)); meta = png_path.with_suffix('.json')
        wait(meta.exists, timeout=20); time.sleep(.1)
        shutil.copyfile(png_path, out / (name + '.png')); shutil.copyfile(meta, out / (name + '.json'))
        return out / (name + '.png'), json.loads(meta.read_text())

    def watermark(on):
        s.ctl('eval', 'hl.plugin.hyprune.config({capture={watermark=%s}})' % ('true' if on else 'false')); time.sleep(.3)
        check('config_' + ('on' if on else 'off'), s.camera()['config']['capture']['watermark'] is on)

    from PIL import ImageChops
    results = []
    for name, position, yaw, pitch in POSES:
        client.call('camera.photo', {'position': position, 'orientation': quat(yaw, pitch), 'fov': 60, 'renderScale': 1})
        time.sleep(1.2)
        watermark(False)
        off_path, off = capture(name + '-off')
        check(name + ' off recorded', off['frame']['watermark'] == {'enabled': False})
        watermark(True)
        on_path, on = capture(name + '-on')
        wm = on['frame']['watermark']
        check(name + ' on applied', wm['enabled'] and wm['applied'], wm)
        W, H = on['frame']['width'], on['frame']['height']
        x, y, w, h = wm['rect']
        clear = wm['clearSpacePx']
        check(name + ' bottom-right with clear space', x > W / 2 and y > H / 2 and W - (x + w) >= clear - .01 and H - (y + h) >= clear - .01, wm)
        diff = ImageChops.difference(png(off_path).convert('RGB'), png(on_path).convert('RGB')).getbbox()
        check(name + ' changes only inside the mark rect', diff is not None and diff[0] >= x and diff[1] >= y and diff[2] <= x + w and diff[3] <= y + h, (diff, wm['rect']))
        # Live output never carries the mark: the screen matches the unmarked capture there.
        shot = s.STATE / 'live.png'; subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(shot)], check=True)
        live = png(shot).convert('RGB')
        if live.size == (W, H):
            box = (x, y, x + w, y + h)
            def mad(a, b):
                d = ImageChops.difference(a.crop(box), b.crop(box)).convert('L').getdata(); return sum(d) / len(d)
            off_mad, on_mad = mad(live, png(off_path).convert('RGB')), mad(live, png(on_path).convert('RGB'))
            check(name + ' live output has no mark', off_mad < on_mad, (off_mad, on_mad))
        results.append({'pose': name, 'size': [W, H], **{k: wm[k] for k in ('ink', 'regionLuminance', 'contrast', 'rect', 'variant', 'lockupWidthPx', 'opacity')}, 'watermarkMs': on['frame'].get('watermarkMs')})
        print('MEASURE watermark', json.dumps(results[-1]), flush=True)
    client.call('camera.release', {})
    watermark(False)
    for r in results:
        expected = '#0f1a27' if r['contrast']['navy'] > r['contrast']['white'] else '#ffffff'
        check(r['pose'] + ' ink is the higher-contrast one', r['ink'] == expected)
    bright = max(results, key=lambda r: r['regionLuminance']); dark = min(results, key=lambda r: r['regionLuminance'])
    check('bright stone gets navy', bright['pose'].startswith('sun-court') and bright['ink'] == '#0f1a27', bright)
    check('dark area gets white', dark['ink'] == '#ffffff', dark)
    report['captures'] = results; report['bright'] = bright['pose']; report['dark'] = dark['pose']
    (s.ROOT / 'docs/evidence/capture-watermark.json').write_text(json.dumps(report, indent=2) + '\n')
