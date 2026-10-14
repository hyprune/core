"""Workspace screens (IPC 0.27, object format 0.11): dev/nested/run.sh --screens.

Nested only. Lumen Reach and Switchyard (the worlds checkout's dist packages, or
HYPRUNE_LUMEN_PACKAGE / HYPRUNE_SWITCHYARD_PACKAGE) and the starter kit's four screen
objects copied into the user folder (HYPRUNE_SCREENS_DIR, default the worlds checkout's
objects/screens). Checks:
 1. Contract: 0.27 hello; catalog entries of the screens carry screen {aspect, size, mountType}.
 2. objects.place without a workspace binds the next free one (Lumen's homes are 21-24: 25);
    the home is in mounts (area, anchor, binding) and the instance's screen names it.
 3. A real client opened on that workspace stays tiled on it (not floating, not in the room)
    and is composed on the screen: the workspace frame sits at the screen's anchor.
 4. Aimed at, the screen is the workspace surface; F flies into the workspace and
    Super+Tab back out; workspace.activate travels to the screen's arrival spot.
 5. objects.transform moves the screen: the anchor and the composed frame follow.
 6. objects.configure: workspace 21 (an authored home) is refused (workspace.taken), 26 binds,
    null leaves the screen empty (no home), next-free binds again.
 7. Persistence: editor.json keeps the placement with its workspace; a world reload brings the
    screen back as the same home.
 8. Switchyard (no authored homes): a screen there becomes a home on 21 and composes its
    workspace, while room windows keep the world's room anchors.
Evidence: docs/evidence/screens.json and stills in the state dir.
"""
import json, math, os, pathlib, shutil, subprocess, time
import session as s
from m2 import wait, keys

LUMEN = 'com.hyprune.lumen-reach'
SWITCHYARD = 'com.hyprune.switchyard'
STAND = 'user/workspace-screen-stand-16x9'
WALL21 = 'user/workspace-screen-wall-21x9'
SCREENS = ['workspace-screen-wall-16x9', 'workspace-screen-wall-21x9', 'workspace-screen-stand-16x9', 'workspace-screen-stand-21x9']


def prepare(installed):
    for world, env in [(LUMEN, 'HYPRUNE_LUMEN_PACKAGE'), (SWITCHYARD, 'HYPRUNE_SWITCHYARD_PACKAGE')]:
        source = pathlib.Path(os.environ.get(env, str(s.ROOT.parent / 'worlds/dist' / world)))
        if not (source / 'world.json').is_file():
            raise SystemExit(f'--screens needs {world} at {source} ({env})')
        shutil.rmtree(installed / world, ignore_errors=True)
        shutil.copytree(source, installed / world)
    screens = pathlib.Path(os.environ.get('HYPRUNE_SCREENS_DIR', str(s.ROOT.parent / 'worlds/objects/screens')))
    if not all((screens / n / 'object.json').is_file() for n in SCREENS):
        raise SystemExit(f'--screens needs the starter screens at {screens} (HYPRUNE_SCREENS_DIR)')
    data = s.STATE / 'datahome'
    shutil.rmtree(data, ignore_errors=True)
    for n in SCREENS:
        shutil.copytree(screens / n, data / 'hyprune/objects' / n)
    os.environ['XDG_DATA_HOME'] = str(data)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    checks = []
    out = s.STATE

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        assert ok, (name, data)
        print('PASS screens:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    def shot(name):
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True)

    def monitor():
        return next(m for m in json.loads(s.ctl('monitors', '-j')) if m['name'] == 'HEADLESS-1')

    def workspace():
        return monitor()['activeWorkspace']['id']

    def windows():
        return json.loads(s.ctl('clients', '-j'))

    check('negotiated IPC 0.27', client.hello['protocol'] == '0.27', client.hello['protocol'])
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': LUMEN})
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    def instance(id_):
        return next((i for i in state()['objects']['instances'] if i['id'] == id_), None)

    def bindings():
        return {b['id']: b['areaId'] for b in state()['mounts']['bindings']}

    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active'])

    # 1. The catalog lists the screens with their anchor.
    def catalog():
        return {e['ref']: e for e in client.call('objects.catalog', {})['entries']}
    entries = wait(lambda: (lambda c: c if all(f'user/{n}' in c for n in SCREENS) else None)(catalog()), timeout=20)
    shown = {r: entries[r]['screen'] for r in entries if r.startswith('user/workspace-screen')}
    check('catalog: screens are placeable with aspect, size and mount type',
          all(entries[f'user/{n}']['placeable'] and entries[f'user/{n}']['state'] == 'ready' for n in SCREENS) and
          shown[STAND] == {'aspect': '16:9', 'size': [3.2, 1.8], 'mountType': 'kiosk-pillar'} and
          shown[WALL21] == {'aspect': '21:9', 'size': [4.2, 1.8], 'mountType': 'hanging-wall'}, shown)

    # 2. Place a stand screen in front of the player: it takes the next free workspace.
    homes = bindings()
    check('Lumen Reach homes are 21-24', sorted(homes) == [21, 22, 23, 24], homes)
    placed = None
    for attempt in range(12):
        try:
            placed = wait(lambda: client.call('objects.place', {'ref': STAND})['id'], timeout=10)
            break
        except (RuntimeError, AssertionError) as e:
            # No free spot ahead: turn and try again (the arrival court has screens all round).
            c = s.camera()['position']
            s.ctl('eval', f'hl.plugin.hyprune.camera({c[0]},{c[1]},{c[2]},{attempt * .7},0)')
            time.sleep(.3)
            last = str(e)
    check('objects.place put the stand screen in front of the player', placed is not None, placed or last)
    screen = wait(lambda: (lambda i: i if i and i['state'] == 'active' else None)(instance(placed)))
    area = f'screen-{placed}'
    check('the screen shows the next free workspace (25) as a home',
          screen['screen'] == {'workspace': 25, 'home': area, 'aspect': '16:9', 'size': [3.2, 1.8]} and bindings().get(25) == area,
          {'screen': screen['screen'], 'bindings': bindings()})
    mounts = state()['mounts']
    anchor = next((a for a in mounts['anchors'] if a['id'] == area), None)
    home = next((a for a in mounts['areas'] if a['id'] == area), None)
    check('mounts list the screen home (area, anchor, mount role)',
          anchor and home and anchor['mount'] == {'type': 'kiosk-pillar', 'role': 'mounted-workspace', 'interaction': 'focus-in'} and
          home['workspaceSlot'] == '25' and home['primaryAnchor'] == area and anchor['size'] == [3.2, 1.8],
          {'anchor': anchor, 'area': home})
    located = {l['id']: l for l in state()['workspaces']['locations']}
    check('the Director lists workspace 25 as a home', located.get(25, {}).get('label') == area, located.get(25))

    # 3. A real client on workspace 25: tiled there, composed on the screen.
    s.ctl('eval', 'hl.window_rule({name="screen-25",match={title="^Screen-25$"},workspace="25 silent"})')
    banner = 'printf "\\n  WORKSPACE 25\\n  on a placed screen\\n"; exec bash --noprofile --norc'
    children.append(s.envrun('kitty', '--title', 'Screen-25', '--override', 'font_size=40', '--override', 'cursor_blink_interval=0',
                             '--override', 'background=#123a52', 'bash', '--noprofile', '--norc', '-c', banner,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    w = wait(lambda: next((w for w in windows() if w['title'] == 'Screen-25'), None))
    wait(lambda: any(e['title'] == 'Screen-25' and e['workspaceId'] == 25 for e in s.camera()['captures']))
    time.sleep(1)
    w = next(w for w in windows() if w['title'] == 'Screen-25')
    check('the window stays tiled on workspace 25 (only the room floats)', w['workspace']['id'] == 25 and not w['floating'] and workspace() == 20,
          {'workspace': w['workspace']['id'], 'floating': w['floating'], 'active': workspace()})

    def frame(id_):
        return next((f for f in s.camera()['workspaceFrames'] if f['workspaceId'] == id_), None)

    def anchor_of(area_):
        return next(a for a in state()['mounts']['anchors'] if a['id'] == area_)

    f = wait(lambda: frame(25))
    check('workspace 25 is composed at the screen anchor', math.dist(f['position'], anchor['pose']['position']) < .05,
          {'frame': f, 'anchor': anchor['pose']})

    def face(area_, id_):
        a = anchor_of(area_)
        x, y, z, w_ = a['pose']['rotation']
        p = a['pose']['position']
        n = [2 * (x * z + w_ * y), 2 * (y * z - w_ * x), 1 - 2 * (x * x + y * y)]
        yaw = math.atan2(-n[0], n[2])
        for d in [3.2, 2.6, 4, 2]:
            s.ctl('eval', f'hl.plugin.hyprune.camera({p[0] + n[0] * d},{p[1]},{p[2] + n[2] * d},{yaw},0)')
            time.sleep(.4)
            if s.camera()['aim'] == f'workspace-{id_}':
                return d
        return None

    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    d = face(area, 25)
    check('the screen is aimable as workspace 25', d is not None, {'distance': d, 'aim': s.camera()['aim']})
    time.sleep(.8)
    shot('screen-25-stand.png')

    # 4. F flies into the workspace, Super+Tab back; workspace.activate travels to it.
    keys('-k', 'f')
    check('F on the screen focuses workspace 25', wait(lambda: state()['mounts']['desktopWorkspaceId'] == 25, timeout=8), state()['mounts']['desktopWorkspaceId'])
    keys('-P', 'Super_L', '-k', 'Tab', '-p', 'Super_L')
    check('Super+Tab returns to the world', wait(lambda: state()['mounts']['desktopWorkspaceId'] is None and s.camera()['handover'] == 0, timeout=8))
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    c = s.camera()['position']
    s.ctl('eval', f'hl.plugin.hyprune.camera({c[0] + 6},{c[1]},{c[2] + 6},0,0)')
    time.sleep(.3)

    def transition():
        return state()['location'].get('transition') or {}
    before = transition().get('id')
    client.call('workspace.activate', {'id': 25})
    wait(lambda: transition().get('id') != before and transition().get('phase') == 'arrived', timeout=10)
    time.sleep(.3)
    p = client.call('camera.get', {})['position']
    a = anchor_of(area)['pose']['position']
    check('workspace.activate 25 travels in front of the screen', workspace() == 25 and 2 < math.dist([p[0], a[1], p[2]], a) < 6,
          {'camera': p, 'anchor': a, 'workspace': workspace()})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="20"}))')
    wait(lambda: workspace() == 20)

    # 5. Move the screen: its home and wall follow.
    pose = screen['pose']
    moved = None
    for dx, dz in [(1.5, 0), (-1.5, 0), (0, 1.5), (0, -1.5), (1, 1), (-1, -1)]:
        target = {'position': [pose['position'][0] + dx, pose['position'][1], pose['position'][2] + dz], 'rotation': pose['rotation']}
        try:
            client.call('objects.transform', {'id': placed, 'pose': target})
            moved = target
            break
        except RuntimeError:
            continue
    check('objects.transform moved the screen', moved is not None, moved)
    wait(lambda: math.dist(anchor_of(area)['pose']['position'], anchor['pose']['position']) > .9)
    shift = [anchor_of(area)['pose']['position'][k] - anchor['pose']['position'][k] for k in range(3)]
    want = [moved['position'][k] - pose['position'][k] for k in range(3)]
    f2 = wait(lambda: (lambda f: f if f and math.dist(f['position'], anchor_of(area)['pose']['position']) < .05 else None)(frame(25)))
    check('the anchor and the composed wall follow the screen', math.dist(shift, want) < .02,
          {'shift': shift, 'moved': want, 'frame': f2['position']})

    # 6. Rebinding.
    try:
        client.call('objects.configure', {'id': placed, 'patch': {'workspace': 21}})
        refused = None
    except RuntimeError as e:
        refused = e.args[0]
    check('workspace 21 (an authored home) is refused', refused and refused['code'] == -32005 and refused['data']['rule'] == 'workspace.taken', refused)
    client.call('objects.configure', {'id': placed, 'patch': {'workspace': 26}})
    check('configure workspace 26 rebinds the screen', wait(lambda: bindings().get(26) == area and 25 not in bindings()) and
          instance(placed)['screen']['workspace'] == 26, bindings())
    client.call('objects.configure', {'id': placed, 'patch': {'workspace': None}})
    check('configure null leaves the screen empty (no home)', wait(lambda: area not in bindings().values()) and
          instance(placed)['screen'] == {'workspace': None, 'home': None, 'aspect': '16:9', 'size': [3.2, 1.8]} and
          not any(a['id'] == area for a in state()['mounts']['areas']), instance(placed)['screen'])
    client.call('objects.configure', {'id': placed, 'patch': {'workspace': 'next-free'}})
    check('configure next-free binds 25 again', wait(lambda: bindings().get(25) == area), bindings())

    # 7. Persistence: the overlay keeps it, a world reload brings the home back.
    editor = json.loads((s.STATE / 'editor.json').read_text())
    record = next(r for r in editor['placements'] if r['id'] == placed)
    check('editor.json keeps the screen and its workspace', record['workspace'] == 25 and record['ref'] == STAND, record)
    client.call('world.load', {'worldId': SWITCHYARD})
    check('in another world the screen is no home', wait(lambda: 25 not in bindings()), bindings())
    client.call('world.load', {'worldId': LUMEN})
    back = wait(lambda: (lambda i: i if i and i['state'] == 'active' and i['screen']['home'] else None)(instance(placed)), timeout=20)
    check('after a world reload the screen is the same home on 25',
          back['screen']['workspace'] == 25 and bindings().get(25) == area and math.dist(back['pose']['position'], moved['position']) < .01,
          {'screen': back['screen'], 'bindings': bindings()})
    f3 = wait(lambda: frame(25), timeout=10)
    check('its workspace is composed on it again', math.dist(f3['position'], anchor_of(area)['pose']['position']) < .05, f3)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    face(area, 25)
    time.sleep(.8)
    shot('screen-25-after-reload.png')
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})

    # 8. Switchyard: no authored homes, room anchors stay.
    client.call('world.load', {'worldId': SWITCHYARD})
    time.sleep(1)
    other = None
    for attempt in range(12):
        try:
            other = client.call('objects.place', {'ref': 'user/workspace-screen-stand-21x9'})['id']
            break
        except RuntimeError as e:
            c = s.camera()['position']
            s.ctl('eval', f'hl.plugin.hyprune.camera({c[0]},{c[1]},{c[2]},{attempt * .7},0)')
            time.sleep(.3)
            last = str(e)
    check('a 21:9 screen placed in Switchyard', other is not None, other or last)
    screen2 = wait(lambda: (lambda i: i if i and i['state'] == 'active' and i['screen']['home'] else None)(instance(other)))
    area2 = f'screen-{other}'
    check('it becomes a home on 21 (Switchyard has none of its own)', screen2['screen']['workspace'] == 21 and bindings().get(21) == area2,
          {'screen': screen2['screen'], 'bindings': bindings()})
    s.ctl('eval', 'hl.window_rule({name="screen-21",match={title="^Screen-21$"},workspace="21 silent"})')
    children.append(s.envrun('kitty', '--title', 'Screen-21', '--override', 'font_size=40', '--override', 'cursor_blink_interval=0',
                             '--override', 'background=#3a1252', 'bash', '--noprofile', '--norc', '-c',
                             'printf "\\n  WORKSPACE 21\\n  21:9 screen in Switchyard\\n"; exec bash --noprofile --norc',
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    wait(lambda: any(e['title'] == 'Screen-21' and e['workspaceId'] == 21 for e in s.camera()['captures']))
    f4 = wait(lambda: frame(21), timeout=10)
    check('workspace 21 is composed on the Switchyard screen', math.dist(f4['position'], anchor_of(area2)['pose']['position']) < .05, f4)
    anchors_room = [a for a in state()['mounts']['anchors'] if a['id'] != area2]
    check('Switchyard keeps no authored homes: only the screen is an area', [a['id'] for a in state()['mounts']['areas']] == [area2],
          state()['mounts']['areas'])
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    face(area2, 21)
    time.sleep(.8)
    shot('screen-21-switchyard.png')
    evidence = s.ROOT / 'docs/evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'screens.json').write_text(json.dumps({'checks': checks, 'roomAnchors': len(anchors_room)}, indent=1) + '\n')
    print(f'PASS screens: {len(checks)} checks', flush=True)
