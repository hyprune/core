"""Owner interaction acceptance: nested compositor only, run.sh owns the flock."""
import json,math,subprocess,time
import session as s
from m2 import wait,keys,pose

def acceptance(client,children,args,token,socket):
    checks=[]
    def check(name,value=True):
        assert value,name
        checks.append(name);print('PASS placement:',name,flush=True)
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.2)
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    def pointer(*args):
        device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.1)
    def chord():keys('-M','logo','-P','Super_L','-k','Tab','-p','Super_L','-m','logo')
    def modified(mods,fn):
        args=[]
        for name,key in mods:args+=['-M',name,'-P',key]
        tail=[]
        for name,key in reversed(mods):tail+=['-p',key,'-m',name]
        hold=s.envrun('wtype',*args,'-s','1400',*tail);children.append(hold);time.sleep(.2);fn();hold.wait(timeout=4);time.sleep(.12)
    logo=[('logo','Super_L')];shift=[('shift','Shift_L')];ctrl=[('ctrl','Control_L')]
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    def queue():return state()['editor']['unplaced']
    def mode():return state()['editor']['mode']
    def capture(sid):return next(v for v in s.camera()['captures'] if v['id']==sid)
    def aim(sid):
        v=capture(sid);x,y,z,w=v['rotation'];n=[2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)]
        pos=[v['position'][i]+3*n[i] for i in range(3)]
        pose(client,pos,math.atan2(-n[0],n[2]),math.asin(-n[1]));time.sleep(.3)
        wait(lambda:s.camera()['aim']==sid)
    def launch(title):
        child=s.envrun('kitty','--class','placement-'+title,'--title',title,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(child)
        wait(lambda:any(w['title']==title for w in json.loads(s.ctl('clients','-j'))));time.sleep(.4);return child
    client.call('world.load',{'worldId':'org.hyprune.m3b'})
    client.call('workspace.bind',{'id':20,'worldId':'org.hyprune.m3b','areaId':'sun-court'})
    s.ctl('eval','hl.dispatch(hl.dsp.focus({monitor="HEADLESS-1"}))')
    s.ctl('eval','hl.dispatch(hl.dsp.focus({workspace="20"}))')
    launch('Pocket-A');launch('Pocket-B');launch('Pocket-C')
    s.ctl('eval','hl.dispatch(hl.dsp.focus({workspace="91"}))')
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda:queue()['count']==3)
    client.call('movement.set',{'modeId':'com.hyprune.fly'});pose(client,[0,2.3,4]);time.sleep(.3)
    check('entry_selects_placement_with_queue',state()['editor']['tool']==5)
    check('generic_slot_labels',state()['editor']['toolSlots']['alternate']['action']=='placement.pocket' and len(state()['editor']['toolDefinitions'])==5)
    check('unplaced_not_drawn',all(not capture(w['surfaceId'])['visible'] for w in queue()['windows']))
    check('ghost_published',queue()['preview'])
    selected=queue()['selectedId'];pointer('scroll',-1);wait(lambda:queue()['selectedId']!=selected)
    check('wheel_cycles_queue');first=queue()['selectedId']
    pointer('button',272);wait(lambda:queue()['count']==2)
    check('LMB_places_full_size_client',capture(first)['visible'] and not capture(first)['collision'])
    aim(first);pointer('button',274);wait(lambda:queue()['count']==3)
    check('stash_keeps_client_and_is_newest',queue()['windows'][0]['surfaceId']==first and not capture(first)['visible'])
    pose(client,[0,2.3,4]);time.sleep(.3);pointer('button',272);wait(lambda:queue()['count']==2);check('pocketed_window_places_again',capture(first)['visible'])
    # Pick another unplaced window and mount it to the fixture's arbitrary side wall.
    second=queue()['selectedId'];pose(client,[0,2.3,0],math.pi/2,0);time.sleep(.3)
    pointer('button',273);wait(lambda:queue()['count']==1)
    check('RMB_surface_mount',capture(second)['visible'] and not capture(second)['collision'])
    keys('-k','1');aim(first)
    initial=capture(first);base=initial['pixels'];size=initial['size']
    modified(logo+shift,lambda:pointer('scroll',-1))
    wait(lambda:state()['editor']['contentScale'] is not None)
    scaled=wait(lambda:capture(first) if capture(first)['pixels'][0]<base[0] else None)
    check('content_scale_changes_resolution_not_world_size',all(abs(scaled['size'][i]-size[i])<.003 for i in range(2)))
    check('content_scale_feedback',state()['editor']['contentScale']['scale']>1)
    oldq=scaled['rotation'];modified(logo+ctrl,lambda:pointer('scroll',-1));wait(lambda:capture(first)['rotation']!=oldq)
    check('rotate_new_chord');aim(first)
    carrying=s.envrun('wtype','-M','logo','-P','Super_L','-s','3000','-k','r','-s','3000','-k','r','-s','1000','-p','Super_L','-m','logo');children.append(carrying)
    wait(lambda:s.camera()['superHeld']);pointer('button',272,1);wait(lambda:s.camera()['gesture']==1)
    check('carry_defaults_face_player',state()['editor']['carryFacing'] is True)
    # One keyboard emits held Super and R: separate wtype keymaps reuse keycodes.
    wait(lambda:state()['editor']['carryFacing'] is False)
    q=capture(first)['rotation'];pointer(100,-30);time.sleep(.3)
    check('keep_orientation_while_looking',sum(abs(a-b) for a,b in zip(q,capture(first)['rotation']))<.003)
    wait(lambda:state()['editor']['carryFacing'] is True)
    pointer('button',272,0);wait(lambda:s.camera()['gesture']==0);carrying.wait(timeout=10)
    check('carry_releases')
    # The room wall (20 is bound here) is behind the held window, not reached:
    # releasing places the window instead of dropping it into that workspace.
    check('release_before_wall_keeps_window',capture(first)['visible'] and any(i['surfaceId']==first for i in state()['mounts']['instances']))
    pose(client,[0,capture(first)['position'][1],2]);time.sleep(.3);wait(lambda:s.camera()['aim']==first)
    modified(logo+shift,lambda:pointer('button',274));wait(lambda:state()['editor']['contentScale']['reset'])
    reset=capture(first);check('reset_preserves_quad_size',all(abs(reset['size'][i]-size[i])<.003 for i in range(2)))
    check('reset_client_size',reset['pixels']==base)
    seq=state()['editor']['contentScale']['sequence'];aim(first)
    modified(logo+shift,lambda:pointer('button',274));wait(lambda:state()['editor']['contentScale']['sequence']>seq)
    check('repeated_reset_has_feedback')
    # The same four physical slots drive each tool; unassigned Alternate is inert.
    keys('-k','2');before=queue()['count'];pointer('button',274);check('unassigned_slot_noop',queue()['count']==before)
    keys('-k','3');pose(client,[0,2.3,0],math.pi/2,0);time.sleep(.3)
    pointer('button',272);wait(lambda:len(state()['editor']['paths'])>0)
    points=len(state()['editor']['paths'][-1]['points']);pointer('button',272)
    wait(lambda:len(state()['editor']['paths'][-1]['points'])==points+1)
    pointer('button',273);wait(lambda:len(state()['editor']['paths'][-1]['points'])==points)
    pointer('button',274);wait(lambda:state()['editor']['paths'][-1]['closed'])
    pointer('scroll',-1);wait(lambda:state()['editor']['submode']=='closed curve')
    check('path_slots_add_remove_close_cycle')
    original=client.call('input.get',{})['config'];rebound=json.loads(json.dumps(original))
    rebound['modes']['world']['cycle']=['F7'];client.call('input.set',{'config':rebound})
    keys('-k','F7');wait(lambda:state()['editor']['submode']=='closed arc');time.sleep(.2)
    check('cycle_key_advances_once_on_press',state()['editor']['submode']=='closed arc')
    client.call('input.set',{'config':original})
    keys('-k','4');pointer('button',273);wait(lambda:state()['editor']['toolSlots']['secondary']['label']=='Use world axes')
    pointer('button',274);wait(lambda:state()['editor']['toolSlots']['alternate']['label']=='Disable snapping')
    pointer('scroll',-1);wait(lambda:state()['editor']['submode']=='rotate')
    check('position_slots_axes_snap_cycle')
    keys('-k','1');aim(first);pointer('button',272,1);wait(lambda:s.camera()['pressedButtons']==1)
    keys('-k','5');pointer('button',272,0);wait(lambda:s.camera()['pressedButtons']==0)
    check('slot_release_owned_across_tool_change');keys('-k','1')
    rebound=json.loads(json.dumps(original));rebound['modes']['world']['primary']=['Wheel'];rebound['modes']['world']['cycle']=[]
    client.call('input.set',{'config':rebound});aim(first);pointer('scroll',-1)
    check('wheel_bound_primary_balances_client_release',s.camera()['pressedButtons']==0)
    client.call('input.set',{'config':original})
    # Every ordinary exit is one press to world; the next press is free cursor.
    for use,key in [('interactive','e'),('focus','f')]:
        aim(first);keys('-k',key);wait(lambda:mode()==use)
        if use=='focus':wait(lambda:s.camera()['fullscreen']==2)
        pointer('button',275);wait(lambda:mode()=='world')
        check(use+'_Back_returns_world')
        chord();wait(lambda:mode()=='cursor');chord();wait(lambda:mode()=='world')
        check(use+'_no_cursor_trap')
        aim(first);keys('-k',key);wait(lambda:mode()==use)
        if use=='focus':wait(lambda:s.camera()['fullscreen']==2)
        chord();wait(lambda:mode()=='world');check(use+'_SuperTab_returns_world')
    # Clear app focus by entering/leaving use, then Escape from free cursor.
    aim(first);keys('-k','e');wait(lambda:mode()=='interactive');chord();wait(lambda:mode()=='world')
    pointer('button',275);wait(lambda:mode()=='cursor');keys('-k','Escape');wait(lambda:mode()=='world');check('Escape_without_app_focus')
    # Persistence: scale, reload, and return to exact world size/logical request.
    aim(first);modified(logo+shift,lambda:pointer('scroll',-1));time.sleep(.3)
    expected=capture(first);class_name=next(w['appClass'] for w in state()['apps'] if w['surfaceId']==first)
    placements=client.call('placements.list',{})['placements'];saved=next(p for p in placements if p['rule']['appClass']==class_name)
    check('placement_contains_content',saved['content']['logicalSize']==expected['pixels'])
    client.call('world.load',{'worldId':'org.hyprune.m3b'});time.sleep(.5)
    loaded=next(w for w in s.camera()['captures'] if w['title']==expected['title'])
    check('reload_preserves_placement_and_content',loaded['visible'] and loaded['pixels']==expected['pixels'] and all(abs(loaded['size'][i]-size[i])<.003 for i in range(2)))
    path=s.STATE/'placement-transcript.json';path.write_text(json.dumps(client.transcript));subprocess.run(['node',str(s.ROOT/'tests/validate-authoring-wire.mjs'),str(path),'0.15'],check=True)
    check('IPC015_wire_conformance')
    s.ctl('eval','hl.plugin.hyprune.toggle()');wait(lambda:not s.camera()['active']);check('toggle_returns_previous_workspace',next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')['activeWorkspace']['id']==91)
    (s.ROOT/'docs/evidence/placement.json').write_text(json.dumps({'nestedOnly':True,'checks':checks},indent=2)+'\n')
