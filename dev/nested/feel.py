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
    # The frames themselves: their spacing (cadence) and the motion each one adds. A steady
    # 30 fps on a 60 Hz display repeats every other presentation yet moves evenly.
    new = [e for e in log if e[1] == 0]
    new = new[len(new) // 10: len(new) - len(new) // 10]
    gaps, steps = [], []
    for a, b in zip(new, new[1:]):
        dt = (b[0] - a[0]) / 1000
        if dt <= 0:
            continue
        d = abs(math.remainder(b[2] - a[2], 2 * math.pi)) if kind == 'turn' else math.hypot(b[4] - a[4], b[6] - a[6])
        gaps.append(dt * 1000); steps.append(d / dt)
    cv = lambda v: round(statistics.pstdev(v) / statistics.mean(v), 3) if v and statistics.mean(v) else None
    span = (log[-1][0] - log[0][0]) / 1000 if len(log) > 1 else 1
    mean_v = statistics.mean(velocity) if velocity else 0
    return {'presentsPerS': round(len(log) / span, 1), 'newPerS': round(counts[0] / span, 1),
            'repeatedPct': round(100 * counts[1] / max(1, len(log)), 1),
            'presentMs': {'mean': round(statistics.mean(dts), 2), 'std': round(statistics.pstdev(dts), 2),
                          'p95': round(sorted(dts)[int(.95 * (len(dts) - 1))], 2)} if dts else None,
            'apparentVelocity': round(mean_v, 3), 'judder': round(statistics.pstdev(velocity) / mean_v, 3) if mean_v else None,
            'frameCadenceCV': cv(gaps), 'frameMotionCV': cv(steps), 'frameGapMs': round(statistics.mean(gaps), 2) if gaps else None}


def latency(pointer, camera):
    """Input to the first newly rendered frame that shows the motion (render start, same
    steady clock as the input timestamps). Add GPU frame time and queued frames for photons."""
    out = {}
    for name, act, key in [('key', lambda: keys('-P', 'w', '-s', '250', '-p', 'w'), 'w'), ('mouse', lambda: pointer('stream', 3.5, 0, 500, 200), 'MouseMove')]:
        samples = []
        for _ in range(10):
            camera(); s.camera(); act(); time.sleep(.3); d = s.camera()
            press = max((e['timeMs'] for e in d.get('inputRecent', []) if e['key'] == key and e['pressed']), default=None)
            if press is None:
                continue
            # Render on demand presents nothing while still: compare with the pose camera() set.
            moved = next((e for e in d['presentLog'] if e[1] == 0 and e[0] >= press and (abs(e[2]) > 1e-4 or abs(e[4] + 8) + abs(e[6] - 12) > 1e-4)), None)
            if moved:
                samples.append(moved[0] - press)
        out[name] = {'n': len(samples), 'medianMs': round(statistics.median(samples), 1), 'maxMs': round(max(samples), 1)} if samples else None
    return out


def video(client, children, pointer, configs):
    """Same scripted motion per configuration (2.5 s turn, 2.5 s walk), recorded at 60 fps
    into HYPRUNE_FEEL_VIDEO/<n>.mkv for before/after comparisons."""
    import signal
    out = __import__('pathlib').Path(os.environ['HYPRUNE_FEEL_VIDEO']); out.mkdir(parents=True, exist_ok=True)
    for i, patch in enumerate(configs):
        client.call('graphics.set', {'patch': {'quality': 'low', 'offload': 'off', 'dynamic_resolution': True, 'render_scale': 'auto', **patch}})
        time.sleep(2); s.ctl('eval', 'hl.plugin.hyprune.camera(-8,1.7,12,0,0)'); time.sleep(1.2)
        rec = s.envrun('wf-recorder', '-o', 'HEADLESS-1', '-r', '60', '-F', 'scale=1920:-2', '-c', 'libx264', '-p', 'preset=ultrafast', '-p', 'crf=20',
                       '-f', str(out / f'{i}.mkv'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        children.append(rec); time.sleep(1)
        pointer('stream', 3.5, 0, 500, 2500)
        keys('-P', 'w', '-s', '2500', '-p', 'w'); time.sleep(.5)
        rec.send_signal(signal.SIGINT)
        # wf-recorder handles the signal on its next captured frame; render on demand
        # presents nothing while still, so nudge the view until it has finished.
        for n in range(1200):
            if rec.poll() is not None:
                break
            s.ctl('eval', f'hl.plugin.hyprune.camera(-8,1.7,12,{1e-4 * (n % 2)},0)'); time.sleep(.1)
        print('PASS feel video', out / f'{i}.mkv', json.dumps(patch), flush=True)


def wall(client, children):
    """Wall clarity: a terminal of dense text on the Sun Court home (21), photographed at
    8 m (the 16x9 m wall spans ~one 4K screen there) and 5 m, plus the same workspace
    focused in stock 2D as the native reference. Stills go to HYPRUNE_FEEL_SHOTS."""
    out = __import__('pathlib').Path(os.environ.get('HYPRUNE_FEEL_SHOTS', str(s.STATE / 'wall')));out.mkdir(parents=True, exist_ok=True)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    s.ctl('eval', 'hl.window_rule({name="wall-text",match={title="^Wall-Text$"},workspace="21 silent"})')
    text = 'for i in $(seq 1 60); do printf "%03d The quick brown fox jumps over the lazy dog 0123456789 ~!@#$%%^&*()[]{}\\n" $i; done; exec sleep 3600'
    children.append(s.envrun('kitty', '--title', 'Wall-Text', '--override', 'font_size=11', '--override', 'cursor_blink_interval=0', 'bash', '--noprofile', '--norc', '-c', text,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    wait(lambda: any(c['title'] == 'Wall-Text' and c['workspaceId'] == 21 for c in s.camera()['captures']), timeout=20); time.sleep(2)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    st = client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    anchors = {a['id']: a for a in st['mounts']['anchors']}; areas = {a['id']: a for a in st['mounts']['areas']}
    a = anchors[areas['sun-court']['primaryAnchor']]; x, y, z, w = a['pose']['rotation']; p = a['pose']['position']
    n = [2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)]
    for d in (8, 5):
        s.ctl('eval', f'hl.plugin.hyprune.camera({p[0] + n[0] * d},{p[1]},{p[2] + n[2] * d},{math.atan2(-n[0], n[2])},0)'); time.sleep(1.5)
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / f'wall-{d}m.png')], check=True)
    client.call('workspace.focus', {'id': 21}); time.sleep(2.5)
    subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / 'wall-native.png')], check=True)
    client.call('workspace.unfocus', {}); time.sleep(1)
    print('PASS feel wall shots', out, flush=True)


def acceptance(client, children, args, shell_token, socketpath):
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True); children.append(device)
    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush(); assert device.stdout.readline().strip() == 'ok'
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active']); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    if os.environ.get('HYPRUNE_FEEL_WALL') == '1':
        return wall(client, children)
    configs = json.loads(os.environ.get('HYPRUNE_FEEL_CONFIGS', json.dumps(DEFAULT)))
    if os.environ.get('HYPRUNE_FEEL_VIDEO'):
        return video(client, children, pointer, configs)
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
        run['latency'] = latency(pointer, camera)
        t = client.call('graphics.get', {})['timings']
        run['timings'] = {k: t.get(k) for k in ['renderScale', 'deliveredFPS', 'frameMs', 'inFlight', 'gpuPassMs']}
        report['runs'].append(run)
        print('MEASURE feel', json.dumps(run), flush=True)
    out = os.environ.get('HYPRUNE_FEEL_OUT', str(s.STATE / 'feel.json'))
    pathlib_out = __import__('pathlib').Path(out); pathlib_out.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS feel report', out, flush=True)
