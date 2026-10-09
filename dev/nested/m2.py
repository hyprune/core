"""Real M2 acceptance, always invoked inside run.sh's lifetime flock."""
import json,os,pathlib,subprocess,time,math,signal
import session as s
from ctl import verified
ROOT=s.ROOT;HERE=s.HERE

def wait(test,timeout=15):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  value=test()
  if value:return value
  time.sleep(.1)
 raise AssertionError('M2 condition timed out')
def keys(*args):subprocess.run([str(HERE/'ctl.py'),'env','wtype',*args],check=True)
def pointer(*args):subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer'),*map(str,args)],check=True)
def shot(name):subprocess.run([str(HERE/'ctl.py'),'shot',str(s.STATE/name)],check=True)
def pose(client,pos,yaw=0,pitch=0):
 sy=math.sin(-yaw/2);cy=math.cos(-yaw/2);sp=math.sin(pitch/2);cp=math.cos(pitch/2)
 client.call('camera.set',{'position':pos,'orientation':[cy*sp,sy*cp,-sy*sp,cy*cp]})

def acceptance(client,children,args,shell_token,socketpath):
 s.ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
 def terminal():
  p=s.envrun('kitty','--class','hyprune-m2-terminal','--title','M2 terminal','--override','font_size=17','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(p);return p
 term=terminal()
 children.append(s.envrun('kitty','--class','hyprune-m2-metrics','--title','M2 metrics','--override','font_size=16','btop',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
 time.sleep(1)
 for window in json.loads(s.ctl('clients','-j')):
  a=window['address'];s.ctl('eval',f'hl.dispatch(hl.dsp.window.float({{window="address:{a}",action="enable"}}))');s.ctl('eval',f'hl.dispatch(hl.dsp.window.resize({{window="address:{a}",x=1200,y=675}}))')
 client.call('world.load',{'worldId':'org.hyprune.m2'})
 client.call('workspace.bind',{'id':10,'worldId':'org.hyprune.m2','areaId':'sun-court'})
 client.call('workspace.bind',{'id':11,'worldId':'org.hyprune.m2','areaId':'array-hall'})
 client.call('world.load',{'worldId':'org.hyprune.m2.remote'})
 client.call('workspace.bind',{'id':12,'worldId':'org.hyprune.m2.remote','areaId':'sun-court'})
 client.call('world.load',{'worldId':'org.hyprune.m2'})
 sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
 def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
 s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:state()['runtime']['active']);time.sleep(.8)
 # Native workspaces are listed too (5cd2229); count only the two area wall frames.
 assert len([x for x in state()['surfaces'] if x['kind']=='workspace' and ' / workspace ' in x['title']])==2
 assert len(state()['workspaces']['locations'])==3
 client.call('data.publish',{'sourceId':'org.hyprune.nested.globe','sequence':1,'values':{'status':'live'},'ttlMs':60000})
 client.call('data.object.bind',{'objectId':'sun-court-slot-01-globe-01','sourceId':'org.hyprune.nested.globe'})
 assert state()['mounts']['objects'][0]['sourceId']=='org.hyprune.nested.globe' 
 surface=wait(lambda:next((x for x in state()['surfaces'] if x['title']=='M2 terminal'),None))
 client.call('surface.pull',{'surfaceId':surface['id']})
 assert state()['mounts']['carriedSurfaceId']==surface['id']
 # Support-ray clamp prevents loss through the room's back wall.
 client.call('movement.set',{'modeId':'com.hyprune.fly'});pose(client,[0,1.7,-14.35]);time.sleep(.2)
 carried=next(x for x in s.camera()['captures'] if x['id']==surface['id']);assert not carried['collision'] and carried['position'][2]>-14.76,carried
 pose(client,[0,1.7,4]);time.sleep(.2)
 desk='sun-court-slot-01-operator-desk-01'
 client.call('surface.place',{'surfaceId':surface['id'],'anchorId':desk})
 assert next(x for x in state()['mounts']['instances'] if x['surfaceId']==surface['id'])['anchorId']==desk
 try:client.call('surface.place',{'surfaceId':surface['id'],'anchorId':'array-hall-slot-02-banner-01'})
 except RuntimeError as e:assert e.args[0]['code']==-32004
 else:raise AssertionError('cross-area placement accepted')
 # Actual seat click and typing, with no public input-injection method.
 pose(client,[-2.7,1.7,2.2]);time.sleep(.15);pointer();time.sleep(.2)
 assert next(x for x in state()['surfaces'] if x['id']==surface['id'])['focused']
 # Keyboard text reaches a window only in Use (E) mode; in movement mode keys
 # drive the player. The click above only clicks into the aimed window.
 keys('-k','e');wait(lambda:s.camera()['mode']=='typing')
 marker=s.STATE/'m2-typed.txt';marker.unlink(missing_ok=True)
 keys(f"printf 'M2 mounted terminal input\\n' > {marker}");keys('-k','Return')
 # The redirect creates the file before printf writes it; wait for the content.
 wait(lambda:marker.exists() and marker.read_text()=='M2 mounted terminal input\n')
 pointer('drag',100,30);pointer('scroll',1);time.sleep(.2)
 assert s.camera()['pressedButtons']==0 and s.camera()['grabbed']==''
 keys('-k','Escape')
 # Relaunch resolves durable class/title rules to a new compositor surface ID.
 term.terminate();term.wait(timeout=5);wait(lambda:not any(x['id']==surface['id'] for x in state()['surfaces']))
 terminal();surface=wait(lambda:next((x for x in state()['surfaces'] if x['title']=='M2 terminal'),None));time.sleep(.3)
 restored=next(x for x in state()['mounts']['instances'] if x['surfaceId']==surface['id']);assert restored['anchorId']==desk,restored
 assert json.loads((s.STATE/'layout.json').read_text())['placements']
 # Stock Hyprland focus-in/out preserves the world camera and app ownership.
 before=state()['movement']['position'];client.call('workspace.focus',{'id':10});assert state()['mounts']['desktopWorkspaceId']==10
 keys("printf 'stock desktop still works\\n'");keys('-k','Return');client.call('workspace.unfocus',{})
 assert state()['mounts']['desktopWorkspaceId'] is None and state()['movement']['position']==before
 # A tool reached by walking is usable even while another workspace is selected.
 s.ctl('dispatch','hl.dsp.focus({ workspace = "11" })');time.sleep(.8)
 children.append(s.envrun('kitty','--class','hyprune-m2-other','--title','M2 other','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
 other=wait(lambda:next((x for x in state()['surfaces'] if x['title']=='M2 other'),None))
 client.call('surface.pull',{'surfaceId':other['id']});client.call('surface.place',{'surfaceId':other['id'],'anchorId':'array-hall-slot-02-banner-01'})
 client.call('workspace.activate',{'id':10});time.sleep(.8);pose(client,[5,2,-1.5]);time.sleep(.15)
 before=state()['movement']['position'];pointer();keys('-k','e')
 wait(lambda:state()['workspaces']['currentId']==11 and s.camera()['mode']=='typing')
 assert state()['movement']['position']==before,'tool click unexpectedly travelled away from its mount'
 other_marker=s.STATE/'m2-other-typed.txt';other_marker.unlink(missing_ok=True)
 keys(f"printf 'M2 other home input\\n' > {other_marker}");keys('-k','Return');wait(lambda:other_marker.exists())
 assert other_marker.read_text()=='M2 other home input\n';keys('-k','Escape')
 client.call('surface.return',{'surfaceId':other['id']});client.call('workspace.activate',{'id':10});time.sleep(.8)
 # Same-world workspace changes use travel and retain the same package.
 client.call('workspace.activate',{'id':11});wait(lambda:state()['location']['transition'] and state()['location']['transition']['phase']=='arrived')
 assert state()['world']['id']=='org.hyprune.m2' and abs(state()['movement']['position'][0]-3)<.01
 client.call('workspace.activate',{'id':10});time.sleep(.8)
 # Cross-world workspace selection goes through asynchronous warp events.
 client.call('workspace.activate',{'id':12});wait(lambda:state()['world']['id']=='org.hyprune.m2.remote')
 client.call('workspace.activate',{'id':10});wait(lambda:state()['world']['id']=='org.hyprune.m2')
 assert any(x['method']=='location.transition' and x['params']['phase']=='loading' for x in client.events)
 client.call('camera.fov',{'degrees':72});assert state()['presentation']['fov']==72;client.call('camera.fov',{'degrees':60})
 profile=client.call('atmosphere.get',{})['profile'];profile['exposure']=.85;client.call('atmosphere.set',{'profile':profile});assert state()['presentation']['profile']['exposure']==.85
 # Invalid saved mount after a package update relocates in the same home.
 package=s.STATE/'worlds/org.hyprune.m2/world.json';manifest=json.loads(package.read_text());original=package.read_text()
 manifest['anchors']=[a for a in manifest['anchors'] if a['id']!=desk];manifest['areas'][0]['toolAnchors'].remove(desk);package.write_text(json.dumps(manifest))
 client.call('world.load',{'worldId':'org.hyprune.m2'});time.sleep(.4)
 relocated=next(x for x in state()['mounts']['instances'] if x['surfaceId'] in [v['id'] for v in state()['surfaces'] if v['title']=='M2 terminal']);assert relocated['anchorId']=='sun-court-slot-01-lectern-01',relocated
 assert any(x.get('method')=='runtime.notice' and x['params']['code']=='placement.relocated' for x in client.events)
 package.write_text(original);client.call('world.load',{'worldId':'org.hyprune.m2'});time.sleep(.3)
 surface=next(x for x in state()['surfaces'] if x['title']=='M2 terminal');client.call('surface.place',{'surfaceId':surface['id'],'anchorId':desk})
 # Real reference Director, authenticated Wayland child on only the nested output.
 read,write=os.pipe();os.write(write,(shell_token+'\n').encode());os.close(write)
 _,environment=verified();environment.update(HYPRUNE_TOKEN_FD=str(read),HYPRUNE_PROTOCOL='0.4',HYPRUNE_SOCKET=str(socketpath),HYPRUNE_OUTPUT_SCREEN='HEADLESS-1',HYPRUNE_DEV_AUTOMATION='1')
 log=open(s.STATE/'shell.log','w');shell=subprocess.Popen(['python3',str(ROOT.parent/'shell/bridge/launch.py'),'--protocol','0.4'],env=environment,pass_fds=(read,),stdout=log,stderr=subprocess.STDOUT);os.close(read);children.append(shell)
 def childpid():
  if shell.poll() is not None:raise AssertionError('M2 shell exited; see shell.log')
  return next((int(p) for p in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split() if pathlib.Path(f'/proc/{p}/comm').read_text().strip()=='qs'),None)
 pid=wait(childpid)
 def ui(command,*values):
  verified();assert pid in list(map(int,pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()))
  return subprocess.check_output([str(HERE/'ctl.py'),'env','qs','ipc','--pid',str(pid),'call','preview',command,*map(str,values)],text=True).strip()
 def status():
  try:return json.loads(ui('status'))
  except (subprocess.CalledProcessError,json.JSONDecodeError):return {}
 wait(lambda:status().get('lease'))
 keys('-P','Tab','-s','1100','-p','Tab');wait(lambda:status().get('director'));ui('tab',2);time.sleep(.3);shot('m2-director.png')
 # Click the actual Recall button through the nested seat on the leased output.
 current=list(map(float,s.ctl('cursorpos').split(',')));output=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
 # Locate the visible label (the Director layout changes between shell releases).
 import csv,io
 tsv=subprocess.check_output(['tesseract',str(s.STATE/'m2-director.png'),'stdout','--psm','11','tsv'],text=True,stderr=subprocess.DEVNULL)
 label=next(r for r in csv.DictReader(io.StringIO(tsv),delimiter='\t') if r['text']=='Recall')
 target=[output['x']+int(label['left'])+int(label['width'])//2,output['y']+int(label['top'])+int(label['height'])//2]
 for _ in range(8):
  dx=target[0]-current[0];dy=target[1]-current[1]
  if abs(dx)+abs(dy)<2:break
  pointer(dx,dy);time.sleep(.06);current=list(map(float,s.ctl('cursorpos').split(',')))
 pointer();wait(lambda:any(x['status']=='recalled' for x in state()['mounts']['instances']))
 ui('close');wait(lambda:not status().get('director'));pose(client,[0,1.7,4])
 client.call('surfaces.recall',{});time.sleep(.2);assert all(not x['collision'] for x in s.camera()['captures'] if x['visible'])
 assert next(x for x in state()['mounts']['instances'] if x['surfaceId']==surface['id'])['status']=='recalled'
 client.call('surface.place',{'surfaceId':surface['id'],'anchorId':desk});time.sleep(.3);pose(client,[0,2.5,3],0,-.1);shot('m2-world.png')
 print('PASS M2 nested: workspace walls, pull/carry wall clamp, owned desk placement, real click/type, relaunch restore, stock focus, same-world travel, cross-world warp, FOV/profile, changed-package relocation, Director and recall',flush=True)
 if args.record:record(client,state,ui)
 if os.environ.get('HYPRUNE_M2_PACKAGE'):
  installed=json.loads((s.STATE/'package-install.json').read_text())
  package=s.STATE/'worlds'/installed['worldId'];manifest=json.loads((package/'world.json').read_text());client.call('world.load',{'worldId':manifest['id']})
  for index,area in enumerate(state()['mounts']['areas']):
   client.call('workspace.bind',{'id':10+index,'worldId':manifest['id'],'areaId':area['id']})
  client.call('workspace.activate',{'id':10});time.sleep(2)
  wait(lambda:not status().get('warp'));shot('m2-package.png')
  (s.STATE/'package-state.json').write_text(json.dumps(state(),indent=2))
  destination=next((p for p in state()['spatial']['world']['places'] if p['id']=='globe-platform'),None)
  if destination:
   client.call('route.set',{'points':[{'position':destination['position']}]})
   assert len(state()['location']['route'])>1,'Lumen route was not expanded through ramp graph'
   client.call('autodrive.start',{'speed':10})
   wait(lambda:not state()['location']['autodrive'])
   assert math.dist(state()['movement']['position'],destination['position'])<.05,'Lumen autodrive blocked before platform'
   print('PASS Lumen Reach graph autodrive: arrival to globe-platform via ramp',flush=True)
 if args.serve:
  print('M2 nested session ready; controller state is dev/nested/.m2-state.',flush=True)
  while True:time.sleep(.5)
 client.call('runtime.exit',{});log.close()
 import sys
 sys.path.insert(0,str(ROOT.parent/'shell/bridge'))
 from protocol import V4_VALIDATOR,validate_response
 for item in client.transcript:validate_response(item['response'],item['method'],V4_VALIDATOR)
 for frame in client.events:
  errors=list(V4_VALIDATOR.iter_errors(frame))
  assert not errors,(frame.get('method'), errors[0].message[:200])
 print('PASS IPC 0.4 wire conformance:',len(client.transcript),'responses,',len(client.events),'notifications',flush=True)
 (s.STATE/'m2-result.json').write_text(json.dumps({'passed':True,'typed':marker.read_text(),'frames':s.camera()['frames'],'bindings':state()['mounts']['bindings'],'placements':state()['mounts']['placements']},indent=2))


def record(client,state,ui):
 gallery=pathlib.Path.home()/'workspace/rick-the-alien/worldshell-gallery/videos';gallery.mkdir(parents=True,exist_ok=True)
 raw=s.STATE/'m2.mkv';output=gallery/'hyprune-core-m2.mp4';log=open(s.STATE/'recorder.log','w')
 recorder=s.envrun('wf-recorder','-o','HEADLESS-1','-r','30','-D','--no-dmabuf','-c','libx264','-p','preset=ultrafast','-p','crf=20','-x','yuv420p','-f',str(raw),stdout=log,stderr=subprocess.STDOUT)
 def pause(n):
  end=time.monotonic()+n
  while time.monotonic()<end:state();time.sleep(.1)
 try:
  pose(client,[0,2.5,3],0,-.1);pause(3)
  surface=next(x for x in state()['surfaces'] if x['title']=='M2 terminal')
  client.call('surface.pull',{'surfaceId':surface['id']});pause(3)
  for i in range(20):pose(client,[math.sin(i/20)*1.3,1.7,4-i*.06],0,0);pause(.07)
  client.call('surface.place',{'surfaceId':surface['id'],'anchorId':'sun-court-slot-01-operator-desk-01'});pose(client,[-2.7,1.7,2.2]);pause(2);pointer();pause(.2)
  keys('-d','45',"printf '\\nA tool in its workspace home.\\nM2 / real mounted terminal input.\\n'");keys('-k','Return');pause(3);keys('-k','Escape')
  client.call('workspace.focus',{'id':10});pause(2);client.call('workspace.unfocus',{});pause(1)
  # Smooth views inspect original alpha cards, wet paving, practical emission and reflections.
  for i in range(140):
   t=i/139;x=1.2*math.sin(t*math.tau);pose(client,[x,2.4+.4*math.sin(t*math.pi),1.2-2.6*t],-x*.13,-.06);pause(.09)
  keys('-P','Tab','-s','1100','-p','Tab');pause(1);ui('tab',2);pause(2);ui('close');pause(.5)
  client.call('surfaces.recall',{});pose(client,[0,1.7,3.5]);pause(2)
  client.call('surface.place',{'surfaceId':surface['id'],'anchorId':'sun-court-slot-01-operator-desk-01'});pose(client,[0,2.5,3],0,-.1);pause(3)
 finally:
  recorder.send_signal(signal.SIGINT);recorder.wait(timeout=15);log.close()
 subprocess.run(['ffmpeg','-y','-loglevel','error','-i',str(raw),'-an','-vf','scale=1920:1080:flags=lanczos:in_range=pc:out_range=tv','-r','30','-c:v','libx264','-preset','medium','-crf','22','-pix_fmt','yuv420p','-color_range','tv','-movflags','+faststart',str(output)],check=True)
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(output)]));assert 30<=float(probe['format']['duration'])<=60,probe['format']
 (gallery/'hyprune-core-m2.json').write_text(json.dumps({'title':'Hyprune core M2 — workspace homes and mounted tools','tag':'core','notes':'Real locked nested Hyprland: whole workspace wall, pull/carry/place, actual terminal click and typing, stock focus-in/out, original CC0 alpha/fog/HDR bloom and environment reflection fixture, IPC 0.4 Director and all-windows recall. Release-world art is built separately as Lumen Reach.'},indent=2)+'\n')
 (s.STATE/'video-probe.json').write_text(json.dumps(probe,indent=2));print('M2 video:',output,flush=True)
