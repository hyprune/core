"""RFC-0011 P1 (IPC 0.22): dev/nested/run.sh --catalog.

Nested only (isolated harness on headless hosts). Lumen Reach (HYPRUNE_CATALOG_WORLD,
else the worlds checkout's dist package), a library archive (HYPRUNE_CATALOG_LIBRARY,
else the starter library dry run) and the CC0 objects of tests/fixtures/catalog.py.
HYPRUNE_GIZMO_OBJECTS=1 stands in for world 0.9's movable authored objects.
Checks:
1. Contract: 0.22 hello with objects.control, the objects state key, the new methods.
2. libraries.install: hash checked, installed, idempotent; libraries.list; the catalog with
   placeable flags and reasons (characters, movers), the SDK's triangle and GPU estimates.
3. The user folder: an object dropped in is validated after the quiet period; an invalid
   one is listed with its errors.
4. Pocket and tool 5: objects.pocket selects tool 5 and the item; the ghost follows the
   aim (valid on open floor, refused in a screen approach); Primary places it.
5. Refusals: tool 5 in an approach, objects.place inside a wall (-32007, rule in data).
6. Tool 4: select the placed object, drag the move handle; the overlay records the pose.
7. Tool 5 Alternate pockets a user object; remove; undo restores it.
8. Authored objects: transform, hide (no draw, no collision), reset.
9. Lock: configure locked refuses transforms and drags.
10. Protection and budgets: characters can't be pocketed; 17 pocket items refused.
11. Persistence: a world reload keeps placements and poses; editor.json holds them.
12. libraries.remove makes placements missing; reinstalling restores them.
Evidence: docs/evidence/catalog.json and stills in the state dir.
"""
import hashlib, json, math, os, pathlib, shutil, subprocess, time
import session as s
from m2 import wait, keys

WORLD = 'com.hyprune.lumen-reach'
ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_CATALOG_LIBRARY', str(pathlib.Path.home() / 'objects-p1/library-dry-run/com.hyprune.starter-0.1.0.tar.zst')))
RACK = 'library/com.hyprune.starter@0.1.0/server-rack'
ARMILLARY = 'sun-court-armillary'


def prepare(installed):
    os.environ['HYPRUNE_GIZMO_OBJECTS'] = '1'
    source = pathlib.Path(os.environ.get('HYPRUNE_CATALOG_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--catalog needs Lumen Reach at {source} (HYPRUNE_CATALOG_WORLD)')
    if not ARCHIVE.is_file():
        raise SystemExit(f'--catalog needs a library archive at {ARCHIVE} (HYPRUNE_CATALOG_LIBRARY)')
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir()
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'editor.json.v1', 'layout.json', 'input.json']:
        (s.STATE / name).unlink(missing_ok=True)
    subprocess.run(['python3', str(s.ROOT / 'tests/fixtures/catalog.py')], check=True, stdout=subprocess.DEVNULL)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)


def acceptance(client, children, args, shell_token, socketpath):
    if os.environ.get('CATALOG_STAGE') == 'probe':  # development: where does an object fit?
        client.call('world.load', {'worldId': WORLD})
        archive = s.STATE / ARCHIVE.name
        client.call('libraries.install', {'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
        ref = os.environ.get('CATALOG_PROBE_REF', 'library/com.hyprune.starter@0.1.0/armillary')
        lift = float(os.environ.get('CATALOG_PROBE_LIFT', '2.055'))
        for z in range(-24, 16, 2):
            row = ''
            for x in range(-34, 26, 2):
                try:
                    r = client.call('objects.place', {'ref': ref, 'pose': {'position': [x, lift, z], 'rotation': [0, 0, 0, 1]}})
                    client.call('objects.remove', {'id': r['id']}); row += 'O'
                except RuntimeError as e:
                    row += (e.args[0].get('data') or {}).get('rule', '?')[0]
            print(f'probe z={z:4d}', row, flush=True)
        return
    if os.environ.get('CATALOG_STAGE') == 'demo':
        s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
        client.call('world.load', {'worldId': WORLD})
        s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
        wait(lambda: s.camera()['active'])
        from catalog_demo import run
        return run(client, children, shell_token, socketpath, s.STATE / ARCHIVE.name, WORLD)
    results = []
    out = s.STATE / 'catalog'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir()

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS catalog:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)

    def click(button=272):
        pointer('button', button, 1); time.sleep(.12); pointer('button', button, 0); time.sleep(.35)

    for _ in range(4):
        xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    def objects():
        return state()['objects']

    def instance(id_):
        return next((i for i in objects()['instances'] if i['id'] == id_), None)

    def user():
        return [i for i in objects()['instances'] if i['source'] == 'user']

    def diag():
        return s.camera()

    def ghost():
        return diag()['objects']['ghost']

    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})')
        time.sleep(.3)

    def look(pos, target):
        d = [target[i] - pos[i] for i in range(3)]
        camera(pos, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def shot(name):
        path = out / name
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        return path.name

    def error(method, params):
        try:
            client.call(method, params)
        except RuntimeError as e:
            return e.args[0]
        return None

    def notices(code):
        return [e['params'] for e in client.events if e.get('method') == 'runtime.notice' and e['params']['code'] == code]

    def overlay():
        return json.loads((s.STATE / 'editor.json').read_text())

    def spot(candidates, valid=True):
        """First camera pose whose ghost is (in)valid; returns (pose, ghost)."""
        for pos, yaw, pitch in candidates:
            camera(pos, yaw, pitch)
            g = wait(lambda: ghost() if ghost()['asset'] >= 0 else None, 20)
            time.sleep(.3)
            g = ghost()
            if g['valid'] == valid:
                return (pos, yaw, pitch), g
        raise AssertionError(('no spot', valid, g))

    open_floor = [((-8, 1.7, 8), 0, -.35), ((-4.5, 1.7, 8), 0, -.35), ((-11, 1.7, 8), 0, -.35), ((-6, 1.7, 6), -.3, -.4),
                  ((-10, 1.7, 6), .3, -.4), ((-8, 1.7, 5), 0, -.45)]

    # 1. Contract.
    hello = client.hello
    new = {'objects.catalog', 'objects.pocket', 'objects.place', 'objects.transform', 'objects.remove', 'objects.hide',
           'objects.reset', 'objects.configure', 'libraries.list', 'libraries.install', 'libraries.remove'}
    check('IPC 0.22 with objects.control and the object and library methods',
          hello['protocol'] == '0.22' and 'objects.control' in hello['capabilities'] and new <= set(hello['methods']),
          {'protocol': hello['protocol']})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active'])
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    st = state()
    arm = instance(ARMILLARY)
    check('state carries objects: the authored armillary, empty pocket, usage and limits',
          arm and arm['source'] == 'authored' and arm['state'] == 'active' and arm['ref'] == 'package/armillary' and
          st['objects']['pocket'] == [] and st['objects']['usage']['placements'] == 0 and st['objects']['limits']['placements'] == 128 and
          'objects' in st['editor']['unplaced'] and st['editor']['gizmo'] == {'space': 'world', 'pivot': 'centre', 'snap': False},
          {'armillary': arm, 'limits': st['objects']['limits']})

    # 2. Libraries.
    archive = s.STATE / ARCHIVE.name
    sha = hashlib.sha256(archive.read_bytes()).hexdigest()
    bad = error('libraries.install', {'archive': str(archive), 'sha256': '0' * 64})
    check('libraries.install checks the archive hash first', bad and bad['code'] == -32602 and 'sha256' in bad['message'], bad)
    installed = client.call('libraries.install', {'archive': str(archive), 'sha256': sha})
    again = client.call('libraries.install', {'archive': str(archive), 'sha256': sha})
    libs = client.call('libraries.list', {})['libraries']
    check('the starter library installs (and again, idempotently) and lists ready',
          installed['id'] == 'com.hyprune.starter' and installed['version'] == '0.1.0' and again['id'] == installed['id'] and
          libs and libs[0]['state'] == 'ready' and libs[0]['objects'] == 11 and libs[0]['root'] == 'user' and
          (s.STATE / 'datahome/hyprune/libraries/com.hyprune.starter@0.1.0/library.json').is_file() and
          any('already installed' in n['message'] for n in notices('library.installed')),
          {'install': installed, 'library': libs[0]})
    catalog = client.call('objects.catalog', {})
    entries = {e['ref']: e for e in catalog['entries']}
    rack = entries[RACK]
    reasons = {e['id']: e['reason'] for e in catalog['entries'] if not e['placeable']}
    check('catalog: library objects with placeability, characters and movers refused with a reason',
          catalog['revision'] == state()['objects']['catalogRevision'] and rack['placeable'] and rack['state'] == 'ready' and
          rack['traits'] == ['collidable', 'decorative', 'machine', 'movable'] and rack['thumbnail'].endswith('server-rack/thumbnail.png') and
          reasons['resident-blue'].startswith('character:') and reasons['job-token'].startswith('mover:') and
          entries['package/armillary']['placeable'] and entries['library/com.hyprune.starter@0.1.0/armillary']['placeable'],
          {'entries': len(entries), 'reasons': reasons})
    check('catalog estimates are the SDK\'s: server rack triangles, GPU bytes, proxy triangles',
          rack['triangles'] == 2818 and rack['gpuBytes'] == 448238 and rack['collisionTriangles'] == 24 and
          rack['bounds'] == {'min': [-0.62, 0, -0.65], 'max': [0.62, 2.16, 0.65]},
          {k: rack[k] for k in ['triangles', 'gpuBytes', 'collisionTriangles', 'bounds']})

    # 3. The user folder: drop an object in (and an invalid one).
    folder = s.STATE / 'datahome/hyprune/objects'
    folder.mkdir(parents=True, exist_ok=True)
    before = objects()['catalogRevision']
    shutil.copytree(s.ROOT / 'tests/fixtures/catalog/user/lamp', folder / 'lamp')
    shutil.copytree(s.ROOT / 'tests/fixtures/catalog/user/broken', folder / 'broken')
    wait(lambda: objects()['catalogRevision'] > before, 15)
    entries = {e['ref']: e for e in client.call('objects.catalog', {})['entries']}
    lamp, broken = entries.get('user/lamp'), entries.get('user/broken')
    check('a dropped user object is validated and listed ready; an invalid one with its errors',
          lamp and lamp['placeable'] and lamp['source'] == 'user' and lamp['triangles'] == 36 and
          broken and broken['state'] == 'invalid' and not broken['placeable'] and broken['errors'][0].startswith('bounds:') and
          notices('catalog.invalid'),
          {'lamp': {k: lamp[k] for k in ['ref', 'state', 'triangles', 'gpuBytes']}, 'broken': broken['errors']})

    # 4. Pocket and tool 5.
    pocket = client.call('objects.pocket', {'ref': RACK})
    st = state()
    check('objects.pocket selects tool 5 and the pocket item',
          st['editor']['tool'] == 5 and st['editor']['unplaced']['selectedId'] == pocket['itemId'] and
          st['editor']['unplaced']['objects'] == [{'itemId': pocket['itemId'], 'ref': RACK, 'name': 'Server rack'}] and
          st['editor']['unplaced']['count'] == 1 and st['objects']['pocket'][0]['ref'] == RACK, st['editor']['unplaced'])
    where, g = spot(open_floor)
    ghost_shot = shot('tool5-ghost.png')
    check('the ghost follows the aim, upright, on open floor, valid', g['valid'] and abs(g['position'][1]) < .02 and
          abs(g['rotation'][0]) < 1e-4 and abs(g['rotation'][2]) < 1e-4 and state()['interaction']['reticle'] == {'state': 'valid', 'action': 'place'},
          {'ghost': g, 'camera': where})
    click()
    placed = wait(lambda: user() or None, 10)[0]
    rack_id = placed['id']
    st = state()
    check('Primary places the rack where the ghost was; the pocket item is consumed',
          placed['ref'] == RACK and placed['state'] == 'active' and placed['movable'] and
          max(abs(placed['pose']['position'][i] - g['position'][i]) for i in range(3)) < .05 and
          st['objects']['pocket'] == [] and st['objects']['usage']['placements'] == 1 and st['objects']['usage']['triangles'] == 2818 and
          any(p['id'] == rack_id for p in overlay()['placements']), placed)
    inst = next(i for i in wait(lambda: diag()['objects']['instances'], 5) if i['id'] == rack_id)
    check('the rack has collision at its pose', inst['collision'][1] == 24 and not inst['hidden'], inst)
    time.sleep(1)
    look([where[0][0] + 2.5, 1.9, where[0][2] - 1], placed['pose']['position'])
    placed_shot = shot('rack-placed.png')

    # 5. Refusals.
    client.call('objects.pocket', {'ref': 'user/lamp'})
    refused_at, g = spot([((-12, 1.7, 4), -1.2, -.35), ((-2, 1.7, 4), 1.2, -.35), ((-8, 1.7, 0), 0, -.4)], valid=False)
    refused_shot = shot('tool5-refused.png')
    count = len(user())
    client.events.clear()
    click()
    time.sleep(.5)
    check('tool 5 refuses a blocked pose: nothing placed, the rule said', len(user()) == count and notices('placement.blocked') and
          g['rule'] in ('approach', 'zone', 'walls', 'keep-clear'), {'ghost': g, 'notice': notices('placement.blocked')[-1]})
    wall = error('objects.place', {'ref': RACK, 'pose': {'position': [-8.0, 1.2, -1.07], 'rotation': [0, 0, 0, 1]}})
    check('objects.place inside a wall: -32007 naming the rule in error data',
          wall and wall['code'] == -32007 and wall['data'] == {'retryable': False, 'rule': 'walls'}, wall)
    _, g = spot([p for p in open_floor if p != where])
    click()
    lamp_id = wait(lambda: next((i['id'] for i in user() if i['ref'] == 'user/lamp'), None), 10)
    check('the user lamp places from the pocket', instance(lamp_id)['state'] == 'active', instance(lamp_id))

    # 6. Tool 4 moves the rack.
    keys('-k', '4'); time.sleep(.3)
    p = instance(rack_id)['pose']['position']
    look([p[0], p[1] + 2.2, p[2] + 3.4], [p[0], p[1] + 1.0, p[2]])
    click()
    wait(lambda: state()['editor']['selectedId'] == 'object:' + rack_id, 5)
    check('tool 4 selects the user object', state()['editor']['selectedKind'] == 'object', state()['editor']['selectedId'])

    def aim(part):
        for _ in range(6):
            g4 = wait(lambda: diag()['gizmo'] if part in diag()['gizmo']['handles'] else None, 5)
            look(diag()['position'], g4['handles'][part]['world'])
            time.sleep(.15)
            if diag()['gizmo']['hover'] == part:
                return diag()['gizmo']
        return wait(lambda: diag()['gizmo'] if diag()['gizmo']['hover'] == part else None, 3)
    g4 = aim('move-x')
    px, py = g4['pivotScreen']
    dx, dy = (.5 - px) * 1920, (.5 - py) * 1080
    n = math.hypot(dx, dy) or 1
    start = instance(rack_id)['pose']['position']
    pointer('button', 272, 1); time.sleep(.15)
    for _ in range(6):
        pointer(-dx / n * 30, -dy / n * 30); time.sleep(.05)
    time.sleep(.25)
    pointer('button', 272, 0); time.sleep(.5)
    moved = instance(rack_id)['pose']['position']
    record = next(r for r in overlay()['placements'] if r['id'] == rack_id)
    check('a move-handle drag moves the rack along x; the overlay records it',
          abs(moved[0] - start[0]) > .1 and abs(moved[1] - start[1]) < 1e-3 and abs(moved[2] - start[2]) < 1e-3 and
          abs(record['pose']['position'][0] - moved[0]) < 1e-4, {'from': start, 'to': moved})
    drag_shot = shot('rack-tool4.png')
    client.events.clear()
    keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'z', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.5)
    check('undo returns the rack', max(abs(instance(rack_id)['pose']['position'][i] - start[i]) for i in range(3)) < 1e-3,
          instance(rack_id)['pose'])
    keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'y', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.5)

    # 7. Tool 5 Alternate pockets the lamp; remove; undo.
    keys('-k', '5'); time.sleep(.3)
    lp = instance(lamp_id)['pose']['position']
    look([lp[0], 1.7, lp[2] + 2.5], [lp[0], .8, lp[2]])
    client.events.clear()
    click(274)
    st = state()
    check('tool 5 Alternate pockets a user object: it leaves the world, the pocket selects it',
          not instance(lamp_id) and st['editor']['unplaced']['objects'][-1]['ref'] == 'user/lamp' and
          st['editor']['unplaced']['selectedId'] == st['editor']['unplaced']['objects'][-1]['itemId'] and notices('placement.pocketed'),
          st['editor']['unplaced'])
    _, g = spot(open_floor)
    click()
    lamp_id = wait(lambda: next((i['id'] for i in user() if i['ref'] == 'user/lamp'), None), 10)
    client.call('objects.remove', {'id': lamp_id})
    check('objects.remove deletes a user placement and its record',
          not instance(lamp_id) and not any(r['id'] == lamp_id for r in overlay()['placements']), lamp_id)
    keys('-k', '4'); time.sleep(.3)
    keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'z', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.6)
    check('undo restores the removed object', instance(lamp_id) and instance(lamp_id)['state'] == 'active', instance(lamp_id))

    # 8. The authored armillary: transform, hide, reset.
    a0 = instance(ARMILLARY)['pose']
    p = list(a0['position']); p[0] += .5
    client.call('objects.transform', {'id': ARMILLARY, 'pose': {'position': p, 'rotation': a0['rotation']}})
    moved = instance(ARMILLARY)
    o = next(r for r in overlay()['overrides'] if r['objectId'] == ARMILLARY)
    check('objects.transform moves an authored object into an override',
          abs(moved['pose']['position'][0] - p[0]) < 1e-4 and o['pose']['position'][0] == p[0], o)
    client.call('objects.hide', {'id': ARMILLARY, 'hidden': True})
    dinst = next(i for i in diag()['objects']['instances'] if i['id'] == ARMILLARY)
    look([-7, 5, -4], [-7, 4.18, -10])
    hidden_shot = shot('armillary-hidden.png')
    check('a hidden authored object is not drawn and has no collision',
          instance(ARMILLARY)['state'] == 'hidden' and dinst['hidden'] and dinst['collision'][1] == 0, dinst)
    client.call('objects.reset', {'id': ARMILLARY})
    check('objects.reset clears the override: authored pose, shown',
          instance(ARMILLARY)['state'] == 'active' and instance(ARMILLARY)['pose'] == a0 and
          not any(r['objectId'] == ARMILLARY for r in overlay()['overrides']), instance(ARMILLARY))
    hide_user = error('objects.hide', {'id': rack_id, 'hidden': True})
    check('user objects are removed, not hidden', hide_user and hide_user['code'] == -32005, hide_user)

    # 9. Lock.
    client.call('objects.configure', {'id': rack_id, 'patch': {'locked': True}})
    locked = error('objects.transform', {'id': rack_id, 'pose': instance(rack_id)['pose']})
    check('a locked object refuses transforms (-32005); the state says so',
          instance(rack_id)['locked'] and locked and locked['code'] == -32005 and
          next(r for r in overlay()['placements'] if r['id'] == rack_id)['locked'], locked)
    client.call('objects.configure', {'id': rack_id, 'patch': {'locked': False}})

    # 10. Protection and budgets.
    char = error('objects.pocket', {'ref': 'library/com.hyprune.starter@0.1.0/resident-blue'})
    check('characters are never pocketed or placed (P2): -32005 with the reason',
          char and char['code'] == -32005 and char['message'].startswith('character:'), char)
    for _ in range(16 - len(objects()['pocket'])):
        client.call('objects.pocket', {'ref': 'user/lamp'})
    full = error('objects.pocket', {'ref': 'user/lamp'})
    check('the pocket holds 16 objects: the 17th is a budget refusal (-32008)',
          full and full['code'] == -32008 and full['data']['rule'] == 'budget', full)
    unknown = error('objects.transform', {'id': 'u-000000', 'pose': instance(rack_id)['pose']})
    check('unknown ids are -32004', unknown and unknown['code'] == -32004, unknown)

    # 11. Persistence across a world reload.
    before = {i['id']: i['pose'] for i in user()}
    client.call('world.load', {'worldId': WORLD})
    wait(lambda: all(instance(i) and instance(i)['state'] == 'active' for i in before), 20)
    after = {i['id']: i['pose'] for i in user()}
    check('a world reload keeps every placement and pose; the pocket is dropped',
          after == before and objects()['pocket'] == [] and len(overlay()['placements']) == len(before), after)
    time.sleep(1)
    look([-8, 2.4, 9], instance(rack_id)['pose']['position'])
    reload_shot = shot('after-reload.png')

    # 12. Library removal and reinstall.
    client.call('libraries.remove', {'id': 'com.hyprune.starter', 'version': '0.1.0'})
    wait(lambda: instance(rack_id)['state'] == 'missing', 15)
    check('removing the library makes its placements missing (record kept)',
          not (s.STATE / 'datahome/hyprune/libraries/com.hyprune.starter@0.1.0').exists() and
          any(r['id'] == rack_id for r in overlay()['placements']) and notices('object.missing'), instance(rack_id))
    client.call('libraries.install', {'archive': str(archive), 'sha256': sha})
    wait(lambda: instance(rack_id)['state'] == 'active', 20)
    check('reinstalling restores them', instance(rack_id)['pose'] == before[rack_id], instance(rack_id))

    stills = [ghost_shot, placed_shot, refused_shot, drag_shot, hidden_shot, reload_shot]
    evidence = {'nestedOnly': True, 'world': WORLD, 'protocol': '0.22', 'library': ARCHIVE.name, 'stills': stills, 'checks': results}
    (s.ROOT / 'docs/evidence/catalog.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('PASS catalog:', len(results), 'checks', flush=True)
    client.call('runtime.exit', {})
