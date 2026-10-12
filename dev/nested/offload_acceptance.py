#!/usr/bin/env python3
"""Only run under lifetime flock AND offload-power.py. No real-session control."""
import argparse,gzip,json,os,pathlib,signal,subprocess,time,tempfile
import session as s
import m3b
from m2 import wait,keys,pose
from ctl import verified

def acceptance(client,children,args,shell_token,socketpath):
    client.call('world.load',{'worldId':'com.hyprune.lumen-reach'})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    def perf():return client.call('graphics.get',{})['performance']
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda:state()['runtime']['active'])
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    pose(client,[-8,2,12])
    assert perf()['mode']=='off' and perf()['activeRenderer']=='in-process'
    recording=None
    if os.environ.get('HYPRUNE_OFFLOAD_RECORD'):
        temp=pathlib.Path(tempfile.mkdtemp(prefix='hyprune-offload-',dir='/tmp'));raw=temp/'raw.mp4'
        keys('-k','F3')
        recording=s.envrun('wf-recorder','-o','HEADLESS-1','-r','30','-D','--no-dmabuf','-c','libx264','-p','preset=ultrafast','-p','crf=20','-x','yuv420p','-f',str(raw),stdout=open(temp/'record.log','w'),stderr=subprocess.STDOUT)
        children.append(recording);time.sleep(1)
        (pathlib.Path('/tmp')/'hyprune-offload-recording.json').write_text(json.dumps({'raw':str(raw)}))
    results=[]
    def active():
        p=perf()
        if p['reason'] not in ['off','inactive','warming','admitted'] and p['activeRenderer']!='offload':
            raise AssertionError(p)
        return p if p['activeRenderer']=='offload' else None
    def switch(mode):
        client.call('graphics.set',{'patch':{'offload':mode}})
        p=wait(active,timeout=25) if mode=='on' else wait(lambda:perf()['activeRenderer']=='in-process')
        if mode=='on':
            samples=[];end=time.monotonic()+1
            while time.monotonic()<end:
                samples.append(perf()['activeRenderer']);time.sleep(.025)
            fraction=samples.count('offload')/len(samples)
            assert fraction>.8,('unstable offload',fraction,perf())
        else:fraction=0
        results.append({'mode':mode,'state':perf(),'activeFraction':fraction})
        if recording:time.sleep(1.3)
        return p
    switch('on')
    before=state()['movement']['position']
    keys('-P','w','-s','600','-p','w')
    assert state()['movement']['position']!=before
    switch('off')
    held=s.envrun('wtype','-P','w','-s','2500','-p','w');children.append(held)
    switch('on');held.wait(timeout=5)
    switch('off')
    pose(client,[-8,2,12])
    children.append(s.envrun('kitty','--class','offload-fixture','--title','Offload carry','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    surface=wait(lambda:next((v for v in state()['surfaces'] if v['title']=='Offload carry'),None))
    plane=wait(lambda:next((v for v in s.camera()['captures'] if v['id']==surface['id']),None));p=plane['position']
    pose(client,[p[0],p[1],p[2]+3]);time.sleep(.2)
    pointer=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(pointer)
    def button(down):pointer.stdin.write('button 272 '+str(down)+'\n');pointer.stdin.flush();assert pointer.stdout.readline().strip()=='ok'
    held=s.envrun('wtype','-M','logo','-P','Super_L','-s','15000','-p','Super_L','-m','logo');children.append(held)
    wait(lambda:s.camera()['superHeld']);button(1);wait(lambda:s.camera()['gesture']==1)
    switch('on');assert s.camera()['gesture']==1
    p=perf();pid=p['pid'];info,_=verified()
    ancestor=pid
    for _ in range(8):
        if ancestor==info['pid']:break
        ancestor=int(pathlib.Path(f'/proc/{ancestor}/stat').read_text().rsplit(')',1)[1].split()[1])
    assert ancestor==info['pid'],'worker is not a nested child'
    assert pathlib.Path(f'/proc/{pid}/exe').resolve()==s.ROOT/'build/hyprune-renderer'
    os.kill(pid,signal.SIGKILL)
    wait(lambda:perf()['activeRenderer']=='in-process')
    assert state()['runtime']['active'] and s.camera()['gesture']==1
    button(0);held.terminate();held.wait(timeout=5)
    switch('off');switch('on');switch('off')
    evidence={'checks':['on/off','on during movement','on during carry','worker crash during carry','explicit retry after crash'], 'states':results}
    (s.STATE/'offload-acceptance.json').write_text(json.dumps(evidence,indent=2)+'\n')
    evidence_dir=s.ROOT/'docs/evidence/offload';evidence_dir.mkdir(exist_ok=True)
    tag='4k' if os.environ.get('HYPRUNE_NESTED_4K') else '1080p'
    (evidence_dir/(tag+'.json')).write_text(json.dumps(evidence,indent=2)+'\n')
    (evidence_dir/(tag+'-wire.json')).write_text(json.dumps(client.transcript)+'\n')
    subprocess.run(['node','--input-type=module','-e',
        "import {readFileSync} from 'node:fs';import {validate} from './lib/validate.mjs';let t=JSON.parse(readFileSync(process.argv[1]));for(let r of t){let v=validate('ipc',r.response,r.method,'0.10');if(!v.valid)throw Error(r.method+' '+v.errors.slice(0,4));}console.log('PASS canonical 0.10 wire',t.length);",
        str(evidence_dir/(tag+'-wire.json'))],cwd=s.ROOT.parent/'schema',check=True)
    wire=evidence_dir/(tag+'-wire.json')
    wire.with_suffix('.json.gz').write_bytes(gzip.compress(wire.read_bytes(),mtime=0))
    wire.unlink()
    if recording:
        recording.send_signal(signal.SIGINT);recording.wait(timeout=10)
        subprocess.run([str(s.HERE/'ctl.py'),'shot',str(evidence_dir/'offload-local.png')],check=True)
    print('PASS OFFLOAD',[(v['mode'],v['activeFraction']) for v in results],flush=True)

if __name__=='__main__':
    # The external watchdog can interrupt safely; session.run finally owns nested cleanup.
    signal.signal(signal.SIGTERM,lambda *_:(_ for _ in ()).throw(SystemExit('power watchdog')))
    os.environ['HYPRUNE_NESTED_PROTOCOL']='0.10'
    fixture=s.state_dir('offload-acceptance');fixture.mkdir(exist_ok=True)
    for name in ['input.json','layout.json','editor.json']:(fixture/name).unlink(missing_ok=True)
    m3b.acceptance=acceptance
    s.run(argparse.Namespace(state='offload-acceptance',m3b=True,m3a=False,m2=False,m1=False,integration=False,live=False,serve=False,record=False,visible=False))
