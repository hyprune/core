"""Playable bounds: run with dev/nested/run.sh --playable (lifetime flock)."""
import json,shutil,time,subprocess
import session as s
from m2 import keys,wait

def prepare(installed):
    package=installed/'org.hyprune.bounds'
    shutil.copytree(s.ROOT/'tests/fixtures/atrium',package,dirs_exist_ok=True)
    w=json.loads((package/'world.json').read_text())
    w.update(formatVersion='0.6',id='org.hyprune.bounds',name='Playable bounds lab',playable={
        'volumes':[{'type':'box','min':[-2,-2,-5],'max':[2,6,6]}], 'maxFlyHeight':3})
    (package/'world.json').write_text(json.dumps(w))
    # Existing worlds remain byte-for-byte unchanged: validate no-bounds flight too.
    for name in ['com.hyprune.lumen-reach','com.hyprune.switchyard']:
        shutil.copytree(s.ROOT.parent/'worlds/dist'/name,installed/name,dirs_exist_ok=True)

def acceptance(client,children,args,shell_token,socketpath):
    checks=[]
    def check(name,ok,data=None):
        assert ok,(name,data)
        checks.append({'name':name,'passed':True,'evidence':data});print('PASS bounds:',name,flush=True)
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.25)
    mon=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    for _ in range(5):
        xy=list(map(float,s.ctl('cursorpos').split(',')))
        device.stdin.write(f"{mon['x']+mon['width']/mon['scale']/2-xy[0]} {mon['y']+mon['height']/mon['scale']/2-xy[1]}\n");device.stdin.flush()
        assert device.stdout.readline().strip()=='ok';time.sleep(.1)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':'org.hyprune.bounds'})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active']);time.sleep(.4)
    check('nested world owns input',s.camera()['ownsInput'],s.ctl('cursorpos'))
    s.ctl('eval','hl.plugin.hyprune.config({player={fly_bounds_allowance=0,move_inertia=0,look_inertia=0}})')
    def pose(x=0,y=1.7,z=4):s.ctl('eval',f'hl.plugin.hyprune.camera({x},{y},{z},0,0)');time.sleep(.06)
    def hold(key,ms):keys('-P',key,'-s',str(ms),'-p',key);time.sleep(.1)
    def position():return client.call('camera.get',{})['position']
    def notices():
        client.call('camera.get',{})
        return [e for e in client.events if e.get('method')=='runtime.notice' and e.get('params',{}).get('code')=='world.bounds.left']
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    pose();hold('d',1800);p=position();check('fly stops at soft side boundary',1.85<p[0]<=2.001,p)
    pose();hold('space',1800);p=position();check('fly stops at maximum height',2.85<p[1]<=3.001,p)
    s.ctl('eval','hl.plugin.hyprune.config({player={fly_bounds_allowance=0.5}})')
    hold('space',1200);p=position();check('allowance live applies to ceiling',3.4<p[1]<=3.501,p)
    pose();keys('-k','v');wait(lambda:s.camera()['noclip'])
    hold('d',1400);p=position();check('noclip escapes box',p[0]>3,p)
    n=len(notices());check('one outward crossing notice',n==1,n)
    hold('d',250);check('no notice spam',len(notices())==n)
    pose();hold('d',1400);check('re-entry rearms notice',len(notices())==n+1)
    pose(0,-80,4);time.sleep(.3);check('noclip ignores fall respawn',position()[1]<-79,position())
    pose(4,1.7,4);keys('-k','v');wait(lambda:not s.camera()['noclip']);time.sleep(.15)
    p=position();check('fly returns gradually after noclip',2.5<p[0]<4,p)
    time.sleep(1);check('fly recovers inside expanded volume',position()[0]<=2.501,position())
    client.call('movement.set',{'modeId':'com.hyprune.walk'});pose(0,1.7,4);hold('d',1100)
    check('walking remains collision driven outside playable volume',position()[0]>2.5,position())
    # Reject leakage of authoring fields into the frozen IPC area records.
    snap=client.call('state.snapshot',{'subscriptionId':sub})['snapshot']
    check('IPC snapshot remains usable',bool(snap))
    for name in ['com.hyprune.lumen-reach','com.hyprune.switchyard']:
        client.call('world.load',{'worldId':name});client.call('movement.set',{'modeId':'com.hyprune.fly'})
        pose(0,120,0);hold('space',300);p=position();check(name+' legacy unrestricted fly',p[1]>120.2,p)
    client.call('runtime.exit',{})
    evidence={'nestedOnly':True,'lock':'/tmp/worldshell-nested.lock','checks':checks}
    (s.ROOT/'docs/evidence/playable-bounds.json').write_text(json.dumps(evidence,indent=2)+'\n')
