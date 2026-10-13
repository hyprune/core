"""The waypoint: its route line on the ground, its marker label, arrival (RFC-0012):
dev/nested/run.sh --waypoint.

Nested only. Lumen Reach (HYPRUNE_LUMEN_PACKAGE, else the worlds checkout's dist package);
HYPRUNE_NESTED_4K=1 for the 4K label layout (labels at 2x). Checks:
 1. Graph routes to every place from the arrival: no leg of the drawn line passes through
    collision. From Signal Rise, a waypoint and route to the Rime Vault zone (off the graph,
    the straight leg through the hill): the route follows the walking graph, its line lies on
    the ground, and autopilot walks it until arrival clears the waypoint.
 2. The marker: the fallback "+" panel ends before the label's panel starts (no glyph
    covered) at this output's scale.
 3. Arrival: 3 m away across the ground it stays; 6 m above it (another level) it stays;
    within 1.5 m it clears, and the route ending there goes with it.
Stills in the state dir (HYPRUNE_WAYPOINT_SHOTS: elsewhere): waypoint-route.png,
waypoint-label.png (a crop round the marker).
"""
import json, math, os, pathlib, shutil, subprocess, time
import session as s
from m2 import wait

LUMEN = 'com.hyprune.lumen-reach'
RISE = [38.87, 5.70, -145.09]  # an owner capture's eye at Signal Rise (finding: the white line)


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--waypoint needs {LUMEN} at {source} (HYPRUNE_LUMEN_PACKAGE)')
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(source, installed / LUMEN)


def acceptance(client, children, args, token, socket):
    out = pathlib.Path(os.environ.get('HYPRUNE_WAYPOINT_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        assert ok, (name, data)
        print('PASS waypoint:', name, json.dumps(data)[:400] if data is not None else '', flush=True)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    diag = lambda: s.camera()

    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})')
        time.sleep(.4)

    def shot(name):
        path = out / name
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        return path

    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: diag()['active'], 60)
    client.call('movement.set', {'modeId': 'com.hyprune.walk'}); time.sleep(1)
    start = diag()['position']
    places = state()['spatial']['world']['places']

    # 1. Routes and their drawn lines.
    def route_to(place, label):
        client.call('waypoint.set', {'id': 'test-' + place['id'], 'label': label, 'target': {'position': place['position']}})
        client.call('route.set', {'points': [{'position': place['position']}]})
        time.sleep(.5)
        return diag()['route']
    # Graph routes to every place: no drawn leg goes through collision.
    tried = []
    for p in places:
        r = route_to(p, p['name'])
        tried.append({'id': p['id'], 'points': r['points'], 'buried': r['buried']})
    graph = [t for t in tried if t['points']]
    check('graph routes to every place: no leg of the drawn line goes through collision',
          graph and all(t['buried'] == 0 for t in graph), tried)
    # The owner's case: at Signal Rise, a resident's waypoint to the Rime Vault zone (its centre
    # is no graph place, and the straight leg runs through the hill): the route takes the walking
    # graph, the line lies on it, and autopilot walks it.
    zone = next(z for z in state()['location']['zones'] if z['id'] == 'rime-vault')
    camera(RISE, 0); time.sleep(.5)
    client.call('waypoint.set', {'id': 'test-rime-vault', 'label': 'Way to Rime Vault', 'target': {'zoneId': 'rime-vault'}})
    client.call('route.set', {'points': [{'zoneId': 'rime-vault'}]}); time.sleep(.5)
    route = state()['location']['route']
    r = diag()['route']
    check('an off-graph target behind a hill: the route takes the walking graph, its line on the ground',
          len(route) > 2 and route[-1]['target'] == {'zoneId': 'rime-vault'} and r['buried'] == 0,
          {'route': [p['position'] for p in route], 'drawn': r['points'], 'buried': r['buried'], 'zone': zone['id']})
    first = route[1]['position']
    d = [first[0] - RISE[0], first[2] - RISE[2]]
    camera(RISE, math.atan2(d[0], -d[1]), -.3); time.sleep(.8)
    shot('waypoint-route-rise.png')
    client.call('autodrive.start', {'speed': 3.5})
    t0 = time.monotonic()
    try:
        wait(lambda: state()['location']['waypoint'] is None, 150)
    finally:
        if state()['location'].get('autodrive'):
            client.call('autodrive.stop', {})
    check('autopilot walks it and arriving clears the waypoint', not state()['location']['route'],
          {'seconds': round(time.monotonic() - t0, 1), 'at': diag()['position']})
    camera(start, 0); time.sleep(.5)
    place = next((p for p in places if p['id'] == 'globe-platform'), places[1])
    label = place['name'][:24]
    route_to(place, label)
    route = state()['location']['route']
    # Look along the route from the feet, at the first point some metres out.
    first = route[min(1, len(route) - 1)]['position']
    d = [first[0] - start[0], first[2] - start[2]]
    camera(start, math.atan2(d[0], -d[1]), -.25); time.sleep(.6)
    shot('waypoint-route.png')

    # 2. The marker label, looking at the target from 9 m.
    t = place['position']
    eye = [t[0] + 9 * math.sin(.6), t[1] + 1.5, t[2] + 9 * math.cos(.6)]
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    camera(eye, math.atan2(t[0] - eye[0], -(t[2] - eye[2])), math.atan2(t[1] - eye[1], math.hypot(t[0] - eye[0], t[2] - eye[2])))
    time.sleep(.6)
    labels = diag()['route']['labels']
    plus = next((l for l in labels if l['text'] == '+'), None)
    text = next((l for l in labels if l['text'] == label), None)
    check('the waypoint label starts clear of its "+" marker', plus and text and plus['panel'][1] <= text['panel'][0]
          and abs(plus['y'] - text['y']) < 1, {'plus': plus, 'label': text, 'labels': len(labels)})
    full = shot('waypoint-marker-full.png')
    from PIL import Image
    img = Image.open(full)
    x, y = int(plus['panel'][0]), int(plus['y'])
    img.crop((max(0, x - 60), max(0, y - 80), min(img.width, x + 900), min(img.height, y + 120))).save(out / 'waypoint-label.png')
    full.unlink()

    # 3. Arrival. Fly mode: 3 m off across the ground, then 6 m straight above: it stays.
    camera([t[0] + 3, t[1], t[2]], 0); time.sleep(.5)
    check('3 m away across the ground: the waypoint stays', state()['location']['waypoint'] is not None,
          {'waypoint': state()['location']['waypoint']})
    camera([t[0], t[1] + 6, t[2]], 0); time.sleep(.5)
    check('6 m above it (another level): the waypoint stays', state()['location']['waypoint'] is not None, None)
    camera([t[0] + 1.2, t[1], t[2]], 0)
    loc = wait(lambda: (lambda l: l if l['waypoint'] is None else None)(state()['location']), 5)
    check('within 1.5 m: the waypoint clears, and the route ending there', not loc['route'],
          {'waypoint': loc['waypoint'], 'route': loc['route'], 'drawn': diag()['route']['points']})
    (s.ROOT / 'docs/evidence/waypoint.json').write_text(json.dumps({'nestedOnly': True, 'world': LUMEN,
        'output4k': os.environ.get('HYPRUNE_NESTED_4K') == '1', 'checks': checks}, indent=1) + '\n')
    print('PASS waypoint:', len(checks), 'checks', flush=True)
