"""World/style/visibility IPC regression; only run under the nested lifetime flock."""
import json,os,pathlib,re,shutil,subprocess,time
import session as s
from m2 import wait,keys,pose

def prepare(installed):
    (s.STATE/'layout.json').unlink(missing_ok=True);(s.STATE/'ipc-warp-pending.json').unlink(missing_ok=True)
    source=pathlib.Path(os.environ.get('HYPRUNE_WARP_PACKAGE',s.ROOT/'tests/fixtures/visibility'))
    if 'HYPRUNE_WARP_PACKAGE' not in os.environ:subprocess.run(['python3',str(s.ROOT/'tests/fixtures/visibility.py')],check=True,stdout=subprocess.DEVNULL)
    manifest=json.loads((source/'world.json').read_text())
    target=installed/manifest['id']
    shutil.copytree(source,target,dirs_exist_ok=True)
    if 'HYPRUNE_WARP_PACKAGE' not in os.environ:
        manifest.setdefault('extensions',{})['com.hyprune.hud-style']={'theme':'hud/test.json'}
        (target/'hud').mkdir(exist_ok=True)
        (target/'hud/test.json').write_text(json.dumps({'version':1,'colors':{'accent':'#88ddff'}}))
        (target/'world.json').write_text(json.dumps(manifest))
    (s.STATE/'warp-target.json').write_text(json.dumps({'id':manifest['id']}))

def acceptance(client,children,args,token,socketpath):
    target=json.loads((s.STATE/'warp-target.json').read_text())['id']
    lumen='com.hyprune.lumen-reach'
    checks=[]
    def check(label,value=True):assert value,label;checks.append(label);print('PASS IPC warp:',label,flush=True)
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.2)
    # Same persistent virtual pointer as the placement acceptance: entry grabs it.
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    def pointer(*args):
        device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.1)
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    client.call('hud.subscribe',{'topics':['hud.frame','hud.pose','zone.entered','zone.exited','tool.changed']})
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    def queue():return state()['editor']['unplaced']
    def counters():
        v=state();return {'queue':v['editor']['unplaced']['count'],'tool':v['editor']['tool'],'world':v['world']['id'] if v.get('world') else None,'instances':len(v['mounts']['instances'])}
    def launch(title):
        child=s.envrun('kitty','--class','warp-'+title,'--title',title,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(child)
        wait(lambda:any(w['title']==title for w in json.loads(s.ctl('clients','-j'))));time.sleep(.4)
    def place():
        # Tool 5 LMB mounts the selected pocketed client in free space, with no
        # workspace binding: after a world load it is an unzoned mount instance.
        keys('-k','5');wait(lambda:state()['editor']['tool']==5)
        before=queue()['count']
        position=state()['movement']['position']
        for yaw in [0,1.57,3.14,-1.57]:
            # Turn in place if the spawn view is blocked; never move the player.
            pose(client,position,yaw,0);time.sleep(.3)
            pointer('button',272)
            try:wait(lambda:queue()['count']==before-1,timeout=3);return
            except AssertionError:print('placement retry',counters(),flush=True)
        raise AssertionError('tool-5 placement did not mount: '+json.dumps(counters()))
    def unzoned(label):
        instances=state()['mounts']['instances']
        check(label+': mount instances present',bool(instances))
        check(label+': no empty instance areaId',all(v.get('areaId')!='' for v in instances))
        check(label+': unzoned instance omits areaId',any('areaId' not in v for v in instances))
    client.call('world.load',{'worldId':target})
    # Leave world workspace 8 unbound; real clients adopted there are pocketed.
    s.ctl('eval','hl.dispatch(hl.dsp.focus({monitor="HEADLESS-1"}))')
    s.ctl('eval','hl.dispatch(hl.dsp.focus({workspace="8"}))')
    launch('Warp-A');launch('Warp-B')
    s.ctl('eval','hl.dispatch(hl.dsp.focus({workspace="91"}))')
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda:queue()['count']==2)
    check('unbound world workspace',not any(b['id']==8 for b in state()['mounts']['bindings']))
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    place()
    client.call('world.load',{'worldId':lumen});wait(lambda:state()['world']['id']==lumen);time.sleep(.4)
    wait(lambda:queue()['count']==2)
    place()
    sequence=[target,lumen,target,lumen]
    connection=client.socket.fileno()
    for index,ident in enumerate(sequence):
        client.call('world.load',{'worldId':ident});wait(lambda:state()['world']['id']==ident);time.sleep(.6)
        label='warp %d'%(index+1)
        unzoned(label+' after')
        # Generic read paths touched by the shell after every warp.
        client.call('graphics.get',{});client.call('atmosphere.get',{});client.call('camera.get',{})
        unzoned(label+' before next')
    check('same IPC connection, no reconnect through warps',client.socket.fileno()==connection and bool(client.call('session.ping',{})))
    path=s.STATE/'ipc-warp-wire';path.mkdir(exist_ok=True)
    (path/'transcript.json').write_text(json.dumps(client.transcript));(path/'notifications.json').write_text(json.dumps(client.events))
    result=subprocess.run(['node',str(s.ROOT/'tests/validate-m3b-wire.mjs'),str(path)],check=True,capture_output=True,text=True).stdout.strip()
    check('client-side 0.15 responses and notifications valid',result.startswith('PASS IPC 0.15'));print(result,flush=True)
    client.call('runtime.exit',{})
    # The helper reports its audit total when nested Hyprland shuts down; finish() checks it.
    (s.STATE/'ipc-warp-pending.json').write_text(json.dumps({'clientFrames':result,'checks':checks}))

def finish():
    pending=s.STATE/'ipc-warp-pending.json'
    if not pending.exists():return
    record=json.loads(pending.read_text());pending.unlink()
    log=(s.STATE/'hyprland.log').read_text()
    audit=re.findall(r'IPC schema audit: (\d+) outgoing frames validated',log)
    assert audit and int(audit[-1])>0 and 'IPC SCHEMA ASSERTION' not in log,'core wire audit missing or failed'
    record['checks'].append('core wire audit validated every outgoing frame');print('PASS IPC warp: core wire audit',audit[-1],'frames',flush=True)
    # Only generic checks are publishable; never serialize package identities.
    (s.ROOT/'docs/evidence/ipc-warp.json').write_text(json.dumps({'nestedOnly':True,'status':'passed','outgoingFramesSchemaValidated':int(audit[-1]),'clientFrames':record['clientFrames'],'checks':record['checks']},indent=2)+'\n')
