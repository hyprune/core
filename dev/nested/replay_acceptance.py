#!/usr/bin/env python3
"""Run under flock /tmp/worldshell-nested.lock; no outer-session control."""
import argparse,json,os,pathlib,subprocess,time,shutil
import session as s
import m3b
from m2 import wait,keys,pose,pointer

def acceptance(client,children,args,shell_token,socketpath):
    client.call('world.load',{'worldId':'com.hyprune.lumen-reach'})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:state()['runtime']['active'])
    client.call('movement.set',{'modeId':'com.hyprune.fly'});pose(client,[-8,2,12]);time.sleep(.2)
    cache=client.call('graphics.get',{})['timings']['shaderCache'];assert cache['warming'] is False
    before=cache['hits']+cache['misses']
    for quality in ['low','medium','high','ultra','low']:
        client.call('graphics.set',{'patch':{'quality':quality}});time.sleep(.08)
    after=client.call('graphics.get',{})['timings']['shaderCache'];assert before==after['hits']+after['misses']
    config=client.call('input.get',{})['config'];assert config['modes']['world']['record.toggle']==[]
    config['modes']['world']['record.toggle']=['F8'];client.call('input.set',{'config':config})
    keys('-k','F8');wait(lambda:client.call('recording.status',{})['recording'])
    keys('-P','w','-s','450','-p','w');keys('-P','d','-s','300','-p','d')
    keys('-k','2');pointer('button','272');pointer('scroll','-120');keys('-P','q','-s','150','-p','q')
    pointer('45','-15');time.sleep(.4)
    keys('-P','Tab','-s','1300','-p','Tab');time.sleep(.15);keys('-k','Escape');time.sleep(.2)
    client.call('recording.mark',{'name':'end'})
    keys('-k','F8');record=wait(lambda: (lambda v: v if not v['recording'] else None)(client.call('recording.status',{})))
    path=pathlib.Path(record['path']);data=[json.loads(line) for line in path.read_text().splitlines()]
    assert {'input','frame','view','mark','end'} <= {j['kind'] for j in data}
    assert {'key','button','axis','motion'} <= {j['event']['type'] for j in data if j['kind']=='input'}
    assert path.stat().st_mode&0o777==0o600
    results=[]
    for attempt in range(2):
        client.call('replay.start',{'path':str(path),'assertions':{'recorded':True,'tolerance':.002,'marks':{'end':{'tool':2,'mode':'world'}}}})
        done=wait(lambda: (lambda v: v if not v['playing'] else None)(client.call('replay.status',{})),timeout=20)
        assert not done['failures'],done
        results.append(done)
    client.call('replay.start',{'path':str(path),'mode':'camera','smoothingMs':100})
    done=wait(lambda: (lambda v: v if not v['playing'] else None)(client.call('replay.status',{})),timeout=20);assert not done['failures']
    # Explicit stop is idempotent and cleans input ownership.
    client.call('replay.start',{'path':str(path)});client.call('replay.stop',{});client.call('replay.stop',{})
    evidence=s.ROOT/'docs/evidence/replay';evidence.mkdir(exist_ok=True)
    shutil.copyfile(path,evidence/'scripted.jsonl')
    (evidence/'acceptance.json').write_text(json.dumps({'replays':results,'camera':done,'shaderCache':after,'checks':['rebindable F8','raw key/button/axis/motion','per-frame view','marker assertions','two exact fixed-step replays','camera smoothing','idempotent stop','no new effect programs']},indent=2)+'\n')
    (evidence/'assert.json').write_text(json.dumps({'recorded':True,'tolerance':.002,'marks':{'end':{'tool':2,'mode':'world'}}},indent=2)+'\n')
    # Verify the real keyboard routing boundary, not just the recorder's redaction helper.
    children.append(s.envrun('kitty','--class','replay-private','--title','Replay privacy','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    surface=wait(lambda:next((v for v in state()['surfaces'] if v['title']=='Replay privacy'),None))
    for opt in [False,True]:
        client.call('recording.start',{'includeAppKeys':opt})
        client.call('surface.focus',{'surfaceId':surface['id'],'intent':'keyboard'})
        keys('z');time.sleep(.15);keys('-M','logo','-P','Super_L','-s','40','-k','Tab','-p','Super_L','-m','logo');wait(lambda:state()['editor']['mode']=='world')
        clip=client.call('recording.stop',{})
        private=[json.loads(line) for line in pathlib.Path(clip['path']).read_text().splitlines()]
        events=[v['event'] for v in private if v['kind']=='input']
        if opt:assert any(e.get('key')=='z' for e in events),events
        else:assert any(e.get('redacted') for e in events) and not any(e.get('key')=='z' for e in events),events
    result=json.loads((evidence/'acceptance.json').read_text());result['checks']+=['real app-key default redaction','explicit raw-key opt-in'];(evidence/'acceptance.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS nested recording/replay + app-key privacy',len(data),'records',flush=True)

if __name__=='__main__':
    os.environ['HYPRUNE_NESTED_PROTOCOL']='0.12'
    fixture=s.HERE/'.m3b-state';fixture.mkdir(exist_ok=True)
    for name in ['input.json','layout.json','editor.json']:(fixture/name).unlink(missing_ok=True)
    m3b.acceptance=acceptance
    s.run(argparse.Namespace(m3b=True,m3a=False,m2=False,m1=False,integration=False,live=False,serve=False,record=False,visible=False))
