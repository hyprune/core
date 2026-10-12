"""M3B real seat acceptance; lifetime flock belongs to run.sh."""
import builtins,json,math,os,pathlib,subprocess,time
import session as s
from m2 import wait,keys as nativekeys,pose,shot
def keys(*args):
 nativekeys(*args);time.sleep(.12)
from ctl import verified
ROOT=s.ROOT

def acceptance(client,children,args,shell_token,socketpath):
 results={}
 def check(name,condition,evidence=None):
  assert condition,(name,evidence);results[name]={'passed':True,'evidence':evidence};print('PASS M3B:',name,flush=True)
 children.append(s.envrun('wtype','-s','300000'));time.sleep(.2)
 device=s.envrun(str(ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
 def pointer(*args):
  device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.08)
 s.ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
 children.append(s.envrun('kitty','--class','hyprune-m3b-terminal','--title','M3B terminal','--override','font_size=18','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL));time.sleep(1)
 for w in json.loads(s.ctl('clients','-j')):
  a=w['address'];s.ctl('eval',f'hl.dispatch(hl.dsp.window.float({{window="address:{a}",action="enable"}}))');s.ctl('eval',f'hl.dispatch(hl.dsp.window.resize({{window="address:{a}",x=900,y=560,relative=false}}))')
 for _ in range(6):
  xy=list(map(float,s.ctl('cursorpos').split(',')));pointer(2560-xy[0],540-xy[1]);time.sleep(.04)
 s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
 client.call('world.load',{'worldId':'org.hyprune.m3b'});client.call('workspace.bind',{'id':10,'worldId':'org.hyprune.m3b','areaId':'sun-court'})
 sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
 def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
 def diag():return s.camera()
 def cap(id):return builtins.next(v for v in diag()['captures'] if v['id']==id)
 def unrelatedIn(mode):
  marker=s.STATE/'native-unrelated';marker.unlink(missing_ok=True);keys('-M','logo','-P','Super_L','-s','40','-k','F9','-p','Super_L','-m','logo');wait(marker.exists);check('noncolliding_native_'+mode,True)
 def chord():keys('-M','logo','-P','Super_L','-s','40','-k','Tab','-p','Super_L','-m','logo');time.sleep(.5)
 def back():pointer('button',275);time.sleep(.5)
 native=s.STATE/'native-super-tab';native.unlink(missing_ok=True)
 keys('-M','logo','-P','Super_L','-s','100','-k','Tab','-s','100','-p','Super_L','-m','logo');wait(native.exists);check('steal_inactive_restore',True);native.unlink()
 s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:state()['runtime']['active']);time.sleep(.4)
 # Core graphics 0.7 controls coexist with the unmodified 0.6 shell.
 original_graphics=client.call('graphics.get',{})
 authored_exposure=original_graphics['effective']['exposure']
 from jsonschema import Draft202012Validator
 amendment=json.loads((ROOT/'docs/graphics-ipc-amendment.json').read_text())
 validator=Draft202012Validator({'$defs':amendment['$defs'],'$ref':'#/$defs/graphics.get.result'})
 validator.validate(original_graphics)
 (s.STATE/'graphics-get.json').write_text(json.dumps(original_graphics,indent=2)+'\n')
 check('graphics_07_response_contract',True)
 for preset in ['low','medium','high','ultra']:
  result=client.call('graphics.set',{'patch':{'quality':preset}})
  check('graphics_preset_'+preset,result['effective']['quality']==preset)
 for option,values in original_graphics['options'].items():
  if option=='quality':continue
  for value in values:
   result=client.call('graphics.set',{'patch':{option:value}})
   check('graphics_option_'+option+'_'+str(value),result['effective'][option]==value)
  if option!='dynamic_resolution':client.call('graphics.set',{'patch':{option:None}})
 client.call('graphics.set',{'patch':{'exposure':2.5}})
 check('graphics_exposure_override',client.call('graphics.get',{})['effective']['exposure']==2.5)
 graphics_path=pathlib.Path(original_graphics['path'])
 replacement=graphics_path.with_suffix('.new');replacement.write_text(json.dumps({'quality':'low','bloom':'high'}));replacement.replace(graphics_path)
 wait(lambda:client.call('graphics.get',{})['effective']['bloom']=='high')
 check('graphics_atomic_reload',client.call('graphics.get',{})['effective']['quality']=='low')
 replacement.write_text('{"msaa":999}');replacement.replace(graphics_path)
 wait(lambda:bool(client.call('graphics.get',{})['error']))
 check('graphics_invalid_reload_keeps_last_good',client.call('graphics.get',{})['effective']['msaa']==1)
 replacement.write_text('{}');replacement.replace(graphics_path)
 wait(lambda:client.call('graphics.get',{})['overrides']=={})
 check('graphics_clear_restores_device_preset',not client.call('graphics.get',{})['error'])
 check('graphics_clear_restores_authored_exposure',client.call('graphics.get',{})['effective']['exposure']==authored_exposure)
 # Real shell, same verified association as M3A.
 read,write=os.pipe();os.write(write,(shell_token+'\n').encode());os.close(write);_,environment=verified();environment.update(HYPRUNE_TOKEN_FD=str(read),HYPRUNE_PROTOCOL='0.6',HYPRUNE_SOCKET=str(socketpath),HYPRUNE_OUTPUT_SCREEN='HEADLESS-1',HYPRUNE_DEV_AUTOMATION='1')
 shell=subprocess.Popen(['python3',str(pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR',str(ROOT.parent/'shell')))/'bridge/launch.py'),'--protocol','0.6'],env=environment,pass_fds=(read,),stdout=open(s.STATE/'shell.log','w'),stderr=subprocess.STDOUT);os.close(read);children.append(shell)
 def childpid():
  assert shell.poll() is None,'shell exited; see shell.log'
  return builtins.next((int(p) for p in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split() if pathlib.Path(f'/proc/{p}/comm').read_text().strip()=='qs'),None)
 qpid=wait(childpid)
 def ui(*args):return subprocess.check_output([str(s.HERE/'ctl.py'),'env','qs','ipc','--pid',str(qpid),'call','preview',*map(str,args)],text=True).strip()
 def status():
  try:return json.loads(ui('status'))
  except (subprocess.CalledProcessError,json.JSONDecodeError):return {}
 wait(lambda:status().get('lease'))
 client.call('movement.set',{'modeId':'com.hyprune.fly'})
 terminal=wait(lambda:builtins.next((v for v in state()['surfaces'] if v['title']=='M3B terminal'),None));sid=terminal['id']
 def target():
  p=cap(sid)['position'];pose(client,[p[0],p[1],p[2]+3]);time.sleep(.3)
 pose(client,[0,2.3,4]);client.call('surface.pull',{'surfaceId':sid});time.sleep(.3);target()
 # Free cursor (docs/FREE-CURSOR.md) owns Super+Tab in world: it toggles the real cursor and never reaches native.
 chord();wait(lambda:s.camera()['layerCursor']);time.sleep(.3);check('world_super_tab_free_cursor_not_native',not native.exists())
 chord();wait(lambda:not s.camera()['layerCursor']);check('world_super_tab_returns',s.camera()['mode']=='movement');target()
 before=state()['movement']['position'];keys('-k','e');wait(lambda:state()['editor']['mode']=='interactive');check('E_window_in_place',state()['movement']['position']==before)
 marker=s.STATE/'typed.txt';marker.unlink(missing_ok=True);keys(f"printf 'M3B use window\\n' > {marker}");keys('-k','Return');wait(marker.exists);check('relative_cursor_keyboard',marker.read_text()=='M3B use window\n');unrelatedIn('interactive')
 pointer(40,10);time.sleep(.2);check('relative_cursor_moves',diag()['pointer']!=[.5,.5],diag()['pointer'])
 chord();check('SuperTab_interactive_exit_steals',state()['editor']['mode']=='world' and not native.exists());target();keys('-k','e');
 for _ in range(12):
  if diag()['cursor']=='desktop':break
  pointer(-400,0)
 check('virtual_cursor_free_other_output',diag()['cursor']=='desktop',diag()['pointer']);chord();check('SuperTab_other_output_exit_steals',state()['editor']['mode']=='world' and not native.exists());chord();wait(lambda:s.camera()['layerCursor']);time.sleep(.3);check('mode_change_world_super_tab_free_cursor',not native.exists());chord();wait(lambda:not s.camera()['layerCursor'])
 client.call('surface.place',{'surfaceId':sid,'anchorId':'occluded-window'});pose(client,[2,1.8,1]);time.sleep(.3);keys('-k','e');check('E_tolerance_preserves_geometry_occlusion',state()['editor']['mode']=='world',diag())
 client.call('surface.place',{'surfaceId':sid,'anchorId':'sun-court-slot-01-operator-desk-01'});time.sleep(.3);plane=cap(sid);p=plane['position'];pose(client,[p[0]+plane['size'][0]/2+.06,p[1],p[2]+3]);time.sleep(.3);keys('-k','e');check('E_use_tolerance_outside_window_edge',state()['editor']['mode']=='interactive',{'surface':plane,'diag':diag()});back()
 target();pointer();time.sleep(.2);check('normal_LMB_keeps_world',state()['editor']['mode']=='world');
 # Round 3: real client events, with mixed backing buffer scales.
 probes=[]
 for scale in [1,2]:
  title='Round3 pointer '+str(scale);log=s.STATE/('round3-pointer-'+str(scale)+'.log')
  proc=s.envrun('env','GDK_BACKEND=wayland','GDK_SCALE='+str(scale),'HYPRUNE_POINTER_TEST=1',str(ROOT/'build/nested-parity-client'),str(s.STATE/'round3-marker'),title,stdout=subprocess.DEVNULL,stderr=open(log,'w'));children.append(proc)
  probe=wait(lambda:builtins.next((v for v in state()['surfaces'] if v['title']==title),None));pid=probe['id']
  native_probe=builtins.next(v for v in json.loads(s.ctl('clients','-j')) if v['title']==title)
  address=native_probe['address'];s.ctl('eval',f'hl.dispatch(hl.dsp.window.float({{window="address:{address}",action="enable"}}))');s.ctl('eval',f'hl.dispatch(hl.dsp.window.resize({{window="address:{address}",x=800,y=500,relative=false}}))')
  pose(client,[0,2.3,4]);client.call('surface.pull',{'surfaceId':pid});time.sleep(.4)
  plane=cap(pid);p=plane['position'];pose(client,[p[0],p[1],p[2]+3]);time.sleep(.3)
  hold=s.envrun('wtype','-P','Super_L','-s','650','-p','Super_L');children.append(hold);wait(lambda:diag()['superHeld']);pointer('button',272,1);pointer('button',272,0);hold.wait(timeout=5)
  plane=cap(pid);check('logical_scale_'+str(scale),all(abs(plane['size'][n]-plane['pixels'][n]*.002)<.001 for n in [0,1]),plane)
  pointer('button',272,1);pointer(25,0);pointer('button',272,0);time.sleep(.2)
  text=log.read_text();check('plain_drag_release_'+str(scale),'fixture button 1' in text and len(set(line for line in text.splitlines() if line.startswith('fixture drag ')))>1 and 'fixture release 1' in text,text)
  check('plain_click_stays_world_'+str(scale),state()['editor']['mode']=='world')
  pointer(-25,0);time.sleep(.2);pointer('button',272,1);pointer(500,0);pointer('button',272,0);time.sleep(.2)
  text=log.read_text();check('off_window_release_balanced_'+str(scale),text.count('fixture button 1')==text.count('fixture release 1'),text)
  proc.terminate();proc.wait(timeout=5);time.sleep(.2)
 pose(client,[0,2.3,4]);client.call('surface.pull',{'surfaceId':sid});time.sleep(.3);target()
 stolen=s.STATE/'round3-native-w';stolen.unlink(missing_ok=True)
 s.ctl('eval',f'hl.bind("SUPER + w",hl.dsp.exec_cmd("touch {stolen}"))')
 mod=s.envrun('wtype','-M','logo','-P','Super_L','-s','900','-P','w','-s','700','-p','w','-s','2200','-p','Super_L','-m','logo');children.append(mod);wait(lambda:diag()['superHeld']);pointer('button',272,1);wait(lambda:diag()['gesture']==1)
 before=state()['movement']['position'];time.sleep(1.7);check('carry_SuperW_moves_and_steals',state()['movement']['position']!=before and not stolen.exists(),{'before':before,'camera':diag(),'stolen':stolen.exists()})
 rotation=cap(sid)['rotation'];pointer(0,-90);time.sleep(.3);after=cap(sid)
 check('carry_pitch_preserves_orientation',all(abs(a-b)<.0001 for a,b in zip(rotation,after['rotation'])),after)
 pointer('button',272,0);mod.wait(timeout=6)
 keys('-M','logo','-P','Super_L','-k','w','-p','Super_L','-m','logo');wait(stolen.exists);check('SuperW_native_restored_after_carry',True)
 target();keys('-k','f');wait(lambda:diag()['fullscreen']==2);check('F_window_fullscreen',state()['editor']['mode']=='focus');shot('window-fullscreen.png');unrelatedIn('window_focus');chord();wait(lambda:diag()['fullscreen']==0);check('SuperTab_fullscreen_exit_steals',not native.exists())
 # Workspace wall faces the camera at its authored pose.
 client.call('surface.return',{'surfaceId':sid});pose(client,[0,3,-10]);time.sleep(.3)
 before=state()['movement']['position'];keys('-k','e');wait(lambda:state()['editor']['mode']=='interactive');check('E_workspace_in_place',state()['movement']['position']==before and state()['mounts']['desktopWorkspaceId'] is None);wallmarker=s.STATE/'workspace-typed.txt';wallmarker.unlink(missing_ok=True);keys(f"printf 'M3B workspace in place\\n' > {wallmarker}");keys('-k','Return');wait(wallmarker.exists);check('E_workspace_keyboard',wallmarker.read_text()=='M3B workspace in place\n');back();check('Back_interactive_exit',state()['editor']['mode']=='world')
 keys('-k','f');wait(lambda:state()['mounts']['desktopWorkspaceId']==10);check('F_workspace_fullscreen',state()['editor']['mode']=='focus');unrelatedIn('workspace_focus');chord();wait(lambda:state()['mounts']['desktopWorkspaceId'] is None);check('SuperTab_workspace_exit_steals',not native.exists())
 # With the world active, new windows map in the world room (workspace 20 since the room move): pin the popup to the bound wall.
 s.ctl('eval','hl.window_rule({name="m3b-popup",match={title="^M3B workspace popup$"},workspace="10 silent"})')
 popupmarker=s.STATE/'workspace-popup.txt';popupmarker.unlink(missing_ok=True);children.append(s.envrun('env','GDK_BACKEND=wayland',str(ROOT/'build/nested-parity-client'),str(popupmarker),'M3B workspace popup',stdout=subprocess.DEVNULL,stderr=open(s.STATE/'popup.log','w')))
 popup=wait(lambda:builtins.next((v for v in state()['surfaces'] if v['title']=='M3B workspace popup'),None));client.call('surface.return',{'surfaceId':popup['id']});pose(client,[0,3,-10]);time.sleep(.3);keys('-k','e');wait(lambda:state()['editor']['mode']=='interactive');pointer('button',273);child=wait(lambda:builtins.next((v for v in diag()['childSurfaces'] if v['parentId']==popup['id']),None));check('workspace_popup_composed',not child['visible'],child);shot('workspace-popup.png')
 nativewindow=builtins.next(w for w in json.loads(s.ctl('clients','-j')) if w['title']=='M3B workspace popup');u=(nativewindow['at'][0]-1600+child['offset'][0]+child['pixels'][0]*.5)/1920;v=(nativewindow['at'][1]+child['offset'][1]+child['pixels'][1]*.5)/1080;pointer((u-.5)*1280,(v-.5)*720);pointer();wait(popupmarker.exists);check('interactive_virtual_popup_click',popupmarker.read_text()=='popup click landed\n');pointer('scroll',1);wait(lambda:pathlib.Path(str(popupmarker)+'.scroll').exists());check('interactive_virtual_scroll',True);back()
 zoom=s.envrun('wtype','-P','q','-s','900','-p','q');children.append(zoom);time.sleep(.2);first=state()['presentation']['fov'];pointer('scroll',-4);time.sleep(.2);check('Q_scope_wheel',state()['presentation']['fov']<first,(first,state()['presentation']['fov']));shot('scope.png');zoom.wait(timeout=5)
 keys('-k','F3');check('F3_debug',state()['editor']['debug']);keys('-k','y');check('Y_debug',not state()['editor']['debug'])
 keys('-k','u');captures=s.STATE/'statehome/hyprune/captures';wait(lambda:list(captures.glob('*.json')));captured=json.loads(sorted(captures.glob('*.json'))[-1].read_text());check('U_debug_capture',all(k in captured for k in ['camera','mode','tool','surfaces','placements','recentIPC','recentEvents','input']),str(captures))
 # Every movement variant uses actual keyboard events, with reset poses.
 for key in ['k','w','Up','j','s','Down','h','a','Left','l','d','Right']:
  pose(client,[0,2.3,4]);before=state()['movement']['position'];keys('-P',key,'-s','140','-p',key);check('move_'+key,state()['movement']['position']!=before)
 pose(client,[0,2.3,4]);keys('-P','space','-s','180','-p','space');check('Space_fly_up',state()['movement']['position'][1]>2.4)
 pose(client,[0,2.3,4]);keys('-P','Control_L','-s','180','-p','Control_L');check('Ctrl_fly_down',state()['movement']['position'][1]<2.2)
 pose(client,[0,2.3,4]);keys('-P','w','-s','250','-p','w');normal=4-state()['movement']['position'][2];pose(client,[0,2.3,4]);keys('-M','shift','-P','Shift_L','-P','w','-s','250','-p','w','-p','Shift_L','-m','shift');boost=4-state()['movement']['position'][2];check('Shift_boost',boost>normal*1.2,(normal,boost))
 keys('-k','v');check('V_noclip',state()['interaction']['noclip']);keys('-k','v');check('V_collision_restore',not state()['interaction']['noclip']);keys('-k','F4');check('F4_fly_toggle',state()['movement']['modeId']=='com.hyprune.walk');keys('-k','F4');check('F4_back_fly',state()['movement']['modeId']=='com.hyprune.fly')
 keys('-k','z');state();check('Z_transport_notice',any(v['method']=='runtime.notice' and v['params']['code']=='transport.pending' for v in client.events))
 client.call('route.set',{'points':[{'position':[0,2.3,-8]}]});keys('-k','x');check('X_autopilot_on',state()['location']['autodrive'],{'diag':diag(),'location':state()['location']});keys('-k','x');check('X_autopilot_off',not state()['location']['autodrive'])
 client.call('movement.set',{'modeId':'com.hyprune.walk'});pose(client,[0,1.7,4]);crouch=s.envrun('wtype','-P','Control_L','-s','850','-p','Control_L');children.append(crouch);time.sleep(.2);captured=diag();check('Ctrl_crouch',captured['eyeHeight']==1,captured);crouch.wait(timeout=5);time.sleep(.15);check('Ctrl_stand_height_restore',abs(diag()['eyeHeight']-1.7)<.001,diag()['eyeHeight'])
 pose(client,[0,1.7,4]);keys('-P','space','-s','180','-p','space');check('Space_walk_jump',state()['movement']['position'][1]>1.9)
 pose(client,[-4,1.7,2]);keys('-P','k','-s','750','-p','k');time.sleep(.2);check('walk_stair_step_up',state()['movement']['position'][1]>2,state()['movement']['position'])
 pose(client,[0,-35,4]);wait(lambda:state()['movement']['position'][1]>1);check('fall_respawn',True)
 config=client.call('input.get',{})['config'];next=json.loads(json.dumps(config));next['longHoldMs']=1200;next['scopeVignette']=False
 # Below IPC 0.15 input.get is the frozen pre-placement view; the file keeps the newer bindings input.set carried over.
 client.call('input.set',{'config':next});saved=json.loads((s.STATE/'input.json').read_text())
 check('IPC_config_persist',client.call('input.get',{})['config']==next and saved['longHoldMs']==1200 and saved['scopeVignette'] is False and 'carry.facing' in saved['modes']['world'],{k:saved[k] for k in ['longHoldMs','scopeVignette']})
 # The file is canonical (current names); a 0.8 view written to disk would rebind Tool 5 to the old HUD chord.
 ondisk=json.loads(json.dumps(saved));ondisk['longHoldMs']=1000;path=s.STATE/'input.json';tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(ondisk));tmp.replace(path);wait(lambda:state()['keymap']['config']['longHoldMs']==1000);check('watched_atomic_reload',True);good=state()['keymap']['config']
 invalid=json.loads(json.dumps(ondisk));invalid['longHoldMs']=1;path.write_text(json.dumps(invalid));wait(lambda:state()['keymap']['error']);check('bad_reload_retains_last_good',state()['keymap']['config']==good);client.call('input.set',{'config':config})
 wait(lambda:status().get('lease'))
 hold=s.envrun('wtype','-P','Tab','-s','700','-p','Tab');children.append(hold)
 pending=wait(lambda:state()['keymap']['holds'])
 check('hold_start_published',pending[0]['action']=='menu' and pending[0]['binding']=='Tab[L]' and pending[0]['durationMs']==1000,pending)
 start=pending[0]['startedAtMs'];time.sleep(.12)
 check('hold_start_stable',state()['keymap']['holds'][0]['startedAtMs']==start)
 hold.wait(timeout=5);wait(lambda:not state()['keymap']['holds']);check('hold_cancel_published',not status().get('director'))
 keys('-P','Tab','-s','450','-p','Tab');check('Tab_short_hold_does_not_open',not status().get('director'));keys('-P','Tab','-s','1100','-p','Tab');wait(lambda:status().get('director'));check('Tab_hold_Map',not state()['keymap']['holds'],status());unrelatedIn('menu');ui('tabNamed','settings');time.sleep(.4);shot('settings.png')
 # Verified Director capture uses the shell actor, not the acceptance actor.
 # QML dev action passes published intent methods through its own IPC connection.
 # Settings has sections now; open Input bindings and start the slot through the shell's own dev intent (shell actor).
 # World mode binds Super+Tab (free cursor exit), so capture the other native-colliding chord, Super+F9.
 ui('settingsSection',2);time.sleep(.4);ui('inputCapture','world','debug',2);wait(lambda:state()['keymap']['capture'] and state()['keymap']['capture']['status']=='waiting');keys('-M','logo','-P','Super_L','-s','40','-k','F9','-p','Super_L','-m','logo');captured=wait(lambda:status().get('captured'));check('settings_capture',captured['binding']=='Super+F9' and captured['mode']=='world' and captured['action']=='debug' and captured['slot']==2,captured);ui('inputSave');wait(lambda:'Super+F9' in state()['keymap']['config']['modes']['world']['debug']);check('settings_core_save',True);check('settings_Hyprland_collision',any(v['binding']=='Super+F9' for v in state()['keymap']['steals']));shot('settings-conflict.png');keys('-k','Escape');wait(lambda:not status().get('director'));check('Esc_menu_exit',True)
 client.call('input.set',{'config':config})
 client.call('movement.set',{'modeId':'com.hyprune.fly'});pose(client,[0,2.3,4]);client.call('surface.pull',{'surfaceId':sid});time.sleep(.25);target()
 client.call('surface.place',{'surfaceId':sid,'anchorId':'sun-court-slot-01-operator-desk-01'});target()
 before=cap(sid);mod=s.envrun('wtype','-P','Super_L','-s','800','-p','Super_L');children.append(mod);time.sleep(.1);pointer('button',273,1);pointer(-80,35);time.sleep(.15);pointer('button',273,0);mod.wait(timeout=5);check('SuperRMB_corner_client_resize',cap(sid)['pixels']!=before['pixels'],cap(sid))
 pose(client,[0,3,4]);client.call('surface.pull',{'surfaceId':sid});time.sleep(.25);before=cap(sid);mod=s.envrun('wtype','-P','Super_L','-s','650','-p','Super_L');children.append(mod);time.sleep(.1);pointer('scroll',-1);mod.wait(timeout=5);check('SuperWheel_distance',cap(sid)['position']!=before['position'])
 # Keymap v2: Super+Shift+Wheel is content scale on an aimed window (resolution, not world size); facing moved to R.
 client.call('surface.place',{'surfaceId':sid,'anchorId':'sun-court-slot-01-operator-desk-01'});target()
 before=cap(sid);mod=s.envrun('wtype','-P','Super_L','-P','Shift_L','-s','650','-p','Shift_L','-p','Super_L');children.append(mod);time.sleep(.1);pointer('scroll',-1);mod.wait(timeout=5)
 scaled=wait(lambda:cap(sid) if cap(sid)['pixels']!=before['pixels'] else None);check('SuperShiftWheel_content_scale',all(abs(scaled['size'][i]-before['size'][i])<.003 for i in range(2)),scaled)
 keys('-k','2');pointer();check('window_place_tag',state()['interaction']['taggedSurfaceId']==sid);pose(client,[4,2,1],math.pi/2);time.sleep(.2);pointer();wait(lambda:state()['interaction']['taggedSurfaceId'] is None);check('window_place_arbitrary_wall',not cap(sid)['collision'] and cap(sid)['position'][0]>4,cap(sid));client.call('surface.place',{'surfaceId':sid,'anchorId':'sun-court-slot-01-operator-desk-01'});target();pointer('scroll',-1);wait(lambda:state()['editor']['submode']=='lock');pointer();check('window_lock',sid in state()['editor']['locked']);shot('window-locked.png')
 before=cap(sid)['position'];mod=s.envrun('wtype','-P','Super_L','-s','600','-p','Super_L','-m','logo');children.append(mod);wait(lambda:diag()['superHeld']);pointer('button',272,1);pointer(80,0);time.sleep(.2);pointer('button',272,0);mod.wait(timeout=5);check('locked_carry_rejected',cap(sid)['position']==before)
 target();pointer();check('window_unlock',sid not in state()['editor']['locked'])
 # The authored desk anchor is flush with collision; use the free-zone pose
 # for a lateral carry, while the prior checks keep the mounted lock coverage.
 client.call('surface.pull',{'surfaceId':sid});time.sleep(.3);target()
 before=cap(sid);mod=s.envrun('wtype','-P','Super_L','-s','800','-p','Super_L');children.append(mod);wait(lambda:diag()['superHeld']);pointer('button',272,1);started=diag();check('SuperLMB_carry_started',started['gesture']!=0,{'gesture':started['gesture'],'aim':started['aim'],'superHeld':started['superHeld']});pointer(70,-15);time.sleep(.35);pointer('button',272,0);mod.wait(timeout=5);after=cap(sid);check('SuperLMB_carry',after['position']!=before['position'] and not after['collision'],{'before':before,'after':after,'aim':diag().get('aim')})
 client.call('surface.place',{'surfaceId':sid,'anchorId':'sun-court-slot-01-operator-desk-01'});target()
 keys('-k','4');pointer();wait(lambda:state()['editor']['selectedId']==sid);check('position_gizmo_select',state()['editor']['submode']=='move');shot('position-gizmo.png');pointer('scroll',-1);check('gizmo_rotate_mode',state()['editor']['submode']=='rotate');pointer('scroll',-1);check('gizmo_scale_mode',state()['editor']['submode']=='scale');shot('position-scale.png')
 # Gizmo 2.0: handles keep a constant screen size; aim at them from their published anchors.
 def gaim(part):
  for _ in range(6):
   g=wait(lambda:diag()['gizmo'] if part in diag()['gizmo']['handles'] else None);t=g['handles'][part]['world'];e=diag()['position'];d=[t[k]-e[k] for k in range(3)]
   pose(client,e,math.atan2(d[0],-d[2]),math.atan2(d[1],math.hypot(d[0],d[2])));time.sleep(.25)
   if diag()['gizmo']['hover']==part:return
  wait(lambda:diag()['gizmo']['hover']==part)
 pointer('scroll',-1);before=cap(sid);target();gaim('move-x');pointer('button',272,1);pointer(100,0);time.sleep(.2);pointer('button',272,0);check('gizmo_window_move',cap(sid)['position']!=before['position'],cap(sid))
 pointer('scroll',-1);pointer('scroll',-1);target();gaim('scale-uniform');before=cap(sid);pointer('button',272,1);check('gizmo_uniform_pick',diag()['gizmo']['active']=='scale-uniform',diag()['gizmo']);pointer(-80,0);time.sleep(.2);pointer('button',272,0);check('gizmo_uniform_client_scale',cap(sid)['pixels']!=before['pixels'],cap(sid))

 for n,delta in enumerate([-60,40,-40]):
  target();gaim('scale-uniform');before=cap(sid)['pixels'];pointer('button',272,1);pointer(delta,0);time.sleep(.25);pointer('button',272,0);check('gizmo_scale_configure_ack_'+str(n),cap(sid)['pixels']!=before,{'before':before,'after':cap(sid)['pixels']})
 # Independently editable solid cube: mesh and collision share the persisted transform.
 pointer('scroll',-1);pose(client,[2,1.8,3]);time.sleep(.2);pointer();check('gizmo_object_selection',state()['editor']['selectedKind']=='object',state()['editor']);shot('object-gizmo.png')
 pose(client,[2,1.8,3]);time.sleep(.2);gaim('move-x');pointer('button',272,1);snap=s.envrun('wtype','-P','Shift_L','-s','650','-p','Shift_L');children.append(snap);time.sleep(.1);pointer(-100,0);time.sleep(.2);pointer('button',272,0);snap.wait(timeout=5);obj=state()['editor']['objects'][-1];check('gizmo_object_move_snap',obj['position'][0]!=2 and abs((obj['position'][0]-2)*10-round((obj['position'][0]-2)*10))<.001,obj)
 pointer('scroll',-1);p=obj['position'];pose(client,[p[0],p[1]+.5,p[2]+3]);time.sleep(.2);gaim('rotate-y');pointer('button',272,1);pointer(100,0);time.sleep(.2);pointer('button',272,0);obj=state()['editor']['objects'][-1];check('gizmo_object_rotate',obj['rotation']!=[0,0,0,1],obj)
 pointer('scroll',-1);p=obj['position'];pose(client,[p[0],p[1],p[2]+3]);time.sleep(.2);gaim('scale-uniform');pointer('button',272,1);pointer(80,0);time.sleep(.2);pointer('button',272,0);obj=state()['editor']['objects'][-1];check('gizmo_object_scale',obj['scale']!=[1,1,1],obj);check('objects_persist',obj in json.loads((s.STATE/'editor.json').read_text())['objects'])
 # Placement rules: a collision-owning node may not be committed into the player
 # (probed clear of the fixture's window zone and spawn capsules).
 pose(client,[4.5,1.8,4]);time.sleep(.3);e=diag()['position'];probe=s.STATE/'probe.json';s.ctl('eval',f'local f = assert(io.open({json.dumps(str(probe))}, "w")); f:write(hl.plugin.hyprune.gizmo_probe({e[0]},{e[1]-.8},{e[2]})); f:close()');verdict=json.loads(probe.read_text());check('gizmo_object_refuses_player',verdict['rule']=='player',verdict)
 keys('-k','3');pose(client,[1,2.5,4],pitch=-.6);time.sleep(.2)
 modes=[]
 for _ in range(6):
  # Tool slots: LMB add, RMB remove point, MMB open/closed, wheel segment type; reselecting the tool finishes a path.
  if len(modes)==3:pointer('button',274);time.sleep(.1)
  modes.append(state()['editor']['submode']);pointer();pointer(40,0);time.sleep(.1);pointer();keys('-k','3');pointer('scroll',-1);time.sleep(.1)
 check('six_path_types',len(set(modes))==6,modes);check('paths_persist',len(json.loads((s.STATE/'editor.json').read_text())['paths'])>=6);shot('paths.png')
 for slot in range(1,6):keys('-k',str(slot));check('tool_'+str(slot),state()['editor']['tool']==slot)
 unrelated=s.STATE/'native-unrelated';unrelated.unlink(missing_ok=True);keys('-M','logo','-P','Super_L','-s','40','-k','F9','-p','Super_L','-m','logo');wait(unrelated.exists);check('noncolliding_native_bind',True)
 for world in ['com.hyprune.lumen-reach','com.hyprune.switchyard']:
  client.call('world.load',{'worldId':world});check('world_'+world,state()['world']['id']==world);shot(world.split('.')[-1]+'.png')
 if args.record:
  from m3b_demo import demo
  demo(client,children,state,pointer,ui,status,sid)
 # Automatic helper failure removes interception; only this verified nested helper is signalled.
 session,_=verified();daemon=builtins.next((int(pid) for pid in pathlib.Path(f"/proc/{session['pid']}/task/{session['pid']}/children").read_text().split() if pathlib.Path(f'/proc/{pid}/comm').read_text().strip()=='hypruned'),None);assert daemon
 os.kill(daemon,15);wait(lambda:not diag()['active']);native.unlink(missing_ok=True);keys('-M','logo','-P','Super_L','-s','40','-k','Tab','-p','Super_L','-m','logo');wait(native.exists);check('helper_failure_restores_native_bind',True)
 copies=list(s.STATE.glob('hyprune-*.so'));assert copies;result=s.ctl('plugin','unload',str(max(copies,key=lambda p:p.stat().st_mtime)));check('plugin_unload',result=='ok',result);native.unlink();keys('-M','logo','-P','Super_L','-s','40','-k','Tab','-p','Super_L','-m','logo');wait(native.exists);check('unload_restores_native_bind',True)
 (s.STATE/'acceptance.json').write_text(json.dumps(results,indent=2)+'\n')
 print('PASS M3B checks:',len(results),flush=True)
