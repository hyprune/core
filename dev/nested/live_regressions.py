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
        try:(logs/'regression-failure-ui.json').write_text(json.dumps(status(),indent=2))
        except Exception:pass
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
    # Carry follows camera heading and preserves an off-centre grab.
    carry_modifier=s.envrun('wtype','-M','logo','-P','Super_L','-s','3500','-p','Super_L','-m','logo');children.append(carry_modifier)
    wait(lambda:s.camera()['superHeld']);pointer('button',272,1)
    carried_id=wait(lambda:s.camera()['gestureId'] if s.camera()['gesture']==1 else None)
    initial_heading=s.camera()['yaw'];pointer(210,0)
    turned=wait(lambda:s.camera() if s.camera()['yaw']>initial_heading+.45 else None)
    carried=next(v for v in turned['captures'] if v['id']==carried_id)
    q=carried['rotation'];normal=[2*(q[0]*q[2]+q[3]*q[1]),2*(q[1]*q[2]-q[3]*q[0]),1-2*(q[0]*q[0]+q[1]*q[1])]
    forward=[math.sin(turned['yaw'])*math.cos(turned['pitch']),math.sin(turned['pitch']),-math.cos(turned['yaw'])*math.cos(turned['pitch'])]
    check('carried_window_faces_player_after_right_turn',sum(-normal[i]*forward[i] for i in range(3))>.98 and not carried['collision'])
    pointer('button',272,0);carry_modifier.wait(timeout=5);pointer(-210,0)
    # Retest round 2: motion conventions, actual debug drawing, and deferred
    # paired captures through the same live bootstrap as the owner.
    before=s.camera()['pitch'];pointer(0,-40)
    wait(lambda:s.camera()['pitch']>before+.03)
    check('mouse_up_looks_up_default',True)
    before=s.camera()['pitch'];pointer(0,40);wait(lambda:s.camera()['pitch']<before-.03)
    input_path=pathlib.Path(recorded['runRoot'])/'input.json' if 'runRoot' in recorded else pathlib.Path(recorded['runroot'])/'input.json'
    config=json.loads(input_path.read_text());config['invert_y']=True
    temp=input_path.with_suffix('.tmp');temp.write_text(json.dumps(config));temp.replace(input_path);time.sleep(.7)
    before=s.camera()['pitch'];pointer(0,-40);wait(lambda:s.camera()['pitch']<before-.03)
    check('invert_y_live_reload_reverses_only_pitch',True)
    config['invert_y']=False;temp.write_text(json.dumps(config));temp.replace(input_path);time.sleep(.7)
    keys('-k','F3');wait(lambda:s.camera()['debug'])
    check('F3_core_debug_enabled',s.camera()['timings']['frameMs']>0)
    keys('-k','u')
    capture_dir=pathlib.Path(s.camera()['captureDirectory'])
    capture_json=wait(lambda:next((p for p in sorted(capture_dir.glob('capture-*.json'),reverse=True) if p.stat().st_mtime>recorded['since']),None))
    dump=json.loads(capture_json.read_text());png=pathlib.Path(dump['screenshot'])
    check('U_persists_JSON_and_core_FBO_PNG',png.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n' and dump['frame']['width']>0 and dump['frame']['height']>0)
    check('capture_files_private',capture_json.stat().st_mode&0o777==0o600 and png.stat().st_mode&0o777==0o600)
    (logs/'round2-capture.json').write_text(json.dumps(dump,indent=2)+'\n')
    keys('-k','y');wait(lambda:not s.camera()['debug']);check('Y_core_debug_disabled',True)
    check('area_markers_default_off',not s.camera()['overlays']['areaMarkers'])
    ui('select',0);ui('routeTo',0)
    wait(lambda:s.camera()['overlays']['routeSegments']>0)
    ui('overlayStyle');wait(lambda:s.camera()['overlayStyle']['waypointColor']==[1,0,0])
    check('lease_owned_theme_hook_applies_live',True)
    before=s.camera()['yaw'];pointer(25,0)
    aligned=wait(lambda:s.camera() if abs(s.camera()['overlays']['heading']-s.camera()['yaw'])<.001 and s.camera()['yaw']>before+.04 else None)
    check('core_markers_use_frame_camera',abs(aligned['overlays']['heading']-aligned['yaw'])<.001)
    config['show_area_markers']=True;temp.write_text(json.dumps(config));temp.replace(input_path)
    wait(lambda:s.camera()['overlays']['areaMarkers']);check('area_markers_option_reloads',True)
    config['show_area_markers']=False;temp.write_text(json.dumps(config));temp.replace(input_path)
    wait(lambda:not s.camera()['overlays']['areaMarkers'])
    if os.environ.get('HYPRUNE_NESTED_4K')=='1':
        output=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
        assert (output['width'],output['height'])==(3840,2160),'4K test needs actual 4K FBO'
        time.sleep(2);samples=[]
        for n in range(80):
            pointer(1 if n%2 else -1,0);samples.append(s.camera()['timings'])
        (logs/'4k-profile.json').write_text(json.dumps({'resolution':[3840,2160],'world':'Lumen Reach','debug':False,'samples':samples},indent=2)+'\n')
        check('4K_core_cpu_gpu_profile',all(v['cpuRenderMs']>0 for v in samples))
        check('core_GPU_queue_is_bounded',all(v['inFlight']<=2 for v in samples))
        check('auto_Intel_quality_applied_on_world_load',all('Intel' not in v['renderer'] or (v['quality']=='intel' and v['msaaSamples']<=2) for v in samples))
        check('auto_render_scale_preserves_native_output',all(v['nativeSize']==[3840,2160] and .375<=v['renderScale']<=1 for v in samples))
        baseline=[]
        s.ctl('eval','hl.plugin.hyprune.config({graphics={render_scale=1}})');time.sleep(1)
        for n in range(40):
            pointer(1 if n%2 else -1,0);baseline.append(s.camera()['timings'])
        check('fixed_native_scale_reload',all(v['internalSize']==[3840,2160] for v in baseline))
        before=[(v['id'],v['size'],v['pixels']) for v in s.camera()['captures']]
        s.ctl('eval','hl.plugin.hyprune.config({graphics={render_scale=0.5,target_fps=90}})')
        wait(lambda:s.camera()['timings']['internalSize']==[1920,1080])
        check('scale_reload_keeps_native_windows',before==[(v['id'],v['size'],v['pixels']) for v in s.camera()['captures']] and s.camera()['timings']['targetFPS']==90)
        s.ctl('eval','hl.plugin.hyprune.config({graphics={render_scale="auto",target_fps=60}})');time.sleep(2)
        (logs/'render-scale-profile.json').write_text(json.dumps({'resolution':[3840,2160],'auto':samples,'native':baseline},indent=2)+'\n')

    def capture_in(mode):
        previous=time.time();keys('-M','logo','-P','Super_L','-k','u','-p','Super_L','-m','logo')
        result=wait(lambda:next((p for p in capture_dir.glob('capture-*.json') if p.stat().st_mtime>=previous),None),20)
        assert json.loads(result.read_text())['mode']==mode
        return result
    keys('-k','e');wait(lambda:s.camera()['mode']=='typing')
    capture_in('interactive');check('U_works_in_window_interactive_mode',True)
    chord();wait(lambda:s.camera()['mode']=='movement')
    keys('-k','f');wait(lambda:s.camera()['fullscreen']==2)
    keys('-k','F3');wait(lambda:s.camera()['debug'])
    focused_capture=capture_in('focus');focused_dump=json.loads(focused_capture.read_text())
    check('F3_and_U_work_in_native_focus',focused_dump['frame']['source']=='last-completed-world-frame' and focused_dump['frame']['ageMs']>=0)
    chord();wait(lambda:s.camera()['fullscreen']==0)
    keys('-k','F3');wait(lambda:not s.camera()['debug'])
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
    original_connections=(logs/'shell.log').read_text().count('"type": "presentation.peer"')
    for n in range(3):
        for tab in [3,4,6,0]:
            ui('tab',tab);time.sleep(.12)
            assert status()['tab']==tab and status()['director']
    check('Workspaces_and_Help_repeat_without_Qt_crash',qpid()==original_pid)
    keys('-M','logo','-P','Super_L','-k','u','-p','Super_L','-m','logo');wait(lambda:any(p.stat().st_mtime>capture_json.stat().st_mtime for p in capture_dir.glob('capture-*.json')))
    check('U_works_in_Director_menu',True)
    ui('tab',4)
    # Exercise Quickshell's own crash supervisor: the replacement QML PID must
    # supersede even a retained old socket and retain the authenticated bridge.
    subprocess.run(['prlimit','--pid',str(original_pid),'--core=0'],check=True)
    os.kill(original_pid,signal.SIGSEGV)
    wait(lambda:(logs/'shell.log').read_text().count('"type": "presentation.peer"')>original_connections,20)
    wait(lambda:status().get('status')=='live',20)
    check('Quickshell_crash_reconnects_through_owned_bridge',(logs/'shell.log').read_text().count('"type": "presentation.peer"')>original_connections and status()['lease']==initial['lease'])
    keys('-P','Tab','-s','1100','-p','Tab');wait(lambda:status()['director'])
    ui('tab',4);time.sleep(.4);check('Help_opens_after_QML_crash_recovery',status()['director'] and status()['tab']==4)
    keys('-k','Escape');wait(lambda:not status()['director'])
    check('daemon_lifecycle_logging', 'IPC ready' in (logs/'hypruned.log').read_text())
    (logs/'regression-ui.json').write_text(json.dumps(status(),indent=2)+'\n')
