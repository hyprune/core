"""Live-take regressions: dev/nested/run.sh --live-take.

Nested only (run.sh holds /tmp/worldshell-nested.lock). Replays three findings from
a supervised take in Lumen Reach:

1. A terminal opened from the Sun Court plaza lands in front of the player on
   reachable floor, not in a far corner of the area's free zone in mid-air.
2. Browsers shown on a home workspace wall keep presenting: Chromium (and an
   Electron app when available) launched without any backgrounding flags keep
   committing frames while their workspace is not visible on any output.
3. Windows moved onto home workspaces take the area's primary wall first, then
   its tool anchors in authored order, with no relocation notices and no
   placeholder poses left for windows shown on a wall.

HYPRUNE_LIVE_TAKE_WORLD: unpacked Lumen Reach package (default ../worlds/dist).
HYPRUNE_LIVE_TAKE_OBSERVE=1 records results without asserting (for "before" runs).
Results go to <state>/live-take.json and screenshots to <state>/live-take-*.png.
"""
import json, math, os, pathlib, shutil, subprocess, tempfile, time
import session as s
from m2 import wait, pose

LUMEN = 'com.hyprune.lumen-reach'
OBSERVE = os.environ.get('HYPRUNE_LIVE_TAKE_OBSERVE') == '1'
ELECTRON = next((p for p in ['/usr/lib/electron39/electron', '/usr/lib/electron/electron', shutil.which('electron') or '']
                 if p and os.path.exists(p)), None)
CHROMIUM = shutil.which('chromium') or shutil.which('google-chrome-stable')
# Capture pose of the take: camera on the Sun Court plaza, looking towards the terrace.
PLAZA = ([-13.2327, 1.7002, 11.6469], 0.92676, 0.04071)
PAGE = """<!doctype html><title>Animated</title><body style="margin:0;background:#123">
<canvas id=c style="width:100vw;height:100vh"></canvas><script>
const c=document.getElementById('c'),g=c.getContext('2d');let n=0;
function f(){c.width=innerWidth;c.height=innerHeight;n++;g.fillStyle=`hsl(${n%360} 70% 50%)`;
g.fillRect(0,0,c.width,c.height);g.fillStyle='#fff';g.font='120px sans-serif';g.fillText(n,40,160);
document.title='Animated '+(n>>6);requestAnimationFrame(f)}f();</script>"""


def package():
    return pathlib.Path(os.environ.get('HYPRUNE_LIVE_TAKE_WORLD', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))


def prepare(installed):
    shutil.copytree(package(), installed / LUMEN, dirs_exist_ok=True)


def acceptance(client, children, args, token, socket):
    results = {'observe': OBSERVE, 'checks': {}, 'data': {}}
    out = s.STATE / 'live-take.json'

    def record(name, ok, data=None):
        results['checks'][name] = bool(ok)
        if data is not None:
            results['data'][name] = data
        out.write_text(json.dumps(results, indent=2) + '\n')
        print(('PASS' if ok else 'FAIL') + ' live-take:', name, json.dumps(data) if data is not None else '', flush=True)
        if not OBSERVE:
            assert ok, (name, data)

    def shot(name):
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(s.STATE / f'live-take-{name}.png')], check=True)

    def timings(phase):
        samples = []
        for _ in range(4):
            time.sleep(.5)
            t = s.camera().get('timings', {})
            samples.append({k: round(t.get(k) or 0, 2) for k in ('captureMs', 'frameMs', 'deliveredFPS')})
        results['data'].setdefault('timings', {})[phase] = samples
        print('TIMINGS', phase, json.dumps(samples), flush=True)

    def clients():
        return json.loads(s.ctl('clients', '-j'))

    def captures():
        return s.camera()['captures']

    def capture(title):
        return next((c for c in captures() if c['title'].startswith(title)), None)

    def launch(argv, title, timeout=30):
        before = {c['address'] for c in clients()}
        children.append(s.envrun(*argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        return wait(lambda: next((c for c in clients() if c['address'] not in before and c['title'].startswith(title)), None),
                    timeout=timeout)

    def move(window, ws):
        s.ctl('dispatch', f'hl.dsp.window.move({{workspace="{ws}",follow=false,window="address:{window["address"]}"}})')

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    manifest = json.loads((s.STATE / 'worlds' / LUMEN / 'world.json').read_text())
    areas = {a['id']: a for a in manifest['areas']}
    results['data']['world'] = manifest.get('version')

    page = pathlib.Path(tempfile.mkdtemp(prefix='live-take-', dir=s.STATE)) / 'animated.html'
    page.write_text(PAGE)
    monitors = json.loads(s.ctl('monitors', '-j'))
    other = next((m['name'] for m in monitors if m['name'] != 'HEADLESS-1'), None)
    results['data']['outputs'] = [m['name'] for m in monitors]

    # Staged on the desktop with the world inactive, as in the take: exec, then move each
    # window to its home with follow=false. The browser moves before its first paint.
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    s.ctl('dispatch', 'hl.dsp.focus({ workspace = "10" })')
    browser = None
    if CHROMIUM:
        profile = tempfile.mkdtemp(prefix='chromium-', dir=s.STATE)
        browser = launch([CHROMIUM, '--ozone-platform=wayland', f'--user-data-dir={profile}', '--no-first-run',
                          '--no-default-browser-check', '--password-store=basic', '--new-window', page.as_uri()],
                         '', timeout=60)
        time.sleep(.8)
        move(browser, 22)
    staged = {}
    for title, ws in [('Desk', 21), ('Kiosk', 21), ('Array', 22), ('Signal', 24)]:
        window = launch(['kitty', '--class', 'HypruneDemo', '--title', title], title)
        time.sleep(.8)
        move(window, ws)
        staged[title] = ws
    # An app on the other output, moved to a home that does not exist yet: that home
    # workspace is created on the other output, hidden there.
    electron = None
    if ELECTRON and other:
        s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{other}" }}))')
        s.ctl('dispatch', 'hl.dsp.focus({ workspace = "5" })')
        app = page.parent / 'electron'
        app.mkdir()
        (app / 'package.json').write_text(json.dumps({'name': 'live-take', 'main': 'main.js'}))
        (app / 'main.js').write_text("const {app,BrowserWindow}=require('electron');app.whenReady().then(()=>"
                                     f"new BrowserWindow({{width:1100,height:700}}).loadFile({json.dumps(str(page))}))")
        electron = launch([ELECTRON, '--ozone-platform=wayland', '--no-sandbox', str(app)], '', timeout=60)
        time.sleep(.8)
        move(electron, 23)
        s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    time.sleep(3)
    homes = {w['id']: w['monitor'] for w in json.loads(s.ctl('workspaces', '-j')) if w['id'] in (21, 22, 23, 24)}
    results['data']['homeOutputs'] = {str(k): v for k, v in homes.items()}

    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60)
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    position, yaw, pitch = PLAZA
    pose(client, position, yaw, pitch); time.sleep(1.5)
    timings('entered')

    # 1. Spawn from the plaza: in front of the player, on the floor it stands on.
    launch(['kitty', '--class', 'live-take', '--title', 'Spawned'], 'Spawned')
    spawned = wait(lambda: (lambda c: c if c and c['visible'] else None)(capture('Spawned')), timeout=20)
    eye = s.camera()['position']
    delta = [spawned['position'][i] - eye[i] for i in range(3)]
    forward = [math.sin(yaw), 0, -math.cos(yaw)]
    ahead = sum(delta[i] * forward[i] for i in range(3))
    data = {'position': spawned['position'], 'size': spawned['size'], 'eye': eye,
            'distance': round(math.dist(spawned['position'], eye), 2), 'ahead': round(ahead, 2)}
    shot('spawn')
    record('spawn lands in front of the player', data['distance'] < 6 and ahead > 1, data)
    lower = spawned['position'][1] - spawned['size'][1] / 2
    record('spawn rests at the player\'s height, not in mid-air', lower < eye[1] and spawned['position'][1] < eye[1] + 1.2,
           {'lowerEdge': round(lower, 2)})
    record('spawned window is collision-free', not spawned['collision'])
    timings('spawned')

    # 3. The staged windows on their homes.
    time.sleep(2)
    snapshot = state()
    instances = {i['surfaceId']: i for i in snapshot['mounts']['instances']}
    by_title = {c['title']: c for c in captures()}
    homes = {}
    for title in staged:
        c = by_title.get(title)
        homes[title] = {'capture': c and {k: c.get(k) for k in ('id', 'visible', 'position', 'workspaceId', 'wall')},
                        'instance': c and instances.get(c['id'])}
    tool = homes['Kiosk']['instance'] or {}
    record('second arrival takes the area\'s first tool anchor',
           tool.get('anchorId') == areas['sun-court']['toolAnchors'][0], homes)
    record('first arrival stays on the area wall',
           homes['Desk']['instance'] is None and (homes['Desk']['capture'] or {}).get('wall') == 'workspace-21')
    record('a lone arrival keeps the wall', homes['Signal']['instance'] is None and
           (homes['Signal']['capture'] or {}).get('wall') == 'workspace-24')
    leftovers = [c['title'] for c in captures() if not c['visible'] and not c.get('wall') and not c.get('queued')]
    record('no invisible placeholder surfaces', not leftovers, leftovers)
    floated = {c['title']: c['floating'] for c in clients() if c['title'] in staged}
    record('wall windows stay tiled, tool windows float', floated.get('Desk') is False and floated.get('Kiosk') is True,
           floated)
    timings('homes')
    # Evidence: the Sun Court wall (first arrival) and its first tool desk (second arrival).
    pose(client, [8, 1.7, -4], 0, .42); time.sleep(2); shot('sun-court-wall')
    pose(client, [-15, 1.7, 10.8], 0, -.18); time.sleep(2); shot('sun-court-desk')
    # A free room window moved onto a home arrives there too.
    move(next(c for c in clients() if c['title'] == 'Spawned'), 24)
    time.sleep(2)
    moved = capture('Spawned') or {}
    moved_instance = next((i for i in state()['mounts']['instances'] if i['surfaceId'] == moved.get('id')), {})
    record('room window moved onto a home takes its first tool anchor',
           moved_instance.get('anchorId') == areas['signal-rise']['toolAnchors'][0],
           {'capture': {k: moved.get(k) for k in ('position', 'workspaceId', 'wall', 'visible')}, 'instance': moved_instance})

    # 2. Browsers on hidden home workspaces keep presenting frames.
    for label, window, ws in [('chromium (same output, moved before first paint)', browser, 22),
                              ('electron (home workspace on another output)', electron, 23)]:
        if not window:
            results['data'][label] = 'not installed'
            continue
        def find():
            return next((c for c in captures() if c.get('workspaceId') == ws and c['title'].startswith('Animated')), None)
        try:
            found = wait(find, timeout=10)
        except AssertionError:
            found = None
        uncaptured = s.camera().get('uncaptured', [])
        if not found:
            record(f'{label} on a home wall is captured', False, {'uncaptured': uncaptured})
            continue
        first = found['commits']; samples = []
        for _ in range(6):
            time.sleep(.5)
            timings = s.camera().get('timings', {})
            samples.append({k: round(timings.get(k) or 0, 2) for k in ('captureMs', 'frameMs', 'deliveredFPS')})
        later = next(c for c in captures() if c['id'] == found['id'])['commits']
        record(f'{label} keeps committing frames on a hidden home workspace', later - first >= 30,
               {'commits': [first, later], 'uncaptured': uncaptured, 'timings': samples})
    if browser:
        hall = areas['array-hall']
        anchor = next(a for a in manifest['anchors'] if a['id'] == hall['primaryAnchor'])
        p = anchor['pose']['position']
        pose(client, [p[0], 1.7, p[2] + 14], 0, .28); time.sleep(2.5)
        shot('array-hall-wall')
    if electron:
        # The home created on the other output: its wall, seen from the area's arrival.
        vault = areas['rime-vault']
        spawn = next(sp for sp in manifest['spawns'] if sp['id'] == vault['spawnId'])['pose']['position']
        target = next(a for a in manifest['anchors'] if a['id'] == vault['primaryAnchor'])['pose']['position']
        eye = [spawn[0], spawn[1] + 1.7, spawn[2]]
        d = [target[i] - eye[i] for i in range(3)]
        pose(client, eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2]))); time.sleep(2.5)
        shot('rime-vault-wall')
    out.write_text(json.dumps(results, indent=2) + '\n')
    print('LIVE-TAKE', json.dumps(results['checks']), flush=True)
