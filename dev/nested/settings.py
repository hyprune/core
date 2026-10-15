#!/usr/bin/env python3
"""Global settings presets and overrides (IPC 0.30 settings.get / settings.set), nested only.

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/settings.py OUTDIR

Starts from graphics.json and player.json as an older core left them (no settings.json): the
migration picks the preset their quality implies and keeps only the values that differ from
it. Then: an override shows as the user's and survives a preset switch; the preset's other
defaults apply; reset returns a setting to the preset's default; the USB-C preset turns offload
on; reset all clears every override but keeps the preset; an IPC 0.29 peer's graphics.get still
answers. Results in OUTDIR/results.json.
"""
import argparse, json, os, pathlib, shutil
import session as s
import npcs
from m2 import wait

WORLD = 'com.hyprune.lumen-reach'
OLD_GRAPHICS = {'quality': 'medium', 'dynamic_resolution': True, 'offload': 'off', 'target_fps': 110, 'sharpen': 1,
                'msaa': 1, 'render_scale': 'auto', 'bloom': None}
OLD_PLAYER = {'walk_speed': 4.0, 'run_speed': 12.0}


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_SETTINGS_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    (s.STATE / 'settings.json').unlink(missing_ok=True)
    (s.STATE / 'graphics.json').write_text(json.dumps(OLD_GRAPHICS))
    (s.STATE / 'player.json').write_text(json.dumps(OLD_PLAYER))


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    results = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS settings:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    get = lambda: client.call('settings.get', {})
    setting = lambda st, key: st['settings'][key]
    st = wait(lambda: (lambda v: v if v['preset'] else None)(get()), timeout=20)
    graphics = json.loads((s.STATE / 'graphics.json').read_text())
    player = json.loads((s.STATE / 'player.json').read_text())
    check('migration: preset from the old quality, only differing values stay overrides',
          st['preset'] == 'medium' and json.loads((s.STATE / 'settings.json').read_text()) == {'version': 1, 'preset': 'medium'} and
          graphics == {'target_fps': 110, 'sharpen': 1, 'bloom': None} and player == {'run_speed': 12.0},
          {'graphics': graphics, 'player': player})
    check('sources: user for overrides, preset for the rest',
          setting(st, 'graphics.target_fps') == {'value': 110, 'default': 60, 'source': 'user'} and
          setting(st, 'graphics.msaa')['source'] == 'preset' and setting(st, 'player.run_speed')['source'] == 'user' and
          st['overrides'] == 3, {k: setting(st, k) for k in ('graphics.target_fps', 'graphics.msaa', 'player.run_speed')})
    st = client.call('settings.set', {'set': {'graphics.msaa': 2, 'player.carry_turn': .2}})
    check('an explicit override', setting(st, 'graphics.msaa') == {'value': 2, 'default': 1, 'source': 'user'} and
          setting(st, 'player.carry_turn')['value'] == .2, setting(st, 'graphics.msaa'))
    st = client.call('settings.set', {'preset': 'ultra'})
    eff = client.call('graphics.get', {})['effective']
    check('a preset switch keeps overrides and applies its other defaults',
          st['preset'] == 'ultra' and setting(st, 'graphics.msaa') == {'value': 2, 'default': 4, 'source': 'user'} and
          setting(st, 'graphics.anisotropy') == {'value': 16, 'default': 16, 'source': 'preset'} and eff['quality'] == 'ultra' and
          eff['msaa'] == 2 and setting(st, 'player.carry_turn')['value'] == .2,
          {'msaa': setting(st, 'graphics.msaa'), 'anisotropy': setting(st, 'graphics.anisotropy'), 'effective': {k: eff[k] for k in ('quality', 'msaa', 'anisotropy')}})
    st = client.call('settings.set', {'reset': ['graphics.msaa']})
    check('reset returns to the preset default', setting(st, 'graphics.msaa') == {'value': 4, 'default': 4, 'source': 'preset'} and
          client.call('graphics.get', {})['effective']['msaa'] == 4, setting(st, 'graphics.msaa'))
    st = client.call('settings.set', {'preset': 'usb-c'})
    check('the USB-C preset: offload on, even pacing, medium tier',
          setting(st, 'graphics.offload') == {'value': 'on', 'default': 'on', 'source': 'preset'} and
          setting(st, 'graphics.frame_pacing')['value'] == 'even' and client.call('graphics.get', {})['config']['quality'] == 'medium',
          {k: setting(st, k) for k in ('graphics.offload', 'graphics.frame_pacing', 'graphics.quality')})
    st = client.call('settings.set', {'resetAll': True})
    check('reset all clears every override and keeps the preset',
          st['overrides'] == 0 and st['preset'] == 'usb-c' and json.loads((s.STATE / 'graphics.json').read_text()) == {} and
          json.loads((s.STATE / 'player.json').read_text()) == {} and setting(st, 'graphics.target_fps')['value'] == 60,
          {'overrides': st['overrides']})
    try:
        client.call('settings.set', {'set': {'graphics.msaa': 3}})
        refused = False
    except Exception:
        refused = True
    check('an invalid value is refused', refused)
    client.call('settings.set', {'preset': 'auto'})
    old = s.Client(str(socketpath), shell_token, '0.29', 'com.hyprune.shell')
    check('an IPC 0.29 peer: graphics.get answers, settings methods are not offered',
          'settings.get' not in old.hello['methods'] and old.call('graphics.get', {})['config'] is not None)
    old.close()
    (out / 'results.json').write_text(json.dumps(results, indent=2))
    client.call('runtime.exit', {})
    print(f'PASS settings: {len(results)} checks', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve()),
                             protocol='0.30'))
