"""Owner live-run regressions, called only by the lifetime-locked nested harness."""
import contextlib,json,math,os,pathlib,signal,subprocess,time
import session as s
from ctl import verified


def acceptance(logs,recorded,children,check):
    _,env=verified()
    def wait(fn,seconds=12):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            try:
                result=fn()
                if result:return result
            except (OSError,subprocess.SubprocessError,ValueError,KeyError):pass
            time.sleep(.1)
        (logs/'regression-failure-camera.json').write_text(json.dumps(s.camera(),indent=2))
        (logs/'regression-failure-clients.json').write_text(s.ctl('clients','-j'))
        (logs/'regression-failure-monitors.json').write_text(s.ctl('monitors','-j'))
        (logs/'regression-failure-binds.json').write_text(s.ctl('binds','-j'))
        raise AssertionError('live regression timed out: '+str(fn))
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.2)
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    def pointer(*args):
        device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush()
        assert device.stdout.readline().strip()=='ok';time.sleep(.08)
    def keys(*args):
        subprocess.run(['wtype',*args],env=env,check=True,timeout=10);time.sleep(.12)
    def chord():keys('-M','logo','-P','Super_L','-s','40','-k','Tab','-p','Super_L','-m','logo')
    def qpid():
        events=[]
        for line in (logs/'shell.log').read_text().splitlines():
            try:
                event=json.loads(line)
                if event.get('type')=='presentation.peer':events.append(event['pid'])
            except ValueError:pass
        return events[-1] if events else 0
    def ui(*args):
        return subprocess.check_output(['qs','ipc','--pid',str(qpid()),'call','preview',*map(str,args)],env=env,text=True,stderr=subprocess.DEVNULL,timeout=3).strip()
    def status():return json.loads(ui('status'))
    wait(lambda:status().get('lease'))
    initial=status();check('live_like_IPC06_grant',initial['protocol']=='0.6' and bool(initial['lease']))
    check('native_workspace_inventory_without_home_bindings',len(initial['workspaces'])>0)
    s.ctl('eval','hl.bind("SUPER + Return",hl.dsp.exec_cmd("kitty --class hyprune-live-regression --title Live-launch-regression"))')
    keys('-M','logo','-P','Super_L','-s','40','-k','Return','-s','150','-p','Super_L','-m','logo')
    window=wait(lambda:next((v for v in s.camera()['captures'] if v['title']=='Live-launch-regression' and v['visible']),None))
    check('terminal_keybind_adopted_on_unbound_workspace',window['visible'] and not window['collision'])
    camera=s.camera();delta=[window['position'][i]-camera['position'][i] for i in range(3)]
    forward=[math.sin(camera['yaw'])*math.cos(camera['pitch']),math.sin(camera['pitch']),-math.cos(camera['yaw'])*math.cos(camera['pitch'])]
    depth=sum(delta[i]*forward[i] for i in range(3))
    right=[math.cos(camera['yaw']),0,math.sin(camera['yaw'])]
    check('new_terminal_is_in_front_and_in_view',depth>0 and abs(sum(delta[i]*right[i] for i in range(3))) < depth*math.tan(math.radians(30))*16/9)
    (logs/'regression-spawn.json').write_text(json.dumps({'camera':camera,'window':window},indent=2)+'\n')
    instances=s.camera()['instances'];check('new_terminal_has_current_home',any(v['surfaceId']==window['id'] and v['areaId'] for v in instances))
    app=wait(lambda:next((a for a in status()['appDetails'] if a['title']=='Live-launch-regression'),None))
    check('workspaces_list_native_terminal_app',any(w['id']==app['workspaceId'] for w in status()['workspaces']))
    # A native top layer represents the owner's launcher/bar. It must receive
    # ordinary pointer input, while hidden desktop clients remain masked.
    marker=logs/'layer-clicked';marker.unlink(missing_ok=True)
    config=logs/'layer-regression';config.mkdir(exist_ok=True)
    (config/'shell.qml').write_text('''import Quickshell
import Quickshell.Wayland
import Quickshell.Io
import QtQuick
ShellRoot {
 Process { id: clicked; command: ["touch", %s] }
 PanelWindow {
  screen: Quickshell.screens.find(s => s.name === "HEADLESS-1")
  anchors { top: true; left: true }
  width: 300; height: 80; color: "#285060"
  WlrLayershell.layer: WlrLayer.Top
  WlrLayershell.namespace: "hyprune-live-layer-regression"
  MouseArea { anchors.fill: parent; onClicked: clicked.running = true }
 }
}
'''%json.dumps(str(marker)))
    layer=subprocess.Popen(['qs','-p',str(config),'--no-color'],env=env,stdout=open(logs/'layer.log','w'),stderr=subprocess.STDOUT);children.append(layer);time.sleep(.8)
    pointer('button',275);wait(lambda:s.camera()['layerCursor'])
    check('Back_world_opens_real_layer_cursor',s.camera()['cursor']=='desktop' and s.camera()['nativeCursorVisible'])
    monitor=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    xy=list(map(float,s.ctl('cursorpos').split(',')));pointer(monitor['x']+120-xy[0],monitor['y']+40-xy[1]);pointer('button',272)
    wait(marker.exists);check('launcher_layer_receives_real_click',True)
    xy=list(map(float,s.ctl('cursorpos').split(',')));pointer(monitor['x']+700-xy[0],monitor['y']+500-xy[1]);pointer('button',272)
    check('off_layer_hidden_desktop_has_no_pointer_focus',s.camera()['layerCursor'] and not s.camera()['nativePointerFocused'])
    chord();wait(lambda:not s.camera()['layerCursor']);check('SuperTab_leaves_layer_cursor',s.camera()['mode']=='movement' and s.camera()['cursor']=='reticle')
    layer.terminate();layer.wait(timeout=5)
    # Open exactly as live, and switch all the pages whose nested layout size
    # hints crashed Qt in the owner's two coredumps.
    keys('-P','Tab','-s','1100','-p','Tab');wait(lambda:status()['director'])
    original_pid=qpid()
    for n in range(3):
        for tab in [3,4,6,0]:
            ui('tab',tab);time.sleep(.12)
            assert status()['tab']==tab and status()['director']
    check('Workspaces_and_Help_repeat_without_Qt_crash',qpid()==original_pid)
    ui('tab',4)
    # Exercise Quickshell's own crash supervisor: the replacement QML PID must
    # supersede even a retained old socket and retain the authenticated bridge.
    subprocess.run(['prlimit','--pid',str(original_pid),'--core=0'],check=True)
    os.kill(original_pid,signal.SIGSEGV)
    wait(lambda:qpid()!=original_pid,20)
    wait(lambda:status().get('status')=='live',20)
    check('Quickshell_crash_reconnects_through_owned_bridge',qpid()!=original_pid and status()['lease']==initial['lease'])
    keys('-P','Tab','-s','1100','-p','Tab');wait(lambda:status()['director'])
    ui('tab',4);time.sleep(.4);check('Help_opens_after_QML_crash_recovery',status()['director'] and status()['tab']==4)
    keys('-k','Escape');wait(lambda:not status()['director'])
    check('daemon_lifecycle_logging', 'IPC ready' in (logs/'hypruned.log').read_text())
    (logs/'regression-ui.json').write_text(json.dumps(status(),indent=2)+'\n')
