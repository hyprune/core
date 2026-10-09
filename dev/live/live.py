#!/usr/bin/env python3
"""Supervised session swap. Normal commands select WAYLAND_DISPLAY, not inherited signature."""
import argparse, re, contextlib, fcntl, json, os, pathlib, secrets, shutil, signal, socket, struct, subprocess, sys, time
ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = ROOT / 'dev/live'
BUILD = ROOT / 'build/live'
SHELL = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', ROOT.parent / 'shell'))
DEFAULT_WORLD_ID = 'com.hyprune.lumen-reach'
GRANTS = ['state.read', 'world.control', 'surface.control', 'movement.control', 'shell.control', 'runtime.control', 'input.control']


def core_protocols(header=None):
    """Single inventory: C++ protocol helper. Numeric order, no duplicate Python pin."""
    text = (header or ROOT / 'src/ipc/Protocol.hpp').read_text()
    inventory = re.search(r'\bSupported\s*\{([^}]+)\}', text)
    if not inventory: raise RuntimeError('Cannot read core protocol inventory')
    versions = re.findall(r'"((?:0|[1-9]\d*)\.(?:0|[1-9]\d*))"', inventory[1])
    if not versions or len(versions) != len(set(versions)): raise RuntimeError('Invalid core protocol inventory')
    return sorted(versions, key=lambda value: tuple(map(int, value.split('.'))), reverse=True)


def shell_protocol(supported=None, maximum=None):
    # The installed shell may trail core. Read its actual validator inventory;
    # never start it with a protocol its bridge cannot parse.
    if supported is None:
        code = 'import sys,json;sys.path.insert(0,sys.argv[1]);from protocol import SUPPORTED_PROTOCOLS;print(json.dumps(SUPPORTED_PROTOCOLS))'
        supported = json.loads(run([sys.executable, '-c', code, str(SHELL / 'bridge')]))
    bound = tuple(map(int, maximum.split('.'))) if maximum else None
    match = next((version for version in core_protocols() if version in supported and
                  (bound is None or tuple(map(int, version.split('.'))) <= bound)), None)
    if not match: raise RuntimeError('Core and installed shell have no common protocol')
    return match


def run(argv, **kwargs):
    return subprocess.check_output(argv, text=True, timeout=kwargs.pop('timeout', 15), **kwargs).strip()


def atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    temp = path.with_suffix('.tmp')
    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w') as f:
        json.dump(data, f, indent=2); f.write('\n'); f.flush(); os.fsync(f.fileno())
    os.replace(temp, path)


def process(pid):
    try:
        p = pathlib.Path('/proc') / str(pid)
        stat = p.joinpath('stat').read_text().rsplit(')', 1)[1].split()
        if stat[0] == 'Z': return None
        return {'pid': int(pid), 'start': stat[19], 'argv': [os.fsdecode(x) for x in p.joinpath('cmdline').read_bytes().split(b'\0') if x],
                'cwd': str(p.joinpath('cwd').resolve()),
                'env': dict(os.fsdecode(x).split('=', 1) for x in p.joinpath('environ').read_bytes().split(b'\0') if b'=' in x)}
    except (OSError, ValueError): return None


def alive(record):
    current = process(record['pid']) if record else None
    return current and current['start'] == record['start']


def qs_processes(session):
    result = []
    for p in pathlib.Path('/proc').iterdir():
        if not p.name.isdigit(): continue
        rec = process(int(p.name))
        if not rec or not rec['argv'] or pathlib.Path(rec['argv'][0]).name not in ('qs', 'quickshell'): continue
        # Only the chosen Wayland session, never another compositor's HUD.
        if rec['env'].get('WAYLAND_DISPLAY') != session['wayland']: continue
        rec['oldHUD'] = any(rec['argv'][i] == '-p' and pathlib.Path(os.path.expanduser(rec['argv'][i+1])).resolve().as_posix().rstrip('/').endswith('/quickshell/hypr3d')
                            for i in range(len(rec['argv'])-1))
        result.append(rec)
    return result


def stop(rec, group=False):
    if not alive(rec): return
    send = lambda sig: os.killpg(rec['pid'], sig) if group else os.kill(rec['pid'], sig)
    send(signal.SIGTERM)
    for _ in range(50):
        if not alive(rec): return
        time.sleep(.1)
    if alive(rec): send(signal.SIGKILL)


def instances(runtime):
    env = {**os.environ, 'XDG_RUNTIME_DIR': str(runtime)}
    return json.loads(run(['hyprctl', 'instances', '-j'], env=env))


def select(args, saved=None, recovery=False):
    runtime = pathlib.Path(os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}'))
    if saved: runtime = pathlib.Path(saved['session']['runtime'])
    available = instances(runtime)
    if args.instance:
        choices = [s for s in available if s['instance'] == args.instance]
    elif recovery and saved:
        choices = [s for s in available if s['instance'] == saved['session']['signature']]
        if not choices: choices = [s for s in available if s['wl_socket'] == saved['session']['wayland']]
    else:
        display = os.environ.get('WAYLAND_DISPLAY')
        if not display: raise RuntimeError('WAYLAND_DISPLAY is required for preflight/up; use --instance for an explicit session')
        choices = [s for s in available if s['wl_socket'] == display]
    if len(choices) != 1: raise RuntimeError('No unique session socket matches; use hyprctl instances -j and --instance SIGNATURE')
    s = choices[0]; rec = process(s['pid'])
    sock = runtime / 'hypr' / s['instance'] / '.socket.sock'
    lock = sock.parent / 'hyprland.lock'
    lines = lock.read_text().splitlines()
    if not sock.is_socket() or not rec or int(lines[0]) != rec['pid'] or lines[1] != s['wl_socket']:
        raise RuntimeError('Session socket/PID identity check failed')
    return {'signature': s['instance'], 'wayland': s['wl_socket'], 'pid': s['pid'], 'start': rec['start'], 'runtime': str(runtime)}


def env_for(s):
    return {**os.environ, 'XDG_RUNTIME_DIR': s['runtime'], 'HYPRLAND_INSTANCE_SIGNATURE': s['signature'], 'WAYLAND_DISPLAY': s['wayland']}


def ctl(s, *args): return run(['hyprctl', '-i', s['signature'], *args], env=env_for(s), stderr=subprocess.STDOUT)
def query(s, *args): return json.loads(ctl(s, *args, '-j'))
def mutate(s, *args):
    out = ctl(s, *args)
    if out != 'ok': raise RuntimeError('hyprctl '+args[0]+': '+out)
def lua(s, code): mutate(s, 'eval', code)
def q(value): return json.dumps(str(value))


def plugin_paths(s):
    # Hyprland 0.56 plugin list omits paths; /proc maps and reload.sh's pointer
    # provide candidates. up confirms the exact loaded path via unload's result.
    paths = []
    loaded = pathlib.Path(s['runtime']) / 'hypr3d/loaded'
    preferred = loaded.read_text().strip() if loaded.is_file() else None
    for line in pathlib.Path(f"/proc/{s['pid']}/maps").read_text().splitlines():
        path = line.split(maxsplit=5)[-1]
        if path.startswith('/') and 'hypr3d' in pathlib.Path(path).name.lower() and path.endswith('.so'): paths.append(path)
    paths = list(dict.fromkeys(p for p in paths if pathlib.Path(p).is_file()))
    return ([preferred] if preferred in paths else []) + [p for p in paths if p != preferred]


def choose_bind(binds):
    # binds -j omits ignoreMods in 0.56: reserve matching keys across all
    # modifier masks/submaps instead of guessing. Include XKB and evdev codes.
    for key, code in [('H', 43), ('N', 57), ('F10', 76), ('F11', 95), ('F8', 74)]:
        if not any(b.get('key', '').lower() == key.lower() or b.get('keycode') in (code, code-8) or
                   b.get('key') in ('code:'+str(code), 'code:'+str(code-8)) or b.get('multiKey') or
                   '&' in b.get('key', '') or b.get('catch_all') for b in binds):
            return 'SUPER + SHIFT + '+key
    raise RuntimeError('No safe toggle chord among Super+Shift+H/N/F10/F11/F8')


def world():
    return ROOT.parent / 'worlds/dist' / DEFAULT_WORLD_ID


def world_packages(paths=None):
    """Validate all packages before any session changes; directory names need not match IDs."""
    packages=[]
    for raw in paths or [world()]:
        package=pathlib.Path(raw).expanduser().resolve()
        manifest=package/'world.json'
        if not manifest.is_file(): raise RuntimeError('Missing world package manifest: '+str(manifest))
        value=json.loads(manifest.read_text())
        ident=value.get('id') if isinstance(value,dict) else None
        if not isinstance(ident,str) or not re.fullmatch(r'[a-z0-9][a-z0-9.-]{0,127}',ident) or ident in ('.','..'):
            raise RuntimeError('Invalid world package ID: '+str(manifest))
        if any(p['id']==ident for p in packages): raise RuntimeError('Duplicate world ID: '+ident)
        try: run([str(BUILD/'hyprune-world-check'),str(package)],timeout=60,stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as error:
            raise RuntimeError('Invalid world package '+str(package)+': '+error.output.strip()) from error
        packages.append({'id':ident,'name':value.get('name',ident),'source':str(package)})
    return packages


def print_worlds(packages):
    print('Selected worlds (first starts):')
    for index,package in enumerate(packages):
        print(' ', '[start]' if index==0 else '       ', package['id'], '—', package['name'], '—', package['source'])


def shell_world_args(packages):
    return [arg for package in packages for arg in ('--world-package',str(pathlib.Path(package['installed'])/'world.json'))]


def abi(s):
    actual = query(s, 'version')
    built = json.loads(run([str(BUILD / 'hyprune-live-abi')]))
    if actual.get('commit') != built['commit'] or actual.get('abiHash') != built['abiHash'] or actual.get('tag') != built['tag']:
        raise RuntimeError(f'ABI/version mismatch: session {actual.get("tag")} {actual.get("abiHash")}; build {built}')
    return actual


def verify(s, paths=None):
    for file in [BUILD/'hyprune-core.so', BUILD/'hypruned', BUILD/'hyprune-renderer', BUILD/'hyprune-live-abi', BUILD/'hyprune-world-check', SHELL/'bridge/launch.py', SHELL/'qml/shell.qml']:
        if not file.is_file(): raise RuntimeError('Missing '+str(file)+'; run preflight')
    if not shutil.which('qs'): raise RuntimeError('qs is missing')
    packages = world_packages(paths); version = abi(s)
    plugins = query(s, 'plugin', 'list')
    if any('hyprune' in x['name'].lower() for x in plugins): raise RuntimeError('Hyprune already loaded; run down for this test or unload it explicitly first')
    olds = [x for x in plugins if 'hypr3d' in x['name'].lower()]
    candidates = plugin_paths(s) if olds else []
    if olds and not candidates: raise RuntimeError('Cannot recover the old plugin path; refusing the swap')
    return packages, version, plugins, candidates


def preflight(args):
    s = select(args)
    print('Selected:', s['signature'], s['wayland'], 'PID', s['pid'], flush=True)
    # Local build only. No compositor mutation or config reload.
    subprocess.run(['cmake', '-S', str(ROOT), '-B', str(BUILD), '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', '-DHYPRUNE_LIVE_BUILD=ON',
                    '-DHYPRUNE_DAEMON_EXECUTABLE='+str(HERE/'daemon')], check=True)
    subprocess.run(['cmake', '--build', str(BUILD), '-j', '2'], check=True)
    packages, version, plugins, candidates = verify(s, args.world)
    run([sys.executable,'-c','import sys; sys.path.insert(0, sys.argv[1]); import launch',str(SHELL/'bridge')])
    print('IPC newest:', core_protocols()[0], '; installed shell:', shell_protocol())
    print('ABI/version matches:', version['tag'], version['abiHash'])
    print_worlds(packages)
    print('Shell:', SHELL/'bridge/launch.py', '\nHelper:', BUILD/'hypruned')
    print('Loaded plugins:', json.dumps(plugins, indent=2))
    print('Old-plugin candidate paths:', candidates)
    print('Quickshell instances:', [(r['pid'], r['argv'], 'STOP old HUD' if r['oldHUD'] else 'KEEP') for r in qs_processes(s)])
    print('Toggle candidate:', choose_bind(query(s, 'binds')), '(rechecked after old-plugin unload)')
    print('up will: record private recovery state; unload only hypr3d; stop only its -p .../quickshell/hypr3d HUD; copy/load core from a unique path;\n'
          'start its private helper and detached logged shell with in-memory grants; install all selected worlds and load the first without activating it; add one free runtime toggle.\n'
          'Core switches the chosen output to its configured world workspace (default 8), floating new room clients before layout; exit restores the prior workspace.\n'
          'Core materializes default input.json in a private runtime test directory. No installed config is edited.\n'
          'Hyprland itself re-evaluates Lua on plugin load/unload; no explicit reload command is issued.\n'
          'down removes that bind, exits/shows cursor after the frame, unloads core, stops only recorded test processes, restores old plugin/HUD.')


def camera(s, root):
    path = root / 'camera.json'
    lua(s, 'local f=assert(io.open('+q(path)+',"w")); f:write(hl.plugin.hyprune.camera()); f:close()')
    return json.loads(path.read_text())


def state_path(args):
    return pathlib.Path(args.state_dir or pathlib.Path(os.environ.get('XDG_STATE_HOME', str(pathlib.Path.home()/'.local/state'))) / 'hyprune/live') / 'state.json'


def shell_bootstrapped(text):
    connection=None
    presentation=False
    for line in text.splitlines():
        try: event=json.loads(line)
        except ValueError: continue
        if not isinstance(event,dict): continue
        if event.get('type')=='connection': connection=event.get('status')
        if event.get('type')=='presentation.peer': presentation=True
    return connection=='live' and presentation


def ready_message(chord):
    labels = {'SUPER':'Super', 'SHIFT':'Shift', 'CTRL':'Ctrl', 'ALT':'Alt'}
    display = '+'.join(labels.get(part.strip().upper(), part.strip()) for part in chord.split('+'))
    return 'Hyprune ready · press '+display+' to enter'


def up(args, path):
    if path.exists() and json.loads(path.read_text()).get('phase') != 'down': raise RuntimeError('Unfinished live test; run down first')
    s = select(args); packages, version, plugins, candidates = verify(s, args.world)
    root = pathlib.Path(s['runtime']) / 'hyprune-live' / s['signature']
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    logs = path.parent; logs.mkdir(parents=True, exist_ok=True, mode=0o700)
    runroot = root / str(time.time_ns()); runroot.mkdir(mode=0o700)
    installed = runroot / 'worlds'; installed.mkdir()
    for package in packages:
        target=installed/package['id']; shutil.copytree(package['source'],target)
        package['installed']=str(target)
    copy = runroot / 'hyprune-core.so'; shutil.copy2(BUILD/'hyprune-core.so', copy)
    state = {'phase':'starting', 'worlds':packages, 'session':s, 'version':version, 'plugins':plugins, 'qs':qs_processes(s), 'oldCandidates':candidates, 'oldUnloaded':[],
             'runroot':str(runroot), 'plugin':str(copy), 'binds':[], 'logs':str(logs), 'since':time.time(), 'processes':[], 'restoredHUD':{}, 'crashDirectory':str(pathlib.Path(os.environ.get('XDG_CACHE_HOME',str(pathlib.Path.home()/'.cache')))/'hyprland')}
    atomic(path, state)
    settings = {'pid':s['pid'], 'state':str(path), 'daemon':str(BUILD/'hypruned'), 'environment':{
        'HYPRUNE_CAPTURE_DIR':str(path.parent.parent/'captures'), 'HYPRUNE_WORLD_DIR':str(installed), 'HYPRUNE_INPUT_FILE':str(runroot/'input.json'), 'HYPRUNE_EDITOR_FILE':str(runroot/'editor.json'),
        'HYPRUNE_LAYOUT_FILE':str(runroot/'layout.json'), 'HYPRUNE_LIVE_ROOT':str(root), 'HYPRUNE_WORKSPACE_LOCATIONS':None}}
    atomic(root/'bootstrap.json', settings)
    try:
        for candidate in candidates:
            state['pendingOld'] = candidate; atomic(path,state)
            out = ctl(s, 'plugin', 'unload', candidate)
            if out == 'ok': state['oldUnloaded'].append(candidate)
            elif out != 'plugin not loaded': raise RuntimeError(out)
            state.pop('pendingOld',None); atomic(path,state)
        time.sleep(.5) # Let Hyprland's implicit lifecycle config evaluation finish.
        if any('hypr3d' in x['name'].lower() for x in query(s,'plugin','list')): raise RuntimeError('Old plugin still loaded; refusing coexistence')
        for rec in state['qs']:
            if rec['oldHUD']:
                rec['stopRequested']=True; atomic(path,state); stop(rec)
        log=open(logs/'launcher.log','a')
        child=subprocess.Popen([sys.executable,str(HERE/'live.py'),'bootstrap','--state-dir',str(logs)], env=env_for(s), start_new_session=True, stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        state['bootstrap']=process(child.pid); atomic(path,state)
        wait(lambda:(root/'grants.sock').is_socket(), 8)
        mutate(s,'plugin','load',str(copy))
        wait(lambda:(runroot/'ready.json').exists(),40)
        ready=json.loads((runroot/'ready.json').read_text());state['processes']=ready['processes'];state['phase']='up';atomic(path,state)
        if any('hypr3d' in p['name'].lower() for p in query(s,'plugin','list')):
            raise RuntimeError('Old plugin reappeared during load; refusing coexistence')
        state['binds']=[{'chord':choose_bind(query(s,'binds')), 'description':'Hyprune supervised live toggle'}];atomic(path,state)
        bind=state['binds'][0]
        lua(s,'hl.bind('+q(bind['chord'])+', hl.plugin.hyprune.toggle, {description='+q(bind['description'])+'})')
        message = ready_message(bind['chord'])
        ctl(s, 'notify', '-1', '7000', 'rgb(66bbdd)', message)
        print(message)
        print_worlds(packages)
        print('UP:',s['signature'],'starting world',packages[0]['id'],'; toggle',bind['chord'],'\nRecovery state:',path,'\nLogs:',logs)
    except BaseException:
        print('up failed; attempting recorded rollback',file=sys.stderr)
        with contextlib.suppress(Exception): down(args,path)
        raise


def wait(test, seconds=8):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        value=test()
        if value:return value
        time.sleep(.1)
    raise RuntimeError('Timed out waiting for live bootstrap; inspect status/logs')


class RPC:
    def __init__(self,path,token):
        self.s=socket.socket(socket.AF_UNIX); self.s.settimeout(35);self.s.connect(str(path));self.n=0
        self.hello=self.call('session.hello',{'protocols':core_protocols(),'client':{'id':'org.hyprune.live','name':'Supervised live launcher'},'capabilities':GRANTS,'token':token})
    def read(self,n):
        b=b''
        while len(b)<n:
            piece=self.s.recv(n-len(b))
            if not piece:raise RuntimeError('Core IPC closed')
            b+=piece
        return b
    def call(self,method,params):
        self.n+=1; ident=str(self.n); data=json.dumps({'jsonrpc':'2.0','id':ident,'method':method,'params':params}).encode();self.s.sendall(struct.pack('>I',len(data))+data)
        while True:
            response=json.loads(self.read(struct.unpack('>I',self.read(4))[0]))
            if response.get('id')!=ident:continue
            if 'error' in response:raise RuntimeError(response['error'])
            return response['result']


def bootstrap(args,path):
    from credentials import CredentialBroker
    state=json.loads(path.read_text());s=state['session']; root=pathlib.Path(s['runtime'])/'hyprune-live'/s['signature'];runroot=pathlib.Path(state['runroot'])
    tokens={name:secrets.token_hex(32) for name in ['org.hyprune.live','com.hyprune.shell']}
    server=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET);sockpath=root/'grants.sock';sockpath.unlink(missing_ok=True);server.bind(str(sockpath));os.chmod(sockpath,0o600);server.listen(1);server.settimeout(20)
    try:
        conn,_=server.accept()
        pid,uid,_=struct.unpack('3i',conn.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
        parent=int(pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[1])
        if uid!=os.getuid() or parent!=s['pid']:raise RuntimeError('Grant recipient is not this compositor helper')
        conn.send(json.dumps([{'token':token,'client':name,'capabilities':GRANTS} for name,token in tokens.items()]).encode())
    finally:server.close();sockpath.unlink(missing_ok=True)
    sock=pathlib.Path(s['runtime'])/'hyprune'/s['signature']/'control.sock';wait(sock.is_socket)
    rpc=RPC(sock,tokens['org.hyprune.live']);rpc.call('world.load',{'worldId':state['worlds'][0]['id']})
    renew,recipient=socket.socketpair(socket.AF_UNIX,socket.SOCK_STREAM)
    renew.setsockopt(socket.SOL_SOCKET,socket.SO_PASSCRED,1)
    read=recipient.fileno()
    protocol=shell_protocol(maximum=rpc.hello["protocol"])
    print(f'IPC: launcher {rpc.hello["protocol"]}; shell {protocol}',flush=True)
    env=env_for(s);env.update(HYPRUNE_TOKEN_FD=str(read),HYPRUNE_TOKEN_REFRESH="1",HYPRUNE_PROTOCOL=protocol,HYPRUNE_SOCKET=str(sock))
    log=open(path.parent/'shell.log','a')
    log_start=log.tell()
    child=subprocess.Popen([sys.executable,str(SHELL/'bridge/launch.py'),'--protocol',protocol,*shell_world_args(state['worlds'])],env=env,pass_fds=(read,),start_new_session=True,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT);recipient.close()
    broker=CredentialBroker(renew,conn,child.pid)
    # Persist identity immediately: down can recover even before ready.
    atomic(runroot/'processes.json',[{'record':process(child.pid),'group':True}])
    def shell_ready():
        if not broker.poll(.05):raise RuntimeError('Shell credential channel closed')
        if child.poll() is not None:raise RuntimeError('Hyprune shell exited; see shell.log')
        # Inactive shells intentionally hold no output lease. Verify this launch's
        # authenticated subscription and presentation connection without entering.
        with (path.parent/'shell.log').open() as stream:
            stream.seek(log_start)
            return shell_bootstrapped(stream.read())
    wait(shell_ready,15)
    procs=json.loads((runroot/'processes.json').read_text())
    childpids=pathlib.Path(f'/proc/{child.pid}/task/{child.pid}/children').read_text().split()
    procs += [{'record':process(int(pid)),'group':False} for pid in childpids if process(int(pid))]
    atomic(runroot/'processes.json',procs)
    atomic(runroot/'ready.json',{'processes':procs});rpc.s.close()
    print('Bootstrap ready; authenticated shell and presentation verified',flush=True)
    try:
        while child.poll() is None and broker.poll():pass
    finally:
        renew.close();conn.close()
        print('Credential supervisor stopped',flush=True)


def daemon():
    # Bridge FD3 is inherited; obtain grants over a one-shot, credential-checked
    # socket. Tokens remain in memory and inherited pipes, never argv/state/logs.
    root=pathlib.Path(os.environ['HYPRUNE_LIVE_ROOT']);settings=json.loads((root/'bootstrap.json').read_text())
    fd=os.open(pathlib.Path(settings['state']).parent/'hypruned.log',os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600)
    os.dup2(fd,1);os.dup2(fd,2);os.close(fd)
    server=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET);server.connect(str(root/'grants.sock'));data=server.recv(8192);os.set_inheritable(server.fileno(),True)
    read,write=os.pipe();os.write(write,data);os.close(write);os.set_inheritable(read,True)
    env={**os.environ,'HYPRUNE_LAUNCH_FD':str(read),'HYPRUNE_GRANT_FD':str(server.fileno())}
    rec=process(os.getpid());atomic(pathlib.Path(settings['state']).parent/'helper.json',rec)
    os.execve(settings['daemon'],[settings['daemon']],env)


def down(args,path):
    if not path.exists():print('DOWN: no recorded test');return
    state=json.loads(path.read_text())
    if state['phase']=='down':print('DOWN: already restored');return
    runroot=pathlib.Path(state['runroot']); errors=[]
    try:s=select(args,state,recovery=True)
    except Exception as e:s=None;errors.append(str(e))
    same=s and s['signature']==state['session']['signature'] and s['start']==state['session']['start']
    core_loaded=False
    if s:
        try:core_loaded=any('hyprune' in p['name'].lower() for p in query(s,'plugin','list'))
        except Exception as e:s=None;errors.append(str(e))
    if core_loaded and same:
        try:
            lua(s,'hl.plugin.hyprune.live_stop()')
            wait(lambda:not camera(s,runroot)['active'] and camera(s,runroot)['nativeCursorVisible'],3)
            mutate(s,'plugin','unload',state['plugin'])
        except Exception as e:errors.append(str(e))
    elif core_loaded:errors.append('A different session owns Hyprune; refusing to unload it')
    # Kill only recorded identities; daemon is owned by core, shell owns a new group.
    procs=list(state.get('processes',[]))
    if (runroot/'processes.json').exists():procs+=json.loads((runroot/'processes.json').read_text())
    if state.get('bootstrap'):procs.append({'record':state['bootstrap'],'group':False})
    helper=path.parent/'helper.json'
    if helper.exists():procs.append({'record':json.loads(helper.read_text()),'group':False})
    for p in procs:
        try:stop(p['record'],p['group'])
        except Exception as e:errors.append(str(e))
    if s:
        for bind in state['binds']:
            # Unbind only our description; a user replacement is preserved.
            current=query(s,'binds')
            if any(b.get('description')==bind['description'] for b in current):
                try:lua(s,'hl.unbind('+q(bind['chord'])+')')
                except Exception as e:errors.append(str(e))
        if not any('hyprune' in p['name'].lower() for p in query(s,'plugin','list')):
            restore=list(state['oldUnloaded'])
            if state.get('pendingOld') and not restore:restore.append(state['pendingOld'])
            if not any('hypr3d' in p['name'].lower() for p in query(s,'plugin','list')):
                for plugin in restore:
                    try:mutate(s,'plugin','load',plugin)
                    except Exception as e:errors.append(str(e))
            for rec in state['qs']:
                if not rec.get('stopRequested'):continue
                previous=state['restoredHUD'].get(str(rec['pid']))
                if alive(rec) or alive(previous):continue
                try:
                    environment=rec['env'].copy();environment.update(WAYLAND_DISPLAY=s['wayland'],HYPRLAND_INSTANCE_SIGNATURE=s['signature'],XDG_RUNTIME_DIR=s['runtime'])
                    child=subprocess.Popen(rec['argv'],cwd=rec['cwd'],env=environment,start_new_session=True,stdin=subprocess.DEVNULL,
                                           stdout=open(path.parent/'old-hud.log','a'),stderr=subprocess.STDOUT)
                    state['restoredHUD'][str(rec['pid'])]=process(child.pid);atomic(path,state)
                except Exception as e:errors.append(str(e))
    if errors:
        state['phase']='recovery-pending';state['errors']=errors;atomic(path,state)
        raise RuntimeError('Recovery pending: '+'; '.join(errors)+'. Run down again after the compositor is available.')
    state['phase']='down';state.pop('errors',None);atomic(path,state)
    # Each up copies its worlds and plugin into the runtime tmpfs; release them once restored.
    shutil.rmtree(runroot/'worlds',ignore_errors=True);(runroot/'hyprune-core.so').unlink(missing_ok=True)
    print('DOWN: core stopped, runtime bind removed, old plugin/HUD restored; cursor reset was deferred.')


def status(args,path):
    state=json.loads(path.read_text()) if path.exists() else None
    print('State:',path,'phase:',state['phase'] if state else 'none')
    try:
        s=select(args,state,recovery=True) if state else select(args)
        print('Session:',s['signature'],s['wayland']);print('Plugins:',json.dumps(query(s,'plugin','list'),indent=2))
        print('Quickshell:',[(r['pid'],r['argv']) for r in qs_processes(s)])
    except Exception as e:print('Session unavailable:',e)
    if state:
        if state.get('worlds'): print_worlds(state['worlds'])
        else: print('Selected worlds: not recorded by this older launcher')
        helper=path.parent/'helper.json'
        print('Helper:',alive(json.loads(helper.read_text())) if helper.exists() else False)
        for log in sorted(path.parent.glob('*.log')):
            print('\n'+str(log)+' (last 20 lines)'); print('\n'.join(log.read_text(errors='replace').splitlines()[-20:]))
        cache=pathlib.Path(state.get('crashDirectory',str(pathlib.Path.home()/'.cache/hyprland')))
        print('New crash reports:',[str(p) for p in cache.glob('hyprlandCrashReport*') if p.stat().st_mtime>=state['since']])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('command',choices=['preflight','up','down','status','bootstrap','daemon']);parser.add_argument('--instance');parser.add_argument('--state-dir')
    parser.add_argument('--world',action='append',metavar='PACKAGE_DIR',help='world package directory; repeat to populate Places, first starts (default: Lumen Reach)')
    args=parser.parse_args()
    if args.world and args.command not in ('preflight','up'): parser.error('--world is only valid with preflight/up')
    if args.command=='daemon':return daemon()
    path=state_path(args)
    if args.command=='bootstrap':return bootstrap(args,path)
    if args.command=='preflight':return preflight(args)
    if args.command=='status':return status(args,path)
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    with open(path.parent/'operation.lock','a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        return up(args,path) if args.command=='up' else down(args,path)

if __name__=='__main__':
    try:main()
    except (Exception,KeyboardInterrupt) as e:sys.exit('live: '+str(e))
