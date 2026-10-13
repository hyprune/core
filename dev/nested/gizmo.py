"""Gizmo 2.0 (position tool): dev/nested/run.sh --gizmo.

Nested only (isolated harness on headless hosts). Lumen Reach (HYPRUNE_GIZMO_WORLD,
else the worlds checkout's dist package) with a placed window and its world 0.8
object (the Sun Court armillary), world objects and the universal mode enabled
(HYPRUNE_GIZMO_OBJECTS=1, HYPRUNE_GIZMO_UNIVERSAL=1). Checks every mode on an
object and a window: handle picks from the published anchors (including an
end-on axis and an edge-on ring), drags with readouts, Shift snapping, numeric
entry, the world/local toggle, the base pivot, undo/redo, drop to floor, the
red ghost refusing a pose inside a wall, the player and a keepClear volume, and
stills of each mode from three angles (one near edge-on) for the demo sheet.
"""
import json, math, os, pathlib, shutil, subprocess, time
import session as s
import cinema
from m2 import wait, keys

WORLD = 'com.hyprune.lumen-reach'
OBJECT = 'object:sun-court-armillary'


def prepare(installed):
    os.environ['HYPRUNE_GIZMO_OBJECTS'] = '1'
    os.environ['HYPRUNE_GIZMO_UNIVERSAL'] = '1'
    # A keepClear volume (world 0.9 has them; a development file stands in) east of the armillary.
    keep = s.STATE / 'keepclear.json'
    keep.write_text(json.dumps([{'id': 'test-passage', 'min': [3.6, 3.0, -11.0], 'max': [4.4, 6.0, -9.0]}]))
    os.environ['HYPRUNE_GIZMO_KEEPCLEAR'] = str(keep)
    source = os.environ.get('HYPRUNE_GIZMO_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD))
    # Fail fast: the suite edits the Sun Court world object (Lumen Reach 0.10 and later).
    if not (pathlib.Path(source) / 'world.json').is_file():
        raise SystemExit(f"--gizmo needs a Lumen Reach package with the world object '{OBJECT.split(':', 1)[1]}' "
                         f"(Lumen Reach 0.10+); there is none at {source}. Set HYPRUNE_GIZMO_WORLD to one.")
    manifest = json.loads((pathlib.Path(source) / 'world.json').read_text())
    if not any(o.get('id') == OBJECT.split(':', 1)[1] for o in manifest.get('objects', [])):
        raise SystemExit(f"--gizmo needs Lumen Reach with the world object '{OBJECT.split(':', 1)[1]}' (world format 0.8, "
                         f"Lumen Reach 0.10+); {source} is version {manifest.get('version')}. "
                         "Set HYPRUNE_GIZMO_WORLD to a newer package.")
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    (s.STATE / 'editor.json').unlink(missing_ok=True)
    (s.STATE / 'layout.json').unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    results = []
    out = s.STATE / 'gizmo'
    out.mkdir(exist_ok=True)

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS gizmo:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)

    def held(mods, fn, seconds=1.2):
        """Holds modifier keys (wtype -P) around fn()."""
        a = []
        for name, key in mods:
            a += ['-M', name, '-P', key]
        tail = []
        for name, key in reversed(mods):
            tail += ['-p', key, '-m', name]
        hold = s.envrun('wtype', *a, '-s', str(int(seconds * 1000)), *tail); children.append(hold)
        time.sleep(.2); fn(); hold.wait(timeout=8); time.sleep(.15)

    shift = [('shift', 'Shift_L')]
    ctrl = [('ctrl', 'Control_L')]
    for _ in range(4):
        xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    def editor():
        return state()['editor']

    def diag():
        return s.camera()

    def gizmo():
        return diag()['gizmo']

    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})')
        time.sleep(.25)

    def look(pos, target):
        d = [target[i] - pos[i] for i in range(3)]
        camera(pos, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def shot(name):
        path = out / name
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        return path

    def aim(part):
        """Turns the camera (same position: same gizmo layout) so the reticle sits on a handle.
        View-aligned handles (view ring, trackball) move with the view: re-aim until it holds."""
        for _ in range(6):
            g = wait(lambda: gizmo() if part in gizmo()['handles'] else None, 5)
            look(diag()['position'], g['handles'][part]['world'])
            time.sleep(.15)
            if gizmo()['hover'] == part:
                return gizmo()
        return wait(lambda: gizmo() if gizmo()['hover'] == part else None, 3)

    def drag(dx, dy, steps=6, before=None, release=True):
        pointer('button', 272, 1); time.sleep(.15)
        if before:
            before()
        for _ in range(steps):
            pointer(dx / steps, dy / steps); time.sleep(.05)
        time.sleep(.2)
        g = gizmo()
        if release:
            pointer('button', 272, 0); time.sleep(.25)
        return g

    def record(id_=OBJECT):
        """The object's overlay record. World objects are RFC-0011 overrides in editor.json
        (IPC 0.22 publishes them in the objects key); scene nodes stay in editor.objects."""
        nodes = editor()['objects']  # also reads pending notices off the socket
        if not id_.startswith('object:'):
            return next((o for o in nodes if o['objectId'] == id_), None)
        path = s.STATE / 'editor.json'
        overrides = json.loads(path.read_text()).get('overrides', []) if path.exists() else []
        o = next((r for r in overrides if r['objectId'] == id_.split(':', 1)[1] and 'pose' in r), None)
        return o and {'objectId': id_, 'position': o['pose']['position'], 'rotation': o['pose']['rotation'],
                      'scale': [o.get('scale', 1)] * 3}

    def transform():
        return gizmo()['transform']

    def mode(name):
        for _ in range(5):
            if gizmo()['mode'] == name:
                return
            pointer('scroll', -1); time.sleep(.15)
        assert gizmo()['mode'] == name, gizmo()['mode']

    W, H = 1920, 1080

    def along(part, pixels):
        """Pointer motion of `pixels` along a handle's screen direction (after aim)."""
        g = gizmo()
        px, py = g['pivotScreen']
        dx, dy = (.5 - px) * W, (.5 - py) * H
        n = math.hypot(dx, dy) or 1
        return dx / n * pixels, dy / n * pixels

    def probe(x, y, z):
        path = s.STATE / 'probe.json'
        s.ctl('eval', f'local f = assert(io.open({json.dumps(str(path))}, "w")); f:write(hl.plugin.hyprune.gizmo_probe({x},{y},{z})); f:close()')
        return json.loads(path.read_text())

    def pos():
        return transform()['position']

    def notices(code):
        return [e['params'] for e in client.events if e.get('method') == 'runtime.notice' and e['params']['code'] == code]

    def undo():
        keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'z', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.4)

    def typed(*names):
        for n in names:
            keys('-k', n); time.sleep(.08)

    def sheet(window):
        """Before/after stills: the placed window in each mode from three angles. Works with
        the M3B plugin too (HYPRUNE_NESTED_PLUGIN): no gizmo diagnostics are needed."""
        build = os.environ.get('GIZMO_SHEET_LABEL', 'new')
        w = window['position']
        look([w[0], w[1] + .3, w[2] + 3.2], w); time.sleep(.3)
        pointer('button', 272); time.sleep(.2); pointer('button', 272, 0)
        wait(lambda: editor()['selectedKind'] == 'window', 5)
        angles = {'front': [w[0], w[1] + 1.0, w[2] + 3.0], 'three-quarter': [w[0] + 2.4, w[1] + 1.4, w[2] + 2.2],
                  'edge-on': [w[0], w[1], w[2] + 3.2]}
        modes = ['move', 'rotate', 'scale'] + (['universal'] if 'gizmo' in diag() else [])
        for m in modes:
            for _ in range(5):
                current = diag()['gizmo']['mode'] if 'gizmo' in diag() else editor()['submode']
                if current == m:
                    break
                pointer('scroll', -1); time.sleep(.2)
            for name, at in angles.items():
                # The reticle rests below-left of the handles: no hover in the comparison.
                look(at, [w[0] - 1.35, w[1] - .95, w[2]]); time.sleep(.4)
                shot(f'sheet-{build}-window-{m}-{name}.png')
        # The world object, same three cameras around its pivot.
        if 'gizmo' in diag():
            for _ in range(5):
                if diag()['gizmo']['mode'] == 'move':
                    break
                pointer('scroll', -1); time.sleep(.2)
            look([-7.0, 4.6, -6.2], [-7.0, 4.18, -10.0]); time.sleep(.3)
            pointer('button', 272); time.sleep(.2); pointer('button', 272, 0)
            wait(lambda: editor()['selectedId'] == OBJECT, 5)
            c = gizmo()['pivotPoint']
            views = {'front': [c[0], c[1] + .9, c[2] + 5.2], 'three-quarter': [c[0] + 3.4, c[1] + 2.6, c[2] + 3.6],
                     'edge-on': [c[0], c[1], c[2] + 5.2]}
            for m in ['move', 'rotate', 'scale', 'universal']:
                for _ in range(5):
                    if diag()['gizmo']['mode'] == m:
                        break
                    pointer('scroll', -1); time.sleep(.2)
                for name, at in views.items():
                    # Reticle off the handles (below-left), as for the window.
                    look(at, [c[0] - 2.4, c[1] - 1.7, c[2]]); time.sleep(.4)
                    shot(f'sheet-{build}-object-{m}-{name}.png')
        print('PASS gizmo: sheet stills', build, flush=True)

    def demo(window):
        """45-60 s recording (GIZMO_VIDEO=<file>): object move / snap / plane, rotate with
        snapping, uniform scale, a refused pose, the window, universal mode."""
        import signal
        out = os.environ['GIZMO_VIDEO']
        timeline = []
        t0 = [0.]

        def mark(text):
            timeline.append({'time': round(time.monotonic() - t0[0], 2), 'action': text})

        cam = cinema.Cinema(s)

        def glide(eye, target, seconds=1.0):
            # An eased move made by core per presented frame; released after (look() and aim() set the camera).
            cam.move_to(eye, target=target, seconds=seconds)
            cam.stop()

        def slow(dx, dy, seconds=1.2, shift_=False, release=True):
            hold = None
            pointer('button', 272, 1); time.sleep(.2)
            if shift_:
                hold = s.envrun('wtype', '-M', 'shift', '-P', 'Shift_L', '-s', str(int(seconds * 1000) + 900), '-p', 'Shift_L', '-m', 'shift')
                children.append(hold); time.sleep(.25)
            n = max(4, int(seconds / .1))  # pointer() itself paces ~0.1 s per step
            for _ in range(n):
                pointer(dx / n, dy / n)
            time.sleep(.5)
            if release:
                pointer('button', 272, 0)
            if hold:
                hold.wait(timeout=8)
            time.sleep(.4)

        c = [-7.0, 4.83, -10.0]
        view = [c[0] + 3.6, c[1] + 1.8, c[2] + 4.2]
        look(view, [c[0], c[1] - .3, c[2]]); time.sleep(1.5)
        clip = pathlib.Path(out).with_suffix('.capture.mkv')
        recorder = cam.recording(clip)
        t0[0] = recorder.__enter__()
        try:
            mark('Tool 4 on a world 0.8 object: select the armillary')
            pointer('button', 272); time.sleep(.15); pointer('button', 272, 0)
            wait(lambda: editor()['selectedId'] == OBJECT, 5); time.sleep(.8)
            c = gizmo()['pivotPoint']
            mode('move'); time.sleep(.3)
            mark('Hover highlights; drag the X arrow with a live readout')
            aim('move-x'); time.sleep(.8)
            slow(*along('move-x', 150), seconds=1.6)
            mark('Shift: 0.1 m snapping with tick marks')
            aim('move-y'); time.sleep(.4)
            slow(*along('move-y', 110), seconds=1.8, shift_=True)
            mark('Plane square: move in X and Z')
            aim('plane-zx'); time.sleep(.4)
            slow(-70, 35, seconds=1.4)
            mark('Rotate: rings face the camera; Shift snaps to 15 degrees')
            mode('rotate'); time.sleep(.6)
            aim('rotate-y'); time.sleep(.4)
            slow(-150, 60, seconds=2.0, shift_=True)
            aim('rotate-view'); time.sleep(.4)
            mark('Outer view ring')
            slow(40, 70, seconds=1.2)
            mark('Scale: world objects scale uniformly from the ring')
            mode('scale'); time.sleep(.6)
            aim('scale-uniform'); time.sleep(.3)
            slow(-30, -20, seconds=1.2)
            mark('Towards a wall or the pedestal: red ghost, BLOCKED, refused on release')
            mode('move'); time.sleep(.3)
            c = gizmo()['pivotPoint']
            # The nearest refused pose along an axis (probe), then a real drag past it.
            best = None
            for part, axis, sign in [('move-y', 1, -1), ('move-x', 0, 1), ('move-x', 0, -1), ('move-z', 2, -1), ('move-z', 2, 1)]:
                for k in range(1, 25):
                    q = list(c); q[axis] += sign * .25 * k
                    if not probe(*q)['ok']:
                        if best is None or k < best[3]:
                            best = (part, axis, sign, k)
                        break
            part, axis, sign, k = best
            side = [c[0] + 4.2, c[1] + 2.4, c[2] + 5.0]
            glide(side, [c[0] + (sign * .125 * k if axis == 0 else 0), c[1] - (.125 * k if axis == 1 else .3), c[2] + (sign * .125 * k if axis == 2 else 0)], .8)
            g = aim(part); time.sleep(.2)
            hp = g['handles'][part]['screen']; pv = g['pivotScreen']
            metres_per_px = g['size'] / max(1, math.hypot((hp[0] - pv[0]) * W, (hp[1] - pv[1]) * H) / .9)
            towards = (1 if (sign > 0) == (g['sign'][axis] > 0) else -1)
            px = min(500, (.25 * k + .4) / metres_per_px)
            pointer('button', 272, 1); time.sleep(.2)
            dx, dy = along(part, px * towards)
            for _ in range(22):
                pointer(dx / 22, dy / 22)
            time.sleep(1.6); refused = gizmo()
            pointer('button', 272, 0); time.sleep(1.0)
            mark('refused: ' + (refused['rule'] or 'none'))
            mark('Undo (Ctrl+Z) and redo')
            undo(); time.sleep(.8)
            keys('-M', 'ctrl', '-P', 'Control_L', '-M', 'shift', '-P', 'Shift_L', '-k', 'z', '-p', 'Shift_L', '-m', 'shift', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.8)
            mark('A window: select, move along its own X, scale its client')
            wx = window['position']
            front = [wx[0] + 1.2, wx[1] + .7, wx[2] + 3.4]
            glide(front, wx, 1.4)
            pointer('button', 272); time.sleep(.15); pointer('button', 272, 0)
            wait(lambda: editor()['selectedKind'] == 'window', 5); time.sleep(.6)
            aim('move-x'); time.sleep(.3)
            slow(*along('move-x', 120), seconds=1.4)
            mode('scale'); time.sleep(.5)
            aim('scale-x'); time.sleep(.3)
            slow(*along('scale-x', 70), seconds=1.4)
            time.sleep(.6)
            mode('rotate'); time.sleep(.4)
            aim('rotate-y'); time.sleep(.3)
            slow(-90, 0, seconds=1.4, shift_=True)
            mark('Universal mode: move, rotate and scale handles together')
            glide([c[0] + 3.4, c[1] + 1.6, c[2] + 3.8], c, 1.4)
            pointer('button', 272); time.sleep(.15); pointer('button', 272, 0)
            wait(lambda: editor()['selectedId'] == OBJECT, 5)
            mode('universal'); time.sleep(.5)
            orbit = []  # orbit: axes flip to the visible side, none collapses
            for k in range(7):
                a = .9 * k / 6
                eye = [c[0] + 5.1 * math.sin(.75 - a), c[1] + 1.6, c[2] + 5.1 * math.cos(.75 - a)]
                d = [c[i] - eye[i] for i in range(3)]
                orbit.append({'t': k * .7, 'eye': eye, 'yaw': math.atan2(d[0], -d[2]) + .12, 'pitch': math.atan2(d[1], math.hypot(d[0], d[2]))})
            orbit[0]['ease'] = 'in'; orbit[-2]['ease'] = 'out'
            cam.run(orbit, blend_ms=400); cam.stop()
            time.sleep(1.0)
            elapsed = time.monotonic() - t0[0]
            if elapsed < 46:
                time.sleep(46 - elapsed)
        finally:
            recorder.__exit__(None, None, None)
        cinema.encode([clip], out, trim=.5)
        print('gizmo demo:', cinema.gate_summary(cinema.gate(out)), flush=True)
        pathlib.Path(out).with_suffix('.timeline.json').write_text(json.dumps(timeline, indent=2))
        print('PASS gizmo: demo recorded', round(time.monotonic() - t0[0], 1), 's', flush=True)

    # World, a placed window and tool 4.
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({monitor="HEADLESS-1"}))')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="20"}))')
    title = 'Gizmo window'
    children.append(s.envrun('kitty', '--class', 'hyprune-gizmo', '--title', title, '--override', 'font_size=16',
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    wait(lambda: any(w['title'] == title for w in json.loads(s.ctl('clients', '-j'))))
    time.sleep(.5)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="91"}))')
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active']); time.sleep(1)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    wait(lambda: editor()['unplaced']['count'] >= 1, 10)
    # Place the window ahead, from the arrival spawn (an open view by design).
    time.sleep(.5)
    pointer('button', 272); time.sleep(.6)
    if editor()['unplaced']['count']:
        shot('placement-debug.png')
        print('DEBUG', editor()['tool'], editor()['unplaced'], [e['params'] for e in client.events if e.get('method') == 'runtime.notice'][-5:], diag()['position'], flush=True)
    wait(lambda: editor()['unplaced']['count'] == 0, 10)
    window = next(c for c in s.camera()['captures'] if c['title'] == title)
    check('window placed in the world', window['visible'], {'position': window['position']})

    keys('-k', '4'); time.sleep(.3)
    if os.environ.get('GIZMO_STAGE') == 'sheet':
        sheet(window)
        client.call('runtime.exit', {})
        return
    check('tool 4 selected', editor()['tool'] == 4 and gizmo()['objectsEnabled'])
    if os.environ.get('GIZMO_STAGE') == 'demo':
        demo(window)
        client.call('runtime.exit', {})
        return
    # Select the armillary (a world 0.8 placed object).
    eye = [-7.0, 4.6, -6.2]
    look(eye, [-7.0, 4.18, -10.0]); time.sleep(.3)
    pointer('button', 272); time.sleep(.3); pointer('button', 272, 0)
    wait(lambda: editor()['selectedId'] == OBJECT, 5)
    check('a world 0.8 object is selectable', editor()['selectedKind'] == 'object', editor()['selectedId'])
    start = transform()
    check('gizmo drawn at constant screen size', gizmo()['visible'] and gizmo()['primitives'] > 20, {'size': gizmo()['size']})

    # Stills: each mode from three angles (front, high three-quarter, level: edge-on ring,
    # end-on axis).
    c = gizmo()['pivotPoint']
    angles = {'front': [c[0], c[1] + .9, c[2] + 5.2], 'three-quarter': [c[0] + 3.4, c[1] + 2.6, c[2] + 3.6],
              'edge-on': [c[0], c[1], c[2] + 5.2]}
    for m in ['move', 'rotate', 'scale', 'universal']:
        mode(m)
        for name, at in angles.items():
            look(at, c); time.sleep(.35)
            shot(f'gizmo-{m}-{name}.png')
    if os.environ.get('GIZMO_STOP_AFTER') == 'stills':
        print('PASS gizmo: stills only', flush=True)
        client.call('runtime.exit', {})
        return

    three = [c[0] + 3.4, c[1] + 2.0, c[2] + 3.6]  # all three axes and planes readable
    look(three, c); time.sleep(.3)
    mode('move')
    # 1. Move along X: hover highlight, live readout, only X changes.
    p0 = pos()
    g = aim('move-x')
    check('hovering a handle highlights it and targets the reticle', g['hover'] == 'move-x' and state()['interaction']['reticle']['state'] == 'target',
          {'hover': g['hover'], 'reticle': state()['interaction']['reticle']})
    dx, dy = along('move-x', 160)
    during = drag(dx, dy)
    p1 = pos()
    check('move X drags along X only, with a live readout', p1[0] - p0[0] > .1 and abs(p1[1] - p0[1]) < 1e-3 and abs(p1[2] - p0[2]) < 1e-3
          and during['readout'].startswith('dX +') and during['active'] == 'move-x', {'from': p0, 'to': p1, 'readout': during['readout']})
    rec = record()
    check('the move is an overlay record of the world object', rec and abs(rec['position'][0] - p1[0]) < 1e-4, rec)
    # 2. Shift snaps: 0.1 m steps, tick marks drawn.
    look(three, c); aim('move-y')
    p0 = pos()
    plain = drag(*along('move-y', 70), release=False); pointer('button', 272, 0); time.sleep(.25)
    look(three, c); aim('move-y'); p0 = pos()
    pointer('button', 272, 1); time.sleep(.15)
    hold = s.envrun('wtype', '-M', 'shift', '-P', 'Shift_L', '-s', '2200', '-p', 'Shift_L', '-m', 'shift'); children.append(hold); time.sleep(.3)
    mx, my = along('move-y', 95)
    for _ in range(5):
        pointer(mx / 5, my / 5); time.sleep(.05)
    time.sleep(.3); snapped = gizmo()
    pointer('button', 272, 0); hold.wait(timeout=6); time.sleep(.3)
    p1 = pos(); dy_ = p1[1] - p0[1]
    check('Shift snaps moves to 0.1 m and draws tick marks', abs(dy_) > .05 and abs(dy_ * 10 - round(dy_ * 10)) < 2e-3
          and snapped['primitives'] > plain['primitives'] + 20, {'delta': dy_, 'primitives': [plain['primitives'], snapped['primitives']]})
    # 3. Plane: the horizontal (ZX) square keeps Y.
    look(three, c); aim('plane-zx'); p0 = pos()
    mid = drag(60, 40)
    p1 = pos()
    check('the ZX plane square moves in X and Z, never Y', abs(p1[1] - p0[1]) < 1e-3 and math.hypot(p1[0] - p0[0], p1[2] - p0[2]) > .05,
          {'from': p0, 'to': p1, 'active': mid['active'], 'readout': mid['readout'], 'valid': mid['valid'], 'rule': mid['rule']})
    # 4. Centre disc: screen plane.
    look(three, c); aim('move-view'); p0 = pos()
    drag(50, -40)
    p1 = pos()
    check('the centre disc moves in the screen plane', math.dist(p0, p1) > .05, {'from': p0, 'to': p1})
    # 5. Undo / redo (Ctrl+Z, Ctrl+Shift+Z).
    before_undo = record()
    undo()
    undone = record()
    check('Ctrl+Z undoes the last transform', undone != before_undo and abs(undone['position'][1] - p0[1]) < 1e-3, {'before': before_undo['position'], 'after': undone['position']})
    keys('-M', 'ctrl', '-P', 'Control_L', '-M', 'shift', '-P', 'Shift_L', '-k', 'z', '-p', 'Shift_L', '-m', 'shift', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.4)
    check('Ctrl+Shift+Z redoes it', record()['position'] == before_undo['position'], record()['position'])
    # The chord owns Ctrl: no fly-down (crouch in walk) on undo/redo; Ctrl alone still descends.
    y0 = diag()['position'][1]
    undo(); time.sleep(.3)
    keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'y', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.4)
    y1 = diag()['position'][1]
    hold = s.envrun('wtype', '-M', 'ctrl', '-P', 'Control_L', '-s', '800', '-p', 'Control_L', '-m', 'ctrl'); children.append(hold)
    hold.wait(timeout=5); time.sleep(.3)
    y2 = diag()['position'][1]
    check('Ctrl+Z / Ctrl+Y consume Ctrl (no fly-down); Ctrl alone still flies down', abs(y1 - y0) < 1e-3 and y2 < y1 - .1
          and record()['position'] == before_undo['position'], {'before': y0, 'afterChords': y1, 'afterCtrlHeld': y2})
    look(three, c)
    # 6. Numeric entry: start a drag, type, release, Enter.
    look(three, c); aim('move-y'); p0 = pos()
    pointer('button', 272, 1); time.sleep(.15)
    typed('0', 'period', '5'); time.sleep(.2)
    mid = gizmo()
    pointer('button', 272, 0); time.sleep(.3)
    still_open = gizmo()['active'] == 'move-y'
    keys('-k', 'Return'); time.sleep(.4)
    p1 = pos()
    check('numeric entry: typed 0.5 m applies exactly on Enter', still_open and mid['typed'] == '0.5' and 'MOVE Y = 0.5_' in mid['readout']
          and abs(p1[1] - p0[1] - .5) < 1e-4 and gizmo()['active'] == 'none', {'typed': mid['typed'], 'readout': mid['readout'], 'delta': p1[1] - p0[1]})
    # 7. Escape cancels a drag.
    look(three, c); aim('move-x'); p0 = pos(); r0 = record()
    pointer('button', 272, 1); time.sleep(.1)
    for _ in range(4):
        pointer(*[v / 4 for v in along('move-x', 120)]); time.sleep(.05)
    keys('-k', 'Escape'); time.sleep(.3); pointer('button', 272, 0); time.sleep(.2)
    check('Escape cancels the drag', record() == r0 and pos() == p0 and gizmo()['active'] == 'none', pos())
    # 8. Refused: down into the pedestal and floor. Red ghost, BLOCKED readout, nothing committed.
    look(three, c); aim('move-y'); r0 = record()
    pointer('button', 272, 1); time.sleep(.1)
    typed('minus', '4', 'period', '5'); time.sleep(.4)
    blocked = gizmo(); reticle = state()['interaction']['reticle']
    shot('gizmo-refused-walls.png')
    keys('-k', 'Return'); time.sleep(.3); pointer('button', 272, 0); time.sleep(.3)
    check('a pose inside walls shows the red ghost and is refused', not blocked['valid'] and blocked['rule'] == 'walls' and 'BLOCKED' in blocked['readout']
          and reticle['state'] == 'invalid' and record() == r0 and notices('placement.blocked'),
          {'rule': blocked['rule'], 'readout': blocked['readout'], 'reticle': reticle, 'notice': notices('placement.blocked')[-1:]})
    # 9. Every rule on real world data (probe: the selection's bounds centre moved there).
    c_now = gizmo()['pivotPoint']
    rules = {
        'open air': probe(c_now[0], c_now[1] + .3, c_now[2]),
        'walls': probe(c_now[0], .4, c_now[2]),
        'keep-clear': probe(4.0, c_now[1], -10.0),
        'approach': probe(-13.3, 2.4, 2.2),
        # Lifted clear of the floor: the bounds' lower half reaches through the window.
        'window': probe(window['position'][0], gizmo()['bounds'][1] + .15, window['position'][2]),
        'playable': probe(c_now[0], -40, c_now[2]),
    }
    check('placement rules on Lumen Reach: walls, keep-clear, approach, window, below the world',
          rules['open air']['ok'] and rules['walls']['rule'] == 'walls' and rules['keep-clear']['rule'] == 'keep-clear'
          and rules['approach']['rule'] == 'approach' and rules['window']['rule'] == 'window' and rules['playable']['rule'] == 'playable', rules)
    # 10. Rotate: Y ring, Shift snaps to 15 degrees; view ring; trackball; an edge-on ring.
    mode('rotate')
    look(three, c); aim('rotate-y'); q0 = transform()['rotation']
    pointer('button', 272, 1); time.sleep(.15)
    hold = s.envrun('wtype', '-M', 'shift', '-P', 'Shift_L', '-s', '2000', '-p', 'Shift_L', '-m', 'shift'); children.append(hold); time.sleep(.3)
    for _ in range(6):
        pointer(-20, 12); time.sleep(.05)
    time.sleep(.3); rot = gizmo()
    pointer('button', 272, 0); hold.wait(timeout=6); time.sleep(.3)
    q1 = transform()['rotation']
    angle = math.degrees(2 * math.atan2(q1[1], q1[3]))
    check('the Y ring turns about Y only; Shift snaps to 15 degrees', abs(q1[0]) < 1e-3 and abs(q1[2]) < 1e-3 and abs(angle) > 7 and abs(angle / 15 - round(angle / 15)) < .01
          and rot['readout'].startswith('Y '), {'angle': angle, 'readout': rot['readout']})
    look(three, c); aim('rotate-view'); q0 = transform()['rotation']
    drag(30, 50)
    check('the outer view ring turns about the view axis', transform()['rotation'] != q0, transform()['rotation'])
    look(three, c); aim('rotate-free'); q0 = transform()['rotation']
    drag(60, 30)
    check('the trackball turns freely inside the sphere', transform()['rotation'] != q0, transform()['rotation'])
    pc = gizmo()['pivotPoint']
    level = [pc[0], pc[1], pc[2] + 5.2]  # level with the pivot: the Y ring is a line, Z points at us
    look(level, pc); g = aim('rotate-y'); q0 = transform()['rotation']
    drag(80, 0)
    q1 = transform()['rotation']
    check('an edge-on ring (a line on screen) still drags', q1 != q0 and g['hover'] == 'rotate-y', {'from': q0, 'to': q1})
    look(level, pc); mode('move'); time.sleep(.2)
    g = gizmo()
    check('an axis pointing at the camera is disabled, never a nub', 'move-z' not in g['handles'] and 'move-x' in g['handles'] and 'move-y' in g['handles'],
          sorted(g['handles']))
    # 11. Local / world axes (Secondary) and the base pivot (period).
    look(three, c)
    world_x = gizmo()['handles']['move-x']['world']
    pointer('button', 273); time.sleep(.3)
    local_x = gizmo()['handles'].get('move-x', {}).get('world')
    check('Secondary toggles local axes; handles follow the rotation', gizmo()['local'] and local_x and math.dist(local_x, world_x) > .02, {'world': world_x, 'local': local_x})
    pointer('button', 273); time.sleep(.2)
    centre_pivot = gizmo()['pivotPoint']
    keys('-k', 'period'); time.sleep(.3)
    base_pivot = gizmo()['pivotPoint']
    check('period puts the pivot at the base', gizmo()['pivot'] == 'base' and base_pivot[1] < centre_pivot[1] - .3, {'centre': centre_pivot, 'base': base_pivot})
    keys('-k', 'period'); time.sleep(.2)
    # 12. Drop to floor (End).
    look(three, c); aim('move-y'); p0 = pos()
    pointer('button', 272, 1); time.sleep(.1); typed('1', 'period', '5'); keys('-k', 'Return'); time.sleep(.3); pointer('button', 272, 0); time.sleep(.2)
    lifted = pos()
    keys('-k', 'End'); time.sleep(.5)
    dropped = pos()
    check('End drops the object onto the surface below it', lifted[1] - p0[1] > 1.4 and dropped[1] < lifted[1] - .3 and probe(*gizmo()['pivotPoint'])['supported'],
          {'lifted': lifted, 'dropped': dropped})
    # 13. Scale (world objects: uniform only, from the ring); Shift snaps to 0.1x.
    mode('scale')
    g = gizmo()
    check('world objects offer uniform scale only', 'scale-uniform' in g['handles'] and not any(k in g['handles'] for k in ['scale-x', 'scale-y', 'scale-z']), sorted(g['handles']))
    look(three, c); aim('scale-uniform'); s0 = transform()['scale']
    pointer('button', 272, 1); time.sleep(.15)
    hold = s.envrun('wtype', '-M', 'shift', '-P', 'Shift_L', '-s', '1800', '-p', 'Shift_L', '-m', 'shift'); children.append(hold); time.sleep(.3)
    for _ in range(5):
        pointer(-12, -8); time.sleep(.05)
    time.sleep(.3); sc = gizmo()
    pointer('button', 272, 0); hold.wait(timeout=6); time.sleep(.3)
    s1 = transform()['scale']
    check('uniform scale with Shift snaps to 0.1x', s1[0] == s1[1] == s1[2] and s1[0] != s0[0] and abs(s1[0] / s0[0] * 10 - round(s1[0] / s0[0] * 10)) < 1e-3
          and sc['readout'].startswith('SCALE x'), {'from': s0, 'to': s1, 'readout': sc['readout']})
    # 14. Universal: all three kinds of handle at once.
    mode('universal')
    look(three, c)
    kinds = sorted(gizmo()['handles'])
    check('universal mode shows move, rotate and scale handles together', 'move-x' in kinds and 'rotate-y' in kinds and 'scale-uniform' in kinds, kinds)
    aim('rotate-x'); q0 = transform()['rotation']; drag(40, 30)
    check('universal rotate ring drags', transform()['rotation'] != q0)
    shot('gizmo-universal-after.png')
    # 15. The window: select, move along its own X, scale its client, undo.
    mode('move')
    wx = window['position']
    front = [wx[0], wx[1] + .4, wx[2] + 3.2]
    look(front, wx); time.sleep(.3)
    pointer('button', 272); time.sleep(.2); pointer('button', 272, 0); time.sleep(.3)
    sid = wait(lambda: editor()['selectedId'] if editor()['selectedKind'] == 'window' else None, 5)
    check('the window is selectable', sid, sid)
    def cap():
        return next(v for v in s.camera()['captures'] if v['id'] == sid)
    w0 = cap()
    look(front, wx); aim('move-x')
    drag(*along('move-x', 140))
    w1 = cap()
    check('window move along X', abs(w1['position'][0] - w0['position'][0]) > .1 and abs(w1['position'][1] - w0['position'][1]) < .01, {'from': w0['position'], 'to': w1['position']})
    mode('scale')
    g = gizmo()
    check('windows scale in X and Y (no depth axis)', 'scale-x' in g['handles'] and 'scale-z' not in g['handles'], sorted(g['handles']))
    look(front, wx); aim('scale-x'); px0 = cap()['pixels']
    drag(*along('scale-x', 90))
    time.sleep(.6)
    px1 = cap()['pixels']
    check('window scale X resizes the client', px1[0] != px0[0], {'from': px0, 'to': px1})
    undo(); time.sleep(.4)
    check('Ctrl+Z restores the window size', cap()['pixels'][0] == px0[0], cap()['pixels'])
    shot('gizmo-window-scale.png')
    (s.ROOT / 'docs/evidence/gizmo').mkdir(exist_ok=True)
    (s.ROOT / 'docs/evidence/gizmo/nested-gizmo.json').write_text(json.dumps({'nestedOnly': True, 'world': WORLD, 'checks': results}, indent=2) + '\n')
    print('PASS gizmo:', len(results), 'checks', flush=True)
    client.call('runtime.exit', {})
