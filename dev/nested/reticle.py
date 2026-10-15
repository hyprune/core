"""Reticle states, reach limits, player settings, view bob: dev/nested/run.sh --reticle.

Nested only (run.sh holds /tmp/worldshell-nested.lock); IPC 0.19, the m3b fixture world.
Every reticle state is reached the way a player would reach it and checked in the core
diagnostics and on the wire (state.interaction.reticle); each reach limit is lowered below
the aimed distance and the refused action checked (nothing changes, an out-of-range notice).
HYPRUNE_RETICLE_SHOTS=dir: crops of every style x state x HUD palette (contact sheets are
composed by dev/nested/reticle_sheet.py). HYPRUNE_RETICLE_VIDEO=file: a clip of the dynamic
and contextual behaviour (wf-recorder, native pixels, centre crop).
"""
import json, math, os, pathlib, signal, subprocess, time
import session as s
from m2 import wait, keys, pose

STYLES = ['cross_dot', 't_dot', 'circle_dot', 'dot', 'cross', 'chevron']
# The shell's HUD palettes (qml/theme/Tokens.qml: hudForeground, hudPositive, hudWarning).
PALETTES = {'adventure': ('#f6f5ea', '#59f299', '#ff9e59'), 'instrument': ('#f6f5ea', '#74eec4', '#ffb35c'),
            'minimal': ('#f6f5ea', '#9fe3ad', '#f2a777')}


def rgb(hexcolour):
    return [round(int(hexcolour[i:i + 2], 16) / 255, 4) for i in (1, 3, 5)]


def acceptance(client, children, args, token, socket):
    checks = []

    def check(name, value=True, evidence=None):
        assert value, (name, evidence)
        checks.append({'name': name, 'evidence': evidence}); print('PASS reticle:', name, flush=True)
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True); children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush(); assert device.stdout.readline().strip() == 'ok'; time.sleep(.1)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    def queue(): return state()['editor']['unplaced']
    def capture(sid): return next(v for v in s.camera()['captures'] if v['id'] == sid)
    def reticle(): return s.camera()['reticle']
    def player(patch): return client.call('player.set', {'patch': patch})

    def expect(name, st, action=None, timeout=4):
        try:
            r = wait(lambda: (lambda v: v if v['state'] == st and (action is None or v['action'] == action) else None)(reticle()), timeout=timeout)
        except AssertionError:
            raise AssertionError((name, 'reticle is', reticle()))
        wire = state()['interaction']['reticle']
        check(name, wire['state'] == st and (action is None or wire['action'] == action), {'diagnostics': r, 'wire': wire})
        return r

    def facing(sid, distance=3):
        v = capture(sid); x, y, z, w = v['rotation']; n = [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
        pose(client, [v['position'][i] + distance * n[i] for i in range(3)], math.atan2(-n[0], n[2]), math.asin(-n[1])); time.sleep(.3)

    def launch(title):
        children.append(s.envrun('kitty', '--class', 'reticle-' + title, '--title', title, '--override', 'font_size=16',
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        wait(lambda: any(w['title'] == title for w in json.loads(s.ctl('clients', '-j')))); time.sleep(.4)

    def place_ahead(remaining):
        # Place ahead (LMB) needs free space for the window's full size: try a few headings.
        for yaw in (0, math.pi, -math.pi / 2, math.pi / 4):
            pose(client, [0, 2.3, 4], yaw, 0); time.sleep(.3); pointer('button', 272); time.sleep(.5)
            if queue()['count'] == remaining:
                return
        raise AssertionError('no free space to place ahead')

    def notices(code):
        state()  # notifications are read from the socket during calls
        return [e for e in client.events if e.get('method') == 'runtime.notice' and e['params']['code'] == code]

    def held(mods, seconds):
        args = []
        for name, key in mods: args += ['-M', name, '-P', key]
        tail = []
        for name, key in reversed(mods): tail += ['-p', key, '-m', name]
        h = s.envrun('wtype', *args, '-s', str(int(seconds * 1000)), *tail); children.append(h); time.sleep(.25); return h
    logo = [('logo', 'Super_L')]
    def chord(): keys('-M', 'logo', '-P', 'Super_L', '-k', 'Tab', '-p', 'Super_L', '-m', 'logo'); time.sleep(.3)

    # Setup: the fixture world, two windows on the bound home workspace (they arrive unplaced).
    client.call('world.load', {'worldId': 'org.hyprune.m3b'})
    client.call('workspace.bind', {'id': 20, 'worldId': 'org.hyprune.m3b', 'areaId': 'sun-court'})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({monitor="HEADLESS-1"}))')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="20"}))')
    launch('Reticle-A'); launch('Reticle-B')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="91"}))')
    check('hidden before entering', reticle()['state'] == 'hidden')
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: queue()['count'] == 2)
    output = state()['runtime']['outputs'][0]
    lease = client.call('shell.claim', {'outputs': [output]})['leaseId']

    def palette(name):
        neutral, valid, invalid = PALETTES[name]
        return client.call('overlay.style', {'leaseId': lease, 'style': {'reticleColor': rgb(neutral), 'validColor': rgb(valid), 'invalidColor': rgb(invalid)}})['style']
    style = palette('adventure')
    check('overlay style carries the reticle colours (0.19)', style['validColor'] == rgb(PALETTES['adventure'][1]))

    # Player settings: defaults, live patch, persistence, reset.
    got = client.call('player.get', {})
    check('player defaults', got['config']['walk_speed'] == 4 and got['config']['run_speed'] == 10 and got['config']['view_bob'] == 'subtle'
          and got['config']['reach_grab'] == 15 and got['config']['reach_mount'] == 8 and got['config']['reach_placement'] == 12 and got['overrides'] == {}, got['config'])
    check('graphics fov default 65, accepts 120', client.call('graphics.get', {})['config']['fov'] == 65 and
          client.call('graphics.set', {'patch': {'fov': 120}})['config']['fov'] == 120)
    client.call('graphics.set', {'patch': {'fov': 65}})
    try:
        player({'reach_grab': 500}); rejected = False
    except RuntimeError:
        rejected = True
    check('out-of-range player setting rejected', rejected)
    saved = player({'walk_speed': 2.5, 'reticle_style': 'circle_dot'})
    on_disk = json.loads((s.STATE / 'player.json').read_text())
    check('player.set applies live and persists overrides only', saved['config']['walk_speed'] == 2.5 and on_disk == {'walk_speed': 2.5, 'reticle_style': 'circle_dot'}, on_disk)
    reset = player({'walk_speed': None, 'reticle_style': None})
    check('null resets to the default', reset['config']['walk_speed'] == 4 and reset['overrides'] == {} and json.loads((s.STATE / 'player.json').read_text()) == {})

    # Tool 5 with a window to place: a free side wall in reach is valid; out of reach is refused.
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    keys('-k', '5'); wall = ([0, 2.3, 0], math.pi / 2, 0); pose(client, *wall)
    r = expect('tool 5 on a free wall in reach: valid place', 'valid', 'place')
    check('placement reach reported', r['reach'] == 12 and 0 < r['distance'] < 12, r)
    player({'reach_placement': max(1, math.floor(r['distance']) - 1)})
    r = expect('beyond placement reach: out of range', 'out_of_range', 'place')
    too_far = lambda d: f"Too far: {d:.1f} m" if d < 10 else f"Too far: {d:.0f} m"
    check('out of range: "Too far" label with the distance', r['hint'] == too_far(r['distance']), r)
    check('out of range draws in the warning colour', all(abs(a - b) < .01 for a, b in zip(r['color'], rgb(PALETTES['adventure'][2]))), r)
    before = queue()['count']; n = len(notices('placement.out_of_range'))
    pointer('button', 273); time.sleep(.6)
    check('surface placement beyond reach is refused', queue()['count'] == before and len(notices('placement.out_of_range')) == n + 1,
          notices('placement.out_of_range')[-1:])
    player({'reach_placement': None})
    expect('reach restored: valid again', 'valid', 'place')
    first = queue()['selectedId']; pointer('button', 273); wait(lambda: queue()['count'] == before - 1)
    check('surface placement in reach works', capture(first)['visible'])
    facing(first, 3)
    expect('aiming at the placed window: no room, invalid', 'invalid', 'place')
    place_ahead(0)
    second = next(c['id'] for c in s.camera()['captures'] if c['title'] == 'Reticle-B')
    # Nothing to place: tool 5 pockets the aimed window, within placement reach.
    facing(first, 3)
    expect('tool 5, empty queue, window in reach: valid pocket', 'valid', 'pocket')
    player({'reach_placement': 2})
    expect('pocket beyond reach: out of range', 'out_of_range', 'pocket')
    n = len(notices('placement.out_of_range')); pointer('button', 274); time.sleep(.6)
    check('pocket beyond reach is refused', queue()['count'] == 0 and capture(first)['visible'] and len(notices('placement.out_of_range')) == n + 1)
    player({'reach_placement': None})

    # Tool 2 mount: select a window, then a wall in reach (mount) / out of reach.
    keys('-k', '2'); facing(first, 3)
    expect('tool 2 on a window: valid select', 'valid', 'select')
    pointer('button', 272); wait(lambda: state()['interaction']['taggedSurfaceId'] == first)
    # A wall clear of the placed windows (their size depends on the output resolution).
    beside = None
    for k in range(25):
        candidate = ([0, 2.3, 0], math.pi / 2 + .6 + .25 * k, 0); pose(client, *candidate); time.sleep(.25)
        if reticle()['action'] == 'mount':
            beside = candidate; break
    check('found a clear wall for mounting', beside is not None, beside)
    expect('tool 2 with a tagged window on a wall in reach: valid mount', 'valid', 'mount')
    player({'reach_placement': 1})
    expect('mount on a wall beyond reach: out of range', 'out_of_range', 'mount')
    old = capture(first)['position']; n = len(notices('placement.out_of_range')); pointer('button', 272); time.sleep(.6)
    check('wall mount beyond reach is refused', capture(first)['position'] == old and len(notices('placement.out_of_range')) == n + 1,
          {'from': old, 'to': capture(first)['position'], 'notices': len(notices('placement.out_of_range')) - n, 'reticle': reticle(),
           'aim': s.camera()['aim'], 'tagged': state()['interaction']['taggedSurfaceId'], 'first': first, 'tool': state()['editor']['tool']})
    player({'reach_placement': None})
    keys('-k', '3'); pose(client, *beside)
    expect('tool 3 on a wall in reach: valid path point', 'valid', 'path')

    # Tool 1: a window takes the click (target); the sky is neutral.
    keys('-k', '1'); facing(second, 3)
    expect('tool 1 on a window: target click', 'target', 'click')
    pose(client, [0, 2.3, 4], 0, 1.3)
    expect('tool 1 on the sky: neutral', 'neutral')

    # Super: grab in reach (valid) / out of reach (refused), then carrying (holding).
    facing(second, 3)
    h = held(logo, 2.5); wait(lambda: s.camera()['superHeld'])
    r = expect('Super on a window in reach: valid grab', 'valid', 'grab')
    check('grab reach reported', r['reach'] == 15, r)
    h.wait(timeout=5)
    player({'reach_grab': 2})
    h = held(logo, 2.5); wait(lambda: s.camera()['superHeld'])
    expect('Super on a window beyond grab reach: out of range', 'out_of_range', 'grab')
    n = len(notices('interaction.out_of_range'))
    pointer('button', 272, 1); time.sleep(.4)
    check('Super+LMB beyond grab reach does not grab', s.camera()['gesture'] == 0)
    check('the refused grab shows the out-of-range reticle', reticle()['state'] == 'out_of_range' and reticle()['action'] == 'grab', reticle())
    pointer('button', 272, 0); h.wait(timeout=5)
    check('a refused grab says "Too far"', len(notices('interaction.out_of_range')) == n + 1 and 'Too far' in notices('interaction.out_of_range')[-1]['params']['message'],
          notices('interaction.out_of_range')[-1:])
    player({'reach_grab': None})
    h = held(logo, 1.5); wait(lambda: s.camera()['superHeld'])
    pointer('button', 272, 1); wait(lambda: s.camera()['gesture'] == 1); h.wait(timeout=5)
    pose(client, [0, 2.3, 4], 0, 1.2); time.sleep(.3)
    expect('carrying, release leaves it in place: holding', 'holding')
    pointer('button', 272, 0); wait(lambda: s.camera()['gesture'] == 0)

    # Typing and the free cursor.
    facing(second, 3); keys('-k', 'e')
    expect('typing mode (E use): typing', 'typing')
    check('in use, the window\'s "E USE / F FOCUS" prompt is hidden', not s.camera()['overlays']['interactionHints'], s.camera()['overlays'])
    chord(); wait(lambda: s.camera()['mode'] != 'typing'); expect('typing off (exit): back to target', 'target')
    check('out of use, the prompt is back', s.camera()['overlays']['interactionHints'], s.camera()['overlays'])
    # E beyond the use distance (6 m): refused with "Too far" and the out-of-range flash.
    facing(second, 9); time.sleep(.3); n = len(notices('interaction.out_of_range')); keys('-k', 'e'); time.sleep(.4)
    check('E beyond the use distance: refused, "Too far", out-of-range flash', s.camera()['mode'] != 'typing' and
          len(notices('interaction.out_of_range')) == n + 1 and reticle()['state'] == 'out_of_range' and reticle()['action'] == 'use',
          {'reticle': reticle(), 'notice': notices('interaction.out_of_range')[-1:]})
    time.sleep(1.4); facing(second, 3)
    chord(); expect('free cursor: hidden', 'hidden')
    chord(); expect('back from the free cursor', 'target')

    # Dynamic spread: opens while moving, settles when still; off by setting and reduced motion.
    def spread_while(action):
        samples = []
        proc = action()
        for _ in range(12):
            samples.append(reticle()['spread']); time.sleep(.08)
        proc.wait(timeout=5); time.sleep(1.2)
        return max(samples), reticle()['spread']
    walk_w = lambda: s.envrun('wtype', '-P', 'w', '-s', '1400', '-p', 'w')
    pose(client, [0, 2.3, 4], 0, 0); time.sleep(.5)
    moving, still = spread_while(walk_w)
    check('dynamic reticle opens while moving and settles', moving > 1.5 and still < .2, {'moving': moving, 'still': still})
    player({'reticle_dynamic': False})
    moving, _ = spread_while(walk_w)
    check('dynamic off: constant reticle', moving == 0, moving)
    player({'reticle_dynamic': None}); client.call('graphics.set', {'patch': {'reduced_motion': True}})
    moving, _ = spread_while(walk_w)
    check('reduced motion: constant reticle', moving == 0, moving)
    client.call('graphics.set', {'patch': {'reduced_motion': False}})

    # Walking speed and the view bob (walk mode on the fixture floor).
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    def walk_sample(mods=(), seconds=1.6):
        pose(client, [0, 1.7, 6], math.pi, 0); time.sleep(.8)
        args = []
        for k in mods: args += ['-P', k]
        args += ['-P', 'w', '-s', str(int(seconds * 1000)), '-p', 'w']
        for k in reversed(mods): args += ['-p', k]
        proc = s.envrun('wtype', *args); bob, speed = [], []
        while proc.poll() is None:
            d = s.camera(); bob.append(d['viewBob'][1]); speed.append(d['motion']['speed']); time.sleep(.03)
        time.sleep(1.6); rest = s.camera()['viewBob']
        return {'minBob': min(bob), 'maxBob': max(bob), 'peakSpeed': max(speed), 'rest': rest}
    w = walk_sample()
    check('walk speed about 4 m/s', 3.3 < w['peakSpeed'] < 4.5, w)
    check('subtle bob while walking, never above eye height, settles', -0.0105 < w['minBob'] < -0.004 and w['maxBob'] <= 0 and w['rest'] == [0, 0, 0], w)
    r = walk_sample(('Shift_L',))
    check('run speed about 10 m/s (or a wall first)', r['peakSpeed'] > 6, r)
    check('running bobs less than walking', r['minBob'] > w['minBob'], {'walk': w['minBob'], 'run': r['minBob']})
    player({'walk_speed': 2.0})
    slow = walk_sample()
    check('walk speed applies live', 1.6 < slow['peakSpeed'] < 2.3, slow)
    player({'walk_speed': None, 'view_bob': 'off'})
    off = walk_sample()
    check('view bob off', off['minBob'] == 0 and off['maxBob'] == 0, off)
    player({'view_bob': 'normal'}); client.call('graphics.set', {'patch': {'reduced_motion': True}})
    reduced = walk_sample()
    check('reduced motion turns the bob off', reduced['minBob'] == 0, reduced)
    client.call('graphics.set', {'patch': {'reduced_motion': False}})
    normal = walk_sample()
    check('normal bob is larger than subtle', normal['minBob'] < w['minBob'], {'normal': normal['minBob'], 'subtle': w['minBob']})
    player({'view_bob': None}); client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    fly = walk_sample()
    check('no bob in flight', fly['minBob'] == 0, fly)

    if os.environ.get('HYPRUNE_RETICLE_SHOTS'):
        shots(client, children, pointer, player, palette, capture, queue, held, logo, launch, facing, wall, first, second, expect, place_ahead)
    if os.environ.get('HYPRUNE_RETICLE_VIDEO'):
        video(client, children, pointer, player, capture, queue, facing, first, second, held, logo, expect, place_ahead)
    (s.ROOT / 'docs/evidence/reticle.json').write_text(json.dumps({'nestedOnly': True, 'protocol': '0.19', 'checks': checks}, indent=2) + '\n')
    print('PASS reticle states, reach limits, player settings:', len(checks), 'checks', flush=True)


def shots(client, children, pointer, player, palette, capture, queue, held, logo, launch, facing, wall, first, second, expect, place_ahead):
    """Every style x state x HUD palette, cropped around the reticle at native pixels."""
    from PIL import Image
    out = pathlib.Path(os.environ['HYPRUNE_RETICLE_SHOTS']); out.mkdir(parents=True, exist_ok=True)
    full = s.STATE / 'reticle-full.png'

    def snap(state_name):
        for pal in PALETTES:
            palette(pal)
            for st in STYLES:
                player({'reticle_style': st}); time.sleep(.25)
                subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(full)], check=True)
                img = Image.open(full).convert('RGB'); cx, cy = img.width // 2, img.height // 2
                img.crop((cx - 160, cy - 80, cx + 160, cy + 120)).save(out / f'{pal}-{state_name}-{st}.png')
        print('SHOT reticle', state_name, flush=True)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    keys('-k', '1'); pose(client, [0, 2.3, 4], 0, 1.3); expect('shots: neutral', 'neutral'); snap('neutral')
    facing(second, 3); expect('shots: target', 'target'); snap('target')
    keys('-k', '5'); facing(second, 3); expect('shots: pocket valid', 'valid', 'pocket'); snap('valid')
    player({'reach_placement': 2}); expect('shots: out of range', 'out_of_range'); snap('out_of_range'); player({'reach_placement': None})
    # Invalid: a locked-in-place target is not available here; an occupied spot with a window to place is.
    # The verdict tests the wall behind the aimed window (windows are not collision), so pocket the
    # second window (carried and released in mid-air above, often with no wall behind it) and aim at
    # the first, which is placed on a wall.
    keys('-k', '5'); facing(second, 3); pointer('button', 274); wait(lambda: queue()['count'] == 1)
    facing(first, 3); expect('shots: invalid place', 'invalid', 'place'); snap('invalid')
    place_ahead(0)
    keys('-k', '1'); facing(second, 3)
    h = held(logo, 1.5); wait(lambda: s.camera()['superHeld']); pointer('button', 272, 1); wait(lambda: s.camera()['gesture'] == 1); h.wait(timeout=5)
    pose(client, [0, 2.3, 4], 0, 1.2); expect('shots: holding', 'holding'); snap('holding')
    pointer('button', 272, 0); wait(lambda: s.camera()['gesture'] == 0)
    facing(second, 3); keys('-k', 'e'); expect('shots: typing', 'typing'); snap('typing'); keys('-M', 'logo', '-P', 'Super_L', '-k', 'Tab', '-p', 'Super_L', '-m', 'logo')
    player({'reticle_style': None}); palette('adventure')


def video(client, children, pointer, player, capture, queue, facing, first, second, held, logo, expect, place_ahead):
    """The dynamic and contextual reticle at native pixels (centre 1920x1080 crop of the 4K
    output), larger than the default so it reads in a small clip. <video>.json lists the
    caption timeline (seconds from the start of the recording)."""
    import threading
    path = pathlib.Path(os.environ['HYPRUNE_RETICLE_VIDEO']); events = []
    player({'reticle_size': 32, 'reticle_thickness': 3, 'reticle_gap': 6})
    client.call('movement.set', {'modeId': 'com.hyprune.fly'}); keys('-k', '1')
    view = ([0, 2.3, 7], .35, -.05); pose(client, *view); time.sleep(1)
    rec = s.envrun('wf-recorder', '-o', 'HEADLESS-1', '-r', '60', '-F', 'crop=1920:1080', '-c', 'libx264', '-p', 'preset=ultrafast', '-p', 'crf=14',
                   '-f', str(path), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    children.append(rec); time.sleep(1.0); t0 = time.time()

    def hold(seconds):
        # Render on demand presents nothing while still: nudge the view invisibly.
        d = s.camera(); x, y, z = d['position']; yaw, pitch = d['yaw'], d['pitch']; end = time.time() + seconds; n = 0
        while time.time() < end:
            s.ctl('eval', f'hl.plugin.hyprune.camera({x},{y},{z},{yaw + 1e-5 * (n % 2)},{pitch})'); n += 1; time.sleep(.03)

    def caption(text, seconds, action=None):
        begin = time.time() - t0
        if action:
            threading.Thread(target=action).start()
        hold(seconds) if not action else time.sleep(seconds)
        events.append({'start': round(begin, 2), 'end': round(time.time() - t0, 2), 'text': text})
    caption('Still: neutral', 1.6)
    caption('Moving: the reticle opens', 1.4, lambda: s.envrun('wtype', '-P', 'w', '-s', '1200', '-p', 'w').wait())
    caption('Still again: it settles', 1.4)
    caption('Turning: it opens', 1.2, lambda: pointer('stream', 4, 0, 500, 1000))
    facing(second, 5); caption('A window takes the click: target', 1.8)
    h = held(logo, 2.4); wait(lambda: s.camera()['superHeld']); caption('Super: grab in reach (valid)', 1.8); h.wait(timeout=5)
    player({'reach_grab': 3}); h = held(logo, 2.4); wait(lambda: s.camera()['superHeld'])
    caption('Beyond reach: out of range, with the distance', 1.8); h.wait(timeout=5); player({'reach_grab': None})
    keys('-k', '5'); caption('Tool 5: pocket this window (valid)', 1.6)
    pointer('button', 274); wait(lambda: queue()['count'] == 1); facing(first, 4)
    caption('Placing onto an occupied spot: invalid', 1.8)
    pose(client, [0, 2.3, 0], math.pi / 2 + .6, 0); time.sleep(.3)
    caption('A free wall in reach: valid', 1.6)
    player({'reach_placement': 1}); caption('Out of placement reach', 1.6); player({'reach_placement': None})
    place_ahead(0); keys('-k', '1'); facing(second, 5)
    h = held(logo, 1.5); wait(lambda: s.camera()['superHeld']); pointer('button', 272, 1); wait(lambda: s.camera()['gesture'] == 1); h.wait(timeout=5)
    pose(client, [0, 2.3, 4], 0, 1.1); time.sleep(.2); caption('Carrying: holding', 1.6)
    pointer('button', 272, 0); wait(lambda: s.camera()['gesture'] == 0)
    pose(client, *view); caption('Settings: style, size, colour, reach', .8)
    rec.send_signal(signal.SIGINT)
    for n in range(1200):
        if rec.poll() is not None:
            break
        s.ctl('eval', f'hl.plugin.hyprune.camera(0,2.3,7,{.35 + 1e-4 * (n % 2)},-.05)'); time.sleep(.1)
    path.with_suffix('.json').write_text(json.dumps(events, indent=2) + '\n')
    player({'reticle_size': None, 'reticle_thickness': None, 'reticle_gap': None})
    print('PASS reticle video', path, flush=True)
