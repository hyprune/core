"""Autodrive over stairs and ramps in real packages, inside run.sh's lifetime flock.

Every walking-graph edge whose ends differ by >= 0.3 m in height is driven in both
directions in walk mode: start on one node, route to the other, autodrive, and
require arrival on the target's footprint at its floor. Packages:
HYPRUNE_AUTODRIVE_WORLDS (os.pathsep-separated package directories), default the
built Lumen Reach in ../worlds/dist.
"""
import json, math, os, pathlib, shutil, time
import session as s
from m2 import wait, pose

ROUTES = 'com.hyprune.lumen-reach.routes'


def packages():
    default = str(s.ROOT.parent / 'worlds/dist/com.hyprune.lumen-reach')
    return [pathlib.Path(p) for p in os.environ.get('HYPRUNE_AUTODRIVE_WORLDS', default).split(os.pathsep) if p]


def prepare(installed):
    for package in packages():
        manifest = json.loads((package / 'world.json').read_text())
        shutil.rmtree(installed / manifest['id'], ignore_errors=True)
        shutil.copytree(package, installed / manifest['id'])


def acceptance(client, children, args, shell_token, socketpath):
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    results, failures, active = {}, [], False
    for package in packages():
        manifest = json.loads((package / 'world.json').read_text())
        client.call('world.load', {'worldId': manifest['id']})
        if not active:
            s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: state()['runtime']['active']); active = True
        wait(lambda: state()['spatial']['world'] and state()['spatial']['world']['id'] == manifest['id'], timeout=60)
        client.call('movement.set', {'modeId': 'com.hyprune.walk'})
        graph = manifest.get('extensions', {}).get(ROUTES, {})
        nodes = {n['id']: n['position'] for n in graph.get('nodes', [])}
        edges = [(e['from'], e['to']) for e in graph.get('edges', []) if abs(nodes[e['from']][1] - nodes[e['to']][1]) >= .3]
        rows = []
        for a, b in edges + [(b, a) for a, b in edges]:
            start, goal = nodes[a], nodes[b]
            pose(client, [start[0], start[1] + 1.7, start[2]]); time.sleep(.4)
            client.call('route.set', {'points': [{'position': [goal[0], goal[1] + 1.7, goal[2]]}]})
            client.call('autodrive.start', {'speed': 3.5})
            length = math.dist(start, goal)
            wait(lambda: not state()['location']['autodrive'], timeout=10 + 3 * length / 3.5)
            end = state()['movement']['position']
            flat = math.hypot(end[0] - goal[0], end[2] - goal[2]); rise = end[1] - 1.7 - goal[1]
            ok = flat < .1 and abs(rise) < .35
            rows.append({'from': a, 'to': b, 'climb': round(goal[1] - start[1], 2), 'flat': round(flat, 3), 'height': round(rise, 3), 'arrived': ok})
            print(('PASS' if ok else 'FAIL') + f" autodrive {manifest['id']}: {a} -> {b} climb {goal[1] - start[1]:+.2f} m, off by {flat:.2f} m / {rise:+.2f} m", flush=True)
            if not ok: failures.append((manifest['id'], a, b))
        results[manifest['id']] = {'edges': len(edges), 'drives': rows}
    (s.STATE / 'autodrive.json').write_text(json.dumps(results, indent=2) + '\n')
    assert not failures, failures
    print('PASS autodrive: ' + ', '.join(f"{k} {len(v['drives'])} drives" for k, v in results.items()), flush=True)
