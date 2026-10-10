"""Render pass without RENDER_POST: dev/nested/run.sh --render-fail (lifetime flock).

Hyprland 0.56.2 emits RENDER_PRE and returns early when beginRender fails. The
nested-only hook hl.plugin.hyprune.test_render_fail() emits exactly that. Core
must keep ticking, serving IPC and running deferred work, and log once.
"""
import json,time
import session as s
from m2 import wait

def acceptance(client,children,args,shell_token,socketpath):
    checks=[]
    def check(name,ok,data=None):
        assert ok,(name,data)
        checks.append(name);print('PASS render-fail:',name,flush=True)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':'org.hyprune.atrium'})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active'])
    # A real beginRender failure (unallocatable output) is rejected at mode set here
    # and falls back; the hook emits what Hyprland's failing path emits instead.
    for _ in range(4):
        s.ctl('eval','hl.plugin.hyprune.test_render_fail()');time.sleep(.2)
    # Everything that used to stop for good: ticks, IPC, deferred work.
    f0=s.camera()['frames'];r0=client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['revision']
    for _ in range(5):
        client.call('session.ping',{});time.sleep(.4)
    s.ctl('eval','hl.plugin.hyprune.camera(0.5,1.7,3.5,0.3,0)');time.sleep(.6)
    snap=client.call('state.snapshot',{'subscriptionId':sub})['snapshot']
    check('IPC keeps answering and publishing',snap['revision']>r0,(r0,snap['revision']))
    check('world keeps rendering on the healthy output',s.camera()['frames']>f0,(f0,s.camera()['frames']))
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')  # deferred: must not be re-deferred forever
    check('deferred work still runs (toggle out)',wait(lambda:not s.camera()['active'],10))
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
    check('and back in',wait(lambda:s.camera()['active'],10))
    leaked=s.camera().get('renderPassesLeaked',0)
    check('every unpaired RENDER_PRE was noticed and closed',leaked>=4 and not s.camera().get('renderPassOpen',False),leaked)
    text=(s.STATE/'hyprland.log').read_text(errors='replace')
    check('logged once',text.count('render pass ended without RENDER_POST')==1,text.count('render pass ended without RENDER_POST'))
    check('no stuck render-pass exceptions',"deferred work inside compositor render pass" not in text)
    client.call('runtime.exit',{})
    (s.ROOT/'docs/evidence/render-fail.json').write_text(json.dumps({'nestedOnly':True,'checks':checks,'leakedPasses':s.camera()['renderPassesLeaked']},indent=2)+'\n')
    print('PASS render fail:',len(checks),'checks',flush=True)
