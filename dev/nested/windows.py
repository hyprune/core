"""Window records match the compositor's clients: dev/nested/run.sh --windows.

Lumen Reach with the room (workspace 20) and its homes 21-24 populated past the old
16-window capture lease, then the in-world window actions: spawning while facing a
home wall and elsewhere, recall, pocket from the Surfaces list, F (focus), moving
clients between workspaces, closing them and reloading the world. After each step
core's records are audited against `hyprctl clients`:

- every record (capture entry) is a live mapped client (no ghost records);
- every client on workspaces 20-24 has a record (none listed as desktop-window-*);
- every room client is reachable: drawn, carried or in the pocket; every home client
  is on its workspace wall;
- window volumes (placement refusals) only exist for drawn windows;
- Surfaces actions (pocket, take out) work on every listed room window.

IPC 0.26: with more than 16 room windows pocketed, editor.unplaced lists every one and its
count is right (a 0.25 peer gets 16, the selected one kept); the real shell
(HYPRUNE_SHELL_DIR, default the shell checkout) lists them all in Director > Surfaces.
HYPRUNE_WINDOWS_SHELL=0 skips the shell.

HYPRUNE_LUMEN_PACKAGE selects the Lumen Reach package (default: the worlds checkout's
dist); HYPRUNE_WINDOWS_HOMES the home clients ("21:6,22:3,23:1,24:6", the live layout).
Evidence: docs/evidence/windows.json (stills in the state directory).
"""
import builtins,json,math,os,pathlib,secrets,shutil,subprocess,time
import session as s
from ctl import verified
from m2 import wait,keys,pointer

LUMEN='com.hyprune.lumen-reach'
ROOM=20
HOMES={21:'sun-court',22:'array-hall',23:'rime-vault',24:'signal-rise'}
GROUP='hyprune-windows'
SHELL_TOKEN=secrets.token_hex(32)   # the real shell at 0.26
OLD_PEER=secrets.token_hex(32)      # a 0.25 peer: 16-item pocket list
EXTRA=14                            # room windows added for the >16 pocket step

def grants():
    return [{'token':SHELL_TOKEN,'client':'com.hyprune.shell','capabilities':['state.read','world.control','surface.control','movement.control',
             'shell.control','runtime.control','input.control','quest.control','objects.control']},
            {'token':OLD_PEER,'client':'com.example.peer-025','capabilities':['state.read']}]

def prepare(installed):
    source=pathlib.Path(os.environ.get('HYPRUNE_LUMEN_PACKAGE',str(s.ROOT.parent/'worlds/dist'/LUMEN)))
    if not (source/'world.json').is_file():raise RuntimeError('Build or set HYPRUNE_LUMEN_PACKAGE first')
    shutil.rmtree(installed/LUMEN,ignore_errors=True);shutil.copytree(source,installed/LUMEN)

def acceptance(client,children,args,shell_token,socketpath):
    steps=[];stills=[];failures=[]
    def clients():return json.loads(s.ctl('clients','-j'))
    def workspace():return next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')['activeWorkspace']['id']
    def evidence(world=None):
        data={'nestedOnly':True,'world':world,'homes':os.environ.get('HYPRUNE_WINDOWS_HOMES','21:6,22:3,23:1,24:6'),
              'steps':steps,'failures':failures,'stills':[pathlib.Path(p).name for p in stills]}
        for path in (s.STATE/'windows.json',s.ROOT/'docs/evidence/windows.json'):path.write_text(json.dumps(data,indent=2)+'\n')
    def shot(name):
        path=s.STATE/name;subprocess.run([str(s.HERE/'ctl.py'),'shot',str(path)],check=True);stills.append(str(path))
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':LUMEN})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    # Launch tokens expire in 30 s: the 0.25 peer and the real shell connect now.
    old=s.Client(str(socketpath),OLD_PEER,'0.25','com.example.peer-025')
    osub=old.call('state.subscribe',{'topics':['state']})['subscriptionId']
    ui=None
    if os.environ.get('HYPRUNE_WINDOWS_SHELL','1')!='0':
        read,write=os.pipe();os.write(write,(SHELL_TOKEN+'\n').encode());os.close(write)
        _,environment=verified()
        environment.update(HYPRUNE_TOKEN_FD=str(read),HYPRUNE_PROTOCOL='0.26',HYPRUNE_SOCKET=str(socketpath),
                           HYPRUNE_OUTPUT_SCREEN='HEADLESS-1',HYPRUNE_DEV_AUTOMATION='1')
        shell_dir=pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR',str(s.ROOT.parent/'shell')))
        shell=subprocess.Popen(['python3',str(shell_dir/'bridge/launch.py'),'--protocol','0.26'],env=environment,
                               pass_fds=(read,),stdout=open(s.STATE/'shell.log','w'),stderr=subprocess.STDOUT)
        os.close(read);children.append(shell)
        def childpid():
            assert shell.poll() is None,'shell exited; see shell.log'
            return builtins.next((int(q) for q in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
                                  if pathlib.Path(f'/proc/{q}/comm').read_text().strip()=='qs'),None)
        qpid=wait(childpid,30)
        def ui(*a):
            return subprocess.check_output([str(s.HERE/'ctl.py'),'env','qs','ipc','--pid',str(qpid),'call','preview',*map(str,a)],text=True).strip()
        def status():
            try:return json.loads(ui('status'))
            except (subprocess.CalledProcessError,json.JSONDecodeError):return {}
        wait(lambda:status().get('protocol')=='0.26',30)  # the lease follows once the world is shown
        print('windows: shell connected at',status().get('protocol'),flush=True)

    # One kitty process for every window (as with a single-instance terminal).
    serial=[0]
    def spawn(prefix,ws=None,colour='#1a1d21'):
        serial[0]+=1;title=f'{prefix}-{serial[0]:02d}'
        if ws:s.ctl('eval','hl.window_rule({name="'+title+'",match={title="^'+title+'$"},workspace="'+str(ws)+' silent"})')
        banner=f'printf "\\n  {title}\\n"; exec bash --noprofile --norc'
        children.append(s.envrun('kitty','--single-instance','--instance-group',GROUP,'--title',title,'--override','background='+colour,
                                 '--override','cursor_blink_interval=0','bash','--noprofile','--norc','-c',banner,
                                 stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
        wait(lambda:any(c['title']==title for c in clients()),20)
        return title
    def native(title):return next(c for c in clients() if c['title']==title)

    def audit(step,settle=6):
        """Mismatches between core's records and the compositor's clients, after letting
        floats/tiles settle (a fresh room client floats on the next tick)."""
        found=[]
        until=time.monotonic()+settle
        while True:
            found=[]
            live={c['address']:c for c in clients() if c['mapped'] and not c['hidden']}
            ours={a:c for a,c in live.items() if ROOM<=c['workspace']['id']<=max(HOMES)}
            cam=s.camera();st=state()
            captures=[c for c in cam['captures'] if not c['parentId']]
            byaddr={c['address']:c for c in captures}
            pocket={w['surfaceId'] for w in st['editor']['unplaced']['windows']}
            for c in captures:
                if c['address'] not in live:
                    found.append({'kind':'ghost record','id':c['id'],'title':c['title']})
            for a,c in ours.items():
                e=byaddr.get(a)
                if not e:
                    why=next((u['reason'] for u in cam['uncaptured'] if u.get('address')==a),'not seen')
                    found.append({'kind':'no record','title':c['title'],'workspace':c['workspace']['id'],'reason':why});continue
                ws=c['workspace']['id']
                if e['workspaceId']!=ws:
                    found.append({'kind':'stale workspace','id':e['id'],'title':c['title'],'record':e['workspaceId'],'client':ws})
                if ws==ROOM and not (e['visible'] or e['id'] in pocket or e['queued']):
                    found.append({'kind':'unreachable room window','id':e['id'],'title':c['title'],'floating':c['floating']})
                if ws==ROOM and not c['floating']:
                    found.append({'kind':'room window tiled','id':e['id'],'title':c['title']})
                if ws in HOMES and c['floating']:
                    found.append({'kind':'home window floating','id':e['id'],'title':c['title'],'workspace':ws})
            listed={x['id'] for x in st['surfaces'] if x['kind']=='window'}
            for x in st['surfaces']:
                if x['kind']=='window' and x['id'].startswith('desktop-window-'):
                    a='0x%x'%int(x['id'].rsplit('-',1)[1])
                    if a in ours:found.append({'kind':'listed without record','id':x['id'],'title':x['title']})
            for e in captures:
                if e['id'] not in listed:found.append({'kind':'record not listed','id':e['id'],'title':e['title']})
            if not found or time.monotonic()>until:break
            time.sleep(.5)
        counts={'clients':len(ours),'records':len(captures),'room':sum(1 for c in ours.values() if c['workspace']['id']==ROOM),
                'drawn':sum(1 for c in captures if c['visible']),'pocketed':len(pocket)}
        steps.append({'step':step,'counts':counts,'mismatches':found})
        print(('PASS' if not found else 'MISMATCH')+' windows:',step,counts,json.dumps(found)[:900],flush=True)
        failures.extend({'step':step,**f} for f in found)
        return found

    # The live layout: six Sun Court windows, Array Hall's browser and desk terminals,
    # Rime Vault's kiosk, six Signal Rise terminals and two room clients: 18 clients.
    homes=[tuple(map(int,h.split(':'))) for h in os.environ.get('HYPRUNE_WINDOWS_HOMES','21:6,22:3,23:1,24:6').split(',')]
    for ws,count in homes:
        for _ in range(count):spawn(f'Home{ws}',ws)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({workspace="20"}))')
    for _ in range(2):spawn('Room',ROOM,'#203040')
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active'])
    wait(lambda:workspace()==ROOM)
    time.sleep(2)
    audit(f'entered with {sum(c for _,c in homes)+2} clients on 20-24')

    # Facing Sun Court's primary wall from the plaza, spawn terminals (focused room).
    st=state();anchors={a['id']:a for a in st['mounts']['anchors']};areas={a['id']:a for a in st['mounts']['areas']}
    a=anchors[areas['sun-court']['primaryAnchor']];x,y,z,w=a['pose']['rotation'];p=a['pose']['position']
    n=[2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)];yaw=math.atan2(-n[0],n[2])
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    s.ctl('eval',f'hl.plugin.hyprune.camera({p[0]+n[0]*14},1.7,{p[2]+n[2]*14},{yaw},0)');time.sleep(.5)
    spawned=[spawn('Spawn',None,'#302018') for _ in range(4)]
    time.sleep(1.5);shot('windows-spawn-sun-court.png')
    audit('spawned 4 facing Sun Court')
    for turn in [math.pi/2,math.pi]:
        s.ctl('eval',f'hl.plugin.hyprune.camera({p[0]+n[0]*14},1.7,{p[2]+n[2]*14},{yaw+turn},0)');time.sleep(.4)
        spawned.append(spawn('Spawn',None,'#183020'))
    audit('spawned 2 facing away')

    # Recall all windows.
    client.call('surfaces.recall',{});time.sleep(1)
    shot('windows-recall.png')
    audit('recall all')
    # Recalled windows spread out ahead of the player: no two overlap (same plane facing
    # the player: their rectangles are disjoint, or they stand at different depths).
    rec={x['surfaceId'] for x in state()['mounts']['instances'] if x['status']=='recalled'}
    planes=[c for c in s.camera()['captures'] if c['id'] in rec and c['visible'] and not c['parentId']]
    def apart(a,b):
        n=a['normal'];r=[-n[2],0,n[0]]
        d=[b['position'][k]-a['position'][k] for k in range(3)]
        dot=lambda u,v:sum(x*y for x,y in zip(u,v))
        return abs(dot(d,r))>=(a['size'][0]+b['size'][0])/2-.01 or abs(d[1])>=(a['size'][1]+b['size'][1])/2-.01 or abs(dot(d,n))>.3
    piled=[(a['title'],b['title']) for i,a in enumerate(planes) for b in planes[i+1:] if not apart(a,b)]
    result={'recalled':len(rec),'visible':len(planes),'overlapping':piled[:8]}
    steps.append({'step':'recall spreads windows','result':result})
    if piled or len(planes)<len(rec):failures.append({'step':'recall','kind':'recalled windows overlap or hidden','result':result})
    print(('PASS' if not piled and len(planes)==len(rec) else 'MISMATCH')+' windows: recall spreads windows',result,flush=True)

    # IPC 0.26: more than 16 room windows in the pocket, all listed (0.25: 16), and the real
    # shell's Director > Surfaces lists them all.
    extra=[spawn('Extra',ROOM,'#282038') for _ in range(EXTRA)]
    time.sleep(1.5)
    audit(f'{EXTRA} more room windows')
    for c in s.camera()['captures']:
        if c['workspaceId']==ROOM and not c['parentId']:
            try:client.call('surface.pocket',{'surfaceId':c['id']})
            except Exception as error:failures.append({'step':'pocket > 16','kind':'pocket refused','id':c['id'],'error':str(error)})
    time.sleep(1)
    st=state();u=st['editor']['unplaced']
    room_ids={c['id'] for c in s.camera()['captures'] if c['workspaceId']==ROOM and not c['parentId']}
    listed=[w['surfaceId'] for w in u['windows']]
    ou=old.call('state.snapshot',{'subscriptionId':osub})['snapshot']['state']['editor']['unplaced']
    whole={'room':len(room_ids),'listed':len(listed),'count':u['count'],'objects':len(u['objects']),'old':len(ou['windows']),'oldCount':ou['count'],
           'oldSelected':ou['selectedId'] in [w['surfaceId'] for w in ou['windows']] if ou['selectedId'] else None}
    ok=len(room_ids)>16 and set(listed)==room_ids and len(listed)==len(set(listed)) and u['count']==len(listed)+len(u['objects']) and \
       len(ou['windows'])==16 and ou['count']==min(u['count'],32) and whole['oldSelected'] is not False
    steps.append({'step':'pocket more than 16 (IPC 0.26)','result':whole})
    if not ok:failures.append({'step':'pocket > 16','kind':'pocket list cut','result':whole})
    print(('PASS' if ok else 'MISMATCH')+' windows: whole pocket list',whole,flush=True)
    audit(f'{len(room_ids)} room windows pocketed')
    if ui:
        wait(lambda:status().get('lease'),30)
        keys('-P','Tab','-s','1100','-p','Tab');wait(lambda:status().get('director'),10)  # a Tab hold opens the Director
        ui('tabNamed','surfaces');time.sleep(1.5)
        seen=status().get('surfaces',{})
        badges=set(seen.get('pocketBadges') or [])
        director={'pocketed':seen.get('pocketed'),'badges':len(badges),'label':seen.get('pocketLabel'),'tab':status().get('tabName')}
        ok=seen.get('pocketed')==len(listed) and badges==set(listed) and seen.get('pocketLabel')==f'IN THE POCKET  /  {len(listed)}'
        steps.append({'step':'Director > Surfaces lists the whole pocket','result':director})
        if not ok:failures.append({'step':'Director pocket','kind':'shell list cut','result':director})
        print(('PASS' if ok else 'MISMATCH')+' windows: Director > Surfaces',director,flush=True)
        shot('windows-director-pocket.png')
        ui('scrollPage',1);time.sleep(.6);shot('windows-director-pocket-end.png')
        ui('close');wait(lambda:not status().get('director'),10)
    # The extra windows close while pocketed: their records go with them.
    for title in extra:
        s.ctl('eval','hl.dispatch(hl.dsp.window.close({window='+json.dumps('address:'+native(title)['address'])+'}))')
    wait(lambda:not any(c['title'] in extra for c in clients()),20)
    for c in list(state()['editor']['unplaced']['windows']):client.call('surface.unpocket',{'surfaceId':c['surfaceId']})
    time.sleep(1)
    audit('closed the pocketed extras, took the rest out')
    old.close()

    # Director > Surfaces: pocket and take out every listed room window.
    room={c['address'] for c in clients() if c['workspace']['id']==ROOM}
    results=[]
    for x in [x for x in state()['surfaces'] if x['kind']=='window']:
        e=next((c for c in s.camera()['captures'] if c['id']==x['id']),None)
        addr=e['address'] if e else ('0x%x'%int(x['id'].rsplit('-',1)[1]) if x['id'].startswith('desktop-window-') else None)
        if addr not in room:continue
        try:client.call('surface.pocket',{'surfaceId':x['id']});results.append({'id':x['id'],'title':x['title'],'pocket':'ok'})
        except Exception as error:results.append({'id':x['id'],'title':x['title'],'pocket':str(error)})
    steps.append({'step':'pocket each listed room window','results':results})
    refused=[r for r in results if r['pocket']!='ok']
    print(('PASS' if not refused else 'MISMATCH')+' windows: pocket from Surfaces',json.dumps(refused)[:600],flush=True)
    failures.extend({'step':'pocket from Surfaces','kind':'pocket refused',**r} for r in refused)
    audit('all room windows pocketed')
    for r in results:
        if r['pocket']=='ok':
            try:client.call('surface.unpocket',{'surfaceId':r['id']})
            except Exception as error:failures.append({'step':'take out','kind':'unpocket refused','id':r['id'],'error':str(error)})
    time.sleep(1);shot('windows-unpocketed.png')
    audit('taken out of the pocket')

    # The Placement tool places a pocketed window ahead, on open ground (the plaza cleared
    # into the pocket first).
    def front(c,d):
        n=c['normal'];q=c['position'];return q[0]+n[0]*d,q[1],q[2]+n[2]*d,math.atan2(-n[0],n[2])
    def pocketed():return state()['editor']['unplaced']['count']
    for c in s.camera()['captures']:
        if c['workspaceId']==ROOM and c['visible']:client.call('surface.pocket',{'surfaceId':c['id']})
    before=pocketed()
    s.ctl('eval',f'hl.plugin.hyprune.camera({p[0]+n[0]*14},1.7,{p[2]+n[2]*14},{yaw+math.pi},0)');time.sleep(.5)
    keys('-k','5');wait(lambda:state()['editor']['tool']==5)
    pointer('button',272)
    try:wait(lambda:pocketed()==before-1,5)
    except AssertionError:pass
    placed=pocketed()
    steps.append({'step':'place ahead with the Placement tool','pocketed':[before,placed]})
    if placed!=before-1:failures.append({'step':'place ahead','kind':'placement refused','pocketed':[before,placed]})
    print(('PASS' if placed==before-1 else 'MISMATCH')+' windows: place ahead',[before,placed],flush=True)
    shot('windows-placed.png');keys('-k','1')
    audit('placed with the Placement tool')
    for c in list(state()['editor']['unplaced']['windows']):client.call('surface.unpocket',{'surfaceId':c['surfaceId']})
    time.sleep(1)
    audit('rest taken out of the pocket')

    # F (focus) on a drawn room window: native full screen, records still follow the
    # clients (one closed meanwhile), then back to the world.
    target=next((c for c in s.camera()['captures'] if c['visible'] and c['workspaceId']==ROOM),None)
    if not target:
        failures.append({'step':'F','kind':'no drawn room window to focus'})
        evidence()
        raise AssertionError(json.dumps(failures)[:4000])
    s.ctl('eval','hl.plugin.hyprune.camera(%s,%s,%s,%s,0)'%front(target,2.4));time.sleep(.4)
    try:aimed=wait(lambda:s.camera()['aim']==target['id'],5)
    except AssertionError:aimed=False
    if not aimed:failures.append({'step':'F','kind':'window not aimable from its front','id':target['id'],'aim':s.camera()['aim']})
    keys('f')
    try:wait(lambda:s.camera()['fullscreen']==2,5)
    except AssertionError:failures.append({'step':'F','kind':'focus did not engage','fullscreen':s.camera()['fullscreen']})
    shot('windows-focus.png')
    kept=next((c for c in s.camera()['captures'] if c['id']==target['id']),None)
    steps.append({'step':'F','target':target['id'],'aimed':bool(aimed),'kept':bool(kept)})
    if not kept:failures.append({'step':'F','kind':'focused window lost its record','id':target['id']})
    audit('F on '+target['title'])
    other=next(c for c in clients() if c['workspace']['id']==ROOM and c['address']!=target['address'] and c['title'] not in spawned)
    s.ctl('eval','hl.dispatch(hl.dsp.window.close({window='+json.dumps('address:'+other['address'])+'}))')
    wait(lambda:not any(c['address']==other['address'] for c in clients()),10)
    audit('closed a room window during F focus')
    s.ctl('eval',f'hl.dispatch(hl.dsp.window.fullscreen({{window="address:{target["address"]}",action="unset"}}))')
    try:wait(lambda:s.camera()['fullscreen']==0,8)
    except AssertionError:failures.append({'step':'F','kind':'focus did not end','fullscreen':s.camera()['fullscreen']})
    time.sleep(.8)
    audit('left focus')

    # Native moves: a room client home, a home client into the room, one between homes.
    housed=[c for c in clients() if c['workspace']['id'] in HOMES]
    moves=[(spawned[0],22),(housed[0]['title'],ROOM)]
    if len(housed)>1:moves.append((housed[-1]['title'],23 if housed[-1]['workspace']['id']!=23 else 21))
    for title,ws in moves:
        s.ctl('eval',f'hl.dispatch(hl.dsp.window.move({{workspace="{ws}",follow=false,window="address:{native(title)["address"]}"}}))')
    time.sleep(1.5)
    audit('moved clients between workspaces')

    # Close clients (some captured, some formerly uncaptured).
    for title in spawned[2:5]:
        s.ctl('eval','hl.dispatch(hl.dsp.window.close({window='+json.dumps('address:'+native(title)['address'])+'}))')
    wait(lambda:not any(c['title'] in spawned[2:5] for c in clients()),10)
    audit('closed 3 clients')

    # Reload the active world.
    client.call('world.load',{'worldId':LUMEN});time.sleep(3)
    shot('windows-reloaded.png')
    audit('world reloaded')

    client.call('movement.set',{'modeId':'com.hyprune.walk'})
    evidence(state()['world'])
    print('windows evidence: docs/evidence/windows.json',flush=True)
    assert not failures,json.dumps(failures)[:4000]
    print('PASS windows: core records match the compositor through every step',flush=True)
