"""Generic authoring-camera/ink/visibility acceptance, lifetime flock required."""
import json,math,statistics,subprocess,time
import session as s
from m2 import wait,keys

def acceptance(client,children,args,shell_token,socketpath):
    results=[];report={'nestedOnly':True,'protocol':'0.13','samplesPerCase':24,'preset':'low','checks':results,'measurements':[]}
    def check(name,value):
        assert value,name
        results.append(name);print('PASS authoring:',name,flush=True)
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.2)
    monitor=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    xy=list(map(float,s.ctl('cursorpos').split(',')))
    mover=s.envrun(str(s.ROOT/'build/nested-pointer'),str(monitor['x']+monitor['width']/monitor['scale']/2-xy[0]),str(monitor['y']+monitor['height']/monitor['scale']/2-xy[1]));mover.wait(timeout=5)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':'org.hyprune.visibility'})
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');time.sleep(.7)
    check('author_grant', 'camera.author' in client.hello['capabilities'])
    anonymous=s.Client(str(socketpath),protocol='0.13')
    check('observer_cannot_author','camera.photo' not in anonymous.hello['methods'])
    try:anonymous.call('camera.photo',{'position':[0,2,4],'orientation':[0,0,0,1],'fov':15,'renderScale':.5})
    except RuntimeError:pass
    else:raise AssertionError('observer accepted camera write')
    anonymous.close()
    before=client.call('camera.get',{})
    lens={'position':[0,2,4],'orientation':[0,0,math.sqrt(.5),math.sqrt(.5)],'fov':15,'lensShift':[.25,-.15],'renderScale':.5}
    client.call('camera.photo',lens);keys('-P','w','-s','300','-p','w')
    actual=client.call('camera.get',{})
    check('roll_fov_shift_held',actual['held'] and abs(actual['roll']-math.pi/2)<1e-5 and actual['fov']==15 and abs(actual['lensShift'][0]-.25)<1e-6)
    check('movement_cannot_move_held_camera',actual['position']==lens['position'])
    wait(lambda:abs(s.camera()['timings']['renderScale']-.5)<1e-5)
    check('fixed_render_scale',True)
    client.call('waypoint.set',{'id':'lens','label':'LENS MARKER','target':{'position':[0,2,0]}})
    wait(lambda:any(v['text']=='LENS MARKER' for v in s.camera()['overlays']['labels']))
    # Core renders labels with the camera of that frame, including lens shift.
    marker=next(v for v in s.camera()['overlays']['labels'] if v['text']=='LENS MARKER')
    check('shifted_marker_projected',.62<marker['x']<.65 and abs(marker['y']-.575)<.002)
    capture_dir=s.STATE/'statehome/hyprune/captures';prior=set(capture_dir.glob('*.png'))
    keys('-k','u')
    wait(lambda:bool(set(capture_dir.glob('*.png'))-prior),timeout=20)
    # Capture files are written asynchronously, PNG first: wait for the JSON too.
    png=next(iter(set(capture_dir.glob('*.png'))-prior)); wait(lambda:png.with_suffix('.json').exists(),timeout=20)
    dump=json.loads(png.with_suffix('.json').read_text())
    check('photo_capture_PNG',abs(dump['camera']['roll']-math.pi/2)<1e-5 and dump['camera']['fov']==15 and abs(dump['camera']['lensShift'][0]-.25)<1e-6)
    client.call('camera.release',{});restored=client.call('camera.get',{})
    check('release_restores_pose_and_lens',not restored['held'] and restored['orientation']==before['orientation'] and restored['fov']==before['fov'])
    # Measure paired settings with fixed camera/scale; no dGPU requests.
    def measure(label,patch):
        client.call('graphics.set',{'patch':patch});time.sleep(1)
        samples=[]
        for _ in range(24):
            samples.append(client.call('graphics.get',{})['timings']);time.sleep(.12)
        last=samples[-1]
        item={'case':label,'renderer':last['renderer'],'size':last['nativeSize'],'renderScale':last['renderScale'],
              'visibility':last['visibility'],'cpuRenderMs':statistics.median(v['cpuRenderMs'] for v in samples),
              'gpuMs':statistics.median(sum(v['gpuPassMs']) for v in samples if all(x is not None for x in v['gpuPassMs'])),
              'outlineMs':statistics.median(v['gpuEffectsMs']['outline'] for v in samples if v['gpuEffectsMs']['outline'] is not None)}
        report['measurements'].append(item);print('MEASURE',json.dumps(item),flush=True)
        return item
    lens.update(orientation=[0,0,0,1],fov=60,lensShift=[0,0],renderScale=.5)
    client.call('camera.photo',lens)
    client.call('graphics.set',{'patch':{'quality':'low','offload':'off','dynamic_resolution':False}})
    off=measure('lab culling off',{'outline':False,'visibility_culling':False})
    on=measure('lab culling on',{'visibility_culling':True})
    check('hidden_room_not_submitted',on['visibility']['culled']>=384 and off['visibility']['culled']==0)
    ink=measure('lab ink on',{'outline':True})
    check('ink_cost_available',ink['outlineMs']>0)
    lens['position']=[0,-8,4];client.call('camera.photo',lens)
    wait(lambda:s.camera()['timings']['visibility']['culled']==0)
    check('directed_visibility_link',True)
    client.call('camera.release',{})
    for world in ['com.hyprune.lumen-reach','com.hyprune.switchyard']:
        client.call('world.load',{'worldId':world});time.sleep(1)
        pose=client.call('camera.get',{})
        client.call('camera.photo',{'position':pose['position'],'orientation':pose['orientation'],'fov':60,'renderScale':.5})
        atmosphere=client.call('atmosphere.get',{})['profile']
        atmosphere['outline']={'enabled':True,'colour':[.02,.025,.04],'widthPx':2,'depthThreshold':.02,'normalThreshold':.2}
        client.call('atmosphere.set',{'profile':atmosphere})
        measure(world+' baseline',{'outline':False,'visibility_culling':False})
        measure(world+' culling unannotated',{'visibility_culling':True})
        measure(world+' ink',{'outline':True})
        client.call('camera.release',{})
    (s.STATE/'authoring-transcript.json').write_text(json.dumps(client.transcript))
    subprocess.run(['node',str(s.ROOT/'tests/validate-authoring-wire.mjs'),str(s.STATE/'authoring-transcript.json')],check=True)
    check('IPC013_wire_conformance',True)
    # A dropped author connection must restore automatically, without another caller.
    client.call('camera.photo',lens);client.close()
    wait(lambda:abs(s.camera()['position'][1]+8)>1)
    check('disconnect_restores_camera',True)
    (s.ROOT/'docs/evidence/authoring.json').write_text(json.dumps(report,indent=2)+'\n')
