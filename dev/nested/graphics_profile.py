"""Focused native-4K profile, called only under live_check's verified nested lock.
Keeps the full interaction acceptance separate so its geometry fixtures do not
silently define the performance scene. Uses the same live launcher and teardown.
"""
import json,subprocess,time,statistics
import session as s
from ctl import verified

def acceptance(logs,recorded,children,check):
    _,env=verified()
    def wait(fn):
        end=time.monotonic()+15
        while time.monotonic()<end:
            value=fn()
            if value:return value
            time.sleep(.1)
        raise AssertionError('graphics profile timed out')
    output=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    check('native_4K_output',(output['width'],output['height'])==(3840,2160))
    s.ctl('eval','hl.unbind("SUPER + RETURN")')
    s.ctl('eval','hl.bind("SUPER + Return",hl.dsp.exec_cmd("kitty --start-as normal --class hyprune-live-regression --title Live-launch-regression"))')
    subprocess.run(['wtype','-M','logo','-P','Super_L','-k','Return','-p','Super_L','-m','logo'],env=env,check=True)
    wait(lambda:any(v['title']=='Live-launch-regression' and v['visible'] for v in s.camera()['captures']))
    time.sleep(6)
    first=s.camera()['timings'];start=time.monotonic();samples=[]
    for n in range(100):
        time.sleep(.2);samples.append(s.camera()['timings'])
    elapsed=time.monotonic()-start;last=samples[-1]
    fps=(last['renderedFrames']-first['renderedFrames'])/elapsed
    medians={key:statistics.median(v[key] for v in samples) for key in ['cpuUpdateMs','cpuRenderMs','captureMs','ipcMs']}
    gpu={key:statistics.median(v['gpuEffectsMs'][key] for v in samples if v['gpuEffectsMs'][key] is not None) for key in last['gpuEffectsMs']}
    report={'world':'Lumen Reach','resolution':[3840,2160],'renderer':last['renderer'],
        'elapsedSeconds':elapsed,'deliveredFPS':fps,'reusedFrames':last['reusedFrames']-first['reusedFrames'],
        'cpuMedianMs':medians,'gpuMedianMs':gpu,'settings':last['graphics'],'samples':samples}
    (logs/'graphics-sustained.json').write_text(json.dumps(report,indent=2)+'\n')
    print('MEASURE',json.dumps({k:v for k,v in report.items() if k!='samples'}),flush=True)
    check('native_surfaces_scaled_world',all(v['nativeSize']==[3840,2160] and .375<=v['renderScale']<=1 for v in samples))
    check('bounded_GPU_queue',all(v['inFlight']<=2 for v in samples))
    check('incremental_world_upload',last['upload']['steps']>1 and not last['upload']['pending'])
    # No synthetic FPS pass: the measured result is preserved even below target.
    check('actual_new_frames_measured',fps>0)
