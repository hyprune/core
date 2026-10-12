#!/usr/bin/env python3
"""IPC 0.20 in a nested session: flock /tmp/worldshell-nested.lock python3 dev/nested/pocket_check.py

1. surface.pocket / surface.unpocket on a real window in the atrium room: pocketing hides it and
   lists it in editor.unplaced.windows (idempotent); unpocketing draws it again (placed); the
   error paths answer -32004 (not pocketed), -32003 (unknown window), -32602 (bad params).
2. With HYPRUNE_M2_PACKAGE pointing at a package that carries com.hyprune.blueprint, loading it
   publishes spatial.map.levels to this 0.20 client (ids, ordered bands, class-carrying SVG).
Writes docs/evidence/ipc-0.20.json. Nested only.
"""
import argparse, json, os, subprocess, time
import session as s
import m3b
from m2 import wait


def acceptance(client, children, args, shell_token, socketpath):
    checks = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        checks.append({'name': name, 'evidence': data}); print('PASS 0.20:', name, flush=True)

    def call_error(method, params):
        try:
            client.call(method, params)
        except RuntimeError as error:
            return error.args[0]['code']
        return None

    check('IPC 0.20 negotiated with the pocket methods', client.hello['protocol'] == '0.20'
          and {'surface.pocket', 'surface.unpocket'} <= set(client.hello['methods']), client.hello['protocol'])
    client.call('world.load', {'worldId': 'org.hyprune.atrium'})
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'])
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    children.append(s.envrun('kitty', '--title', 'Pocket-A', 'bash', '--noprofile', '--norc',
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    capture = wait(lambda: next((c for c in s.camera()['captures'] if c['title'] == 'Pocket-A' and c['visible']), None), 20)
    sid = capture['id']
    pocketed = lambda: [w['surfaceId'] for w in state()['editor']['unplaced']['windows']]
    visible = lambda: next(c for c in s.camera()['captures'] if c['id'] == sid)['visible']
    check('the window is placed and drawn before pocketing', sid not in pocketed() and visible(), sid)
    result = client.call('surface.pocket', {'surfaceId': sid})
    check('surface.pocket returns a revision', set(result) == {'revision'}, result)
    wait(lambda: not visible(), 5)
    check('a pocketed window is listed in editor.unplaced and not drawn', sid in pocketed() and not visible(), pocketed())
    again = client.call('surface.pocket', {'surfaceId': sid})
    check('pocketing again changes nothing', pocketed().count(sid) == 1 and again['revision'] >= result['revision'], again)
    out = client.call('surface.unpocket', {'surfaceId': sid})
    check('surface.unpocket places it in front of the player', out.get('placed') is True and set(out) == {'revision', 'placed'}, out)
    wait(lambda: visible(), 5)
    check('an unpocketed window is drawn and no longer listed', visible() and sid not in pocketed(), pocketed())
    # No free full-size spot (looking straight down at the floor): it stays pocketed, selected,
    # with the Placement tool on, and the answer says so.
    client.call('surface.pocket', {'surfaceId': sid}); wait(lambda: not visible(), 5)
    s.ctl('eval', 'hl.plugin.hyprune.camera(0, 1.7, 2, 0, -1.55)'); time.sleep(.4)
    blocked = client.call('surface.unpocket', {'surfaceId': sid}); time.sleep(.3)
    editor = state()['editor']
    check('with no free spot it stays pocketed, selected for the Placement tool', blocked.get('placed') is False and
          sid in pocketed() and editor['unplaced']['selectedId'] == sid and editor['tool'] == 5,
          {'result': blocked, 'tool': editor['tool'], 'selected': editor['unplaced']['selectedId']})
    s.ctl('eval', 'hl.plugin.hyprune.camera(0, 1.7, 2, 0, 0)'); time.sleep(.3)
    client.call('surface.unpocket', {'surfaceId': sid}); wait(lambda: visible(), 5)
    check('unpocketing a window that is not pocketed answers -32004', call_error('surface.unpocket', {'surfaceId': sid}) == -32004)
    check('an unknown window answers -32003', call_error('surface.pocket', {'surfaceId': 'surface-nope'}) == -32003)
    check('bad params answer -32602', call_error('surface.pocket', {}) == -32602 and
          call_error('surface.unpocket', {'surfaceId': sid, 'anchorId': 'x'}) == -32602)
    package = os.environ.get('HYPRUNE_M2_PACKAGE')
    if package:
        world_id = json.loads((s.pathlib.Path(package) / 'world.json').read_text())['id']
        client.call('world.load', {'worldId': world_id}); time.sleep(.5)
        levels = state()['spatial']['map'].get('levels') or []
        check('a blueprint world publishes spatial.map.levels to 0.20 clients',
              len(levels) >= 1 and all(a['maxY'] <= b['minY'] for a, b in zip(levels, levels[1:])) and
              all(' class="' in l['svg'] and len(l['sha256']) == 64 for l in levels),
              [{'id': l['id'], 'name': l['name'], 'minY': l['minY'], 'maxY': l['maxY'], 'bytes': len(l['svg'])} for l in levels])
    client.call('runtime.exit', {})
    (s.ROOT / 'docs/evidence/ipc-0.20.json').write_text(json.dumps({'nestedOnly': True, 'checks': checks}, indent=2) + '\n')
    print('PASS IPC 0.20:', len(checks), 'checks', flush=True)


if __name__ == '__main__':
    os.environ['HYPRUNE_NESTED_PROTOCOL'] = '0.20'
    fixture = s.state_dir('pocket-check'); fixture.mkdir(exist_ok=True)
    for name in ['input.json', 'layout.json', 'editor.json', 'graphics.json']: (fixture / name).unlink(missing_ok=True)
    m3b.acceptance = acceptance
    s.run(argparse.Namespace(state='pocket-check', m3b=True, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False, record=False, visible=False))
