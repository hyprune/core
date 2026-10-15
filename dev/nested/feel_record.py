#!/usr/bin/env python3
"""A fixed movement recorded with the debug recorder (Ctrl+R) for dev/feel/analyze.py.

  HYPRUNE_NESTED_STATE_ROOT=$(mktemp -d) dev/nested/run.sh dev/nested/feel_record.py OUTDIR

Nested only. Lumen Reach from ../worlds/dist (HYPRUNE_FEEL_WORLD: an extracted package). For
each configuration in HYPRUNE_FEEL_RECORD_CONFIGS (JSON list of graphics patches, default
in-process and offload with even and max_rate pacing), Ctrl+R starts a recording, then:
a 3 s walk (W), a 3 s steady pan (a 500 Hz relative pointer stream, like a mouse), a 3 s
sprint (Ctrl+W) turning at the same time, and a short stop; Ctrl+R stops it and the
recording is labelled through recording.label. OUTDIR/<n>-<label>.jsonl are the recordings;
OUTDIR/runs.json lists them with their configuration. HYPRUNE_NESTED_REFRESH sets the
output's refresh (98 like the owner's external panel). HYPRUNE_FEEL_RECORD_BASELINE=1 also
runs each configuration once without recording and keeps the renderer's presentation log
(OUTDIR/<n>-unrecorded.json), so recording on and off can be compared.
HYPRUNE_FEEL_RECORD_CARRY=1 first places a terminal ahead (tool 5) and records a carry:
Super+LMB on it, a steady 1 s turn, a hold, release (OUTDIR/carry-*.jsonl; analyze.py reports
the carried window's eased turn against its target).
"""
import argparse, json, math, os, pathlib, shutil, subprocess, time
import session as s
import npcs
from m2 import keys, wait

WORLD = 'com.hyprune.lumen-reach'
DEFAULT = [{'offload': 'off', 'target_fps': 120},
           {'offload': 'on', 'target_fps': 60, 'frame_pacing': 'even'},
           {'offload': 'on', 'target_fps': 60, 'frame_pacing': 'max_rate'}]


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_FEEL_WORLD', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    shutil.rmtree(installed / WORLD, ignore_errors=True)
    shutil.copytree(source, installed / WORLD)
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output); out.mkdir(parents=True, exist_ok=True)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)
    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush(); assert device.stdout.readline().strip() == 'ok'
    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    carry = os.environ.get('HYPRUNE_FEEL_RECORD_CARRY') == '1'
    if carry:  # a terminal on the world workspace before entry: it waits in the placement queue
        s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="20"}))')
        children.append(s.envrun('kitty', '--class', 'feel-carry', '--title', 'Feel-Carry', stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        wait(lambda: any(w['title'] == 'Feel-Carry' for w in json.loads(s.ctl('clients', '-j'))), timeout=20); time.sleep(.5)
        s.ctl('eval', 'hl.dispatch(hl.dsp.focus({workspace="91"}))')
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    perf = lambda: client.call('graphics.get', {})['performance']
    configs = json.loads(os.environ.get('HYPRUNE_FEEL_RECORD_CONFIGS', json.dumps(DEFAULT)))

    def start_pose():
        s.ctl('eval', 'hl.plugin.hyprune.camera(-8,1.7,12,0,0)'); time.sleep(1.5)

    def movement():
        keys('-P', 'w', '-s', '3000', '-p', 'w'); time.sleep(.4)
        pointer('stream', 3.5, 0, 500, 3000); time.sleep(.4)  # 1750 px/s, steady
        sprint = s.envrun('wtype', '-M', 'ctrl', '-P', 'Control_L', '-P', 'w', '-s', '3000', '-p', 'w', '-p', 'Control_L', '-m', 'ctrl')
        children.append(sprint); time.sleep(.05)
        pointer('stream', -2.5, 0, 500, 2900)
        sprint.wait(timeout=10); time.sleep(.6)

    runs = []
    if carry:
        sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
        state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
        wait(lambda: state()['editor']['unplaced']['count'] == 1, timeout=30)
        start_pose(); pointer('button', 272); time.sleep(.8)  # tool 5: place it ahead
        sid = next(c['id'] for c in s.camera()['captures'] if c['title'] == 'Feel-Carry')
        v = next(c for c in s.camera()['captures'] if c['id'] == sid)
        x, y, z, w = v['rotation']; n = [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
        eye = [v['position'][k] + 2.5 * n[k] for k in range(3)]
        s.ctl('eval', f'hl.plugin.hyprune.camera({eye[0]},{eye[1]},{eye[2]},{math.atan2(-n[0], n[2])},{math.asin(-n[1])})'); time.sleep(.8)
        keys('-k', '1'); time.sleep(.3)
        keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'r', '-p', 'Control_L', '-m', 'ctrl')
        wait(lambda: client.call('recording.status', {})['recording'], timeout=10)
        hold = s.envrun('wtype', '-M', 'logo', '-P', 'Super_L', '-s', '4200', '-p', 'Super_L', '-m', 'logo'); children.append(hold)
        wait(lambda: s.camera()['superHeld']); pointer('button', 272, 1); wait(lambda: s.camera()['gesture'] == 1)
        time.sleep(.4)
        pointer('stream', 4, 0, 500, 1000)  # a steady 1 s turn while carrying
        time.sleep(.9)
        pointer('stream', -6, 0, 500, 600)  # and back, faster
        time.sleep(.9)
        pointer('button', 272, 0); hold.wait(timeout=10); time.sleep(.3)
        keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'r', '-p', 'Control_L', '-m', 'ctrl')
        wait(lambda: not client.call('recording.status', {})['recording'], timeout=10)
        last = client.call('recording.label', {'label': 'nested carry', 'meta': {'harness': 'feel_record.py'}})['last']['path']
        shutil.copyfile(last, out / ('carry-' + pathlib.Path(last).name))
        print('RECORDED carry', pathlib.Path(last).name, flush=True)
    for i, patch in enumerate(configs):
        name = '-'.join(f'{v}' for v in patch.values())
        client.call('graphics.set', {'patch': {'quality': 'medium', 'dynamic_resolution': True, 'render_scale': 'auto',
                                               'npc_motion': 'always', **patch}})
        want = 'offload' if patch.get('offload', 'off') != 'off' else 'in-process'
        wait(lambda: perf()['activeRenderer'] == want, 60); time.sleep(2)
        if os.environ.get('HYPRUNE_FEEL_RECORD_BASELINE') == '1':
            start_pose(); s.camera()  # clears the presentation log
            movement()
            (out / f'{i}-unrecorded.json').write_text(json.dumps({'config': patch, 'presentLog': s.camera()['presentLog']}))
        start_pose()
        keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'r', '-p', 'Control_L', '-m', 'ctrl')
        wait(lambda: client.call('recording.status', {})['recording'], timeout=10)
        s.camera()  # clears the presentation log: compared with the unrecorded run's
        movement()
        recorded_log = s.camera()['presentLog']
        keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'r', '-p', 'Control_L', '-m', 'ctrl')
        status = wait(lambda: (lambda v: v if not v['recording'] else None)(client.call('recording.status', {})), timeout=10)
        labelled = client.call('recording.label', {'label': f'nested {name}', 'meta': {'harness': 'feel_record.py', 'config': patch}})
        path = pathlib.Path(labelled['last']['path'])
        target = out / f'{i}-{path.name}'
        shutil.copyfile(path, target)
        side = path.with_suffix('.json')
        if side.exists():
            shutil.copyfile(side, target.with_suffix('.json'))
        (out / f'{i}-recorded-presentlog.json').write_text(json.dumps({'config': patch, 'presentLog': recorded_log}))
        stats = [json.loads(l) for l in target.read_text().splitlines() if '"kind":"stats"' in l]
        runs.append({'config': patch, 'recording': target.name, 'renderer': perf()['activeRenderer'],
                     'final': stats[-1] if stats else None})
        print('RECORDED', target.name, json.dumps(runs[-1]['final']), flush=True)
    (out / 'runs.json').write_text(json.dumps(runs, indent=1) + '\n')
    client.call('runtime.exit', {})
    print('PASS feel recordings', len(runs), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, authoring=False, climbing=False, output=str(pathlib.Path(a.output).resolve()),
                             protocol='0.29'))
