"""World format 0.9 NPCs (RFC-0010, RFC-0011 P2; IPC 0.23): dev/nested/run.sh --npcs.

Nested only (isolated harness on headless hosts). A test world built from Switchyard
(HYPRUNE_SWITCHYARD_PACKAGE, else the worlds checkout's dist package) as world format 0.9:
two starter-kit residents referenced from the starter library (HYPRUNE_NPCS_LIBRARY, a
com.hyprune.starter .tar.zst, else the worlds checkout's tools/library-pack
output) on authored routes, a bench with a seat anchor at a stop.
Checks:
 1. A world referencing a library that is not installed fails with world.library.missing;
    libraries.install, then it loads: 0.23 hello, the npcs state key, both agents walk at
    their walk clip's speed, drawn.
 2. paths.list: the authored routes walkable; tool 3 refuses a route point off walkable floor.
 3. A user resident on a freshly drawn route: paths.draw (tool 3), points clicked on the
    floor, paths.finish spawns it; it walks the user route.
 4. Protection: carry (Super+LMB), tool 4 select, tool 5 pocket and objects.transform /
    pocket / place on characters are refused (object.protected).
 5. Glance: a player 2.5 m in front-left gets looked at (head yaw clamped, eased).
 6. Step aside: a player standing on the route ahead is passed, not walked through.
 7. Frames: animating in view (followed 5 m behind on its walk) renders frames; out of view
    (and paused, still) none.
 8. A seat: the authored resident sits on the bench at its stop (root lifted by the seat
    fit). Asked to guide while seated, it stands up and steps off its seat (core 0.5.0
    refused: "can't walk there"); the player walking off cancels it and it sits down again.
    Then it stands and walks on.
 9. Interactions: aimed resident shows the prompt (reticle interact), E opens the choice
    list; say lines advance with E (a 150-character line wraps); waypoint sets the waypoint.
    Occlusion: a resident behind opaque geometry is not drawn and requests no frames, from
    the tool 3 spot and from the yard's side behind the research dock wall; in the open, drawn.
10. Disable/remove: authored characters disable (and enable), never remove; user ones remove.
11. Persistence: a world reload keeps the user character on its route and a disabled one off.
Steering (9d): a second resident walks the loop the other way and they never overlap; a
    build monitor dropped on the north walk gets the resident a detour round it and back.
Motion policy (9c): with `auto` a guide is refused under still, reduced motion and off (and
    talk under off) with the reason; a player standing still 32 s doesn't pause residents; a
    guide with no movement input runs to its end and back onto its route.
12. Offload parity (HYPRUNE_NPCS_OFFLOAD != 0): characters posed still, stills with offload
    off and on compared per pixel; the worker draws them (a still with characters off differs).
13. Lumen Reach (HYPRUNE_LUMEN_PACKAGE) under auto: at each spawn pose, facing away from every
    resident, a still player costs 0 fps while they animate; facing one, frames come.
Evidence: docs/evidence/npcs.json; stills in the state dir (HYPRUNE_NPCS_SHOTS: elsewhere).
"""
import hashlib, json, math, os, pathlib, shutil, subprocess, sys, time
import session as s
from m2 import wait, keys, pose

WORLD = 'org.hyprune.npc-yard'
LUMEN = 'com.hyprune.lumen-reach'   # step 13 (HYPRUNE_LUMEN_PACKAGE, else the worlds checkout's dist)
LUMEN_POSES = {'arrival': [-8, 1.7, 12], 'array-hall': [8, 1.7, -34], 'rime-vault': [38, -4.3, -79], 'signal-rise': [54, 1.7, -125]}
ARCHIVE = s.starter_archive('HYPRUNE_NPCS_LIBRARY')
LIBRARY = s.library_of(ARCHIVE, 'com.hyprune.starter@0.1.0')  # the archive's own id@version
EMBER = f'library/{LIBRARY}/resident-ember'
LOOP = [[-12, 9.5], [2, 9.5], [2, 3], [-12, 3]]       # clear Switchyard floor (dev: the walkability grid)
NORTH = [[-12, -12.5], [12, -12.5]]
SOUTH = [[2, 3], [-10, 3]]                             # the loop's south edge: the guide's path, clear from the bench
USER = [[6, 0, 0], [11, 0, 0], [11, 0, 4]]
LONG_LINE = ('The loop takes about a minute. Mind the step by the signal box, and wait for me at the bench '
             'where the yard lamps stand; the north walk is near gates.')
# A spot where the yard's opaque geometry hides the north walk from x 2 to 10 (and shows it
# from about -6 to 0): the walking resident there is occluded on every ray.
HIDE_EYE = [-8, 1.7, 4]             # drawn with tool 3
BENCH = {'position': [3.6, 0, 3], 'rotation': [0, -.7071068, 0, .7071068]}
BENCH_HEIGHT = .45


def lifted(agent):
    """Seated with the root lifted by the seat fit: bench height minus the measured seat."""
    return abs(agent['seatOffset'] - (BENCH_HEIGHT - agent['seatFit']['seatHeight'])) < .02


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_SWITCHYARD_PACKAGE', str(s.ROOT.parent / 'worlds/dist/com.hyprune.switchyard')))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--npcs needs Switchyard at {source} (HYPRUNE_SWITCHYARD_PACKAGE)')
    if not ARCHIVE.is_file():
        raise SystemExit(f'--npcs needs the starter library archive at {ARCHIVE} (HYPRUNE_NPCS_LIBRARY)')
    target = installed / WORLD
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(source, target)
    # The bench: the fixture's generated 0.45 m seat asset. Both resident generations fit it: the
    # starter kit's own low sit (0.2.0 and earlier, a seat fit near -0.06 m) and the shared
    # starter-motion sit (0.3.0 on, about 0.36 m).
    subprocess.run([sys.executable, str(s.ROOT / 'tests/fixtures/npcs.py')], check=True, stdout=subprocess.DEVNULL)
    (target / 'objects').mkdir(exist_ok=True)
    shutil.copy2(s.ROOT / 'tests/fixtures/npcs/objects/bench.gltf', target / 'objects/bench.gltf')
    shutil.copy2(s.ROOT / 'tests/fixtures/npcs/objects/bench.bin', target / 'objects/bench.bin')
    lumen = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if (lumen / 'world.json').is_file():
        shutil.rmtree(installed / LUMEN, ignore_errors=True)
        shutil.copytree(lumen, installed / LUMEN)
    w = json.loads((target / 'world.json').read_text())
    pt = lambda p, t='line': {'position': [p[0], 0, p[1]], 'type': t}
    # The second line is 150 characters: a caption wraps (up to four lines), never cut.
    talk = ['Welcome to the yard.', LONG_LINE]
    w.update({
        'formatVersion': '0.9', 'id': WORLD, 'name': 'NPC yard',
        'objectAssets': [{'id': 'blue', 'library': LIBRARY, 'object': 'resident-blue'},
                         {'id': 'crimson', 'library': LIBRARY, 'object': 'resident-crimson'},
                         {'id': 'bench', 'scene': 'objects/bench.gltf', 'collision': {'node': 'proxy'},
                          'traits': {'seat': {'anchors': [{'id': 'left', 'position': [-.4, 0, .6], 'yaw': 0, 'height': BENCH_HEIGHT},
                                                          {'id': 'right', 'position': [.4, 0, .6], 'yaw': 0, 'height': BENCH_HEIGHT}]}}}],
        'paths': [{'id': 'yard-loop', 'name': 'Yard loop', 'use': 'route', 'closed': True, 'points': [pt(p) for p in LOOP],
                   'stops': [{'point': 2, 'seconds': 6, 'seat': 'bench-1/left'}, {'point': 0, 'seconds': 2, 'clip': 'wave'}]},
                  {'id': 'north-walk', 'use': 'route', 'points': [pt(p) for p in NORTH]},
                  {'id': 'south-walk', 'use': 'route', 'points': [pt(p) for p in SOUTH]}],
        'keepClear': [{'id': 'arrival', 'min': [-1, 0, 10.5], 'max': [1, 2.2, 11.2]}],
        'objects': [{'id': 'blue-walker', 'asset': 'blue', 'path': 'yard-loop', 'phase': .55, 'accent': [.2, .5, .95],
                     'interactions': [{'id': 'talk', 'label': 'Talk', 'action': {'type': 'say', 'lines': talk}},
                                      {'id': 'mark', 'label': 'Mark the north walk', 'action': {'type': 'waypoint', 'target': {'position': [0, 0, -12.5]}}},
                                      {'id': 'show', 'label': 'Show me the south walk', 'prompt': 'Follow me', 'action': {'type': 'guide', 'path': 'south-walk'}}]},
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
        # With nothing changing, render on demand draws no frame and grim's copy can wait for
        # good: after 10 s, a 1 px pointer nudge there and back draws one.
        for attempt in range(3):
            try:
                subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=10)
                return out / name
            except subprocess.TimeoutExpired:
                pointer(1, 0); pointer(-1, 0)
        raise AssertionError(f'no frame to capture for {name}')

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
    check('authored residents are named by their object.json name, not the asset id',
          all(i['name'].startswith('Resident') and i['name'] not in ('blue', 'crimson') for i in chars), [i['name'] for i in chars])
    a0 = agent('crimson-walker'); look([a0['position'][0], 3, a0['position'][2] + 6], a0['position'])
    a0 = agent('crimson-walker'); t0 = time.monotonic(); time.sleep(2)
    a1 = agent('crimson-walker'); t1 = time.monotonic()
    speed = math.dist(a0['position'], a1['position']) / (t1 - t0)
    check('walks its route at about the walk clip speed (0.87 m/s)', .6 < speed < 1.1 and a1['activity'] == 'walking', {'mPerS': speed, 'agent': a1})
    check('drawn in view', diag()['drawn'] >= 1 and diag()['animationVisible'], {k: diag()[k] for k in ('drawn', 'animationVisible')})
    shot('npcs-1-walking.png')

    # 2. Paths: authored routes walkable; tool 3 refuses a route point on a wall.
    paths = client.call('paths.list', {})['paths']
    check('paths.list: authored routes, walkable', {p['id']: p['walkable'] for p in paths} == {'yard-loop': True, 'north-walk': True, 'south-walk': True}, paths)

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

    # 7. Frames: in view vs out of view. The eye follows 5 m behind on the walk, in the open
    # wherever the walker is (from the yard's side the dock walls hide most of the walk).
    facing('crimson-walker', -5., height=2.2)
    look_fps = fps(); followed = agent('crimson-walker')
    camera([0, 1.7, -14.3], 0, 0)  # at the north wall, facing it: every character behind
    time.sleep(.5)  # the jump's own frames
    causes0 = s.camera()['frameCauses']; away = fps(); causes1 = s.camera()['frameCauses']
    causes = {k: v - causes0.get(k, 0) for k, v in causes1.items() if v != causes0.get(k, 0)}
    print('frame causes out of view:', causes, flush=True)
    # Residents walking behind walls off screen change nothing drawn: no 'npcs' key changes.
    check('animating characters in view render frames; out of view, none', look_fps > 20 and away < 2 and not causes.get('npcs'),
          {'inView': look_fps, 'occluded': followed['occluded'], 'outOfView': away, 'causes': causes,
           'forcesFrames': s.camera()['npcs']['forcesFrames']})
    motion('still'); facing('crimson-walker', -5., height=2.2); still_fps = fps()
    check('still characters cost no frames', still_fps < 2 and state()['npcs']['state'] == 'still', {'fps': still_fps})
    client.call('graphics.set', {'patch': {'npc_motion': 'always', 'reduced_motion': True}}); time.sleep(.3)
    reduced = fps()
    check('reduced motion poses them still', reduced < 2 and state()['npcs']['reason'] == 'reduced motion', state()['npcs'])
    client.call('graphics.set', {'patch': {'reduced_motion': False}})

    # 8. The authored resident sits on the bench at its stop.
    look([0, 6, 9], [3, 0, 3])
    sat = wait(lambda: (lambda b: b if b and b['activity'] == 'sitting' and b['seat'] else None)(agent('blue-walker')), timeout=120)
    # Align (0.25 s), then the sit clip plays through (2.3 s for the starter residents).
    seated = wait(lambda: (lambda b: b if b and b['clip'] == 'sit_idle' else None)(agent('blue-walker')), timeout=6)
    check('sits on the bench seat (root lifted by the seat fit)', seated['seat'] == 'bench-1/left' and seated['clip'] == 'sit_idle' and
          lifted(seated) and instance('blue-walker')['seat'] == 'bench-1/left', seated)
    look([1.2, 1.4, 1.2], [3.2, .6, 2.7]); shot('npcs-4-seated.png')
    # 8b. Guide from the seat: stand up (the stand clip), step off to the seat's approach point,
    # join the guide path. The player walking 30 m off cancels it: back to the seat, seated again.
    seat_at = seated['position']
    try:
        client.call('interaction.choose', {'objectId': 'blue-walker', 'interactionId': 'show'}); refusal = None
    except Exception as e:  # noqa: BLE001 - the refusal is the evidence
        refusal = str(e)
    check('asked to guide while seated, it accepts (no "can\'t walk there")', refusal is None, refusal)
    off = wait(lambda: (lambda b: b if b and b['activity'] == 'guiding' and abs(b['seatOffset']) < .01 and
                        math.dist(b['position'][::2], seat_at[::2]) > .3 else None)(agent('blue-walker')), timeout=10)
    check('it stands up and steps off its seat to guide; the seat stays reserved',
          off and 'bench-1/left' in diag()['seatsTaken'], {'agent': off, 'seatsTaken': diag()['seatsTaken']})
    shot('npcs-4b-guide-from-seat.png')
    camera([-40, 1.7, 40], 0)  # the player walks off: a guide ends 30 m away
    back = wait(lambda: (lambda b: b if b and b['activity'] == 'sitting' and b['clip'] == 'sit_idle' else None)(agent('blue-walker')), timeout=60)
    check('cancelled, it walks back and sits on its seat again',
          back['seat'] == 'bench-1/left' and lifted(back) and math.dist(back['position'][::2], seat_at[::2]) < .1, back)
    look([0, 6, 9], [3, 0, 3])
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
    time.sleep(.3)
    caption = next((l for l in s.camera()['overlays']['labels'] if l.get('lines')), None)
    check('a 150-character line wraps over the resident: every word drawn, no ellipsis',
          len(LONG_LINE) == 150 and caption and 2 <= len(caption['lines']) <= 4 and not caption['truncated'] and
          not any('...' in l or '\u2026' in l for l in caption['lines']) and ' '.join(caption['lines']) == LONG_LINE, caption)
    shot('npcs-6b-say-wrapped.png')
    keys('-k', 'e'); time.sleep(.3)
    check('and ends it after the last', state()['npcs']['interaction'] is None, state()['npcs'])
    client.call('interaction.choose', {'objectId': 'blue-walker', 'interactionId': 'mark'}); time.sleep(.4)
    wp = state()['location']['waypoint']
    check('waypoint sets the waypoint', wp is not None, wp)
    motion('always')

    # 9c. Motion policy: auto animates whenever the world is shown (no power, idle, typing or
    # free-cursor pause); only the user's settings stop characters, and they refuse a guide
    # (-32005, the reason in the message) instead of accepting it and never walking.
    client.call('interaction.choose', {'objectId': 'blue-walker', 'interactionId': None})  # the waypoint's
    for name, patch, words in (('still', {'npc_motion': 'still'}, 'motion still'),
                               ('reduced motion', {'npc_motion': 'auto', 'reduced_motion': True}, 'reduced motion'),
                               ('off', {'npc_motion': 'off', 'reduced_motion': False}, 'motion off')):
        client.call('graphics.set', {'patch': patch}); time.sleep(.4)
        ok, text = refused(None, -32005, 'interaction.choose', {'objectId': 'blue-walker', 'interactionId': 'show'})
        check(f'{name}: a guide is refused with the reason ({words})', ok and words in text and state()['npcs']['interaction'] is None, text)
    ok, text = refused(None, -32005, 'interaction.choose', {'objectId': 'blue-walker', 'interactionId': 'talk'})
    check('off: talk is refused too (characters are hidden)', ok and 'motion off' in text, text)
    client.call('graphics.set', {'patch': {'npc_motion': 'auto', 'reduced_motion': False}}); time.sleep(.4)
    facing('crimson-walker', -5., height=2.2)
    p0 = agent('crimson-walker')['position']; seen = set(); t0 = time.monotonic()
    while time.monotonic() - t0 < 32:  # no input at all: past the old 30 s idle pause
        n = state()['npcs']; seen.add((n['state'], n['reason'])); time.sleep(1)
    p1 = agent('crimson-walker')['position']
    check('auto: a player standing still for 32 s does not pause them', seen == {('animating', None)} and math.dist(p0, p1) > 5,
          {'states': sorted(map(str, seen)), 'walkedM': round(math.dist(p0, p1), 2)})
    # A guide under auto with no movement input (the eye is placed, not walked) runs to the
    # end of its path and back onto its route; the bench at the guide's start is its seat stop.
    look([0, 6, 9], [3, 0, 3])
    wait(lambda: (lambda b: b if b and b['activity'] == 'sitting' and b['clip'] == 'sit_idle' else None)(agent('blue-walker')), timeout=120)
    client.call('interaction.choose', {'objectId': 'blue-walker', 'interactionId': 'show'})
    t0 = time.monotonic(); end = SOUTH[-1]; nearest = 99.; guided = False; reasons = set()
    while time.monotonic() - t0 < 150:
        b = agent('blue-walker'); reasons.add(state()['npcs']['reason'])
        guided |= b['activity'] == 'guiding'
        nearest = min(nearest, math.dist(b['position'][::2], end))
        if guided and b['activity'] != 'guiding':
            break
        p = b['position']; look([p[0] + .5, 1.7, p[2] + 2.5], [p[0], 1, p[2]])  # the player keeps up
        time.sleep(.5)
    b = agent('blue-walker'); elapsed = time.monotonic() - t0
    check('auto: a guide runs to completion with no movement input (over 30 s), back on its route',
          guided and nearest < .5 and b['activity'] != 'guiding' and elapsed > 30 and reasons == {None} and
          state()['npcs']['interaction'] is None, {'seconds': round(elapsed, 1), 'nearestEnd': round(nearest, 2), 'agent': b,
                                                   'reasons': sorted(map(str, reasons))})
    motion('always')

    # 9d. Steering (RFC-0010 steering amendment). A second resident walks the yard loop the
    # other way: meeting head-on they pass, never closer than their radii allow. A machine
    # dropped on the north walk is walked round (a detour) and the route rejoined.
    # Spawned on the north edge (phase 0.25), away from the bench; counted once they are apart.
    ember = client.call('npcs.spawn', {'ref': EMBER, 'path': 'package/yard-loop', 'reverse': True, 'phase': .25})['id']
    wait(lambda: agent(ember) and agent(ember)['activity'] in ('walking', 'stopped'), timeout=10)
    look([-5, 14, 14], [-5, 0, 6])
    closest, both, worst, apart, passes = 1e9, 0., None, False, 0
    keys_ = ('activity', 'mode', 'position', 'lateral', 'speed', 'backingOff', 'detour', 'stop')
    t0 = time.monotonic(); near = False
    while time.monotonic() - t0 < 60:
        a, b = agent('blue-walker'), agent(ember)
        if a and b and a['activity'] != 'sitting' and b['activity'] != 'sitting':
            d = math.dist(a['position'][::2], b['position'][::2])
            apart |= d > 1.5
            if apart and d < closest:
                closest, both = d, a['radius'] + b['radius']
                worst = {'seconds': round(time.monotonic() - t0, 1), 'blue': {k: a[k] for k in keys_}, 'ember': {k: b[k] for k in keys_}}
            if d < 1.2 and not near:
                passes += 1
            near = d < 1.2
        time.sleep(.1)
    check('two residents walking a loop in opposite directions never overlap', apart and closest >= both - .05 and both > 0,
          {'closest': round(closest, 3), 'radii': both, 'meetings': passes, 'at': worst})
    client.call('npcs.remove', {'id': ember}); time.sleep(.5)
    MONITOR = f'library/{LIBRARY}/build-monitor'
    def place_monitor():  # the library asset loads on first use: retryable until it has
        try:
            return client.call('objects.place', {'ref': MONITOR, 'pose': {'position': [6, 0, -12.5], 'rotation': [0, .7071068, 0, .7071068]}})['id']
        except RuntimeError as e:
            if 'retry' not in str(e):
                raise
            return None
    placed = wait(place_monitor, timeout=30)
    time.sleep(1)
    c = agent('crimson-walker')
    check('a machine dropped on its route: the resident gets a detour, not a suspension',
          c['detours'] >= 1 and instance('crimson-walker')['state'] == 'active', {'agent': c})
    look([6, 9, -5], [6, 0, -12.5])
    inside, detoured, rejoined = False, False, False
    t0 = time.monotonic()
    while time.monotonic() - t0 < 70 and not rejoined:
        c = agent('crimson-walker'); p = c['position']
        # The monitor's footprint (turned: 0.81 x 1.1 m) against the body's box (half 0.85 x radius,
        # the walkability rule), 2 cm slack.
        inside |= abs(p[0] - 6) < .405 + .3 - .02 and abs(p[2] + 12.5) < .554 + .3 - .02
        if c['activity'] == 'walking' and c['detour'] >= 0:
            detoured = True
        if detoured and c['detour'] < 0 and abs(p[2] + 12.5) < .05 and abs(p[0] - 6) > 1.2:
            rejoined = True
        time.sleep(.1)
    shot('npcs-9-detour.png')
    check('it walks round the machine and rejoins its route', detoured and rejoined and not inside,
          {'detoured': detoured, 'rejoined': rejoined, 'inside': inside, 'agent': agent('crimson-walker')})
    client.call('objects.remove', {'id': placed}); time.sleep(.5)

    # 9b. Occlusion: alone on its walk (the others disabled), the resident behind the yard's
    # opaque geometry is not drawn and keeps no frames coming; in the open again it is both.
    for other in ('blue-walker', user_id):
        client.call('npcs.disable', {'id': other, 'disabled': True})
    time.sleep(.3)
    look(HIDE_EYE, [0, 1, -12.5])
    hid = wait(lambda: (lambda b: b if b and 3 <= b['position'][0] <= 9 and b['occluded'] else None)(agent('crimson-walker')), timeout=90)
    time.sleep(.3)
    hidden = diag()
    check('a resident behind opaque geometry is occluded: not drawn, no frames requested',
          hid and hidden['drawn'] == 0 and not hidden['animationVisible'], {'agent': hid, 'drawn': hidden['drawn'], 'animationVisible': hidden['animationVisible']})
    hidden_fps = fps(2)
    shot('npcs-6c-occluded.png')
    seen = wait(lambda: (lambda b: b if b and -5 <= b['position'][0] <= -1 and not b['occluded'] else None)(agent('crimson-walker')), timeout=90)
    time.sleep(.3)
    open_ = diag()
    check('in the open it is drawn and keeps frames coming again',
          seen and open_['drawn'] == 1 and open_['animationVisible'], {'agent': seen, 'drawn': open_['drawn'], 'animationVisible': open_['animationVisible'],
                                                                       'hiddenFps': hidden_fps})
    # From the yard's side (7 m south of the walk, 3 m up), the walker behind the research
    # dock wall is hidden by it (only the wall in view); past the wall's end it is drawn.
    def from_south(lo, hi, occluded, name):
        wait(lambda: (lambda b: b if b and lo <= b['position'][0] <= hi else None)(agent('crimson-walker')), timeout=90)
        motion('still'); b = agent('crimson-walker')
        look([b['position'][0], 3, b['position'][2] + 7], b['position'])
        b = wait(lambda: (lambda b: b if b['occluded'] == occluded else None)(agent('crimson-walker')), timeout=5)
        time.sleep(.3); d = diag(); shot(name); motion('always')
        return {'agent': b and {k: b[k] for k in ('position', 'occluded')}, 'drawn': d['drawn'], 'animationVisible': d['animationVisible']}
    behind = from_south(.5, 3.5, True, 'npcs-6e-dock-wall.png')
    check('from the yard, behind the dock wall: hidden by the wall', behind['agent'] and behind['drawn'] == 0, behind)
    past = from_south(6, 9, False, 'npcs-6f-past-dock-wall.png')
    check('the same view past the wall\'s end: drawn', past['agent'] and past['drawn'] == 1, past)
    for other in ('blue-walker', user_id):
        client.call('npcs.disable', {'id': other, 'disabled': False})
    time.sleep(.3)

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

    # 13. Lumen Reach under auto: at each spawn pose a still player with no resident in view
    # costs no frames while the residents keep animating (render on demand and occlusion, not
    # a pause); looking at one, frames come.
    if (s.STATE / 'worlds' / LUMEN / 'world.json').is_file():
        client.call('world.load', {'worldId': LUMEN}); wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=90)
        motion('auto'); wait(lambda: len(diag()['agents']) >= 4, timeout=30); time.sleep(2)
        def hidden_fps(seconds=3.):
            """(frames per second, frame causes) over a window in which no resident was drawn
            (None: one came into view)."""
            c0 = s.camera(); end = time.monotonic() + seconds
            while time.monotonic() < end:
                if diag()['drawn'] or diag()['animationVisible']:
                    return None
                time.sleep(.2)
            c1 = s.camera()
            causes = {k: v - c0['frameCauses'].get(k, 0) for k, v in c1['frameCauses'].items() if v != c0['frameCauses'].get(k, 0)}
            return (c1['frames'] - c0['frames']) / seconds, causes
        poses = {}
        for name, eye in LUMEN_POSES.items():
            best = f = None
            for k in list(range(8)) * 2:  # a heading clear of residents for 3 s; one at 0 fps first
                camera(eye, k * math.pi / 4); time.sleep(1.2)
                if diag()['drawn'] == 0 and not diag()['animationVisible']:
                    g = hidden_fps()
                    if g is not None and (best is None or g[0] < .5):
                        best, f = k * math.pi / 4, g
                    if g is not None and g[0] < .5:
                        break
            if best is None:
                poses[name] = {'noView': True}; continue
            poses[name] = {'yaw': round(best, 3), 'fps': f[0], 'causes': f[1], 'state': state()['npcs']['state'], 'drawn': diag()['drawn']}
        # 0 fps, or frames that come from something else in that view (never from the characters).
        check('Lumen poses: a still player with no resident in view stays at 0 fps (residents animating)',
              all(not v.get('noView') and (v['fps'] < .5 or not v['causes'].get('npcs')) and v['state'] == 'animating' for v in poses.values()),
              poses)
        # A stop facing a screen (anchor) stands in front of it on its normal, outside its 1.5 m
        # approach zone, and faces it (RFC-0010 steering amendment). Which of Lumen's four
        # screen stops get a front spot is evidence for the world's authoring.
        spots = {a['id']: [x for x in a['stopSpots'] if x['face']] for a in diag()['agents']}
        fronted = [(i, x) for i, xs in spots.items() for x in xs if x['spot']]
        at_front = None
        if fronted:
            rid, stop = fronted[0]
            look([stop['spot'][0] + 3, stop['spot'][1] + 2.5, stop['spot'][2] + 3], stop['spot'])
            at_front = wait(lambda: (lambda a: a if a['activity'] == 'stopped' and math.dist(a['position'][::2], stop['spot'][::2]) < .1 else None)(
                next(a for a in diag()['agents'] if a['id'] == rid)), timeout=150)
            time.sleep(1.5)
            at_front = next(a for a in diag()['agents'] if a['id'] == rid)
            to = [stop['face'][0] - at_front['position'][0], stop['face'][2] - at_front['position'][2]]
            facing_err = abs(math.remainder(at_front['heading'] - math.atan2(to[0], to[1]), math.tau))
            shot('npcs-10-screen-front.png')
        check('Lumen: a stop facing a screen stands in front of it and faces it',
              at_front is not None and facing_err < .15,
              {'spots': {i: [{'point': x['point'], 'spot': x['spot']} for x in xs] for i, xs in spots.items() if xs},
               'resident': at_front and at_front['id'], 'facingError': at_front and round(facing_err, 3)})
        resident = next(a for a in diag()['agents'] if a['id'] == 'sun-court-resident')
        # Walking again (after a stop it faces its screen: 4 m ahead of it is behind the screen);
        # followed from 4 m behind, as in the yard.
        wait(lambda: agent('sun-court-resident')['activity'] == 'walking', timeout=60)
        facing('sun-court-resident', -4., height=2.2)
        looked = fps()
        check('Lumen: a resident in view keeps frames coming', looked > 20 and diag()['drawn'] >= 1, {'fps': looked, 'agent': resident['id']})
        shot('npcs-8-lumen-resident.png')
        motion('always')
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
