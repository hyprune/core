"""User characters spawned in front of the player (RFC-0011 "User-spawned characters"):
dev/nested/run.sh --spawn-spots.

Nested only. Lumen Reach (HYPRUNE_LUMEN_PACKAGE, else the worlds checkout's dist), the companion
character library (HYPRUNE_COMPANION_LIBRARY: Drunk Ash, 1.9 m) and the starter library
(HYPRUNE_NPCS_LIBRARY: its residents). Checks:
 1. In every area, standing on open floor and looking along it, npcs.spawn without a pose puts
    Ash and a starter resident on a standing spot in front of the player (within 6 m, on the
    floor near the player's, in the view's half-plane); each is removed again.
 2. Standing in a window placement zone (Sun Court's open work area), the spot is found past
    its edge, or refused with rule "zone" and a reason naming it.
 3. Facing a wall up close: a spot to the side, or a refusal that says why (its rule and a
    reason after "no free standing spot in front of you: ").
 4. A given spot refused says which rule failed and why: a placement zone ("zone", naming it),
    the arrival spawn ("keep-clear"), mid-air ("support").
HYPRUNE_SPAWN_SPOTS_DIAG=1 prints, for every pose and yaw, what a spot 2 m ahead is refused for.
Evidence: docs/evidence/spawn-spots.json (HYPRUNE_SPAWN_SPOTS_SHOTS: elsewhere).
"""
import hashlib, json, math, os, pathlib, shutil, time
import session as s
from m2 import wait

LUMEN = 'com.hyprune.lumen-reach'
ASH_ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_COMPANION_LIBRARY', ''))
STARTER_ARCHIVE = s.starter_archive('HYPRUNE_NPCS_LIBRARY')
# Open floor in each area (feet) and the yaw to look along it (0 = -z, pi/2 = +x).
OPEN = [
    ('sun-court', [-8, 0, 12], 0.0),             # arrival, looking north across the court
    ('sun-court', [-6, 0, 6], math.pi / 2),      # west of the open work zone, looking east
    ('sun-court', [10, 0, -4], math.pi),         # east court, looking south
    ('array-hall', [8, 0, -34], 0.0),
    ('rime-vault', [38, -6, -79], math.pi),
    ('signal-rise', [54, 0, -125], math.pi / 2),
]
ZONE = ('sun-court', [2.5, 0, 4], 0.0)           # inside sun-court-open-work


def prepare(installed):
    for archive, env in ((ASH_ARCHIVE, 'HYPRUNE_COMPANION_LIBRARY'), (STARTER_ARCHIVE, 'HYPRUNE_NPCS_LIBRARY')):
        if not archive.is_file():
            raise SystemExit(f'--spawn-spots needs {env} (a library .tar.zst)')
    source = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--spawn-spots needs {LUMEN} at {source} (HYPRUNE_LUMEN_PACKAGE)')
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(source, installed / LUMEN)
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'companion.json']:
        (s.STATE / name).unlink(missing_ok=True)
    for archive in (ASH_ARCHIVE, STARTER_ARCHIVE):
        shutil.copy2(archive, s.STATE / archive.name)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(os.environ.get('HYPRUNE_SPAWN_SPOTS_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    checks, diag = [], []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'spawn-spots-checks.json').write_text(json.dumps({'checks': checks, 'diag': diag}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS spawn-spots:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    def install(archive):
        a = s.STATE / archive.name
        r = client.call('libraries.install', {'archive': str(a), 'sha256': hashlib.sha256(a.read_bytes()).hexdigest()})
        return f"{r['id']}@{r['version']}"

    def stand(feet, yaw):
        s.ctl('eval', f'hl.plugin.hyprune.camera({feet[0]},{feet[1] + 1.7},{feet[2]},{yaw},0)'); time.sleep(.6)

    def spawn(ref, pose=None):
        params = {'ref': ref}
        if pose is not None:
            params['pose'] = pose
        try:
            return client.call('npcs.spawn', params)['id'], None
        except RuntimeError as e:
            return None, e.args[0]

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def placed(ident):
        state = client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
        return next((i for i in state['objects']['instances'] if i['id'] == ident), None)

    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    ash_lib, starter_lib = install(ASH_ARCHIVE), install(STARTER_ARCHIVE)
    entries = client.call('objects.catalog', {})['entries']
    ash = next(e['ref'] for e in entries if e.get('library') == ash_lib and 'character' in e['traits'])
    resident = next(e['ref'] for e in entries if e.get('library') == starter_lib and 'character' in e['traits'])
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60); time.sleep(1)
    client.call('graphics.set', {'patch': {'offload': 'off', 'npc_motion': 'still'}})
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})

    if os.environ.get('HYPRUNE_SPAWN_SPOTS_DIAG') == '1':
        for area, feet, _ in OPEN + [ZONE]:
            for k in range(8):
                yaw = k * math.pi / 4
                stand(feet, yaw)
                ahead = [feet[0] + 2 * math.sin(yaw), feet[1], feet[2] - 2 * math.cos(yaw)]
                row = {'area': area, 'feet': feet, 'yaw': round(math.degrees(yaw))}
                for name, ref in (('ash', ash), ('resident', resident)):
                    ident, err = spawn(ref, {'position': ahead, 'rotation': [0, 0, 0, 1]})
                    row[name + '@2m'] = 'ok' if ident else err
                    if ident:
                        client.call('npcs.remove', {'id': ident})
                    ident, err = spawn(ref)
                    row[name + '@front'] = placed(ident)['pose']['position'] if ident else err
                    if ident:
                        client.call('npcs.remove', {'id': ident})
                diag.append(row)
                print('DIAG', json.dumps(row), flush=True)
        (out / 'spawn-spots-diag.json').write_text(json.dumps(diag, indent=1) + '\n')

    # 1. Open floor in every area: both characters, in front.
    for area, feet, yaw in OPEN:
        stand(feet, yaw)
        eye = client.call('camera.get', {})['position']
        fwd = [math.sin(yaw), -math.cos(yaw)]
        for name, ref in (('Drunk Ash', ash), ('a starter resident', resident)):
            ident, err = spawn(ref)
            o = placed(ident) if ident else None
            p = o['pose']['position'] if o else None
            d = [p[0] - eye[0], p[2] - eye[2]] if p else None
            ok = (p is not None and math.hypot(*d) <= 6.1 and d[0] * fwd[0] + d[1] * fwd[1] > .25 and
                  abs(p[1] - feet[1]) < 1.2)
            check(f'{area} at {feet}: {name} spawns in front on open floor', ok,
                  {'eye': eye, 'spot': p, 'error': err})
            client.call('npcs.remove', {'id': ident}); time.sleep(.2)

    # 2. In a window placement zone: a spot past it, or a refusal that names it.
    area, feet, yaw = ZONE
    stand(feet, yaw)
    ident, err = spawn(ash)
    if ident:
        p = placed(ident)['pose']['position']
        zone = json.loads((s.STATE / 'worlds' / LUMEN / 'world.json').read_text())['placementZones'][0]['bounds']
        inside = all(zone['min'][i] - .2 <= p[i] <= zone['max'][i] + .2 for i in (0, 2))
        check('in a placement zone, the spot is found outside the zone', not inside, {'spot': p, 'zone': zone})
        client.call('npcs.remove', {'id': ident})
    else:
        check('in a placement zone, the refusal names the zone', err.get('data', {}).get('rule') == 'zone' and
              'sun-court-open-work' in err.get('message', ''), err)

    # 3. Facing a wall up close: the reason is given.
    stand([2, 0, -12.6], 0.0)   # the Sun Court's north wall, a step in front of it
    ident, err = spawn(ash)
    if ident:
        client.call('npcs.remove', {'id': ident})
    check('facing a wall: a spot to the side, or a refusal with its rule and reason',
          ident is not None or (err['message'].startswith('no free standing spot in front of you: ') and
                                err.get('data', {}).get('rule') in ('walls', 'support', 'approach', 'keep-clear', 'zone', 'playable')),
          {'id': ident, 'error': err})

    # 4. A given spot that is refused says which rule and why (no longer a blanket "support").
    for label, pose, rule, words in (('in a window placement zone', [2.5, 0, 4], 'zone', 'sun-court-open-work, kept free for windows'),
                                     ('on the arrival spawn', [-8, 0, 12], 'keep-clear', 'keep-clear'),
                                     ('in mid-air', [-8, 6, 30], 'support', 'no floor')):
        ident, err = spawn(ash, {'position': pose, 'rotation': [0, 0, 0, 1]})
        if ident:
            client.call('npcs.remove', {'id': ident})
        check(f'a refused spot {label} gives rule {rule} and the reason', ident is None and err['data'].get('rule') == rule and
              err['message'].startswith('not a free standing spot: ') and words in err['message'], err)

    if not os.environ.get('HYPRUNE_SPAWN_SPOTS_SHOTS'):
        (s.ROOT / 'docs/evidence/spawn-spots.json').write_text(json.dumps(
            {'nestedOnly': True, 'world': LUMEN, 'characters': [ash, resident], 'checks': checks}, indent=1) + '\n')
    print(f'PASS spawn-spots: {len(checks)} checks', flush=True)
