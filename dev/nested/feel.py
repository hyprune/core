"""Motion feel: presentation cadence and apparent-motion smoothness. dev/nested/run.sh --feel.

Nested only (run.sh holds /tmp/worldshell-nested.lock); use HYPRUNE_NESTED_4K=1 for the
standard 4K Intel setup. Lumen Reach from ../worlds/dist. For each configuration it walks
(W held) and turns (constant-rate relative pointer motion) and reads the renderer's
presentation log: every presentation is a new frame, a repeated frame (GPU queue full) or
an idle re-present. A repeated frame shows the previous camera, as the eye sees it.

judder = coefficient of variation of the apparent velocity across presentations
(0 = every presented frame advances by the same amount per unit time).
HYPRUNE_FEEL_CONFIGS: JSON list of graphics patches (default: target 30/45/60, dynamic).
HYPRUNE_FEEL_OUT: report path (default <state>/feel.json).
"""
import json, math, os, shutil, statistics, subprocess, time
import session as s
from m2 import keys, wait

LUMEN = 'com.hyprune.lumen-reach'
DEFAULT = [{'target_fps': 30}, {'target_fps': 45}, {'target_fps': 60}]


def prepare(installed):
    shutil.copytree(s.ROOT.parent / 'worlds/dist' / LUMEN, installed / LUMEN, dirs_exist_ok=True)


def analyse(log, kind):
    shown, series, counts = None, [], {0: 0, 1: 0, 2: 0}
    for t, k, yaw, pitch, x, y, z in log:
        counts[int(k)] += 1
        if k == 0:
            shown = (yaw, x, z)
        if shown is not None:
            series.append((t, shown))
    n = len(series)
    series = series[n // 10: n - n // 10]  # ramp-up and ramp-down excluded
    dts, velocity = [], []
    for (t0, a), (t1, b) in zip(series, series[1:]):
        dt = (t1 - t0) / 1000
        if dt <= 0:
            continue
        d = abs(math.remainder(b[0] - a[0], 2 * math.pi)) if kind == 'turn' else math.hypot(b[1] - a[1], b[2] - a[2])
        dts.append(dt * 1000); velocity.append(d / dt)
    span = (log[-1][0] - log[0][0]) / 1000 if len(log) > 1 else 1
    mean_v = statistics.mean(velocity) if velocity else 0
    return {'presentsPerS': round(len(log) / span, 1), 'newPerS': round(counts[0] / span, 1),
            'repeatedPct': round(100 * counts[1] / max(1, len(log)), 1),
            'presentMs': {'mean': round(statistics.mean(dts), 2), 'std': round(statistics.pstdev(dts), 2),
                          'p95': round(sorted(dts)[int(.95 * (len(dts) - 1))], 2)} if dts else None,
            'apparentVelocity': round(mean_v, 3), 'judder': round(statistics.pstdev(velocity) / mean_v, 3) if mean_v else None}


def acceptance(client, children, args, shell_token, socketpath):
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True); children.append(device)
    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush(); assert device.stdout.readline().strip() == 'ok'
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active']); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    configs = json.loads(os.environ.get('HYPRUNE_FEEL_CONFIGS', json.dumps(DEFAULT)))
    report = {'nestedOnly': True, 'world': LUMEN, 'runs': []}
    def camera(): s.ctl('eval', 'hl.plugin.hyprune.camera(-8,1.7,12,0,0)'); time.sleep(1.2)
    for patch in configs:
        client.call('graphics.set', {'patch': {'quality': 'low', 'offload': 'off', 'dynamic_resolution': True, 'render_scale': 'auto', **patch}})
        time.sleep(2)
        run = {'config': patch}
        camera(); s.camera()  # clears the presentation log
        keys('-P', 'w', '-s', '3000', '-p', 'w')
        run['walk'] = analyse(s.camera()['presentLog'], 'walk')
        camera(); s.camera()
        pointer('stream', 3.5, 0, 500, 3000)  # 500 Hz mouse, 1750 px/s, steady
        run['turn'] = analyse(s.camera()['presentLog'], 'turn')
        t = client.call('graphics.get', {})['timings']
        run['timings'] = {k: t.get(k) for k in ['renderScale', 'deliveredFPS', 'frameMs', 'inFlight', 'gpuPassMs']}
        report['runs'].append(run)
        print('MEASURE feel', json.dumps(run), flush=True)
    out = os.environ.get('HYPRUNE_FEEL_OUT', str(s.STATE / 'feel.json'))
    pathlib_out = __import__('pathlib').Path(out); pathlib_out.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS feel report', out, flush=True)
