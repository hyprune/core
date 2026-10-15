#!/usr/bin/env python3
"""The humanised demo driver (dev/nested/human.py): deterministic per seed, ends where asked,
turns no faster than a hand, overshoots and corrects, Fitts-timed pointer moves that land on
target, natural typing delays, and recordings' look smoothing keeps the total turn and order."""
import json, math, pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'dev/nested'))
import human


def check(ok, why):
    if not ok:
        raise SystemExit('FAIL: ' + why)


def track(seed):
    tr = human.CameraTrack([0, 1.7, 0], 0.0, 0.0, seed=seed)
    tr.idle(1.0).look_at([5, 1.2, -5]).idle(.5)
    tr.walk([[2, 1.7, -4], [0, 1.7, -9], [-3, 1.7, -12]], speed=1.35, glances=[(.5, [4, 1, -8], 1.2)]).idle(.6)
    return tr


a, b, c = track(7).keys(), track(7).keys(), track(8).keys()
check(a == b and a != c, 'a seed repeats a take exactly; another seed differs')
end = a[-1]['eye']
check(math.dist([end[0], 1.7, end[2]], [-3, 1.7, -12]) < .02, f'the walk ends where asked ({end})')
turn = max(abs(human.wrap(q['yaw'] - p['yaw'])) for p, q in zip(a, a[1:]))
check(math.degrees(turn) < 8, f'the view turns at most 8 degrees a frame at 30 Hz ({math.degrees(turn):.1f})')
bob = [k['eye'][1] for k in a if k['t'] > 6 and k['t'] < 9]
check(max(bob) - min(bob) > .02, 'a head bob while walking')
look = human.CameraTrack([0, 1.7, 0], 0.0, 0.0, seed=3, drift=0)
start = len(look.keys())
look.turn(1.0, .2, overshoot=math.radians(2))
yaws = [k['yaw'] for k in look.keys()[start:]]
check(max(yaws) > 1.0 + math.radians(1.5) and abs(yaws[-1] - 1.0) < 1e-3, 'a look overshoots, then settles on the target')
metrics = human.track_metrics(a)
check(metrics['linear']['steadyShare'] < .7 and metrics['angular']['steadyShare'] < .2, f'no long constant-speed stretches {metrics}')

sent, pressed = [], []
p = human.Pointer(send=lambda dx, dy: sent.append((dx, dy)), press=lambda b, d: pressed.append((b, d)), at=(100, 100), seed=5, sleep=lambda s: None)
p.move((900, 520), width=40).click()
x, y = 100 + sum(d[0] for d in sent), 100 + sum(d[1] for d in sent)
check(abs(x - 900) <= 1 and abs(y - 520) <= 1, f'the pointer lands on the target ({x}, {y})')
check(pressed == [(272, 1), (272, 0)], 'a click presses and releases')
check(max(math.hypot(px - 100 - (900 - 100) * 0, 0) for _, px, _ in p.trace) > 905 or max(t[1] for t in p.trace) > 900.5,
      'the pointer overshoots a little')
near, far = p.fitts_seconds(100, 40), human.Pointer(send=None, seed=5).fitts_seconds(1600, 40)
check(far > near, 'Fitts: farther takes longer')
args = human.typing_args('git log --oneline', seed=1)
typed, i = '', 0
while i < len(args):
    if args[i] == '-s':
        check(int(args[i + 1]) >= 18, 'a key delay of at least 18 ms'); i += 2
    elif args[i] == '-k':
        typed += {'minus': '-'}[args[i + 1]]; i += 2
    else:
        check(not args[i].startswith('-'), 'no text argument wtype would read as an option'); typed += args[i]; i += 1
check(typed == 'git log --oneline', f'typing types the text with per-key delays ({typed!r})')

rows = [{'kind': 'header'}]
ns = 0
for f in range(60):
    for k in range(f % 3):
        ns += 1000; rows.append({'kind': 'input', 'frame': f, 'ns': ns, 'event': {'type': 'motion', 'dx': 2.0, 'dy': -1.0, 'mode': 'world'}})
    ns += 16_000_000; rows.append({'kind': 'frame', 'frame': f, 'ns': ns, 'state': {}})
rows.append({'kind': 'end', 'frames': 60, 'ns': ns + 1})
out = human.smooth_recording_look(rows, 100)
total = lambda rr: (sum(r['event']['dx'] for r in rr if r['kind'] == 'input'), sum(r['event']['dy'] for r in rr if r['kind'] == 'input'))
check(all(abs(u - v) < 1e-6 for u, v in zip(total(rows), total(out))), 'look smoothing keeps the total turn')
times = [r['ns'] for r in out if 'ns' in r]
check(times == sorted(times) and out[-1]['kind'] == 'end', 'look smoothing keeps the recording in time order')
per = [r['event']['dx'] for r in out if r['kind'] == 'input' and 10 < r['frame'] < 50]
check(max(per) - min(per) < 1.0, 'look smoothing evens the per-tick motion out')
print('human driver: deterministic takes, walks, looks, pointer, typing, recording smoothing')
