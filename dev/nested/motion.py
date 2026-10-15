"""The shared clip library (RFC-0010 amendment, IPC 0.28 draft): dev/nested/run.sh --motion.

Nested only (isolated harness on headless hosts). A test world built from Switchyard
(HYPRUNE_SWITCHYARD_PACKAGE, else the worlds checkout's dist package) with one route. The
libraries (HYPRUNE_MOTION_LIBRARIES, colon-separated .tar.zst archives; required): the starter
kit with its starter-motion pack, com.hyprune.companions with companion-moves, and a library
whose companion has no styled walk of its own (Drunk Ash). Checks:
 1. IPC 0.28 hello; the libraries install; before any spawn the catalog's motion lists what each
    character gains from the packs: the Muse a walk style only the Desert rig has (stroll), her
    dances; Ash the Muse's confident walk.
 2. A Muse user character spawned on the route with style {walk: stroll} walks it playing
    walk_stroll (the Desert-only clip, retargeted); her instance carries the style.
 3. Ash as the companion with companion.mode {style: {walk: confident}} follows a walk playing
    walk_confident; the style is kept across Stay and Follow.
 4. The Muse as the companion: aimed, she offers Dance with her dances as options;
    interaction.choose {option: shimmy} dances dance_shimmy (activity dancing, dance shimmy);
    Dance without an option dances one of them; walking off ends it.
 5. A 0.27 peer sees no motion, style or dance fields and a dancing companion as idle.
Evidence: docs/evidence/motion.json; stills in the state dir (HYPRUNE_MOTION_SHOTS: elsewhere).
"""
import hashlib, json, math, os, pathlib, secrets, shutil, subprocess, sys, time
import session as s
from m2 import wait

WORLD = 'org.hyprune.motion-yard'
LOOP = [[-12, 9.5], [2, 9.5], [2, 3], [-12, 3]]       # clear Switchyard floor (as --npcs)
ARCHIVES = [pathlib.Path(p) for p in os.environ.get('HYPRUNE_MOTION_LIBRARIES', '').split(':') if p]
OLD_PEER = secrets.token_hex(32)                      # the 0.27 peer of step 5


def grants():
    return [{'token': OLD_PEER, 'client': 'com.example.peer-027', 'capabilities': ['state.read', 'objects.control']}]


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_SWITCHYARD_PACKAGE', str(s.ROOT.parent / 'worlds/dist/com.hyprune.switchyard')))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--motion needs Switchyard at {source} (HYPRUNE_SWITCHYARD_PACKAGE)')
    if len(ARCHIVES) < 3 or not all(a.is_file() for a in ARCHIVES):
        raise SystemExit('--motion needs the starter, companions and Ash library archives (HYPRUNE_MOTION_LIBRARIES)')
    target = installed / WORLD
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(source, target)
    w = json.loads((target / 'world.json').read_text())
    w.update({'formatVersion': '0.9', 'id': WORLD, 'name': 'Motion yard',
              'paths': [{'id': 'yard-loop', 'name': 'Yard loop', 'use': 'route', 'closed': True,
                         'points': [{'position': [p[0], 0, p[1]], 'type': 'line'} for p in LOOP]}]})
    (target / 'world.json').write_text(json.dumps(w, indent=1))
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'companion.json']:
        (s.STATE / name).unlink(missing_ok=True)
    for a in ARCHIVES:
        shutil.copy2(a, s.STATE / a.name)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(os.environ.get('HYPRUNE_MOTION_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'motion-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS motion:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    agent = lambda id_: next((a for a in s.camera()['npcs']['agents'] if a['id'] == id_), None)
    comp = lambda: s.camera()['companion']

    def camera(p, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({p[0]},{p[1]},{p[2]},{yaw},{pitch})'); time.sleep(.25)

    def look(eye, target):
        d = [target[i] - eye[i] for i in range(3)]
        camera(eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def feet():
        p = client.call('camera.get', {})['position']; return [p[0], p[1] - 1.7, p[2]]

    def shot(name):
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=20)

    def drive(points, speed, timeout=60, sample=None):
        client.call('route.set', {'points': [{'position': [p[0], 1.7, p[1]]} for p in points]})
        client.call('autodrive.start', {'speed': speed})
        until = time.monotonic() + timeout; started = time.monotonic()
        while time.monotonic() < until and (time.monotonic() - started < 2 or state()['location']['autodrive']):
            if sample:
                sample()
            time.sleep(.25)

    # 1. Hello, libraries, catalog motion before any spawn.
    old = s.Client(str(socketpath), OLD_PEER, '0.27', 'com.example.peer-027')  # launch tokens expire in 30 s
    check('IPC 0.28 hello', client.hello['protocol'] == '0.28', client.hello['protocol'])
    libraries = []
    for a in ARCHIVES:
        archive = s.STATE / a.name
        r = client.call('libraries.install', {'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
        libraries.append(f"{r['id']}@{r['version']}")
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60); time.sleep(1)
    client.call('graphics.set', {'patch': {'offload': 'off', 'npc_motion': 'auto'}})
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    entries = client.call('objects.catalog', {})['entries']
    ref = lambda obj: next(e['ref'] for e in entries if e['ref'].endswith('/' + obj) and e.get('library'))
    MUSE, ASH = ref('neon-cyber-muse'), ref('drunk-ash')
    motion = {e['ref'].split('/')[-1]: e['motion'] for e in entries if e.get('motion')}
    muse, ash = motion['neon-cyber-muse'], motion['drunk-ash']
    check('catalog motion before a spawn: the Muse gains the Desert-only stroll and her dances; Ash the confident walk',
          'stroll' in muse['styles']['walk'] and 'shimmy' in muse['dances'] and {'starter-motion', 'companion-moves'} <= set(muse['packs'])
          and 'confident' in ash['styles']['walk'] and muse['skeleton'] == 'mixamo-v1',
          {'libraries': libraries, 'muse': muse, 'ash': {k: ash[k] for k in ('skeleton', 'styles', 'packs')}})
    check('non-characters have no motion', all(e['motion'] is None for e in entries if 'character' not in e['traits']), None)

    # 2. The Muse walks the route with the Desert-only stroll.
    walker = client.call('npcs.spawn', {'ref': MUSE, 'path': 'package/yard-loop', 'phase': .1, 'style': {'walk': 'stroll'}})['id']
    wait(lambda: (agent(walker) or {}).get('activity') == 'walking', timeout=30); time.sleep(2)
    # At its walk speed the gait may sit at the very start of the walk-run blend (run at ~0 weight
    # over walk): the walk it plays is the clip, or the blend's previous one.
    walked = lambda a: a['played'] if a['clip'] == 'walk' else a['playedPrevious'] if a['clip'] == 'run' and a['gait']['fade'] < .5 else a['played']
    a = agent(walker); inst = next(i for i in state()['objects']['instances'] if i['id'] == walker)
    check('the Muse walks playing walk_stroll (a Desert-only clip, retargeted)', walked(a) == 'walk_stroll'
          and inst['style'] == {'walk': 'stroll'} and a['speed'] > .3,
          {'clip': a['clip'], 'played': a['played'], 'previous': a['playedPrevious'], 'fade': a['gait']['fade'], 'speed': a['speed'], 'style': inst['style']})
    p = a['position']; h = a['heading']; f = [math.sin(h), math.cos(h)]
    look([p[0] + f[0] * 3.2 + f[1] * 1.2, 1.5, p[2] + f[1] * 3.2 - f[0] * 1.2], [p[0], 1.0, p[2]]); time.sleep(.4)
    shot('motion-muse-stroll.png')
    client.call('npcs.bind', {'id': walker, 'style': None}); time.sleep(1)
    check('a rebind with style null plays the plain walk again', walked(agent(walker)) == 'walk'
          and next(i for i in state()['objects']['instances'] if i['id'] == walker)['style'] is None, walked(agent(walker)))
    client.call('npcs.remove', {'id': walker})

    # 3. Ash as the companion, with the Muse's confident walk.
    camera([-10, 1.7, 9.5], math.pi / 2)
    client.call('companion.set', {'ref': ASH})
    wait(lambda: comp()['state']['state'] == 'active' and comp()['position'] is not None, timeout=30); time.sleep(1)
    client.call('companion.mode', {'mode': 'follow', 'style': {'walk': 'confident'}}); time.sleep(.5)
    played = set()
    drive([[-10, 9.5], [0, 9.5], [0, 4]], 1.0, sample=lambda: played.add(walked(agent('companion')) if agent('companion') else None))
    check('Ash follows with walk_confident', 'walk_confident' in played and state()['companion']['style'] == {'walk': 'confident'},
          {'played': sorted(map(str, played)), 'style': state()['companion']['style']})
    client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'stay'}); time.sleep(.3)
    client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'follow'}); time.sleep(.3)
    check('the style is kept across Stay and Follow', state()['companion']['style'] == {'walk': 'confident'}, state()['companion']['style'])
    # On the route (npcs.bind companion) it keeps the style: a still from in front of it.
    client.call('npcs.bind', {'id': 'companion', 'path': 'package/yard-loop', 'phase': .3})
    wait(lambda: (agent('companion') or {}).get('activity') == 'walking', timeout=30); time.sleep(2)
    a = agent('companion'); p = a['position']; h = a['heading']; f = [math.sin(h), math.cos(h)]
    look([p[0] + f[0] * 3.2 + f[1] * 1.2, 1.5, p[2] + f[1] * 3.2 - f[0] * 1.2], [p[0], 1.0, p[2]]); time.sleep(.4)
    check('on a route Ash keeps the confident walk', walked(agent('companion')) == 'walk_confident', walked(agent('companion')))
    shot('motion-ash-confident.png')
    client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'follow'}); time.sleep(3)

    # 4. The Muse as the companion dances.
    client.call('companion.set', {'ref': MUSE})
    wait(lambda: comp()['state']['state'] == 'active' and comp()['state']['ref'] == MUSE and comp()['position'] is not None, timeout=30)
    time.sleep(3)
    c = comp()['position']; f = feet()
    look([f[0], 1.7, f[2]], [c[0], 1.0, c[2]])
    wait(lambda: (state()['npcs']['aimed'] or {}).get('objectId') == 'companion', timeout=10)
    aimed = state()['npcs']['aimed']
    dance = next((i for i in aimed['interactions'] if i['type'] == 'dance'), None)
    check('aimed, the Muse offers Dance with her dances as options', dance and {'shimmy', 'step'} <= {o['id'] for o in dance['options']}
          and all(i['options'] is None for i in aimed['interactions'] if i['type'] != 'dance'),
          {'interactions': [{k: i[k] for k in ('id', 'type')} for i in aimed['interactions']], 'options': dance and len(dance['options'])})
    client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'dance', 'option': 'shimmy'})
    wait(lambda: state()['companion']['dance'] == 'shimmy', timeout=5); time.sleep(1.5)
    check('Dance shimmy: activity dancing, playing dance_shimmy', state()['companion']['activity'] == 'dancing'
          and agent('companion')['played'] == 'dance_shimmy', {'state': state()['companion'], 'played': agent('companion')['played']})
    c = comp()['position']; look([c[0] + 2.2, 1.4, c[2] + 2.2], [c[0], 1.0, c[2]]); time.sleep(.5); shot('motion-muse-dance.png')
    osub = old.call('state.subscribe', {'topics': ['state']})['snapshot']['state']
    check('a 0.27 peer sees no style or dance and a dancing companion as idle', 'style' not in osub['companion'] and 'dance' not in osub['companion']
          and osub['companion']['activity'] == 'idle' and all('style' not in i for i in osub['objects']['instances'])
          and all('motion' not in e for e in old.call('objects.catalog', {})['entries']), osub['companion'])
    try:
        client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'dance', 'option': 'polka'}); refused = 'accepted'
    except Exception as e:  # noqa: BLE001 - the refusal is the evidence
        refused = str(e)
    check('an unknown dance is refused (-32602)', '-32602' in refused, refused)
    wait(lambda: state()['companion']['dance'] is None, timeout=25)
    client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'dance'})
    wait(lambda: state()['companion']['dance'] is not None, timeout=5)
    any_dance = state()['companion']['dance']
    check('Dance without an option dances one of hers', any_dance in {o['id'] for o in dance['options']}, any_dance)
    f = feet(); drive([[f[0], f[2]], [f[0] - 8, f[2]]], 1.6)
    wait(lambda: state()['companion']['dance'] is None, timeout=10)
    check('walking off ends the dance', state()['companion']['activity'] != 'dancing', state()['companion']['activity'])

    evidence = s.ROOT / 'docs/evidence/motion.json'
    evidence.write_text(json.dumps({'checks': checks, 'libraries': libraries}, indent=1) + '\n')
    print(f'PASS motion: {len(checks)} checks', flush=True)
