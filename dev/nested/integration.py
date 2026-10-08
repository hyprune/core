#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real-core/real-shell acceptance and showcase, always inside run.sh's flock."""
import json, os, pathlib, signal, subprocess, time
from session import HERE, ROOT, STATE, ctl, envrun, camera
from ctl import verified

def keys(*args): subprocess.run([str(HERE/'ctl.py'),'env','wtype',*args],check=True)
def map_click_research():
    # The deterministic 1920x1080 Director fits Switchyard's 900x1000 SVG
    # in a 585x650 rectangle at (503,282); use the package affine coordinates.
    output=next(m for m in json.loads(ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    target=(output['x']+763,output['y']+470)
    for _ in range(4):
        current=[float(v.strip()) for v in ctl('cursorpos').split(',')]
        dx,dy=target[0]-current[0],target[1]-current[1]
        if abs(dx)+abs(dy)<3:break
        subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer'),str(dx),str(dy)],check=True)
        time.sleep(.06)
    subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer')],check=True)

def shot(name): subprocess.run([str(HERE/'ctl.py'),'shot',str(STATE/name)],check=True)
def wait_for(check, timeout=10):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        result=check()
        if result:return result
        time.sleep(.1)
    raise AssertionError('condition timed out')

def first_session(client,children,args,shell_token,socketpath):
    world_output='WAYLAND-1' if (args.serve or getattr(args,'visible',False)) and not args.record else 'HEADLESS-1'
    ctl('eval',f'hl.dispatch(hl.dsp.focus({{ monitor = "{world_output}" }}))')
    ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
    client.call('world.load',{'worldId':'com.hyprune.switchyard'})
    subscription=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':subscription})['snapshot']['state']
    snapshot=state();assert snapshot['spatial']['map']['svg'].startswith('<svg')
    assert len(snapshot['spatial']['world']['places'])==7
    # Live root-surface applications: nested environment only.
    children.append(envrun('kitty','--title','Switchyard / Build terminal','--override','font_size=18','--override','initial_window_width=1200','--override','initial_window_height=675','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    (STATE/'btop.conf').write_text('shown_boxes = "cpu mem proc"\nupdate_ms = 1000\n')
    children.append(envrun('kitty','--title','Switchyard / Systems btop','--override','font_size=12','--override','initial_window_width=1200','--override','initial_window_height=675','btop','--force-utf','--config',str(STATE/'btop.conf'),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    # Chromium uses a private profile and an original local static page; no network.
    page=STATE/'welcome.html';page.write_text('<!doctype html><meta charset="utf-8"><style>body{background:#14232b;color:#e7dfca;font:24px sans-serif;padding:50px}h1{color:#dcac61}p{line-height:1.7}</style><h1>SWITCHYARD</h1><p>A place for your projects.<br>Build · Research · Commons<br>Studio · Systems · Archive</p><hr><p>Hyprune / first session<br>Live applications, within a world.</p>')
    if pathlib.Path('/usr/bin/chromium').exists():children.append(envrun('chromium','--ozone-platform=wayland','--no-first-run','--disable-gpu','--disable-dev-shm-usage','--disable-background-networking','--user-data-dir='+str(STATE/'browser-profile'),'--app=file://'+str(page),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    time.sleep(2)
    for window in json.loads(ctl('clients','-j')):
        address=window['address']
        ctl('eval',f'hl.dispatch(hl.dsp.window.float({{ window = "address:{address}", action = "enable" }}))')
        ctl('eval',f'hl.dispatch(hl.dsp.window.resize({{ window = "address:{address}", x = 1200, y = 675 }}))')
    ctl('eval',f'hl.plugin.hyprune.toggle("{world_output}")')
    wait_for(lambda:state()['runtime']['active'])
    # Credential/namespace impersonation is rejected even for our own direct child.
    output=state()['runtime']['outputs'][0]
    lease=client.call('shell.claim',{'outputs':[output]})['leaseId']
    try: client.call('shell.bind',{'leaseId':lease,'pid':children[0].pid})
    except RuntimeError as error: assert error.args[0]['code']==-32002
    else: raise AssertionError('ordinary window impersonated a shell HUD')
    try: client.call('shell.overlay',{'leaseId':lease,'outputId':output,'visible':True,'exclusiveInput':True})
    except RuntimeError as error: assert error.args[0]['code']==-32007
    else: raise AssertionError('unassociated exclusive overlay accepted')
    try: client.call('camera.get',{'_pid':os.getpid()})
    except RuntimeError as error: assert error.args[0]['code']==-32602
    else: raise AssertionError('public credential spoof accepted')
    client.call('shell.release',{'leaseId':lease})
    token_r,token_w=os.pipe();os.write(token_w,(shell_token+'\n').encode());os.close(token_w)
    _,environment=verified();environment.update(HYPRUNE_TOKEN_FD=str(token_r),HYPRUNE_PROTOCOL='0.3',HYPRUNE_SOCKET=str(socketpath),HYPRUNE_OUTPUT_SCREEN=world_output,HYPRUNE_DEV_AUTOMATION='1')
    log=open(STATE/'shell.log','w')
    shell=subprocess.Popen(['python3',str(ROOT.parent/'shell/bridge/launch.py'),'--protocol','0.3'],env=environment,pass_fds=(token_r,),stdout=log,stderr=subprocess.STDOUT)
    os.close(token_r);children.append(shell)
    def shell_pid():
        if shell.poll() is not None:raise AssertionError('shell exited; see shell.log')
        ids=pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
        return next((int(p) for p in ids if pathlib.Path(f'/proc/{p}/comm').read_text().strip()=='qs'),None)
    pid=wait_for(shell_pid)
    def ui(command,*params):
        verified() # every qs command targets the verified child PID explicitly
        assert pid in [int(p) for p in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()]
        return subprocess.check_output([str(HERE/'ctl.py'),'env','qs','ipc','--pid',str(pid),'call','preview',command,*map(str,params)],text=True).strip()
    def status():
        try:return json.loads(ui('status'))
        except (subprocess.CalledProcessError,json.JSONDecodeError):return {}
    try: wait_for(lambda:status().get('lease'))
    except AssertionError:
        print('shell failed:',status(),flush=True);raise
    print('shell ready:',status(),flush=True)
    wait_for(lambda:not status().get('warp'))
    wait_for(lambda:'hyprune-hud' in json.dumps(json.loads(ctl('layers','-j'))[world_output]))
    shot('session-arrival.png')
    assert status()['screen']==world_output and status()['map']
    assert any('btop' in s['title'] for s in state()['surfaces']), 'btop terminal failed to map'
    assert len(state()['surfaces']) >= (3 if pathlib.Path('/usr/bin/chromium').exists() else 2), 'live terminal/btop/browser missing'
    layers=json.loads(ctl('layers','-j'))
    (STATE/'layers.json').write_text(json.dumps(layers,indent=2))
    # Check actual rendered layers, not just shell state.
    assert 'hyprune-hud' in json.dumps(layers[world_output])
    for output,data in layers.items():
        if output!=world_output:assert 'hyprune-hud' not in json.dumps(data)
    before=state()['movement']['position'];keys('-P','w','-s','600','-p','w');after=state()['movement']['position'];assert after[2]<before[2]-.5
    keys('-P','Tab','-s','1150','-p','Tab')
    wait_for(lambda:status().get('director'))
    assert any(o['exclusiveInput'] for l in state()['shell']['leases'] for o in l['overlays'])
    shot('session-director.png')
    # Real Director Tab navigation, then Map intent over shell's private channel.
    keys('-k','Tab');assert status()['director']
    research_index=next(i for i,p in enumerate(status()['places']) if p['id']=='research')
    ui('select',research_index);wait_for(lambda:state()['location']['waypoint'] is not None)
    assert state()['location']['waypoint']['id']=='research'
    keys('-k','Escape');wait_for(lambda:not status().get('director'))
    assert not any(o['exclusiveInput'] for l in state()['shell']['leases'] for o in l['overlays'])
    # Autodrive on the clear concourse, initiated by the real shell.
    client.call('camera.set',{'position':[-4,1.7,7],'orientation':[0,0,0,1]})
    ui('drive',0);wait_for(lambda:state()['location']['autodrive']);wait_for(lambda:not state()['location']['autodrive'])
    assert abs(state()['movement']['position'][2]-4.5)<.1
    # Travel requested by Map and authoritative passive warp.
    keys('-P','Tab','-s','1150','-p','Tab');wait_for(lambda:status().get('director'))
    ui('travel',research_index);wait_for(lambda:status().get('warp'));shot('session-warp.png')
    wait_for(lambda:state()['location']['transition']['phase']=='arrived',10)
    wait_for(lambda:not status().get('warp'),5)
    # Actual station-anchor placement and real click/typing into the live shell.
    work=next(s for s in state()['surfaces'] if 'Build terminal' in s['title'])
    capture=next(s for s in camera()['captures'] if s['id']==work['id']);pos=capture['position']
    assert any(abs(pos[0]-x)<.01 for x in [-10.7,1.3,13.3])
    client.call("camera.set", {"position": [pos[0], 2.25, pos[2]+4], "orientation": [0,0,0,1]})
    subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer')],check=True)
    wait_for(lambda:next(s for s in state()['surfaces'] if s['id']==work['id'])['focused'])
    # A plain click is pointer-only since M3B. E explicitly enters use mode.
    keys('-k','e');wait_for(lambda:camera()['mode']=='typing')
    marker=STATE/'integration-typed.txt';marker.unlink(missing_ok=True)
    keys(f"printf 'typed in Switchyard\\n' > {marker}");keys('-k','Return')
    wait_for(lambda:marker.exists() and marker.read_text()=='typed in Switchyard\n')
    keys('-M','logo','-P','Super_L','-k','Tab','-p','Super_L','-m','logo');wait_for(lambda:camera()['mode']!='typing')
    ui('workspace',11);wait_for(lambda:state()['world']['id']=='org.hyprune.quiet',20)
    wait_for(lambda:not status().get('warp'),5)
    # A compositor workspace switch (not an IPC world.load) takes us home.
    ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
    wait_for(lambda:state()['world']['id']=='com.hyprune.switchyard',25)
    wait_for(lambda:not status().get('warp'),5)
    # A newer workspace wins even while the previous package is decoding.
    ui('workspace',11)
    ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
    wait_for(lambda:state()['workspaces']['currentId']==10 and state()['world']['id']=='com.hyprune.switchyard' and state()['location']['transition']['phase']!='loading',25)
    if args.record:showcase(client,state,ui,status,pos)
    if args.serve:
        print('Interactive first session ready; ctl.py verifies every nested command. Ctrl+C exits.',flush=True)
        while True:time.sleep(.5)
    client.call('runtime.exit',{});wait_for(lambda:not camera()['active']);wait_for(lambda:not status().get('director'))
    assert shell.poll() is None
    errors=('ReferenceError:', 'TypeError:', 'SyntaxError:', 'Segmentation fault')
    assert not any(error in (STATE/'shell.log').read_text() for error in errors), 'shell runtime error; see shell.log'
    print('PASS first session: Switchyard load/map, HUD output isolation, walk, real hold/Director exclusive association and Tab/Escape, waypoint, autodrive, travel/warp, anchored live apps/click/typing, IPC and compositor workspace warps, exit; no crash.',flush=True)
    (STATE/'integration-result.json').write_text(json.dumps({'passed':True,'shell':status(),'frames':camera()['frames'],'typed':marker.read_text()},indent=2))
    log.close()

def showcase(client,state,ui,status,pos):
    import math
    gallery=pathlib.Path.home()/'workspace/rick-the-alien/worldshell-gallery/videos';gallery.mkdir(parents=True,exist_ok=True)
    output=gallery/'hyprune-first-session.mp4';raw=STATE/'first-session.mkv'
    def pause(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:state();time.sleep(.08)
    def pose(position,yaw=0,pitch=0):
        sy=math.sin(-yaw/2);cy=math.cos(-yaw/2);sp=math.sin(pitch/2);cp=math.cos(pitch/2)
        client.call('camera.set',{'position':position,'orientation':[cy*sp,sy*cp,-sy*sp,cy*cp]})
    def glide(start,end,seconds,yaw_start=0,yaw_end=0,pitch=0):
        begun=time.monotonic()
        while True:
            progress=min(1,(time.monotonic()-begun)/seconds);e=progress*progress*(3-2*progress)
            pose([a+(b-a)*e for a,b in zip(start,end)],yaw_start+(yaw_end-yaw_start)*e,pitch)
            if progress>=1:break
            time.sleep(.028)
    research=next(i for i,p in enumerate(status()['places']) if p['id']=='research')
    # Prepare the docked terminal before capture; it stays a live interactive app.
    windows=state()['surfaces'];work=next(s for s in windows if 'Build terminal' in s['title'])
    live=camera()['captures'];workpos=next(s['position'] for s in live if s['id']==work['id'])
    pose([workpos[0],2.25,workpos[2]+4]);client.call('surface.focus',{'surfaceId':work['id'],'intent':'keyboard'})
    keys('clear');keys('-k','Return');keys("printf '\\033[1;36mHYPRUNE / SWITCHYARD\\033[0m\\n\\nA live terminal, docked in a world.\\n\\n'");keys('-k','Return');keys('-k','Escape')
    pose([0,1.7,10]);pause(1.5)
    log=open(STATE/'first-session-recorder.log','w')
    recorder=envrun('wf-recorder','-o','HEADLESS-1','-r','30','-D','--no-dmabuf','-c','libx264','-p','preset=ultrafast','-p','crf=19','-x','yuv420p','-f',str(raw),stdout=log,stderr=subprocess.STDOUT)
    started=time.monotonic()
    try:
        # Arrival and a real walk along the concourse. No mock/backdrop renderer.
        client.call('world.load',{'worldId':'com.hyprune.switchyard'});pause(3)
        glide([0,1.7,10],[-4,1.7,10],2.2);keys('-P','w','-s','1000','-p','w');pause(1)
        # Inspect the architecture on the way to the working docks.
        glide([-4,1.7,6.5],[-9,2.1,-1.6],3,0,-.32);pause(1)
        live=camera()['captures'];workpos=next(s['position'] for s in live if 'Build terminal' in s['title'])
        glide([-9,2.1,-1.6],[workpos[0],2.25,workpos[2]+4.1],2,-.32,0)
        subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer')],check=True)
        keys('-d','40',"printf '\\nHello from inside Hyprune.\\nThis terminal receives real keyboard input.\\n'");keys('-k','Return');pause(2.2);keys('-k','Escape')
        btop=next((s['position'] for s in live if 'btop' in s['title']),None)
        if btop:
            glide([workpos[0],2.25,workpos[2]+4.1],[btop[0],2.25,btop[2]+4.1],2);pause(2)
        browser=next((s['position'] for s in live if 'chromium' in s['title'].lower() or 'welcome' in s['title'].lower()),None)
        if browser:
            glide([btop[0],2.25,btop[2]+4.1] if btop else [workpos[0],2.25,workpos[2]+4.1],[browser[0],2.25,browser[2]+4.1],1.8);pause(1.5)
        # Core-owned hold opens the real shell. Set the waypoint from its Map.
        pose([-4,1.7,4.5]);keys('-P','Tab','-s','1150','-p','Tab');wait_for(lambda:status().get('director'))
        ui('tab',0);pause(2);map_click_research();wait_for(lambda:state()['location']['waypoint'] and state()['location']['waypoint']['id']=='research');pause(2.5);shot('showcase-map.png')
        ui('travel',research);wait_for(lambda:status().get('warp'));pause(3);shot('showcase-travel.png')
        wait_for(lambda:state()['location']['transition']['phase']=='arrived',10);pause(2)
        # Workspace location change, then the genuine compositor switch home.
        keys('-P','Tab','-s','1150','-p','Tab');wait_for(lambda:status().get('director'))
        ui('tab',3);pause(2.5);ui('workspace',11)
        wait_for(lambda:state()['world']['id']=='org.hyprune.quiet',20);pause(2.5)
        glide([0,1.7,4],[0,1.7,4],2,0,.32);pause(1)
        ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
        wait_for(lambda:state()['world']['id']=='com.hyprune.switchyard',25);pause(2.5)
        glide([0,1.7,10],[-3,1.9,8.5],2,0,-.18)
        pause(max(1,61-(time.monotonic()-started)))
        shot('showcase-return.png')
    finally:
        recorder.send_signal(signal.SIGINT);recorder.wait(timeout=15);log.close()
    subprocess.run(['ffmpeg','-y','-loglevel','error','-i',str(raw),'-an','-vf','scale=1920:1080:flags=lanczos:in_range=pc:out_range=tv','-r','30','-c:v','libx264','-preset','medium','-crf','22','-maxrate','6500k','-bufsize','13000k','-pix_fmt','yuv420p','-color_range','tv','-movflags','+faststart',str(output)],check=True)
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(output)]));stream=probe['streams'][0]
    assert stream['codec_name']=='h264' and stream['pix_fmt']=='yuv420p' and stream['width']==1920 and stream['height']==1080
    assert 45<=float(probe['format']['duration'])<=75 and output.stat().st_size<60_000_000,probe['format']
    notes='Real Hyprland 0.56.2 nested session, real Hyprune core and Quickshell shell over authenticated IPC 0.3. Switchyard package by Hyprune contributors (CC-BY-4.0), copied unchanged from worlds/dist; original CC0 quiet-observatory test fixture. Arrival, real walking, station-anchored live terminals/btop/browser, terminal typing, core-owned Tab hold, verified Wayland-exclusive Director, package Map waypoint/travel, and workspace-triggered warp out and back. No mock or owner-session compositor control.'
    output.with_suffix('.json').write_text(json.dumps({'title':'Hyprune: first session','tag':'integration','notes':notes},indent=2)+'\n')
    (STATE/'first-session-probe.json').write_text(json.dumps(probe,indent=2))
    print('video:',output,probe['format']['duration'],output.stat().st_size,'bytes',flush=True)
