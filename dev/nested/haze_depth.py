"""World haze (blended world materials: Rime Vault's vent steam) behind a character stays
behind it, in-process and with offload: dev/nested/run.sh --haze-depth.

Nested only. Lumen Reach (HYPRUNE_LUMEN_PACKAGE, else the worlds checkout's dist) and a
character library (HYPRUNE_COMPANION_LIBRARY: Drunk Ash, spawned as the user's character).
The player stands on the vault floor looking at the purge vents' steam with the character
posed still between them (the owner's capture: steam drawn over the resident in front of it).
Per renderer (in-process, then offload), four stills at render scale 1:
  A  haze on, character      B  haze off, character
  C  haze on, no character   D  haze off, no character
The character's pixels are where B and D differ (eroded 2 px); the steam's pixels are where
C and D differ. Where both overlap (steam behind the character), A must equal B: the steam
adds nothing in front of the character. Offload also matches in-process (A per pixel).
Evidence: docs/evidence/haze-depth.json; stills and masks in the state dir
(HYPRUNE_HAZE_DEPTH_SHOTS: elsewhere).
"""
import hashlib, json, math, os, pathlib, shutil, subprocess, time
import numpy as np
from PIL import Image
import session as s
from m2 import wait

LUMEN = 'com.hyprune.lumen-reach'
ARCHIVE = pathlib.Path(os.environ.get('HYPRUNE_COMPANION_LIBRARY', ''))
EYE = [40.8, -4.3, -98.6]          # the owner's capture: the vault's south-west, looking north-east
STEAM = [47.6, -4.0, -91.0]        # at the purge vent cluster's plumes, 10 m away
AIM = list(STEAM)
LEVELS = 4                         # 8-bit levels: more is a differing pixel


def prepare(installed):
    if not ARCHIVE.is_file():
        raise SystemExit('--haze-depth needs HYPRUNE_COMPANION_LIBRARY (a character library .tar.zst)')
    source = pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN)))
    if not (source / 'world.json').is_file():
        raise SystemExit(f'--haze-depth needs {LUMEN} at {source} (HYPRUNE_LUMEN_PACKAGE)')
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(source, installed / LUMEN)
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json', 'companion.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(ARCHIVE, s.STATE / ARCHIVE.name)


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)


def differs(a, b):
    return np.abs(a - b).max(axis=2) > LEVELS


def erode(mask, n=2):
    m = mask.copy()
    for _ in range(n):
        m[1:, :] &= m[:-1, :]; m[:-1, :] &= m[1:, :]; m[:, 1:] &= m[:, :-1]; m[:, :-1] &= m[:, 1:]
    return m


def acceptance(client, children, args, shell_token, socketpath):
    global EYE
    out = pathlib.Path(os.environ.get('HYPRUNE_HAZE_DEPTH_SHOTS', str(s.STATE)))
    out.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'haze-depth-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS haze-depth:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    perf = lambda: client.call('graphics.get', {})['performance']
    agents = lambda: s.camera()['npcs']['agents']

    def graphics(**patch):
        client.call('graphics.set', {'patch': patch}); time.sleep(.3)

    def offload(mode):
        graphics(offload=mode)
        want = 'offload' if mode == 'on' else 'in-process'
        return wait(lambda: perf()['activeRenderer'] == want, 40)

    def look(eye, target):
        d = [target[i] - eye[i] for i in range(3)]
        s.ctl('eval', f'hl.plugin.hyprune.camera({eye[0]},{eye[1]},{eye[2]},{math.atan2(d[0], -d[2])},{math.atan2(d[1], math.hypot(d[0], d[2]))})')

    def still(name):
        time.sleep(1.5)
        for attempt in range(3):
            try:
                subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=10)
                return out / name
            except subprocess.TimeoutExpired:  # nothing changed, no frame: nudge the view
                look([EYE[0] + .001 * (attempt + 1), EYE[1], EYE[2]], AIM); time.sleep(.5); look(EYE, AIM)
        raise AssertionError(f'no frame for {name}')

    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    a = s.STATE / ARCHIVE.name
    installed = client.call('libraries.install', {'archive': str(a), 'sha256': hashlib.sha256(a.read_bytes()).hexdigest()})
    library = f"{installed['id']}@{installed['version']}"
    ref = next(e['ref'] for e in client.call('objects.catalog', {})['entries'] if e.get('library') == library and 'character' in e['traits'])
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active'] and not s.camera().get('loading'), timeout=60); time.sleep(1)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    graphics(offload='off', render_scale=1, dynamic_resolution=False, npc_motion='still', alpha_haze=True)
    AIM[:] = STEAM
    look(EYE, AIM); time.sleep(.5)

    # The character: the free standing spot core finds in front of the player (the floor on the
    # line itself is a keep-clear walkway); the view then looks past it at the steam.
    ident = client.call('npcs.spawn', {'ref': ref})['id']
    wait(lambda: any(x['id'] == ident for x in agents()), timeout=20); time.sleep(1)
    at = next(x for x in agents() if x['id'] == ident)['position']
    away = [at[0] - STEAM[0], at[2] - STEAM[2]]
    n = math.hypot(*away)
    EYE = [at[0] + away[0] / n * 3.2, at[1] + 1.7, at[2] + away[1] / n * 3.2]
    AIM[:] = [STEAM[0], at[1] + 1.4, STEAM[2]]
    look(EYE, AIM); time.sleep(.5)
    check('a character stands between the player and the vent steam', n > 1.5, {'id': ident, 'at': at, 'eye': EYE, 'toSteam': round(n, 2)})

    results = {}
    for renderer in ('in-process', 'offload'):
        offload('on' if renderer == 'offload' else 'off')
        shots = {}
        for key, haze, shown in (('A', True, True), ('B', False, True), ('C', True, False), ('D', False, False)):
            graphics(alpha_haze=haze, npc_motion='still' if shown else 'off')
            if renderer == 'offload':
                try:
                    wait(lambda: perf()['activeRenderer'] == 'offload', 30)
                except AssertionError:
                    pass
                p = perf()
                check(f'{renderer} {key}: offload is displayed', p['activeRenderer'] == 'offload',
                      {k: p.get(k) for k in ('activeRenderer', 'status', 'reason')})
            shots[key] = still(f'haze-{renderer}-{key}.png')
        A, B, C, D = (load(shots[k]) for k in 'ABCD')
        character = erode(differs(B, D))
        steam = differs(C, D)
        behind = character & steam
        over = differs(A, B) & behind
        Image.fromarray((np.stack([character, steam, over], axis=2) * 255).astype(np.uint8)).save(out / f'haze-{renderer}-masks.png')
        stats = {'characterPixels': int(character.sum()), 'steamPixels': int(steam.sum()), 'steamBehindCharacter': int(behind.sum()),
                 'steamOverCharacter': int(over.sum()), 'maxLevelsOverCharacter': int(np.abs(A - B).max(axis=2)[behind].max()) if behind.any() else 0}
        results[renderer] = stats
        check(f'{renderer}: the steam lies behind the character in this view', stats['steamBehindCharacter'] > 1500 and stats['characterPixels'] > 20000, stats)
        check(f'{renderer}: no steam drawn over the character', stats['steamOverCharacter'] <= stats['steamBehindCharacter'] * .002, stats)
        results[renderer + '-A'] = shots['A']
    parity = np.abs(load(results['in-process-A']) - load(results['offload-A'])).max(axis=2)
    stats = {'differingFraction': round(float((parity > LEVELS).mean()), 6), 'meanLevels': round(float(parity.mean()), 4)}
    check('offload matches the in-process renderer (haze and character)', stats['differingFraction'] < .01, stats)
    offload('off'); graphics(npc_motion='auto', alpha_haze=True)
    if not os.environ.get('HYPRUNE_HAZE_DEPTH_SHOTS'):
        (s.ROOT / 'docs/evidence/haze-depth.json').write_text(json.dumps(
            {'nestedOnly': True, 'world': LUMEN, 'character': ref, 'checks': checks}, indent=1) + '\n')
    print(f'PASS haze-depth: {len(checks)} checks', flush=True)
