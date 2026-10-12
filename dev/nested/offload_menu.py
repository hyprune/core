#!/usr/bin/env python3
"""flock /tmp/worldshell-nested.lock python3 dev/nested/offload-power.py /tmp/offload-menu-power.json python3 dev/nested/offload_menu.py"""
import argparse,json,os,pathlib,signal,subprocess,time
import session as s
import m3b
from m2 import wait,keys
from ctl import verified

def acceptance(client,children,args,shell_token,socketpath):
    client.call('world.load',{'worldId':'com.hyprune.lumen-reach'})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    def perf():return client.call('graphics.get',{})['performance']
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:state()['runtime']['active'])
    before=state()['world']
    r,w=os.pipe();os.write(w,(shell_token+'\n').encode());os.close(w)
    _,env=verified();env.update(HYPRUNE_TOKEN_FD=str(r),HYPRUNE_PROTOCOL='0.10',HYPRUNE_SOCKET=str(socketpath),HYPRUNE_OUTPUT_SCREEN='HEADLESS-1',HYPRUNE_DEV_AUTOMATION='1')
    shell=subprocess.Popen(['python3',str(pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR',str(s.ROOT.parent/'shell')))/'bridge/launch.py'),'--protocol','0.10'],env=env,pass_fds=(r,),stdout=open(s.STATE/'shell.log','w'),stderr=subprocess.STDOUT)
    os.close(r);children.append(shell)
    def child():
        assert shell.poll() is None
        return next((int(p) for p in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split() if pathlib.Path(f'/proc/{p}/comm').read_text().strip() in ['qs','quickshell']),None)
    pid=wait(child)
    def ui(*args):return subprocess.check_output([str(s.HERE/'ctl.py'),'env','qs','ipc','--pid',str(pid),'call','preview',*map(str,args)],text=True).strip()
    def status():
        try:return json.loads(ui('status'))
        except (subprocess.CalledProcessError,json.JSONDecodeError):return {}
    wait(lambda:status().get('lease'));keys('-P','Tab','-s','1300','-p','Tab');wait(lambda:status().get('director'))
    ui('tabNamed','performance');wait(lambda:status().get('graphics',{}).get('performance'))
    def choose(move):
        assert ui('graphicsFocus','offload')=='true'
        keys('-k','space');time.sleep(.15);keys('-k',move);keys('-k','Return')
    choose('End');wait(lambda:perf()['activeRenderer']=='offload',timeout=25)
    assert state()['world']==before and state()['runtime']['active']
    def costs_ready():
        shown=status().get('graphics',{}).get('performance',{})
        return shown.get('activeRenderer')=='offload' and shown.get('controls',{}).get('sky',{}).get('gpuMs') is not None
    wait(costs_ready)
    out=s.ROOT/'docs/evidence/offload';out.mkdir(exist_ok=True)
    subprocess.run([str(s.HERE/'ctl.py'),'shot',str(out/'menu-performance.png')],check=True)
    on=perf();choose('Home');wait(lambda:perf()['mode']=='off' and perf()['activeRenderer']=='in-process')
    assert state()['world']==before
    errors=(s.STATE/'shell.log').read_text()
    assert not any(x in errors for x in ['ReferenceError','TypeError','Presentation:']),errors[-2000:]
    (out/'menu.json').write_text(json.dumps({'protocol':status()['protocol'],'on':on,'off':perf(),'input':'Performance ComboBox, real nested keyboard events','sameWorld':True},indent=2)+'\n')
    print('PASS menu hot switch through shell IPC 0.10',flush=True)

if __name__=='__main__':
    signal.signal(signal.SIGTERM,lambda *_:(_ for _ in ()).throw(SystemExit('power watchdog')))
    os.environ['HYPRUNE_NESTED_PROTOCOL']='0.10'
    fixture=s.state_dir('offload-menu');fixture.mkdir(exist_ok=True)
    for name in ['input.json','layout.json','editor.json']:(fixture/name).unlink(missing_ok=True)
    m3b.acceptance=acceptance
    s.run(argparse.Namespace(state='offload-menu',m3b=True,m3a=False,m2=False,m1=False,integration=False,live=False,serve=False,record=False,visible=False))
