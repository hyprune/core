"""The companion (RFC-0010 companions amendment, IPC 0.25): dev/nested/run.sh --companion.

Nested only (isolated harness on headless hosts). Worlds: Switchyard and Lumen Reach from the
worlds checkout's dist packages (HYPRUNE_SWITCHYARD_PACKAGE, HYPRUNE_LUMEN_PACKAGE). The
companion is a character library archive (HYPRUNE_COMPANION_LIBRARY, a .tar.zst with one
companion-capable character; required). Checks, in Switchyard:
 1. IPC 0.25 hello and the companion state key; the library installs and its character is
    companion-capable in the catalog; companion.set spawns it out of view.
 2. Following a walked route (autodrive, walk mode): behind and beside, never within 1 m, no
    teleport while it can keep up, never popping in view; it ran to catch up.
 3. Frames: walking in view keeps frames coming; at rest beside the still player, none.
 4. Stay and Follow (E menu choices): stays put while the player walks off; comes back.
    Motion `auto` (the default, used throughout): after 32 s standing still it follows the next
    walk; `npc_motion` off refuses Stay (-32005, the reason in the message).
 5. Talk: the aimed companion offers Talk and Stay; Talk shows a say line, the next one each time.
 6. Tool 3: paths.draw {bind: companion}, points on the floor, paths.finish binds it; it walks
    the route; Follow returns it to the player.
 7. Carry and set down: Super+LMB picks it up; held within 0.75 m of the route the route is the
    snap target; released, it walks that route from there. Released on a wall: back where it was.
    Released on free floor away from routes: it stays there. A user character carried near the
    route snaps on and walks it.
 8. Persistence: companion.json keeps the choice, editor.json the per-world binding; a world
    reload restores it.
 9. Offload parity (HYPRUNE_COMPANION_OFFLOAD != 0): posed still, stills with offload off and on
    compared per pixel; the worker draws it.
10. A 0.24 peer sees no companion key and no companion methods.
Then Lumen Reach:
11. A world switch: it comes along (out of view, near the player); the Switchyard route binding
    stays in Switchyard.
12. A route walk through Sun Court into Array Hall; up the hall ramp (stairs) to the balcony.
13. A warp to another home (workspace.activate): it teleports in nearby, out of view.
14. Focus: standing between the player and a window, focusing the window moves it out of view.
15. Back in Switchyard: on its route again.
Evidence: docs/evidence/companion.json; stills in the state dir (HYPRUNE_COMPANION_SHOTS: elsewhere).
"""
import hashlib, json, math, os, pathlib, secrets, shutil, subprocess, sys, time
import session as s
from m2 import wait, keys, pose

YARD = 'com.hyprune.switchyard'
LUMEN = 'com.hyprune.lumen-reach'
ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_COMPANION_LIBRARY', ''))
USER = [[6, 0, 0], [11, 0, 0], [11, 0, 4]]                       # drawn with tool 3 (clear Switchyard floor)
LOOP = [[-10, 9.5], [0, 9.5], [0, 3], [-10, 3], [-10, 9.5]]       # walked by the player
LUMEN_WALK = ['arrival', 'court-junction', 'court-wall', 'hall-entry', 'hall-arrival']
LUMEN_STAIRS = ['hall-arrival', 'hall-ramp-base', 'hall-balcony']
OLD_PEER = secrets.token_hex(32)                                  # the 0.24 peer of step 10


def grants():
    return [{'token': OLD_PEER, 'client': 'com.example.older-peer', 'capabilities': ['state.read']}]


def prepare(installed):
    if not ARCHIVE.is_file():
        raise SystemExit('--companion needs a companion library archive (HYPRUNE_COMPANION_LIBRARY)')
    for env, world in (('HYPRUNE_SWITCHYARD_PACKAGE', YARD), ('HYPRUNE_LUMEN_PACKAGE', LUMEN)):
        source = pathlib.Path(os.environ.get(env, str(s.ROOT.parent / 'worlds/dist' / world)))
        if not (source / 'world.json').is_file():
            raise SystemExit(f'--companion needs {world} at {source} ({env})')
        shutil.rmtree(installed / world, ignore_errors=True)
        shutil.copytree(source, installed / world)
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'companion.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(os.environ.get('HYPRUNE_COMPANION_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'companion-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS companion:', name, json.dumps(data)[:300] if data is not None else '', flush=True)
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

    def held(mods, fn, seconds=2.5):
        a, tail = [], []
        for name, key in mods:
            a += ['-M', name, '-P', key]
        for name, key in reversed(mods):
            tail += ['-p', key, '-m', name]
        hold = s.envrun('wtype', *a, '-s', str(int(seconds * 1000)), *tail); children.append(hold)
        time.sleep(.3); fn(); hold.wait(timeout=12); time.sleep(.2)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    comp = lambda: s.camera()['companion']
    pos = lambda: comp()['position']

    def camera(p, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({p[0]},{p[1]},{p[2]},{yaw},{pitch})'); time.sleep(.25)

    def look(eye, target):
        d = [target[i] - eye[i] for i in range(3)]
        camera(eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def feet():
        p = client.call('camera.get', {})['position']; return [p[0], p[1] - 1.7, p[2]]

    def flat(a, b):
        return math.hypot(a[0] - b[0], a[2] - b[2])

    def shot(name):
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True)
        return out / name

    def fps(seconds=2.5):
        f0 = s.camera()['frames']; time.sleep(seconds); return (s.camera()['frames'] - f0) / seconds

    def refused(code, *a):
        try:
            client.call(*a)
        except Exception as e:  # noqa: BLE001 - the refusal is the evidence
            return str(code) in str(e), str(e)
        return False, 'accepted'

    def drive(points, speed, timeout=90, sample=None):
        client.call('route.set', {'points': [{'position': [p[0], p[1] + 1.7, p[2]]} for p in points]})
        client.call('autodrive.start', {'speed': speed})
        until = time.monotonic() + timeout; started = time.monotonic()
        while time.monotonic() < until and (time.monotonic() - started < 2 or state()['location']['autodrive']):
            if sample:
                sample()
            time.sleep(.25)

    def walk_check(label, points, speed, settle=6):
        """Follows a walked route: distances, teleports, pops, running."""
        c0 = comp(); samples = []
        def sample():
            c = comp(); f = feet()
            if c['position'] and c['state']['state'] == 'active':
                samples.append({'d': round(flat(c['position'], f), 2), 'activity': c['state']['activity'], 'inView': c['inView'],
                                'y': round(c['position'][1] - f[1], 2)})
        drive(points, speed, sample=sample)
        time.sleep(settle)
        c1 = comp(); f = feet()
        result = {'end': round(flat(c1['position'], f), 2), 'min': min(x['d'] for x in samples), 'max': max(x['d'] for x in samples),
                  'teleports': c1['teleports'] - c0['teleports'], 'pops': c1['pops'] - c0['pops'],
                  'ran': any(x['activity'] == 'running' for x in samples), 'samples': len(samples), 'endY': round(c1['position'][1] - f[1], 2)}
        return result

    # 1. Hello, install, capability, set.
    old = s.Client(str(socketpath), OLD_PEER, '0.24', 'com.example.older-peer')  # launch tokens expire in 30 s
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    hello = client.hello
    check('IPC 0.25 hello advertises the companion methods', hello['protocol'] == '0.25' and
          all(m in hello['methods'] for m in ['companion.set', 'companion.clear', 'companion.mode']), hello['methods'][-6:])
    check('the companion state key: none chosen', state()['companion']['state'] == 'none' and state()['companion']['ref'] is None,
          state()['companion'])
    archive = s.STATE / ARCHIVE.name
    installed = client.call('libraries.install', {'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
    library = f"{installed['id']}@{installed['version']}"
    entries = [e for e in client.call('objects.catalog', {})['entries'] if e.get('library') == library]
    character = next(e for e in entries if 'character' in e['traits'])
    check('the installed character is companion-capable', character['companion'] and character['companion']['capable'],
          {'ref': character['ref'], 'companion': character['companion'], 'triangles': character['triangles'], 'gpuBytes': character['gpuBytes']})
    REF = character['ref']
    ok, text = refused(-32004, 'companion.set', {'ref': 'library/com.example.none@1.0.0/nobody'})
    check('companion.set refuses an unknown ref', ok, text)
    client.call('world.load', {'worldId': YARD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60); time.sleep(1)
    client.call('graphics.set', {'patch': {'offload': 'off', 'npc_motion': 'auto'}})  # the default: no power or idle pause
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    center()
    camera([-10, 1.7, 9.5], math.pi / 2)  # looking east along the north edge of the loop
    client.call('companion.set', {'ref': REF})
    wait(lambda: comp()['state']['state'] == 'active' and comp()['position'] is not None, timeout=30); time.sleep(1.5)
    c = comp()
    check('companion.set: it joins out of view, near the player', c['state']['mode'] == 'follow' and c['teleports'] >= 1 and
          1.4 < flat(c['position'], feet()) < 6.5 and not c['inView'] and c['pops'] == 0, c)
    check('companion.json keeps the choice', json.loads((s.STATE / 'companion.json').read_text()) == {'ref': REF}, None)

    if os.environ.get('HYPRUNE_COMPANION_STAGE') == 'demo':  # the recording, not the checks
        client.call('world.load', {'worldId': LUMEN}); wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60)
        client.call('movement.set', {'modeId': 'com.hyprune.walk'}); time.sleep(2)
        from companion_demo import run
        run(client, children, out, comp, look)
        return
    full = os.environ.get('HYPRUNE_COMPANION_ONLY') != 'lumen'  # lumen: steps 1 and 11-14 (debugging)
    if full:
        # 2. Following a walked route.
        r = walk_check('loop', [[p[0], 0, p[1]] for p in LOOP], 1.3)
        check('follows a walked route: behind and beside, never within 1 m, no teleport or pop', r['teleports'] == 0 and r['pops'] == 0 and
              r['min'] >= .95 and 1.4 <= r['end'] <= 3.0 and r['max'] < 8, r)
        r = walk_check('sprint', [[0, 0, 9.5], [0, 0, 3]], 3.5, settle=8)
        check('catches up by running', r['ran'] and r['teleports'] == 0 and r['end'] <= 3.0, r)
        f = feet(); look([f[0] + .1, 1.7, f[2] + .1], pos()); shot('companion-1-follow.png')

        # 3. Frames.
        camera([0, 1.7, 9], math.pi)  # face south: the companion settles behind
        wait(lambda: comp()['brain'] == 'idle' and not comp()['moving'], timeout=20); time.sleep(1)
        c = comp(); f = feet()
        look([f[0], 1.7, f[2]], [c['position'][0], 1, c['position'][2]])  # turn to it without stepping
        wait(lambda: comp()['brain'] == 'idle' and not comp()['moving'], timeout=20); time.sleep(2)
        causes0 = s.camera()['frameCauses']; rest = fps(); causes1 = s.camera()['frameCauses']
        causes = {k: v - causes0.get(k, 0) for k, v in causes1.items() if v != causes0.get(k, 0)}
        c = comp()
        check('at rest beside a still player, in view: no frames', rest < 2 and not c['moving'] and c['inView'] and c['state']['activity'] == 'idle',
              {'fps': rest, 'causes': causes, 'companion': c})
        camera([-8, 1.7, 9.5], math.pi / 2); time.sleep(.3)
        c = comp(); look([-8, 1.7, 9.5], [c['position'][0], 1, c['position'][2]])
        moving = []
        client.call('route.set', {'points': [{'position': [4, 1.7, 9.5]}]}); client.call('autodrive.start', {'speed': 1.2})
        f0 = s.camera()['frames']; t0 = time.monotonic()
        for _ in range(12):
            moving.append(comp()['moving']); time.sleep(.25)
        walking_fps = (s.camera()['frames'] - f0) / (time.monotonic() - t0)
        client.call('autodrive.stop', {}) if 'autodrive.stop' in hello['methods'] else None
        check('walking, it keeps frames coming', any(moving) and walking_fps > 15, {'fps': walking_fps})

        # 4. Stay and Follow.
        time.sleep(3); camera([0, 1.7, 9], math.pi); time.sleep(3)
        client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'stay'}); time.sleep(.5)
        at = pos(); check('Stay: mode stay', comp()['state']['mode'] == 'stay', comp()['state'])
        drive([[0, 0, 3], [-8, 0, 3]], 1.3); time.sleep(2)
        check('it stays while the player walks off', flat(pos(), at) < .1, {'at': at, 'now': pos()})
        client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'follow'})
        wait(lambda: flat(pos(), feet()) < 3 and comp()['state']['activity'] == 'idle', timeout=30)
        check('Follow: it comes back', comp()['state']['mode'] == 'follow', comp()['state'])

        # 4b. Motion policy: `auto` never pauses it while the world is shown (a player standing
        # still past the old 30 s idle pause, any power supply); `off` refuses its requests.
        time.sleep(32)
        r = walk_check('after-idle', [[-8, 0, 9.5], [-2, 0, 9.5]], 1.3)  # north, away from where it stands
        check('auto: after 32 s standing still it follows the next walk', state()['npcs']['state'] == 'animating' and
              r['teleports'] == 0 and r['pops'] == 0 and r['min'] >= .95 and r['end'] <= 3.0, {**r, 'npcs': state()['npcs']['state']})
        client.call('graphics.set', {'patch': {'npc_motion': 'off'}}); time.sleep(.4)
        ok, text = refused(-32005, 'interaction.choose', {'objectId': 'companion', 'interactionId': 'stay'})
        check('motion off: Stay is refused with the reason', ok and 'motion off' in text and comp()['state']['mode'] == 'follow', text)
        client.call('graphics.set', {'patch': {'npc_motion': 'auto'}}); time.sleep(.4)

        # 5. Talk.
        c = comp(); p = c['position']; f = feet()
        look([f[0], 1.7, f[2]], [p[0], 1.1, p[2]]); center(); time.sleep(1)  # turn to it on the spot (a 3 m jump is a warp)
        aimed = state()['npcs']['aimed']
        check('aimed: Talk and Stay', aimed and aimed['objectId'] == 'companion' and [i['id'] for i in aimed['interactions']] == ['talk', 'stay'] and
              aimed['interactions'][1]['type'] == 'companion', aimed)
        shot('companion-2-prompt.png')
        client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'talk'}); time.sleep(.6)
        it = state()['npcs']['interaction']; first = it['line']
        check('Talk shows a say line; it talks', it['type'] == 'say' and first and comp()['state']['activity'] == 'talking', it)
        shot('companion-3-talk.png')
        keys('-k', 'e'); time.sleep(.4)
        client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'talk'}); time.sleep(.4)
        second = state()['npcs']['interaction']['line']; keys('-k', 'e'); time.sleep(.4)
        check('the next Talk shows the next line', second and second != first, {'first': first, 'second': second})

        # 6. Tool 3: draw a route and put the companion on it.
        drawn = client.call('paths.draw', {'use': 'route', 'bind': 'companion'})
        check('paths.draw {bind: companion}', state()['npcs']['drawing']['bind'] == 'companion', state()['npcs']['drawing'])
        for q in USER:
            look([q[0] - 1.5, 1.7, q[2] + 2.5], q); time.sleep(.2); click()
        finished = client.call('paths.finish', {})
        route = f"user/{drawn['id']}"
        time.sleep(.5)
        check('paths.finish binds the companion to the drawn route', finished['bound'] == 'companion' and comp()['state']['mode'] == 'route' and
              comp()['state']['path'] == route, {'finish': finished, 'state': comp()['state']})
        keys('-k', '1'); time.sleep(.3)
        camera([8, 1.7, 7], math.pi); time.sleep(.5)
        wait(lambda: comp()['state']['activity'] == 'walking', timeout=30)
        p0 = pos(); time.sleep(3); p1 = pos()
        on_route = min(math.hypot(p1[0] - x, p1[2] - z) for x, z in [(6 + t * .25, 0) for t in range(21)] + [(11, t * .25) for t in range(17)])
        check('it walks the route', flat(p0, p1) > 1 and on_route < .3, {'from': p0, 'to': p1, 'offRoute': on_route})
        shot('companion-4-route.png')
        listed = next(x for x in client.call('paths.list', {})['paths'] if x['ref'] == route)
        check('paths.list names it as bound', 'companion' in listed['bound'], listed['bound'])
        client.call('companion.mode', {'mode': 'follow'})
        wait(lambda: comp()['state']['mode'] == 'follow' and flat(pos(), feet()) < 3.2, timeout=40)
        check('Follow unbinds it and it comes back', comp()['state']['path'] is None, comp()['state'])

        # 7. Carry and set down.
        def carry(target, aim_from=None):
            c = comp()['position']
            eye = [c[0] + 2.2, 1.7, c[2] + .2]
            look(eye, [c[0], 1.1, c[2]]); center(); time.sleep(.5)
            seen = {}
            def gesture():
                pointer('button', 272, 1); time.sleep(.5)
                look(aim_from or eye, target); time.sleep(.8)
                seen.update(comp().get('carry') or {}); seen['activity'] = comp()['state']['activity']
                pointer('button', 272, 0)
            held([('logo', 'Super_L')], gesture)
            time.sleep(.6)
            return seen
        camera([8, 1.7, 3], math.pi / 2); time.sleep(3)
        snapped = carry([10.55, 0, 2], aim_from=[8, 1.7, 2])
        check('carried near the route: the route is the snap target', snapped.get('valid') and snapped.get('route') == route and
              snapped.get('activity') == 'carried', snapped)
        time.sleep(.5)
        check('released, it walks that route from there', comp()['state']['mode'] == 'route' and comp()['state']['path'] == route and
              flat(pos(), [11, 0, 2]) < 1.5, {'state': comp()['state'], 'position': pos()})
        shot('companion-5-dropped-on-route.png')
        client.call('companion.mode', {'mode': 'follow'})
        wait(lambda: comp()['brain'] == 'idle' and not comp()['moving'], timeout=30)
        client.call('companion.mode', {'mode': 'stay'}); time.sleep(.5)  # a still target to pick up
        before = pos(); mode = comp()['state']['mode']
        wall = carry([0, 2.5, -15], aim_from=[0, 1.7, -10])
        check('released on a wall: back where it was', not wall.get('valid') and flat(pos(), before) < .2 and comp()['state']['mode'] == mode,
              {'carry': wall, 'before': before, 'after': pos()})
        camera([-4, 1.7, 6], 0); time.sleep(3)
        spot = carry([-4, 0, 3.5], aim_from=[-4, 1.7, 6])
        check('released on free floor away from routes: it stays there', spot.get('valid') and not spot.get('route') and
              comp()['state']['mode'] == 'stay' and flat(pos(), [-4, 0, 3.5]) < .4, {'carry': spot, 'state': comp()['state']})
        # A user character: carried near the drawn path, it snaps on and walks it (and stays on it).
        agent = lambda i: next((x for x in s.camera()['npcs']['agents'] if x['id'] == i), None)
        user = None
        for spot in ([-7, 0, 3.5], [-2, 0, 3.5], [-7, 0, 8], [-2, 0, 8]):  # the first free standing spot
            try:
                user = client.call('npcs.spawn', {'ref': REF, 'pose': {'position': spot, 'rotation': [0, 0, 0, 1]}})['id']; break
            except RuntimeError as e:
                print('spawn at', spot, e, flush=True)
        wait(lambda: agent(user) is not None, timeout=20); time.sleep(1)
        u = agent(user)['position']; eye = [u[0] + 2.2, 1.7, u[2] + .2]
        look(eye, [u[0], 1.1, u[2]]); center(); time.sleep(.5)
        seen = {}
        def gesture():
            pointer('button', 272, 1); time.sleep(.5)
            seen['reticle'] = s.camera().get('reticle')
            look([8, 1.7, 1], [10.55, 0, 1]); time.sleep(.8)
            seen['position'] = agent(user)['position']
            pointer('button', 272, 0)
        held([('logo', 'Super_L')], gesture)
        time.sleep(1.5)
        a = agent(user)
        on = min(math.hypot(a['position'][0] - x, a['position'][2] - z) for x, z in [(6 + t * .25, 0) for t in range(21)] + [(11, t * .25) for t in range(17)])
        check('a user character carried near the path snaps on and walks it', a['path'] == route and on < .3 and
              flat(seen['position'], [11, 0, 1]) < .3, {'held': seen, 'agent': {k: a[k] for k in ('path', 'position', 'activity')}})
        client.call('npcs.remove', {'id': user}); time.sleep(.5)
        client.call('companion.mode', {'mode': 'follow'}); time.sleep(2)

        # 8. Persistence (and the binding we leave in Switchyard for step 15).
        client.call('npcs.bind', {'id': 'companion', 'path': route, 'phase': .5}); time.sleep(.5)
        editor = json.loads((s.STATE / 'editor.json').read_text())
        record = next((r for r in editor.get('companions', []) if r['worldId'] == YARD), None)
        check('editor.json keeps the per-world binding', record and record['path'] == route, record)
        client.call('world.load', {'worldId': YARD}); wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60); time.sleep(2)
        check('after a reload it is on its route again', comp()['state']['mode'] == 'route' and comp()['state']['path'] == route, comp()['state'])

        # 9. Offload parity.
        if os.environ.get('HYPRUNE_COMPANION_OFFLOAD', '1') != '0':
            offload_parity(client, check, comp, look, out)

        # 10. An older peer.
        osub = old.call('state.subscribe', {'topics': ['state']})['subscriptionId']
        ostate = old.call('state.snapshot', {'subscriptionId': osub})['snapshot']['state']
        check('a 0.24 peer sees no companion', 'companion' not in ostate and not any(m.startswith('companion.') for m in old.hello['methods']), None)
        old.close()

    # 11. A world switch: it comes along; the route binding stays in Switchyard.
    pops = comp()['pops']
    client.call('world.load', {'worldId': LUMEN}); wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60)
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    manifest = json.loads((s.STATE / 'worlds' / LUMEN / 'world.json').read_text())
    nodes = {n['id']: n['position'] for n in manifest['extensions']['com.hyprune.lumen-reach.routes']['nodes']}
    a, b = nodes[LUMEN_WALK[0]], nodes[LUMEN_WALK[1]]
    pose(client, [a[0], a[1] + 1.7, a[2]], math.atan2(b[0] - a[0], -(b[2] - a[2])), 0)
    wait(lambda: comp()['state']['state'] == 'active' and comp()['position'] is not None and comp()['brain'] == 'idle', timeout=30); time.sleep(1)
    c = comp()
    check('a world switch: it comes along, out of view, following', c['state']['mode'] == 'follow' and flat(c['position'], feet()) < 6.5 and
          not c['inView'] and c['pops'] == pops, c)

    # 12. Lumen Reach: a route walk and the hall ramp.
    r = walk_check('lumen', [nodes[n] for n in LUMEN_WALK[1:]], 1.3, settle=8)
    check('follows through Sun Court into Array Hall', r['pops'] == 0 and r['min'] >= .95 and r['end'] <= 3.2, r)
    f = feet(); look([f[0], f[1] + 1.7, f[2]], pos()); shot('companion-6-lumen.png')
    r = walk_check('ramp', [nodes[n] for n in LUMEN_STAIRS[1:]], 1.2, settle=8)
    check('follows up the hall ramp to the balcony', r['pops'] == 0 and abs(r['endY']) < .6 and r['end'] <= 3.5, r)

    # 13. A warp to another home.
    t0 = comp()['teleports']
    client.call('workspace.activate', {'id': 21}); time.sleep(4)
    c = comp()
    check('a warp: it teleports in nearby, out of view', c['teleports'] > t0 and flat(c['position'], feet()) < 6.5 and not c['inView'] and c['pops'] == pops, c)

    if full:
        # 15. Back in Switchyard: on its route again.
        client.call('world.load', {'worldId': YARD}); wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60); time.sleep(2)
        check('back in Switchyard: on its route again', comp()['state']['mode'] == 'route' and comp()['state']['path'] == route, comp()['state'])

    # 14. Focus (Switchyard): standing between the player and a window at eye height, the
    # window going fullscreen moves the companion out of the view.
    if not full:
        client.call('world.load', {'worldId': YARD}); wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60); time.sleep(2)
    client.call('companion.mode', {'mode': 'follow'}); time.sleep(1)
    title = 'Companion focus'
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="20"}))'); time.sleep(1)  # the world's room workspace: captured
    children.append(s.envrun('kitty', '--title', title, '--override', 'initial_window_width=1200', '--override', 'initial_window_height=675',
                             'bash', '--noprofile', '--norc', stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    captured = lambda: next((c for c in s.camera()['captures'] if c['title'] == title), None)
    wait(lambda: captured() or state()['editor']['unplaced']['count'] >= 1, timeout=30)
    if not (captured() or {}).get('visible'):
        dock = [1.3, 2.25, -6.79]  # research-dock: placed on its wall from 4 m (tool 5, unplaced window)
        look([dock[0], 1.7, dock[2] + 4], dock); center(); time.sleep(.5)
        click(); time.sleep(1)
    print('focus window:', json.dumps({k: (captured() or {}).get(k) for k in ('visible', 'position', 'normal')}),
          state()['editor']['unplaced'], flush=True)
    capture = wait(lambda: next((c for c in s.camera()['captures'] if c['title'] == title and c.get('position') and c.get('visible')), None), timeout=30)
    time.sleep(1.5)
    capture = next(c for c in s.camera()['captures'] if c['title'] == title)
    centre, normal = capture['position'], capture['normal']
    eye = [centre[0] + normal[0] * 4.2, 1.7, centre[2] + normal[2] * 4.2]
    look(eye, centre); time.sleep(1)
    spot = [centre[0] + normal[0] * 2.2 + .3, 0, centre[2] + normal[2] * 2.2]
    client.call('npcs.bind', {'id': 'companion', 'pose': {'position': spot, 'rotation': [0, 0, 0, 1]}}); time.sleep(1)
    look(eye, centre); time.sleep(.8)
    between = comp(); pops = between['pops']
    shot('companion-7-before-focus.png')
    address = next(w['address'] for w in json.loads(s.ctl('clients', '-j')) if w['title'] == title)
    fullscreen = lambda action: s.ctl('eval', f'hl.dispatch(hl.dsp.window.fullscreen({{window="address:{address}",action="{action}"}}))')
    fullscreen('set'); wait(lambda: s.camera()['fullscreen'] == 2, timeout=20); time.sleep(1)
    c = comp()
    check('a window going fullscreen moves it out of the view', between['inView'] and flat(c['position'], between['position']) > .5 and
          not c['inView'] and c['pops'] == pops, {'window': centre, 'before': between['position'], 'after': c['position'],
                                                   'inViewBefore': between['inView'], 'inViewAfter': c['inView'], 'focusMoves': c['focusMoves'], 'focusMisses': c['focusMisses'], 'step': c['step'], 'mode': c['state']['mode']})
    shot('companion-7-focus.png')
    fullscreen('unset'); wait(lambda: s.camera()['fullscreen'] == 0, timeout=20); time.sleep(1)
    client.call('companion.mode', {'mode': 'follow'})
    wait(lambda: comp()['brain'] == 'idle' and flat(pos(), feet()) < 3.2, timeout=30)
    check('after focus it follows again', comp()['state']['mode'] == 'follow' and comp()['state']['state'] == 'active', comp()['state'])
    if full:
        (s.ROOT / 'docs/evidence/companion.json').write_text(json.dumps({'nestedOnly': True, 'worlds': [YARD, LUMEN], 'library': library,
                                                                         'checks': checks}, indent=1) + '\n')
    print('PASS companion:', len(checks), 'checks', flush=True)


def offload_parity(client, check, comp, look, out):
    import numpy as np
    from PIL import Image
    perf = lambda: client.call('graphics.get', {})['performance']

    def offload(mode):
        client.call('graphics.set', {'patch': {'offload': mode, 'render_scale': 1, 'dynamic_resolution': False}})
        want = 'offload' if mode == 'on' else 'in-process'
        return wait(lambda: perf()['activeRenderer'] == want, 40)

    def motion(value):
        client.call('graphics.set', {'patch': {'npc_motion': value}}); time.sleep(.3)
    motion('still'); time.sleep(.5)
    p = comp()['position']
    look([p[0], 1.6, p[2] + 3.2], [p[0], .9, p[2]]); time.sleep(.8)
    shots = {}
    for name, mode, npc in (('in-process', 'off', 'still'), ('offload', 'on', 'still'), ('offload-hidden', 'on', 'off')):
        offload(mode); motion(npc); time.sleep(1.2)
        path = out / f'companion-8-{name}.png'
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        shots[name] = path
    offload('off'); motion('auto')
    img = Image.open(shots['in-process']); w, h = img.size
    box = (w // 2 - w // 8, h // 4, w // 2 + w // 8, h - h // 10)
    region = lambda path: np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)[box[1]:box[3], box[0]:box[2]]
    same = np.abs(region(shots['in-process']) - region(shots['offload'])).max(axis=2)
    gone = np.abs(region(shots['offload']) - region(shots['offload-hidden'])).max(axis=2)
    stats = {'parityMeanLevels': round(float(same.mean()), 3), 'parityDiffering': round(float((same > 6).mean()), 5),
             'hiddenDiffering': round(float((gone > 6).mean()), 5)}
    check('offload draws the companion like the in-process renderer', stats['parityDiffering'] < .01 and stats['hiddenDiffering'] > .01, stats)
