"""Real nested M3B demo; encode to a temp directory before gallery publication.
Every presented frame is exported (cinema.py), 60 fps; the cut is gated."""
import hashlib,json,math,pathlib,subprocess,tempfile,time
import session as s
import cinema
from m2 import wait,pose
from m3b import keys

def demo(client,children,state,pointer,ui,status,sid):
 temp=pathlib.Path(tempfile.mkdtemp(prefix='hyprune-m3b-video-'));raw=temp/'capture.mkv';out=temp/'hyprune-core-m3b.mp4'
 client.call('world.load',{'worldId':'org.hyprune.m3b'});client.call('workspace.bind',{'id':10,'worldId':'org.hyprune.m3b','areaId':'sun-court'});client.call('movement.set',{'modeId':'com.hyprune.fly'});time.sleep(.4)
 # Surface identities are connection/capture scoped and change after world loads.
 sid=wait(lambda:next((x['surfaceId'] for x in state()['apps'] if x['appClass']=='hyprune-m3b-terminal'),None))
 defaults=json.loads((s.ROOT/'config/input.default.json').read_text())
 for actions in defaults['modes'].values():actions.pop('record.toggle',None)  # unbound by default; needs IPC 0.12 (this session is older)
 client.call('input.set',{'config':defaults});keys('-k','1')
 def cap():return next(x for x in s.camera()['captures'] if x['id']==sid)
 def target():
  p=cap()['position'];pose(client,[p[0],p[1],p[2]+3]);time.sleep(.3)
 def exit():keys('-M','logo','-P','Super_L','-s','40','-k','Tab','-p','Super_L','-m','logo');time.sleep(.5)
 def mode(slot,name):
  keys('-k',str(slot))
  for _ in range(8):
   if state()['editor']['submode']==name:return
   # Path tool: the wheel picks the segment type, the middle button open or closed.
   if slot==3 and state()['editor']['submode'].split()[0]!=name.split()[0]:pointer('button',274)
   else:pointer('scroll',-1)
  raise AssertionError(('demo submode',slot,name,state()['editor']))
 client.call('surface.place',{'surfaceId':sid,'anchorId':'sun-court-slot-01-operator-desk-01'});target();keys('-k','e');time.sleep(.2);keys("printf '\\033[2J\\033[H  HYPRUNE / KEYMAP V2\\n\\n  E Use in place   F Focus\\n  Super+Tab Return   Q Scope\\n\\n  HJKL / WASD / Arrows\\n  1 Normal  2 Window  3 Path  4 Position\\n'");keys('-k','Return');exit()
 cam=cinema.Cinema(s);record=cam.recording(raw);start=record.__enter__();timeline=[]
 def mark(label):timeline.append({'time':round(time.monotonic()-start,2),'action':label})
 try:
  target();mark('E: use a real terminal in place; relative cursor and typing');keys('-k','e');keys("printf 'Use in place, camera stays here\\n'");keys('-k','Return');pointer(75,10);time.sleep(2);exit()
  client.call('surface.return',{'surfaceId':sid});pose(client,[0,3,-8]);mark('E: use the workspace wall in place');keys('-k','e');wait(lambda:state()['editor']['mode']=='interactive');pointer(70,25);time.sleep(2);exit()
  client.call('surface.place',{'surfaceId':sid,'anchorId':'sun-court-slot-01-operator-desk-01'});target();mark('F: zoom to terminal fullscreen');keys('-k','f');wait(lambda:s.camera()['fullscreen']==2);time.sleep(2);mark('Super+Tab: exit; native colliding bind stays stolen');exit();wait(lambda:s.camera()['fullscreen']==0)
  client.call('surface.return',{'surfaceId':sid});pose(client,[0,3,-8]);mark('F: zoom to workspace fullscreen');keys('-k','f');wait(lambda:state()['mounts']['desktopWorkspaceId']==10);time.sleep(2);exit();wait(lambda:state()['mounts']['desktopWorkspaceId'] is None)
  pose(client,[0,2.2,4]);mark('Q: scope zoom with wheel and vignette');q=s.envrun('wtype','-P','q','-s','2800','-p','q');children.append(q);time.sleep(.4);pointer('scroll',-2);time.sleep(1.8);q.wait(timeout=5)
  mark('Settings: three capture boxes, native shortcut conflict');keys('-P','Tab','-s','1100','-p','Tab');wait(lambda:status().get('director'));ui('tabNamed','settings');time.sleep(.4);ui('settingsSection',2);time.sleep(.4);ui('inputCapture','world','debug',2);wait(lambda:state()['keymap']['capture'] and state()['keymap']['capture']['status']=='waiting');keys('-M','logo','-P','Super_L','-s','40','-k','Tab','-p','Super_L','-m','logo');time.sleep(2.5);ui('inputSave');time.sleep(1);keys('-k','Escape');time.sleep(.4);client.call('input.set',{'config':defaults})
  client.call('surface.place',{'surfaceId':sid,'anchorId':'sun-court-slot-01-operator-desk-01'});target();mode(4,'move');pointer();mark('Position tool: move arrows');time.sleep(1.2);p=cap()['position'];pose(client,[p[0]+.6,p[1],p[2]+3]);pointer('button',272,1);pointer(90,0);time.sleep(.3);pointer('button',272,0);target();time.sleep(.5);mode(4,'rotate');mark('Position tool: rotation arcs');time.sleep(2);mode(4,'scale');mark('Position tool: axis cubes and uniform scale centre');time.sleep(2)
  mode(2,'lock');mark('Window tool: lock badge prevents accidental carrying');pointer();wait(lambda:sid in state()['editor']['locked']);time.sleep(2.5);pointer();mode(3,'open line');pose(client,[1,2.5,4],pitch=-.6);mark('Path tool: open and closed line, curve and arc');time.sleep(.4)
  for k in range(6):  # LMB adds points, MMB open/closed, wheel segment type; reselecting the tool finishes the path
   if k==3:pointer('button',274);time.sleep(.1)
   pointer();pointer(25,0);time.sleep(.1);pointer();keys('-k','3');pointer('scroll',-1);time.sleep(.35)
  keys('-k','1');client.call('world.load',{'worldId':'com.hyprune.lumen-reach'});mark('Lumen Reach: unchanged authored world');time.sleep(2.5);client.call('world.load',{'worldId':'com.hyprune.switchyard'});mark('Switchyard: unchanged authored world');time.sleep(2.5)
  elapsed=time.monotonic()-start
  if elapsed<60:time.sleep(60-elapsed)
 finally:record.__exit__(None,None,None)
 subprocess.run(['ffmpeg','-y','-i',str(raw),'-an','-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(out)],check=True,stdout=subprocess.DEVNULL,stderr=open(temp/'encode.log','w'));gate=cinema.gate(out)
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(out)],text=True));duration=float(probe['format']['duration']);assert 45<=duration<=75,duration
 meta={'title':'Hyprune core M3B — keymap v2','tag':'core','tags':['hyprune','core','m3b','nested'],'duration':duration,'timeline':timeline,'notes':'Real nested Hyprland, live terminal and verified Quickshell Director. E use-in-place window/workspace, F fullscreen window/workspace, Super+Tab collision stealing, Q scope, Settings capture/core save, position handles, window lock and six path types. Lumen Reach and Switchyard unchanged. Native cursor/focus/workspaces deferred; hardware cursor awaits supervised live test.','sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'width':1920,'height':1080,'fps':60,'smoothness':cinema.gate_summary(gate)}
 metadata=temp/'hyprune-core-m3b.json';metadata.write_text(json.dumps(meta,indent=2)+'\n');(s.STATE/'video-temp.json').write_text(json.dumps({'directory':str(temp),'video':str(out),'metadata':str(metadata)}));print('VIDEO READY FOR INSPECTION',out,flush=True)
