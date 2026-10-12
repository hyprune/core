"""World format 0.9 NPCs (RFC-0010, RFC-0011 P2; IPC 0.23): dev/nested/run.sh --npcs.

Nested only (isolated harness on headless hosts). A test world built from Switchyard
(HYPRUNE_SWITCHYARD_PACKAGE, else the worlds checkout's dist package) as world format 0.9:
two starter-kit residents referenced from the starter library (HYPRUNE_NPCS_LIBRARY, a
com.hyprune.starter .tar.zst) on authored routes, a bench with a seat anchor at a stop.
Checks:
 1. A world referencing a library that is not installed fails with world.library.missing;
    libraries.install, then it loads: 0.23 hello, the npcs state key, both agents walk at
    their walk clip's speed, drawn.
 2. paths.list: both authored routes walkable; tool 3 refuses a route point off walkable floor.
 3. A user resident on a freshly drawn route: paths.draw (tool 3), points clicked on the
    floor, paths.finish spawns it; it walks the user route.
 4. Protection: carry (Super+LMB), tool 4 select, tool 5 pocket and objects.transform /
    pocket / place on characters are refused (object.protected).
 5. Glance: a player 2.5 m in front-left gets looked at (head yaw clamped, eased).
 6. Step aside: a player standing on the route ahead is passed, not walked through.
 7. Frames: animating in view renders frames; out of view (and paused, still) none.
 8. A seat: the authored resident sits on the bench at its stop (root lifted by the seat
    fit), then stands and walks on.
 9. Interactions: aimed resident shows the prompt (reticle interact), E opens the choice
    list; say lines advance with E; waypoint sets the waypoint.
10. Disable/remove: authored characters disable (and enable), never remove; user ones remove.
11. Persistence: a world reload keeps the user character on its route and a disabled one off.
12. Offload parity (HYPRUNE_NPCS_OFFLOAD != 0): characters posed still, stills with offload
    off and on compared per pixel; the worker draws them (a still with characters off differs).
Evidence: docs/evidence/npcs.json; stills in the state dir (HYPRUNE_NPCS_SHOTS: elsewhere).
"""
import hashlib, json, math, os, pathlib, shutil, subprocess, sys, time
import session as s
from m2 import wait, keys, pose

WORLD = 'org.hyprune.npc-yard'
LIBRARY = 'com.hyprune.starter@0.1.0'
ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_NPCS_LIBRARY', str(pathlib.Path.home() / 'npc-p2/dl/starter/com.hyprune.starter-0.1.0.tar.zst')))
EMBER = f'library/{LIBRARY}/resident-ember'
LOOP = [[-12, 9.5], [2, 9.5], [2, 3], [-12, 3]]       # clear Switchyard floor (dev: the walkability grid)
NORTH = [[-12, -12.5], [12, -12.5]]
USER = [[6, 0, 0], [11, 0, 0], [11, 0, 4]]             # drawn with tool 3
BENCH = {'position': [3.6, 0, 3], 'rotation': [0, -.7071068, 0, .7071068]}


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_SWITCHYARD_PACKAGE', str(s.ROOT.parent / 'worlds/dist/com.hyprune.switchyard')))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--npcs needs Switchyard at {source} (HYPRUNE_SWITCHYARD_PACKAGE)')
    if not ARCHIVE.is_file():
        raise SystemExit(f'--npcs needs the starter library archive at {ARCHIVE} (HYPRUNE_NPCS_LIBRARY)')
    target = installed / WORLD
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(source, target)
    # The bench: the fixture's generated seat asset (a 0.3 m seat for the residents' low sit).
    subprocess.run([sys.executable, str(s.ROOT / 'tests/fixtures/npcs.py')], check=True, stdout=subprocess.DEVNULL)
    (target / 'objects').mkdir(exist_ok=True)
    bench = json.loads((s.ROOT / 'tests/fixtures/npcs/objects/bench.gltf').read_text())
    accessors = bench['accessors']
    for a in accessors:  # scale the 0.45 m bench to 0.3 m
        if 'min' in a:
            a['min'][1] *= 2 / 3; a['max'][1] *= 2 / 3
    bench['nodes'] = [dict(n, scale=[1, 2 / 3, 1]) for n in bench['nodes']]
    (target / 'objects/bench.gltf').write_text(json.dumps(bench))
    shutil.copy2(s.ROOT / 'tests/fixtures/npcs/objects/bench.bin', target / 'objects/bench.bin')
    w = json.loads((target / 'world.json').read_text())
    pt = lambda p, t='line': {'position': [p[0], 0, p[1]], 'type': t}
    talk = ['Welcome to the yard.', 'The loop takes about a minute.']
    w.update({
        'formatVersion': '0.9', 'id': WORLD, 'name': 'NPC yard',
        'objectAssets': [{'id': 'blue', 'library': LIBRARY, 'object': 'resident-blue'},
                         {'id': 'crimson', 'library': LIBRARY, 'object': 'resident-crimson'},
                         {'id': 'bench', 'scene': 'objects/bench.gltf', 'collision': {'node': 'proxy'},
                          'traits': {'seat': {'anchors': [{'id': 'left', 'position': [-.4, 0, .6], 'yaw': 0, 'height': .3},
                                                          {'id': 'right', 'position': [.4, 0, .6], 'yaw': 0, 'height': .3}]}}}],
        'paths': [{'id': 'yard-loop', 'name': 'Yard loop', 'use': 'route', 'closed': True, 'points': [pt(p) for p in LOOP],
                   'stops': [{'point': 2, 'seconds': 6, 'seat': 'bench-1/left'}, {'point': 0, 'seconds': 2, 'clip': 'wave'}]},
                  {'id': 'north-walk', 'use': 'route', 'points': [pt(p) for p in NORTH]}],
        'keepClear': [{'id': 'arrival', 'min': [-1, 0, 10.5], 'max': [1, 2.2, 11.2]}],
        'objects': [{'id': 'blue-walker', 'asset': 'blue', 'path': 'yard-loop', 'phase': .55, 'accent': [.2, .5, .95],
                     'interactions': [{'id': 'talk', 'label': 'Talk', 'action': {'type': 'say', 'lines': talk}},
                                      {'id': 'mark', 'label': 'Mark the north walk', 'action': {'type': 'waypoint', 'target': {'position': [0, 0, -12.5]}}},
                                      {'id': 'show', 'label': 'Show me the north walk', 'prompt': 'Follow me', 'action': {'type': 'guide', 'path': 'north-walk'}}]},
                    {'id': 'crimson-walker', 'asset': 'crimson', 'path': 'north-walk', 'phase': .2},
                    {'id': 'bench-1', 'asset': 'bench', 'pose': BENCH}]})
    (target / 'world.json').write_text(json.dumps(w, indent=1))
    # Fresh data home: the library is not installed until the test installs it.
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'editor.json.v1', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(os.environ.get('HYPRUNE_NPCS_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    checks = []
    notices = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'npcs.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS npcs:', name, json.dumps(data)[:300] if data is not None else '', flush=True)
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

    def held(mods, fn, seconds=1.4):
        a, tail = [], []
        for name, key in mods:
            a += ['-M', name, '-P', key]
        for name, key in reversed(mods):
            tail += ['-p', key, '-m', name]
        hold = s.envrun('wtype', *a, '-s', str(int(seconds * 1000)), *tail); children.append(hold)
        time.sleep(.3); fn(); hold.wait(timeout=8); time.sleep(.2)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    diag = lambda: s.camera()['npcs']
    agent = lambda id_: next((a for a in diag()['agents'] if a['id'] == id_), None)
    instance = lambda id_: next((i for i in state()['objects']['instances'] if i['id'] == id_), None)
    perf = lambda: client.call('graphics.get', {})['performance']

    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})')
        time.sleep(.25)

    def look(eye, target):
        d = [target[i] - eye[i] for i in range(3)]
        camera(eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def facing(id_, distance, side=0., height=1.7):
        """The eye `distance` ahead of a character (side: metres to its left), looking at it."""
        a = agent(id_); p = a['position']; h = a['heading']
        f = [math.sin(h), math.cos(h)]; left = [f[1], -f[0]]
        eye = [p[0] + f[0] * distance + left[0] * side, height, p[2] + f[1] * distance + left[1] * side]
        look(eye, [p[0], p[1] + 1.1, p[2]])
        return eye

    def shot(name):
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True)
        return out / name

    def fps(seconds=2.5):
        f0 = s.camera()['frames']; time.sleep(seconds); return (s.camera()['frames'] - f0) / seconds

    def motion(value):
        client.call('graphics.set', {'patch': {'npc_motion': value}}); time.sleep(.3)

    def refused(call, code, *a):
        try:
            client.call(*a)
        except Exception as e:
            text = str(e)
            return str(code) in text, text
        return False, 'accepted'

    # 1. A missing library fails the load; installed, the world loads with its characters.
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    hello = client.hello
    check('IPC 0.23 hello advertises the NPC methods', hello['protocol'] == '0.23' and
          all(m in hello['methods'] for m in ['npcs.spawn', 'npcs.bind', 'npcs.disable', 'npcs.remove', 'paths.list', 'paths.set',
                                              'paths.remove', 'paths.draw', 'paths.finish', 'interaction.choose']), hello['methods'][-12:])
    ok, text = refused(None, -32602, 'world.load', {'worldId': WORLD})
    check('a world referencing an uninstalled library does not load', ok and 'world.library.missing' in text, text)
    archive = s.STATE / ARCHIVE.name
    client.call('libraries.install', {'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60); time.sleep(1)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    client.call('graphics.set', {'patch': {'offload': 'off', 'npc_motion': 'always'}})
    center()
    wait(lambda: len(diag()['agents']) == 2, timeout=20)
    st = state()
    chars = [i for i in st['objects']['instances'] if i['character']]
    check('two authored residents from the library, bound to routes', len(chars) == 2 and
          {i['path'] for i in chars} == {'package/yard-loop', 'package/north-walk'} and st['npcs']['usage']['authored'] == 2, chars)
    check('npcs state', st['npcs']['motion'] == 'always' and st['npcs']['state'] == 'animating' and st['npcs']['limits'] == {'authored': 16, 'user': 8},
          st['npcs'])
    a0 = agent('crimson-walker'); look([a0['position'][0], 3, a0['position'][2] + 6], a0['position'])
    a0 = agent('crimson-walker'); t0 = time.monotonic(); time.sleep(2)
    a1 = agent('crimson-walker'); t1 = time.monotonic()
    speed = math.dist(a0['position'], a1['position']) / (t1 - t0)
    check('walks its route at about the walk clip speed (0.87 m/s)', .6 < speed < 1.1 and a1['activity'] == 'walking', {'mPerS': speed, 'agent': a1})
    check('drawn in view', diag()['drawn'] >= 1 and diag()['animationVisible'], {k: diag()[k] for k in ('drawn', 'animationVisible')})
    shot('npcs-1-walking.png')

    # 2. Paths: authored routes walkable; tool 3 refuses a route point on a wall.
    paths = client.call('paths.list', {})['paths']
    check('paths.list: authored routes, walkable', {p['id']: p['walkable'] for p in paths} == {'yard-loop': True, 'north-walk': True}, paths)

    # 3. A user resident on a freshly drawn route.
    drawn = client.call('paths.draw', {'use': 'route', 'spawn': {'ref': EMBER, 'accent': [.95, .55, .15]}})
    check('paths.draw selects tool 3 with a new route', state()['editor']['tool'] == 3 and state()['npcs']['drawing']['pathId'] == drawn['id'],
          state()['npcs']['drawing'])
    look([8, 1.7, 6], [8, 1.2, 11.6]); time.sleep(.3)
    before = len(next(p for p in state()['editor']['paths'] if p['id'] == drawn['id'])['points'])
    click(); time.sleep(.3)
    after = len(next(p for p in state()['editor']['paths'] if p['id'] == drawn['id'])['points'])
    check('tool 3 refuses a route point off walkable floor (a wall)', after == before, {'points': after})
    for p in USER:
        look([p[0] - 1.5, 1.7, p[2] + 2.5], p); time.sleep(.2); click()
    route = next(p for p in state()['editor']['paths'] if p['id'] == drawn['id'])
    check('three floor points drawn', len(route['points']) == 3 and route['use'] == 'route', route)
    shot('npcs-2-drawn-route.png')
    finished = client.call('paths.finish', {})
    user_id = finished['bound']
    check('paths.finish spawns the resident on the drawn route', user_id and instance(user_id) and instance(user_id)['path'] == f"user/{drawn['id']}",
          {'finish': finished, 'instance': instance(user_id) if user_id else None})
    keys('-k', '1'); time.sleep(.3)
    wait(lambda: agent(user_id) and agent(user_id)['activity'] == 'walking', timeout=15)
    u0 = agent(user_id); time.sleep(2); u1 = agent(user_id)
    check('the user resident walks its route', math.dist(u0['position'], u1['position']) > .8, {'from': u0['position'], 'to': u1['position']})
    second = client.call('npcs.spawn', {'ref': EMBER, 'path': 'package/north-walk', 'phase': .5})['id']; time.sleep(.3)
    check('npcs.spawn binds to an authored route too', instance(second)['path'] == 'package/north-walk', instance(second))
    client.call('npcs.remove', {'id': second})
    ok, text = refused(None, -32007, 'npcs.bind', {'id': user_id, 'path': 'package/missing'})
    check('npcs.bind refuses an unknown route', not ok and '-32004' in text, text)

    # 4. Protection.
    facing('crimson-walker', 2.5); motion('still'); time.sleep(.3)
    p0 = agent('crimson-walker')['position']
    protected = diag()['protectedRefusals']
    held([('logo', 'Super_L')], lambda: (pointer('button', 272, 1), time.sleep(.4), look([p0[0], 1.7, p0[2] + 2], [p0[0] + 1, 1, p0[2]]),
                                         time.sleep(.3), pointer('button', 272, 0)))
    check('carry (Super+LMB) on a character is refused', diag()['protectedRefusals'] > protected and
          math.dist(agent('crimson-walker')['position'], p0) < .01, {'refusals': diag()['protectedRefusals']})
    facing('crimson-walker', 2.5)
    keys('-k', '4'); time.sleep(.3); protected = diag()['protectedRefusals']; click()
    check('tool 4 does not select a character', state()['editor']['selectedId'] is None and diag()['protectedRefusals'] > protected,
          state()['editor']['selectedId'])
    keys('-k', '5'); time.sleep(.3); protected = diag()['protectedRefusals']; click(274)
    check('tool 5 does not pocket a character', not state()['objects']['pocket'] and diag()['protectedRefusals'] > protected,
          state()['objects']['pocket'])
    keys('-k', '1'); time.sleep(.3)
    for method, params in [('objects.transform', {'id': 'crimson-walker', 'pose': {'position': [0, 0, 0], 'rotation': [0, 0, 0, 1]}}),
                           ('objects.pocket', {'ref': EMBER}), ('objects.place', {'ref': EMBER}),
                           ('objects.configure', {'id': 'crimson-walker', 'patch': {'locked': True}})]:
        ok, text = refused(None, -32005, method, params)
        check(f'{method} refuses characters (-32005)', ok, text)
    motion('always')

    # 5. Glance: a player in front-left within 4 m.
    facing('crimson-walker', 2.2, side=1.2); time.sleep(1.2)
    a = agent('crimson-walker')
    check('glances at a player within 4 m (head turned, clamped)', .15 < abs(a['look'][0]) <= .79, a['look'])
    shot('npcs-3-glance.png')

    # 6. Step aside: stand on the route 2.2 m ahead of the walker, away from the route's ends.
    # North walk runs along x at z = -12.5: wait until it walks straight in the middle.
    a = wait(lambda: (lambda x: x if abs(x['position'][0]) < 5 and abs(math.cos(x['heading'])) < .1 and x['activity'] == 'walking'
                      else None)(agent('crimson-walker')), timeout=60)
    h = a['heading']; p = a['position']
    stand = [p[0] + math.copysign(2.2, math.sin(h)), 1.7, -12.5]
    camera(stand, h + math.pi); closest = 99; widest = 0; samples = []
    for _ in range(50):
        d = diag(); q = next(x for x in d['agents'] if x['id'] == 'crimson-walker')
        closest = min(closest, math.hypot(q['position'][0] - stand[0], q['position'][2] - stand[2]))
        widest = max(widest, abs(q['lateral'])) if not q['yielding'] else max(widest, 1)
        if len(samples) < 6:
            samples.append({'agent': q['position'], 'lateral': q['lateral'], 'player': d['player'], 'present': d['present']})
        time.sleep(.1)
    check('steps aside instead of walking into the player', closest > .55 and widest > .3,
          {'closest': closest, 'widest': widest, 'stand': stand, 'samples': samples})

    # 7. Frames: in view vs out of view.
    a = agent('crimson-walker'); look([a['position'][0], 3, a['position'][2] + 7], a['position'])
    look_fps = fps()
    camera([0, 1.7, -14.3], 0, 0)  # at the north wall, facing it: every character behind
    causes0 = s.camera()['frameCauses']; away = fps(); causes1 = s.camera()['frameCauses']
    print('frame causes out of view:', {k: v - causes0.get(k, 0) for k, v in causes1.items() if v != causes0.get(k, 0)}, flush=True)
    check('animating characters in view render frames; out of view, none', look_fps > 20 and away < 2,
          {'inView': look_fps, 'outOfView': away, 'forcesFrames': s.camera()['npcs']['forcesFrames']})
    motion('still'); look([a['position'][0], 3, a['position'][2] + 7], a['position']); still_fps = fps()
    check('still characters cost no frames', still_fps < 2 and state()['npcs']['state'] == 'still', {'fps': still_fps})
    client.call('graphics.set', {'patch': {'npc_motion': 'always', 'reduced_motion': True}}); time.sleep(.3)
    reduced = fps()
    check('reduced motion poses them still', reduced < 2 and state()['npcs']['reason'] == 'reduced motion', state()['npcs'])
    client.call('graphics.set', {'patch': {'reduced_motion': False}})

    # 8. The authored resident sits on the bench at its stop.
    look([0, 6, 9], [3, 0, 3])
    sat = wait(lambda: (lambda b: b if b and b['activity'] == 'sitting' and b['seat'] else None)(agent('blue-walker')), timeout=120)
    time.sleep(2.5)
    seated = agent('blue-walker')
    check('sits on the bench seat (root lifted by the seat fit)', seated['seat'] == 'bench-1/left' and seated['clip'] == 'sit_idle' and
          seated['seatOffset'] > .2 and instance('blue-walker')['seat'] == 'bench-1/left', seated)
    look([1.2, 1.4, 1.2], [3.2, .6, 2.7]); shot('npcs-4-seated.png')
    up = wait(lambda: (lambda b: b if b and b['activity'] == 'walking' and 'bench-1/left' not in diag()['seatsTaken'] else None)(agent('blue-walker')),
              timeout=40)
    check('stands up and walks on', abs(up['seatOffset']) < .01 and 'bench-1/left' not in diag()['seatsTaken'], up)

    # 9. Interactions.
    motion('still')
    facing('blue-walker', 1.8); center(); time.sleep(.6)
    st = state()
    check('the aimed resident offers its interactions (reticle interact)', st['npcs']['aimed'] and st['npcs']['aimed']['objectId'] == 'blue-walker'
          and st['interaction']['reticle']['action'] == 'interact', {'aimed': st['npcs']['aimed'], 'reticle': st['interaction']['reticle']})
    shot('npcs-5-prompt.png')
    keys('-k', 'e'); time.sleep(.4)
    it = state()['npcs']['interaction']
    check('E opens the choice list', it and it['type'] == 'choose' and [c['id'] for c in it['choices']] == ['talk', 'mark', 'show'], it)
    client.call('interaction.choose', {'objectId': 'blue-walker', 'interactionId': 'talk'}); time.sleep(.3)
    it = state()['npcs']['interaction']
    check('say shows its first line', it['type'] == 'say' and it['line'] == 'Welcome to the yard.' and it['lineCount'] == 2, it)
    shot('npcs-6-say.png')
    keys('-k', 'e'); time.sleep(.3)
    check('E advances the line', state()['npcs']['interaction']['lineIndex'] == 1, state()['npcs']['interaction'])
    keys('-k', 'e'); time.sleep(.3)
    check('and ends it after the last', state()['npcs']['interaction'] is None, state()['npcs'])
    client.call('interaction.choose', {'objectId': 'blue-walker', 'interactionId': 'mark'}); time.sleep(.4)
    wp = state()['location']['waypoint']
    check('waypoint sets the waypoint', wp is not None, wp)
    motion('always')

    # 10. Disable and remove.
    client.call('npcs.disable', {'id': 'crimson-walker', 'disabled': True}); time.sleep(.3)
    check('an authored character disables (hidden, no agent)', instance('crimson-walker')['state'] == 'hidden' and agent('crimson-walker') is None,
          instance('crimson-walker'))
    ok, text = refused(None, -32005, 'npcs.remove', {'id': 'crimson-walker'})
    check('authored characters are never removed', ok, text)
    client.call('npcs.disable', {'id': user_id, 'disabled': True}); time.sleep(.3)
    check('a user character disables too', instance(user_id)['state'] == 'hidden', instance(user_id))
    client.call('npcs.disable', {'id': user_id, 'disabled': False}); time.sleep(.3)
    spare = client.call('npcs.spawn', {'ref': EMBER})['id']; time.sleep(.3)
    check('a standing user character', agent(spare) and agent(spare)['path'] == '' and instance(spare)['path'] is None, agent(spare))
    client.call('npcs.remove', {'id': spare}); time.sleep(.3)
    check('a user character removes', instance(spare) is None, None)

    # 11. Persistence across a world reload.
    editor = json.loads((s.STATE / 'editor.json').read_text())
    record = next(p for p in editor['placements'] if p['id'] == user_id)
    check('editor.json keeps the user character with its route', record['path'] == f"user/{drawn['id']}" and record['ref'] == EMBER, record)
    client.call('world.load', {'worldId': WORLD}); wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60); time.sleep(1.5)
    wait(lambda: agent(user_id) is not None, timeout=20)
    check('after a reload: the user character walks its route, the disabled one stays off',
          instance(user_id)['path'] == f"user/{drawn['id']}" and instance('crimson-walker')['state'] == 'hidden', [instance(user_id), instance('crimson-walker')])
    client.call('npcs.disable', {'id': 'crimson-walker', 'disabled': False}); time.sleep(.5)

    # 12. Offload parity for skinned characters.
    if os.environ.get('HYPRUNE_NPCS_OFFLOAD', '1') != '0':
        offload_parity(client, check, perf, look, agent, motion, out)
    (s.ROOT / 'docs/evidence/npcs.json').write_text(json.dumps({'nestedOnly': True, 'world': 'Switchyard as world format 0.9',
                                                                'library': LIBRARY, 'checks': checks}, indent=1) + '\n')
    print('PASS npcs:', len(checks), 'checks', flush=True)


def offload_parity(client, check, perf, look, agent, motion, out):
    import numpy as np
    from PIL import Image

    def offload(mode):
        client.call('graphics.set', {'patch': {'offload': mode, 'render_scale': 1, 'dynamic_resolution': False}})
        want = 'offload' if mode == 'on' else 'in-process'
        return wait(lambda: perf()['activeRenderer'] == want, 40)

    def region(path, box):
        img = np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)
        x0, y0, x1, y1 = box
        return img[y0:y1, x0:x1]
    motion('still'); time.sleep(.5)
    a = agent('crimson-walker')
    look([a['position'][0], 1.6, a['position'][2] + 3.2], [a['position'][0], .9, a['position'][2]]); time.sleep(.8)
    shots = {}
    for name, mode, npc in (('in-process', 'off', 'still'), ('offload', 'on', 'still'), ('offload-hidden', 'on', 'off')):
        offload(mode); motion(npc); time.sleep(1.2)
        path = out / f'npcs-7-{name}.png'
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        shots[name] = path
    offload('off'); motion('always')
    img = Image.open(shots['in-process']); w, h = img.size
    box = (w // 2 - w // 8, h // 4, w // 2 + w // 8, h - h // 10)  # the character, centred
    same = np.abs(region(shots['in-process'], box) - region(shots['offload'], box)).max(axis=2)
    gone = np.abs(region(shots['offload'], box) - region(shots['offload-hidden'], box)).max(axis=2)
    stats = {'parityMeanLevels': round(float(same.mean()), 3), 'parityDiffering': round(float((same > 6).mean()), 5),
             'hiddenDiffering': round(float((gone > 6).mean()), 5)}
    check('offload draws skinned characters like the in-process renderer', stats['parityDiffering'] < .01 and stats['hiddenDiffering'] > .01, stats)


if __name__ == '__main__':
    pass
