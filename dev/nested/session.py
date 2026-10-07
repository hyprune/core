#!/usr/bin/env python3
import argparse, json, os, pathlib, secrets, shutil, signal, socket, struct, subprocess, sys, time
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parent.parent;STATE=HERE/'.state'
sys.path.insert(0,str(HERE))
from ctl import verified
class Client:
    def __init__(self,path,token=None):
        self.socket=socket.socket(socket.AF_UNIX);self.socket.settimeout(5);self.socket.connect(path);self.count=0;self.events=[];self.transcript=[]
        params={'protocols':['0.1'],'client':{'id':'org.hyprune.nested','name':'Nested test'},'capabilities':['state.read','world.control','surface.control','movement.control','runtime.control']}
        if token:params['token']=token
        self.hello=self.call('session.hello',params)
    def receive(self):
        def exact(n):
            b=b''
            while len(b)<n:
                d=self.socket.recv(n-len(b))
                if not d:raise EOFError('IPC closed')
                b+=d
            return b
        return json.loads(exact(struct.unpack('>I',exact(4))[0]))
    def call(self,method,params):
        self.count+=1;ident=str(self.count);message=json.dumps({'jsonrpc':'2.0','id':ident,'method':method,'params':params}).encode();self.socket.sendall(struct.pack('>I',len(message))+message)
        while True:
            result=self.receive()
            if result.get('id')==ident:
                self.transcript.append({'method':method,'response':result})
                if 'error' in result:raise RuntimeError(result['error'])
                return result['result']
            self.events.append(result)
            if STATE.exists():
                with (STATE/'events.log').open('a') as stream:
                    p=result.get('params',{});stream.write(f"revision {p.get('baseRevision','?')} -> {p.get('revision','?')}  {result.get('method')}\n")
                    if 'movement' in p.get('changes',{}):stream.write('  camera '+str([round(v,2) for v in p['changes']['movement']['position']])+'\n')
    def close(self):self.socket.close()
def ctl(*args):
    return subprocess.check_output([str(HERE/'ctl.py'),'hyprctl',*args],text=True).strip()
def envrun(*args,**kwargs):
    return subprocess.Popen([str(HERE/'ctl.py'),'env',*args],**kwargs)
def camera():
    path=STATE/'camera.json'
    ctl('eval',f'local f = assert(io.open({json.dumps(str(path))}, "w")); f:write(hl.plugin.hyprune.camera()); f:close()')
    return json.loads(path.read_text())

def run(args):
    STATE.mkdir(exist_ok=True);os.chmod(STATE,0o700)
    subprocess.run([sys.executable,str(ROOT/'tests/fixtures/generate.py')],check=True,stdout=subprocess.DEVNULL)
    installed=STATE/'worlds';installed.mkdir(exist_ok=True)
    package=installed/'org.hyprune.atrium'
    if package.exists():shutil.rmtree(package)
    shutil.copytree(ROOT/'tests/fixtures/atrium',package)
    token=secrets.token_hex(32)
    grants=[{'token':token,'client':'org.hyprune.nested','capabilities':['state.read','world.control','surface.control','movement.control','runtime.control']}]
    read,write=os.pipe();os.write(write,json.dumps(grants).encode());os.close(write)
    environment=os.environ.copy();environment.update(AQ_BACKEND='wayland',HYPRUNE_WORLD_DIR=str(installed),HYPRUNE_LAUNCH_FD=str(read))
    config=str(HERE/'hyprland.lua');log=open(STATE/'hyprland.log','w')
    process=subprocess.Popen(['Hyprland','-c',config],env=environment,pass_fds=(read,),stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);os.close(read)
    children=[];client=None
    try:
        runtime=pathlib.Path(environment['XDG_RUNTIME_DIR']);signature=None
        for _ in range(100):
            if process.poll() is not None:raise RuntimeError('nested Hyprland exited; see .state/hyprland.log')
            for lock in (runtime/'hypr').glob('*/hyprland.lock'):
                lines=lock.read_text().splitlines()
                if lines and lines[0]==str(process.pid) and (lock.parent/'.socket.sock').is_socket():signature=lock.parent.name;wayland=lines[1];break
            if signature:break
            time.sleep(.1)
        if not signature:raise RuntimeError('nested startup timed out')
        state={'pid':process.pid,'startTime':pathlib.Path(f'/proc/{process.pid}/stat').read_text().split()[21],'config':config,'runtime':str(runtime),'signature':signature,'wayland':wayland,'outerSignature':environment.get('HYPRLAND_INSTANCE_SIGNATURE',''),'outerWayland':environment.get('WAYLAND_DISPLAY','')}
        (STATE/'session.json').write_text(json.dumps(state));verified()
        ctl('output','create','headless');time.sleep(.3)
        ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
        copy=STATE/f'hyprune-{time.time_ns()}.so';shutil.copy2(ROOT/'build/hyprune-core.so',copy)
        result=ctl('plugin','load',str(copy));print('load:',result,flush=True)
        if result!='ok':raise RuntimeError(result)
        ctl('reload')
        socketpath=runtime/'hyprune'/signature/'control.sock'
        for _ in range(50):
            if socketpath.is_socket():break
            time.sleep(.1)
        client=Client(str(socketpath),token)
        print('hello:',client.hello['protocol'],client.hello['methods'],flush=True)
        if args.serve:
            print('Nested ready; dev/nested/ctl.py targets only this session.',flush=True)
            while process.poll() is None:time.sleep(.5)
            return
        smoke(client,children,args)
        if args.record:record(client)
        failures(client,copy,socketpath,process.pid)
        (STATE/'transcript.json').write_text(json.dumps(client.transcript))
        (STATE/'notifications.json').write_text(json.dumps(client.events))
    finally:
        if client:client.close()
        for p in children:
            if p.poll() is None:p.terminate()
        if process.poll() is None:
            process.terminate()
            try:process.wait(timeout=8)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        (STATE/'session.json').unlink(missing_ok=True)
        for artifact in STATE.glob('*.so'):artifact.unlink()
        log.close()

def smoke(client,children,args):
    assert 'state.subscribe' in client.hello['methods']
    loaded=client.call('world.load',{'worldId':'org.hyprune.atrium'});print('world loaded:',loaded,flush=True)
    subscription=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    # Real clients and real input within the verified nested Wayland session.
    children.append(envrun('kitty','--title','Hyprune workbench','--override','font_size=17','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    children.append(envrun('kitty','--title','Hyprune event stream','--override','font_size=13','bash','-c',f'printf "HYPRUNE / IPC 0.1\\nLive state deltas\\n\\n"; tail -n 12 -f {STATE}/events.log',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    (STATE/'events.log').write_text('Waiting for subscription...\n')
    time.sleep(1.5)
    for window in json.loads(ctl('clients','-j')):
        address=window['address']
        ctl('eval',f'hl.dispatch(hl.dsp.window.float({{ window = "address:{address}", action = "enable" }}))')
        ctl('eval',f'hl.dispatch(hl.dsp.window.resize({{ window = "address:{address}", x = 1100, y = 660 }}))')
    time.sleep(.3)
    print('toggle:' ,ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")'),flush=True);time.sleep(1.5)
    print('camera:',camera(),flush=True)
    state=client.call('state.snapshot',{'subscriptionId':subscription})['snapshot']['state'];assert state['runtime']['active'];assert len(state['surfaces'])>=2,state
    assert camera()['frames']>0
    subprocess.run([str(HERE/'ctl.py'),'shot',str(STATE/'world.png')],check=True)
    before=state['movement']['position']
    subprocess.run([str(HERE/'ctl.py'),'env','wtype','-P','w','-s','700','-p','w'],check=True)
    time.sleep(.1);after=client.call('state.snapshot',{'subscriptionId':subscription})['snapshot']['state']['movement']['position']
    assert abs(after[2]-before[2])>.5,(before,after)
    print('walk:',before,'->',after,flush=True)
    surface=next(s for s in state['surfaces'] if 'workbench' in s['title'])
    # Center the real picking ray on the first floating surface and click through the Wayland seat.
    index=next(i for i,s in enumerate(state['surfaces']) if s['id']==surface['id'])
    x=[0,-3.8,3.8][index]
    ctl('eval',f'hl.plugin.hyprune.camera({x}, 1.7, 1.5, 0, 0)')
    subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer')],check=True)
    time.sleep(.15)
    focus_state=client.call('state.snapshot',{'subscriptionId':subscription})['snapshot']['state']
    assert next(s for s in focus_state['surfaces'] if s['id']==surface['id'])['focused'], focus_state
    print('surface click: passed',flush=True)
    marker=STATE/'typed.txt';marker.unlink(missing_ok=True)
    subprocess.run([str(HERE/'ctl.py'),'env','wtype',f"printf 'surface input works\\n' > {marker}"],check=True)
    subprocess.run([str(HERE/'ctl.py'),'env','wtype','-k','Return'],check=True);time.sleep(.5)
    assert marker.read_text()=='surface input works\n'
    print('terminal typing: passed',flush=True)
    subprocess.run([str(HERE/'ctl.py'),'env','wtype','clear'],check=True)
    subprocess.run([str(HERE/'ctl.py'),'env','wtype','-k','Return'],check=True)

    assert any(e.get('method')=='state.delta' for e in client.events)
    client.call('runtime.exit',{});time.sleep(.3);assert not camera()['active']
    print('PASS: load, hello, snapshot, toggle, render, walk, keyboard routing, events, exit',flush=True)
def failures(client,plugin,socketpath,pid):
    # Rejected reload must preserve the previously committed world.
    try:client.call('world.load',{'worldId':'org.hyprune.missing'})
    except RuntimeError:pass
    else:raise AssertionError('missing world unexpectedly loaded')
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    client.call('movement.set',{'modeId':'com.hyprune.walk'})
    ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');time.sleep(.25)
    # A hidden workspace must remain live under an explicit capture lease.
    windows=json.loads(ctl('clients','-j'));workspace=windows[0]['workspace']['id']
    before=camera()['captures'];assert before
    ctl('dispatch','hl.dsp.focus({ workspace = "99" })');time.sleep(.2)
    for i in range(8):
        with (STATE/'events.log').open('a') as stream:stream.write(f'hidden workspace liveness {i}\n')
        time.sleep(.08)
    after=camera()['captures']
    assert any(e['commits']>next(b['commits'] for b in before if b['id']==e['id']) for e in after),'hidden captures stopped committing'
    try:client.call('surface.focus',{'surfaceId':after[0]['id'],'intent':'keyboard'})
    except RuntimeError as error:assert error.args[0]['code']==-32007,error
    else:raise AssertionError('hidden workspace keyboard focus accepted')
    ctl('dispatch',f'hl.dsp.focus({{ workspace = "{workspace}" }})');time.sleep(.2)
    print('hidden workspace: captures live, keyboard focus safely rejected',flush=True)
    # Public read access cannot be acquired merely by claiming a client ID.
    anonymous=Client(str(socketpath));assert anonymous.hello['capabilities']==[]
    try:anonymous.call('state.subscribe',{'topics':['state']})
    except RuntimeError:pass
    else:raise AssertionError('anonymous state access')
    anonymous.close()
    # Kill only the helper parented by this verified nested compositor.
    children=pathlib.Path(f'/proc/{pid}/task/{pid}/children').read_text().split()
    helpers=[int(child) for child in children if pathlib.Path(f'/proc/{child}/comm').read_text().strip()=='hypruned']
    assert len(helpers)==1,helpers
    started=time.monotonic();os.kill(helpers[0],signal.SIGKILL)
    while camera()['active'] and time.monotonic()-started<1.2:time.sleep(.03)
    assert not camera()['active'],'daemon watchdog did not release world input'
    print('daemon death: desktop restored in',round(time.monotonic()-started,3),'s',flush=True)
    # Repeated unload/load catches retained pass elements, listener callbacks,
    # GL resources and NODELETE/static-state bugs. Fresh mapping every cycle.
    for i in range(3):
        assert ctl('plugin','unload',str(plugin))=='ok'
        next_plugin=STATE/f'reload-{i}-{time.time_ns()}.so';shutil.copy2(ROOT/'build/hyprune-core.so',next_plugin)
        assert ctl('plugin','load',str(next_plugin))=='ok';plugin=next_plugin;time.sleep(.15)
        ping=Client(str(socketpath));ping.call('session.ping',{});ping.close()
        ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');time.sleep(.4);assert camera()['active'];assert camera()['error']==''
    assert ctl('plugin','unload',str(plugin))=='ok'
    print('PASS: rejected reload, modes, anonymous denial, watchdog, 3 active unload/reload cycles',flush=True)

def record(client):
    gallery=pathlib.Path.home()/'workspace/rick-the-alien/worldshell-gallery/videos';gallery.mkdir(parents=True,exist_ok=True)
    output=gallery/'hyprune-core-m0.mp4';raw=STATE/'capture.mkv'
    # The previous smoke leaves desktop mode active. Capture only HEADLESS-1.
    log=open(STATE/'recorder.log','w')
    recorder=envrun('wf-recorder','-o','HEADLESS-1','-r','30','-D','--no-dmabuf','-c','libx264','-p','preset=ultrafast','-p','crf=20','-x','yuv420p','-f',str(raw),stdout=log,stderr=subprocess.STDOUT)
    started=time.monotonic()
    subscription=next(x['response']['result']['subscriptionId'] for x in client.transcript if x['method']=='state.subscribe')
    def sample():return client.call('state.snapshot',{'subscriptionId':subscription})['snapshot']['state']
    def pause(seconds):
        until=time.monotonic()+seconds
        while time.monotonic()<until:sample();time.sleep(.12)
    def type_keys(*args):subprocess.run([str(HERE/'ctl.py'),'env','wtype',*args],check=True)
    try:
        pause(2)
        ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');pause(2)
        type_keys('-P','w','-s','450','-p','w');pause(.5)
        type_keys('-P','a','-s','350','-p','a');pause(.5)
        # A short look around with relative pointer input, then face the workbench.
        for _ in range(8):subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer'),'12','0'],check=True);pause(.12)
        state=sample();surface=next(s for s in state['surfaces'] if 'workbench' in s['title']);index=next(i for i,s in enumerate(state['surfaces']) if s['id']==surface['id']);x=[0,-3.8,3.8][index]
        ctl('eval',f'hl.plugin.hyprune.camera({x}, 1.7, 1.2, 0, 0)');pause(.5)
        subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer')],check=True)
        type_keys('-d','55',"printf '\\nHello from inside Hyprune.\\nA live terminal, in a world.\\n'")
        type_keys('-k','Return');pause(2)
        type_keys('-k','Escape')
        ctl('eval','hl.plugin.hyprune.camera(0, 1.7, 4, 0, 0)');pause(1)
        # Show the second live surface and the real IPC stream beside it.
        for _ in range(10):subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer'),'-10','0'],check=True);pause(.12)
        pause(max(0,27-(time.monotonic()-started)))
        client.call('runtime.exit',{});pause(2)
    finally:
        recorder.send_signal(signal.SIGINT);recorder.wait(timeout=15);log.close()
    subprocess.run(['ffmpeg','-y','-loglevel','error','-i',str(raw),'-an','-vf','scale=1920:1080:flags=lanczos:in_range=pc:out_range=tv','-r','30','-c:v','libx264','-preset','medium','-crf','22','-pix_fmt','yuv420p','-color_range','tv','-movflags','+faststart',str(output)],check=True)
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(output)]))
    stream=probe['streams'][0];assert stream['codec_name']=='h264' and stream['pix_fmt']=='yuv420p' and stream['width']==1920 and stream['height']==1080
    assert 20<=float(probe['format']['duration'])<=40 and output.stat().st_size<40_000_000,probe['format']
    (gallery/'hyprune-core-m0.json').write_text(json.dumps({'title':'Hyprune core M0','tag':'core','notes':'Original CC0 atrium. Hyprland 0.56.2 nested session: toggle, capsule walking, live floating terminals, real pointer click and typing, authenticated IPC 0.1 state stream, return to desktop. GLES3 4x MSAA, linear lighting, tone mapping with preserved application colors. No real-session control or proprietary assets.'},indent=2)+'\n')
    (STATE/'video-probe.json').write_text(json.dumps(probe,indent=2));print('video:',output,output.stat().st_size,'bytes',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--serve',action='store_true');parser.add_argument('--record',action='store_true');args=parser.parse_args()
    run(args)
