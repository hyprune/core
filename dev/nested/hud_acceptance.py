#!/usr/bin/env python3
"""flock /tmp/worldshell-nested.lock python3 dev/nested/hud_acceptance.py"""
import argparse, json, pathlib, subprocess, time, os
import session as s
import m3b
from m2 import wait, keys, pose
from ctl import verified

def acceptance(client, children, args, shell_token, socketpath):
    client.call('world.load', {'worldId': 'com.hyprune.lumen-reach'})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda:state()['runtime']['active'])
    output=state()['runtime']['outputs'][0]
    lease=client.call('shell.claim',{'outputs':[output]})['leaseId']
    # A real mapped Wayland child, authenticated through the same native PID / lease check as shell.
    qml=s.STATE/'hud-fixture.qml'
    qml.write_text('''import QtQuick
import Quickshell
import Quickshell.Wayland
PanelWindow {
 screen: Quickshell.screens.find(s => s.name === "HEADLESS-1")
 anchors { top:true; right:true }
 implicitWidth:200; implicitHeight:32; color:"transparent"
 WlrLayershell.namespace:"hyprune-hud"
 WlrLayershell.layer:WlrLayer.Overlay
 WlrLayershell.keyboardFocus:WlrKeyboardFocus.None
 Text { text:"IPC HUD regression"; color:"white" }
}''')
    _,env=verified()
    hud=subprocess.Popen(['qs','-p',str(qml)],env=env,stdout=open(s.STATE/'hud-fixture.log','w'),stderr=subprocess.STDOUT)
    children.append(hud);time.sleep(1)
    client.call('shell.bind',{'leaseId':lease,'pid':hud.pid})
    hs=client.call('hud.subscribe',{'topics':['hud.pose','hud.frame','zone.entered','zone.exited','tool.changed']})['subscriptionId']
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    pose(client,[-8,2,12])
    start=len(client.events)
    until=time.monotonic()+2
    while time.monotonic()<until:
        client.call('session.ping',{});time.sleep(.02)
    events=client.events[start:]
    poses=[e['params'] for e in events if e.get('method')=='hud.pose']
    frames=[e['params'] for e in events if e.get('method')=='hud.frame']
    assert 10<=len(poses)<=61, len(poses)
    assert 3<=len(frames)<=9, len(frames)
    assert all(p['subscriptionId']==hs and p['zone']['homeName']=='Sun Court' for p in poses)
    # 5 is tool 5 (Placement) since the IPC 0.15 owner corrections; hud.cycle is unbound by
    # default. Bind it to a free key, then one physical press, regardless of repeat.
    config=client.call('input.get',{})['config']
    assert not config['modes']['world'].get('hud.cycle'),config['modes']['world'].get('hud.cycle')
    config['modes']['world']['hud.cycle']=['7'];client.call('input.set',{'config':config})
    before=state()['editor']['tool']
    keys('-P','7','-s','900','-p','7');client.call('session.ping',{})
    cycles=[e for e in client.events if e.get('method')=='ui.hud.cycle']
    assert len(cycles)==1, cycles
    assert cycles[0]['params']['leaseId']==lease and cycles[0]['params']['outputId']==output
    assert state()['editor']['tool']==before
    keys('-k','2');wait(lambda:state()['editor']['tool']==2);time.sleep(.1);client.call('session.ping',{})
    assert any(e.get('method')=='tool.changed' and e['params']['slot']==2 for e in client.events)
    pose(client,[0,2,-40]);time.sleep(.2);client.call('session.ping',{})
    assert any(e.get('method')=='zone.entered' and e['params']['zone']['id']=='array-hall' for e in client.events)
    assert any(e.get('method')=='zone.exited' and e['params']['zone']['id']=='sun-court' for e in client.events)
    # Rebind without compositor reload, and verify the former key stops cycling.
    config=client.call('input.get',{})['config']
    config['modes']['world']['hud.cycle']=['6'];client.call('input.set',{'config':config})
    keys('-k','7');keys('-k','6');client.call('session.ping',{})
    assert len([e for e in client.events if e.get('method')=='ui.hud.cycle'])==2
    client.call('hud.unsubscribe',{'subscriptionId':hs});client.call('session.ping',{})
    start=len(client.events);time.sleep(.4);client.call('session.ping',{})
    assert not any(e.get('method','').startswith('hud.') for e in client.events[start:])
    evidence={'protocol':client.hello['protocol'],'poseSamples':len(poses),'frameSamples':len(frames),
              'sampleFrame':frames[-1],'samplePose':poses[-1],'cycle':cycles[0],
              'checks':['scoped nonrepeat cycle','no tool mutation','zone exit/enter','tool changed','live rebind','unsubscribe']}
    (s.STATE/'hud-acceptance.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('PASS HUD 0.9',json.dumps(evidence),flush=True)

if __name__=='__main__':
    os.environ['HYPRUNE_NESTED_PROTOCOL']='0.9'
    fixture=s.state_dir('hud-acceptance');fixture.mkdir(exist_ok=True)
    for name in ['input.json','layout.json','editor.json']:(fixture/name).unlink(missing_ok=True)
    m3b.acceptance=acceptance
    s.run(argparse.Namespace(state='hud-acceptance',m3b=True,m3a=False,m2=False,m1=False,integration=False,live=False,serve=False,record=False,visible=False))
