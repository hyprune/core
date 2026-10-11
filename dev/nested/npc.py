"""NPC prototype (RFC-0010 draft): dev/nested/run.sh --npc.

Nested only (run.sh holds /tmp/worldshell-nested.lock). Switchyard with a published
resident from ../worlds/characters (HYPRUNE_NPC_CHARACTER: another file; without either,
the generated mannequin, tests/fixtures/make-mannequin.py) walking a route through the
hall (HYPRUNE_NPC_PROTOTYPE). Checks: skinned character drawn, walks the route at the
walk clip's speed, turns and idles at points, yields to the player, pauses (and stops
costing frames) when typing / in focus / idle / free cursor / reduced motion / on battery
in `auto`, still and off. HYPRUNE_NPC_MEASURE=1: GPU and idle cost (continuous render at
a fixed scale). HYPRUNE_NPC_SHOTS=dir: stills; HYPRUNE_NPC_VIDEO=file: a clip.
"""
import json, math, os, pathlib, shutil, signal, statistics, subprocess, sys, time
import session as s
from m2 import wait, keys, pose

SWITCHYARD = 'com.hyprune.switchyard'
ROUTE = [[-6.5, 0, 7], [-6.5, 0, -3], [5, 0, -3], [5, 0, 7]]  # clear of the ring platform and furniture
RESIDENT = 'resident-crimson.glb'  # worlds/characters (CC-BY-4.0, Hyprune contributors)
CHARACTER = 'make-mannequin.py placeholder'


def prepare(installed):
    shutil.copytree(s.ROOT.parent / 'worlds/dist' / SWITCHYARD, installed / SWITCHYARD, dirs_exist_ok=True)
    global CHARACTER
    glb, resident = s.STATE / 'mannequin.glb', s.ROOT.parent / 'worlds/characters' / RESIDENT
    if os.environ.get('HYPRUNE_NPC_CHARACTER'):  # e.g. a tools/npc-import output
        glb = pathlib.Path(os.environ['HYPRUNE_NPC_CHARACTER'])
    elif resident.exists():
        glb = resident
    else:
        subprocess.run([sys.executable, str(s.ROOT / 'tests/fixtures/make-mannequin.py'), str(glb)], check=True, stdout=subprocess.DEVNULL)
    if glb.name != 'mannequin.glb':
        CHARACTER = glb.stem
    print('npc character:', CHARACTER, flush=True)
    spec = {'worldId': SWITCHYARD, 'character': str(glb), 'agents': [{'route': ROUTE, 'idleSeconds': 2.5, 'accent': [.95, .55, .15]}]}
    (s.STATE / 'npc-prototype.json').write_text(json.dumps(spec))
    os.environ['HYPRUNE_NPC_PROTOTYPE'] = str(s.STATE / 'npc-prototype.json')
    if os.environ.get('HYPRUNE_NPC_MEASURE') == '1':
        os.environ['HYPRUNE_CONTINUOUS_RENDER'] = '1'  # steady GPU clocks: NPC on vs off at the same cadence


def acceptance(client, children, args, token, socket):
    checks = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        checks.append({'name': name, 'evidence': data}); print('PASS npc:', name, flush=True)
    def npc(): return s.camera()['npcs']
    def agent(): return npc()['agents'][0]
    def lua(cfg): s.ctl('eval', f'hl.plugin.hyprune.config({cfg})')
    def shot(name):
        if os.environ.get('HYPRUNE_NPC_SHOTS'):
            out = pathlib.Path(os.environ['HYPRUNE_NPC_SHOTS']); out.mkdir(parents=True, exist_ok=True)
            subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / f'{name}.png')], check=True)
    def look_at_npc(distance=5, height=1.6):
        a = agent(); p = a['position']
        eye = [p[0] + 2, height, p[2] + distance]
        d = [p[0] - eye[0], 1.0 - eye[1], p[2] - eye[2]]
        pose(client, eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2]))); time.sleep(.3)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': SWITCHYARD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60); time.sleep(1)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    check('the prototype character is loaded', npc()['state'] != 'off' and len(npc()['agents']) == 1, npc())
    lua('{npc={motion="always"}}'); time.sleep(.5)
    look_at_npc(); time.sleep(.5)
    check('animating and drawn in view', npc()['state'] == 'animating' and npc()['drawn'] == 1 and npc()['forcesFrames'], npc())
    shot('1-walking')
    a0 = agent(); time.sleep(2.0); a1 = agent()
    moved = math.dist(a0['position'], a1['position'])
    expected = 2.0 * a0['walkSpeed']  # the character's walk clip speed (extras.speed)
    check('walks the route at about the walk clip speed', .7 * expected < moved < 1.3 * expected if a0['clip'] == 'walk' and a1['clip'] == 'walk' else moved > .2,
          {'from': a0, 'to': a1, 'metres': moved, 'expected': expected})
    print('agent', json.dumps(agent()), flush=True)
    if os.environ.get('HYPRUNE_NPC_SURVEY'):
        for name, eye, yaw, pitch in (('survey-top', [0, 26, 0], 0, -1.45),):
            pose(client, eye, yaw, pitch); time.sleep(.8); shot(name)
    if os.environ.get('HYPRUNE_NPC_MEASURE') == '1':
        return measure(client, npc, agent, look_at_npc, lua, check, checks)
    if os.environ.get('HYPRUNE_NPC_VIDEO'):
        return video(client, npc, agent, look_at_npc, lua)
    # The turn at a route corner: idle, then the heading swings ~90 degrees.
    corner = wait(lambda: agent() if agent()['clip'] == 'idle' else None, timeout=20)
    h0 = corner['heading']; wait(lambda: agent()['clip'] == 'walk', timeout=8); time.sleep(1); h1 = agent()['heading']
    check('idles at a route point, then turns in place and walks on', abs(math.remainder(h1 - h0, 2 * math.pi)) > 1.2, {'before': h0, 'after': h1})
    # Personal space: standing in its way makes it wait.
    a = agent(); f = [math.sin(a['heading']), 0, math.cos(a['heading'])]
    ahead = [a['position'][0] + f[0] * 1.4, 1.7, a['position'][2] + f[2] * 1.4]
    pose(client, ahead, math.atan2(-f[0], f[2]) + math.pi, 0); time.sleep(.6)
    p0 = agent()['position']; time.sleep(1.0); p1 = agent()['position']
    check('yields to a player standing in its way', agent()['yielding'] and math.dist(p0, p1) < .05, {'agent': agent(), 'moved': math.dist(p0, p1)})
    pose(client, [ahead[0] + 6, 1.7, ahead[2] + 6], 0, 0); time.sleep(.5)
    check('walks on once the way is clear', wait(lambda: not agent()['yielding'], timeout=4) is not None, agent())

    def frames_per_second(seconds=3.0):
        f0 = s.camera()['frames']; time.sleep(seconds); return (s.camera()['frames'] - f0) / seconds
    def frozen(name, reason, aim=True):
        if aim:
            look_at_npc(); time.sleep(.6)
        q0 = agent(); fps = frames_per_second(); q1 = agent()
        check(f'{name}: paused ({reason}), frozen, no frames', npc()['state'] == f'paused: {reason}' and not npc()['forcesFrames']
              and q0['position'] == q1['position'] and q0['clipTime'] == q1['clipTime'] and fps < 2,
              {'state': npc()['state'], 'framesPerSecond': fps})
        return fps
    look_at_npc(); time.sleep(.5)
    animating_fps = frames_per_second()
    check('animating in view renders frames', npc()['state'] == 'animating' and animating_fps > 20, {'framesPerSecond': animating_fps})
    look_at_npc(); pose(client, s.camera()['position'], s.camera()['yaw'] + math.pi, 0); time.sleep(.6)
    away_fps = frames_per_second()
    check('animating out of view costs no frames', npc()['state'] == 'animating' and not npc()['forcesFrames'] and away_fps < 2, {'framesPerSecond': away_fps})
    lua('{npc={motion="auto"}}'); time.sleep(.3)
    keys('-M', 'logo', '-P', 'Super_L', '-k', 'Tab', '-p', 'Super_L', '-m', 'logo'); wait(lambda: s.camera()['layerCursor'])
    frozen('free cursor', 'free cursor')
    keys('-M', 'logo', '-P', 'Super_L', '-k', 'Tab', '-p', 'Super_L', '-m', 'logo'); wait(lambda: not s.camera()['layerCursor'])
    client.call('graphics.set', {'patch': {'reduced_motion': True}}); time.sleep(.3)
    frozen('reduced motion', 'reduced motion')
    client.call('graphics.set', {'patch': {'reduced_motion': False}})
    # auto on this machine's power source: mains animates, battery / USB-C pauses.
    supply = {e.name: (e / 'type').read_text().strip() for e in pathlib.Path('/sys/class/power_supply').iterdir()}
    look_at_npc(); keys('-P', 'w', '-s', '200', '-p', 'w'); time.sleep(.3)  # the player is present (poses don't count)
    state = npc()['state']
    check('auto follows the power source (mains animates; battery or USB-C pauses)', state in ('animating', 'paused: battery'), {'state': state, 'supplies': supply})
    print('npc auto state on this power source:', state, flush=True)
    time.sleep(31)  # no input for 30 s: the player is away (checked before the power source)
    frozen('player away', 'idle', aim=False)  # re-aiming would be movement
    lua('{npc={motion="still"}}'); time.sleep(.3); look_at_npc(); time.sleep(.5)
    q0 = agent(); time.sleep(1); q1 = agent()
    check('still: posed, not moving', npc()['state'] == 'still' and q0['position'] == q1['position'] and npc()['drawn'] == 1, npc())
    lua('{npc={motion="off"}}'); time.sleep(.3); look_at_npc(); time.sleep(.5)
    check('off: hidden', npc()['state'] == 'off' and npc()['drawn'] == 0, npc())
    lua('{npc={motion="auto"}}')
    (s.ROOT / 'docs/evidence/npc.json').write_text(json.dumps({'nestedOnly': True, 'world': SWITCHYARD, 'character': CHARACTER,
                                                               'checks': checks}, indent=2) + '\n')
    print('PASS npc prototype:', len(checks), 'checks', flush=True)


def measure(client, npc, agent, look_at_npc, lua, check, checks):
    """GPU cost of one NPC: world pass GPU ms with the NPC drawn vs off, continuous render at a fixed scale."""
    out = {}
    for scale in (.5, 1.0):
        client.call('graphics.set', {'patch': {'dynamic_resolution': False, 'render_scale': scale}})
        results = {}
        for motion in ('always', 'off', 'always', 'off'):
            lua(f'{{npc={{motion="{motion}"}}}}'); time.sleep(.4); look_at_npc(3.5, 1.5); time.sleep(1.5)
            samples = []
            for _ in range(20):
                t = client.call('graphics.get', {})['timings']
                if t.get('gpuPassMs') and all(isinstance(v, (int, float)) for v in t['gpuPassMs']):
                    samples.append(sum(t['gpuPassMs']))
                time.sleep(.1)
            results.setdefault(motion, []).append(statistics.median(samples))
        out[str(scale)] = {'gpuMsWithNpc': round(statistics.mean(results['always']), 3), 'gpuMsWithout': round(statistics.mean(results['off']), 3),
                           'npcMs': round(statistics.mean(results['always']) - statistics.mean(results['off']), 3)}
        print('MEASURE npc', scale, json.dumps(out[str(scale)]), flush=True)
    (s.STATE / 'npc-measure.json').write_text(json.dumps(out, indent=2) + '\n')
    check('measured', True, out)


def video(client, npc, agent, look_at_npc, lua):
    """Camera inside the route loop turning to keep the NPC in frame; still during the pause."""
    path = pathlib.Path(os.environ['HYPRUNE_NPC_VIDEO']); events = []
    eye = [0, 1.4, 0]
    view = {'h': None}
    def track():
        # Follow from its inner side, 3.4 m off and a little behind; the follow angle eases.
        a = agent(); p = a['position']
        view['h'] = a['heading'] if view['h'] is None else view['h'] + math.remainder(a['heading'] - view['h'], 2 * math.pi) * .08
        h = view['h']; f = [math.sin(h), math.cos(h)]; r = [-f[1], f[0]]  # inner side of the clockwise loop
        eye[0], eye[2] = p[0] + r[0] * 3.4 - f[0] * 1.2, p[2] + r[1] * 3.4 - f[1] * 1.2
        d = [p[0] - eye[0], .95 - eye[1], p[2] - eye[2]]
        s.ctl('eval', f'hl.plugin.hyprune.camera({eye[0]},{eye[1]},{eye[2]},{math.atan2(d[0], -d[2])},{math.atan2(d[1], math.hypot(d[0], d[2]))})')
    lua('{npc={motion="always"}}'); track(); time.sleep(1)
    rec = s.envrun('wf-recorder', '-o', 'HEADLESS-1', '-r', '60', '-c', 'libx264', '-p', 'preset=ultrafast', '-p', 'crf=18', '-f', str(path),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1); t0 = time.time()
    def phase(text, seconds, follow=True):
        b = time.time() - t0; end = time.time() + seconds; n = 0
        while time.time() < end:
            if follow:
                track()
            else:  # render on demand presents nothing while paused: nudge invisibly so the clip shows it
                d = s.camera(); s.ctl('eval', f'hl.plugin.hyprune.camera({eye[0]},{eye[1]},{eye[2]},{d["yaw"] + 1e-5 * (n % 2)},{d["pitch"]})')
            n += 1; time.sleep(.04)
        events.append({'start': round(b, 2), 'end': round(time.time() - t0, 2), 'text': text})
    phase('Placeholder NPC on a route in Switchyard: in-place walk clip, core moves it at the clip speed', 10)
    lua('{npc={motion="still"}}')
    phase('Paused (here: still): the NPC freezes and costs no frames', 3.5, follow=False)
    lua('{npc={motion="always"}}')
    phase('It walks on from where it stopped', 5)
    rec.send_signal(signal.SIGINT)
    for n in range(600):
        if rec.poll() is not None:
            break
        track(); time.sleep(.1)
    path.with_suffix('.json').write_text(json.dumps(events, indent=2) + '\n')
    print('PASS npc video', path, flush=True)
