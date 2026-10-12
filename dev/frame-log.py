#!/usr/bin/env python3
"""Hitches in a frame log (HYPRUNE_FRAME_LOG): presentation gaps over a threshold where
a tick in the gap asked for a frame (render on demand: a static scene presents nothing,
which is not a hitch). Each is attributed to the tick sections, renderer draw time and
IPC methods inside the gap, and to the demo chapter it falls in (a timeline written by
dev/nested/catalog_demo.py, optional).

    dev/frame-log.py LOG.jsonl [--timeline T.json] [--threshold 25] [--json OUT]
"""
import argparse, bisect, json, statistics, sys

parser = argparse.ArgumentParser()
parser.add_argument('log')
parser.add_argument('--timeline')
parser.add_argument('--threshold', type=float, default=25)
parser.add_argument('--json')
args = parser.parse_args()
events = [json.loads(l) for l in open(args.log)]
presents = sorted(e['t'] for e in events if e['kind'] == 'present')
ticks = sorted((e for e in events if e['kind'] == 'tick'), key=lambda e: e['t'])
draws = sorted((e for e in events if e['kind'] == 'draw'), key=lambda e: e['t'])
renders = sorted((e for e in events if e['kind'] == 'render'), key=lambda e: e['t'])
render_t = [e['t'] for e in renders]
tick_t = [e['t'] for e in ticks]
draw_t = [e['t'] for e in draws]
chapters = json.load(open(args.timeline)) if args.timeline else []


def chapter(t):
    name = 'rehearsal' if chapters and t < chapters[0]['monotonicMs'] else ''
    for c in chapters:
        if c['monotonicMs'] <= t:
            name = c['action']
    return name


def within(times, items, a, b):
    return items[bisect.bisect_left(times, a):bisect.bisect_left(times, b)]


hitches, frames = [], []
for a, b in zip(presents, presents[1:]):
    gap = b - a
    inside = within(tick_t, ticks, a, b)
    wanted = any('frame' in t.get('marks', []) for t in inside)
    frames.append({'t': a, 'ms': gap, 'wanted': wanted})
    if gap <= args.threshold or not wanted:
        continue
    sections = {}
    for t in inside:
        for k, v in t['sections'].items():
            sections[k] = sections.get(k, 0) + v
    tick_ms = sum(t['ms'] for t in inside)
    draw_ms = sum(d['ms'] for d in within(draw_t, draws, a, b))
    render_ms = sum(d['ms'] for d in within(render_t, renders, a, b))  # the compositor's frames (draw included)
    methods = sorted({m for t in inside for m in t.get('marks', []) if m != 'frame'})
    top = sorted(((v, k) for k, v in sections.items() if k not in ('tools', 'objects') or len(sections) == 1), reverse=True)[:4]
    hitches.append({'t': round(a, 1), 'ms': round(gap, 1), 'tickMs': round(tick_ms, 1), 'drawMs': round(draw_ms, 1),
                    'renderMs': round(render_ms, 1), 'otherMs': round(gap - tick_ms - max(draw_ms, render_ms), 1), 'top': [[k, round(v, 1)] for v, k in top],
                    'methods': methods, 'chapter': chapter(a)})
wanted = [f['ms'] for f in frames if f['wanted']]
summary = {'presents': len(presents), 'wantedFrames': len(wanted), 'hitches': len(hitches),
           'p50': round(statistics.median(wanted), 2) if wanted else None,
           'p99': round(sorted(wanted)[int(len(wanted) * .99)], 2) if wanted else None,
           'max': round(max(wanted), 1) if wanted else None}
print(json.dumps(summary))
for h in hitches:
    print(f"{h['t']:12.1f} {h['ms']:6.1f} ms  tick {h['tickMs']:5.1f} draw {h['drawMs']:5.1f} frame {h['renderMs']:5.1f} other {h['otherMs']:5.1f}  "
          f"{', '.join(f'{k} {v}' for k, v in h['top'])}  {' '.join(h['methods'])}  [{h['chapter']}]")
if args.json:
    json.dump({'summary': summary, 'hitches': hitches, 'frames': frames}, open(args.json, 'w'))
