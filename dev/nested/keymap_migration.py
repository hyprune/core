#!/usr/bin/env python3
"""The keymap goes back to Shift sprint and Ctrl down, once (nested only).

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/keymap_migration.py OUTDIR

Starts from an input.json at IPC 0.29's swapped pair (sprint Control_L, down Shift_L) and a
customised binding: core restores Shift sprint and Ctrl down once (marker in
$XDG_STATE_HOME/hyprune/migrations.json), keeps the customised binding and Ctrl+R; the swapped
pair chosen again afterwards stays. Results in OUTDIR/results.json.
"""
import argparse, json, os, pathlib, shutil
import session as s
import npcs
from m2 import wait

WORLD = 'com.hyprune.lumen-reach'


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_KEYMAP_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    shutil.rmtree(s.STATE / 'statehome' / 'hyprune', ignore_errors=True)
    swapped = json.loads((s.ROOT / 'config/input.default.json').read_text())
    world = swapped['modes']['world']
    world['sprint'], world['down'] = ['Control_L'], ['Shift_L']
    world['noclip'] = ['n']  # a customised binding
    (s.STATE / 'input.json').write_text(json.dumps(swapped))


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    results = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS keymap:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    marker = s.STATE / 'statehome' / 'hyprune' / 'migrations.json'
    wait(lambda: marker.exists(), timeout=20)
    world = lambda: client.call('input.get', {})['config']['modes']['world']
    w = world()
    saved = json.loads((s.STATE / 'input.json').read_text())['modes']['world']
    check('the swapped pair is restored once, customised bindings and Ctrl+R stay',
          w['sprint'] == ['Shift_L'] and w['down'] == ['Control_L'] and w['noclip'] == ['n'] and w['record.toggle'] == ['Ctrl+r'] and
          saved['sprint'] == ['Shift_L'] and json.loads(marker.read_text()) == {'keymap.sprint-shift': True},
          {'sprint': w['sprint'], 'down': w['down'], 'noclip': w['noclip']})
    config = client.call('input.get', {})['config']
    config['modes']['world']['sprint'], config['modes']['world']['down'] = ['Control_L'], ['Shift_L']
    client.call('input.set', {'config': config})
    w = world()
    check('the swapped pair chosen again stays', w['sprint'] == ['Control_L'] and w['down'] == ['Shift_L'], w['sprint'])
    (out / 'results.json').write_text(json.dumps(results, indent=2))
    client.call('runtime.exit', {})
    print(f'PASS keymap: {len(results)} checks', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve()),
                             protocol='0.30'))
