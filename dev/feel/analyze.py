#!/usr/bin/env python3
"""Motion feel of debug recordings (Ctrl+R, recording format 2): a report per recording.

    dev/feel/analyze.py REC.jsonl [REC2.jsonl ...] [--label NAME ...] --out DIR

Writes DIR/analysis.json and DIR/analysis.html (side by side for several recordings).
Light work: pure Python, fine on a laptop. Format 1 recordings have no timing; refused.

Per recording:
- presents: intervals between the backend's presentation times (p50/p90/p99/max), how many
  refreshes each took, missed refreshes;
- ticks: the dt each tick moved by, its jitter, and how well it matches the presents
  around it (a tick's dt should be the display interval of the frame it produces);
- shown frames (new draws, or new offload fronts): content time (the camera sample) and
  display time (the first present after the draw); latency = display - content, its spread
  is judder in milliseconds whatever the frame rate;
- motion per shown frame under steady motion: the step each frame takes relative to what
  its display interval should carry (1 = exact; stalled, backward, overshoot), and the
  residual of the shown yaw/position against a smooth path of display time;
- input against the camera: the yaw the raw pointer deltas asked for against the yaw shown
  (look smoothing lag);
- hitches (present intervals over 2.5x the median and over two refreshes) with the CPU
  sections of the ticks around them; section means and p95.
"""
import argparse, bisect, html, json, math, pathlib, statistics as st, sys

SIGMA = 0.060  # s, smoothing of the reference path (as dev/feel/video.py)


def load(path):
    header, frames, views, inputs, stats, marks = None, [], [], [], [], []
    with open(path) as f:
        for line in f:
            j = json.loads(line)
            k = j['kind']
            if k == 'header':
                header = j
            elif k == 'frame':
                frames.append(j)
            elif k == 'view':
                views.append(j)
            elif k == 'input':
                inputs.append(j)
            elif k == 'stats':
                stats.append(j)
            elif k == 'mark':
                marks.append(j)
    if not header or header.get('version') != 2:
        raise SystemExit(f'{path}: not a format 2 recording (no live timing)')
    return header, frames, views, inputs, stats, marks


def pct(v, q):
    if not v:
        return None
    s = sorted(v)
    return s[min(len(s) - 1, int(q * (len(s) - 1) + .5))]


def summary(v, digits=2):
    if not v:
        return None
    return {'mean': round(st.mean(v), digits), 'std': round(st.pstdev(v), digits), 'p50': round(pct(v, .5), digits),
            'p90': round(pct(v, .9), digits), 'p99': round(pct(v, .99), digits), 'max': round(max(v), digits), 'n': len(v)}


def smooth(t, y, sigma=SIGMA):
    out = []
    lo = 0
    n = len(t)
    for i, ti in enumerate(t):
        while lo < n and t[lo] < ti - 3 * sigma:
            lo += 1
        hi = lo
        sw = sy = 0.0
        while hi < n and t[hi] <= ti + 3 * sigma:
            w = math.exp(-0.5 * ((t[hi] - ti) / sigma) ** 2)
            sw += w
            sy += w * y[hi]
            hi += 1
        out.append(sy / sw)
    return out


def corr(a, b):
    if len(a) < 5:
        return None
    ma, mb = st.mean(a), st.mean(b)
    sa = math.sqrt(sum((x - ma) ** 2 for x in a))
    sb = math.sqrt(sum((x - mb) ** 2 for x in b))
    return None if not sa or not sb else round(sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (sa * sb), 3)


def analyse(path, label):
    header, frames, views, inputs, stats, marks = load(path)
    system = header.get('system', {})
    refresh = st.median(f['timing']['refreshMs'] for f in frames) if frames else 16.667
    out = {'label': label, 'file': pathlib.Path(path).name, 'monitor': system.get('monitor'), 'gpu': system.get('gpu', {}).get('renderer'),
           'renderer': system.get('profile', {}).get('renderer'), 'graphics': {k: system.get('profile', {}).get('graphics', {}).get(k)
           for k in ('quality', 'target_fps', 'offload', 'frame_pacing', 'render_scale', 'dynamic_resolution')},
           'versions': system.get('versions'), 'frames': len(frames), 'refreshMs': round(refresh, 3),
           'seconds': round((frames[-1]['tickNs'] - frames[0]['tickNs']) / 1e9, 3) if frames else 0}
    # Presents.
    presents = sorted({p for f in frames for p in f['timing'].get('presents', [])})
    intervals = [(b - a) / 1e6 for a, b in zip(presents, presents[1:])]
    refreshes = [max(1, round(i / refresh)) for i in intervals]
    out['presents'] = {'count': len(presents), 'intervalMs': summary(intervals),
                       'refreshes': {str(k): refreshes.count(k) for k in sorted(set(refreshes))},
                       'missedRefreshes': sum(r - 1 for r in refreshes)}
    # Ticks: dt against the presents around them.
    dts = [f['dt'] * 1000 for f in frames[1:]]
    out['ticks'] = {'dtMs': summary(dts), 'tickMs': summary([f['timing'].get('tickMs', 0) for f in frames])}
    before, after, dt_pairs = [], [], []
    for f in frames[1:]:
        t = f['tickNs']
        k = bisect.bisect_right(presents, t)
        if 2 <= k < len(presents):
            prev = (presents[k - 1] - presents[k - 2]) / 1e6
            nxt = (presents[k] - presents[k - 1]) / 1e6
            before.append(prev); after.append(nxt); dt_pairs.append(f['dt'] * 1000)
    out['ticks']['dtVsPreviousPresentMs'] = round(st.mean(abs(a - b) for a, b in zip(dt_pairs, before)), 3) if before else None
    out['ticks']['dtVsNextPresentMs'] = round(st.mean(abs(a - b) for a, b in zip(dt_pairs, after)), 3) if after else None
    out['ticks']['corrPrevious'] = corr(dt_pairs, before)
    out['ticks']['corrNext'] = corr(dt_pairs, after)
    # Shown frames: a new draw (in-process) or a new offload front.
    shown, last = [], None
    for v in views:
        key = ('w', v.get('front')) if v.get('worker') else ('p', v['renderedFrame'])
        if key == last:
            continue
        last = key
        k = bisect.bisect_left(presents, v['drawNs'])
        if k >= len(presents):
            continue
        shown.append({'display': presents[k] / 1e9, 'content': (v['drawNs'] - v.get('ageMs', 0) * 1e6) / 1e9,
                      'yaw': v['camera']['yaw'], 'pitch': v['camera']['pitch'], 'pos': v['camera']['position'], 'worker': v.get('worker', False)})
    # Several new frames can reach one present (a late frame): keep the last per present.
    dedup = {}
    for s in shown:
        dedup[s['display']] = s
    shown = [dedup[k] for k in sorted(dedup)]
    lat = [(s['display'] - s['content']) * 1000 for s in shown]
    out['shown'] = {'count': len(shown), 'perSecond': round(len(shown) / out['seconds'], 1) if out['seconds'] else None,
                    'latencyMs': summary(lat), 'intervalMs': summary([(b['display'] - a['display']) * 1000 for a, b in zip(shown, shown[1:])])}
    # Motion consistency per shown frame: yaw (pan) and horizontal position (walk).
    series = {}
    t = [s['display'] for s in shown]
    for name, value, threshold in (('pan', lambda s: s['yaw'], .3), ('walk', lambda s: math.hypot(s['pos'][0], s['pos'][2]), .5)):
        if len(shown) < 10:
            break
        if name == 'walk':  # distance along the path walked, not from the origin
            y, acc = [0.0], 0.0
            for a, b in zip(shown, shown[1:]):
                acc += math.hypot(b['pos'][0] - a['pos'][0], b['pos'][2] - a['pos'][2]); y.append(acc)
        else:
            y = [value(s) for s in shown]
        fit = smooth(t, y)
        speed = [abs((fit[min(i + 1, len(fit) - 1)] - fit[max(i - 1, 0)]) / max(1e-9, t[min(i + 1, len(t) - 1)] - t[max(i - 1, 0)])) for i in range(len(t))]
        rel, res = [], []
        for i in range(1, len(t) - 1):
            if speed[i] < threshold or speed[i - 1] < threshold:
                continue
            expected = fit[i] - fit[i - 1]
            if abs(expected) > 1e-9:
                rel.append((y[i] - y[i - 1]) / expected)
            res.append(y[i] - fit[i])
        unit = 'rad' if name == 'pan' else 'm'
        entry = {'steadyFrames': len(rel), 'unit': unit}
        if len(rel) > 10:
            entry.update({'stepRelative': {'p10': round(pct(rel, .1), 3), 'p50': round(pct(rel, .5), 3), 'p90': round(pct(rel, .9), 3),
                                           'stalled': round(sum(abs(r) < .15 for r in rel) / len(rel), 3),
                                           'backward': round(sum(r < -.15 for r in rel) / len(rel), 3),
                                           'overshoot': round(sum(r > 2 for r in rel) / len(rel), 3),
                                           'within25pct': round(sum(abs(r - 1) < .25 for r in rel) / len(rel), 3)},
                          'residualRms': round(math.sqrt(sum(r * r for r in res) / len(res)), 6),
                          'speedP50': round(pct([s for s in speed if s >= threshold], .5), 3)})
        out[name] = entry
        series[name] = {'t': t, 'y': y, 'fit': fit, 'rel': rel}
    # Input against the camera: the yaw asked for by raw pointer deltas (look sensitivity from
    # the recorded player settings) against the yaw each tick shows.
    sens = header.get('player', {}).get('look_sensitivity', .0025)
    dx = {}
    for e in inputs:
        ev = e['event']
        if ev.get('type') == 'motion':
            dx[e['frame']] = dx.get(e['frame'], 0) + ev['dx']
    if dx and len(frames) > 20:
        asked, shown_yaw, tt = [], [], []
        acc = frames[0]['state']['camera']['yaw']
        for f in frames:
            acc += dx.get(f['frame'], 0) * sens
            asked.append(acc); shown_yaw.append(f['state']['camera']['yaw']); tt.append(f['tickNs'] / 1e9)
        # Lag: the delay that best aligns the shown yaw with the asked one (0-150 ms).
        best = None
        for lag_ms in range(0, 151, 2):
            err, n = 0.0, 0
            for i, ti in enumerate(tt):
                k = bisect.bisect_left(tt, ti - lag_ms / 1000)
                if 0 < k < len(tt):
                    err += (shown_yaw[i] - asked[k]) ** 2; n += 1
            if n and (best is None or err / n < best[1]):
                best = (lag_ms, err / n)
        out['input'] = {'motionEvents': sum(1 for e in inputs if e['event'].get('type') == 'motion'),
                        'lookSensitivity': sens, 'lookLagMs': best[0] if best else None,
                        'residualRad': round(math.sqrt(best[1]), 6) if best else None}
        series['input'] = {'t': tt, 'asked': asked, 'shown': shown_yaw}
    # Hitches with the sections around them.
    med = pct(intervals, .5) or refresh
    hitches = []
    for a, b in zip(presents, presents[1:]):
        ms = (b - a) / 1e6
        if ms > 2.5 * med and ms > 2 * refresh:
            near = [f for f in frames if a - 2e7 <= f['tickNs'] <= b]
            secs = {}
            for f in near:
                for k, v in f['timing'].get('sections', {}).items():
                    secs[k] = secs.get(k, 0) + v
            top = sorted(secs.items(), key=lambda kv: -kv[1])[:4]
            hitches.append({'t': round((a - frames[0]['tickNs']) / 1e9, 3), 'ms': round(ms, 1),
                            'sections': {k: round(v, 2) for k, v in top}})
    out['hitches'] = hitches[:100]
    out['hitchCount'] = len(hitches)
    sec = {}
    for f in frames:
        for k, v in f['timing'].get('sections', {}).items():
            sec.setdefault(k, []).append(v)
    out['sectionsMs'] = {k: {'mean': round(sum(v) / len(frames), 3), 'p95': round(pct(v, .95), 3)}
                         for k, v in sorted(sec.items(), key=lambda kv: -sum(kv[1]))[:14]}
    gpu = [f['timing']['gpuMs'] for f in frames if f['timing'].get('gpuMs') is not None]
    out['gpuMs'] = summary(gpu)
    off = [f['timing']['offload'] for f in frames if f['timing'].get('offload')]
    if off:
        out['offload'] = {'frontAgeMs': summary([o['frontAgeMs'] for o in off if o.get('frontAgeMs', -1) >= 0]),
                          'latencyRefreshes': pct([o['latencyRefreshes'] for o in off], .5), 'divisor': pct([o['divisor'] for o in off], .5),
                          'readyP90Ms': pct([o['readyP90Ms'] for o in off], .5)}
    out['stats'] = stats
    out['marks'] = [{'name': m['name'], 'frame': m['frame']} for m in marks]
    return out, {'intervals': intervals, 'dts': dts, 'lat': lat, 'shown': shown, 'series': series}


# --- report ------------------------------------------------------------------------------------

COLORS = ['#2a7fff', '#e8590c', '#2b8a3e', '#ae3ec9']


def svg_series(xs_list, ys_list, w=900, h=170, label='', colors=COLORS, ylim=None, dots=False):
    xs_all = [x for xs in xs_list for x in xs]
    ys_all = [y for ys in ys_list for y in ys]
    if not xs_all:
        return ''
    x0, x1 = min(xs_all), max(xs_all)
    y0, y1 = ylim or (pct(ys_all, .01), pct(ys_all, .99))
    if y1 <= y0:
        y1 = y0 + 1
    sx = lambda x: 44 + (x - x0) / max(x1 - x0, 1e-9) * (w - 54)
    sy = lambda y: h - 18 - (min(max(y, y0), y1) - y0) / (y1 - y0) * (h - 30)
    p = [f'<svg width="{w}" height="{h}" xmlns="http://www.w3.org/2000/svg" style="background:#fff">',
         f'<text x="4" y="12" font-size="11" fill="#555">{html.escape(label)}</text>',
         f'<text x="4" y="{sy(y1) + 4:.0f}" font-size="10" fill="#888">{y1:.4g}</text>',
         f'<text x="4" y="{sy(y0):.0f}" font-size="10" fill="#888">{y0:.4g}</text>']
    for xs, ys, c in zip(xs_list, ys_list, colors):
        if dots:
            p += [f'<circle cx="{sx(x):.1f}" cy="{sy(y):.1f}" r="1.5" fill="{c}"/>' for x, y in zip(xs, ys)]
        else:
            p.append('<polyline fill="none" stroke="%s" stroke-width="1" points="%s"/>' % (c, ' '.join(f'{sx(x):.1f},{sy(y):.1f}' for x, y in zip(xs, ys))))
    p.append('</svg>')
    return ''.join(p)


def svg_hist(values_list, lo, hi, step, w=900, h=150, label='', colors=COLORS):
    bins = int(round((hi - lo) / step))
    counts = []
    for v in values_list:
        c = [0] * bins
        for x in v:
            i = int((min(max(x, lo), hi - 1e-9) - lo) / step)
            c[i] += 1
        counts.append([n / max(1, len(v)) for n in c])
    top = max((max(c) for c in counts if c), default=1) or 1
    bw = (w - 54) / bins
    p = [f'<svg width="{w}" height="{h}" xmlns="http://www.w3.org/2000/svg" style="background:#fff">',
         f'<text x="{w - 220}" y="12" font-size="11" fill="#555">{html.escape(label)}</text>']
    for k, (c, col) in enumerate(zip(counts, colors)):
        for i, n in enumerate(c):
            bh = n / top * (h - 34)
            p.append(f'<rect x="{44 + i * bw + k * bw / len(counts):.1f}" y="{h - 18 - bh:.1f}" width="{bw / len(counts):.1f}" height="{bh:.1f}" fill="{col}"/>')
    for i in range(0, bins + 1, max(1, bins // 10)):
        p.append(f'<text x="{44 + i * bw:.0f}" y="{h - 4}" font-size="10" fill="#666">{lo + i * step:.3g}</text>')
    p.append('</svg>')
    return ''.join(p)


def fmt(v):
    if v is None:
        return '–'
    if isinstance(v, float):
        return f'{v:.3g}' if abs(v) < 1000 else f'{v:.0f}'
    if isinstance(v, dict):
        return ' / '.join(f'{k} {fmt(x)}' for k, x in v.items() if k != 'n')
    return html.escape(str(v))


def report(results, out):
    out.mkdir(parents=True, exist_ok=True)
    (out / 'analysis.json').write_text(json.dumps([r[0] for r in results], indent=1) + '\n')
    rows = [('renderer', lambda r: r['renderer']), ('graphics', lambda r: r['graphics']),
            ('monitor', lambda r: r['monitor'] and f"{r['monitor']['width']:.0f}x{r['monitor']['height']:.0f} @ {r['monitor']['refreshHz']:.2f} Hz{' VRR' if r['monitor'].get('vrr') else ''}"),
            ('GPU', lambda r: r['gpu']), ('seconds / frames', lambda r: f"{r['seconds']} s / {r['frames']}"),
            ('present interval ms', lambda r: r['presents']['intervalMs']), ('refreshes per present', lambda r: r['presents']['refreshes']),
            ('missed refreshes', lambda r: r['presents']['missedRefreshes']),
            ('shown frames / s', lambda r: r['shown']['perSecond']), ('shown interval ms', lambda r: r['shown']['intervalMs']),
            ('latency content→display ms', lambda r: r['shown']['latencyMs']),
            ('tick dt ms', lambda r: r['ticks']['dtMs']),
            ('|dt − previous / next present| ms', lambda r: f"{fmt(r['ticks']['dtVsPreviousPresentMs'])} / {fmt(r['ticks']['dtVsNextPresentMs'])}"),
            ('pan: within ±25% / stalled / backward / overshoot', lambda r: r.get('pan', {}).get('stepRelative') and ' / '.join(fmt(r['pan']['stepRelative'][k]) for k in ('within25pct', 'stalled', 'backward', 'overshoot'))),
            ('pan: residual rms rad', lambda r: r.get('pan', {}).get('residualRms')),
            ('walk: within ±25% / stalled / backward / overshoot', lambda r: r.get('walk', {}).get('stepRelative') and ' / '.join(fmt(r['walk']['stepRelative'][k]) for k in ('within25pct', 'stalled', 'backward', 'overshoot'))),
            ('walk: residual rms m', lambda r: r.get('walk', {}).get('residualRms')),
            ('look lag ms (input→camera)', lambda r: r.get('input', {}).get('lookLagMs')),
            ('GPU ms', lambda r: r['gpuMs']), ('hitches', lambda r: r['hitchCount'])]
    table = '<table><tr><th></th>' + ''.join(f'<th>{html.escape(r[0]["label"])}</th>' for r in results) + '</tr>'
    for name, f in rows:
        table += f'<tr><td>{html.escape(name)}</td>' + ''.join(f'<td>{fmt(f(r[0]))}</td>' for r in results) + '</tr>'
    table += '</table>'
    legend = ' '.join(f'<span style="color:{c}">&#9632; {html.escape(r[0]["label"])}</span>' for r, c in zip(results, COLORS))
    parts = [f'<h2>Present intervals (ms)</h2>' + svg_hist([r[1]['intervals'] for r in results], 0, 80, 2, label='present interval ms'),
             '<h2>Tick dt (ms) over time</h2>' + ''.join(svg_series([[i / 100 for i in range(len(r[1]['dts']))]], [r[1]['dts']], label=f'{r[0]["label"]}: dt per tick', colors=[c], dots=True) for r, c in zip(results, COLORS)),
             '<h2>Latency, camera sample to display (ms), per shown frame</h2><p>A flat line is even motion; its spread is judder.</p>' +
             ''.join(svg_series([[s['display'] for s in r[1]['shown']]], [r[1]['lat']], label=r[0]['label'], colors=[c], dots=True) for r, c in zip(results, COLORS))]
    for m in ('pan', 'walk'):
        rels = [r[1]['series'].get(m, {}).get('rel', []) for r in results]
        parts.append(f'<h2>{m}: motion of each shown frame relative to its display interval</h2><p>1 = exact, 0 = stalled, negative = went back.</p>' +
                     svg_hist([[max(-1, min(3, x)) for x in v] for v in rels], -1, 3, .1, label='relative step'))
    for r, c in zip(results, COLORS):
        s = r[1]['series'].get('input')
        if s:
            parts.append(f'<h2>{html.escape(r[0]["label"])}: yaw asked by the mouse vs shown (rad)</h2>' +
                         svg_series([s['t'], s['t']], [s['asked'], s['shown']], label='asked (blue) vs shown (orange)', colors=['#2a7fff', '#e8590c']))
    hit = ''.join(f'<h3>{html.escape(r[0]["label"])}: {r[0]["hitchCount"]} hitches</h3><ul>' +
                  ''.join(f'<li>{h["t"]} s: {h["ms"]} ms ({fmt(h["sections"])})</li>' for h in r[0]['hitches'][:30]) + '</ul>' for r in results)
    secs = ''.join(f'<h3>{html.escape(r[0]["label"])}</h3><table><tr><th>section</th><th>mean ms/tick</th><th>p95 ms</th></tr>' +
                   ''.join(f'<tr><td>{html.escape(k)}</td><td>{v["mean"]}</td><td>{v["p95"]}</td></tr>' for k, v in r[0]['sectionsMs'].items()) + '</table>' for r in results)
    page = f'''<!doctype html><meta charset="utf-8"><title>Motion feel: recordings</title>
<style>body{{font:14px system-ui;margin:24px;max-width:1000px}}table{{border-collapse:collapse;margin:8px 0}}td,th{{border:1px solid #ddd;padding:4px 8px;text-align:right}}td:first-child{{text-align:left}}</style>
<h1>Motion feel from debug recordings</h1><p>{legend}</p>{table}{''.join(parts)}<h2>Hitches</h2>{hit}<h2>CPU sections per tick</h2>{secs}'''
    (out / 'analysis.html').write_text(page)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('recordings', nargs='+')
    ap.add_argument('--label', action='append', default=[])
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    labels = a.label + [pathlib.Path(p).stem for p in a.recordings[len(a.label):]]
    results = [analyse(p, l) for p, l in zip(a.recordings, labels)]
    report(results, pathlib.Path(a.out))
    for r in results:
        x = r[0]
        print(json.dumps({k: x.get(k) for k in ('label', 'renderer', 'presents', 'shown', 'ticks', 'pan', 'walk', 'input', 'hitchCount')}, default=str)[:1500])


if __name__ == '__main__':
    main()
