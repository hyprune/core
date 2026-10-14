"""A user character set down on a route drawn with tool 3 (RFC-0011 "Use and binding"):
dev/nested/run.sh --route-carry.

Nested only. Lumen Reach (HYPRUNE_LUMEN_PACKAGE, else the worlds checkout's dist) and the
companion character library (HYPRUNE_COMPANION_LIBRARY: Drunk Ash, 1.9 m, spawned as the
user's own character with npcs.spawn, not as the companion). Everything uses the real input
flow on Signal Rise's open floor (where the owner drew theirs):
 1. Tool 3 (key 3), Alternate (middle button) closes the path, four floor clicks draw a
    square, key 1 leaves tool 3: one closed four-point user path (generic: a drawn path
    becomes a route when a character is put on it).
 2. Super+LMB on the spawned character picks it up; held with the aim 0.5 m outside the
    square's edge, the square is the snap target (highlighted, valid); released, the
    character is bound to it, the path became a walkable route and the character walks it.
 3. Held 1.6 m away from every route and released: it stands there (no snap).
 4. Rebind route (the Director's npcs.bind on a second freshly drawn square): bound, the
    path became a route, and it walks it.
Evidence: docs/evidence/route-carry.json (HYPRUNE_ROUTE_CARRY_SHOTS: elsewhere).
"""
import hashlib, json, math, os, pathlib, shutil, subprocess, time
import session as s
from m2 import wait, keys

LUMEN = 'com.hyprune.lumen-reach'
ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_COMPANION_LIBRARY', ''))
# The owner's square on Signal Rise (feet level 0), and a second one east of it for the rebind.
SQUARE = [[50.7, 0, -132.4], [49.9, 0, -137.5], [53.9, 0, -137.7], [54.4, 0, -133.3]]
SECOND = [[58, 0, -133], [58, 0, -137], [62, 0, -137], [62, 0, -133]]


def prepare(installed):
    if not ARCHIVE.is_file():
        raise SystemExit('--route-carry needs HYPRUNE_COMPANION_LIBRARY (a character library .tar.zst)')
    source = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--route-carry needs {LUMEN} at {source} (HYPRUNE_LUMEN_PACKAGE)')
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(source, installed / LUMEN)
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'companion.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)


def edge_distance(p, square):
    """Horizontal distance from p to the closed polygon's edges."""
    best = 1e9
    for i in range(len(square)):
        a, b = square[i], square[(i + 1) % len(square)]
        dx, dz = b[0] - a[0], b[2] - a[2]
        t = max(0., min(1., ((p[0] - a[0]) * dx + (p[2] - a[2]) * dz) / (dx * dx + dz * dz)))
        best = min(best, math.hypot(p[0] - a[0] - t * dx, p[2] - a[2] - t * dz))
    return best


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(os.environ.get('HYPRUNE_ROUTE_CARRY_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'route-carry-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS route-carry:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)

    def center():
        for _ in range(4):
            xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)

    def click(button=272):
        pointer('button', button, 1); time.sleep(.12); pointer('button', button, 0); time.sleep(.35)

    def held(mods, fn, seconds=2.8):
        a, tail = [], []
        for name, key in mods:
            a += ['-M', name, '-P', key]
        for name, key in reversed(mods):
            tail += ['-p', key, '-m', name]
        hold = s.envrun('wtype', *a, '-s', str(int(seconds * 1000)), *tail); children.append(hold)
        time.sleep(.3); fn(); hold.wait(timeout=12); time.sleep(.2)

    def camera(p, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({p[0]},{p[1]},{p[2]},{yaw},{pitch})'); time.sleep(.25)

    def look(eye, target):
        d = [target[i] - eye[i] for i in range(3)]
        camera(eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def shot(name):
        for _ in range(3):
            try:
                subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=10)
                return
            except subprocess.TimeoutExpired:
                pointer(1, 0); pointer(-1, 0)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    agent = lambda i: next((x for x in s.camera()['npcs']['agents'] if x['id'] == i), None)
    flat = lambda a, b: math.hypot(a[0] - b[0], a[2] - b[2])

    def user_paths():
        return [p for p in state()['editor']['paths'] if p['points']]

    def draw(square):
        """Tool 3 with the real keys and buttons: closed, four corners, then tool 1."""
        before = {p['id'] for p in user_paths()}
        keys('-k', '3'); time.sleep(.4)
        center()
        if not state()['editor']['submode'].startswith('closed'):
            pointer('button', 274, 1); time.sleep(.1); pointer('button', 274, 0); time.sleep(.3)
        cx = sum(q[0] for q in square) / len(square); cz = sum(q[2] for q in square) / len(square)
        for q in square:
            # From 3 m outside the corner, looking at it on the floor.
            d = math.hypot(q[0] - cx, q[2] - cz)
            eye = [q[0] + (q[0] - cx) / d * 3, q[1] + 1.7, q[2] + (q[2] - cz) / d * 3]
            look(eye, q); time.sleep(.25); click()
        keys('-k', '1'); time.sleep(.5)
        new = [p for p in user_paths() if p['id'] not in before]
        return new[-1] if new else None

    def carry(ident, target, eye):
        """Super+LMB on the character, aim at `target` from `eye`, read the drop, release."""
        u = agent(ident)['position']
        grab_eye = [u[0] + 2.2, 1.7, u[2] + .4]
        look(grab_eye, [u[0], u[1] + 1.1, u[2]]); center(); time.sleep(.5)
        seen = {}

        def gesture():
            pointer('button', 272, 1); time.sleep(.5)
            seen['carried'] = s.camera()['npcs'].get('carry')
            look(eye, target); time.sleep(.9)
            seen['drop'] = s.camera()['npcs'].get('carry')
            shot('route-carry-held.png')
            pointer('button', 272, 0)
        held([('logo', 'Super_L')], gesture)
        time.sleep(1)
        return seen

    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    a = s.STATE / ARCHIVE.name
    installed = client.call('libraries.install', {'archive': str(a), 'sha256': hashlib.sha256(a.read_bytes()).hexdigest()})
    library = f"{installed['id']}@{installed['version']}"
    ref = next(e['ref'] for e in client.call('objects.catalog', {})['entries'] if e.get('library') == library and 'character' in e['traits'])
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60); time.sleep(1)
    client.call('graphics.set', {'patch': {'offload': 'off', 'npc_motion': 'always'}})
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    center()

    # The user's own character (not the companion), standing west of the square.
    ident = None
    for spot in ([46.5, 0, -135], [47, 0, -131], [46.5, 0, -139]):
        try:
            ident = client.call('npcs.spawn', {'ref': ref, 'pose': {'position': spot, 'rotation': [0, 0, 0, 1]}})['id']; break
        except RuntimeError as e:
            print('spawn at', spot, e, flush=True)
    wait(lambda: agent(ident) is not None, timeout=20); time.sleep(1)
    check('the character is the user\'s own (npcs.spawn), not the companion',
          state()['companion']['state'] == 'none' and agent(ident)['user'], {'id': ident})

    # 1. Tool 3 draws the closed square.
    path = draw(SQUARE)
    check('tool 3 draws a closed four-point path', path is not None and path['closed'] and len(path['points']) == 4,
          path and {k: path[k] for k in ('id', 'use', 'closed')})
    ref1 = f"user/{path['id']}"
    listed = next(p for p in client.call('paths.list', {})['paths'] if p['ref'] == ref1)
    check('paths.list lists the drawn square (generic until a character is put on it)', listed['use'] == 'generic',
          {k: listed[k] for k in ('use', 'walkable', 'problems')})
    shot('route-carry-drawn.png')

    # 2. Carried and set down 0.5 m outside the square's west edge.
    edge = [(SQUARE[0][0] + SQUARE[1][0]) / 2, 0, (SQUARE[0][2] + SQUARE[1][2]) / 2]
    target = [edge[0] - .5, 0, edge[2]]
    seen = carry(ident, target, [target[0] - 2.5, 1.7, target[2] + .3])
    drop = seen.get('drop') or {}
    check('held 0.5 m from the drawn square: it is the snap target, highlighted',
          seen.get('carried') and drop.get('valid') and drop.get('route') == ref1 and drop.get('highlight', 0) > 0, seen)
    time.sleep(.5)
    p0 = agent(ident)['position']; time.sleep(3); g = agent(ident)
    check('released: bound to the square, which is now a route, and it walks it',
          g['path'] == ref1 and g['activity'] in ('walking', 'running') and flat(p0, g['position']) > 1 and
          edge_distance(g['position'], SQUARE) < .3 and
          next(p for p in state()['editor']['paths'] if p['id'] == path['id'])['use'] == 'route',
          {'agent': {k: g[k] for k in ('path', 'position', 'activity')}, 'from': p0})
    listed = next(p for p in client.call('paths.list', {})['paths'] if p['ref'] == ref1)
    check('paths.list: now a walkable route the character is bound to', listed['use'] == 'route' and listed['walkable'] is True and
          ident in listed['bound'], {k: listed[k] for k in ('use', 'walkable', 'problems', 'bound')})
    shot('route-carry-walking.png')

    # 3. Far from every route: it stands where it was set down.
    away = [edge[0] - 1.9, 0, edge[2] - 2]
    client.call('graphics.set', {'patch': {'npc_motion': 'still'}}); time.sleep(.5)  # a still target to pick up
    seen = carry(ident, away, [away[0] - 2.5, 1.7, away[2] + .3])
    client.call('graphics.set', {'patch': {'npc_motion': 'always'}})
    time.sleep(.5); g = agent(ident)
    check('held 1.6 m and more from any route: no snap, it stands there',
          not (seen.get('drop') or {}).get('route') and not g['path'] and flat(g['position'], away) < .5,
          {'drop': seen.get('drop'), 'agent': {k: g[k] for k in ('path', 'position', 'activity')}})

    # 4. Rebind route on a second square drawn with tool 3.
    second = draw(SECOND)
    ref2 = f"user/{second['id']}"
    client.call('npcs.bind', {'id': ident, 'path': ref2})
    time.sleep(1); p0 = agent(ident)['position']; time.sleep(3); g = agent(ident)
    check('Rebind route (npcs.bind) on a drawn square: bound, a route now, and it walks it',
          g['path'] == ref2 and flat(p0, g['position']) > 1 and edge_distance(g['position'], SECOND) < .3 and
          next(p for p in state()['editor']['paths'] if p['id'] == second['id'])['use'] == 'route',
          {'agent': {k: g[k] for k in ('path', 'position', 'activity')}})
    editor = json.loads((s.STATE / 'editor.json').read_text())
    record = next(p for p in editor['paths'] if p['id'] == second['id'])
    check('editor.json keeps the drawn paths as routes', record.get('use') == 'route', {k: record.get(k) for k in ('id', 'use', 'closed')})

    if not os.environ.get('HYPRUNE_ROUTE_CARRY_SHOTS'):
        (s.ROOT / 'docs/evidence/route-carry.json').write_text(json.dumps(
            {'nestedOnly': True, 'world': LUMEN, 'character': ref, 'checks': checks}, indent=1) + '\n')
    print(f'PASS route-carry: {len(checks)} checks', flush=True)
