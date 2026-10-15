#!/usr/bin/env python3
"""Tool 3 (Path) editing: dev/nested/isolated.py dev/nested/path_edit.py [OUT_DIR]

Nested only. In the Lumen Reach Sun Court: paths hide outside path mode and show (with
points) in tool 3; clicks add points; the point under the reticle highlights; pressing on
it and dragging moves it (Shift snaps to 0.1 m), release commits; Ctrl+Z / Ctrl+Shift+Z
undo and redo the move; Escape cancels a drag; secondary on a hovered point deletes that
point; a click on a segment inserts a point; period shows all paths outside tool 3.
Stills and path-edit.json go to OUT_DIR (default: the state dir); evidence to
docs/evidence/path-edit.json.
"""
import argparse, json, math, os, pathlib, subprocess, time
import session as s
import m3b
from m2 import wait, keys

LUMEN = 'com.hyprune.lumen-reach'
EYE = [-1.4, 1.7, 12.7]
FLOOR = 0.  # aim at the Sun Court paving (y 0); points sit 3 cm above it, compared in x and z


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output or s.STATE); out.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        checks.append({'name': name, 'evidence': data}); print('PASS path edit:', name, flush=True)
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    def paths():
        return [p for p in state()['editor']['paths'] if p['worldId'] == LUMEN]

    def diag():
        return s.camera()['path']

    def look(target, eye=EYE):
        d = [target[i] - eye[i] for i in range(3)]
        s.ctl('eval', f'hl.plugin.hyprune.camera({eye[0]},{eye[1]},{eye[2]},{math.atan2(d[0], -d[2])},{math.atan2(d[1], math.hypot(d[0], d[2]))})')
        time.sleep(.35)

    def click(button=272):  # nested-pointer: 'button CODE' clicks, 'button CODE 1' / '0' press / release
        pointer('button', button); time.sleep(.35)

    def shot(name):
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True)

    def held(mods, fn, seconds=1.4):
        a, tail = [], []
        for name, key in mods:
            a += ['-M', name, '-P', key]
        for name, key in reversed(mods):
            tail += ['-p', key, '-m', name]
        hold = s.envrun('wtype', *a, '-s', str(int(seconds * 1000)), *tail); children.append(hold)
        time.sleep(.25); fn(); hold.wait(timeout=8); time.sleep(.2)

    def near(a, b, tol=.08):
        return math.dist([a[0], a[2]], [b[0], b[2]]) < tol

    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=60)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    # The reticle is the pointer: centre the virtual pointer on the output.
    for _ in range(4):
        xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)

    targets = [[-2.5, FLOOR, 7.5], [0.0, FLOOR, 6.0], [2.5, FLOOR, 7.5]]
    keys('-k', '3'); wait(lambda: state()['editor']['tool'] == 3)
    check('path mode shows paths', diag()['visible'], diag())
    for t in targets:
        look(t); click()
    ps = paths(); check('clicks add three points', len(ps) == 1 and len(ps[0]['points']) == 3, ps)
    pid = ps[0]['id']; pts = lambda: next(p for p in paths() if p['id'] == pid)['points']
    keys('-k', '1'); time.sleep(.3)
    check('paths hide outside path mode', not diag()['visible'], diag()); shot('1-tool1-hidden.png')
    keys('-k', '3'); time.sleep(.3)
    look(targets[1]); wait(lambda: diag()['hover'] and diag()['hover']['point'] == 1, timeout=4)
    check('the point under the reticle highlights', diag()['hover']['point'] == 1 and diag()['hover']['path'] == pid, diag()); shot('2-hover.png')

    # Drag point 1 to a new place; release commits.
    goal = [0.0, FLOOR, 4.5]
    pointer('button', 272, 1); wait(lambda: diag()['dragging'], timeout=4)
    look(goal); time.sleep(.3); shot('3-dragging.png')
    pointer('button', 272, 0); wait(lambda: not diag()['dragging'], timeout=4)
    moved = pts()[1]['position']
    check('press and drag moves the hovered point', near(moved, goal), {'point': moved, 'goal': goal})
    check('the drag kept the path otherwise', near(pts()[0]['position'], targets[0]) and near(pts()[2]['position'], targets[2]), pts())

    ctrl, shift = [('ctrl', 'Control_L')], [('shift', 'Shift_L')]
    keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'z', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.5)
    check('Ctrl+Z undoes the move', near(pts()[1]['position'], targets[1]), pts()[1])
    keys('-M', 'ctrl', '-P', 'Control_L', '-M', 'shift', '-P', 'Shift_L', '-k', 'z', '-p', 'Shift_L', '-m', 'shift', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.5)
    check('Ctrl+Shift+Z redoes it', near(pts()[1]['position'], goal), pts()[1])
    check('Ctrl in path mode does not move the player', math.dist(s.camera()['position'], EYE) < .05, s.camera()['position'])

    # Escape cancels a drag.
    look(goal); wait(lambda: diag()['hover'] and diag()['hover']['point'] == 1, timeout=4)
    pointer('button', 272, 1); wait(lambda: diag()['dragging'], timeout=4)
    look([1.0, FLOOR, 3.0]); keys('-k', 'Escape'); time.sleep(.4); pointer('button', 272, 0); time.sleep(.3)
    check('Escape cancels a drag', near(pts()[1]['position'], goal), pts()[1])

    # Ctrl (the sprint key) snaps the dragged point to the 0.1 m grid.
    look(goal); wait(lambda: diag()['hover'] and diag()['hover']['point'] == 1, timeout=4)
    pointer('button', 272, 1); wait(lambda: diag()['dragging'], timeout=4)
    held(ctrl, lambda: (look([0.537, FLOOR, 4.163]), time.sleep(.3), pointer('button', 272, 0), time.sleep(.3)))
    snapped = pts()[1]['position']
    check('Ctrl snaps the drag to 0.1 m', all(abs(v * 10 - round(v * 10)) < 1e-3 for v in (snapped[0], snapped[2])), snapped)

    # A click on a segment inserts a point there.
    a, b = pts()[1]['position'], pts()[2]['position']
    mid = [(a[0] + b[0]) / 2, FLOOR, (a[2] + b[2]) / 2]
    look(mid); wait(lambda: diag()['hover'] and diag()['hover']['segment'] == 2, timeout=4); shot('4-segment.png')
    click()
    check('a click on a segment inserts a point', len(pts()) == 4 and near(pts()[2]['position'], mid, .15), pts())

    # Secondary on a hovered point deletes that point, not the last one.
    look(targets[0]); wait(lambda: diag()['hover'] and diag()['hover']['point'] == 0, timeout=4)
    last = pts()[-1]['position']; click(273)
    check('secondary deletes the hovered point', len(pts()) == 3 and pts()[-1]['position'] == last and not near(pts()[0]['position'], targets[0]), pts())
    keys('-M', 'ctrl', '-P', 'Control_L', '-k', 'z', '-p', 'Control_L', '-m', 'ctrl'); time.sleep(.5)
    check('Ctrl+Z restores the deleted point', len(pts()) == 4 and near(pts()[0]['position'], targets[0]), pts())
    look([0.0, FLOOR, 6.5], [-1.4, 4.5, 12.7]); shot('5-path-mode.png')

    # Period shows every path outside path mode too.
    keys('-k', 'period'); time.sleep(.3); keys('-k', '1'); time.sleep(.3)
    check('period shows all paths outside path mode', diag()['visible'] and diag()['showAll'], diag()); shot('6-tool1-show-all.png')
    keys('-k', '3'); time.sleep(.2); keys('-k', 'period'); time.sleep(.2); keys('-k', '1'); time.sleep(.3)
    check('and hides them again', not diag()['visible'], diag())
    report = {'nestedOnly': True, 'world': LUMEN, 'checks': checks, 'path': next(p for p in paths() if p['id'] == pid)}
    (out / 'path-edit.json').write_text(json.dumps(report, indent=1) + '\n')
    (s.ROOT / 'docs/evidence/path-edit.json').write_text(json.dumps({k: report[k] for k in ('nestedOnly', 'world', 'checks')}, indent=1) + '\n')
    print('PASS path edit:', len(checks), 'checks', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output', nargs='?'); a = parser.parse_args()
    os.environ['HYPRUNE_NESTED_PROTOCOL'] = '0.22'
    m3b.acceptance = acceptance
    s.run(argparse.Namespace(state='path-edit', m3b=True, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False, record=False, visible=False, output=a.output))
