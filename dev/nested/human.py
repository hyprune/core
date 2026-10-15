#!/usr/bin/env python3
"""A humanised synthetic driver for nested demos: camera, pointer and keyboard that move like
a person, not a spline.

Camera (CameraTrack): a dense cinema path (cinema.Cinema.path, yaw/pitch keys at `rate` Hz)
built from actions:
  idle(seconds)        stand: the view drifts slowly (a hand on the mouse), breathing.
  turn(yaw, pitch)     a look: a minimum-jerk turn that overshoots a little and corrects.
  look_at(point)       turn() towards a point.
  walk(points, ...)    walk (or fly, bob=0) through points: gentle start and stop, the
                       speed varying around a pace, a slight path wobble, a head bob in step,
                       the gaze leading along the path with a lag; glances turn to a point
                       for a while and back.
Pointer (Pointer): Fitts-law timed moves on a gently curved path with a minimum-jerk speed
profile, a small overshoot and a corrective sub-movement, sometimes a micro-correction, and
hand tremor; clicks with a pause before, a natural press length and a pause after.
Keyboard (typing_args): wtype arguments with inter-key delays drawn around a typing speed,
longer after spaces and punctuation, now and then a pause before a word.

Recordings (smooth_recording_look): a recorded session's mouse-look spread evenly over a
window (a hand-held mouse delivers motion unevenly per tick), for session_demo.py.

Everything is seeded (seed=...) so a take can be repeated exactly. Units: metres, radians,
seconds, pixels. Yaw/pitch follow the cinema camera: yaw = atan2(dx, -dz), pitch up > 0.

  cam = cinema.Cinema(s)
  track = human.CameraTrack(eye, yaw, pitch, seed=7)
  track.idle(1.0); track.look_at([4, 1, -6]); track.walk([[2, 1.7, 0], [0, 1.7, -4]], speed=1.3)
  cam.path(track.keys()); cam.wait()

  pointer = human.Pointer(send=lambda dx, dy: device_move(dx, dy), press=device_button, seed=3)
  pointer.move((x0, y0), (x1, y1), width=40); pointer.click()
"""
import math, random, time

TAU = math.tau


def min_jerk(u):
    """Minimum-jerk position profile on 0..1 (zero speed and acceleration at both ends)."""
    u = min(1.0, max(0.0, u))
    return u * u * u * (10 - 15 * u + 6 * u * u)


def wrap(a):
    return (a + math.pi) % TAU - math.pi


def angles(eye, target):
    """The cinema camera's yaw and pitch looking from eye at target."""
    dx, dy, dz = (target[i] - eye[i] for i in range(3))
    return math.atan2(dx, -dz), math.atan2(dy, math.hypot(dx, dz))


class Noise:
    """Smooth 1-D noise: a few sines with random phases (deterministic for a seed)."""

    def __init__(self, rng, freqs, amps):
        self.parts = [(f * rng.uniform(.85, 1.15), a, rng.uniform(0, TAU)) for f, a in zip(freqs, amps)]

    def __call__(self, t):
        return sum(a * math.sin(TAU * f * t + p) for f, a, p in self.parts)


def _catmull(points, samples=12):
    """A smooth polyline through points (centripetal-ish uniform Catmull-Rom)."""
    if len(points) < 3:
        return [list(p) for p in points]
    out = []
    ext = [points[0]] + list(points) + [points[-1]]
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        for k in range(samples):
            u = k / samples
            u2, u3 = u * u, u * u * u
            out.append([.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * u + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * u2 +
                              (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * u3) for j in range(3)])
    out.append(list(points[-1]))
    return out


class _Polyline:
    def __init__(self, points):
        self.p = points
        self.s = [0.0]
        for a, b in zip(points, points[1:]):
            self.s.append(self.s[-1] + math.dist(a, b))
        self.length = self.s[-1]

    def at(self, s):
        s = min(self.length, max(0.0, s))
        lo, hi = 0, len(self.s) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if self.s[mid] <= s:
                lo = mid
            else:
                hi = mid
        seg = self.s[hi] - self.s[lo] or 1e-9
        u = (s - self.s[lo]) / seg
        a, b = self.p[lo], self.p[hi]
        return [a[j] + (b[j] - a[j]) * u for j in range(3)]

    def heading(self, s, ahead=.6):
        a, b = self.at(s), self.at(min(self.length, s + ahead))
        if math.hypot(b[0] - a[0], b[2] - a[2]) < 1e-4:
            a, b = self.at(max(0, s - ahead)), self.at(s)
        return math.atan2(b[0] - a[0], -(b[2] - a[2]))


class CameraTrack:
    """Builds a dense camera path from human-like actions (see the module docstring)."""

    def __init__(self, eye, yaw, pitch, fov=None, seed=None, rate=30, drift=1.0):
        self.rng = random.Random(seed)
        self.rate, self.dt = rate, 1.0 / rate
        self.t, self.eye, self.yaw, self.pitch, self.fov = 0.0, list(eye), yaw, pitch, fov
        r = self.rng
        # The resting hand: slow wander and a faint faster wobble (radians), scaled by `drift`.
        self.drift = (Noise(r, [.06, .17, .43], [.0045 * drift, .0022 * drift, .0007 * drift]),
                      Noise(r, [.05, .14, .38], [.0028 * drift, .0014 * drift, .0005 * drift]))
        self.breath = Noise(r, [.24], [.0025])
        self._keys = []
        self._emit()

    # -- output --------------------------------------------------------------------------
    def _emit(self, eye=None, dyaw=0.0, dpitch=0.0):
        e = eye or self.eye
        key = {'t': round(self.t, 4), 'eye': [round(v, 5) for v in e],
               'yaw': round(self.yaw + dyaw + self.drift[0](self.t), 6),
               'pitch': round(max(-1.45, min(1.45, self.pitch + dpitch + self.drift[1](self.t))), 6)}
        if self.fov is not None:
            key['fov'] = self.fov
        if self._keys and abs(self._keys[-1]['t'] - key['t']) < 1e-6:
            self._keys[-1] = key
        else:
            self._keys.append(key)

    def keys(self):
        return list(self._keys)

    @property
    def duration(self):
        return self.t

    # -- actions -------------------------------------------------------------------------
    def idle(self, seconds):
        """Stand still: only the hand's drift and breathing move the view."""
        end = self.t + seconds
        while self.t < end - 1e-9:
            self.t = min(end, self.t + self.dt)
            self._emit([self.eye[0], self.eye[1] + self.breath(self.t), self.eye[2]])
        return self

    def turn(self, yaw, pitch=None, duration=None, overshoot=None):
        """A look to (yaw, pitch): a minimum-jerk turn past the target by a few percent, a
        brief settle, then a corrective sub-movement onto it."""
        pitch = self.pitch if pitch is None else pitch
        dyaw, dpitch = wrap(yaw - self.yaw), pitch - self.pitch
        amp = math.hypot(dyaw, dpitch)
        if amp < 1e-4:
            return self
        deg = math.degrees(amp)
        r = self.rng
        duration = duration or (.24 + .36 * math.sqrt(deg / 90)) * r.uniform(.9, 1.12)
        over = overshoot if overshoot is not None else min(math.radians(3.5), max(math.radians(.35), amp * r.uniform(.04, .085)))
        ux, uy = dyaw / amp, dpitch / amp
        y0, p0 = self.yaw, self.pitch
        y1, p1 = y0 + dyaw + ux * over, p0 + dpitch + uy * over * .6
        self._sweep(y0, p0, y1, p1, duration)
        self._hold(r.uniform(.04, .09))
        self._sweep(y1, p1, y0 + dyaw, p0 + dpitch, r.uniform(.14, .24))
        return self

    def look_at(self, point, **kw):
        yaw, pitch = angles(self.eye, point)
        return self.turn(yaw, pitch, **kw)

    def _sweep(self, y0, p0, y1, p1, seconds):
        start = self.t
        while self.t < start + seconds - 1e-9:
            self.t = min(start + seconds, self.t + self.dt)
            u = min_jerk((self.t - start) / seconds)
            self.yaw, self.pitch = y0 + (y1 - y0) * u, p0 + (p1 - p0) * u
            self._emit([self.eye[0], self.eye[1] + self.breath(self.t), self.eye[2]])
        self.yaw, self.pitch = y1, p1

    def _hold(self, seconds):
        self.idle(seconds)

    def walk(self, points, speed=1.35, bob=.022, sway=.010, wobble=.03, look='path', look_pitch=-.05,
             glances=(), accel=1.4, decel=1.6, lead=.35, stride=.72, max_turn=2.6):
        """Walk (or, with bob=0, fly) through points. look: 'path' (the gaze leads along the
        path with a lag), a point to keep looking at, or None (keep the current view). glances:
        [(fraction of the way, point, seconds)] turn to a point and back. max_turn caps the
        gaze's angular speed (rad/s). Returns self."""
        r = self.rng
        pts = [list(self.eye)] + [list(p) for p in points]
        line = _Polyline(_catmull(pts))
        if line.length < 1e-3:
            return self
        pace = Noise(r, [.11, .29], [.045, .02])
        lateral = Noise(r, [.17, .41], [wobble, wobble * .35])
        s, v, phase = 0.0, 0.0, 0.0
        heading = self.yaw
        look_yaw, look_pitch_now = self.yaw, self.pitch
        glances = sorted(glances, key=lambda g: g[0])
        active = None  # [end time, point]
        gaze_v = [0.0, 0.0]
        start_t = self.t
        while True:
            left = line.length - s
            want = speed * (1 + pace(self.t))
            # Gentle start (accel limit, smoothed) and stop: never faster than can stop in `left`.
            # The stopping speed uses a gentler deceleration than the limit, so the lag below
            # never carries the walker past the end.
            stop_v = math.sqrt(max(0.0, 2 * decel * .7 * max(0.0, left - .02)))
            target_v = min(want, stop_v)
            dv = (target_v - v) * (1 - math.exp(-self.dt / .18))
            v = max(0.0, v + max(-decel * 1.5 * self.dt, min(accel * self.dt, dv)))
            if left < .015 and v < .05:
                s = line.length
            else:
                s = min(line.length, s + v * self.dt)
            self.t += self.dt
            # Step cycle: frequency from the pace; bob in step, sway every two steps.
            phase += (v / stride) * self.dt
            gait = min(1.0, v / max(.2, speed))
            base = line.at(s)
            h = line.heading(s)
            side = (math.cos(h), 0.0, math.sin(h))  # the walker's right in the xz plane
            off = lateral(self.t) * gait + sway * math.sin(math.pi * phase) * gait
            eye = [base[0] + side[0] * off, base[1] - bob * gait * math.cos(TAU * phase) + self.breath(self.t), base[2] + side[2] * off]
            self.eye = [base[0] + side[0] * off, base[1], base[2] + side[2] * off]
            # Gaze: a damped spring towards the wanted direction (smooth starts, no steps);
            # glances are quicker and slightly underdamped (a small natural overshoot).
            frac = s / line.length
            if glances and active is None and frac >= glances[0][0]:
                f, point, hold = glances.pop(0)
                active = [self.t + hold, point]
            if active is not None:
                target_yaw, target_pitch = angles(eye, active[1])
                omega, zeta = 9.0, .78
                if self.t >= active[0]:
                    active = None
            elif look == 'path':
                target_yaw, target_pitch = h, look_pitch
                omega, zeta = 2.0 / lead, 1.0
            elif look is None:
                target_yaw, target_pitch = look_yaw, look_pitch_now
                omega, zeta = 6.0, 1.0
            else:
                target_yaw, target_pitch = angles(eye, look)
                omega, zeta = 6.0, 1.0
            for _ in range(4):  # sub-steps keep the spring stable at 30 Hz
                h4 = self.dt / 4
                ay = omega * omega * wrap(target_yaw - look_yaw) - 2 * zeta * omega * gaze_v[0]
                ap = omega * omega * (target_pitch - look_pitch_now) - 2 * zeta * omega * gaze_v[1]
                gaze_v[0] += ay * h4; gaze_v[1] += ap * h4
                speed_now = math.hypot(*gaze_v)
                if speed_now > max_turn:  # a hand turns the view at most this fast
                    gaze_v[0] *= max_turn / speed_now; gaze_v[1] *= max_turn / speed_now
                look_yaw += gaze_v[0] * h4; look_pitch_now += gaze_v[1] * h4
            self.yaw, self.pitch = look_yaw, look_pitch_now
            self._emit(eye)
            if s >= line.length and v < .05:
                break
            if self.t - start_t > 600:
                raise RuntimeError('walk did not finish')
        self.eye = list(line.at(line.length))
        return self


# ---------------------------------------------------------------------------- pointer
class Pointer:
    """Human pointer moves for a relative pointer device. send(dx, dy) moves by whole pixels
    (it is called at about `rate` Hz with the time between calls slept here); press(button,
    down) presses or releases. position is tracked from the moves (start it with `at`)."""

    def __init__(self, send, press=None, at=(0.0, 0.0), seed=None, rate=120, sleep=time.sleep, clock=time.perf_counter):
        self.send, self.press, self.rng = send, press, random.Random(seed)
        self.rate, self.sleep, self.clock = rate, sleep, clock
        self.x, self.y = float(at[0]), float(at[1])
        self.sent = [self.x, self.y]  # the position actually sent (whole pixels), for the remainder
        self.trace = []         # (t, x, y) of every sample, for metrics

    def _stream(self, xs, ys, seconds):
        """Follow sampled positions over `seconds`, sending whole-pixel deltas on time."""
        n = len(xs)
        t0 = self.clock()
        for i in range(n):
            due = t0 + seconds * (i + 1) / n
            tx, ty = xs[i], ys[i]
            dx, dy = round(tx - self.sent[0]), round(ty - self.sent[1])
            if dx or dy:
                self.send(dx, dy)
                self.sent[0] += dx; self.sent[1] += dy
            self.x, self.y = tx, ty
            self.trace.append((self.clock(), tx, ty))
            wait = due - self.clock()
            if wait > 0:
                self.sleep(wait)

    def fitts_seconds(self, distance, width):
        """Movement time by Fitts' law (Shannon form), a=80 ms, b=130 ms/bit, varied a little."""
        return (.08 + .13 * math.log2(distance / max(4.0, width) + 1)) * self.rng.uniform(.9, 1.15)

    def move(self, to, width=24):
        """Move to `to` (pixels): a curved, minimum-jerk primary movement that overshoots by a
        few percent, a settle, a corrective sub-movement, and now and then a micro-correction."""
        r = self.rng
        x0, y0 = self.x, self.y
        tx, ty = float(to[0]), float(to[1])
        dx, dy = tx - x0, ty - y0
        dist = math.hypot(dx, dy)
        if dist < .5:
            return self
        ux, uy = dx / dist, dy / dist
        nx, ny = -uy, ux
        over = min(18.0, max(2.0, dist * r.uniform(.03, .07))) if dist > 30 else 0.0
        miss = r.gauss(0, min(4.0, dist * .01))
        ex, ey = tx + ux * over + nx * miss, ty + uy * over + ny * miss
        bend = dist * r.uniform(.05, .14) * (1 if r.random() < .7 else -1)  # mostly the same side: a wrist arc
        cx, cy = x0 + dx * .5 + nx * bend, y0 + dy * .5 + ny * bend
        seconds = self.fitts_seconds(dist, width)
        n = max(2, int(seconds * self.rate))
        tremor = (Noise(r, [9.0, 11.5], [.25, .15]), Noise(r, [8.5, 12.0], [.25, .15]))
        xs, ys = [], []
        for i in range(1, n + 1):
            u = min_jerk(i / n)
            bx = (1 - u) ** 2 * x0 + 2 * (1 - u) * u * cx + u * u * ex
            by = (1 - u) ** 2 * y0 + 2 * (1 - u) * u * cy + u * u * ey
            k = i / n
            xs.append(bx + tremor[0](k * seconds) * (1 - k)); ys.append(by + tremor[1](k * seconds) * (1 - k))
        self._stream(xs, ys, seconds)
        if over or abs(miss) > .5:
            self.sleep(r.uniform(.03, .07))
            self._sub(tx + r.gauss(0, .6), ty + r.gauss(0, .6), r.uniform(.12, .2))
        if r.random() < .45:
            self.sleep(r.uniform(.06, .12))
            self._sub(tx, ty, r.uniform(.06, .1))
        return self

    def _sub(self, tx, ty, seconds):
        x0, y0 = self.x, self.y
        n = max(2, int(seconds * self.rate))
        xs = [x0 + (tx - x0) * min_jerk(i / n) for i in range(1, n + 1)]
        ys = [y0 + (ty - y0) * min_jerk(i / n) for i in range(1, n + 1)]
        self._stream(xs, ys, seconds)

    def click(self, button=272):
        """A pause on the target, a natural press, a pause after."""
        r = self.rng
        self.sleep(r.uniform(.09, .2))
        if self.press:
            self.press(button, 1)
        self.sleep(r.uniform(.07, .13))
        if self.press:
            self.press(button, 0)
        self.sleep(r.uniform(.12, .25))
        return self


# ---------------------------------------------------------------------------- keyboard
def typing_args(text, wpm=62, seed=None):
    """wtype arguments typing `text` like a person: per-key delays (-s ms) drawn around `wpm`,
    longer after spaces and punctuation, sometimes a pause before a word. '-' goes as the
    keysym (`-k minus`)."""
    rng = random.Random(seed)
    base = 60.0 / (wpm * 5)  # seconds per character
    args = []
    for i, ch in enumerate(text):
        if i:
            d = base * math.exp(rng.gauss(0, .32))
            prev = text[i - 1]
            if prev == ' ':
                d *= rng.uniform(1.1, 1.6)
                if rng.random() < .08:
                    d += rng.uniform(.25, .55)  # thinking about the next word
            elif prev in '.,;:/-_|':
                d *= rng.uniform(1.2, 1.8)
            args += ['-s', str(max(18, int(round(d * 1000))))]
        args += ['-k', 'minus'] if ch == '-' else [ch]  # a lone '-' would read as an option
    return args


# ---------------------------------------------------------------------------- recordings
def smooth_recording_look(rows, ms):
    """A recording (hyprune.session rows) with its world-mode mouse-look motion low-passed: per
    frame, the motion of a centred window of frames (ms wide) averaged, sent as one event just
    before the frame's tick. The total turn is kept; every other input (and motion in other
    modes: a free cursor, a menu) is untouched."""
    if ms <= 0:
        return rows
    per = {}
    for r in rows:
        if r.get('kind') == 'input' and r['event'].get('type') == 'motion' and r['event'].get('mode', 'world') == 'world':
            d = per.setdefault(r['frame'], [0.0, 0.0])
            d[0] += r['event'].get('dx', 0.0); d[1] += r['event'].get('dy', 0.0)
    if not per:
        return rows
    ticks = [r for r in rows if r.get('kind') == 'frame']
    seconds = (ticks[-1]['ns'] - ticks[0]['ns']) / 1e9 if len(ticks) > 1 else 1
    rate = (len(ticks) - 1) / seconds if seconds > 0 else 60
    half = max(1, int(round(ms / 1000 * rate / 2)))
    # Each tick's motion spread evenly over the ticks of its window that exist (the sum stays).
    exists = {r['frame'] for r in ticks}
    out_motion = {}
    for g, (dx, dy) in per.items():
        window = [f for f in range(g - half, g + half + 1) if f in exists] or [g]
        for f in window:
            o = out_motion.setdefault(f, [0.0, 0.0])
            o[0] += dx / len(window); o[1] += dy / len(window)
    # A tick's inputs come before its `frame` row: each new event goes just before it.
    merged, last = [], 0
    for r in rows:
        if r.get('kind') == 'input' and r['event'].get('type') == 'motion' and r['event'].get('mode', 'world') == 'world':
            continue
        if r.get('kind') == 'frame' and r['frame'] in out_motion:
            dx, dy = out_motion[r['frame']]
            merged.append({'event': {'dx': dx, 'dy': dy, 'mode': 'world', 'type': 'motion'}, 'frame': r['frame'],
                           'kind': 'input', 'ns': max(last, r['ns'] - 1)})  # times stay in file order
        merged.append(r)
        last = r.get('ns', last)
    return merged


# ---------------------------------------------------------------------------- metrics
def track_metrics(keys, rate=None):
    """Motion statistics of a camera path (keys from CameraTrack or any cinema path sampled
    densely): angular speed and its variation, jerk, and the share of time at constant speed
    (a spline's signature) while moving."""
    if len(keys) < 4:
        return {}
    ts = [k['t'] for k in keys]
    yaws = [k['yaw'] if 'yaw' in k else 0 for k in keys]
    pitches = [k['pitch'] if 'pitch' in k else 0 for k in keys]
    eyes = [k['eye'] for k in keys]
    w, v = [], []
    for i in range(1, len(keys)):
        dt = max(1e-6, ts[i] - ts[i - 1])
        w.append(math.hypot(wrap(yaws[i] - yaws[i - 1]), pitches[i] - pitches[i - 1]) / dt)
        v.append(math.dist(eyes[i], eyes[i - 1]) / dt)
    def stats(xs):
        moving = [x for x in xs if x > 1e-3]
        if not moving:
            return {'moving': 0}
        mean = sum(moving) / len(moving)
        sd = math.sqrt(sum((x - mean) ** 2 for x in moving) / len(moving))
        steady = sum(1 for a, b in zip(moving, moving[1:]) if abs(b - a) < .005 * max(a, 1e-6)) / max(1, len(moving) - 1)
        return {'moving': len(moving), 'mean': round(mean, 4), 'cv': round(sd / mean, 3), 'steadyShare': round(steady, 3)}
    return {'angular': stats(w), 'linear': stats(v), 'seconds': round(ts[-1] - ts[0], 2)}
