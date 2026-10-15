#!/usr/bin/env python3
"""The Hyprune showcase video, driven like a person and rendered frame-exact (nested only).

  dev/nested/run.sh dev/nested/showcase.py --stage take   --output DIR [--seed N]
  dev/nested/run.sh dev/nested/showcase.py --stage render --output DIR

Lumen Reach with the starter library (a gauge) and the official companions (Muse follows).
Three tiled terminals sit on workspace 21, the Sun Court home wall on the facade.

take: a live session driven through core's real input: keys with human timing (wtype) and
  mouse look from human.LookHand (eased turns that overshoot a little and settle, hand
  tremor and drift, smooth steering while walking) played by the nested pointer at 250 Hz.
  Story: from the spawn the player looks round, walks out, places a gauge from the pocket
  (tool 5), crosses the plaza (a pause by the globe, a short sprint), opens a terminal (it
  lands in front), selects it with tool 2, walks to the facade and mounts it on the home
  wall, turns to find the companion, then steps back to take in the wall. Closed loop between actions (each turn is aimed from the camera's pose), open
  loop within one. Core records it (recording.start; the window launch is a mark):
  DIR/take.jsonl.
render: a fresh session in the same state. The opening is the home wall's desktop (a still
  for STILL seconds), then the exported pull-out back into the world, then core replays the
  take (input mode, one recorded tick per presented frame, long ticks split into 1/60 s ones,
  mouse look smoothed over SMOOTH_MS) while every presented frame is exported; the
  terminal is launched again at the take's mark. Output: DIR/showcase-1080p60.mp4,
  DIR/gate.json (the cinema gate over the recorded turns, and the whole video for reference),
  DIR/cut.json.
"""
import argparse, builtins, hashlib, json, math, os, pathlib, shutil, subprocess, sys, time
from types import SimpleNamespace

p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
p.add_argument('--stage', choices=['take', 'render'], required=True)
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--seed', type=int, default=7)
p.add_argument('--shell', type=pathlib.Path)
a = p.parse_args()
a.output = a.output.resolve(); a.output.mkdir(parents=True, exist_ok=True)
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import session as s, gauges, cinema, human
from ctl import verified
from m2 import wait

PROTOCOL = '0.28'
LUMEN = 'com.hyprune.lumen-reach'
STARTER = s.starter_archive('HYPRUNE_CATALOG_LIBRARY')
COMPANIONS = pathlib.Path(os.environ.get('HYPRUNE_COMPANIONS_LIBRARY', str(s.ROOT.parent / 'libraries/com.hyprune.companions-0.1.0.tar.zst')))
GAUGE = 'gauge-tube'
COMPANION = 'neon-cyber-muse'
HOME = 21
# The spawn, framing the home wall (facade screen centre 8, 10.1, -23.1).
START = ([-8, 1.7002, 12], .43, .12)
FACADE = [8, 8.6, -23.1]
STILL, SMOOTH_MS, PULL_SECONDS = 2.4, 70, 1.1
MOVE_INERTIA = .16  # eased starts and stops (player feel setting, recorded with the take)


def prepare(installed):
    shutil.rmtree(installed / LUMEN, ignore_errors=True)
    shutil.copytree(pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE', str(s.ROOT.parent / 'worlds/dist' / LUMEN))), installed / LUMEN)
    for name in ['layout.json', 'editor.json', 'input.json', 'companion.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True); (s.STATE / 'datahome').mkdir(parents=True)
    for archive in (STARTER, COMPANIONS):
        if not archive.is_file():
            raise SystemExit(f'showcase needs {archive} (HYPRUNE_CATALOG_LIBRARY, HYPRUNE_COMPANIONS_LIBRARY)')
        shutil.copy2(archive, s.STATE / archive.name)
    shutil.rmtree(s.STATE / 'statehome/hyprune/recordings', ignore_errors=True)


SYSMON = r'''
import os, time, shutil
def cpu():
    with open('/proc/stat') as f:
        return [list(map(int, l.split()[1:8])) for l in f if l.startswith('cpu') and l[3] != ' ']
prev = cpu()
while True:
    time.sleep(.5); now = cpu(); cols = shutil.get_terminal_size().columns
    out = ['\x1b[H\x1b[1;36m  activity\x1b[0m   load ' + open('/proc/loadavg').read().split()[0], '']
    for i, (a, b) in enumerate(zip(prev[:24], now[:24])):
        busy = sum(b[:3]) - sum(a[:3]); total = max(1, sum(b) - sum(a)); f = busy / total
        n = int(f * (cols - 16)); colour = '32' if f < .5 else '33' if f < .8 else '31'
        out.append(f'  cpu{i:<3}\x1b[{colour}m' + '█' * n + '\x1b[0m' + ' ' * (cols - 16 - n) + f'{f*100:4.0f}%')
    mem = dict(l.split(':', 1) for l in open('/proc/meminfo'))
    used = 1 - int(mem['MemAvailable'].split()[0]) / int(mem['MemTotal'].split()[0])
    out += ['', f'  memory \x1b[35m' + '█' * int(used * (cols - 16)) + f'\x1b[0m {used*100:.0f}%']
    print('\n'.join(out), end='', flush=True); prev = now
'''


def terminals(children):
    """Three terminals tiled on the home workspace, mapped silently, and the script for the one
    opened in the world."""
    (s.STATE / 'sysmon.py').write_text(SYSMON)
    s.ctl('eval', f'hl.window_rule({{name="showcase-home",match={{class="^showcase-home$"}},workspace="{HOME} silent"}})')
    log = f"git -C {s.ROOT} log --graph --color=always --format='%C(yellow)%h%Creset %s' -60; exec bash --noprofile --norc"
    notes = ("printf '\\n  \\e[1;33mSun Court\\e[0m\\n\\n  home wall: workspace 21\\n  tools: 1 navigate  2 mount  5 place\\n"
             "  Super+drag: carry\\n\\n'; exec bash --noprofile --norc")
    for title, colour, cmd in [('log', '#141c26', log), ('activity', '#1a1426', f'exec python3 {s.STATE / "sysmon.py"}'), ('notes', '#12201a', notes)]:
        children.append(s.envrun('kitty', '--class', 'showcase-home', '--title', title, '--override', 'background=' + colour,
                                 '--override', 'font_size=15', '--override', 'cursor_blink_interval=0', 'bash', '--noprofile', '--norc', '-c', cmd,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        time.sleep(.6)


def open_window(children):
    children.append(s.envrun('kitty', '--class', 'showcase-new', '--title', 'activity', '--override', 'background=#101820',
                             '--override', 'font_size=16', '--override', 'cursor_blink_interval=0', 'python3', str(s.STATE / 'sysmon.py'),
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))


def setup(client, children):
    """World, libraries, shell, home terminals, the companion and the gauge in the pocket; the
    player at START. Returns (state, ui, status)."""
    read, write = os.pipe()
    shell_token = getattr(setup, 'token')
    os.write(write, (shell_token + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL=PROTOCOL, HYPRUNE_SOCKET=str(setup.socket),
                       HYPRUNE_OUTPUT_SCREEN='HEADLESS-1', HYPRUNE_DEV_AUTOMATION='1')
    shell_dir = a.shell or pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', PROTOCOL], env=environment,
                             pass_fds=(read,), stdout=open(s.STATE / 'shell.log', 'w'), stderr=subprocess.STDOUT)
    os.close(read); children.append(shell)

    def childpid():
        assert shell.poll() is None, 'shell exited; see shell.log'
        return builtins.next((int(q) for q in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
                              if pathlib.Path(f'/proc/{q}/comm').read_text().strip() == 'qs'), None)
    qpid = wait(childpid, 30)

    def ui(*x):
        return subprocess.check_output([str(s.HERE / 'ctl.py'), 'env', 'qs', 'ipc', '--pid', str(qpid), 'call', 'preview', *map(str, x)], text=True).strip()

    def status():
        try:
            return json.loads(ui('status'))
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            return {}
    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': LUMEN})
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    for archive in (STARTER, COMPANIONS):
        copy = s.STATE / archive.name
        client.call('libraries.install', {'archive': str(copy), 'sha256': hashlib.sha256(copy.read_bytes()).hexdigest()})
    terminals(children)
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], 60)
    wait(lambda: status().get('lease'), 30)
    client.call('graphics.set', {'patch': {'quality': 'high', 'offload': 'off', 'dynamic_resolution': False, 'render_scale': 1}})
    s.ctl('eval', f'hl.plugin.hyprune.config({{player={{move_inertia={MOVE_INERTIA}}}}})')
    s.ctl('eval', f'hl.plugin.hyprune.config({{windows={{transition_seconds={PULL_SECONDS}}}}})')
    pose(*START); time.sleep(1)
    version = lambda archive: archive.name.rsplit('-', 1)[1].removesuffix('.tar.zst')
    client.call('companion.set', {'ref': f'library/com.hyprune.companions@{version(COMPANIONS)}/{COMPANION}'})
    client.call('objects.pocket', {'ref': f'library/com.hyprune.starter@{version(STARTER)}/{GAUGE}'})
    # The companion joins out of view; let it come up and settle behind the player.
    time.sleep(6); pose(*START); time.sleep(1.5)
    return state, ui, status


def pose(eye, yaw, pitch):
    s.ctl('eval', f'hl.plugin.hyprune.camera({eye[0]},{eye[1]},{eye[2]},{yaw},{pitch})')


def keys_env():
    return verified()[1]


def wtype(*args):
    """wtype in the nested session, started now (returns the process)."""
    return subprocess.Popen(['wtype', *args], env=keys_env(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def sprint_key():
    def items(j):
        if isinstance(j, dict):
            for k, v in j.items():
                yield k, v
                yield from items(v)
    bindings = json.loads((s.ROOT / 'config/input.default.json').read_text())
    return next(v for k, v in items(bindings) if k == 'sprint')[0].split('[')[0]


def angles(eye, target):
    d = [target[i] - eye[i] for i in range(3)]
    return math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2]))


def take(client, children):
    import random
    state, ui, status = setup(client, children)
    rng = random.Random(a.seed)
    hand = human.LookHand(hz=250, seed=a.seed)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)
    playfile = s.STATE / 'showcase-hand.txt'

    def pointer(*x):
        device.stdin.write(' '.join(map(str, x)) + '\n'); device.stdin.flush(); assert device.stdout.readline().strip() == 'ok'

    def play(events):
        playfile.write_text(''.join(f'{dx:.4f} {dy:.4f}\n' for dx, dy in events)); pointer('play', hand.hz, playfile)

    cam = lambda: s.camera()

    def look(yaw, pitch, seconds=None):
        c = cam(); play(hand.turn(human.wrap(yaw - c['yaw']), pitch - c['pitch'], seconds))

    def look_at(point, seconds=None, miss=0.0):
        c = cam(); y, pt = angles(c['position'], point)
        look(y + rng.uniform(-miss, miss), pt + rng.uniform(-miss, miss) * .5, seconds)

    def rest(seconds):
        play(hand.rest(seconds))

    def tap(key):
        wtype('-k', key).wait(); rest(rng.uniform(.18, .3))

    def click():
        pointer('button', 272, 1); play(hand.rest(rng.uniform(.08, .12))); pointer('button', 272, 0)

    def walk(target, speed=4.0, sprint=None, wobble=.1, pitch=None, turn_in=True):
        """Walk (forward key) to target, the way a person does: the turn towards it starts as
        the feet do (the path curves in), what is left of the heading error is steered out
        smoothly, and the view sways gently. sprint=(start, seconds) holds the sprint key in the
        same key sequence (one virtual keyboard)."""
        c = cam(); here = c['position']
        dist = math.hypot(target[0] - here[0], target[2] - here[2])
        run = 10.0  # default run_speed
        if sprint:
            seconds = max(sprint[0] + sprint[1] + .3, (dist - run * sprint[1]) / speed + sprint[1])
            keys = ['-P', 'w', '-s', str(int(sprint[0] * 1000)), '-P', sprint_key(), '-s', str(int(sprint[1] * 1000)),
                    '-p', sprint_key(), '-s', str(int((seconds - sprint[0] - sprint[1]) * 1000)), '-p', 'w']
        else:
            seconds = max(.4, dist / speed)
            keys = ['-P', 'w', '-s', str(int(seconds * 1000)), '-p', 'w']
        held = wtype(*keys)
        want, _ = angles(here, target)
        err = human.wrap(want - c['yaw'])
        dp = (pitch - c['pitch']) if pitch is not None else 0.0
        events, used = [], 0.0
        if turn_in and abs(err) > .1:
            part = err * rng.uniform(.88, .96)
            events = hand.turn(part, dp * .7)
            used = len(events) / hand.hz
            err, dp = err - part, dp * .3
        left = max(.3, seconds - used)
        n = max(3, int(left / .25))
        phase, slow = rng.uniform(0, math.tau), rng.uniform(.18, .3)
        for i in range(n):
            u0, u1 = i / n, (i + 1) / n
            corr = err * (human.min_jerk(min(1, u1 / .6)) - human.min_jerk(min(1, u0 / .6))) / (left / n)
            sway = wobble * math.sin(phase + math.tau * slow * left * u0) + .4 * wobble * math.sin(2.7 * phase + math.tau * .7 * left * u0)
            events += hand.steer(corr + sway, left / n, dp / left)
        events += hand.steer(0.0, .35)
        play(events)
        held.wait()
        rest(.25)

    def mark(name):
        client.call('recording.mark', {'name': name})

    # Take.
    eye = START[0]
    client.call('recording.start', {})
    rest(1.1)
    look(START[1] - .85, -.02)               # the pavilion and the field rigs on the left
    rest(.7)
    look(START[1] + .25, .06, .9)            # back across to the plaza and the hall
    rest(.5)
    look_at([-7.4, 1.4, 4], .7)              # down the way ahead
    walk([-7.4, 1.7, 8.2], wobble=.06, pitch=-.1)
    tap('5')                                 # tool 5: the gauge in the pocket shows its ghost
    look_at([-8.6, 0, 4.4], .8)
    for _ in range(6):                       # nudge onto a valid spot, as a person would
        g = cam()['objects']['ghost']
        if g.get('valid'):
            break
        play(hand.turn(rng.uniform(-.12, .12), rng.uniform(-.05, .02), .35)); rest(.2)
    rest(.35); click(); rest(1.1)
    mark('gauge')
    look_at([-8.6, .9, 4.4], .7)             # the gauge comes up: a closer look at its reading
    rest(.9)
    tap('1')
    look_at(FACADE, .9, miss=.04)            # up at the home wall
    rest(.6)
    walk([-4.5, 1.7, 4.6], pitch=0.0)
    look_at([-7, 4.2, -10], .8)              # a pause by the globe
    rest(.8)
    walk([4, 1.7, -6], sprint=(.5, 1.0), pitch=0.0)
    look(cam()['yaw'] + 1.0, .02, .9)        # to the right, towards the kiosk
    rest(.4)
    mark('window')
    open_window(children)
    rest(1.6)
    new = wait(lambda: next((c for c in cam()['captures'] if c.get('title') == 'activity' and c.get('workspaceId') != HOME and c.get('placed')), None), 15)
    rest(.4)
    tap('2')                                 # tool 2: select the window, then a wall to mount it on
    look_at(new['position'], .55, miss=.02)
    rest(.25); click(); rest(.6)
    walk([8, 1.7, -15], pitch=.05)
    look_at(FACADE, 1.0)
    for _ in range(4):
        r = cam()['reticle']
        if r['action'] == 'mount' and r['state'] == 'valid':
            break
        look_at(FACADE, .4)
    rest(.3); click(); rest(1.6)
    mark('mounted')
    tap('1')
    look(cam()['yaw'] + math.pi * .92, -.08, 1.35)   # turn round: the plaza, the gauge, the companion
    rest(1.0)
    comp = cam().get('companion') or {}
    if comp.get('position'):
        look_at([comp['position'][0], comp['position'][1] + .9, comp['position'][2]], .8)
    rest(1.6)
    look_at(FACADE, 1.3)                     # back to the wall, and a few steps back to take it in
    rest(.3)
    back = wtype('-P', 's', '-s', '1500', '-p', 's')
    play(hand.steer(0.0, 1.6, -.12 / 1.6) + hand.steer(0.0, .4))
    back.wait()
    rest(1.6)
    stop = client.call('recording.stop', {})
    path = pathlib.Path(stop['path'])
    wait(lambda: path.is_file() and path.stat().st_size > 0, 20)
    shutil.copy2(path, a.output / 'take.jsonl')
    rows = [json.loads(l) for l in (a.output / 'take.jsonl').read_text().splitlines() if l.strip()]
    frames = [r for r in rows if r.get('kind') == 'frame']
    dts = [r['dt'] for r in frames if 'dt' in r]
    info = {'frames': len(frames), 'seconds': round(sum(dts), 2), 'marks': {r['name']: r['frame'] for r in rows if r.get('kind') == 'mark'},
            'dt': {'min': min(dts), 'max': max(dts), 'mean': sum(dts) / len(dts)}, 'seed': a.seed}
    (a.output / 'take.json').write_text(json.dumps(info, indent=1) + '\n')
    print('TAKE', json.dumps(info), flush=True)
    client.call('runtime.exit', {})


def split(rows):
    """Long ticks (a missed refresh, an asset load) split into 1/60 s sub-ticks: the tick's
    inputs go to the first, its state to the last, the total time stays. One replayed tick per
    presented frame then moves evenly, and every position matches the take."""
    ks = {r['frame']: max(1, round(r['dt'] * 60)) for r in rows if r.get('kind') == 'frame'}
    first, at = {}, 0
    for f in sorted(ks):
        first[f] = at; at += ks[f]
    last = lambda f: first[f] + ks[f] - 1 if f in ks else at - 1
    out, ns = [], 0.0
    for r in rows:
        r = dict(r); kind = r.get('kind')
        if kind == 'input':
            r['frame'] = first.get(r['frame'], at - 1)
        elif kind in ('mark', 'view'):
            r['frame'] = last(r['frame'])
        elif kind == 'end':
            r['frames'] = at
        if kind == 'frame':
            k, dt = ks[r['frame']], r['dt']
            for i in range(k):
                sub = dict(r, frame=first[r['frame']] + i, dt=dt / k, tickNs=ns)
                ns += dt / k * 1e9
                out.append(sub)
            continue
        out.append(r)
    return out


def render(client, children):
    take_rows = [json.loads(l) for l in (a.output / 'take.jsonl').read_text().splitlines() if l.strip()]
    rows = human.smooth_recording_look(split(take_rows), SMOOTH_MS)
    marks = {r['name']: r['frame'] for r in rows if r.get('kind') == 'mark'}
    frames = [r for r in rows if r.get('kind') == 'frame']
    replay = s.STATE / 'showcase.jsonl'
    replay.write_text(''.join(json.dumps(r, separators=(',', ':')) + '\n' for r in rows)); os.chmod(replay, 0o600)
    state, ui, status = setup(client, children)
    cam = cinema.Cinema(s)
    client.call('workspace.focus', {'id': HOME})
    wait(lambda: state()['mounts']['desktopWorkspaceId'] == HOME, 20); time.sleep(2.5)
    still = a.output / 'desktop.png'
    subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(still)], check=True)
    clip = a.output / 'render.mkv'
    with cam.recording(clip, warmup=.3):
        g_pull = cam.export_status().get('grabbed', 0)
        wtype('-M', 'logo', '-P', 'Super_L', '-k', 'Tab', '-p', 'Super_L', '-m', 'logo').wait()
        wait(lambda: state()['mounts']['desktopWorkspaceId'] is None, 10)
        time.sleep(PULL_SECONDS + .25)
        g0 = cam.export_status().get('grabbed', 0)
        client.call('replay.start', {'path': str(replay), 'mode': 'input', 'smoothingMs': 0})
        launched = False
        while True:
            st = client.call('replay.status', {})
            if not launched and st['frame'] >= marks.get('window', 1 << 60):
                open_window(children); launched = True
            if not st['playing']:
                break
            time.sleep(.05)  # the dev wire audit validates every frame: don't flood it
        g1 = cam.export_status().get('grabbed', 0)
        time.sleep(.3)
    cut = {'take': 'take.jsonl', 'frames': len(frames), 'marks': marks, 'stillSeconds': STILL, 'pullFirstFrame': g_pull,
           'replayFirstFrame': g0, 'replayFrames': g1 - g0, 'replay': {k: st.get(k) for k in ('frame', 'failures', 'error')},
           'smoothLookMs': SMOOTH_MS, 'splitTicks': True}
    (a.output / 'cut.json').write_text(json.dumps(cut, indent=1) + '\n')
    encode(still, clip, cut, frames)
    client.call('runtime.exit', {})


def encode(still, clip, cut, frames):
    fps = 60
    out = a.output / 'showcase-1080p60.mp4'
    start = cut['pullFirstFrame'] / fps
    end = (cut['replayFirstFrame'] + cut['replayFrames']) / fps + .2
    graph = (f'[0:v]loop=loop={int(STILL * fps)}:size=1:start=0,setpts=N/({fps}*TB),format=yuv420p[a];'
             f'[1:v]trim=start={start:.4f}:end={end:.4f},setpts=PTS-STARTPTS,format=yuv420p[b];[a][b]concat=n=2:v=1:a=0[out]')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(still), '-i', str(clip), '-filter_complex', graph, '-map', '[out]', '-r', str(fps),
                    *cinema.ENCODE, str(out)], check=True)
    offset = STILL + (cut['replayFirstFrame'] - cut['pullFirstFrame']) / fps
    g = gate(out, frames, offset)
    (a.output / 'gate.json').write_text(json.dumps(g, indent=1) + '\n')
    print('SHOWCASE', out.name, g['summary'], flush=True)


def gate(out, frames, offset, min_speed=3.0, min_seconds=.6):
    """The cinema gate where the recorded camera turns (>= min_speed deg/s for >= min_seconds),
    at their place in the video; the whole video's figures for reference."""
    spans, run = [], None
    for i, (f0, f1) in enumerate(zip(frames, frames[1:])):
        c0, c1 = f0['state']['camera'], f1['state']['camera']
        speed = math.degrees(math.hypot(human.wrap(c1['yaw'] - c0['yaw']), c1['pitch'] - c0['pitch'])) * 60
        if speed >= min_speed:
            run = i if run is None else run
        elif run is not None:
            if (i - run) / 60 >= min_seconds:
                spans.append([round(offset + run / 60, 3), round(offset + i / 60, 3)])
            run = None
    parts = [dict(cinema.gate(out, s0, s1), span=[s0, s1]) for s0, s1 in spans]
    measured = [g for g in parts if g.get('pass') is not None]
    result = {'rule': 'cinema gate per recorded turn (>= 3 deg/s for >= 0.6 s)', 'turns': parts, 'whole': cinema.gate(out)}
    if measured:
        frames_ = sum(g['motionFrames'] for g in measured)
        result.update(motionFrames=frames_, duplicatePct=round(sum(g['duplicatePct'] * g['motionFrames'] for g in measured) / max(1, frames_), 2),
                      stepCV=max(g['stepCV'] for g in measured), **{'pass': all(g['pass'] for g in measured)})
        result['summary'] = (f"{'PASS' if result['pass'] else 'FAIL'} over {len(measured)} turns: duplicates {result['duplicatePct']}%, "
                             f"worst step CV {result['stepCV']}; whole video: {cinema.gate_summary(result['whole'])}")
    else:
        result.update({'pass': None, 'summary': 'no recorded turn to gate; whole video: ' + cinema.gate_summary(result['whole'])})
    return result


def acceptance(client, children, args, shell_token, socketpath):
    setup.token, setup.socket = shell_token, socketpath
    (take if a.stage == 'take' else render)(client, children)


gauges.prepare = prepare
gauges.acceptance = acceptance
s.run(SimpleNamespace(serve=False, record=False, m1=False, m2=False, m3a=False, m3b=False, live=False, integration=False, visible=False,
                      authoring=False, climbing=False, gauges=True, protocol=PROTOCOL, output=str(a.output),
                      # A video, not a conformance run: the per-frame IPC schema audit falls behind
                      # under the frame export (state deltas every tick) and stops hypruned by design.
                      wire_audit=False))
