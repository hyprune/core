#!/usr/bin/env python3
"""A demo video from a recorded session (`recording.start`/`stop` or the record.toggle bind:
hyprune.session JSON lines).

  dev/nested/run.sh session_demo.py --recording REC.jsonl --package PKG --output DIR
      [--shell DIR] [--protocol 0.27] [--mode input|camera] [--smooth-ms N] [--hud on|off]
      [--trim START:END] [--speed START:END=FACTOR ...] [--name demo]

Nested only. The recording's world (PKG, the same installed manifest: replay checks its
digest) loads in a nested session with the real shell (HYPRUNE_SHELL_DIR or --shell), and
core replays it at frame-exact pacing: one recorded tick per presented frame, while the
cinema export hands every presented frame to the encoder (cinema.recording). In `input`
mode (the default) every recorded input runs again, so the HUD, tool switches, the
Director and other shell actions appear as they did; `camera` replays only the view
(`--smooth-ms` smooths it). Then the cut:
  --trim START:END        keep this part (seconds of the recording, or recording.mark names);
  --speed START:END=F     play this part F times faster (F >= 1: frames are dropped evenly,
                          so motion stays smooth) or slower (F < 1, frames repeat).
A hand-held mouse delivers look motion unevenly per tick, and an exact replay keeps that
judder; --smooth-look MS spreads the mouse-look motion over an MS window first (the total
turn and every other input are kept).
The result is encoded for the gallery (DIR/<name>-1080p60.mp4) and goes through the
smoothness gate (DIR/gate.json; segments sped up or slowed down are gated as they play).
DIR/cut.json records the recording's frame-to-video mapping and every part.
"""
import argparse, builtins, json, os, pathlib, shutil, subprocess, sys, time
from types import SimpleNamespace

p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
p.add_argument('--recording', type=pathlib.Path, required=True)
p.add_argument('--package', type=pathlib.Path, required=True)
p.add_argument('--output', type=pathlib.Path, required=True)
p.add_argument('--shell', type=pathlib.Path)
p.add_argument('--protocol', default='0.27')
p.add_argument('--mode', choices=['input', 'camera'], default='input')
p.add_argument('--smooth-ms', type=float, default=0)
p.add_argument('--hud', choices=['on', 'off'], default='on')
p.add_argument('--smooth-look', type=float, default=0, metavar='MS',
               help='input mode: spread mouse-look motion evenly over this window (a hand-held mouse is uneven per tick)')
p.add_argument('--trim')
p.add_argument('--speed', action='append', default=[])
p.add_argument('--name', default='demo')
p.add_argument('--encode-only', action='store_true', help='cut DIR/replay.mkv again with DIR/cut.json (new --trim/--speed)')
a = p.parse_args()
a.output = a.output.resolve(); a.output.mkdir(parents=True, exist_ok=True)
a.recording, a.package = a.recording.resolve(), a.package.resolve()
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import session as s, gauges, cinema, human
from ctl import verified
from m2 import wait

ROWS = [json.loads(line) for line in a.recording.read_text().splitlines() if line.strip()]
HEADER = ROWS[0]
WID = HEADER.get('worldId') or json.loads((a.package / 'world.json').read_text())['id']
FRAMES = [r for r in ROWS if r.get('kind') == 'frame']
MARKS = {r['name']: r for r in ROWS if r.get('kind') == 'mark'}
NS0 = FRAMES[0]['ns'] if FRAMES else 0


def frame_at(spec):
    """A recording frame for a time spec: seconds from the first frame, or a mark name."""
    if spec in MARKS:
        return MARKS[spec]['frame']
    seconds = float(spec)
    ns = NS0 + seconds * 1e9
    return next((f['frame'] for f in FRAMES if f['ns'] >= ns), FRAMES[-1]['frame'])


def parts():
    """The cut: [(first frame, end frame, speed)], in recording frames."""
    lo, hi = 0, FRAMES[-1]['frame'] + 1
    if a.trim:
        start, end = a.trim.split(':')
        lo = frame_at(start) if start else lo
        hi = frame_at(end) if end else hi
    speeds = []
    for item in a.speed:
        span, factor = item.split('=')
        start, end = span.split(':')
        speeds.append((max(lo, frame_at(start)), min(hi, frame_at(end)), float(factor)))
    speeds.sort()
    out, at = [], lo
    for start, end, factor in speeds:
        if start > at:
            out.append((at, start, 1.0))
        if end > start:
            out.append((start, end, factor))
        at = max(at, end)
    if at < hi:
        out.append((at, hi, 1.0))
    return out


def prepare(installed):
    shutil.rmtree(installed / WID, ignore_errors=True)
    shutil.copytree(a.package, installed / WID)
    for name in ['layout.json', 'editor.json', 'input.json']:
        (s.STATE / name).unlink(missing_ok=True)
    rows = human.smooth_recording_look(ROWS, a.smooth_look) if a.mode == 'input' else ROWS
    (s.STATE / 'recording.jsonl').write_text(''.join(json.dumps(r, separators=(',', ':')) + '\n' for r in rows))
    os.chmod(s.STATE / 'recording.jsonl', 0o600)


def acceptance(client, children, args, shell_token, socketpath):
    # The real shell, as a session runs it (HUD, Director).
    read, write = os.pipe(); os.write(write, (shell_token + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL=a.protocol, HYPRUNE_SOCKET=str(socketpath),
                       HYPRUNE_OUTPUT_SCREEN='HEADLESS-1', HYPRUNE_DEV_AUTOMATION='1')
    shell_dir = a.shell or pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', a.protocol], env=environment,
                             pass_fds=(read,), stdout=open(s.STATE / 'shell.log', 'w'), stderr=subprocess.STDOUT)
    os.close(read); children.append(shell)

    def childpid():
        assert shell.poll() is None, 'shell exited; see shell.log'
        return builtins.next((int(q) for q in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
                              if pathlib.Path(f'/proc/{q}/comm').read_text().strip() == 'qs'), None)
    qpid = wait(childpid, 30)

    def ui(*x):
        return subprocess.check_output([str(s.HERE / 'ctl.py'), 'env', 'qs', 'ipc', '--pid', str(qpid), 'call', 'preview', *map(str, x)],
                                       text=True).strip()

    def status():
        try:
            return json.loads(ui('status'))
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            return {}

    s.ctl('eval', 'hl.config({misc={disable_watchdog_warning=true},cursor={inactive_timeout=1}})')
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': WID})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], 60)
    wait(lambda: status().get('lease'), 30)
    if a.hud == 'off':
        ui('hud', 'false')
    client.call('graphics.set', {'patch': {'quality': 'high', 'offload': 'off', 'dynamic_resolution': False, 'render_scale': 1}})
    time.sleep(3)
    cam = cinema.Cinema(s)
    clip = a.output / 'replay.mkv'
    with cam.recording(clip):
        time.sleep(.5)
        grabbed0 = cam.export_status().get('grabbed', 0)
        started = client.call('replay.start', {'path': str(s.STATE / 'recording.jsonl'), 'mode': a.mode, 'smoothingMs': a.smooth_ms})
        done = wait(lambda: (lambda v: v if not v['playing'] else None)(client.call('replay.status', {})), 30 + len(FRAMES) / 30)
        grabbed1 = cam.export_status().get('grabbed', 0)
        time.sleep(.3)
    cut = {'recording': a.recording.name, 'world': WID, 'mode': a.mode, 'frames': len(FRAMES), 'firstVideoFrame': grabbed0,
           'replayFrames': grabbed1 - grabbed0, 'replay': {k: done.get(k) for k in ('frame', 'frames', 'failures', 'error')},
           'parts': [{'frames': [f0, f1], 'speed': sp} for f0, f1, sp in parts()]}
    if done.get('failures') or done.get('error'):
        cut['warning'] = 'the replay reported failures: the world or the shell differs from the recording'
    (a.output / 'cut.json').write_text(json.dumps(cut, indent=1) + '\n')
    encode(clip, cut, grabbed0, (grabbed1 - grabbed0) / max(1, len(FRAMES)))
    client.call('runtime.exit', {})


def encode(clip, cut, first, per_frame):
    """The cut: each part of the replay, trimmed and re-timed (60 fps out), concatenated."""
    fps = 60.0
    filters, labels = [], []
    for i, part in enumerate(cut['parts']):
        f0, f1 = part['frames']
        v0, v1 = (first + f0 * per_frame) / fps, (first + f1 * per_frame) / fps
        speed = part['speed']
        if speed >= 1:  # every speed-th frame, evenly (a resampling filter would pick unevenly at ties)
            retime = f"select='isnan(prev_selected_n)+gte(n-prev_selected_n\\,{speed - 1e-6:.6f})',setpts=N/({fps:g}*TB)"
        else:  # slower: each frame shown 1/speed times (motion repeats: gate it as such)
            retime = f'setpts=(PTS-STARTPTS)/{speed},fps={fps:g}'
        filters.append(f'[0:v]trim=start={v0:.4f}:end={v1:.4f},setpts=PTS-STARTPTS,{retime}[p{i}]')
        labels.append(f'[p{i}]')
    graph = ';'.join(filters) + ';' + ''.join(labels) + f'concat=n={len(labels)}:v=1:a=0[out]'
    out = a.output / f'{a.name}-1080p60.mp4'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(clip), '-filter_complex', graph, '-map', '[out]', *cinema.ENCODE, str(out)], check=True)
    g = cinema.gate(out)
    (a.output / 'gate.json').write_text(json.dumps(g, indent=1) + '\n')
    print('DEMO', out.name, cinema.gate_summary(g), flush=True)


if a.encode_only:
    cut = json.loads((a.output / 'cut.json').read_text())
    cut['parts'] = [{'frames': [f0, f1], 'speed': sp} for f0, f1, sp in parts()]
    (a.output / 'cut.json').write_text(json.dumps(cut, indent=1) + '\n')
    encode(a.output / 'replay.mkv', cut, cut['firstVideoFrame'], cut['replayFrames'] / max(1, cut['frames']))
    sys.exit(0)
gauges.prepare = prepare
gauges.acceptance = acceptance
s.run(SimpleNamespace(serve=False, record=False, m1=False, m2=False, m3a=False, m3b=False, live=False, integration=False, visible=False,
                      authoring=False, climbing=False, gauges=True, protocol=a.protocol, output=str(a.output)))
