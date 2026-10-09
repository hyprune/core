"""Default area homes (windows.workspace_base + workspaceSlot): dev/nested/run.sh --workspace-homes.

Nested only (run.sh holds /tmp/worldshell-nested.lock). Lumen Reach areas get
workspaces 11-14 with no user binding; native dispatch/keybind and Director
activation travel to the same area; an explicit workspace.bind wins and is the
only binding persisted by hypruned.
"""
import json,math,shutil,subprocess,time
import session as s
from m2 import keys,wait

LUMEN='com.hyprune.lumen-reach'
HOMES=[(11,'sun-court'),(12,'array-hall'),(13,'rime-vault'),(14,'signal-rise')]

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
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active'])
    check('room workspace stays 8',wait(lambda:workspace()==8),workspace())
    check('world defines homes 11-14 with no user binding',homes()==HOMES,state()['mounts']['bindings'])
    check('bindings keep the 0.15 wire shape',all(set(b)=={'id','worldId','areaId'} for b in state()['mounts']['bindings']))
    located={l['id']:l['label'] for l in state()['workspaces']['locations']}
    check('Director lists default homes',all(located.get(i)==a for i,a in HOMES),located)
    check('default homes are not persisted',saved()==[],saved())

    # Real clients on the default homes, mapped silently on their native workspaces.
    for id,_ in HOMES[:2]:
        s.ctl('eval','hl.window_rule({name="home-'+str(id)+'",match={title="^Home-'+str(id)+'$"},workspace="'+str(id)+' silent"})')
        banner=f'printf "\\n  WORKSPACE {id}\\n  {dict(HOMES)[id]}\\n"; exec bash --noprofile --norc'
        children.append(s.envrun('kitty','--title','Home-'+str(id),'--override','font_size=48','bash','--noprofile','--norc','-c',banner,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    for id,_ in HOMES[:2]:
        w=wait(lambda:next((w for w in windows() if w['title']=='Home-'+str(id)),None))
        check(f'client mapped on workspace {id}',w['workspace']['id']==id and workspace()==8,w['workspace'])
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
    for (id,area),title in zip(HOMES[:2],['Home-11','Home-12']):
        ok,data=on_wall(id,area)
        check(f'workspace {id} renders on the {area} primary wall',ok,data)
        d=face(id,area)
        check(f'{area} wall is reachable and aimable',d is not None,d)
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
    native(8);wait(lambda:workspace()==8)
    for id,area in HOMES[:3]:
        p=arrive(lambda:native(id),director[14])
        check(f'native dispatch workspace {id} travels to {area}',workspace()==id and math.dist(p,director[id])<.3,{'native':p,'director':director[id]})
    native(8);wait(lambda:workspace()==8)
    s.ctl('eval','hl.bind("SUPER + F9", hl.dsp.focus({workspace="14"}))')
    p=arrive(lambda:keys('-M','logo','-P','Super_L','-s','40','-k','F9','-p','Super_L','-m','logo'),director[11])
    s.ctl('eval','hl.unbind("SUPER + F9")')
    check('Hyprland keybind to workspace 14 travels to signal-rise',workspace()==14 and math.dist(p,director[14])<.3,p)
    native(8);wait(lambda:workspace()==8)
    p=arrive(lambda:native(14),director[11])
    check('returning to the same home from the room travels again',math.dist(p,director[14])<.3,p)
    native(8);wait(lambda:workspace()==8)

    # Focused into a wall (stock 2D), a native switch leaves the wall and travels (M2 behaviour).
    client.call('workspace.focus',{'id':12});wait(lambda:workspace()==12 and state()['mounts']['desktopWorkspaceId']==12)
    p=arrive(lambda:native(13))
    check('native switch from a focused wall travels to rime-vault',math.dist(p,director[13])<.3 and state()['mounts']['desktopWorkspaceId'] is None,p)
    native(8);wait(lambda:workspace()==8)

    # An explicit user binding wins by area and by workspace ID, and persists alone.
    client.call('workspace.bind',{'id':11,'worldId':LUMEN,'areaId':'signal-rise'})
    check('explicit bind overrides defaults',homes()==[(11,'signal-rise'),(12,'array-hall'),(13,'rime-vault')],homes())
    check('only the explicit bind is persisted',wait(lambda:saved()==[{'id':11,'worldId':LUMEN,'areaId':'signal-rise'}]),saved())
    p=arrive(lambda:native(11),director[12])
    check('workspace 11 now travels to signal-rise',math.dist(p,director[14])<.3,p)
    ok,data=on_wall(11,'signal-rise')
    check('workspace 11 windows move to the signal-rise wall',ok,data)
    native(8);wait(lambda:workspace()==8)

    # A different base moves the derived homes; the explicit bind stays.
    s.ctl('eval','hl.plugin.hyprune.config({windows={workspace_base=20}})')
    check('workspace_base reapplies live',wait(lambda:homes()==[(11,'signal-rise'),(21,'sun-court'),(22,'array-hall'),(23,'rime-vault')]),homes())
    s.ctl('eval','hl.plugin.hyprune.config({windows={workspace_base=10}})')
    # Defaults are per loaded world; a world without areas derives none.
    client.call('world.load',{'worldId':'com.hyprune.switchyard'})
    check('area-less world derives no homes',wait(lambda:homes()==[(11,'signal-rise')]),homes())
    client.call('world.load',{'worldId':LUMEN})
    check('reloading recomputes homes',wait(lambda:homes()==[(11,'signal-rise'),(12,'array-hall'),(13,'rime-vault')]),homes())
    check('explicit bind still the only persisted home',saved()==[{'id':11,'worldId':LUMEN,'areaId':'signal-rise'}],saved())
    client.call('runtime.exit',{})
    evidence={'nestedOnly':True,'lock':'/tmp/worldshell-nested.lock','world':LUMEN,'checks':checks}
    (s.ROOT/'docs/evidence/workspace-homes.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('PASS default workspace homes:',len(checks),'checks',flush=True)
