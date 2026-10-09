"""World/style/visibility IPC regression; only run under the nested lifetime flock."""
import json,os,pathlib,shutil,subprocess,time
import session as s
from m2 import wait,keys

def prepare(installed):
    (s.STATE/'layout.json').unlink(missing_ok=True)
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
    checks=[]
    def check(label,value):assert value,label;checks.append(label);print('PASS IPC warp:',label,flush=True)
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.2)
    client.call('world.load',{'worldId':'com.hyprune.lumen-reach'})
    s.ctl('eval','hl.dispatch(hl.dsp.focus({monitor="HEADLESS-1"}))')
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');time.sleep(.3)
    # Open a real app, then warp with its floating instance alive and unzoned.
    keys('-M','logo','-P','Super_L','-k','Return','-p','Super_L','-m','logo')
    wait(lambda:len(json.loads(s.ctl('clients','-j')))>0)
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    client.call('hud.subscribe',{'topics':['hud.frame','hud.pose','zone.entered','zone.exited','tool.changed']})
    for ident in [target,'com.hyprune.lumen-reach',target]:
        client.call('world.load',{'worldId':ident});time.sleep(.4)
        client.call('surfaces.recall',{});time.sleep(.3)
        snapshot=client.call('state.snapshot',{'subscriptionId':sub})['snapshot']
        check('live instances present after warp',bool(snapshot['state']['mounts']['instances']))
        check('snapshot after warp has no empty instance area',all(v.get('areaId')!='' for v in snapshot['state']['mounts']['instances']))
        client.call('graphics.get',{});client.call('atmosphere.get',{});client.call('camera.get',{})
    check('IPC remains authenticated after repeated warp',bool(client.call('session.ping',{})))
    client.call('runtime.exit',{})
    # Only generic checks are publishable; never serialize package identities.
    (s.ROOT/'docs/evidence/ipc-warp.json').write_text(json.dumps({'nestedOnly':True,'checks':checks},indent=2)+'\n')
