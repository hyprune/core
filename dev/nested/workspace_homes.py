"""Default area homes (windows.workspace_base + workspaceSlot): dev/nested/run.sh --workspace-homes.

Nested only (run.sh holds /tmp/worldshell-nested.lock). Lumen Reach areas get
workspaces 21-24 with no user binding; native dispatch/keybind and Director
activation travel to the same area; an explicit workspace.bind wins and is the
only binding persisted by hypruned.
"""
import json,math,shutil,subprocess,time
import session as s
from m2 import keys,wait

LUMEN='com.hyprune.lumen-reach'
HOMES=[(21,'sun-court'),(22,'array-hall'),(23,'rime-vault'),(24,'signal-rise')]

def prepare(installed):
    for name in [LUMEN,'com.hyprune.switchyard']:
        shutil.copytree(s.ROOT.parent/'worlds/dist'/name,installed/name,dirs_exist_ok=True)

def acceptance(client,children,args,shell_token,socketpath):
    checks=[]
    def check(name,ok,data=None):
        assert ok,(name,data)
        checks.append({'name':name,'passed':True,'evidence':data});print('PASS homes:',name,flush=True)
    def monitor():return next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    def workspace():return monitor()['activeWorkspace']['id']
    def windows():return json.loads(s.ctl('clients','-j'))
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':LUMEN})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    def homes():return [(b['id'],b['areaId']) for b in state()['mounts']['bindings']]
    def saved():
        path=s.STATE/'layout.json'
        return json.loads(path.read_text())['bindings'] if path.exists() else []
    # Plugin init and each inactive world load ask for the ready notice; it shows once.
    client.call('world.load',{'worldId':'com.hyprune.switchyard'});client.call('world.load',{'worldId':LUMEN});time.sleep(1)
    check('ready notice shown once per plugin load',s.camera()['readyNotices']==1,s.camera()['readyNotices'])
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active'])
    check('room workspace is 20',wait(lambda:workspace()==20),workspace())
    check('world defines homes 21-24 with no user binding',homes()==HOMES,state()['mounts']['bindings'])
    check('bindings keep the 0.15 wire shape',all(set(b)=={'id','worldId','areaId'} for b in state()['mounts']['bindings']))
    located={l['id']:l['label'] for l in state()['workspaces']['locations']}
    check('Director lists default homes',all(located.get(i)==a for i,a in HOMES),located)
    check('default homes are not persisted',saved()==[],saved())

    # Real clients on the default homes, mapped silently on their native workspaces.
    for id,_ in HOMES[:2]:
        s.ctl('eval','hl.window_rule({name="home-'+str(id)+'",match={title="^Home-'+str(id)+'$"},workspace="'+str(id)+' silent"})')
        banner=f'printf "\\n  WORKSPACE {id}\\n  {dict(HOMES)[id]}\\n"; exec bash --noprofile --norc'
        children.append(s.envrun('kitty','--title','Home-'+str(id),'--override','font_size=48','--override','cursor_blink_interval=0','bash','--noprofile','--norc','-c',banner,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    for id,_ in HOMES[:2]:
        w=wait(lambda:next((w for w in windows() if w['title']=='Home-'+str(id)),None))
        check(f'client mapped on workspace {id}',w['workspace']['id']==id and workspace()==20,w['workspace'])
        wait(lambda:any(e['title']=='Home-'+str(id) and e['workspaceId']==id for e in s.camera()['captures']))
    anchors={a['id']:a for a in state()['mounts']['anchors']}
    areas={a['id']:a for a in state()['mounts']['areas']}
    def frame(id):return next((f for f in s.camera()['workspaceFrames'] if f['workspaceId']==id),None)
    def on_wall(id,area):
        a=anchors[areas[area]['primaryAnchor']];f=wait(lambda:frame(id))
        return math.dist(f['position'],a['pose']['position'])<.5,{'frame':f,'anchor':a['id']}
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    def face(id,area):
        # Stand in front of the area's primary wall and aim at its centre.
        a=anchors[areas[area]['primaryAnchor']];x,y,z,w=a['pose']['rotation'];p=a['pose']['position']
        n=[2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)]
        yaw=math.atan2(-n[0],n[2])
        for d in [2.5,3.5,1.8,4.5]:
            s.ctl('eval',f'hl.plugin.hyprune.camera({p[0]+n[0]*d},{p[1]},{p[2]+n[2]*d},{yaw},0)');time.sleep(.35)
            if s.camera()['aim']=='workspace-'+str(id):return d
        return None
    for (id,area),title in zip(HOMES[:2],['Home-21','Home-22']):
        ok,data=on_wall(id,area)
        check(f'workspace {id} renders on the {area} primary wall',ok,data)
        d=face(id,area)
        check(f'{area} wall is reachable and aimable',d is not None,d)
    # Intentional drop still works: pull the real client off its wall with
    # Super+LMB and release it at the wall; it returns to native tiling on 21.
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    def pointer(*args):
        device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.1)
    face(21,'sun-court')
    mod=s.envrun('wtype','-P','Super_L','-s','900','-p','Super_L');children.append(mod);time.sleep(.15)
    pointer('button',272,1);wait(lambda:s.camera()['gesture']==1)
    pulled=next(c for c in s.camera()['captures'] if c['id']==s.camera()['gestureId'])
    pointer('button',272,0);mod.wait(timeout=5);wait(lambda:s.camera()['gesture']==0);time.sleep(.3)
    native=next(w for w in windows() if w['title']==pulled['title'])
    check('carry released at its wall drops back into workspace 21',pulled['title']=='Home-21' and native['workspace']['id']==21 and not native['floating'] and not any(i.get('status')=='carried' for i in state()['mounts']['instances']),{'title':pulled['title'],'workspace':native['workspace']['id'],'floating':native['floating']})

    # Stacking: two overlapping floating windows on home 22. An idle wall is not
    # recomposed; raising the lower one (no client commit) recomposes the wall and
    # the world frame shows the raised colour.
    stack={}
    for title,colour in [('Stack-A','#d01010'),('Stack-B','#1030e0')]:
        s.ctl('eval','hl.window_rule({name="'+title+'",match={title="^'+title+'$"},workspace="22 silent",float=true,center=true})')
        stack[title]=s.envrun('kitty','--title',title,'--override','background='+colour,'--override','initial_window_width=1100','--override','initial_window_height=700','--override','cursor_blink_interval=0','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        children.append(stack[title])
        wait(lambda:any(e['title']==title and e['workspaceId']==22 for e in s.camera()['captures']))
    time.sleep(1.5)
    check('stack windows float on workspace 22',all(w['floating'] and w['workspace']['id']==22 for w in windows() if w['title'].startswith('Stack-')))
    face(22,'array-hall');time.sleep(.5)
    def composed():return frame(22).get('composed',-1)
    def tint(name):
        path=s.STATE/name;subprocess.run([str(s.HERE/'ctl.py'),'shot',str(path)],check=True)
        from PIL import Image
        im=Image.open(path).convert('RGB');w,h=im.size;r,g,b=[sum(c)/len(c) for c in zip(*im.crop((w*2//5,h*2//5,w*3//5,h*3//5)).getdata())]
        return r-b
    c0=composed();time.sleep(1.5)
    check('an idle wall is not recomposed',composed()==c0,{'before':c0,'after':composed()})
    before=tint('stack-before.png')
    lower=next(w['address'] for w in windows() if w['title']=='Stack-A')
    s.ctl('eval','hl.dispatch(hl.dsp.window.alter_zorder({mode="top",window="address:'+lower+'"}))')
    try:wait(lambda:composed()>c0,timeout=5)
    except AssertionError:pass
    time.sleep(.5)
    after=tint('stack-after.png')
    check('raising an overlapping window recomposes its wall',before<0<after and composed()>c0,{'redMinusBlueBefore':before,'after':after,'composed':[c0,composed()]})
    for p in stack.values():p.terminate()
    wait(lambda:not any(w['title'].startswith('Stack-') for w in windows()))

    client.call('movement.set',{'modeId':'com.hyprune.walk'})

    # Travel: Director, native dispatcher and a normal Hyprland keybind agree.
    def transition():return (state()['location'].get('transition') or {})
    def arrive(trigger,start=None):
        if start:s.ctl('eval',f'hl.plugin.hyprune.camera({start[0]},{start[1]},{start[2]},0,0)');time.sleep(.2)
        before=transition().get('id')
        trigger()
        wait(lambda:transition().get('id')!=before and transition().get('phase')=='arrived')
        time.sleep(.2);return client.call('camera.get',{})['position']
    def native(id):s.ctl('eval',f'hl.dispatch(hl.dsp.focus({{workspace="{id}"}}))')
    director={}
    for id,area in HOMES:
        director[id]=arrive(lambda:client.call('workspace.activate',{'id':id}))
        check(f'Director workspace {id} travels to {area}',workspace()==id,director[id])
        c=s.camera();q=c['position']  # evidence still: arrival pose, looking up at the wall
        s.ctl('eval',f'hl.plugin.hyprune.camera({q[0]},{q[1]},{q[2]},{c["yaw"]},0.28)');time.sleep(.6)
        subprocess.run([str(s.HERE/'ctl.py'),'shot',str(s.STATE/f'home-{id}-{area}.png')],check=True)
    native(20);wait(lambda:workspace()==20)
    for id,area in HOMES[:3]:
        p=arrive(lambda:native(id),director[24])
        check(f'native dispatch workspace {id} travels to {area}',workspace()==id and math.dist(p,director[id])<.3,{'native':p,'director':director[id]})
    native(20);wait(lambda:workspace()==20)
    s.ctl('eval','hl.bind("SUPER + F9", hl.dsp.focus({workspace="24"}))')
    p=arrive(lambda:keys('-M','logo','-P','Super_L','-s','40','-k','F9','-p','Super_L','-m','logo'),director[21])
    s.ctl('eval','hl.unbind("SUPER + F9")')
    check('Hyprland keybind to workspace 24 travels to signal-rise',workspace()==24 and math.dist(p,director[24])<.3,p)
    native(20);wait(lambda:workspace()==20)
    p=arrive(lambda:native(24),director[21])
    check('returning to the same home from the room travels again',math.dist(p,director[24])<.3,p)
    native(20);wait(lambda:workspace()==20)

    # Focused into a wall (stock 2D), a native switch leaves the wall and travels (M2 behaviour).
    client.call('workspace.focus',{'id':22});wait(lambda:workspace()==22 and state()['mounts']['desktopWorkspaceId']==22)
    p=arrive(lambda:native(23))
    check('native switch from a focused wall travels to rime-vault',math.dist(p,director[23])<.3 and state()['mounts']['desktopWorkspaceId'] is None,p)
    native(20);wait(lambda:workspace()==20)

    # An explicit user binding wins by area and by workspace ID, and persists alone.
    client.call('workspace.bind',{'id':21,'worldId':LUMEN,'areaId':'signal-rise'})
    check('explicit bind overrides defaults',homes()==[(21,'signal-rise'),(22,'array-hall'),(23,'rime-vault')],homes())
    check('only the explicit bind is persisted',wait(lambda:saved()==[{'id':21,'worldId':LUMEN,'areaId':'signal-rise'}]),saved())
    p=arrive(lambda:native(21),director[22])
    check('workspace 21 now travels to signal-rise',math.dist(p,director[24])<.3,p)
    ok,data=on_wall(21,'signal-rise')
    check('workspace 21 windows move to the signal-rise wall',ok,data)
    native(20);wait(lambda:workspace()==20)

    # A different base moves the derived homes; the explicit bind stays.
    s.ctl('eval','hl.plugin.hyprune.config({windows={workspace_base=30}})')
    check('workspace_base reapplies live',wait(lambda:homes()==[(21,'signal-rise'),(31,'sun-court'),(32,'array-hall'),(33,'rime-vault')]),homes())
    s.ctl('eval','hl.plugin.hyprune.config({windows={workspace_base=20}})')
    # Defaults are per loaded world; a world without areas derives none.
    client.call('world.load',{'worldId':'com.hyprune.switchyard'})
    check('area-less world derives no homes',wait(lambda:homes()==[(21,'signal-rise')]),homes())
    client.call('world.load',{'worldId':LUMEN})
    check('reloading recomputes homes',wait(lambda:homes()==[(21,'signal-rise'),(22,'array-hall'),(23,'rime-vault')]),homes())
    check('explicit bind still the only persisted home',saved()==[{'id':21,'worldId':LUMEN,'areaId':'signal-rise'}],saved())
    client.call('runtime.exit',{})
    evidence={'nestedOnly':True,'lock':'/tmp/worldshell-nested.lock','world':LUMEN,'checks':checks}
    (s.ROOT/'docs/evidence/workspace-homes.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('PASS default workspace homes:',len(checks),'checks',flush=True)
