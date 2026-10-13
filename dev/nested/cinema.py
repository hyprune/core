#!/usr/bin/env python3
"""Smooth demo cameras for nested recordings, and the smoothness gate.

The camera moves inside core, per presented frame (hl.plugin.hyprune.cinema, a dev
hook that nested builds register; not a wire method). Python only hands over a
whole track or a follow target, then waits. recording() has core export every
presented frame of HEADLESS-1 to ffmpeg (x264 at exactly 60 fps, one video frame
per presented frame; time in core is counted in presented frames meanwhile).
wf-recorder stays available (method='wf-recorder') but misses frames on the
headless nested output. Every recording should go through gate().

  cinema.py gate VIDEO [--json OUT] [--start S] [--end S]
      Decodes VIDEO, finds where the camera moves and measures, there, the frames
      in which it did not advance (duplicates) and the evenness of its steps.

In a session (dev/nested/run.sh SCRIPT):
  cam = cinema.Cinema(session_module)
  cam.path([{'t': 0, 'eye': [...], 'target': [...]}, ...], blend_ms=400)
  cam.follow('resident-id', back=3.4, side=1.2, height=1.9, ahead=1.5, half_life=.35)
  cam.wait()        # until a path is done
  with cam.recording(out_mkv): ...
  cinema.gate(out_mkv)   # duplicates during motion < 1 %, step CV < 0.15
"""
import argparse, contextlib, json, math, os, pathlib, signal, subprocess, sys, tempfile, time

# The demos' former wf-recorder settings (1080p60, x264), for method='wf-recorder'.
# Captures go to .mkv so a recorder that is not finalised still leaves a playable file.
RECORD = ['-r', '60', '-c', 'libx264', '-p', 'preset=veryfast', '-p', 'crf=16']
# The final cut / gallery encode.
ENCODE = ['-c:v', 'libx264', '-preset', 'slow', '-crf', '18', '-pix_fmt', 'yuv420p', '-color_range', 'tv', '-movflags', '+faststart']

# Gate limits, during camera motion.
MAX_DUPLICATE = .01  # frames in which the camera did not advance
MAX_STEP_CV = .15    # coefficient of variation of the step against its local mean


# ---------------------------------------------------------------- smoothness gate
def _frames(video, width, height, start=None, end=None):
    cmd = ['ffmpeg', '-v', 'error']
    if start is not None:
        cmd += ['-ss', str(start)]
    cmd += ['-i', str(video)]
    if end is not None:
        cmd += ['-t', str(end - (start or 0))]
    cmd += ['-an', '-vf', f'scale={width}:{height}:flags=area,format=gray', '-f', 'rawvideo', '-']
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    size = width * height
    import numpy as np
    while True:
        buf = proc.stdout.read(size)
        if len(buf) < size:
            break
        yield np.frombuffer(buf, np.uint8).reshape(height, width).astype(np.float32)
    proc.wait()


def _fps(video):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=r_frame_rate',
                          '-of', 'csv=p=0', str(video)], capture_output=True, text=True, check=True).stdout.strip()
    a, b = out.split('/')
    return float(a) / float(b)


def _shift(a, b, window):
    """Sub-pixel translation of tile b against tile a (phase correlation) and peak strength."""
    import numpy as np
    fa, fb = np.fft.rfft2((a - a.mean()) * window), np.fft.rfft2((b - b.mean()) * window)
    cross = fa * np.conj(fb)
    cross /= np.abs(cross) + 1e-6
    r = np.fft.irfft2(cross, s=a.shape)
    py, px = np.unravel_index(np.argmax(r), r.shape)
    h, w = r.shape

    def sub(c, l, rr):
        d = l - 2 * c + rr
        return 0. if abs(d) < 1e-9 else .5 * (l - rr) / d
    dy = py + sub(r[py, px], r[(py - 1) % h, px], r[(py + 1) % h, px])
    dx = px + sub(r[py, px], r[py, (px - 1) % w], r[py, (px + 1) % w])
    dy = dy - h if dy > h / 2 else dy
    dx = dx - w if dx > w / 2 else dx
    return math.hypot(dx, dy), float(r[py, px])


def measure(video, start=None, end=None, width=640, height=360, grid=(4, 3), matches=False):
    """Per-frame camera step (median tile shift, pixels at 640x360), mean absolute difference
    and (matches=True) the share of textured tiles whose shift was found."""
    import numpy as np
    tw, th = width // grid[0], height // grid[1]
    window = np.outer(np.hanning(th), np.hanning(tw)).astype(np.float32)
    steps, mads, found, prev = [], [], [], None
    for f in _frames(video, width, height, start, end):
        if prev is not None:
            mads.append(float(np.abs(f - prev).mean()))
            shifts, textured = [], 0
            for gy in range(grid[1]):
                for gx in range(grid[0]):
                    sl = (slice(gy * th, (gy + 1) * th), slice(gx * tw, (gx + 1) * tw))
                    a, b = prev[sl], f[sl]
                    if a.std() < 2 and b.std() < 2:  # flat tile (sky, wall): no information
                        continue
                    textured += 1
                    d, peak = _shift(a, b, window)
                    if peak > .08:
                        shifts.append(d)
            steps.append(float(np.median(shifts)) if shifts else 0.)
            found.append(len(shifts) / textured if textured else 1.)
        prev = f
    if matches:
        return np.array(steps), np.array(mads), np.array(found)
    return np.array(steps), np.array(mads)


def _rolling(x, n):
    import numpy as np
    k = np.ones(n) / n
    pad = n // 2
    return np.convolve(np.pad(x, pad, mode='edge'), k, mode='valid')[:len(x)]


def gate(video, start=None, end=None, max_duplicate=MAX_DUPLICATE, max_cv=MAX_STEP_CV):
    """Smoothness of the camera in a recording.

    Motion segments: runs of frames whose step (median tile shift at 640x360) exceeds
    0.08 px, joined across still gaps of up to 0.2 s (a stutter's held frames) and split
    at cuts (a large difference that most tiles cannot match as a shift); segments under 0.5 s
    are ignored and 0.15 s is trimmed at both ends so starts and stops may ease. There, a
    frame is a duplicate when its step is under a quarter of the local (0.25 s) mean, and
    the step CV is the spread of step / local mean."""
    import numpy as np
    from numpy.lib.stride_tricks import sliding_window_view as windows
    fps = _fps(video)
    steps, mads, found = measure(video, start, end, matches=True)
    if not len(steps):
        return {'video': str(video), 'error': 'no frames'}
    n = len(steps)
    # A cut changes the picture: most textured tiles no longer find their content shifted.
    cut = (mads > 8) & (found < .35)
    moving = (steps > .08) & ~cut
    gap, edge, shortest = int(round(fps * .2)), max(1, int(round(fps * .15))), fps * .5
    segments, i = [], 0
    while i < n:
        if not moving[i]:
            i += 1
            continue
        j = last = i
        while j < n and not cut[j] and j - last <= gap:
            if moving[j]:
                last = j
            j += 1
        if last + 1 - i >= shortest:
            segments.append((i + edge, last + 1 - edge))
        i = max(last + 1, j if j < n and cut[j] else last + 1)
    clean = np.where(cut, np.nan, steps)
    w = max(3, int(round(fps * .25)) | 1)
    local = np.nanmean(windows(np.pad(clean, w // 2, mode='edge'), w), axis=1)
    idx = np.concatenate([np.arange(a, b) for a, b in segments if b > a]) if segments else np.array([], int)
    out = {'video': str(video), 'fps': f'{fps:g}', 'frames': int(n + 1), 'cuts': int(cut.sum()),
           'motionFrames': int(len(idx)), 'segments': [[round(a / fps + (start or 0), 2), round(b / fps + (start or 0), 2)] for a, b in segments]}
    if len(idx):
        ratio = steps[idx] / np.maximum(local[idx], 1e-6)
        dup = ratio < .25
        out['duplicatePct'] = round(100 * float(dup.mean()), 2)
        out['identicalPct'] = round(100 * float((mads[idx] < .25).mean()), 2)
        out['stepCV'] = round(float(ratio.std() / max(ratio.mean(), 1e-6)), 3)
        out['meanStepPx'] = round(float(steps[idx].mean()), 3)
        # Distinct camera positions per second during motion (the effective camera rate).
        out['cameraHz'] = round(fps * (1 - float(dup.mean())), 1)
        out['worst'] = [round(float(idx[k]) / fps + (start or 0), 2) for k in np.argsort(-np.abs(ratio - 1))[:5]]
        out['pass'] = bool(dup.mean() < max_duplicate and out['stepCV'] < max_cv)
    else:
        out['pass'] = None  # nothing moved
    return out


# ---------------------------------------------------------------- session helpers
class Cinema:
    """Core-driven camera for a nested session (s = the session module)."""

    def __init__(self, s):
        self.s = s

    def _call(self, request):
        req, out = self.s.STATE / 'cinema-request.json', self.s.STATE / 'cinema-reply.json'
        req.write_text(json.dumps(request))
        out.unlink(missing_ok=True)
        self.s.ctl('eval', f'local i = assert(io.open({json.dumps(str(req))})); local r = hl.plugin.hyprune.cinema(i:read("a")); '
                           f'i:close(); local f = assert(io.open({json.dumps(str(out))}, "w")); f:write(r); f:close()')
        reply = json.loads(out.read_text())
        if reply.get('error'):
            raise RuntimeError(f'cinema: {reply["error"]}')
        return reply

    def path(self, keys, blend_ms=0, trace=False):
        """keys: [{'t': s, 'eye': [x,y,z], 'target': [x,y,z] | 'yaw': r, 'pitch': r, 'fov': deg, 'ease': ...}]

        ease is for the segment to the next key: 'in' leaves this key from rest, 'out'
        arrives at the next key at rest, 'inout' both. Without eases velocity is
        continuous through keys and the speed follows the key times."""
        return self._call({'op': 'path', 'keys': keys, 'blendMs': blend_ms, 'trace': trace})

    def follow(self, target, back=3.4, side=1.2, height=1.9, ahead=1.5, look_height=1.0, half_life=.35,
               look_half_life=None, heading_half_life=None, blend_ms=600, collide=False, fov=None, trace=False,
               frame='heading', offset=None, look_offset=None):
        """target: an NPC/resident id, 'companion', or an object instance id. frame 'heading':
        the eye sits back/side (+ = the subject's left)/height from its feet in its heading
        frame and looks at a point `ahead` of it at look_height. 'world': eye = feet + offset,
        target = feet + look_offset (world axes). 'fixed': eye = offset (a point) panning to
        keep feet + look_offset in view. All critically damped (half-lives in seconds)."""
        req = {'op': 'follow', 'target': target, 'back': back, 'side': side, 'height': height, 'ahead': ahead,
               'lookHeight': look_height, 'halfLife': half_life, 'blendMs': blend_ms, 'collide': collide, 'trace': trace,
               'frame': frame}
        if offset is not None:
            req['offset'] = list(offset)
        if look_offset is not None:
            req['lookOffset'] = list(look_offset)
        if heading_half_life is not None:
            req['headingHalfLife'] = heading_half_life
        if look_half_life is not None:
            req['lookHalfLife'] = look_half_life
        if fov is not None:
            req['fov'] = fov
        return self._call(req)

    def hold(self, eye, target=None, yaw=None, pitch=0., blend_ms=0, fov=None):
        """Hold a pose (looking at target, or yaw/pitch); blend_ms eases there from the
        camera now (or from the shot in charge, which keeps moving during the blend)."""
        key = {'t': 0, 'eye': list(eye)}
        if target is not None:
            key['target'] = list(target)
        else:
            key.update(yaw=yaw, pitch=pitch)
        if fov is not None:
            key['fov'] = fov
        return self.path([key], blend_ms=blend_ms)

    def move_to(self, eye, target=None, yaw=None, pitch=0., seconds=1.2, wait=True):
        """An eased move from the camera now to a pose, then hold it."""
        self.hold(eye, target, yaw, pitch, blend_ms=int(seconds * 1000))
        if wait:
            time.sleep(seconds)
            self.wait(10)

    def trace(self, clear=True):
        return self._call({'op': 'trace', 'clear': clear})

    def stop(self):
        return self._call({'op': 'stop'})

    def status(self):
        return self._call({'op': 'status'})

    def wait(self, timeout=120):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            st = self.status()
            if (st.get('done') and not st.get('blending')) or st.get('mode') == 'idle':
                return st
            time.sleep(.1)
        raise TimeoutError('cinema path did not finish')

    def run(self, keys, blend_ms=0):
        """Play a path to its end."""
        self.path(keys, blend_ms)
        return self.wait(keys[-1]['t'] + blend_ms / 1000 + 30)

    def export_status(self):
        return self.status()['export']

    @contextlib.contextmanager
    def recording(self, clip, warmup=.5, method=None, crf=16, preset='veryfast', fps=60, encoder=None, out_fps=None):
        """Record HEADLESS-1 while the block runs; yields the start time.

        method 'export' (default): core hands over every presented frame (FIFO, raw RGBX)
        and ffmpeg encodes them at exactly fps (the output's refresh rate), one video
        frame per presented frame; encoder replaces the x264 options (e.g. NVENC for 4K),
        out_fps keeps every n-th frame. 'wf-recorder': the screen-capture client (misses
        frames on the headless host)."""
        clip = pathlib.Path(clip)
        method = method or os.environ.get('HYPRUNE_CINEMA_RECORDER', 'export')
        log = open(clip.with_suffix('.rec.log'), 'w')
        if method == 'wf-recorder':
            rec = self.s.envrun('wf-recorder', '-o', 'HEADLESS-1', *RECORD, '-f', str(clip), stdout=log, stderr=subprocess.STDOUT)
            time.sleep(1.0)
            t0 = time.monotonic()
            try:
                yield t0
            finally:
                stop_recorder(rec)
                log.close()
            return
        fifo = self.s.STATE / f'cinema-export-{os.getpid()}-{time.monotonic_ns()}.fifo'
        os.mkfifo(fifo)
        mon = next(m for m in json.loads(self.s.ctl('monitors', '-j')) if m['name'] == 'HEADLESS-1')
        w, h = mon['width'], mon['height']
        codec = encoder or ['-c:v', 'libx264', '-preset', preset, '-crf', str(crf), '-pix_fmt', 'yuv420p', '-color_range', 'tv']
        rate = ['-r', str(out_fps)] if out_fps else []
        enc = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb0', '-s', f'{w}x{h}',
                                '-framerate', str(fps), '-thread_queue_size', '512', '-i', str(fifo), '-an', *rate, *codec, str(clip)],
                               stdout=log, stderr=subprocess.STDOUT)
        self._call({'op': 'export', 'path': str(fifo), 'width': w, 'height': h})
        time.sleep(warmup)
        t0 = time.monotonic()
        try:
            yield t0
        finally:
            st = self._call({'op': 'exportStop'})['export']
            enc.wait(timeout=120)
            log.close()
            fifo.unlink(missing_ok=True)
            self.last_export = st
            if st['error'] or st['skipped'] or st['written'] != st['grabbed']:
                raise RuntimeError(f'frame export lost frames: {st}')


def stop_recorder(rec, timeout=60):
    rec.send_signal(signal.SIGINT)
    t0 = time.monotonic()
    while rec.poll() is None and time.monotonic() - t0 < timeout:
        time.sleep(.1)
    if rec.poll() is None:
        rec.kill()


def ease(u):
    return u * u * (3 - 2 * u)


def look_angles(eye, target):
    d = [target[i] - eye[i] for i in range(3)]
    return math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2]))


NVENC = ['-c:v', 'h264_nvenc', '-preset', 'p5', '-rc', 'constqp', '-qp', '16', '-pix_fmt', 'yuv420p']


def encode(inputs, output, trim=0.0, extra=()):
    """Cut clips together (each trimmed by `trim` s at its start, frame-exact) into one
    60 fps mp4 with the gallery settings. inputs: paths, or (path, start, end) in seconds."""
    args, labels = [], []
    for k, clip in enumerate(inputs):
        path, start, end = (clip, trim, None) if isinstance(clip, (str, pathlib.Path)) else clip
        args += ['-ss', f'{start:.3f}'] + (['-to', f'{end:.3f}'] if end else []) + ['-i', str(path)]
        labels.append(f'[{k}:v]')
    graph = f"{''.join(labels)}concat=n={len(labels)}:v=1:a=0,fps=60[v]"
    subprocess.run(['ffmpeg', '-v', 'error', '-y', *args, '-filter_complex', graph, '-map', '[v]', '-an', *ENCODE, *extra, str(output)],
                   check=True)
    return pathlib.Path(output)


def sidecar(path, values):
    """Gallery sidecar: every value a string."""
    flat = {k: v if isinstance(v, str) else json.dumps(v) if isinstance(v, (list, dict)) else str(v) for k, v in values.items()}
    pathlib.Path(path).write_text(json.dumps(flat, indent=1) + '\n')


def gate_summary(g):
    if g.get('pass') is None:
        return f"no camera motion found ({g.get('frames')} frames)"
    return (f"{'PASS' if g['pass'] else 'FAIL'} duplicates {g['duplicatePct']}% (identical {g['identicalPct']}%), "
            f"step CV {g['stepCV']}, camera {g['cameraHz']} Hz of {g['fps']}, {g['motionFrames']} motion frames")


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest='cmd', required=True)
    g = sub.add_parser('gate')
    g.add_argument('video')
    g.add_argument('--json')
    g.add_argument('--start', type=float)
    g.add_argument('--end', type=float)
    a = p.parse_args()
    if a.cmd == 'gate':
        result = gate(a.video, a.start, a.end)
        print(gate_summary(result))
        if a.json:
            pathlib.Path(a.json).write_text(json.dumps(result, indent=1) + '\n')
        sys.exit(0 if result.get('pass') is not False else 1)
