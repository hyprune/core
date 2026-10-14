"""The companion follows through Lumen Reach's Rime descent, corridor, stairs and warps:
dev/nested/run.sh --follow-corridor.

Nested only. Lumen Reach (HYPRUNE_LUMEN_PACKAGE, else the worlds checkout's dist) and the
companion character library (HYPRUNE_COMPANION_LIBRARY: Drunk Ash, 1.9 m). The player walks
(autodrive, walk mode) and the companion follows. Along every leg it is sampled four times a
second; in follow mode it may never stand idle far from the player: when the player stops,
within 8 s it is back near them (within 3.5 m, on their floor) or waiting out of view. Legs:
 1. Down the Signal Rise ramp into the Rime Vault (the owner's capture: idle 10 m up the
    ramp while the player stood at its foot), then turning round to look back up it.
 2. Back up the ramp to Signal Rise, then down the service stairs beside it.
 3. Across the vault floor, up the maintenance stairs to the deck and down again.
 4. Up the service descent to the Array Hall and back down into the vault.
 5. Warps: the vault, Signal Rise and Sun Court spawns (camera jumps); the player stands
    still at the spawn. It comes in nearby, out of view, and settles near them.
HYPRUNE_FOLLOW_CORRIDOR_LOG=1 prints every sample.
Evidence: docs/evidence/follow-corridor.json (HYPRUNE_FOLLOW_CORRIDOR_SHOTS: elsewhere).
"""
import hashlib, json, math, os, pathlib, shutil, subprocess, time
import session as s
from m2 import wait

LUMEN = 'com.hyprune.lumen-reach'
ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_COMPANION_LIBRARY', ''))
NEAR, SETTLE = 3.5, 8.0


def prepare(installed):
    if not ARCHIVE.is_file():
        raise SystemExit('--follow-corridor needs HYPRUNE_COMPANION_LIBRARY (a companion library .tar.zst)')
    source = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--follow-corridor needs {LUMEN} at {source} (HYPRUNE_LUMEN_PACKAGE)')
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(source, installed / LUMEN)
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'companion.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(os.environ.get('HYPRUNE_FOLLOW_CORRIDOR_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    verbose = os.environ.get('HYPRUNE_FOLLOW_CORRIDOR_LOG') == '1'
    checks, legs = [], []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'follow-corridor-checks.json').write_text(json.dumps({'checks': checks, 'legs': legs}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS follow-corridor:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    comp = lambda: s.camera()['companion']

    def feet():
        p = client.call('camera.get', {})['position']
        return [p[0], p[1] - 1.7, p[2]]

    def flat(a, b):
        return math.hypot(a[0] - b[0], a[2] - b[2])

    def camera(p, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({p[0]},{p[1] + 1.7},{p[2]},{yaw},{pitch})'); time.sleep(.25)

    def look_at(target):
        f = feet()
        camera(f, math.atan2(target[0] - f[0], -(target[2] - f[2])), math.atan2(target[1] + 1 - (f[1] + 1.7), max(.1, flat(f, target))))

    def shot(name):
        try:
            subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=10)
        except subprocess.TimeoutExpired:
            pass

    def sample(leg, label):
        c, f = comp(), feet()
        if not c['position']:
            return None
        row = {'t': round(time.monotonic() - leg['t0'], 2), 'label': label, 'd': round(flat(c['position'], f), 2),
               'dy': round(c['position'][1] - f[1], 2), 'activity': c['state']['activity'], 'brain': c['brain'],
               'inView': c['inView'], 'step': c['step'], 'slot': c['slot'], 'waypoints': c['waypoints'],
               'teleports': c['teleports'], 'pending': c['teleportPending'], 'slotMiss': c.get('slotMiss'), 'feet': [round(v, 2) for v in f],
               'at': [round(v, 2) for v in c['position']]}
        leg['samples'].append(row)
        if verbose:
            print('SAMPLE', leg['name'], json.dumps(row), flush=True)
        return row

    def settle(leg, label, seconds=SETTLE):
        """The player stands still: within `seconds` it is near them or waiting out of view, and
        it never idles far away in between."""
        t0 = time.monotonic()
        while time.monotonic() - t0 < seconds:
            sample(leg, label)
            time.sleep(.25)
        rows = [r for r in leg['samples'] if r['label'] == label]
        last = rows[-1]
        near = last['d'] <= NEAR and abs(last['dy']) < 1.2
        hidden = last['activity'] == 'waiting' and not last['inView']
        return near or hidden, last

    def drive(leg, points, speed=1.3, timeout=120):
        client.call('route.set', {'points': [{'position': [p[0], p[1] + 1.7, p[2]]} for p in points]})
        client.call('autodrive.start', {'speed': speed})
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout and (time.monotonic() - t0 < 2 or state()['location']['autodrive']):
            sample(leg, 'walking')
            time.sleep(.25)
        client.call('autodrive.stop', {}) if state()['location']['autodrive'] else None

    def stuck(leg):
        """Idle far away while following: 'idle' (not waiting, not moving) more than 4.5 m from
        the player for 2 s or longer, at any time on the leg."""
        run, worst = 0., 0.
        prev = None
        for r in leg['samples']:
            far_idle = r['activity'] == 'idle' and r['d'] > 4.5
            if far_idle and prev is not None:
                run += r['t'] - prev['t']
            elif not far_idle:
                run = 0.
            worst = max(worst, run)
            prev = r
        return round(worst, 2)

    def run_leg(name, start, points=(), look_back=True, speed=1.3):
        """Warp to `start` (feet, yaw) and let it rejoin, walk `points`, stop (and look back)."""
        only = os.environ.get('HYPRUNE_FOLLOW_CORRIDOR_LEGS')
        if only and name not in only.split(','):
            return None
        leg = {'name': name, 't0': time.monotonic(), 'samples': []}
        legs.append(leg)
        camera(start[0], start[1]); time.sleep(.3)
        ok, last = settle(leg, 'arrived', 6)
        check(f'{name}: after the warp it is near or out of view', ok, last)
        t_before = comp()['teleports']
        if points:
            drive(leg, points, speed)
            f = feet()
            check(f'{name}: the player walked the leg', flat(f, points[-1]) < 1.5 and abs(f[1] - points[-1][1]) < 1,
                  {'feet': f, 'target': points[-1]})
        ok, last = settle(leg, 'stopped')
        if look_back and points:
            look_at(comp()['position']); time.sleep(.3)
            ok2, last = settle(leg, 'looking back', 6)
            ok = ok and ok2
        walked = [r for r in leg['samples'] if r['label'] != 'arrived']
        summary = {'leg': name, 'end': last, 'idleFar': stuck({'samples': walked}), 'teleports': comp()['teleports'] - t_before,
                   'pops': comp()['pops'], 'maxD': max((r['d'] for r in walked), default=None)}
        shot(f'follow-{name}.png')
        check(f'{name}: it keeps up (near or out of view at the end, never idle far away)', ok and summary['idleFar'] < 2.0, summary)
        return summary

    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    a = s.STATE / ARCHIVE.name
    installed = client.call('libraries.install', {'archive': str(a), 'sha256': hashlib.sha256(a.read_bytes()).hexdigest()})
    library = f"{installed['id']}@{installed['version']}"
    ref = next(e['ref'] for e in client.call('objects.catalog', {})['entries'] if e.get('library') == library and 'character' in e['traits'])
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60); time.sleep(1)
    client.call('graphics.set', {'patch': {'offload': 'off', 'npc_motion': 'always'}})
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    manifest = json.loads((s.STATE / 'worlds' / LUMEN / 'world.json').read_text())
    nodes = {n['id']: n['position'] for n in manifest['extensions']['com.hyprune.lumen-reach.routes']['nodes']}
    stairs = {p['id']: p for p in manifest['extensions']['com.hyprune.lumen-reach.stairs']['paths']}

    # Start at the top of the Signal Rise ramp, looking down it; the companion joins.
    camera(nodes['rise-arrival'], math.pi)
    client.call('companion.set', {'ref': ref})
    wait(lambda: comp()['state']['state'] == 'active' and comp()['position'] is not None, timeout=30); time.sleep(2)

    foot = [52.4, -6, -98.3]   # the owner's capture: the player at the foot of the ramp
    top, entry, exit_ = nodes['rise-ramp-top'], nodes['rise-entry'], nodes['vault-exit']
    snow = stairs['snow-service-ascent']
    # 1. Down the Signal Rise ramp into the vault (the owner's capture), looking back up it.
    run_leg('ramp-down', (nodes['rise-arrival'], math.pi), [top, [54, -4, -108], entry, foot])
    run_leg('vault-floor', (foot, math.pi / 2), [exit_, nodes['vault-center']])
    # 2. Up the ramp; down the service stairs beside it.
    run_leg('ramp-up', (nodes['vault-center'], 0.0), [exit_, entry, top])
    run_leg('stairs-down', (top, math.pi / 2), [[snow['to'][0], 0, snow['to'][2] + .6], [snow['from'][0], -6, snow['from'][2] - .6]])
    run_leg('stairs-up', ([snow['from'][0], -6, snow['from'][2] - 1], math.pi), [[snow['from'][0], -6, snow['from'][2] - .6],
                                                                               [snow['to'][0], 0, snow['to'][2] + .6]])
    # 3. The maintenance stairs to the deck and back down.
    run_leg('maintenance-up', (nodes['vault-arrival'], 0.0), [nodes['vault-door'], nodes['vault-maintenance-base'], nodes['vault-maintenance'],
                                                              nodes['vault-platform']])
    run_leg('maintenance-down', (nodes['vault-platform'], math.pi), [nodes['vault-maintenance'], nodes['vault-maintenance-base'],
                                                                     nodes['vault-door'], nodes['vault-arrival']])
    # 4. Up the service descent to the hall level and back down into the vault.
    run_leg('descent-up', (nodes['vault-arrival'], 0.0), [nodes['vault-door'], nodes['descent-bottom'], nodes['descent-top']])
    run_leg('descent-down', (nodes['descent-top'], math.pi), [nodes['descent-bottom'], nodes['vault-door'], nodes['vault-arrival']])
    # 5. Warps to the area spawns; the player stands still.
    for spawn in ('signal-rise', 'rime-vault', 'sun-court'):
        p = next(x for x in manifest['spawns'] if x['id'] == spawn)['pose']['position']
        run_leg(f'warp-{spawn}', (p, 0.0))

    if not os.environ.get('HYPRUNE_FOLLOW_CORRIDOR_SHOTS'):
        (s.ROOT / 'docs/evidence/follow-corridor.json').write_text(json.dumps(
            {'nestedOnly': True, 'world': LUMEN, 'companion': ref,
             'checks': checks}, indent=1) + '\n')
    print(f'PASS follow-corridor: {len(checks)} checks', flush=True)
