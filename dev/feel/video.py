#!/usr/bin/env python3
"""Motion smoothness of screen captures: per-frame global motion against frame timestamps.

    dev/feel/video.py capture.mp4 [other.mp4 ...] [--label NAME ...] --out DIR

Each capture should be one video frame per new compositor frame (gpu-screen-recorder in
content mode, or any variable-frame-rate capture that keeps the presentation times). For
every pair of consecutive frames the image shift is measured by phase correlation on
downscaled greyscale frames: over the whole frame (pans: dx, dy) and on the left and right
halves (walking forward: the halves move apart, (right - left) / 2 is the expansion).

The cumulative motion, sampled at each frame's timestamp, should lie on a smooth curve of
time: a frame that shows the scene too far ahead or behind for its display time is judder,
whatever the frame rate. Reported per capture:

- intervals between frames (p50/p90/p99/max), duplicates (no motion where neighbours move);
- velocity per frame (shift / interval) and its spread during steady motion;
- judder: residual of the cumulative motion against a smooth (gaussian, sigma 60 ms) fit of
  time, in pixels and relative to the motion per frame;
- timing model: the same residual when the content is placed at the previous frame's
  time instead of its own. If that fits better, frames carry the state of the previous
  interval (the motion was stepped by the last frame's dt, not by the time it is shown);
- hitches: intervals over 2.5x the median, with timestamps.

Writes DIR/video.json and DIR/video.html (side by side when given several captures).
Decoding 4K video is heavy: run it on the work host, not a laptop.
"""
import argparse, html, json, math, pathlib, subprocess, sys
import numpy as np

# Analysis: the central 60% x 70% of the frame (a yaw pan moves the image nearly uniformly
# there; at a 90 degree field of view the edges move twice as fast), scaled to W wide.
CROP_W, CROP_H = 0.6, 0.7
W = 960
TILE = 128


def timestamps(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'packet=pts_time',
                          '-of', 'csv=p=0', str(path)], check=True, capture_output=True, text=True).stdout
    return np.array(sorted(float(l.split(',')[0]) for l in out.split() if l.strip() and l != 'N/A'))


def source_size(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=width,height',
                          '-of', 'csv=p=0', str(path)], check=True, capture_output=True, text=True).stdout
    w, h = out.strip().split(',')[:2]
    return int(w), int(h)


def frames(path, h):
    """Every decoded frame (no rate conversion): the central crop, greyscale W x h float32."""
    vf = f'crop=iw*{CROP_W}:ih*{CROP_H},scale={W}:{h}:flags=area,format=gray'
    p = subprocess.Popen(['ffmpeg', '-v', 'error', '-threads', '0', '-i', str(path), '-fps_mode', 'passthrough',
                          '-vf', vf, '-f', 'rawvideo', '-'], stdout=subprocess.PIPE)
    size = W * h
    while True:
        buf = p.stdout.read(size)
        if len(buf) < size:
            break
        yield np.frombuffer(buf, np.uint8).reshape(h, W).astype(np.float32)
    p.wait()


_windows = {}


def correlate(a, b):
    """Sub-pixel translation of b relative to a (phase correlation), and the peak height (0..1)."""
    shape = a.shape
    if shape not in _windows:
        _windows[shape] = np.outer(np.hanning(shape[0]), np.hanning(shape[1])).astype(np.float32)
    win = _windows[shape]
    fa = np.fft.rfft2((a - a.mean()) * win)
    fb = np.fft.rfft2((b - b.mean()) * win)
    r = fb * np.conj(fa)
    r /= np.abs(r) + 1e-9
    c = np.fft.irfft2(r, s=shape)
    iy, ix = np.unravel_index(np.argmax(c), c.shape)
    h, w = shape

    def sub(m, p):  # parabolic peak refinement
        d = m - 2 * c[iy, ix] + p
        return 0.0 if abs(d) < 1e-12 else float(np.clip(0.5 * (m - p) / d, -.5, .5))
    dy = iy + sub(c[(iy - 1) % h, ix], c[(iy + 1) % h, ix])
    dx = ix + sub(c[iy, (ix - 1) % w], c[iy, (ix + 1) % w])
    if dy > h / 2:
        dy -= h
    if dx > w / 2:
        dx -= w
    return dx, dy, float(c[iy, ix])


def motion(prev, img):
    """Translation (pan, tilt) at the crop centre and expansion (walking) between two frames.

    A coarse shift over the whole crop first, then a grid of tiles refined around it, and a
    robust fit dx = a + b x, dy = c + b y over the tiles: static HUD tiles and parallax
    outliers drop out. Returns (a, c, b, inlier tiles)."""
    h, w = img.shape
    small = lambda m: m[:h // 4 * 4, :w // 4 * 4].reshape(h // 4, 4, w // 4, 4).mean(axis=(1, 3))
    cx, cy, _ = correlate(small(prev), small(img))
    cx, cy = int(round(cx * 4)), int(round(cy * 4))
    pts = []
    for gy in np.linspace(TILE // 2 + 8, h - TILE // 2 - 8, 4):
        for gx in np.linspace(TILE // 2 + 8, w - TILE // 2 - 8, 5):
            x0, y0 = int(gx) - TILE // 2, int(gy) - TILE // 2
            px, py = x0 - cx, y0 - cy
            if px < 0 or py < 0 or px + TILE > w or py + TILE > h:
                continue
            rx, ry, peak = correlate(prev[py:py + TILE, px:px + TILE], img[y0:y0 + TILE, x0:x0 + TILE])
            if peak > 0.1:
                pts.append((gx - w / 2, gy - h / 2, cx + rx, cy + ry))
    pts = np.array(pts)
    if len(pts) < 6:
        return None
    keep = np.ones(len(pts), bool)
    for _ in range(3):
        p = pts[keep]
        n = len(p)
        # Unknowns a, c, b: rows [1, 0, x] -> dx and [0, 1, y] -> dy.
        A = np.zeros((2 * n, 3))
        A[:n, 0] = 1
        A[:n, 2] = p[:, 0]
        A[n:, 1] = 1
        A[n:, 2] = p[:, 1]
        sol = np.linalg.lstsq(A, np.concatenate([p[:, 2], p[:, 3]]), rcond=None)[0]
        res = np.hypot(pts[:, 2] - (sol[0] + sol[2] * pts[:, 0]), pts[:, 3] - (sol[1] + sol[2] * pts[:, 1]))
        new = res < max(3 * np.median(res), 1.0)
        if new.sum() < 6 or (new == keep).all():
            break
        keep = new
    return sol[0], sol[1], sol[2], int(keep.sum())


def measure(path):
    t = timestamps(path)
    sw, sh = source_size(path)
    h = int(round(W * (sh * CROP_H) / (sw * CROP_W))) // 2 * 2
    scale = sw * CROP_W / W  # source pixels per analysis pixel
    prev, rows, diffs = None, [], []
    for img in frames(path, h):
        if prev is not None:
            m = motion(prev, img)
            # Expansion in source pixels at half the source width (walking forward).
            rows.append(None if m is None else (m[0] * scale, m[1] * scale, m[2] * sw / 2, m[3]))
            diffs.append(float(np.mean(np.abs(img - prev))))
        prev = img
    n = min(len(rows) + 1, len(t))
    return t[:n], rows[:n - 1], np.array(diffs[:n - 1]), (sw, sh)


def smooth(times, values, sigma):
    """Gaussian kernel regression of values over (irregular) times."""
    out = np.empty_like(values)
    for i, ti in enumerate(times):
        lo, hi = np.searchsorted(times, [ti - 3 * sigma, ti + 3 * sigma])
        w = np.exp(-0.5 * ((times[lo:hi] - ti) / sigma) ** 2)
        out[i] = np.dot(w, values[lo:hi]) / w.sum()
    return out


def pct(a, q):
    return float(np.percentile(a, q)) if len(a) else None


def segments(ok, minimum=8):
    """Runs of consecutive measured frames (indices into the per-frame arrays)."""
    out, start = [], None
    for i, v in enumerate(list(ok) + [False]):
        if v and start is None:
            start = i
        elif not v and start is not None:
            if i - start >= minimum:
                out.append((start, i))
            start = None
    return out


def segments(ok, minimum=8):
    """Runs of consecutive measured frames (indices into the per-frame arrays)."""
    out, start = [], None
    for i, v in enumerate(list(ok) + [False]):
        if v and start is None:
            start = i
        elif not v and start is not None:
            if i - start >= minimum:
                out.append((start, i))
            start = None
    return out


def analyse(path, label):
    t, rows, diffs, size = measure(path)
    dt = np.diff(t)
    med = float(np.median(dt))
    ok = np.array([r is not None for r in rows])
    col = lambda k: np.array([r[k] if r is not None else np.nan for r in rows])
    dx, dy, expansion, tiles = col(0), col(1), col(2), col(3)
    result = {'label': label, 'file': pathlib.Path(path).name, 'size': list(size), 'frames': int(len(t)),
              'seconds': round(float(t[-1] - t[0]), 3), 'fps': round(len(dt) / float(t[-1] - t[0]), 2),
              'measured': round(float(ok.mean()), 3),
              'intervalMs': {'p50': pct(dt, 50) * 1e3, 'p90': pct(dt, 90) * 1e3, 'p99': pct(dt, 99) * 1e3,
                             'max': float(dt.max()) * 1e3}}
    # Duplicates: no change at all while the neighbours move.
    moving = np.nan_to_num(np.abs(dx) + np.abs(dy) + np.abs(expansion), nan=10) > 1.0
    result['duplicates'] = sum(1 for i in range(1, len(diffs) - 1)
                               if diffs[i] < 0.05 and moving[i - 1] and moving[i + 1])
    hitches = [{'t': round(float(t[i] - t[0]), 3), 'ms': round(float(dt[i]) * 1e3, 1)}
               for i in range(len(dt)) if dt[i] > 2.5 * med]
    result['hitches'] = hitches[:200]
    result['hitchCount'] = len(hitches)
    series = {}
    sigma = 0.060
    for name, d in (('pan', dx), ('tilt', dy), ('walk', expansion)):
        vel = d / dt  # px/s per frame (frame i+1 against frame i)
        res_own = np.full(len(t), np.nan)
        res_prev = np.full(len(t), np.nan)
        speed = np.full(len(t), np.nan)
        step_rel = np.full(len(dt), np.nan)  # motion of each frame / the motion its interval should carry
        for a, b in segments(ok):
            # Frames a..b (positions at t[a..b]) from the steps a..b-1.
            tt = t[a:b + 1]
            pos = np.concatenate([[0.0], np.cumsum(d[a:b])])
            fit = smooth(tt, pos, sigma)
            # Previous-time model: the content of each frame belongs to the timestamp before it.
            tp = np.concatenate([[tt[0] - med], tt[:-1]])
            res_own[a:b + 1] = pos - fit
            res_prev[a:b + 1] = pos - smooth(tp, pos, sigma)
            speed[a:b + 1] = np.abs(np.gradient(fit, tt))
            expected = np.diff(fit)  # the smooth path's advance over each interval
            step_rel[a:b] = d[a:b] / np.where(np.abs(expected) > 1e-6, expected, np.nan)
            # The segment ends are poorly constrained by the fit.
            res_own[a:a + 3] = res_own[b - 2:b + 1] = np.nan
        steady = np.nan_to_num(speed) > 200  # px/s
        s = steady & ~np.isnan(res_own) & ~np.isnan(res_prev)
        entry = {'steadyFrames': int(s.sum())}
        step = speed * med
        if s.sum() > 10:
            sv = s[1:] & ~np.isnan(vel)
            ratio = np.abs(vel[sv]) / speed[1:][sv]  # per-frame speed relative to the smooth speed
            entry.update({
                'speedPxS': {'p50': pct(speed[s], 50), 'p90': pct(speed[s], 90)},
                'velocityCv': float(np.std(ratio)),
                'judderPx': float(np.sqrt(np.mean(res_own[s] ** 2))),
                'judderRel': float(np.sqrt(np.mean((res_own[s] / step[s]) ** 2))),
                'judderPrevTimePx': float(np.sqrt(np.mean(res_prev[s] ** 2))),
                'judderP95Px': pct(np.abs(res_own[s]), 95),
            })
            sr = step_rel[sv & ~np.isnan(step_rel)]
            entry['stepRelative'] = {'p10': pct(sr, 10), 'p50': pct(sr, 50), 'p90': pct(sr, 90),
                                     'stalled': float(np.mean(np.abs(sr) < .15)),  # a new frame that barely moves
                                     'backward': float(np.mean(sr < -.15)),  # moves against the motion
                                     'overshoot': float(np.mean(sr > 2)), 'within25pct': float(np.mean(np.abs(sr - 1) < .25))}
            # Motion per frame against this and the previous interval: which one sets the step?
            idx = np.where(sv)[0]
            idx = idx[idx > 0]
            if len(idx) > 10:
                per = np.abs(d[idx]) / speed[1:][idx]  # seconds of motion carried by the frame
                entry['stepFollows'] = {'ownInterval': float(np.corrcoef(per, dt[idx])[0, 1]),
                                        'previousInterval': float(np.corrcoef(per, dt[idx - 1])[0, 1])}
        result[name] = entry
        series[name] = {'res': res_own, 'vel': vel, 'steady': s, 'stepRel': step_rel}
    result['_perFrame'] = {'t': (t[1:] - t[0]).tolist(), 'dt': dt.tolist(), 'dx': np.nan_to_num(dx).tolist(),
                           'dy': np.nan_to_num(dy).tolist(), 'expansion': np.nan_to_num(expansion).tolist(),
                           'tiles': np.nan_to_num(tiles).tolist(), 'diff': diffs.tolist()}
    return result, t, dt, series


# --- report ---------------------------------------------------------------------------------

COLORS = ['#2a7fff', '#e8590c']


def svg_line(xs_list, ys_list, w=900, h=180, ylabel='', colors=COLORS, ylim=None, dots=False):
    xs_all = np.concatenate(xs_list)
    ys_all = np.concatenate(ys_list)
    x0, x1 = float(xs_all.min()), float(xs_all.max())
    if ylim:
        y0, y1 = ylim
    else:
        y0, y1 = float(np.percentile(ys_all, 1)), float(np.percentile(ys_all, 99))
    if y1 <= y0:
        y1 = y0 + 1
    sx = lambda x: 40 + (x - x0) / max(x1 - x0, 1e-9) * (w - 50)
    sy = lambda y: h - 20 - (min(max(y, y0), y1) - y0) / (y1 - y0) * (h - 30)
    parts = [f'<svg width="{w}" height="{h}" xmlns="http://www.w3.org/2000/svg" style="background:#fff">',
             f'<text x="4" y="12" font-size="11" fill="#555">{html.escape(ylabel)}</text>',
             f'<text x="4" y="{sy(y1) + 4:.0f}" font-size="10" fill="#888">{y1:.4g}</text>',
             f'<text x="4" y="{sy(y0):.0f}" font-size="10" fill="#888">{y0:.4g}</text>']
    if y0 < 0 < y1:
        parts.append(f'<line x1="40" x2="{w - 10}" y1="{sy(0):.1f}" y2="{sy(0):.1f}" stroke="#ccc"/>')
    for xs, ys, c in zip(xs_list, ys_list, colors):
        if dots:
            parts += [f'<circle cx="{sx(x):.1f}" cy="{sy(y):.1f}" r="1.6" fill="{c}"/>' for x, y in zip(xs, ys)]
        else:
            pts = ' '.join(f'{sx(x):.1f},{sy(y):.1f}' for x, y in zip(xs, ys))
            parts.append(f'<polyline fill="none" stroke="{c}" stroke-width="1" points="{pts}"/>')
    parts.append('</svg>')
    return ''.join(parts)


def svg_hist(values_list, bins, w=440, h=160, xlabel='', colors=COLORS):
    counts = [np.histogram(v, bins=bins)[0] / max(len(v), 1) for v in values_list]
    top = max(c.max() for c in counts) or 1
    bw = (w - 50) / (len(bins) - 1)
    parts = [f'<svg width="{w}" height="{h}" xmlns="http://www.w3.org/2000/svg" style="background:#fff">']
    for k, (c, col) in enumerate(zip(counts, colors)):
        for i, v in enumerate(c):
            bh = v / top * (h - 35)
            parts.append(f'<rect x="{40 + i * bw + k * bw / len(counts):.1f}" y="{h - 20 - bh:.1f}" '
                         f'width="{bw / len(counts):.1f}" height="{bh:.1f}" fill="{col}"/>')
    for i in range(0, len(bins), max(1, len(bins) // 8)):
        parts.append(f'<text x="{40 + i * bw:.0f}" y="{h - 6}" font-size="10" fill="#666">{bins[i]:.3g}</text>')
    parts.append(f'<text x="{w - 120}" y="12" font-size="11" fill="#555">{html.escape(xlabel)}</text></svg>')
    return ''.join(parts)


def report(results, out):
    out.mkdir(parents=True, exist_ok=True)
    for r in results:
        per = r[0].pop('_perFrame')
        (out / f'frames-{r[0]["label"]}.json').write_text(json.dumps(per))
    summary = [r[0] for r in results]
    (out / 'video.json').write_text(json.dumps(summary, indent=1))
    legend = ' '.join(f'<span style="color:{c}">&#9632; {html.escape(r["label"])}</span>'
                      for r, c in zip(summary, COLORS))
    rows = []

    def fmt(v, d=2):
        return '–' if v is None else (f'{v:.{d}f}' if isinstance(v, float) else str(v))
    keys = [('frames', lambda r: r['frames']), ('fps', lambda r: r['fps']),
            ('interval p50 / p90 / p99 / max ms', lambda r: ' / '.join(fmt(r['intervalMs'][k], 1) for k in ('p50', 'p90', 'p99', 'max'))),
            ('hitches (>2.5x median)', lambda r: r['hitchCount']), ('frames measured', lambda r: r['measured']), ('frames measured', lambda r: r['measured']), ('duplicate frames', lambda r: r['duplicates'])]
    for m in ('pan', 'walk'):
        keys += [(f'{m}: steady frames', lambda r, m=m: r[m]['steadyFrames']),
                 (f'{m}: frames within ±25% of the motion their interval should carry', lambda r, m=m: fmt(r[m].get('stepRelative', {}).get('within25pct'), 3)),
                 (f'{m}: stalled / backward / overshoot (>2x) frames', lambda r, m=m: ' / '.join(
                     fmt(r[m].get('stepRelative', {}).get(k), 3) for k in ('stalled', 'backward', 'overshoot'))),
                 (f'{m}: velocity CV per frame', lambda r, m=m: fmt(r[m].get('velocityCv'), 3)),
                 (f'{m}: judder rms px (own time)', lambda r, m=m: fmt(r[m].get('judderPx'))),
                 (f'{m}: judder rms px (previous frame time)', lambda r, m=m: fmt(r[m].get('judderPrevTimePx'))),
                 (f'{m}: judder relative to step', lambda r, m=m: fmt(r[m].get('judderRel'), 3)),
                 (f'{m}: step vs own / previous interval (r)', lambda r, m=m: ' / '.join(
                     fmt(r[m].get('stepFollows', {}).get(k), 2) for k in ('ownInterval', 'previousInterval')))]
    table = '<table><tr><th></th>' + ''.join(f'<th>{html.escape(r["label"])}</th>' for r in summary) + '</tr>'
    for name, f in keys:
        table += f'<tr><td>{html.escape(name)}</td>' + ''.join(f'<td>{f(r)}</td>' for r in summary) + '</tr>'
    table += '</table>'
    rel = lambda t: t - t[0]
    charts = []
    charts.append('<h2>Frame intervals</h2>' + svg_hist([r[2] * 1e3 for r in results], np.arange(0, 102, 2), xlabel='interval ms'))
    for m in ('pan', 'walk'):
        charts.append(f'<h2>{m}: motion of each frame relative to its display interval</h2><p>1.0 = the frame advanced '
                      'exactly as far as the time it is shown for; 0 = a stalled frame; negative = it went back. '
                      'Steady motion only.</p>' +
                      svg_hist([np.clip(r[3][m]['stepRel'][r[3][m]['steady'][1:] & ~np.isnan(r[3][m]['stepRel'])], -1, 3)
                                for r in results], np.arange(-1, 3.01, .1), w=900, xlabel='relative step'))
    for m, unit in (('pan', 'px/s'), ('walk', 'px/s expansion')):
        charts.append(f'<h2>{m}: velocity per frame (shift / interval)</h2><p>A smooth capture draws one thin band; '
                      'scatter around it is judder.</p>' +
                      ''.join(svg_line([rel(r[1])[1:][~np.isnan(r[3][m]['vel'])]], [r[3][m]['vel'][~np.isnan(r[3][m]['vel'])]],
                                       ylabel=f'{r[0]["label"]} {unit}', colors=[c], dots=True)
                              for r, c in zip(results, COLORS)))
        charts.append(f'<h2>{m}: residual against a smooth path of time (px)</h2>' +
                      ''.join(svg_line([rel(r[1])], [np.where(r[3][m]['steady'], np.nan_to_num(r[3][m]['res']), 0)], ylabel=f'{r[0]["label"]} px',
                                       colors=[c], ylim=(-30, 30)) for r, c in zip(results, COLORS)))
    page = f'''<!doctype html><meta charset="utf-8"><title>Motion smoothness</title>
<style>body{{font:14px system-ui;margin:24px;max-width:960px}}table{{border-collapse:collapse}}td,th{{border:1px solid #ddd;padding:4px 8px;text-align:right}}td:first-child{{text-align:left}}</style>
<h1>Motion smoothness: per-frame global motion against frame time</h1><p>{legend}</p>
<p>Phase correlation over a tile grid in the central {int(CROP_W*100)}% x {int(CROP_H*100)}% of each frame ({W} px wide), fitted as translation plus expansion; shifts in source pixels. Judder is the rms distance of the
cumulative motion from a smooth (gaussian, sigma 60 ms) curve of the frame timestamps, during steady motion (&gt;200 px/s).
"Previous frame time" places each frame's content at the timestamp before it: lower there means frames carry the
previous interval's motion.</p>
{table}{''.join(charts)}'''
    (out / 'video.html').write_text(page)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('videos', nargs='+')
    ap.add_argument('--label', action='append', default=[])
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    labels = a.label + [pathlib.Path(v).stem for v in a.videos[len(a.label):]]
    results = []
    for v, l in zip(a.videos, labels):
        print(f'{l}: measuring {v}', file=sys.stderr)
        results.append(analyse(v, l))
    report(results, pathlib.Path(a.out))
    print(json.dumps([r[0] for r in results], indent=1, default=lambda o: None)[:4000])


if __name__ == '__main__':
    main()
