"""Fixed-scale native-4K render profile and idle cost (docs/PERF-2026-10.md).
Called only under run.sh's lifetime flock, against the nested compositor.
Same scene for every run: a frozen Lumen Reach copy (HYPRUNE_PERF_WORLD), Low/iGPU,
offload off, fixed internal scale, the four authored spawn poses, one static
terminal in the room and one terminal on a mounted workspace wall.
"""
import json,math,os,pathlib,statistics,subprocess,time
import session as s
from m2 import wait,pose

LUMEN='com.hyprune.lumen-reach'
POSES=[('arrival',[-8,1.7,12],0,0),('array-hall',[8,1.7,-34],0,0),('rime-vault',[38,-4.3,-79],0,0),('signal-rise',[54,1.7,-125],0,0)]

def gpu_cycles(pid):
    """xe/i915 per-client engine cycles of the nested compositor (render engine)."""
    busy=total=0;seen=set()
    for fd in pathlib.Path(f'/proc/{pid}/fdinfo').iterdir():
        try:text=fd.read_text()
        except OSError:continue
        if 'drm-driver' not in text:continue
        fields=dict(line.split(':\t',1) for line in text.splitlines() if ':\t' in line)
        client=fields.get('drm-client-id')
        if client in seen or 'drm-cycles-rcs' not in fields:continue
        seen.add(client);busy+=int(fields['drm-cycles-rcs'].split()[0]);total=int(fields['drm-total-cycles-rcs'].split()[0])
    return busy,total

def gt_idle_ms():
    try:return int(pathlib.Path('/sys/class/drm/card0/device/tile0/gt0/gtidle/idle_residency_ms').read_text())
    except OSError:return None

def acceptance(client,children,args,shell_token,socketpath):
    label=os.environ.get('HYPRUNE_PERF_LABEL','run')
    scales=[float(v) for v in os.environ.get('HYPRUNE_PERF_SCALES','0.5,0.375').split(',')]
    out=pathlib.Path(os.environ.get('HYPRUNE_PERF_OUT','/tmp/hyprune-perf/results'))/label;out.mkdir(parents=True,exist_ok=True)
    pid=json.loads((s.STATE/'session.json').read_text())['pid']
    report={'label':label,'nestedOnly':True,'world':LUMEN,'scales':scales,'poses':[],'idle':{},'resume':{}}
    m=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    assert (m['width'],m['height'])==(3840,2160),'perf needs HYPRUNE_NESTED_4K=1'
    xy=list(map(float,s.ctl('cursorpos').split(',')))
    subprocess.run([str(s.HERE/'ctl.py'),'env',str(s.ROOT/'build/nested-pointer'),str(m['x']+m['width']/2-xy[0]),str(m['y']+m['height']/2-xy[1])],check=True)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    # Static terminals: no cursor blink, so the only client commits are explicit.
    static=['--override','cursor_blink_interval=0','--override','font_size=14','--override','remember_window_size=no','--override','initial_window_width=1280','--override','initial_window_height=720']
    s.ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
    flag=out/'video.go';flag.unlink(missing_ok=True)
    wallflag=out/'wall.go';wallflag.unlink(missing_ok=True)
    terminals={'Perf wall':f'echo mounted wall; while [ ! -e {wallflag} ]; do sleep .2; done; while :; do date +%s.%N; sleep 0.033; done','Perf-room':'echo placed terminal; exec sleep 3600',
               # Static until the flag appears, then ~30 Hz commits like a playing video.
               'Perf-video':f'echo waiting; while [ ! -e {flag} ]; do sleep .2; done; while :; do date +%s.%N; sleep 0.033; done'}
    for title,command in terminals.items():
        children.append(s.envrun('kitty','--class','hyprune-perf','--title',title,*static,'sh','-c',command,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
        time.sleep(.6)
    timings=lambda:client.call('graphics.get',{})['timings']
    variants=[('A',LUMEN)]+([('B',LUMEN+'.b')] if os.environ.get('HYPRUNE_PERF_WORLD_B') else [])
    def enter(world):
        """Same scene in every variant: wall binding, both mounted terminals, spawn pose."""
        client.call('world.load',{'worldId':world})
        client.call('workspace.bind',{'id':10,'worldId':world,'areaId':'sun-court'})
        if not s.camera()['active']:
            s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
            wait(lambda:s.camera()['active'],timeout=20)
        wait(lambda:not timings()['upload']['pending'],timeout=60)
        for title,anchor in [('Perf-room','sun-court-slot-01-field-rig-02'),('Perf-video','sun-court-slot-01-field-rig-01')]:
            surface=wait(lambda:next((v for v in s.camera()['captures'] if v['title']==title),None),timeout=20)
            client.call('surface.place',{'surfaceId':surface['id'],'anchorId':anchor})
        pose(client,POSES[0][1],POSES[0][2],POSES[0][3])
        try:wait(lambda:sum(v['title'] in ('Perf-room','Perf-video') and v['visible'] for v in s.camera()['captures'])==2,timeout=20)
        except AssertionError:print('captures',json.dumps(s.camera()['captures']),flush=True);raise
        time.sleep(1.5)
        report.setdefault('uploads',{})[world]=timings()['upload']
    client.call('graphics.set',{'patch':{'quality':'low','offload':'off','dynamic_resolution':False,'render_scale':scales[0],'sharpen':.35}})
    enter(LUMEN)
    report['device']=timings()['renderer'];report['upload']=timings()['upload']
    report['plugin']=os.environ.get('HYPRUNE_NESTED_PLUGIN','build/hyprune-core.so')
    # 1. Fixed-scale GPU pass costs per pose. Camera moves a hair each sample so
    #    every sample is a newly rendered frame regardless of idle skipping.
    #    Variants alternate per round so GPU clock/thermal drift hits both.
    rounds=int(os.environ.get('HYPRUNE_PERF_ROUNDS','1'))
    idle_only=os.environ.get('HYPRUNE_PERF_IDLE_ONLY')=='1'   # bisecting idle cost: skip poses, three idle windows
    for round in range(0 if idle_only else rounds):
        for variant,world in variants:
            if len(variants)>1:enter(world)
            for scale in scales:
                client.call('graphics.set',{'patch':{'render_scale':scale}});time.sleep(.5)
                for name,position,yaw,pitch in POSES:
                    pose(client,position,yaw,pitch);time.sleep(1.2)
                    samples=[];first=timings();t0=time.monotonic()
                    for n in range(30):
                        pose(client,position,yaw+(n%2)*1e-4,pitch);time.sleep(.1);samples.append(timings())
                    elapsed=time.monotonic()-t0;last=samples[-1]
                    gpu={k:statistics.median(v['gpuEffectsMs'][k] for v in samples if v['gpuEffectsMs'][k] is not None) for k in last['gpuEffectsMs'] if any(v['gpuEffectsMs'][k] is not None for v in samples)}
                    item={'variant':variant,'round':round,'pose':name,'scale':scale,'internal':last['internalSize'],'gpuMs':gpu,'gpuTotalMs':statistics.median(sum(x for x in v['gpuPassMs'] if x) for v in samples),
                          'cpuRenderMs':statistics.median(v['cpuRenderMs'] for v in samples),'cpuUpdateMs':statistics.median(v['cpuUpdateMs'] for v in samples),
                          'fps':(last['renderedFrames']-first['renderedFrames'])/elapsed,'reused':last['reusedFrames']-first['reusedFrames'],'visibility':last['visibility']}
                    report['poses'].append(item);print('MEASURE pose',json.dumps(item),flush=True)
                    if round==0:
                        subprocess.run([str(s.HERE/'ctl.py'),'shot',str(out/f'{variant}-{name}-{scale}.png')],check=True)
    if len(variants)>1:enter(LUMEN)
    if os.environ.get('HYPRUNE_PERF_POSES_ONLY')=='1':
        (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('PERF report',out/'report.json',flush=True);return
    # 2. Idle: static camera and static clients, no input. Before render-on-demand
    #    every 16 ms tick damaged the output; afterwards no new world frames.
    client.call('graphics.set',{'patch':{'render_scale':scales[0]}})
    pose(client,POSES[0][1],POSES[0][2],POSES[0][3]);time.sleep(2)
    def window(seconds):
        a=timings();c0=gpu_cycles(pid);g0=gt_idle_ms();t0=time.monotonic();time.sleep(seconds)
        b=timings();c1=gpu_cycles(pid);g1=gt_idle_ms();dt=time.monotonic()-t0
        return {'seconds':dt,'renderedPerS':(b['renderedFrames']-a['renderedFrames'])/dt,'reused':b['reusedFrames']-a['reusedFrames'],
                'idlePresented':b.get('idleFrames',0)-a.get('idleFrames',0),
                'nestedGpuBusyPct':100*(c1[0]-c0[0])/max(1,c1[1]-c0[1]),
                'gt0IdlePct':None if g0 is None else 100*(g1-g0)/(dt*1000)}
    report['idle']['static']=window(8);print('MEASURE idle',json.dumps(report['idle']['static']),flush=True)
    if idle_only:
        report['idle']['repeats']=[window(8) for _ in range(2)]
        for w in report['idle']['repeats']:print('MEASURE idle',json.dumps(w),flush=True)
        (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('PERF report',out/'report.json',flush=True);return
    # 3. A continuously updating room client (~30 Hz commits) must keep updating.
    flag.touch();time.sleep(1.5)
    report['idle']['updatingClient']=window(5);print('MEASURE updating',json.dumps(report['idle']['updatingClient']),flush=True)
    flag.unlink();time.sleep(2)
    # 4. Resume: first new frame latency after idle, then a 2 s 60 Hz camera glide.
    before=timings()['renderedFrames'];t0=time.monotonic()
    pose(client,[-8,1.7,11.9],0,0)
    wait(lambda:timings()['renderedFrames']>before,timeout=5)
    report['resume']['firstFrameMs']=(time.monotonic()-t0)*1000
    a=timings();t0=time.monotonic();n=0
    while time.monotonic()-t0<2:
        pose(client,[-8,1.7,11.9-n*.02],n*.004,0);n+=1;time.sleep(1/60)
    b=timings();dt=time.monotonic()-t0
    report['resume'].update({'glideRenderedPerS':(b['renderedFrames']-a['renderedFrames'])/dt,'glideReused':b['reusedFrames']-a['reusedFrames'],'cameraUpdatesPerS':n/dt})
    print('MEASURE resume',json.dumps(report['resume']),flush=True)
    # 5. Video wall: the Sun Court workspace wall (composited client) updates at ~30 Hz.
    pose(client,POSES[0][1],POSES[0][2],POSES[0][3]);time.sleep(1.5)
    wallflag.touch();time.sleep(1.5)
    report['idle']['videoWall']=window(5)
    # The composed wall must keep updating: two stills a second apart differ where the clock prints.
    for name in ['wall-a','wall-b']:
        subprocess.run([str(s.HERE/'ctl.py'),'shot',str(out/f'{name}.png')],check=True);time.sleep(1)
    from PIL import Image, ImageChops
    diff=ImageChops.difference(Image.open(out/'wall-a.png').convert('L'),Image.open(out/'wall-b.png').convert('L')).point(lambda v:255 if v>24 else 0)
    report['idle']['videoWall']['changedPixelPct']=100*sum(diff.histogram()[255:])/(diff.width*diff.height)
    print('MEASURE videoWall',json.dumps(report['idle']['videoWall']),flush=True)
    wallflag.unlink();time.sleep(2)
    # 6. Walking: W held for 6 s from the arrival spawn (renders every tick).
    from m2 import keys
    client.call('movement.set',{'modeId':'com.hyprune.walk'});pose(client,POSES[0][1],POSES[0][2],POSES[0][3]);time.sleep(1)
    a=timings();c0=gpu_cycles(pid);t0=time.monotonic()
    keys('-P','w','-s','6000','-p','w')
    b=timings();c1=gpu_cycles(pid);dt=time.monotonic()-t0
    report['walking']={'seconds':dt,'renderedPerS':(b['renderedFrames']-a['renderedFrames'])/dt,'reused':b['reusedFrames']-a['reusedFrames'],
        'nestedGpuBusyPct':100*(c1[0]-c0[0])/max(1,c1[1]-c0[1]),'renderScale':b['renderScale'],'gpuMs':b['gpuEffectsMs'],'cpuRenderMs':b['cpuRenderMs']}
    print('MEASURE walking',json.dumps(report['walking']),flush=True)
    # 7. Load: reload the active world; time to upload done and to the first new frame.
    prev=timings();before=prev['renderedFrames'];t0=time.monotonic()
    client.call('world.load',{'worldId':LUMEN})
    wait(lambda:(u:=timings()['upload'])['pending'] or u!=prev['upload'],timeout=10)   # a fast upload may finish before the first poll
    wait(lambda:not timings()['upload']['pending'],timeout=60);uploaded=time.monotonic()
    wait(lambda:(pose(client,POSES[0][1],POSES[0][2]+1e-4,POSES[0][3]) or True) and timings()['renderedFrames']>before,timeout=20)
    u=timings()['upload']
    report['load']={'uploadDoneMs':(uploaded-t0)*1000,'firstFrameMs':(time.monotonic()-t0)*1000,'gpuMiB':u.get('gpuBytes',0)/2**20,
        'uploadMiB':u['bytes']/2**20,'steps':u['steps'],'uploadCpuMs':u['cpuMs'],'maxStepMs':u['maxStepMs'],'maxJobMs':u['maxJobMs'],'slowestJob':u['slowestJob']}
    print('MEASURE load',json.dumps(report['load']),flush=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PERF report',out/'report.json',flush=True)
