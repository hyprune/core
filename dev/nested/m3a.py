"""Hands-on acceptance. Invoked only by run.sh under the lifetime flock."""
import json,math,os,pathlib,subprocess,time,shutil,hashlib
import session as s
from m2 import wait,keys,pointer,pose,shot
HERE=s.HERE;ROOT=s.ROOT

def acceptance(client,children,args,shell_token,socketpath):
 results={};record=None
 keyboard_keeper=s.envrun('wtype','-s','300000');children.append(keyboard_keeper);time.sleep(.2)
 device=s.envrun(str(ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
 def pointer(*args):
  device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok' 
 s.ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
 term=s.envrun('kitty','--class','hyprune-m3a-terminal','--title','M3A terminal','--override','font_size=18','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(term)
 marker=s.STATE/'popup-selected.txt';marker.unlink(missing_ok=True);scrollmarker=pathlib.Path(str(marker)+'.scroll');scrollmarker.unlink(missing_ok=True)
 app=s.envrun('env','GDK_BACKEND=wayland',str(ROOT/'build/nested-parity-client'),str(marker),stdout=subprocess.DEVNULL,stderr=open(s.STATE/'popup.log','w'));children.append(app)
 time.sleep(1)
 for window in json.loads(s.ctl('clients','-j')):
  address=window['address'];s.ctl('eval',f'hl.dispatch(hl.dsp.window.fullscreen({{window="address:{address}",action="unset"}}))');s.ctl('eval',f'hl.dispatch(hl.dsp.window.float({{window="address:{address}",action="enable"}}))');s.ctl('eval',f'hl.dispatch(hl.dsp.window.resize({{window="address:{address}",x=900,y=560,relative=false}}))')
 # Place the real nested pointer on its headless output before world input owns it.
 for _ in range(6):
  xy=list(map(float,s.ctl('cursorpos').split(',')));pointer(2560-xy[0],540-xy[1]);time.sleep(.04)
 s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = \"HEADLESS-1\" }))')
 client.call('world.load',{'worldId':'org.hyprune.m3a'});client.call('workspace.bind',{'id':10,'worldId':'org.hyprune.m3a','areaId':'sun-court'})
 # Labs opened later belong to home 10 too; pulled windows leave for the room (20), so the
 # focused workspace is no longer 10 when they open.
 s.ctl('eval','hl.window_rule({name="m3a-labs",match={title="^M3A (subsurface|X11) laboratory$"},workspace="10 silent"})')
 sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
 def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
 s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:state()['runtime']['active']);time.sleep(.5)
 from ctl import verified
 read,write=os.pipe();os.write(write,(shell_token+'\n').encode());os.close(write)
 _,environment=verified();environment.update(HYPRUNE_TOKEN_FD=str(read),HYPRUNE_PROTOCOL='0.5',HYPRUNE_SOCKET=str(socketpath),HYPRUNE_OUTPUT_SCREEN='HEADLESS-1',HYPRUNE_DEV_AUTOMATION='1')
 log=open(s.STATE/'shell.log','w');shell=subprocess.Popen(['python3',str(pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR',str(ROOT.parent/'shell')))/'bridge/launch.py'),'--protocol','0.5'],env=environment,pass_fds=(read,),stdout=log,stderr=subprocess.STDOUT);os.close(read);children.append(shell)
 def childpid():
  if shell.poll() is not None:raise AssertionError('M3A shell exited; see shell.log')
  return next((int(p) for p in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split() if pathlib.Path(f'/proc/{p}/comm').read_text().strip()=='qs'),None)
 pid=wait(childpid)
 def ui(cmd,*values):
  verified();return subprocess.check_output([str(HERE/'ctl.py'),'env','qs','ipc','--pid',str(pid),'call','preview',cmd,*map(str,values)],text=True).strip()
 def status():
  try:return json.loads(ui('status'))
  except (subprocess.CalledProcessError,json.JSONDecodeError):return {}
 wait(lambda:status().get('lease'));time.sleep(.2)
 client.call('movement.set',{'modeId':'com.hyprune.fly'})
 surface=wait(lambda:next((v for v in state()['surfaces'] if v['title']=='M3A terminal'),None))
 popup=wait(lambda:next((v for v in state()['surfaces'] if v['title']=='M3A popup laboratory'),None))
 def diag():return s.camera()
 def diag_ws(id):return next((c['workspaceId'] for c in diag()['captures'] if c['id']==id),None)
 def captured(id):return next(v for v in diag()['captures'] if v['id']==id)
 # Use mode drives a window-local virtual cursor: relative motion moves it across the root
 # client's pixels (localCursor u, v), wherever the plane sits. Aim there, then click.
 def local_move(parent,u,v):
  root=captured(parent);lc=wait(lambda:(lambda c:c if c['surfaceId']==parent else None)(diag()['localCursor']))
  pointer((u-lc['u'])*root['pixels'][0],(v-lc['v'])*root['pixels'][1]);time.sleep(.2)
 def local_click(parent,u,v):local_move(parent,u,v);pointer()
 def click_child(parent):
  # A subsurface/popup offset is in its root client's pixels: aim at its centre.
  child=wait(lambda:next((x for x in diag()['childSurfaces'] if x['parentId']==parent),None));root=captured(parent)
  local_click(parent,(child['offset'][0]+child['pixels'][0]*.5)/root['pixels'][0],(child['offset'][1]+child['pixels'][1]*.5)/root['pixels'][1])
 def nav():keys('-k','Escape');time.sleep(.1)
 # World clicks drive tools; Use (E) hands the aimed window real clicks (owner keymap).
 def use():keys('-k','e');wait(lambda:diag()['mode']=='typing')
 def target(id,offset=(0,0)):
  v=captured(id);p=v['position'];pose(client,[p[0]+offset[0],p[1]+offset[1],p[2]+3]);time.sleep(.25)
 def stage(id):
  nav();
  for v in state()['surfaces']:
   if v['kind']=='window' and v['id']!=id:client.call('surface.return',{'surfaceId':v['id']})
  pose(client,[0,2.3,4]);client.call('surface.pull',{'surfaceId':id});time.sleep(.2)
 def gesture(button,dx,dy,wheel=False):
  mod=s.envrun('wtype','-P','Super_L','-s','1200','-p','Super_L');children.append(mod);time.sleep(.1);pointer('button',button,1);time.sleep(.12)
  pointer(dx,dy);time.sleep(.35)
  if wheel:pointer('scroll',-1);time.sleep(.25)
  pointer('button',button,0);mod.wait(timeout=5);time.sleep(.2)
 def check(name,condition,extra=None):
  assert condition,(name,extra);results[name]={'passed':True,'evidence':extra};print('PASS M3A:',name,flush=True)
 stage(surface['id']);before=captured(surface['id']);gesture(272,110,-35,True);after=captured(surface['id']);check('carry_offset_distance',after['position']!=before['position'] and not after['collision'],after)
 # Rotation is Super+Ctrl+Wheel since the IPC 0.15 owner corrections; MMB is the tools' Alternate.
 target(surface['id'],(.25,0));before=captured(surface['id'])
 mod=s.envrun('wtype','-P','Super_L','-P','Control_L','-s','1500','-p','Control_L','-p','Super_L');children.append(mod);time.sleep(.15)
 for _ in range(3):pointer('scroll',-1);time.sleep(.15)
 mod.wait(timeout=5);time.sleep(.2);after=captured(surface['id']);check('roll',after['rotation']!=before['rotation'],after)
 target(surface['id'],(.2,.2));before=captured(surface['id']);gesture(273,-75,55);after=captured(surface['id']);check('client_resize',after['pixels']!=before['pixels'],after)
 check('balanced_buttons',diag()['pressedButtons']==0 and diag()['gesture']==0)
 # Zoom is the scope action on Q in the owner keymap.
 zoomkey=s.envrun('wtype','-P','q','-s','400','-p','q');children.append(zoomkey);time.sleep(.15);check('zoom',state()['presentation']['fov']<40);zoomkey.wait(timeout=5);time.sleep(.15)
 stage(popup['id']);use();pointer('button',273);wait(lambda:diag()['children']>0);check('xdg_popup_attached',diag()['children']>0);shot('popup-open.png')
 # The fixture menu opens at the clicked root centre, and its first row lies below/right.
 click_child(popup['id']);wait(lambda:marker.exists() and marker.read_text()=='popup click landed\n');check('popup_click',True)
 local_move(popup['id'],.5,.5);pointer('scroll',1);wait(lambda:scrollmarker.exists() and scrollmarker.read_text()=='scroll landed\n');check('virtual_cursor_scroll',True);pointer();wait(lambda:diag()['fullscreen']==2 and state()['interaction']['mode']=='fullscreen');check('fullscreen_in',True);shot('fullscreen.png')
 pointer();wait(lambda:diag()['fullscreen']==0 and state()['interaction']['mode']!='fullscreen');check('fullscreen_out',True)
 # A transient dialog keeps the root pose while receiving its own keyboard.
 client.call('surface.focus',{'surfaceId':popup['id'],'intent':'keyboard'});keys('-k','F10')
 time.sleep(.4);dialog=next((x for x in diag()['captures'] if x.get('parentId')==popup['id']),None)
 check('wayland_dialog_attached',dialog is not None,dialog);client.call('surface.focus',{'surfaceId':dialog['id'],'intent':'keyboard'});time.sleep(.3);keys('-k','Return');time.sleep(.5);check('dialog_keyboard_close',not any(x.get('parentId')==popup['id'] for x in diag()['captures']),diag())
 nav();submarker=s.STATE/'subsurface-selected.txt';submarker.unlink(missing_ok=True)
 subapp=s.envrun(str(ROOT/'build/nested-subsurface-client'),str(submarker),stdout=subprocess.DEVNULL,stderr=open(s.STATE/'subsurface.log','w'));children.append(subapp)
 subsurface=wait(lambda:next((x for x in state()['surfaces'] if x['title']=='M3A subsurface laboratory'),None));stage(subsurface['id']);use();pointer();click_child(subsurface['id']);wait(lambda:submarker.exists() and submarker.read_text()=='subsurface click landed\n');check('subsurface_click',True)
 nav();xmarker=s.STATE/'x11-selected.txt';xmarker.unlink(missing_ok=True)
 # Dispatch from the verified nested compositor: Xwayland DISPLAY must come from it.
 command=f'env GDK_BACKEND=x11 {ROOT}/build/nested-parity-client {xmarker} "M3A X11 laboratory"'
 s.ctl('eval','hl.dispatch(hl.dsp.exec_cmd('+json.dumps(command)+'))')
 xapp=wait(lambda:next((x for x in state()['surfaces'] if x['title']=='M3A X11 laboratory'),None));stage(xapp['id']);use();pointer('button',273);time.sleep(.5)
 menu=next((x for x in diag()['captures'] if x.get('parentId')==xapp['id']),None);check('x11_menu_attached',menu is not None,menu)
 if menu:
  # The override-redirect menu is its own plane beside the staged (level, camera-facing) root:
  # its centre in the root's u, v, which the virtual cursor may leave to reach it.
  root=captured(xapp['id']);u=.5+(menu['position'][0]-root['position'][0])/root['size'][0];v=.5-(menu['position'][1]-root['position'][1])/root['size'][1]
  local_click(xapp['id'],u,v)
 wait(lambda:xmarker.exists() and xmarker.read_text()=='popup click landed\n');check('x11_menu_click',True)
 # Unbound keys and pointer events aimed away from world planes must not hit the hidden GTK app.
 # Typing into it in the world (the room), then every window back on its hidden home.
 [client.call('surface.return',{'surfaceId':v['id']}) for v in state()['surfaces'] if v['kind']=='window']
 client.call('surface.pull',{'surfaceId':popup['id']});wait(lambda:diag_ws(popup['id'])==20)
 client.call('surface.focus',{'surfaceId':popup['id'],'intent':'keyboard'});nav();[client.call('surface.return',{'surfaceId':v['id']}) for v in state()['surfaces'] if v['kind']=='window'];pose(client,[0,2.3,4],0,1.2)
 before=(s.STATE/'popup.log').read_text();keys('-k','z');keys('-P','Super_L','-k','z','-p','Super_L');pointer();pointer('scroll',1);time.sleep(.3)
 check('input_swallowed',(s.STATE/'popup.log').read_text()==before and diag()['fullscreen']==0,diag())
 pose(client,[0,2.3,4])
 # Use (E) on a window enters typing with the virtual cursor; Super+Tab exits to world.
 def exit_use():keys('-P','Super_L','-k','Tab','-p','Super_L')
 stage(surface['id']);use();check('typing_toggle',diag()['cursor']=='virtual')
 # The cursor moves in the window's pixels: push it past the output's left edge onto the
 # neighbour in mouse-sized steps (one huge jump can project behind the camera).
 for _ in range(12):
  if diag()['cursor']=='desktop':break
  pointer(-400,0);time.sleep(.15)
 wait(lambda:diag()['cursor']=='desktop');check('typing_neighbour_cursor',diag()['nativeCursorVisible']);pointer(1500,0);wait(lambda:diag()['cursor']=='virtual' and diag()['ownsInput']);check('typing_cursor_return',not diag()['nativeCursorVisible'])
 exit_use();wait(lambda:state()['interaction']['mode']=='movement')
 stage(surface['id']);use();marker2=s.STATE/'typed.txt';marker2.unlink(missing_ok=True);keys(f"printf 'virtual cursor input\\n' > {marker2}");keys('-k','Return');wait(lambda:marker2.exists() and marker2.read_text()=='virtual cursor input\n');check('virtual_cursor_typing',marker2.read_text()=='virtual cursor input\n')
 nav();[client.call('surface.return',{'surfaceId':v['id']}) for v in state()['surfaces'] if v['kind']=='window'];pose(client,[0,3,-7]);keys('-k','f');wait(lambda:state()['mounts']['desktopWorkspaceId']==10);check('focus_wall',state()['interaction']['mode']=='workspace')
 exit_use();wait(lambda:state()['mounts']['desktopWorkspaceId'] is None and diag()['handover']==0);check('focus_back',state()['interaction']['mode']=='movement')
 # Native wall extraction uses the real compositor rectangle at the ray hit. Every lab
 # window goes back first: one left floating before the wall (the X11 lab) takes the grab.
 [client.call('surface.return',{'surfaceId':v['id']}) for v in state()['surfaces'] if v['kind']=='window'];pose(client,[0,3,-7]);time.sleep(.3)
 wait(lambda:all(c.get('wall')=='workspace-10' or c.get('parentId') for c in diag()['captures']),timeout=8)
 # Returned windows tile, so aim at one window's own rectangle on the wall, not at a tile seam.
 lab=next(c for c in diag()['captures'] if c['title']=='M3A popup laboratory');q=lab['position'];pose(client,[q[0],q[1],q[2]+7]);time.sleep(.3)
 mod=s.envrun('wtype','-P','Super_L','-s','800','-p','Super_L');children.append(mod);time.sleep(.1);pointer('button',272,1);time.sleep(.25);check('drag_off_wall',diag()['gesture']==1,diag());pulled_title=captured(diag()['gestureId'])['title'];pointer('button',272,0);mod.wait(timeout=5);time.sleep(.25)
 tiled=next(w for w in json.loads(s.ctl('clients','-j')) if w['title']==pulled_title);check('drop_to_tile',not any(v['status']=='carried' for v in state()['mounts']['instances']) and not tiled['floating'] and tiled['workspace']['id']==10,{'title':pulled_title,'floating':tiled['floating'],'workspaceId':tiled['workspace']['id']})
 stage(surface['id']);keys('-k','2');pointer();time.sleep(.15);check('mount_tag',state()['interaction']['taggedSurfaceId']==surface['id'])
 # Laboratory right wall, independent of authored screen anchors.
 pose(client,[4,2,1],math.pi/2);pointer();time.sleep(.3);check('arbitrary_mount',any('pose' in v for v in state()['mounts']['placements']),state()['mounts']['placements']);saved=state()['mounts']['placements'];client.call('world.load',{'worldId':'org.hyprune.m3a'});surface=wait(lambda:next((v for v in state()['surfaces'] if v['title']=='M3A terminal'),None));time.sleep(.3);check('mount_persistence',state()['mounts']['placements']==saved and not captured(surface['id'])['collision'])
 keys('-k','1');nav();client.call('movement.set',{'modeId':'com.hyprune.walk'});pose(client,[-4,1.7,2]);keys('-P','w','-s','750','-p','w');time.sleep(.2);p=state()['movement']['position'];check('stairs',p[1]>2,p)
 pose(client,[6,2,1],math.pi/2);keys('-k','v');check('noclip_on',diag()['noclip']);keys('-P','w','-s','600','-p','w');time.sleep(.1);p=state()['movement']['position'];check('noclip_through_wall',p[0]>7.5,p);pose(client,[5.5,2,1]);keys('-k','v');time.sleep(.2);check('noclip_off',not diag()['noclip'])
 keys('-k','F4');time.sleep(.2);check('fly_toggle',state()['movement']['modeId']=='com.hyprune.fly')
 pose(client,[0,-35,4]);wait(lambda:state()['movement']['position'][1]>1);check('respawn',state()['movement']['position'])
 before=state()['movement']['position'];s.ctl('eval','hl.plugin.hyprune.config({player={walk_speed=4.2,look_sensitivity=0.003},input={typing_button=276},graphics={fov=72,quality="intel",exposure=0.8,bloom_strength=0.2}})');time.sleep(.2)
 check('config_reload',diag()['config']['player']['walk_speed']==4.2 and state()['presentation']['fov']==72 and state()['presentation']['quality']=='intel' and state()['presentation']['profile']['exposure']==0.8 and state()['movement']['position']==before)
 
 try:invalid=s.ctl('eval','hl.plugin.hyprune.config({player={walk_speed=-1}})')
 except subprocess.CalledProcessError as e:invalid=e.output
 check('config_rejected',diag()['config']['player']['walk_speed']==4.2,invalid)
 s.ctl('eval','hl.plugin.hyprune.config({input={typing_button=275},graphics={fov=60}})');time.sleep(.2)
 # Actual author packages, unchanged. Bind Lumen home and exercise hand carry/mount there.
 for worldid in ['com.hyprune.lumen-reach','com.hyprune.switchyard']:
  client.call('world.load',{'worldId':worldid});time.sleep(.5);check('world_'+worldid,state()['world']['id']==worldid)
  if state()['mounts']['areas']:
   client.call('workspace.bind',{'id':10,'worldId':worldid,'areaId':state()['mounts']['areas'][0]['id']});time.sleep(.5)
   client.call('surface.pull',{'surfaceId':wait(lambda:next((v for v in state()['surfaces'] if v['title']=='M3A terminal'),None))['id']});time.sleep(.3);gesture(272,45,-10,True);check('hands_'+worldid,not any(v['collision'] for v in diag()['captures'] if v['visible']))
   client.call('surface.return',{'surfaceId':next(v for v in state()['surfaces'] if v['title']=='M3A terminal')['id']});time.sleep(.5)
  else:
   # An area-less world binds no workspaces and adopts only room clients: bring the
   # terminal into the room (windows.world_workspace) for the carry, then take it back.
   term_window=lambda:next(w for w in json.loads(s.ctl('clients','-j')) if w['title']=='M3A terminal')
   s.ctl('eval',f'hl.dispatch(hl.dsp.window.move({{workspace="20",follow=false,window="address:{term_window()["address"]}"}}))')
   client.call('movement.set',{'modeId':'com.hyprune.fly'});t=wait(lambda:next((v for v in diag()['captures'] if v['title']=='M3A terminal' and v['visible']),None));target(t['id']);before=captured(t['id']);gesture(272,35,-10,True);after=captured(t['id']);check('hands_'+worldid,after['position']!=before['position'] and not after['collision'],after)
   s.ctl('eval',f'hl.dispatch(hl.dsp.window.move({{workspace="10",follow=false,window="address:{term_window()["address"]}"}}))')
  shot(worldid+'.png')
 # Spawn, typing-button, live pose persistence and actual shell presentation.
 nav();client.call('world.load',{'worldId':'org.hyprune.m3a'});client.call('workspace.bind',{'id':10,'worldId':'org.hyprune.m3a','areaId':'sun-court'});pose(client,[0,2.3,4]);client.call('movement.set',{'modeId':'com.hyprune.fly'})
 new=s.envrun('kitty','--class','hyprune-m3a-spawn','--title','M3A spawn','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(new)
 spawned=wait(lambda:next((v for v in state()['surfaces'] if v['title']=='M3A spawn'),None));time.sleep(.3);v=captured(spawned['id']);check('spawn_in_front',v['visible'] and not v['collision'] and v['position'][2]<4,v)
 pointer('button',275);wait(lambda:state()['interaction']['mode']=='typing');pointer('button',275);wait(lambda:state()['interaction']['mode']=='movement');check('typing_button',state()['interaction']['mode']=='movement')
 check('shell_mode_tools',status().get('mode')=='movement' and status().get('tool')==1,status())
 hold=json.loads((ROOT/'config/input.default.json').read_text())['longHoldMs']+200  # a long Tab hold opens the Director
 keys('-P','Tab','-s',str(hold),'-p','Tab');wait(lambda:status().get('director'));ui('tabNamed','workspaces');time.sleep(.5);shot('director-workspace-apps.png');check('director_apps',status().get('apps',0)>=2,status());ui('close');wait(lambda:not status().get('director'))
 client.call('runtime.exit',{});time.sleep(.25);d=diag();check('cursor_exit_restore',not d['active'] and d['cursor']=='desktop' and d['nativeCursorVisible']==(not d['savedCursorHidden']),d['cursor'])
 if args.record:
  s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:state()['runtime']['active']);time.sleep(.3)
 results['hardware_cursor_live']={'passed':False,'status':'requires owner supervised DRM session'}
 (s.STATE/'acceptance.json').write_text(json.dumps(results,indent=2)+'\n')
 import sys
 sys.path.insert(0,str(ROOT.parent/'shell/bridge'))
 from protocol import V5_VALIDATOR,validate_response
 for item in client.transcript:validate_response(item['response'],item['method'],V5_VALIDATOR)
 for frame in client.events:V5_VALIDATOR.validate(frame)
 check('ipc_0.5_wire',True,{'responses':len(client.transcript),'notifications':len(client.events)})
 (s.STATE/'acceptance.json').write_text(json.dumps(results,indent=2)+'\n')
 if args.record:demo(client,children,args,state,pointer)
 if args.serve:
  print('M3A nested ready',flush=True)
  while True:time.sleep(.5)

def demo(client,children,args,state,pointer):
 # Recording stays outside gallery until encoding, ffprobe and inspection finish.
 import tempfile,signal
 temp=pathlib.Path(tempfile.mkdtemp(prefix='hyprune-m3a-video-'));raw=temp/'capture.mkv';out=temp/'hyprune-core-m3a.mp4'
 client.call('world.load',{'worldId':'org.hyprune.m3a'});client.call('workspace.bind',{'id':10,'worldId':'org.hyprune.m3a','areaId':'sun-court'});client.call('movement.set',{'modeId':'com.hyprune.fly'});time.sleep(.5)
 surfaces=state()['surfaces'];term=next(x['id'] for x in surfaces if x['title']=='M3A terminal');app=next(x['id'] for x in surfaces if x['title']=='M3A popup laboratory')
 def pause(sec):time.sleep(sec)
 def nav():keys('-k','Escape');pause(.1)
 def stage(id):
  nav()
  for x in state()['surfaces']:
   if x['kind']=='window' and x['id']!=id:client.call('surface.return',{'surfaceId':x['id']})
  pose(client,[0,2.3,4]);client.call('surface.pull',{'surfaceId':id});pause(.3)
 def capture(id):return next(x for x in s.camera()['captures'] if x['id']==id)
 def click_child(id):
  child=wait(lambda:next((x for x in s.camera()['childSurfaces'] if x['parentId']==id),None));d=s.camera();rel=[child['position'][i]-d['position'][i] for i in range(3)];tan=math.tan(state()['presentation']['fov']*math.pi/360);depth=-rel[2]
  nx=.5+rel[0]/(depth*tan*(1920/1080)*2);ny=.5-rel[1]/(depth*tan*2)
  pointer((nx-d['pointer'][0])*1920,(ny-d['pointer'][1])*1080);pause(.3);pointer()
 def centre():
  d=s.camera();pointer((.5-d['pointer'][0])*1920,(.5-d['pointer'][1])*1080);pause(.2)
 def target(id,x=0,y=0):
  p=capture(id)['position'];pose(client,[p[0]+x,p[1]+y,p[2]+3]);pause(.2)
 def gesture(button,dx,dy,wheel=False):
  mod=s.envrun('wtype','-P','Super_L','-s','1900','-p','Super_L');children.append(mod);pause(.12);pointer('button',button,1);pause(.2)
  for _ in range(10):pointer(dx/10,dy/10);pause(.06)
  if wheel:pointer('scroll',-1)
  pause(.2);pointer('button',button,0);mod.wait(timeout=5);pause(.4)
 for window in json.loads(s.ctl('clients','-j')):
  if window['title'] in ['M3A terminal','M3A popup laboratory']:
   address=window['address'];s.ctl('eval',f'hl.dispatch(hl.dsp.window.fullscreen({{window="address:{address}",action="unset"}}))');s.ctl('eval',f'hl.dispatch(hl.dsp.window.float({{window="address:{address}",action="enable"}}))');s.ctl('eval',f'hl.dispatch(hl.dsp.window.resize({{window="address:{address}",x=900,y=560,relative=false}}))')
 s.ctl('eval','hl.plugin.hyprune.config({player={look_sensitivity=0.0025},graphics={quality="auto",fov=60}})')
 stage(term);pointer();keys("printf '\\033[2J\\033[H HYPRUNE — HANDS-ON DESKTOP\\n\\n Carry / Roll / Resize / Type\\n'");keys('-k','Return');nav()
 record=s.envrun('wf-recorder','-o','HEADLESS-1','-f',str(raw),'-r','30',stdout=subprocess.DEVNULL,stderr=open(temp/'recorder.log','w'));children.append(record)
 start=time.monotonic();timeline=[]
 def mark(label):timeline.append({'time':round(time.monotonic()-start,2),'action':label})
 try:
  mark('Carry at hit point; wheel distance');gesture(272,100,-35,True);pause(2)
  target(term,.25,0);mark('Roll');gesture(274,-30,-90);pause(2)
  target(term,.25,.2);mark('Configure client resize');gesture(273,65,-45);pause(2)
  stage(app);mark('Real xdg right-click menu');pointer('button',273);pause(3);click_child(app);pause(.5)
  centre();mark('Client fullscreen to native 2D');pointer();pause(3);mark('Fullscreen back to 3D');pointer();pause(2)
  stage(term);mark('Typing and virtual cursor');pointer();keys("printf 'Typing through the virtual cursor\\n'");keys('-k','Return');pointer(80,10);pause(2);nav()
  client.call('surface.return',{'surfaceId':term});pose(client,[0,3,-7]);mark('F workspace fly-in');keys('-k','f');pause(2);mark('Super+Tab fly-out');keys('-P','Super_L','-k','Tab','-p','Super_L');pause(2)
  stage(term);mark('Carry and drop into workspace tiling');mod=s.envrun('wtype','-P','Super_L','-s','1200','-p','Super_L');children.append(mod);pause(.1);pointer('button',272,1);pose(client,[0,3,-7]);pause(.5);pointer('button',272,0);mod.wait(timeout=5);pause(2)
  stage(term);keys('-k','2');mark('Mount tool: tag window');pointer();pause(1);pose(client,[4,2,1],math.pi/2);mark('Mount arbitrary wall');pointer();pause(2);keys('-k','1')
  nav();client.call('movement.set',{'modeId':'com.hyprune.walk'});pose(client,[-4,1.7,2]);mark('Discrete stairs');keys('-P','w','-s','800','-p','w');pause(2)
  client.call('world.load',{'worldId':'com.hyprune.lumen-reach'});pause(1);mark('Lumen Reach, unchanged author package');pause(4)
  client.call('world.load',{'worldId':'com.hyprune.switchyard'});pause(1);mark('Switchyard, unchanged author package');pause(4)
  elapsed=time.monotonic()-start
  if elapsed<58:pause(58-elapsed)
 finally:
  record.send_signal(signal.SIGINT);record.wait(timeout=15)
 subprocess.run(['ffmpeg','-y','-i',str(raw),'-an','-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(out)],check=True,stdout=subprocess.DEVNULL,stderr=open(temp/'encode.log','w'))
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(out)],text=True));duration=float(probe['format']['duration']);assert 45<=duration<=75,duration
 meta={'title':'Hyprune core M3A — hands-on desktop parity','tag':'core','tags':['hyprune','core','m3a','nested'],'duration':duration,'timeline':timeline,'notes':'Real nested seat input and live GTK popup/client fullscreen; generated CC0 discrete-stair fixture plus unchanged Lumen Reach and Switchyard. Native effects deferred; hardware cursor requires supervised live testing.','sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'width':1920,'height':1080,'fps':30}
 (temp/'hyprune-core-m3a.json').write_text(json.dumps(meta,indent=2)+'\n')
 (s.STATE/'video-temp.json').write_text(json.dumps({'directory':str(temp),'video':str(out),'metadata':str(temp/'hyprune-core-m3a.json')}))
 print('VIDEO READY FOR INSPECTION',out,flush=True)
