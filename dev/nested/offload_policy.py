#!/usr/bin/env python3
"""Offload power policy on this machine: flock /tmp/worldshell-nested.lock python3 dev/nested/offload_policy.py

Checks the power policy itself on whatever supply the machine is on, without the
dGPU load of offload-power.py + offload_acceptance.py (which only gate on AC0 and
so would also run on a USB-C PD supply). The expected profile is read from
/sys/class/power_supply exactly as core reads it.
Automatic offload must refuse cleanly on battery or USB-C power (local renderer,
a reason, capSource "profile"); an explicit "on" is admitted on USB-C unless the
battery is discharging. A desktop (no battery) reports full-ac and a device cap.
"""
import argparse,json,os,pathlib,time
import session as s
import m3b
from m2 import wait

def expected():
    battery=discharging=usb=ac=False
    for p in pathlib.Path('/sys/class/power_supply').iterdir():
        read=lambda n:(p/n).read_text().strip() if (p/n).exists() else ''
        kind=read('type')
        if kind=='Battery':battery=True;discharging|=read('status')=='Discharging'
        if read('online')!='1':continue
        if kind=='Mains' or kind.startswith('USB'):ac=True;usb|=kind.startswith('USB')
    if not battery:return 'full-ac','mains'
    return ('battery' if not ac else 'usb-c' if usb else 'ac'),('discharging' if discharging else 'charging')

def acceptance(client,children,args,shell_token,socketpath):
    checks=[]
    def check(name,ok,data=None):
        assert ok,(name,data)
        checks.append({'name':name,'evidence':data});print('PASS offload policy:',name,flush=True)
    profile,supply=expected()
    client.call('world.load',{'worldId':'org.hyprune.atrium'})
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active'])
    check('IPC 0.18 negotiated',client.hello['protocol']=='0.18',client.hello['protocol'])
    for mode in ['auto','on']:
        client.call('graphics.set',{'patch':{'offload':mode}});time.sleep(2.5)
        perf=client.call('graphics.get',{})['performance']
        info={k:perf.get(k) for k in ['profile','status','activeRenderer','reason','capW','capSource','device','mode']}
        check(f'offload={mode}: profile follows the power supply ({supply})',perf['profile']==profile or (profile=='ac' and perf['profile'] in ('full-ac','limited-ac')),info)
        if profile in ('battery','usb-c') or supply=='discharging':
            check(f'offload={mode}: laptop cap comes from the profile',perf['capSource'] in ('profile','configured') and perf['capW']<=80,info)
            if mode=='auto' or supply=='discharging' or profile=='battery':
                check(f'offload={mode}: refused cleanly, world stays local',perf['activeRenderer']=='in-process' and perf['status']!='active' and perf['reason'] not in ('','ready'),info)
            else:  # explicit "on" on USB-C is the user's choice (policy admits it)
                check('offload=on on USB-C: admitted, renders via offload or reports why',perf['activeRenderer']=='offload' or perf['reason'] not in ('','ready'),info)
        else:
            check(f'offload={mode}: mains cap from the device or fallback',perf['capSource'] in ('device','fallback','configured'),info)
        f0=s.camera()['frames'];s.ctl('eval','hl.plugin.hyprune.camera(0.4,1.7,3.6,0.2,0)');time.sleep(.6)
        check(f'offload={mode}: the world keeps rendering',s.camera()['frames']>f0,(f0,s.camera()['frames']))
    client.call('graphics.set',{'patch':{'offload':'off'}})
    client.call('runtime.exit',{})
    (s.ROOT/'docs/evidence/offload-policy.json').write_text(json.dumps({'nestedOnly':True,'supply':supply,'profile':profile,'checks':checks},indent=2)+'\n')
    print('PASS offload policy:',len(checks),'checks',flush=True)

if __name__=='__main__':
    os.environ['HYPRUNE_NESTED_PROTOCOL']='0.18'
    fixture=s.state_dir('offload-policy');fixture.mkdir(exist_ok=True)
    for name in ['input.json','layout.json','editor.json','graphics.json']:(fixture/name).unlink(missing_ok=True)
    m3b.acceptance=acceptance
    s.run(argparse.Namespace(state='offload-policy',m3b=True,m3a=False,m2=False,m1=False,integration=False,live=False,serve=False,record=False,visible=False))
