#!/usr/bin/env python3
import argparse, json, os, pathlib, secrets, shutil, signal, socket, struct, subprocess, sys, time
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parent.parent;STATE=HERE/'.state'
sys.path.insert(0,str(HERE))
from ctl import verified
class Client:
    def __init__(self,path,token=None):
        self.socket=socket.socket(socket.AF_UNIX);self.socket.settimeout(5);self.socket.connect(path);self.count=0;self.events=[]
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
                if 'error' in result:raise RuntimeError(result['error'])
                return result['result']
            self.events.append(result)
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
        print('load:',ctl('plugin','load',str(copy)),flush=True);ctl('reload')
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
    finally:
        if client:client.close()
        for p in children:
            if p.poll() is None:p.terminate()
        if process.poll() is None:
            process.terminate()
            try:process.wait(timeout=8)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        (STATE/'session.json').unlink(missing_ok=True)
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
    for event in client.events:
        with (STATE/'events.log').open('a') as stream:stream.write(json.dumps(event)+'\n')
    assert any(e.get('method')=='state.delta' for e in client.events)
    client.call('runtime.exit',{});time.sleep(.3);assert not camera()['active']
    print('PASS: load, hello, snapshot, toggle, render, walk, keyboard routing, events, exit',flush=True)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--serve',action='store_true');parser.add_argument('--record',action='store_true');args=parser.parse_args()
    run(args)
