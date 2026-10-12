#!/usr/bin/env python3
"""Primary workspace walls (homes 21-24) at a straight-on standback.

  dev/nested/isolated.py dev/nested/wall_audit.py [OUT_DIR]

Two cases per Lumen Reach area: an empty workspace (the placeholder must fill the
whole 16:9 frame: no gaps, no baked wall art inside it), and a real window plus a
wallpaper client. Stills and wall-audit.json go to OUT_DIR (default: the state dir).
Coverage counts pixels inside the projected frame (inset 2 %) that match the
placeholder (navy field or mint lockup) or, with a wallpaper, the wallpaper/window.
HYPRUNE_AUDIT_RENDER_SCALE: 1 (default, native), another scale, or 'high' (preset).
HYPRUNE_AUDIT_WORLD: audit another package of the world (e.g. a source build).
"""
import argparse, json, math, os, pathlib, subprocess, time
import session as s
import m3b
from m2 import wait
from ctl import verified

LUMEN = 'com.hyprune.lumen-reach'
NAVY, MINT = (0x0f, 0x1a, 0x27), (0xd4, 0xff, 0xe6)


def acceptance(client, children, args, shell_token, socketpath):
    from PIL import Image
    out = pathlib.Path(args.output or s.STATE); out.mkdir(parents=True, exist_ok=True)
    if os.environ.get('HYPRUNE_AUDIT_WORLD'):  # audit another build of the world (e.g. an unoptimised source package)
        import shutil
        shutil.rmtree(s.STATE / 'worlds' / LUMEN, ignore_errors=True)
        shutil.copytree(os.environ['HYPRUNE_AUDIT_WORLD'], s.STATE / 'worlds' / LUMEN)
    manifest = json.loads((s.STATE / 'worlds' / LUMEN / 'world.json').read_text())
    anchors = {a['id']: a for a in manifest['anchors']}
    walls = [(21 + i, a['id'], anchors[a['primaryAnchor']]) for i, a in enumerate(manifest['areas'])]
    def windows(): return json.loads(s.ctl('clients', '-j'))
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': LUMEN})
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="91"}))')
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    # Native scale (default) takes the plain upscale path; 'high' is the High preset as
    # recorded (dynamic resolution, FSR below 1.0). HYPRUNE_AUDIT_GRAPHICS adds a patch.
    scale = os.environ.get('HYPRUNE_AUDIT_RENDER_SCALE', '1')
    patch = {'quality': 'high'} if scale == 'high' else {'dynamic_resolution': False, 'render_scale': float(scale)}
    client.call('graphics.set', {'patch': {**patch, **json.loads(os.environ.get('HYPRUNE_AUDIT_GRAPHICS', '{}'))}})
    fov = state()['presentation']['fov']; t = math.tan(math.radians(fov) / 2)
    report = {'world': manifest['version'], 'format': manifest['formatVersion'], 'renderScale': scale, 'fov': fov, 'cases': {}}

    def frame_box(anchor, d, size):
        w, h = anchor['size']; W, H = size; aspect = W / H
        hx = (w / 2 / d) / (t * aspect) * W / 2; hy = (h / 2 / d) / t * H / 2
        return (W / 2 - hx, H / 2 - hy, W / 2 + hx, H / 2 + hy)

    def measure(img, box, accept):
        x0, y0, x1, y1 = box; ix, iy = (x1 - x0) * .02, (y1 - y0) * .02
        xs = range(int(x0 + ix), int(x1 - ix), 3); ys = range(int(y0 + iy), int(y1 - iy), 3)
        bad = [(x, y) for x in xs for y in ys if not accept(img.getpixel((x, y)))]
        total = len(xs) * len(ys)
        return {'coverage': round(1 - len(bad) / total, 4), 'samples': total,
                'badBox': [min(p[0] for p in bad), min(p[1] for p in bad), max(p[0] for p in bad), max(p[1] for p in bad)] if bad else None}

    def near(p, c, tol): return all(abs(p[i] - c[i]) <= tol for i in range(3))
    def placeholder(p):  # navy field, mint lockup, or their antialiased mix (baked panel art is darker: #02060c)
        if near(p, NAVY, 6) or near(p, MINT, 40):
            return True
        k = (p[1] - NAVY[1]) / (MINT[1] - NAVY[1])  # position along navy -> mint
        return .05 < k < 1 and all(abs(p[i] - (NAVY[i] + k * (MINT[i] - NAVY[i]))) <= 12 for i in range(3))

    def shoot(case, accept):
        rows = []
        for workspace, area, anchor in walls:
            h = anchor['size'][1]; d = h / (0.7 * 2 * t)
            p = anchor['pose']['position']
            s.ctl('eval', f'hl.plugin.hyprune.camera({p[0]},{p[1]},{p[2] + d},0,0)'); time.sleep(1.2)
            path = out / f'wall-{workspace}-{area}-{case}.png'
            subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
            img = Image.open(path).convert('RGB'); box = frame_box(anchor, d, img.size)
            row = {'workspace': workspace, 'area': area, 'distance': round(d, 2), 'frame': [round(v) for v in box],
                   'still': str(path), 'aim': s.camera()['aim'], **measure(img, box, accept)}
            rows.append(row); print('WALL', case, json.dumps(row), flush=True)
        report['cases'][case] = rows

    # Case 1: every primary wall's workspace is empty, no wallpaper client.
    shoot('empty', placeholder)
    # Case 2: a real window on each home plus a wallpaper client (red/blue background layer).
    for workspace, area, _ in walls:
        s.ctl('eval', f'hl.window_rule({{name="audit-{workspace}",match={{title="^Audit-{workspace}$"}},workspace="{workspace} silent",float=true,size="640 360",move="60 60"}})')
        children.append(s.envrun('kitty', '--title', f'Audit-{workspace}', '--override', 'background=#203040', '--override', 'cursor_blink_interval=0',
                                 'bash', '--noprofile', '--norc', stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        wait(lambda: any(w['title'] == f'Audit-{workspace}' and w['workspace']['id'] == workspace for w in windows()), 20)
    _, env = verified()
    wallpaper = subprocess.Popen(['qs', '-p', str(s.HERE / 'wallpaper.qml')], env=env, stdout=open(s.STATE / 'wallpaper.log', 'w'), stderr=subprocess.STDOUT)
    children.append(wallpaper); time.sleep(2)
    def content(p):  # wallpaper (saturated red/blue), the kitty window (#203040 and text), never wall art
        r, g, b = p
        return (r > 1.5 * b and r > 1.3 * g and r > 70) or (b > 1.5 * r and b > 60) or near(p, (0x20, 0x30, 0x40), 24) or (r > 150 and g > 150 and b > 150)
    shoot('window-wallpaper', content)
    (out / 'wall-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print('AUDIT', json.dumps({c: [(r['workspace'], r['coverage']) for r in rows] for c, rows in report['cases'].items()}), flush=True)
    short = [(c, r['workspace'], r['coverage']) for c, rows in report['cases'].items() for r in rows if r['coverage'] < .98]
    assert not short, ('walls not filled', short)
    print(f'PASS wall audit: {len(walls)} primary walls filled, empty and with window + wallpaper', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output', nargs='?'); a = parser.parse_args()
    os.environ.setdefault('HYPRUNE_NESTED_PROTOCOL', '0.15')
    fixture = s.state_dir('wall-audit'); fixture.mkdir(parents=True, exist_ok=True)
    m3b.acceptance = acceptance
    s.run(argparse.Namespace(state='wall-audit', m3b=True, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False, record=False, visible=False, output=a.output))
