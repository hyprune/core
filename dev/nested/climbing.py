"""Climbing acceptance and gallery recording, inside run.sh's lifetime flock."""
import json, math, pathlib, subprocess, time
import session as s
from m2 import wait, keys

def acceptance(client, children, args, shell_token, socketpath):
    results={}
    def check(name, condition, evidence=None):
        results[name]={'passed':bool(condition),'evidence':evidence}
        (s.STATE/'acceptance.json').write_text(json.dumps(results,indent=2)+'\n')
        assert condition,(name,evidence)
        print('PASS climbing:',name,flush=True)
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.3)
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    for _ in range(4):
        xy=list(map(float,s.ctl('cursorpos').split(',')))
        device.stdin.write(f'{2560-xy[0]} {540-xy[1]}\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.08)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':'org.hyprune.climbing'})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']['movement']
    def pose(x=0,y=1.7,z=.55,pitch=0):
        s.ctl('eval',f'hl.plugin.hyprune.camera({x},{y},{z},0,{pitch})');time.sleep(.12)
    def mode(name):
        s.ctl('eval',f'hl.plugin.hyprune.config({{movement={{climb_mode="{name}"}},player={{move_inertia=0,look_inertia=0}}}})');time.sleep(.5)
    def hold(key,ms): keys('-P',key,'-s',str(ms),'-p',key);time.sleep(.1)
    def attached():return state()['state']=='climbing'
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');time.sleep(.6)
    client.call('movement.set',{'modeId':'com.hyprune.walk'})
    pose()
    client.call('waypoint.set',{'id':'cursor-test','label':'KEPT WAYPOINT','target':{'position':[0,1.7,-2]}})
    def labels(): return [label['text'] for label in s.camera()['overlays']['labels']]
    wait(lambda:any('climb' in label.lower() for label in labels()))
    check('world_ladder_hint_drawn',any('climb' in label.lower() for label in labels()))
    device.stdin.write('button 275\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok'
    wait(lambda:s.camera()['layerCursor'] and not s.camera()['overlays']['interactionHints'])
    check('free_cursor_keeps_waypoint_hides_ladder', 'KEPT WAYPOINT' in labels() and not any('climb' in label.lower() for label in labels()), labels())
    device.stdin.write('button 275\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok'
    wait(lambda:not s.camera()['layerCursor'] and any('climb' in label.lower() for label in labels()))
    check('return_restores_ladder_hint',True)
    client.call('waypoint.clear',{})
    for name in ['grab','look-to-climb','auto-traverse']:
        mode(name);pose(pitch=.9)
        if name=='auto-traverse':keys('-k','e');time.sleep(.8)
        else:hold('w',700)
        m=state();check(name+'_attach',m['state']=='climbing' and m['ladderId']=='hatch',m)
        check(name+'_prompt',bool(m['prompt']),m['prompt'])
        if name!='auto-traverse':
            before=state()['position'];time.sleep(.5);after=state()['position'];check(name+'_hold',abs(after[1]-before[1])<.02,[before,after])
            # Real alternative default bindings: K/J, arrows.
            hold('k',350);check(name+'_K_up',state()['position'][1]>after[1]+.15,state())
            before=state()['position'][1];hold('j',350);check(name+'_J_down',state()['position'][1]<before-.15,state())
            before=state()['position'][1];hold('Control_L',350);check(name+'_slide',state()['position'][1]<before-.4,state())
        keys('-k','space');time.sleep(.2);check(name+'_jump_off',not attached(),state())
        # Drop through the actual hatch. No input is needed to stop the fall.
        pose(y=10,pitch=-.4);wait(attached);caught=state();time.sleep(.4)
        check(name+'_fall_catch',attached() and abs(state()['position'][1]-caught['position'][1])<.03,state())
        keys('-k','space');time.sleep(.2);check(name+'_catch_jump_off',not attached(),state())
        # Start just below the rim: continue up, then stop input during auto exit.
        pose(y=6.8,pitch=1.1)
        if name=='auto-traverse':
            # E chooses the other end from the nearest endpoint.
            pose();keys('-k','e');wait(lambda:not attached() and state()['position'][1]>7.6,12)
        else:
            for _ in range(30):
                hold('Up',180)
                if state()['position'][1]>7.69:break
            check(name+'_rim_reached',state()['position'][1]>7.69,state())
            wait(lambda:not attached() and state()['position'][1]>7.6,8)
        check(name+'_top_exit',state()['position'][2]<0,state())
        if name=='auto-traverse':
            keys('-k','e');wait(attached);wait(lambda:not attached() and state()['position'][1]<1.8,12)
        else:
            pose(y=2.1,pitch=1.1);hold('w',100)
            for _ in range(20):
                hold('Down',180)
                if not attached():break
        check(name+'_bottom_detach',state()['position'][1]<1.8 and state()['position'][2]>.9,state())
        pose(y=4);wait(attached)
        keys('-k','v');time.sleep(.2);check(name+'_noclip_detaches',not attached(),state());hold('w',250);check(name+'_noclip_no_grab',not attached(),state());keys('-k','v')
        pose(y=4);wait(attached);keys('-k','F4');time.sleep(.2);check(name+'_fly_detaches',not attached(),state());hold('w',250);check(name+'_fly_no_grab',not attached(),state());keys('-k','F4')
    mode('grab');pose(x=-5,pitch=.5);hold('w',500);hold('d',650);check('wide_strafe',attached() and state()['position'][0]>-4.2,state())
    pose(x=10,z=3,pitch=.5);hold('w',1300);check('angled_ladder',attached() and state()['position'][2]<2.3,state())
    pose(x=5,pitch=.5);hold('w',1200);check('tall_rope',attached() and state()['ladderId']=='tall',state())
    subprocess.run([str(s.HERE/'ctl.py'),'shot',str(s.STATE/'climbing.png')],check=True)
    if args.record: record(client,children,pose,mode,hold,state)
    client.call('runtime.exit',{})

def record(client, children, pose, mode, hold, state):
    import os, signal, tempfile
    temp=pathlib.Path(tempfile.mkdtemp(prefix='hyprune-climbing-',dir='/tmp'))
    raw=temp/'raw.mkv';log=(temp/'recorder.log').open('w')
    recorder=s.envrun('wf-recorder','-o','HEADLESS-1','-r','30','-f',str(raw),stdout=log,stderr=subprocess.STDOUT);children.append(recorder)
    time.sleep(1)
    chapters=[];start=time.monotonic()
    def pause(seconds):time.sleep(max(0,seconds))
    try:
        for name,title in [('grab','GRAB | W/S up/down - release to hold'),('look-to-climb','LOOK-TO-CLIMB | Look up/down + forward'),('auto-traverse','AUTO-TRAVERSE | E takes you to the other exit')]:
            began=time.monotonic();chapters.append((began-start,title))
            mode(name);pose(x=2,y=3,z=9,pitch=.1);pause(2)
            if name=='auto-traverse':
                pose(x=5,pitch=1.1);keys('-k','e');pause(6.5)
                keys('-k','e');pause(6.5)
                pose(y=10,pitch=-.65);pause(2.5);keys('-k','e');pause(4.5)
                pose(x=10,z=3,pitch=.85);keys('-k','e');pause(4.5)
                hold('space',100);pause(.5)
            else:
                pose(x=5,pitch=1.1);hold('w',2000);pause(1.4)
                if name=='look-to-climb':
                    # Reset the view for the downward-pitch comparison; fall catch reattaches.
                    p=state()['position'];pose(x=p[0],y=p[1],z=p[2],pitch=-.85)
                    hold('w',700)
                else:hold('s',700)
                hold('space',100);pause(.7)
                pose(y=6.8,pitch=1.1);hold('w',1100);pause(1.5)
                pose(y=10,pitch=-.65);pause(2.5);hold('space',100);pause(.6)
                pose(x=-5,pitch=.95);hold('w',1100);hold('d',800);hold('Control_L',400);hold('space',100)
                pose(x=10,z=3,pitch=.85);hold('w',1800);pause(.7)
            pose(x=2,y=9,z=9,pitch=-.3)
            # Exactly 36 seconds for each labelled mechanic, including resets.
            pause(36-(time.monotonic()-began))
    finally:
        if recorder.poll() is None:recorder.send_signal(signal.SIGINT)
        recorder.wait(timeout=15);log.close()
    duration=float(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration','-of','default=nw=1:nk=1',str(raw)],text=True))
    filters=[]
    for index,(when,title) in enumerate(chapters):
        label=temp/f'label-{index}.txt';label.write_text('HYPRUNE CLIMBING LAB\n'+title+'\nNested session - original CC0 test world')
        end=chapters[index+1][0] if index+1<len(chapters) else duration
        filters.append(f"drawtext=fontfile=/usr/share/fonts/noto/NotoSans-Regular.ttf:textfile={label}:fontsize=28:fontcolor=white:box=1:boxcolor=black@0.7:boxborderw=16:x=40:y=40:enable='between(t,{when:.3f},{end:.3f})'")
    encoded=temp/'hyprune-climbing.mp4'
    subprocess.run(['ffmpeg','-y','-i',str(raw),'-vf',','.join(filters),'-r','30','-c:v','libx264','-preset','fast','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart','-an',str(encoded)],check=True,stdout=subprocess.DEVNULL,stderr=(temp/'encode.log').open('w'))
    meta={'title':'Hyprune climbing: three mechanics','description':'Grab, look-to-climb and accessible E auto-traverse. 36 seconds each. Hatch fall catch, tall rope, wide chain-link wall and angled vines in an original CC0 procedural world. Verified isolated nested session only.','duration':f'{duration:.2f}','chapters':' | '.join(f'{when:.1f}s {title}' for when,title in chapters),'source':'hyprune/core feature/climbing','license':'CC0-1.0 fixture; MIT source','date':time.strftime('%Y-%m-%d')}
    sidecar=temp/'hyprune-climbing.json';sidecar.write_text(json.dumps(meta,indent=2)+'\n')
    gallery=s.video_dir()
    # Encode entirely in /tmp, then atomically publish the finished artifacts.
    import shutil
    for source in [encoded,sidecar]:
        staging=gallery/('.'+source.name+'.part')
        shutil.move(str(source),staging);os.replace(staging,gallery/source.name)
    print('VIDEO:',gallery/encoded.name,flush=True)
