#!/usr/bin/env python3
"""Run with flock /tmp/worldshell-nested.lock python3 dev/nested/protocol_acceptance.py.
Uses shell's real-seat hold/graphics acceptance; adds adapter capability checks.
Only the PID/socket-verified nested compositor is controlled.
"""
import argparse
import importlib.util
import json
import os
import pathlib

import session as s
import m3b

shell = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
spec = importlib.util.spec_from_file_location('shell_followup', shell / 'dev/followup-core.py')
followup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(followup)

# Persisted user poses exercise the actual Plugin placements.list adapter. The
# unmatched rule avoids disturbing shell's visual/keyboard fixture windows.
placement = {
    'worldId': 'com.hyprune.lumen-reach', 'areaId': 'sun-court', 'anchorId': '',
    'rule': {'appClass': 'protocol-pose-fixture', 'title': 'Saved user mount'},
    'pose': {'position': [0, 2, 1], 'rotation': [0, 0, 0, 1]}, 'size': [1.4, 0.8],
}

def acceptance(client, children, args, shell_token, socketpath):
    followup.acceptance(client, children, args, shell_token, socketpath)
    subscription = next(item['response']['result']['subscriptionId'] for item in client.transcript
                        if item['method'] == 'state.subscribe')
    state = client.call('state.snapshot', {'subscriptionId': subscription})['snapshot']['state']
    exclusive = [o for lease in state['shell']['leases'] for o in lease['overlays']
                 if o['visible'] and o['exclusiveInput']]
    assert exclusive, 'Director is visible without exclusive input'
    listed = client.call('placements.list', {})['placements']
    assert placement in listed, ('placements.list dropped custom pose', listed)
    assert placement in state['mounts']['placements'], 'snapshot lost custom pose'
    evidence = {'protocol': client.hello['protocol'], 'exclusiveOverlays': exclusive,
                'placement': placement, 'shellAcceptance': 'HoldRing progress/cancel/fire, Settings real dropdowns'}
    (s.STATE / 'protocol-acceptance.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('PASS protocol inheritance: Director exclusive input and native placement pose', flush=True)

if __name__ == '__main__':
    fixture = s.STATE_ROOT / '.m3b-state'
    fixture.mkdir(exist_ok=True)
    (fixture / 'layout.json').write_text(json.dumps({'version': 1, 'bindings': [], 'placements': [placement]}))
    # Deterministic per-run graphics; no owner files are read or replaced.
    (fixture / 'graphics.json').write_text('{}\n')
    m3b.acceptance = acceptance
    s.run(argparse.Namespace(m3b=True, m3a=False, m2=False, m1=False, integration=False,
                             live=False, serve=False, record=False, visible=False))
