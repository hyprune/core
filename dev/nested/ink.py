"""IPC 0.14 live ink quality/capture acceptance. Called only under run.sh's flock."""
import json,statistics,time,subprocess,shutil
import session as s
from m2 import wait,keys

def acceptance(client,children,args,shell_token,socketpath):
    report={'nestedOnly':True,'protocol':'0.14','checks':[],'measurements':[],'captures':[]}
    def check(name,ok):
        assert ok,name
        report['checks'].append(name);print('PASS ink:',name,flush=True)
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.2)
    m=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    xy=list(map(float,s.ctl('cursorpos').split(',')))
    p=s.envrun(str(s.ROOT/'build/nested-pointer'),str(m['x']+m['width']/m['scale']/2-xy[0]),str(m['y']+m['height']/m['scale']/2-xy[1]));p.wait(timeout=5)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':'com.hyprune.lumen-reach'})
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');time.sleep(.7)
    settings=client.call('graphics.set',{'patch':{'quality':'low','offload':'off','dynamic_resolution':False,'outline_quality':None}})
    check('low_inherits_fast',settings['effective']['outline_quality']=='fast')
    check('performance_tiers',settings['options']['outline_quality']==['fast','smooth'] and settings['presets']['high']['outline_quality']=='smooth')
    old=s.Client(str(socketpath),shell_token,protocol='0.13',client_id='com.hyprune.shell')
    response=old.call('graphics.get',{})
    check('older_response_frozen','outline_quality' not in response['config'])
    old.close()
    captures=s.STATE/'statehome/hyprune/captures'
    destination=s.STATE/'ink-captures';destination.mkdir(exist_ok=True)
    for world in ['com.hyprune.lumen-reach','com.hyprune.switchyard']:
        client.call('world.load',{'worldId':world});time.sleep(1)
        pose=client.call('camera.get',{})
        client.call('camera.photo',{'position':pose['position'],'orientation':pose['orientation'],'fov':60,'renderScale':.5})
        style=client.call('atmosphere.get',{})['profile']
        style['outline']={'enabled':True,'colour':[.02,.025,.04],'widthPx':2,'depthThreshold':.02,'normalThreshold':.2}
        client.call('atmosphere.set',{'profile':style})
        for tier in ['off','fast','smooth']:
            client.call('graphics.set',{'patch':{'outline':tier!='off','outline_quality':'fast' if tier=='off' else tier}});time.sleep(1)
            samples=[]
            for _ in range(32):
                samples.append(client.call('graphics.get',{})['timings']);time.sleep(.1)
            last=samples[-1]
            item={'world':world,'tier':tier,'renderer':last['renderer'],'size':last['nativeSize'],'scale':last['renderScale'],
                  'outlineMs':statistics.median(t['gpuEffectsMs']['outline'] for t in samples),
                  'totalGpuMs':statistics.median(sum(t['gpuPassMs']) for t in samples),
                  'upscaleMs':statistics.median(t['gpuEffectsMs']['upscale_sharpen'] for t in samples)}
            report['measurements'].append(item);print('MEASURE ink',json.dumps(item),flush=True)
            check(world+' '+tier+' Intel', 'Intel' in str(last['renderer']))
            before=set(captures.glob('*.png'));keys('-k','u');wait(lambda:bool(set(captures.glob('*.png'))-before),timeout=20)
            png=next(iter(set(captures.glob('*.png'))-before));wait(lambda:png.with_suffix('.json').exists(),timeout=20);target=destination/(world+'-'+tier+'.png')
            shutil.copyfile(png,target);shutil.copyfile(png.with_suffix('.json'),target.with_suffix('.json'))
            report['captures'].append(str(target.relative_to(s.ROOT)))
        # Exercise live MSAA/tier changes with a held camera; no reload or app loss.
        for samples in [2,4,1]:
            client.call('graphics.set',{'patch':{'msaa':samples,'outline_quality':'smooth'}});time.sleep(.3)
            check(world+' msaa '+str(samples),client.call('camera.get',{})['held'])
        client.call('camera.release',{})
    client.call('world.load',{'worldId':'org.hyprune.visibility'});time.sleep(.5)
    authored=client.call('atmosphere.get',{})['profile']['outline']
    patch={'outline_quality':'smooth','outline_width_px':3.25,'outline_colour':[.4,.1,.7]}
    client.call('graphics.set',{'patch':patch})
    check('live_width_colour',client.call('atmosphere.get',{})['profile']['outline']['widthPx']==3.25)
    saved=json.loads((s.STATE/'graphics.json').read_text())
    check('override_persisted',all(saved[k]==v for k,v in patch.items()))
    client.call('world.load',{'worldId':'com.hyprune.lumen-reach'});time.sleep(.5)
    client.call('world.load',{'worldId':'org.hyprune.visibility'});time.sleep(.5)
    ink=client.call('atmosphere.get',{})['profile']['outline']
    check('override_survives_world_reload',ink['widthPx']==3.25 and ink['colour']==[.4,.1,.7])
    client.call('graphics.set',{'patch':{'outline_width_px':None,'outline_colour':None,'outline_quality':None}})
    ink=client.call('atmosphere.get',{})['profile']['outline']
    check('null_restores_world_style',ink==authored)
    transcript=s.STATE/'ink-transcript.json';transcript.write_text(json.dumps(client.transcript))
    subprocess.run(['node',str(s.ROOT/'tests/validate-authoring-wire.mjs'),str(transcript),'0.14'],check=True)
    check('IPC014_wire_conformance',True)
    (s.ROOT/'docs/evidence/ink-nested.json').write_text(json.dumps(report,indent=2)+'\n')
