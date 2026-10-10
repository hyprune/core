"""Live findings 2026-10-10: dev/nested/run.sh --live-findings (lifetime flock).

1. Workspace walls show what the user sees on that workspace: the wallpaper
   (a background layer-shell client) behind its windows, recomposed when the
   wallpaper changes; an empty or wallpaper-less wall shows the Hyprune placeholder.
2. Tool mounts are snap targets: aiming a pocketed or carried client at the
   Array Hall lectern lights the frame, and placing fits the client into it
   (flush, same orientation) instead of floating beside it.
"""
import json,math,os,pathlib,shutil,subprocess,time
import session as s
from ctl import verified
from m2 import wait,keys

LUMEN='com.hyprune.lumen-reach'
LECTERN='array-hall-slot-02-lectern-01'
GALLERY=pathlib.Path(os.environ.get('HYPRUNE_LIVE_FINDINGS_STILLS',str(pathlib.Path.home()/'workspace/rick-the-alien/worldshell-gallery/screenshots/live-findings-2026-10-10')))

def prepare(installed):
    shutil.copytree(s.ROOT.parent/'worlds/dist'/LUMEN,installed/LUMEN,dirs_exist_ok=True)

def acceptance(client,children,args,shell_token,socketpath):
    from PIL import Image
    checks=[];stills=[]
    def check(name,ok,data=None):
        assert ok,(name,data)
        checks.append(name);print('PASS findings:',name,flush=True)
    def windows():return json.loads(s.ctl('clients','-j'))
    def workspace():return next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')['activeWorkspace']['id']
    def shot(name):
        path=s.STATE/name;subprocess.run([str(s.HERE/'ctl.py'),'shot',str(path)],check=True);stills.append(path);return Image.open(path).convert('RGB')
    def mean(img,box):
        x0,y0,x1,y1=box;px=[img.getpixel((x,y)) for x in range(x0,x1,4) for y in range(y0,y1,4)]
        return tuple(sum(p[i] for p in px)/len(px) for i in range(3))
    def hue(rgb):
        r,g,b=rgb
        if r>1.5*b and r>1.3*g and r>70:return 'red'
        if b>1.5*r and b>60:return 'blue'
        return 'other'
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    def pointer(*a):
        device.stdin.write(' '.join(map(str,a))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.1)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':LUMEN})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    # A pocketed room client for part 2, before entering (entry selects tool 5).
    s.ctl('eval','hl.dispatch(hl.dsp.focus({workspace="20"}))')
    children.append(s.envrun('kitty','--class','snap-target','--title','Snap-A','--override','background=#203040','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    wait(lambda:any(w['title']=='Snap-A' for w in windows()),20)
    # Workspace 21 gets one small floating client in a corner; 22 stays empty.
    s.ctl('eval','hl.window_rule({name="wall-corner",match={title="^Wall-21$"},workspace="21 silent",float=true,size="480 270",move="40 40"})')
    # No cursor blink: the recomposition bound then measures wallpaper changes only.
    children.append(s.envrun('kitty','--title','Wall-21','--override','cursor_blink_interval=0','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    wait(lambda:any(w['title']=='Wall-21' and w['workspace']['id']==21 for w in windows()),20)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({workspace="91"}))')
    _,env=verified()
    wallpaper=subprocess.Popen(['qs','-p',str(s.HERE/'wallpaper.qml')],env=env,stdout=open(s.STATE/'wallpaper.log','w'),stderr=subprocess.STDOUT)
    children.append(wallpaper);time.sleep(1.5)
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active'])
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    mounts=state()['mounts'];anchors={a['id']:a for a in mounts['anchors']};areas={a['id']:a for a in mounts['areas']}
    def normal(a):
        x,y,z,w=a['pose']['rotation'];return [2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)]
    def look(a,d):
        n=normal(a);p=a['pose']['position'];yaw=math.atan2(-n[0],n[2]);pitch=math.asin(max(-1,min(1,-n[1])))
        s.ctl('eval',f'hl.plugin.hyprune.camera({p[0]+n[0]*d},{p[1]+n[1]*d},{p[2]+n[2]*d},{yaw},{pitch})');time.sleep(.5)
    def face_wall(id,area):
        a=anchors[areas[area]['primaryAnchor']]
        for d in [2.5,3.5,1.8]:
            look(a,d)
            if s.camera()['aim']=='workspace-'+str(id):return d
    def frame(id):return next(f for f in s.camera()['workspaceFrames'] if f['workspaceId']==id)
    centre=(860,440,1060,640)
    # 1a. Wallpaper behind a wall's windows, recomposed when it changes (and only then).
    check('sun-court wall 21 aimable',face_wall(21,'sun-court') is not None)
    before=frame(21)['composed'];seen=set();colours=[]
    for k in range(4):
        img=shot(f'wall-21-wallpaper-{k}.png');c=mean(img,centre);colours.append([round(v) for v in c]);seen.add(hue(c));time.sleep(1.1)
    composed=frame(21)['composed']-before;flips=s.camera()['backdrops'][0]['commits']
    check('wall 21 shows the wallpaper behind its window',{'red','blue'}<=seen,colours)
    check('wallpaper changes recompose the wall (bounded, not per frame)',2<=composed<=8,{'composed':composed,'wallpaperCommits':flips})
    a=anchors[areas['sun-court']['primaryAnchor']];look(a,11);shot('wall-21-wallpaper-wide.png')
    # 1b. An empty workspace wall shows the Hyprune placeholder.
    check('array-hall wall 22 aimable',face_wall(22,'array-hall') is not None)
    img=shot('wall-22-empty-placeholder.png');c=mean(img,(700,300,1220,780))
    mint=sum(1 for x in range(700,1220,3) for y in range(380,700,3) if (lambda p:p[1]>150 and p[1]>p[0]+20)(img.getpixel((x,y))))
    check('empty wall 22 shows the placeholder (navy + lockup)',hue(c)!='red' and c[2]>=c[0] and mint>40,{'mean':[round(v) for v in c],'mint':mint})
    # 1c. No wallpaper surface: the placeholder replaces the background.
    wallpaper.terminate();wallpaper.wait(timeout=5);time.sleep(1)
    face_wall(21,'sun-court');img=shot('wall-21-no-wallpaper-placeholder.png');c=mean(img,centre)
    check('wall without a wallpaper surface shows the placeholder',hue(c)=='other' and c[2]>=c[0],[round(v) for v in c])
    # 2a. Pocket placement aimed at the lectern: highlight, ghost in the frame, then a snap.
    lectern=anchors[LECTERN]
    keys('-k','5');wait(lambda:state()['editor']['tool']==5)
    check('pocketed client queued',state()['editor']['unplaced']['count']>=1,state()['editor']['unplaced']['count'])
    look(lectern,2.2)
    check('lectern lights up as a snap target',wait(lambda:s.camera()['mountTarget']==LECTERN,5),s.camera()['mountTarget'])
    shot('lectern-highlight-pocket.png')
    pointer('button',273);wait(lambda:state()['editor']['unplaced']['count']==0,10)
    inst=wait(lambda:next((i for i in state()['mounts']['instances'] if i.get('anchorId')==LECTERN),None),10)
    surf=next(c for c in s.camera()['captures'] if c['id']==inst['surfaceId'])
    n=normal(lectern);p=lectern['pose']['position'];expected=[p[i]+n[i]*.05 for i in range(3)]
    flush=math.dist(surf['position'],expected);aligned=sum(abs(a-b) for a,b in zip(surf['rotation'],lectern['pose']['rotation']))
    check('pocket placement fits the client into the lectern frame',flush<.08 and aligned<.01 and surf['size'][0]<=lectern['size'][0]+1e-3,
          {'distance':flush,'rotation':aligned,'size':surf['size']})
    keys('-k','1');time.sleep(.3);look(lectern,2.2);shot('lectern-snapped.png')
    # 2b. Carry it away, release in free space, then carry back onto the lectern.
    def carry(release):
        hold=s.envrun('wtype','-P','Super_L','-s','2600','-p','Super_L');children.append(hold);time.sleep(.2)
        pointer('button',272,1);wait(lambda:s.camera()['gesture']==1,5)
        release();time.sleep(.6)
        pointer('button',272,0);hold.wait(timeout=8);wait(lambda:s.camera()['gesture']==0,5);time.sleep(.3)
    sid=inst['surfaceId']
    carry(lambda:s.ctl('eval',f'hl.plugin.hyprune.camera({p[0]+4},{p[1]+.4},{p[2]+3},0.9,0)'))
    away=next(i for i in state()['mounts']['instances'] if i['surfaceId']==sid)
    check('carry release in free space leaves the mount',away.get('anchorId','')!=LECTERN,away)
    # Face the released client, grab it, turn to the lectern and let go.
    surf=next(c for c in s.camera()['captures'] if c['id']==sid);x,y,z,w=surf['rotation']
    m=[2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)];q=surf['position']
    s.ctl('eval',f'hl.plugin.hyprune.camera({q[0]+m[0]*2.5},{q[1]+m[1]*2.5},{q[2]+m[2]*2.5},{math.atan2(-m[0],m[2])},{math.asin(max(-1,min(1,-m[1])))})')
    check('released client aimable again',wait(lambda:s.camera()['aim']==sid,5),s.camera()['aim'])
    def back():
        look(lectern,2.2);wait(lambda:s.camera()['mountTarget']==LECTERN,5)
        shot('lectern-highlight-carry.png')
    carry(back)
    snapped=next(i for i in state()['mounts']['instances'] if i['surfaceId']==sid)
    surf=next(c for c in s.camera()['captures'] if c['id']==sid)
    check('carry release on the lectern snaps into its frame',snapped.get('anchorId')==LECTERN and math.dist(surf['position'],expected)<.08,
          {'instance':snapped,'distance':math.dist(surf['position'],expected)})
    client.call('runtime.exit',{})
    GALLERY.mkdir(parents=True,exist_ok=True)
    for path in stills:  # temp name + rename: the gallery autocommit never sees a partial file
        shutil.copy2(path,GALLERY/('.'+path.name));(GALLERY/('.'+path.name)).rename(GALLERY/path.name)
    (s.ROOT/'docs/evidence/live-findings.json').write_text(json.dumps({'nestedOnly':True,'checks':checks,'stills':[p.name for p in stills]},indent=2)+'\n')
    print('PASS live findings:',len(checks),'checks',flush=True)
