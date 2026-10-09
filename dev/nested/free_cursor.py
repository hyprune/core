#!/usr/bin/env python3
"""Nested-only free cursor transitions; invoke under the lifetime flock."""
import argparse,json,os,pathlib,subprocess,time
import session as s,m3b
from m2 import wait,keys,pose,pointer
from ctl import verified

def acceptance(client,children,args,token,socket):
    results=[]
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    def pointer(*args):
        device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.06)
    def check(name,ok):
        assert ok,name
        results.append(name);print('PASS cursor:',name,flush=True)
    def chord():keys('-M','logo','-P','Super_L','-s','40','-k','Tab','-p','Super_L','-m','logo')
    def back():pointer('button',275)
    client.call('world.load',{'worldId':'org.hyprune.m3b'})
    client.call('workspace.bind',{'id':10,'worldId':'org.hyprune.m3b','areaId':'sun-court'})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    def mode():return state()['editor']['mode']
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:state()['runtime']['active'])
    client.call('movement.set',{'modeId':'com.hyprune.fly'});pose(client,[0,2.3,4]);time.sleep(.3)
    initial=s.camera();before=state()['movement']['position'];chord();wait(lambda:mode()=='cursor')
    check('world SuperTab shows real cursor',s.camera()['nativeCursorVisible'])
    keys('-P','w','-s','300','-p','w');pointer(70,45);time.sleep(.2)
    check('cursor freezes movement and camera',state()['movement']['position']==before and abs(s.camera()['yaw']-initial['yaw'])<.0001 and abs(s.camera()['pitch']-initial['pitch'])<.0001)
    debug=state()['editor']['debug'];keys('-k','F3');check('free cursor does not consume debug binding',state()['editor']['debug']==debug)
    back();wait(lambda:mode()=='world');check('Back restores world',not s.camera()['nativeCursorVisible'])
    terminal_process=s.envrun('kitty','--class','cursor-terminal','--title','M3B terminal','script','-q','-f',str(s.STATE/'cursor-terminal.log'),'-c','bash --noprofile --norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(terminal_process)
    terminal=wait(lambda:next((v for v in state()['surfaces'] if v['title']=='M3B terminal'),None));sid=terminal['id']
    time.sleep(.3)
    def target():
        p=next(v for v in s.camera()['captures'] if v['id']==sid)['position'];pose(client,[p[0],p[1],p[2]+3]);time.sleep(.3)
    target();keys('-k','e');wait(lambda:mode()=='interactive')
    before=state()['movement']['position'];back();wait(lambda:mode()=='world');chord();wait(lambda:mode()=='cursor');back();wait(lambda:mode()=='world')
    check('E use exits to world with original camera',state()['movement']['position']==before)
    client.call('workspace.unfocus',{});wait(lambda:mode()=='world')
    target();keys('-k','f');wait(lambda:s.camera()['fullscreen']==2)
    chord();wait(lambda:mode()=='world' and s.camera()['fullscreen']==0)
    back();wait(lambda:mode()=='cursor');chord();wait(lambda:mode()=='world');check('F window exits without cursor trap',True)
    pose(client,[0,3,-10]);time.sleep(.3);keys('-k','f');wait(lambda:state()['mounts']['desktopWorkspaceId']==10)
    back();wait(lambda:mode()=='world');chord();wait(lambda:mode()=='cursor');back();wait(lambda:mode()=='world')
    check('F workspace focus exits without cursor trap',True)
    client.call('workspace.unfocus',{});wait(lambda:mode()=='world');time.sleep(.4)
    pose(client,[0,3,-10]);time.sleep(.3);keys('-k','e');wait(lambda:mode()=='interactive')
    before=state()['movement']['position'];back();wait(lambda:mode()=='world');chord();wait(lambda:mode()=='cursor');back();wait(lambda:mode()=='world')
    check('E workspace exits without cursor trap',state()['movement']['position']==before)
    client.call('workspace.unfocus',{});wait(lambda:mode()=='world')
    target();hold=s.envrun('wtype','-M','logo','-P','Super_L','-s','1800','-p','Super_L','-m','logo');children.append(hold)
    wait(lambda:s.camera()['superHeld']);pointer('button',272,1);wait(lambda:s.camera()['gesture']!=0)
    back();wait(lambda:mode()=='cursor');pointer('button',272,0);hold.wait(timeout=5)
    check('carry ends and release balanced',s.camera()['gesture']==0 and s.camera()['pressedButtons']==0)
    chord();wait(lambda:mode()=='world');target();back();wait(lambda:mode()=='cursor')
    monitors=json.loads(s.ctl('monitors','-j'));mon=next(v for v in monitors if v['name']=='HEADLESS-1');other=next(v for v in monitors if v['name']!='HEADLESS-1')
    def move(x,y):
        xy=list(map(float,s.ctl('cursorpos').split(',')));pointer(x-xy[0],y-xy[1]);time.sleep(.15)
    move(other['x']+200,other['y']+200)
    check('real pointer leaves output',mode()=='cursor' and s.camera()['nativeCursorVisible'])
    move(mon['x']+mon['width']/mon['scale']/2,mon['y']+mon['height']/mon['scale']/2)
    check('return to output stays free',mode()=='cursor' and s.camera()['nativeCursorVisible'])
    pointer('button',272);check('world projected click release balanced',s.camera()['pressedButtons']==0)
    marker=s.STATE/'cursor-typed';marker.unlink(missing_ok=True)
    keys(f"printf cursor > {marker}");keys('-k','Return');wait(lambda:marker.exists() and marker.read_text()=='cursor')
    check('native typing into projected window',marker.read_text()=='cursor')
    pointer('button',272,1);wait(lambda:s.camera()['pressedButtons']==1)
    move(other['x']+200,other['y']+200);pointer('button',272,0)
    check('projected drag releases on another output',s.camera()['pressedButtons']==0)
    move(mon['x']+mon['width']/mon['scale']/2,mon['y']+mon['height']/mon['scale']/2)
    # A launcher-like native layer must retain ordinary pointer routing.
    released=s.STATE/'cursor-layer-released';released.unlink(missing_ok=True)
    marker=s.STATE/'cursor-layer';marker.unlink(missing_ok=True);config=s.STATE/'cursor-panel';config.mkdir(exist_ok=True)
    (config/'shell.qml').write_text('''import Quickshell
import Quickshell.Wayland
import Quickshell.Io
import QtQuick
ShellRoot { Process { id: hit; command: ["touch", %s] }
 Process { id: release; command: ["touch", %s] }
 PanelWindow { screen: Quickshell.screens.find(s => s.name === "HEADLESS-1")
 anchors { top: true; left: true } width: 300; height: 80; color: "#285060"
 WlrLayershell.layer: WlrLayer.Top
 MouseArea { anchors.fill: parent; onClicked: hit.running = true; onReleased: release.running = true }
 }}'''%(json.dumps(str(marker)),json.dumps(str(released))))
    _,env=verified();layer=subprocess.Popen(['qs','-p',str(config),'--no-color'],env=env,stdout=open(s.STATE/'cursor-layer.log','w'),stderr=subprocess.STDOUT);children.append(layer);time.sleep(.8)
    move(mon['x']+100,mon['y']+40);pointer('button',272);wait(marker.exists);check('native layer clickable',True)
    released.unlink(missing_ok=True);pointer('button',272,1)
    move(mon['x']+20,mon['y']+mon['height']/mon['scale']-20);pointer('button',272,0);wait(released.exists)
    check('native layer drag releases over world',True)
    pointer(1,0);pointer('button',272)
    check('empty world swallowed',not s.camera()['nativePointerFocused'])
    move(other['x']+200,other['y']+200);chord();wait(lambda:mode()=='world');check('SuperTab on other output returns',not s.camera()['nativeCursorVisible'])
    target();keys('-k','e');wait(lambda:mode()=='interactive');back();wait(lambda:mode()=='world');back();wait(lambda:mode()=='cursor')
    terminal_process.terminate();terminal_process.wait(timeout=5);time.sleep(.3);chord();wait(lambda:mode()=='world')
    check('closed return target safely restores world',True)
    back();wait(lambda:mode()=='cursor')
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:not state()['runtime']['active'])
    marker=s.STATE/'native-super-tab';marker.unlink(missing_ok=True);chord();wait(marker.exists);check('deactivation restores native SuperTab',True)
    (s.ROOT/'docs/evidence/free-cursor.json').write_text(json.dumps({'checks':results},indent=2)+'\n')

if __name__=='__main__':
    (s.HERE/'.m3b-state/layout.json').unlink(missing_ok=True)
    os.environ['HYPRUNE_NESTED_PROTOCOL']='0.12';m3b.acceptance=acceptance
    s.run(argparse.Namespace(m3b=True,m3a=False,m2=False,m1=False,integration=False,live=False,serve=False,record=False,visible=False))
