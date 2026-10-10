#!/usr/bin/env python3
"""Review regression cases. Invoke only via flock /tmp/worldshell-nested.lock."""
import argparse,json,os,pathlib,shlex,subprocess,time
import session as s
import m3b

def acceptance(client,children,args,shell_token,socketpath):
    results={}
    def wait(fn,label):
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            v=fn()
            if v:return v
            time.sleep(.12)
        (s.STATE/'audit-failure.json').write_text(json.dumps(s.camera(),indent=2))
        raise AssertionError(label)
    def check(name,value):
        assert value,name;results[name]=True;print('PASS audit:',name,flush=True)
    s.ctl('dispatch','hl.dsp.focus({ workspace = "10" })')
    client.call('world.load',{'worldId':'org.hyprune.m3b'})
    if os.environ.get('HYPRUNE_AUDIT_CASE')=='x11':
        trigger=s.STATE/'x11-trigger';trigger.unlink(missing_ok=True)
        for title in ['Audit X11 owner','Audit X11 distractor']:
            command=shlex.join([str(s.ROOT/'build/nested-x11-owner'),title,str(trigger if 'owner' in title else trigger.with_suffix('.never'))])
            s.ctl('eval','hl.dispatch(hl.dsp.exec_cmd('+json.dumps(command)+'))')
        wait(lambda:len(json.loads(s.ctl('clients','-j')))>=2,'X11 clients')
        s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
        def cap(title):return next((v for v in s.camera()['captures'] if v['title']==title),None)
        owner=wait(lambda:cap('Audit X11 owner'),'owner capture')
        other=wait(lambda:cap('Audit X11 distractor'),'distractor capture')
        client.call('surface.focus',{'surfaceId':other['id'],'intent':'keyboard'})
        trigger.write_text('show')
        menu=wait(lambda:cap('Audit X11 menu'),'menu capture')
        dialog=wait(lambda:cap('Audit X11 dialog'),'dialog capture');time.sleep(.8)
        menu=cap('Audit X11 menu');dialog=cap('Audit X11 dialog')
        check('X11_transient_owner_beats_other_focus',menu['parentId']==owner['id'] and dialog['parentId']==owner['id'])
        print('dialog size',dialog['pixels'],flush=True)
        check('attached_dialog_keeps_requested_size',dialog['pixels']==[1600,900])
        check('attached_windows_skip_root_placement',not any(v['surfaceId'] in [menu['id'],dialog['id']] for v in s.camera()['instances']))
        client.call('runtime.exit',{})
        (s.STATE/'review-x11.json').write_text(json.dumps(results,indent=2)+'\n');return
    # Enter first: clients launched while the world is active open in its room (room 20 model).
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda:s.camera()['active'],'world active')
    apps=[]
    for title in ['Audit first','Audit second']:
        app=s.envrun('kitty','--start-as','normal','--title',title,'--override','initial_window_width=640','--override','initial_window_height=480','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(app);apps.append(app)
    wait(lambda:len(json.loads(s.ctl('clients','-j')))>=2,'two clients mapped')
    def surface(title):return next((v for v in s.camera()['captures'] if v['title']==title),None)
    a=wait(lambda:surface('Audit first'),'first surface');b=wait(lambda:surface('Audit second'),'second surface')
    def fullscreen(title,action):
        w=next(v for v in json.loads(s.ctl('clients','-j')) if v['title']==title)
        s.ctl('eval',f'hl.dispatch(hl.dsp.window.fullscreen({{window="address:{w["address"]}",action="{action}"}}))')
    fullscreen('Audit first','set');wait(lambda:s.camera()['fullscreen']==2,'first fullscreen')
    check('fullscreen_first_owner',s.camera()['fullscreenId']==a['id'])
    fullscreen('Audit second','set')
    wait(lambda:s.camera()['fullscreen']==2 and s.camera()['fullscreenId']==b['id'],'fullscreen owner replacement stuck')
    check('fullscreen_replacement',True)
    apps[1].terminate();apps[1].wait(timeout=5)
    wait(lambda:s.camera()['fullscreen']==0 and not s.camera()['fullscreenId'],'closed fullscreen owner not cleared')
    check('fullscreen_close_cleanup',s.camera()['cursor']=='reticle')
    # Client A still exists and can be used after both fullscreen transitions.
    client.call('surface.focus',{'surfaceId':a['id'],'intent':'keyboard'})
    check('surviving_window_usable',s.camera()['mode']=='typing')
    if os.environ.get('HYPRUNE_NESTED_SMALL')=='1':
        from ctl import verified
        _,env=verified()
        subprocess.run(['wtype','-M','logo','-P','Super_L','-k','Tab','-p','Super_L','-m','logo'],env=env,check=True)
        wait(lambda:s.camera()['mode']=='movement','leave use')
        client.call('movement.set',{'modeId':'com.hyprune.fly'})
        client.call('surface.pull',{'surfaceId':a['id']})
        plane=surface('Audit first');pos=plane['position']
        client.call('camera.set',{'position':[pos[0],pos[1],pos[2]+3],'orientation':[0,0,0,1]})
        device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
        def pointer(*args):
            device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.1)
        modifier=s.envrun('wtype','-M','logo','-P','Super_L','-s','2500','-p','Super_L','-m','logo');children.append(modifier)
        wait(lambda:s.camera()['superHeld'],'resize modifier')
        pointer('button',273,1);wait(lambda:s.camera()['gesture'] in [3,4],'resize gesture')
        pointer(-35,20);pointer('button',273,0);modifier.wait(timeout=5)
        plane=surface('Audit first')
        check('real_resize_640x480',0<plane['pixels'][0]<=640 and 0<plane['pixels'][1]<=480)
    client.call('runtime.exit',{})
    (s.STATE/'review-audit.json').write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__':
    m3b.acceptance=acceptance
    s.run(argparse.Namespace(integration=False,m3b=True,m3a=False,m2=False,m1=False,live=False,serve=False,record=False,visible=False))
