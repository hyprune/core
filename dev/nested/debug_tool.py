"""Tool 9, the debug tool: dev/nested/run.sh --debug-tool.

Nested only (isolated harness on headless hosts). Lumen Reach (HYPRUNE_DEBUG_WORLD, else the
worlds checkout's dist package) with a placed window. Selects tool 9 with its key, steps the
wheel through every layer (and wraps), takes a still of each from a view that shows it,
checks the banner, legend, line width and that a still view renders no frames, aims at the
Sun Court window zone and gets the info card naming it, closes the card, and leaves the tool.
IPC 0.26 publishes it (editor.tool 9, the layer as editor.submode, editor.debugTool with the
legend and card, the slot-9 tool definition, the tool9 action); a 0.25 peer sees the normal
tool, 5 definitions and no tool9 action. Stills go to
<state>/debug-tool/ and, with DEBUG_TOOL_STILLS=<dir>, are copied there.
"""
import json, math, os, pathlib, secrets, shutil, subprocess, time
import session as s
from m2 import wait, keys

WORLD = 'com.hyprune.lumen-reach'
OLD_PEER = secrets.token_hex(32)  # the 0.25 peer
ZONE = 'sun-court-open-work'
# A view per layer that shows it: eye, target.
VIEWS = {
    'windows': ([8, 5, 4], [8, 7, -23]),
    'window-zones': ([12, 6.5, 17], [2.5, 2.5, 4]),
    'collision': ([4, 4.5, 6], [-6, .6, -6]),
    'playable': ([40, 70, 45], [10, 20, -40]),
    'paths': ([6, 9, 9], [8, 0, -6]),
    'keep-clear': ([8, 6, 2], [8, 1.5, -20]),
    'seats': ([20, 2.8, -17.5], [20, .4, -12.8]),
    'visibility': ([0, 1.7, 5], [-2, 2.5, -30]),
    'steering': None,  # looks at the first character
    'placement': ([14, 6, 16], [4, 1, -6]),
}


def grants():
    return [{'token': OLD_PEER, 'client': 'com.example.peer-025', 'capabilities': ['state.read', 'input.control']}]


def prepare(installed):
    source = os.environ.get('HYPRUNE_DEBUG_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD))
    manifest = pathlib.Path(source) / 'world.json'
    if not manifest.is_file():
        raise SystemExit(f'--debug-tool needs a Lumen Reach package; there is none at {source}. '
                         'Set HYPRUNE_DEBUG_WORLD to one.')
    if not any(z.get('id') == ZONE for z in json.loads(manifest.read_text()).get('placementZones', [])):
        raise SystemExit(f'--debug-tool needs Lumen Reach with the window zone {ZONE}')
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    (s.STATE / 'editor.json').unlink(missing_ok=True)
    (s.STATE / 'layout.json').unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    results = []
    old = s.Client(str(socketpath), OLD_PEER, '0.25', 'com.example.peer-025')  # launch tokens expire in 30 s
    out = s.STATE / 'debug-tool'
    out.mkdir(exist_ok=True)

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS debug-tool:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)

    for _ in range(4):
        xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    def diag():
        return s.camera()

    def tool():
        return diag()['debugTool']

    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})')
        time.sleep(.3)

    def look(pos, target):
        d = [target[i] - pos[i] for i in range(3)]
        camera(pos, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def shot(name):
        path = out / name
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        return path

    def frames():
        return diag()['timings']['renderedFrames']

    # World, a placed window (tool 5 places it ahead from the arrival spawn).
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({monitor="HEADLESS-1"}))')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="20"}))')
    title = 'Debug tool window'
    children.append(s.envrun('kitty', '--class', 'hyprune-debug-tool', '--title', title, '--override', 'font_size=16',
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    wait(lambda: any(w['title'] == title for w in json.loads(s.ctl('clients', '-j'))))
    time.sleep(.5)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="91"}))')
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active'] and not s.camera().get('loading'), 90); time.sleep(1)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    wait(lambda: state()['editor']['unplaced']['count'] >= 1, 10)
    time.sleep(.5)
    pointer('button', 272); time.sleep(.6)
    wait(lambda: state()['editor']['unplaced']['count'] == 0, 10)
    window = next(c for c in diag()['captures'] if c['title'] == title)
    check('window placed in the world', window['visible'], window['position'])
    # The windows layer looks at the placed window from 3 m in front, a little to the side.
    x, y, z, w_ = window['rotation']
    n = [2 * (x * z + y * w_), 2 * (y * z - x * w_), 1 - 2 * (x * x + y * y)]
    p = window['position']
    VIEWS['windows'] = ([p[0] + 3.2 * n[0] + 1.2 * n[2], p[1] + 1.2, p[2] + 3.2 * n[2] - 1.2 * n[0]], p)

    # Tool 9 by its key.
    check('the debug tool is hidden in other tools', not tool()['shown'] and tool()['strokes'] == 0)
    keys('-k', '9'); time.sleep(.4)
    wait(lambda: tool()['shown'], 5)
    t = tool()
    check('9 selects the debug tool on its first layer', diag()['tool'] == 9 and t['layer'] == 'windows' and t['index'] == 0,
          {'tool': diag()['tool'], 'layer': t['layer']})
    check('banner names the layer, legend lists its colours',
          t['banner'] == 'DEBUG 1/10  Window boxes & mounts' and t['legend'][:2] == ['window', 'workspace wall'], t['banner'])
    check('lines are 3 px at 1080p', abs(t['lineWidth'] - 3) < 1e-3, t['lineWidth'])
    st = state()
    check('IPC 0.26: tool 9 with the layer as submode, the slot-9 definition and the tool9 action',
          st['editor']['tool'] == 9 and st['editor']['submode'] == 'windows' and st['interaction']['tool'] == 9 and
          st['editor']['toolSlots']['primary'] == {'action': 'debug.inspect', 'label': 'Inspect'} and
          [d['slot'] for d in st['editor']['toolDefinitions']] == [1, 2, 3, 4, 5, 9] and
          st['editor']['toolDefinitions'][-1]['slots'] == st['editor']['toolSlots'] and
          'tool9' in st['keymap']['config']['modes']['world'],
          {'tool': st['editor']['tool'], 'submode': st['editor']['submode'], 'definition': st['editor']['toolDefinitions'][-1]})
    def published(st, t):
        d = st['editor']['debugTool']
        return (d is not None and d['layer'] == st['editor']['submode'] == t['layer'] and d['label'] == t['label'] and
                d['index'] == t['index'] and d['count'] == t['count'] and [l['label'] for l in d['legend']] == t['legend'][:8] and
                all(len(l['colour']) == 3 and all(0 <= c <= 1 for c in l['colour']) for l in d['legend']))
    check('IPC 0.26 editor.debugTool matches the drawn layer (label, index of count, legend)', published(st, t) and st['editor']['debugTool']['card'] is None,
          st['editor']['debugTool'])
    osub = old.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    ost = old.call('state.snapshot', {'subscriptionId': osub})['snapshot']['state']
    check('IPC 0.25 peers see the normal tool, 5 definitions and the debug slots',
          ost['editor']['tool'] == 1 and ost['editor']['submode'] == 'normal' and ost['interaction']['tool'] == 1 and
          ost['editor']['toolSlots']['primary'] == {'action': 'debug.inspect', 'label': 'Inspect'} and
          len(ost['editor']['toolDefinitions']) == 5 and 'debugTool' not in ost['editor'],
          {'tool': ost['editor']['tool'], 'slots': ost['editor']['toolSlots']})
    check('IPC 0.25 peers see no tool9 action', 'tool9' not in json.dumps(ost['keymap']))

    # Every layer: wheel steps through them; a still of each.
    seen = []
    for index in range(10):
        layer = tool()['layer']
        seen.append(layer)
        if layer == 'steering':
            npcs = [n for n in diag()['npcs'].get('agents', []) if n.get('position')]
            target = npcs[0]['position'] if npcs else [8, 0, -6]
            look([target[0] + 4, target[1] + 3.5, target[2] + 4], target)
        else:
            look(*VIEWS[layer])
        time.sleep(.6)
        t = tool()
        check(f'layer {index + 1} {layer} draws', t['items'] > 0 and t['strokes'] > 0,
              {'items': t['items'], 'strokes': t['strokes'], 'fills': t['fills'], 'buildMs': round(t['buildMs'], 2)})
        check(f'layer {index + 1} {layer} is published (editor.submode, editor.debugTool)', published(state(), t), state()['editor']['debugTool'])
        shot(f'debug-tool-{index + 1:02d}-{layer}.png')
        before = tool()['index']
        pointer('scroll', -1); time.sleep(.35)  # wheel up: the next layer (as in every tool)
        wait(lambda: tool()['index'] != before, 5)
    check('the wheel visits every layer once, in order, and wraps',
          seen == ['windows', 'window-zones', 'collision', 'playable', 'paths', 'keep-clear', 'seats', 'visibility', 'steering',
                   'placement'] and tool()['layer'] == seen[0], seen)
    pointer('scroll', 1); time.sleep(.35)
    check('the wheel goes back', tool()['layer'] == seen[-1], tool()['layer'])

    # The Sun Court window zone: aim at it from outside and inspect.
    while tool()['layer'] != 'window-zones':
        pointer('scroll', -1); time.sleep(.3)
    camera([2.5, 2.5, 14], 0, 0); time.sleep(.6)
    # A still view of a static layer renders nothing new.
    # (Characters and world animations held still: they force frames of their own.)
    s.ctl('eval', 'hl.plugin.hyprune.config({npc = {motion = "off"}, graphics = {reduced_motion = true}})'); time.sleep(1)
    first = frames(); time.sleep(1.5)
    check('a still view renders no frames', frames() == first, {'before': first, 'after': frames()})
    s.ctl('eval', 'hl.plugin.hyprune.config({npc = {motion = "auto"}, graphics = {reduced_motion = false}})'); time.sleep(.3)
    pointer('button', 272); time.sleep(.1); pointer('button', 272, 0); time.sleep(.5)
    t = tool()
    card = t['card']
    check('Primary on the window zone shows its card', card and card[0] == ZONE and t['selected'].startswith(ZONE), card)
    check('the card says what it is and why it blocks',
          any('Window zone' in l for l in card) and any('Inside a window zone (rule zone)' in l for l in card) and
          any('Owner: sun-court' in l for l in card), card)
    wire = state()['editor']['debugTool']['card']
    check('IPC 0.26 editor.debugTool.card carries the card', wire == [l[:256] for l in card if l][:8], wire)
    shot('debug-tool-card-window-zone.png')
    pointer('button', 273); time.sleep(.1); pointer('button', 273, 0); time.sleep(.4)
    check('Secondary closes the card', not tool()['card'] and state()['editor']['debugTool']['card'] is None)
    # Placement rules: the probe on an open floor, and the zone's rule from inside the layer.
    while tool()['layer'] != 'placement':
        pointer('scroll', -1); time.sleep(.3)
    look([10, 3, 16], [12, 0, 10]); time.sleep(.5)
    pointer('button', 272); time.sleep(.1); pointer('button', 272, 0); time.sleep(.5)
    card = tool()['card']
    check('the placement layer answers for the aimed spot', card and (card[0] == 'Placement probe' or 'Blocks placement' in ' '.join(card)), card)
    shot('debug-tool-card-placement.png')

    # Leaving the tool takes everything away.
    keys('-k', '1'); time.sleep(.5)
    t = tool()
    check('another tool hides the layer, banner and card', not t['shown'] and t['strokes'] == 0 and not t['banner'] and not t['card'], t)
    st = state()
    check('IPC 0.26: editor.debugTool is null in another tool', st['editor']['tool'] == 1 and st['editor']['debugTool'] is None,
          {'tool': st['editor']['tool'], 'debugTool': st['editor']['debugTool']})
    old.close()
    stills = os.environ.get('DEBUG_TOOL_STILLS')
    if stills:
        pathlib.Path(stills).mkdir(parents=True, exist_ok=True)
        for p in sorted(out.glob('*.png')):
            shutil.copy2(p, pathlib.Path(stills) / p.name)
    (out / 'results.json').write_text(json.dumps(results, indent=2))
    print(f'PASS debug-tool: {len(results)} checks', flush=True)
