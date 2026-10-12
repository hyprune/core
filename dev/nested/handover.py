"""Every way a client comes into the world floats it: dev/nested/run.sh --handover.

Nested only (run.sh holds /tmp/worldshell-nested.lock). Two outputs: the nested window
(WAYLAND-1) and HEADLESS-1 with the world (room workspace 20, Lumen Reach homes 21-24).
Tiled clients (kitty, Chromium, a GTK dialog) on the other output and on the Sun Court
home (21) are brought into the world every way a player can: opened in the room, moved
to the room from the other output, dragged across the outputs with the native Super+LMB
drag, Director pull and recall, tool 5 pocket and place, carried off the home wall. On
the home, the first arrival keeps the wall (tiled) and later ones take the area's tool
anchors (floating, on the desks). Each
must be floating (native relayouts no longer resize it), captured and visible, on the
world output; then resized (Super+RMB corner, Super+Shift+Wheel) and carried it stays
visible and floating. Returned to its wall it is tiled there again. Chromium also uses a
wp_viewporter source crop, which used to keep it out of the world.
"""
import json, math, os, pathlib, shutil, subprocess, time
import session as s
from m2 import wait, keys, pose

LUMEN = 'com.hyprune.lumen-reach'


def prepare(installed):
    shutil.copytree(s.ROOT.parent / 'worlds/dist' / LUMEN, installed / LUMEN, dirs_exist_ok=True)


def acceptance(client, children, args, token, socket):
    checks = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        checks.append({'name': name, 'evidence': data}); print('PASS handover:', name, flush=True)
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True); children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush(); assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)

    def move_to(x, y):
        for _ in range(6):
            cx, cy = map(float, s.ctl('cursorpos').split(','))
            if abs(cx - x) < 2 and abs(cy - y) < 2:
                return
            pointer(x - cx, y - cy); time.sleep(.05)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    def clients(): return json.loads(s.ctl('clients', '-j'))
    def native(title): return next((c for c in clients() if title in c['title']), None)
    def capture(title): return next((c for c in s.camera()['captures'] if title in c['title']), None)

    def report(title):
        c, cap = native(title), capture(title)
        return {'native': c and {'monitor': c['monitor'], 'workspace': c['workspace']['id'], 'floating': c['floating'], 'size': c['size']},
                'capture': cap and {k: cap[k] for k in ('id', 'visible', 'queued', 'placed', 'position', 'size', 'pixels') if k in cap}}
    def headless(): return next(m for m in json.loads(s.ctl('monitors', '-j')) if m['name'] == 'HEADLESS-1')
    def chord(): keys('-M', 'logo', '-P', 'Super_L', '-k', 'Tab', '-p', 'Super_L', '-m', 'logo'); time.sleep(.3)

    def in_world(name, title, workspace):
        """Floating, captured and visible, on the world output, on the expected workspace."""
        def ok():
            c, cap = native(title), capture(title)
            return bool(c and cap and c['floating'] and cap['visible'] and c['monitor'] == headless()['id'] and c['workspace']['id'] == workspace)
        try:
            wait(ok, timeout=8)
        except AssertionError:
            pass
        check(name, ok(), {title: report(title), 'uncaptured': s.camera()['uncaptured']})

    def look_at(eye, target):
        d = [target[i] - eye[i] for i in range(3)]
        pose(client, eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2]))); time.sleep(.4)

    def face(title, distance):
        cap = capture(title); n = cap['normal']
        for d in (distance, distance * .6, 1.2):  # step closer if another window is in front
            look_at([cap['position'][i] + d * n[i] for i in range(3)], cap['position'])
            if s.camera()['aim'] == cap['id']:
                return
        raise AssertionError(('cannot aim at', title, s.camera()['aim'], report(title)))

    def held(seconds, mods=(('logo', 'Super_L'),)):
        a, t = [], []
        for name, key in mods: a += ['-M', name, '-P', key]
        for name, key in reversed(mods): t += ['-p', key, '-m', name]
        h = s.envrun('wtype', *a, '-s', str(int(seconds * 1000)), *t); children.append(h); time.sleep(.25); return h

    def shot(name):  # HYPRUNE_HANDOVER_SHOTS=dir: stills of the moments worth showing
        if os.environ.get('HYPRUNE_HANDOVER_SHOTS'):
            out = pathlib.Path(os.environ['HYPRUNE_HANDOVER_SHOTS']); out.mkdir(parents=True, exist_ok=True)
            subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / f'{name}.png')], check=True)

    def resize_and_carry(title, workspace, distance):
        """Super+RMB corner resize, Super+Shift+Wheel content scale, Super+LMB carry."""
        face(title, distance); before = report(title)
        h = held(1.8); wait(lambda: s.camera()['superHeld'])
        pointer('button', 273, 1); pointer(40, 25); time.sleep(.2); pointer(30, 15); time.sleep(.3); pointer('button', 273, 0); h.wait(timeout=5)
        wait(lambda: s.camera()['gesture'] == 0); time.sleep(1)
        in_world(f'{title}: corner resize keeps it visible and floating', title, workspace)
        if title == 'Drag-Chromium':
            face(title, distance); shot('2-chromium-after-resize')
        check(f'{title}: the resize changed its native size', report(title)['native']['size'] != before['native']['size'],
              {'before': before, 'after': report(title)})
        face(title, distance)
        h = held(1.6, (('logo', 'Super_L'), ('shift', 'Shift_L'))); pointer('scroll', -2); time.sleep(.3); h.wait(timeout=5); time.sleep(1)
        in_world(f'{title}: content scale keeps it visible', title, workspace)
        start = None
        for dx, dy in ((120, 0), (-120, 0), (0, -60)):  # a turn can be blocked by nearby geometry
            face(title, distance); start = start or capture(title)['position']
            h = held(1.6); wait(lambda: s.camera()['superHeld'])
            pointer('button', 272, 1); wait(lambda: s.camera()['gesture'] == 1); grabbed = s.camera()['gestureId']
            pointer(dx, dy); time.sleep(.4)
            # A window carried off its home wall still hangs in front of it: releasing while the
            # reticle shows "drop" would set it back onto the wall. Turn on until it doesn't.
            for _ in range(6):
                if s.camera()['reticle'].get('action') != 'drop':
                    break
                pointer(dx, dy); time.sleep(.3)
            pointer('button', 272, 0)
            h.wait(timeout=5); wait(lambda: s.camera()['gesture'] == 0); time.sleep(.8)
            if math.dist(start, capture(title)['position']) > .2:
                break
        in_world(f'{title}: carried and released, still visible and floating', title, workspace)
        check(f'{title}: the carry moved it', grabbed == capture(title)['id'] and math.dist(start, capture(title)['position']) > .2,
              {'grabbed': grabbed, 'from': start, 'to': capture(title)['position']})

    def chromium(title, profile):
        page = f'data:text/html,<title>{title}</title><body style="background:%23234;color:white;font:40px sans-serif">{title}</body>'
        children.append(s.envrun('chromium', '--ozone-platform=wayland', f'--user-data-dir={s.STATE / "chromium" / profile}', '--no-first-run',
                                 '--no-default-browser-check', '--password-store=basic', f'--app={page}',
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    def kitty(title): children.append(s.envrun('kitty', '--title', title, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))

    # Clients start tiled: on the other output, and on the Sun Court home (21) of the world output.
    shutil.rmtree(s.STATE / 'chromium', ignore_errors=True)
    s.ctl('eval', 'hl.bind("SUPER + mouse:272", hl.dsp.window.drag(), { mouse = true })')  # the usual native drag
    client.call('world.load', {'worldId': LUMEN})
    other = next(m for m in json.loads(s.ctl('monitors', '-j')) if m['name'] != 'HEADLESS-1')
    s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{other["name"]}" }}))')
    chromium('Drag-Chromium', 'a'); wait(lambda: native('Drag-Chromium'), timeout=40)
    kitty('Move-Kitty'); wait(lambda: native('Move-Kitty'))
    children.append(s.envrun('zenity', '--info', '--title', 'Move-GTK', '--text', 'GTK client', stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    wait(lambda: native('Move-GTK'), timeout=20)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))'); s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ workspace = "21" }))')
    # Arrival order decides the home's anchors: Kitty-B keeps the wall, the others take desks.
    kitty('Home-Kitty-B'); wait(lambda: native('Home-Kitty-B'))
    chromium('Home-Chromium', 'b'); wait(lambda: native('Home-Chromium'), timeout=40)
    kitty('Home-Kitty-A'); wait(lambda: native('Home-Kitty-A')); time.sleep(1.5)
    tiled = ('Drag-Chromium', 'Move-Kitty', 'Home-Chromium', 'Home-Kitty-A', 'Home-Kitty-B')
    check('clients start tiled', not any(native(t)['floating'] for t in tiled), {t: report(t) for t in tiled})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ workspace = "20" }))')
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    check('the window on its home wall stays tiled there', not native('Home-Kitty-B')['floating'] and native('Home-Kitty-B')['workspace']['id'] == 21,
          report('Home-Kitty-B'))
    manifest = json.loads((s.ROOT.parent / 'worlds/dist' / LUMEN / 'world.json').read_text())
    anchors = {a['id']: a['pose']['position'] for a in manifest['anchors']}
    frame = next(f for f in s.camera()['workspaceFrames'] if f['workspaceId'] == 21)
    area = min(manifest['areas'], key=lambda a: math.dist(anchors[a['primaryAnchor']], frame['position']))
    spots = {k: anchors[k] for k in area['toolAnchors']}

    def desk(title):
        cap = capture(title)
        return cap and cap['visible'] and min(spots, key=lambda k: math.dist(spots[k], cap['position'])) \
            if cap and min(math.dist(p, cap['position']) for p in spots.values()) < .25 else None
    for title in ('Home-Chromium', 'Home-Kitty-A'):
        wait(lambda: desk(title), timeout=8)
    taken = [desk(t) for t in ('Home-Chromium', 'Home-Kitty-A')]
    check('later home arrivals take its tool anchors, floating and visible',
          all(taken) and len(set(taken)) == 2 and all(native(t)['floating'] for t in ('Home-Chromium', 'Home-Kitty-A')),
          {'anchors': taken, **{t: report(t) for t in ('Home-Chromium', 'Home-Kitty-A')}})

    # 1. Opened in the room.
    kitty('Room-Kitty'); wait(lambda: native('Room-Kitty'))
    in_world('opened in the room: floating world window', 'Room-Kitty', 20)
    # 2. Moved to the room from the other output.
    for title in ('Move-Kitty', 'Move-GTK'):
        s.ctl('eval', f'hl.dispatch(hl.dsp.window.move({{workspace="20",follow=false,window="address:{native(title)["address"]}"}}))')
        in_world(f'moved in from the other output: {title}', title, 20)
    # 3. Dragged across the outputs with the native drag (the free cursor reaches the other output).
    # Earlier arrivals took the free spots ahead; a window with no free full-size spot is
    # pocketed by design. Face open space for the next one.
    pose(client, s.camera()['position'], math.pi, 0); time.sleep(.4)
    chord(); wait(lambda: s.camera()['layerCursor'])
    c = native('Drag-Chromium'); move_to(c['at'][0] + c['size'][0] / 2, c['at'][1] + c['size'][1] / 2)
    h = held(3.0); pointer('button', 272, 1); time.sleep(.3)
    hx = headless()
    for step in range(12):
        cx, cy = map(float, s.ctl('cursorpos').split(','))
        pointer((hx['x'] + hx['width'] / 2 - cx) / (12 - step), (hx['y'] + hx['height'] / 2 - cy) / (12 - step)); time.sleep(.05)
    pointer('button', 272, 0); h.wait(timeout=5); time.sleep(.5)
    print('after drag', json.dumps(report('Drag-Chromium')), flush=True)
    chord(); wait(lambda: not s.camera()['layerCursor'])
    in_world('dragged across the outputs: Chromium (viewport crop) floating and visible', 'Drag-Chromium', 20)
    face('Drag-Chromium', 3.5); shot('1-chromium-dragged-in')
    for title, workspace, distance in (('Room-Kitty', 20, 2.5), ('Move-Kitty', 20, 2.5), ('Drag-Chromium', 20, 3.5)):
        resize_and_carry(title, workspace, distance)
    # 4. Director pull (the pulled window is carried in front of the player) ...
    client.call('surface.pull', {'surfaceId': capture('Home-Chromium')['id']})
    in_world('Director pull: Chromium from its home desk', 'Home-Chromium', 21)
    # 5. ... then tool 5: pocket it and place it again (placing sets it down).
    keys('-k', '5'); time.sleep(.3)
    wait(lambda: s.camera()['aim'] == capture('Home-Chromium')['id'], timeout=4); pointer('button', 274)
    wait(lambda: state()['editor']['unplaced']['count'] >= 1)
    check('tool 5 pocket: floating while pocketed', native('Home-Chromium')['floating'], report('Home-Chromium'))
    eye = s.camera()['position']
    for yaw in (math.pi / 2, -math.pi / 2, 0, math.pi):
        pose(client, eye, yaw, 0); time.sleep(.3); pointer('button', 272); time.sleep(.6)
        if state()['editor']['unplaced']['count'] == 0:
            break
    in_world('tool 5 place: floating and visible', 'Home-Chromium', 21)
    keys('-k', '1'); resize_and_carry('Home-Chromium', 21, 3.5)
    # 6. Carried off the home wall: Super+LMB on the wall where the native client is.
    f = next(f for f in s.camera()['workspaceFrames'] if f['workspaceId'] == 21)
    normal = [f['right'][1] * f['up'][2] - f['right'][2] * f['up'][1], f['right'][2] * f['up'][0] - f['right'][0] * f['up'][2],
              f['right'][0] * f['up'][1] - f['right'][1] * f['up'][0]]
    b, mon = native('Home-Kitty-B'), headless()
    u = (b['at'][0] - mon['x'] + b['size'][0] / 2) / mon['width'] - .5
    v = .5 - (b['at'][1] - mon['y'] + b['size'][1] / 2) / mon['height']
    target = [f['position'][i] + f['right'][i] * u * f['size'][0] + f['up'][i] * v * f['size'][1] for i in range(3)]
    look_at([f['position'][i] + normal[i] * 9 for i in range(3)], target); time.sleep(.4)
    h = held(3.0); wait(lambda: s.camera()['superHeld'])
    pointer('button', 272, 1); wait(lambda: s.camera()['gesture'] == 1, timeout=4)
    check('Super+LMB on the wall picks up the native client there', capture('Home-Kitty-B')['id'] == s.camera()['gestureId'],
          {'gestureId': s.camera()['gestureId'], 'kitty': report('Home-Kitty-B')})
    for _ in range(8):  # turn away with it (releasing while aiming at the wall drops it back in)
        pointer(-80, 0); time.sleep(.08)
    time.sleep(.3); pointer('button', 272, 0)
    h.wait(timeout=5); wait(lambda: s.camera()['gesture'] == 0); time.sleep(.5)
    in_world('carried off the home wall: floating and visible', 'Home-Kitty-B', 21)
    face('Home-Kitty-B', 3); shot('3-kitty-carried-off-its-wall')
    resize_and_carry('Home-Kitty-B', 21, 2.5)
    # 7. Director pull of a desk window, then recall everything (recalled windows come to the player).
    client.call('surface.pull', {'surfaceId': capture('Home-Kitty-A')['id']})
    in_world('Director pull: kitty from its home desk', 'Home-Kitty-A', 21)
    client.call('surfaces.recall', {})
    for title, workspace in (('Home-Kitty-A', 21), ('Home-Chromium', 21), ('Room-Kitty', 20), ('Drag-Chromium', 20)):
        in_world(f'Director recall: {title} comes to the player, floating and visible', title, workspace)
    # Returned to its wall, a window is tiled there again.
    client.call('surface.return', {'surfaceId': capture('Home-Kitty-B')['id']})
    try:
        wait(lambda: not native('Home-Kitty-B')['floating'], timeout=8)
    except AssertionError:
        pass
    check('returned to its home wall: tiled again', not native('Home-Kitty-B')['floating'] and native('Home-Kitty-B')['workspace']['id'] == 21, report('Home-Kitty-B'))
    subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(s.STATE / 'handover-after.png')], check=True)
    (s.ROOT / 'docs/evidence/handover.json').write_text(json.dumps({'nestedOnly': True, 'world': LUMEN, 'checks': checks}, indent=2) + '\n')
    print('PASS handover: every way into the world floats the client:', len(checks), 'checks', flush=True)
