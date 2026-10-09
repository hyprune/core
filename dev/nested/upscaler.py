"""IPC 0.16 upscaler acceptance (graphics.upscaler). Called only under run.sh's flock.
A 0.16 session switches FSR 1 / bilinear live and sees the setting everywhere;
a 0.15 session neither sees nor writes it. Outgoing frames are wire-audited."""
import json,time
import session as s
from m2 import wait

def acceptance(client,children,args,shell_token,socketpath):
    passed=[]
    def check(name,ok,extra=None):
        assert ok,(name,extra);passed.append(name);print('PASS upscaler:',name,flush=True)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':'org.hyprune.atrium'})
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active'],timeout=20)
    get=lambda c=client:c.call('graphics.get',{})
    g=get()
    check('default_fsr1',g['effective']['upscaler']=='fsr1' and g['config']['upscaler'] is None)
    check('options_and_presets',g['options']['upscaler']==['fsr1','bilinear'] and all(p['upscaler']=='fsr1' for p in g['presets'].values()))
    check('performance_control',g['performance']['controls']['upscaler']['sharedPass']=='upscale_sharpen')
    client.call('graphics.set',{'patch':{'render_scale':.5,'dynamic_resolution':False}})
    def active():
        s.ctl('eval','hl.plugin.hyprune.camera(0,1.7,4,0.001,0)');time.sleep(.3)
        return get()['timings']['upscaler']
    wait(lambda:active()['active']=='fsr1',timeout=10);check('fsr1_active_below_output_size',True)
    g=client.call('graphics.set',{'patch':{'upscaler':'bilinear'}})
    check('set_bilinear',g['config']['upscaler']=='bilinear' and g['effective']['upscaler']=='bilinear' and g['overrides']['upscaler']=='bilinear')
    wait(lambda:active()=={'requested':'bilinear','active':'bilinear','error':None},timeout=10);check('bilinear_live',True)
    check('persisted',json.loads((s.STATE/'graphics.json').read_text())['upscaler']=='bilinear')
    try:client.call('graphics.set',{'patch':{'upscaler':'fsr2'}});ok=False
    except RuntimeError:ok=True
    check('invalid_rejected',ok)
    old=s.Client(str(socketpath),shell_token,protocol='0.15',client_id='com.hyprune.shell')
    g15=get(old)
    sections=[g15[k] for k in ('config','effective','overrides','options')]+list(g15['presets'].values())+[g15['performance'][k] for k in ('requested','effective','controls')]
    check('older_reply_frozen',all('upscaler' not in x for x in sections))
    try:old.call('graphics.set',{'patch':{'upscaler':'fsr1'}});ok=False
    except RuntimeError as e:ok='0.16' in str(e)
    check('older_cannot_write',ok)
    old.close()
    g=client.call('graphics.set',{'patch':{'upscaler':None}})
    check('null_restores_preset',g['effective']['upscaler']=='fsr1' and 'upscaler' not in g['overrides'] or g['overrides'].get('upscaler') is None)
    wait(lambda:active()['active']=='fsr1',timeout=10);check('fsr1_restored',True)
    (s.ROOT/'docs/evidence/upscaler.json').write_text(json.dumps({'protocol':'0.16','checks':passed,'nestedOnly':True},indent=2)+'\n')
