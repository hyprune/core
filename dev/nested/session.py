#!/usr/bin/env python3
import argparse, json, math, os, pathlib, re, secrets, shutil, signal, socket, struct, subprocess, sys, time
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parent.parent
# Isolated instances (isolated.py) keep every .*-state dir under their own root.
STATE_ROOT=pathlib.Path(os.environ.get('HYPRUNE_NESTED_STATE_ROOT',str(HERE)));STATE=STATE_ROOT/'.state'
sys.path.insert(0,str(HERE));sys.path.insert(1,str(HERE.parent))
# Keep dynamically imported integration helpers on this exact harness module.
sys.modules.setdefault('session',sys.modules[__name__])
from ctl import verified
import devenv
def video_dir():
    """Finished demo videos: HYPRUNE_VIDEO_DIR (environment or ~/.config/hyprune/dev.env)."""
    return devenv.video_dir()
class Client:
    def __init__(self,path,token=None,protocol="0.1",client_id="org.hyprune.nested"):
        self.socket=socket.socket(socket.AF_UNIX);self.socket.settimeout(5);self.socket.connect(path);self.count=0;self.events=[];self.transcript=[]
        params={'protocols':[protocol],'client':{'id':client_id,'name':'Nested test'},'capabilities':['state.read','world.control','surface.control','movement.control','runtime.control']}
        version=tuple(map(int,protocol.split('.')))  # numeric feature floors, including future test versions
        if version >= (0,2):params['capabilities']+=['shell.control','data.publish']
        if version >= (0,6):params['capabilities']+=['input.control']
        if version >= (0,13):params['capabilities']+=['camera.author']
        if version >= (0,21):params['capabilities']+=['quest.control']
        if version >= (0,22):params['capabilities']+=['objects.control']
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
        # Core gives a package load 28 s (loadDeadline) before failing it; giving up sooner
        # disconnects and cancels a load that was still progressing (a busy host, big package).
        self.socket.settimeout(32 if method=='world.load' else 5)
        try:
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
        finally:self.socket.settimeout(5)
    def close(self):self.socket.close()
def library_of(archive,fallback):
    """'<id>@<version>' from a library .tar.zst's library.json (fallback if the archive is missing)."""
    import tarfile
    if not pathlib.Path(archive).is_file():return fallback
    with tarfile.open(archive,'r:zst') as t:
        for m in t.getmembers():
            if m.name.count('/')==1 and m.name.endswith('/library.json'):
                j=json.load(t.extractfile(m));return f"{j['id']}@{j['version']}"
    return fallback

def helper_connected(process,log,timeout=25):
    """Wait for the source helper's first connection (its log line) before anything that blocks
    core's loop, such as a world load: the launch token is one-use, so a hello that times out
    while core is busy and is answered later leaves the helper's reconnects refused."""
    deadline=time.monotonic()+timeout
    while 'connected to core' not in pathlib.Path(log).read_text(errors='replace'):
        if process.poll() is not None or time.monotonic()>deadline:
            raise AssertionError('source helper did not connect: '+pathlib.Path(log).read_text(errors='replace')[-600:])
        time.sleep(.1)
    return process

def starter_archive(env):
    """The starter library .tar.zst: $env if set, else the newest tools/library-pack output in the
    worlds checkout (build/libraries); a missing path the caller reports."""
    if os.environ.get(env):return pathlib.Path(os.environ[env])
    built=sorted((ROOT.parent/'worlds/build/libraries').glob('com.hyprune.starter-*.tar.zst'),
                 key=lambda a:[int(n) for n in re.findall(r'\d+',a.name.rsplit('-',1)[1])])
    return built[-1] if built else ROOT.parent/'worlds/build/libraries/com.hyprune.starter-<version>.tar.zst'

def library_object_count(archive,fallback):
    """How many objects a library .tar.zst's library.json lists (fallback if the archive is missing)."""
    import tarfile
    if not pathlib.Path(archive).is_file():return fallback
    with tarfile.open(archive,'r:zst') as t:
        for m in t.getmembers():
            if m.name.count('/')==1 and m.name.endswith('/library.json'):
                return len(json.load(t.extractfile(m))['objects'])
    return fallback

def ctl(*args):
    return subprocess.check_output([str(HERE/'ctl.py'),'hyprctl',*args],text=True).strip()
def envrun(*args,**kwargs):
    return subprocess.Popen([str(HERE/'ctl.py'),'env',*args],**kwargs)
def camera():
    path=STATE/'camera.json'
    ctl('eval',f'local f = assert(io.open({json.dumps(str(path))}, "w")); f:write(hl.plugin.hyprune.camera()); f:close()')
    return json.loads(path.read_text())

def nvidia_only():
    drivers={os.path.basename(os.path.realpath(p/'device/driver')) for p in pathlib.Path('/sys/class/drm').glob('renderD*')}
    return bool(drivers) and drivers<={'nvidia'}

def pin_mesa_egl():
    """Nested Hyprland and its clients use Mesa (the iGPU), not the NVIDIA EGL
    vendor glvnd prefers. Offload tests opt out (HYPRUNE_NESTED_ALLOW_DGPU=1)."""
    if os.environ.get('HYPRUNE_NESTED_ALLOW_DGPU')=='1' or '__EGL_VENDOR_LIBRARY_FILENAMES' in os.environ:return
    if nvidia_only():return  # Mesa has no driver for any node; keep glvnd's choice
    for d in ['/usr/share/glvnd/egl_vendor.d','/etc/glvnd/egl_vendor.d','/run/opengl-driver/share/glvnd/egl_vendor.d']:
        if (pathlib.Path(d)/'50_mesa.json').is_file():
            os.environ['__EGL_VENDOR_LIBRARY_FILENAMES']=str(pathlib.Path(d)/'50_mesa.json');return

def state_dir(name):
    """A harness script's own state directory: it runs an m3b-style session but never shares
    .m3b-state (bindings, editor and graphics files left there broke the next --m3b)."""
    return STATE_ROOT/f'.{name}-state'

def run(args):
    global STATE
    pin_mesa_egl()
    if getattr(args,'ipc_warp',False): args.m3b=True
    if getattr(args,'playable',False): args.m3b=True
    if getattr(args,'workspace_homes',False): args.m3b=True
    if getattr(args,'long_titles',False): args.m3b=True
    if getattr(args,'windows',False): args.m3b=True;args.state='windows'
    if getattr(args,'live_findings',False): args.m3b=True
    if getattr(args,'placement',False): args.m3b=True
    if getattr(args,'reticle',False): args.m3b=True
    if getattr(args,'ink',False): args.authoring=True
    if getattr(args,'watermark',False): args.authoring=True
    if getattr(args,'perf',False):
        STATE=STATE_ROOT/'.perf-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'autodrive',False):
        STATE=STATE_ROOT/'.autodrive-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'static_world',False):
        STATE=STATE_ROOT/'.static-world-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'objects',False):
        STATE=STATE_ROOT/'.objects-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'gizmo',False):
        STATE=STATE_ROOT/'.gizmo-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'debug_tool',False):
        STATE=STATE_ROOT/'.debug-tool-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'feel',False):
        STATE=STATE_ROOT/'.feel-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'npcs',False):
        STATE=STATE_ROOT/'.npcs-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'companion',False):
        STATE=STATE_ROOT/'.companion-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'spawn_spots',False):
        STATE=STATE_ROOT/'.spawn-spots-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'lock',False):
        STATE=STATE_ROOT/'.lock-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'screens',False):
        STATE=STATE_ROOT/'.screens-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'screens_shell',False):
        STATE=STATE_ROOT/'.screens-shell-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'import_shell',False):
        STATE=STATE_ROOT/'.import-shell-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'builder_shell',False):
        STATE=STATE_ROOT/'.builder-shell-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'follow_corridor',False):
        STATE=STATE_ROOT/'.follow-corridor-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'route_carry',False):
        STATE=STATE_ROOT/'.route-carry-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'haze_depth',False):
        STATE=STATE_ROOT/'.haze-depth-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'machines',False):
        STATE=STATE_ROOT/'.machines-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'gauges',False):
        STATE=STATE_ROOT/'.gauges-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'data_sources',False):
        STATE=STATE_ROOT/'.data-sources-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'handover',False):
        STATE=STATE_ROOT/'.handover-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'capture_pose',False):
        STATE=STATE_ROOT/'.capture-pose-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'waypoint',False):
        STATE=STATE_ROOT/'.waypoint-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'live_take',False):
        STATE=STATE_ROOT/'.live-take-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'quests',False):
        STATE=STATE_ROOT/'.quests-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'catalog',False):
        STATE=STATE_ROOT/'.catalog-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if not hasattr(args,'authoring'): args.authoring=False
    if args.authoring: args.m3b=True
    if not hasattr(args,'climbing'): args.climbing=False
    if not hasattr(args,'integration'): args.integration=False
    if args.integration:
        STATE=STATE_ROOT/'.integration-state'
        # Prior Lumen acceptance can leave workspace bindings in this test directory.
        # First-session integration starts from its authored Switchyard locations.
        (STATE/'layout.json').unlink(missing_ok=True)
        os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if args.climbing:
        STATE=STATE_ROOT/'.climbing-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if args.m3b:
        STATE=STATE_ROOT/'.m3b-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    elif args.m3a:
        STATE=STATE_ROOT/'.m3a-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    elif args.m2:
        STATE=STATE_ROOT/'.m2-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'watermark',False):
        STATE=STATE_ROOT/'.watermark-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'reticle',False):
        STATE=STATE_ROOT/'.reticle-state';os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    if getattr(args,'state',None):
        STATE=state_dir(args.state);os.environ['HYPRUNE_NESTED_STATE_DIR']=str(STATE)
    elif args.m3a or args.m3b or getattr(args,'handover',False):
        # Local runs keep dev/nested/.<mode>-state across runs: start without the previous run's
        # homes, bindings, editor, graphics and keymap (an area bound earlier failed --m3b).
        for name in ('layout.json','editor.json','graphics.json','input.json'):(STATE/name).unlink(missing_ok=True)
    STATE.mkdir(exist_ok=True);os.chmod(STATE,0o700)
    # Player settings (IPC 0.19) start from defaults in every run.
    (STATE/'player.json').unlink(missing_ok=True)
    if getattr(args,'placement',False) or getattr(args,'reticle',False) or getattr(args,'handover',False) or getattr(args,'playable',False) or getattr(args,'workspace_homes',False) or getattr(args,'long_titles',False) or getattr(args,'live_findings',False) or getattr(args,'live_take',False) or getattr(args,'windows',False): (STATE/'layout.json').unlink(missing_ok=True)
    if args.m3b:
        (STATE/'input.json').unlink(missing_ok=True);(STATE/'editor.json').unlink(missing_ok=True)
        for name in ['acceptance.json','transcript.json','notifications.json']:
            (STATE/name).unlink(missing_ok=True)
        if args.record:
            (STATE/'video-temp.json').unlink(missing_ok=True)
    # Fixtures are regenerated in place; parallel isolated instances take turns.
    import fcntl;prepare_lock=open(ROOT/'tests/fixtures/.prepare.lock','w');fcntl.flock(prepare_lock,fcntl.LOCK_EX)
    subprocess.run([sys.executable,str(ROOT/'tests/fixtures/generate.py')],check=True,stdout=subprocess.DEVNULL)
    installed=STATE/'worlds';installed.mkdir(exist_ok=True)
    package=installed/'org.hyprune.atrium'
    if package.exists():shutil.rmtree(package)
    shutil.copytree(ROOT/'tests/fixtures/atrium',package)
    if args.climbing:
        subprocess.run([sys.executable,str(ROOT/'tests/fixtures/climbing.py')],check=True)
        shutil.copytree(ROOT/'tests/fixtures/climbing',installed/'org.hyprune.climbing',dirs_exist_ok=True)
    if args.m2 or args.m3a or args.m3b:
        subprocess.run([sys.executable,str(ROOT/'tests/fixtures/m2.py')],check=True,stdout=subprocess.DEVNULL)
        shutil.copytree(ROOT/'tests/fixtures/m2',installed/'org.hyprune.m2',dirs_exist_ok=True)
        # A second world exercises the warp lifecycle and persisted cross-world bindings.
        remote=installed/'org.hyprune.m2.remote';shutil.copytree(ROOT/'tests/fixtures/m2',remote,dirs_exist_ok=True)
        remote_manifest=json.loads((remote/'world.json').read_text());remote_manifest['id']='org.hyprune.m2.remote';(remote/'world.json').write_text(json.dumps(remote_manifest))
        if os.environ.get('HYPRUNE_M2_PACKAGE'):
            source=pathlib.Path(os.environ['HYPRUNE_M2_PACKAGE']);manifest=json.loads((source/'world.json').read_text());shutil.copytree(source,installed/manifest['id'],dirs_exist_ok=True)
            (STATE/'package-install.json').write_text(json.dumps({'worldId':manifest['id']}))
    if args.m1:
        subprocess.run([sys.executable,str(ROOT/'tests/fixtures/materials.py')],check=True,stdout=subprocess.DEVNULL)
        shutil.copytree(ROOT/'tests/fixtures/materials',installed/'org.hyprune.materials',dirs_exist_ok=True)
    if args.integration:
        source=pathlib.Path(os.environ.get('HYPRUNE_SWITCHYARD_PACKAGE',str(ROOT.parent/'worlds/dist/com.hyprune.switchyard')))
        if not (source/'world.json').is_file(): raise RuntimeError('Build Switchyard per worlds/switchyard/README.md first')
        shutil.copytree(source,installed/'com.hyprune.switchyard',dirs_exist_ok=True)
        quiet=installed/'org.hyprune.quiet'
        shutil.copytree(ROOT/'tests/fixtures/atrium',quiet,dirs_exist_ok=True)
        manifest=json.loads((quiet/'world.json').read_text());manifest['id']='org.hyprune.quiet';manifest['name']='Quiet observatory';(quiet/'world.json').write_text(json.dumps(manifest,indent=2)+'\n')
        mappings=[{'id':10,'label':'Switchyard','worldId':'com.hyprune.switchyard','spawnId':'arrival'}, {'id':11,'label':'Quiet observatory','worldId':'org.hyprune.quiet','spawnId':manifest['defaultSpawn']}]
        (STATE/'locations.json').write_text(json.dumps(mappings))
    if args.m3a or args.m3b:
        subprocess.run([sys.executable,str(ROOT/'tests/fixtures/m3a.py')],check=True)
        shutil.copytree(ROOT/'tests/fixtures/m3a',installed/'org.hyprune.m3a',dirs_exist_ok=True)
        if args.m3b:
            subprocess.run([sys.executable,str(ROOT/'tests/fixtures/m3b.py')],check=True)
            shutil.copytree(ROOT/'tests/fixtures/m3b',installed/'org.hyprune.m3b',dirs_exist_ok=True)
        for worldid in ['com.hyprune.lumen-reach','com.hyprune.switchyard']:
            source=ROOT.parent/'worlds/dist'/worldid
            if not (source/'world.json').is_file():raise RuntimeError('Build '+worldid+' before M3A acceptance')
            shutil.copytree(source,installed/worldid,dirs_exist_ok=True)
    if args.authoring:
        subprocess.run([sys.executable,str(ROOT/'tests/fixtures/visibility.py')],check=True)
        shutil.copytree(ROOT/'tests/fixtures/visibility',installed/'org.hyprune.visibility',dirs_exist_ok=True)
    if getattr(args,'perf',False):
        # Frozen copy, so before/after runs measure identical package bytes.
        source=pathlib.Path(os.environ.get('HYPRUNE_PERF_WORLD','/tmp/hyprune-perf/com.hyprune.lumen-reach'))
        shutil.rmtree(installed/'com.hyprune.lumen-reach',ignore_errors=True);shutil.copytree(source,installed/'com.hyprune.lumen-reach')
        # Optional B package for interleaved A/B rounds in one compositor.
        shutil.rmtree(installed/'com.hyprune.lumen-reach.b',ignore_errors=True)
        if os.environ.get('HYPRUNE_PERF_WORLD_B'):
            shutil.copytree(os.environ['HYPRUNE_PERF_WORLD_B'],installed/'com.hyprune.lumen-reach.b')
            manifest=json.loads((installed/'com.hyprune.lumen-reach.b/world.json').read_text());manifest['id']='com.hyprune.lumen-reach.b'
            (installed/'com.hyprune.lumen-reach.b/world.json').write_text(json.dumps(manifest))
        (STATE/'layout.json').unlink(missing_ok=True)
    (STATE/"graphics.json").unlink(missing_ok=True)
    if getattr(args,'ipc_warp',False):
        from ipc_warp import prepare
        prepare(installed)
    token=secrets.token_hex(32)
    if getattr(args,'playable',False):
        from playable import prepare
        prepare(installed)
    if getattr(args,'workspace_homes',False):
        from workspace_homes import prepare
        prepare(installed)
    if getattr(args,'windows',False):
        from windows import prepare
        prepare(installed)
    if getattr(args,'long_titles',False):
        from long_titles import prepare
        prepare(installed)
    if getattr(args,'live_findings',False):
        from live_findings import prepare
        prepare(installed)
    if getattr(args,'autodrive',False):
        from autodrive import prepare
        prepare(installed)
    if getattr(args,'static_world',False):
        from static_world import prepare
        prepare(installed)
    if getattr(args,'objects',False):
        from objects import prepare
        prepare(installed)
    if getattr(args,'gizmo',False):
        from gizmo import prepare
        prepare(installed)
    if getattr(args,'debug_tool',False):
        from debug_tool import prepare
        prepare(installed)
    if getattr(args,'feel',False):
        from feel import prepare
        prepare(installed)
    if getattr(args,'npcs',False):
        from npcs import prepare
        prepare(installed)
    if getattr(args,'companion',False):
        from companion import prepare
        prepare(installed)
    if getattr(args,'spawn_spots',False):
        from spawn_spots import prepare
        prepare(installed)
    if getattr(args,'lock',False):
        from lock import prepare
        prepare(installed)
    if getattr(args,'screens',False):
        from screens import prepare
        prepare(installed)
    if getattr(args,'screens_shell',False):
        from screens_shell import prepare
        prepare(installed)
    if getattr(args,'builder_shell',False):
        from builder_shell import prepare
        prepare(installed)
    if getattr(args,'import_shell',False):
        from import_shell import prepare
        prepare(installed)
    if getattr(args,'follow_corridor',False):
        from follow_corridor import prepare
        prepare(installed)
    if getattr(args,'route_carry',False):
        from route_carry import prepare
        prepare(installed)
    if getattr(args,'haze_depth',False):
        from haze_depth import prepare
        prepare(installed)
    if getattr(args,'machines',False):
        from machines import prepare
        prepare(installed)
    if getattr(args,'gauges',False):
        from gauges import prepare
        prepare(installed)
    if getattr(args,'data_sources',False):
        from datasources import prepare
        prepare(installed)
    if getattr(args,'handover',False):
        from handover import prepare
        prepare(installed)
    if getattr(args,'live_take',False):
        from live_take import prepare
        prepare(installed)
    if getattr(args,'capture_pose',False):
        from capture_pose import prepare
        prepare(installed)
    if getattr(args,'waypoint',False):
        from waypoint import prepare
        prepare(installed)
    if getattr(args,'quests',False):
        STATE.mkdir(exist_ok=True);(STATE/'layout.json').unlink(missing_ok=True);(STATE/'input.json').unlink(missing_ok=True)
        from quests import prepare
        prepare(installed)
    if getattr(args,'catalog',False):
        from catalog import prepare
        prepare(installed)
    grants=[{'token':token,'client':'org.hyprune.nested','capabilities':['state.read','world.control','surface.control','movement.control','runtime.control']}]
    if args.m1 or args.integration or args.m2 or args.m3a or args.m3b or getattr(args,'perf',False):grants[0]['capabilities']+=['shell.control','data.publish']
    if args.authoring: grants[0]['capabilities'].append('camera.author')
    shell_token=secrets.token_hex(32)
    if getattr(args,'upscaler',False):
        grants[0]['capabilities']+=['shell.control','data.publish','input.control','camera.author']
        grants.append({'token':shell_token,'client':'com.hyprune.shell','capabilities':['state.read','world.control','surface.control','movement.control','shell.control','runtime.control','data.publish','input.control','camera.author']})
    if args.integration or args.m2 or args.m3a or args.m3b: grants.append({'token':shell_token,'client':'com.hyprune.shell','capabilities':['state.read','world.control','surface.control','movement.control','shell.control','runtime.control']})
    if args.m3b:
        for grant in grants:grant['capabilities'].append('input.control')
    if getattr(args,'quests',False):grants[0]['capabilities']+=['shell.control','data.publish','input.control','camera.author','quest.control']
    if getattr(args,'catalog',False) or getattr(args,'screens',False) or getattr(args,'screens_shell',False) or getattr(args,'import_shell',False) or getattr(args,'builder_shell',False) or getattr(args,'npcs',False) or getattr(args,'gauges',False) or getattr(args,'data_sources',False) or getattr(args,'companion',False) or getattr(args,'spawn_spots',False) or getattr(args,'follow_corridor',False) or getattr(args,'route_carry',False) or getattr(args,'haze_depth',False):
        grants[0]['capabilities']+=['shell.control','data.publish','input.control','quest.control','objects.control']
        grants.append({'token':shell_token,'client':'com.hyprune.shell','capabilities':['state.read','world.control','surface.control','movement.control','shell.control','runtime.control','input.control','quest.control','objects.control']})
    if getattr(args,'quests',False):grants.append({'token':shell_token,'client':'com.hyprune.shell','capabilities':['state.read','world.control','surface.control','movement.control','shell.control','runtime.control','input.control','quest.control']})
    if getattr(args,'long_titles',False):
        from long_titles import grants as extra
        args.extra_grants=extra();grants+=args.extra_grants
    if getattr(args,'machines',False):
        from machines import grants as extra
        grants[0]['capabilities'].append('data.publish');grants+=extra()
    if getattr(args,'gauges',False):
        from gauges import grants as extra
        grants+=extra()
    if getattr(args,'data_sources',False):
        from datasources import grants as extra
        grants+=extra()
    if getattr(args,'companion',False):
        from companion import grants as extra
        grants+=extra()
    if getattr(args,'debug_tool',False):
        from debug_tool import grants as extra
        grants+=extra()
    if getattr(args,'windows',False):
        from windows import grants as extra
        grants+=extra()
    if getattr(args,'screens_shell',False):
        from screens_shell import grants as extra
        grants+=extra()
    if getattr(args,'builder_shell',False):
        from builder_shell import grants as extra
        grants+=extra()
    if getattr(args,'import_shell',False):
        from import_shell import grants as extra
        grants+=extra()
    if getattr(args,'lock',False):
        from lock import grants as extra
        grants[0]['capabilities']+=['shell.control'];grants+=extra()
    read,write=os.pipe();os.write(write,json.dumps(grants).encode());os.close(write)
    environment=os.environ.copy();environment.update(HYPRUNE_VALIDATE_IPC='1',AQ_BACKEND='wayland',HYPRUNE_WORLD_DIR=str(installed),HYPRUNE_LAUNCH_FD=str(read),HYPRUNE_LAYOUT_FILE=str(STATE/"layout.json"),HYPRUNE_INPUT_FILE=str(STATE/"input.json"),HYPRUNE_GRAPHICS_FILE=str(STATE/"graphics.json"),HYPRUNE_PLAYER_FILE=str(STATE/"player.json"),HYPRUNE_MACHINES_FILE=str(STATE/"machines.json"),HYPRUNE_EDITOR_FILE=str(STATE/"editor.json"),HYPRUNE_COMPANION_FILE=str(STATE/"companion.json"),XDG_STATE_HOME=str(STATE/"statehome"),XDG_CACHE_HOME=str(STATE/"cache"/secrets.token_hex(8)))
    if args.integration: environment['HYPRUNE_WORKSPACE_LOCATIONS']=str(STATE/'locations.json')
    config=str(HERE/'hyprland.lua')
    if os.environ.get('HYPRUNE_NESTED_4K')=='1':
        config=str(STATE/'4k-hyprland.lua');pathlib.Path(config).write_text((HERE/'hyprland.lua').read_text().replace('1920x1080@60','3840x2160@60').replace('scale = 1','scale = '+os.environ.get('HYPRUNE_NESTED_4K_SCALE','1')))
    if args.live:
        config=str(STATE/'live-hyprland.lua')
        pathlib.Path(config).write_text((HERE/'hyprland.lua').read_text().replace('general = {','input = { resolve_binds_by_sym = true },\n    general = {')+'\nhl.bind("SUPER + SHIFT + N",hl.dsp.exec_cmd("true"),{description="Keep this binding"})\nhl.bind("SUPER + SHIFT + H",hl.dsp.exec_cmd("true"),{description="Keep H binding"})\n')
    if args.m3b:
        config=str(STATE/'hyprland.lua')
        Path=pathlib.Path
        Path(config).write_text((HERE/'hyprland.lua').read_text().replace('general = {','input = { resolve_binds_by_sym = true },\n    general = {')+ '\nhl.bind("SUPER + TAB", hl.dsp.exec_cmd("touch '+str(STATE/'native-super-tab')+'"))\nhl.bind("SUPER + F9", hl.dsp.exec_cmd("touch '+str(STATE/'native-unrelated')+'"))\n')
    if os.environ.get('HYPRUNE_NESTED_4K')=='1':
        text=pathlib.Path(config).read_text().replace('1920x1080@60','3840x2160@60').replace('scale = 1','scale = '+os.environ.get('HYPRUNE_NESTED_4K_SCALE','1'));config=str(STATE/'4k-hyprland.lua');pathlib.Path(config).write_text(text)
    if os.environ.get('HYPRUNE_NESTED_SMALL')=='1':
        text=pathlib.Path(config).read_text().replace('1920x1080@60','640x480@60');config=str(STATE/'small-hyprland.lua');pathlib.Path(config).write_text(text)
    prepare_lock.close()
    log=open(STATE/'hyprland.log','w')
    if args.live: environment.pop('HYPRUNE_LAUNCH_FD',None)
    shim=ROOT/'build/nested-nvidia-headless.so'
    if nvidia_only() and shim.is_file():environment['LD_PRELOAD']=':'.join(filter(None,[str(shim),environment.get('LD_PRELOAD')]))
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
        state={'pid':process.pid,'startTime':pathlib.Path(f'/proc/{process.pid}/stat').read_text().split()[21],'config':config,'runtime':str(runtime),'signature':signature,'wayland':wayland,'cacheHome':environment['XDG_CACHE_HOME'],'stateHome':environment['XDG_STATE_HOME'],'outerSignature':environment.get('HYPRLAND_INSTANCE_SIGNATURE',''),'outerWayland':environment.get('WAYLAND_DISPLAY','')}
        (STATE/'session.json').write_text(json.dumps(state));verified()
        ctl('output','create','headless');time.sleep(.3)
        if os.environ.get('HYPRUNE_NESTED_INSTANCE'):
            # Headless parents have no input devices, so the seat's pointer capability
            # existed only while an injected virtual pointer did: it flapped with every
            # one-shot nested-pointer and Qt clients dropped their wl_pointer (the Director
            # never saw a click). One idle virtual pointer lives as long as the session.
            # Hyprland registers a virtual pointer with its first event: send a null motion.
            idle=envrun(str(ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,text=True)
            idle.stdin.write('0 0\n');idle.stdin.flush();children.append(idle)
        ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
        if args.live:
            from live_check import acceptance
            acceptance(state, children)
            return
        copy=STATE/f'hyprune-{time.time_ns()}.so';shutil.copy2(os.environ.get('HYPRUNE_NESTED_PLUGIN',str(ROOT/'build/hyprune-core.so')),copy)
        result=ctl('plugin','load',str(copy));print('load:',result,flush=True)
        if result!='ok':raise RuntimeError(result)
        ctl('reload')
        socketpath=runtime/'hyprune'/signature/'control.sock'
        for _ in range(50):
            if socketpath.is_socket():break
            time.sleep(.1)
        client=Client(str(socketpath),token,getattr(args,"protocol",None) or ("0.27" if getattr(args,"screens",False) or getattr(args,"screens_shell",False) or getattr(args,"import_shell",False) or getattr(args,"builder_shell",False) else "0.26" if getattr(args,"waypoint",False) or getattr(args,"companion",False) or getattr(args,"spawn_spots",False) or getattr(args,"follow_corridor",False) or getattr(args,"route_carry",False) or getattr(args,"haze_depth",False) or getattr(args,"lock",False) or getattr(args,"windows",False) or getattr(args,"debug_tool",False) else "0.24" if getattr(args,"data_sources",False) else "0.23" if getattr(args,"npcs",False) else "0.22" if getattr(args,"catalog",False) or getattr(args,"gauges",False) else "0.21" if getattr(args,"quests",False) else "0.19" if getattr(args,"reticle",False) or getattr(args,"gizmo",False) or getattr(args,"live_take",False) else "0.16" if getattr(args,"upscaler",False) or getattr(args,"live_findings",False) else "0.15" if getattr(args,"perf",False) or getattr(args,"autodrive",False) or getattr(args,"static_world",False) or getattr(args,"objects",False) or getattr(args,"feel",False) or getattr(args,"capture_pose",False) or getattr(args,"machines",False) or getattr(args,"handover",False) or getattr(args,"placement",False) or getattr(args,"ipc_warp",False) or getattr(args,"playable",False) or getattr(args,"workspace_homes",False) or getattr(args,"long_titles",False) or getattr(args,"watermark",False) else "0.14" if getattr(args,"ink",False) else "0.13" if args.authoring else "0.11" if args.climbing else os.environ.get("HYPRUNE_NESTED_PROTOCOL","0.8") if args.m3b else "0.5" if args.m3a else "0.4" if args.m2 else "0.3" if args.integration else "0.2" if args.m1 else "0.1"))
        print('hello:',client.hello['protocol'],client.hello['methods'],flush=True)
        if getattr(args,"live_take",False):
            from live_take import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"catalog",False):
            from catalog import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"quests",False):
            from quests import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"capture_pose",False):
            from capture_pose import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"waypoint",False):
            from waypoint import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"handover",False):
            from handover import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"npcs",False):
            from npcs import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"companion",False):
            from companion import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"spawn_spots",False):
            from spawn_spots import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"lock",False):
            from lock import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"screens",False):
            from screens import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"screens_shell",False):
            from screens_shell import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"builder_shell",False):
            from builder_shell import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"import_shell",False):
            from import_shell import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"follow_corridor",False):
            from follow_corridor import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"route_carry",False):
            from route_carry import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"haze_depth",False):
            from haze_depth import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"machines",False):
            from machines import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"gauges",False):
            from gauges import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"data_sources",False):
            from datasources import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"feel",False):
            from feel import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"static_world",False):
            from static_world import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"gizmo",False):
            from gizmo import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"debug_tool",False):
            from debug_tool import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"objects",False):
            from objects import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"autodrive",False):
            from autodrive import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"upscaler",False):
            from upscaler import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"perf",False):
            from perf import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"ipc_warp",False):
            from ipc_warp import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"playable",False):
            from playable import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"workspace_homes",False):
            from workspace_homes import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"windows",False):
            from windows import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"long_titles",False):
            from long_titles import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"live_findings",False):
            from live_findings import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"render_fail",False):
            from render_fail import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"watermark",False):
            from watermark import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"ink",False):
            from ink import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if args.authoring:
            from authoring import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if args.climbing:
            from climbing import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            (STATE/'transcript.json').write_text(json.dumps(client.transcript))
            (STATE/'notifications.json').write_text(json.dumps(client.events))
            return
        if getattr(args,"reticle",False):
            from reticle import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if getattr(args,"placement",False):
            from placement import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            return
        if args.m3b:
            from m3b import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            (STATE/"transcript.json").write_text(json.dumps(client.transcript));(STATE/"notifications.json").write_text(json.dumps(client.events));return
        if args.m3a:
            from m3a import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            (STATE/'transcript.json').write_text(json.dumps(client.transcript));(STATE/'notifications.json').write_text(json.dumps(client.events));return
        if args.m2:
            from m2 import acceptance
            acceptance(client,children,args,shell_token,socketpath)
            (STATE/'transcript.json').write_text(json.dumps(client.transcript));(STATE/'notifications.json').write_text(json.dumps(client.events));return
        if args.integration:
            from integration import first_session
            first_session(client,children,args,shell_token,socketpath)
            (STATE/'transcript.json').write_text(json.dumps(client.transcript))
            (STATE/'notifications.json').write_text(json.dumps(client.events))
            return
        if args.serve:
            print('Nested ready; dev/nested/ctl.py targets only this session.',flush=True)
            while process.poll() is None:time.sleep(.5)
            return
        smoke(client,children,args)
        if args.m1:m1(client)
        if args.record:record(client,args.m1)
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
        if getattr(args,'ipc_warp',False):
            from ipc_warp import finish
            finish()
        if getattr(args,'long_titles',False):
            from long_titles import finish
            finish()

def wait_for(test,timeout=10):
    until=time.monotonic()+timeout
    while not test():
        if time.monotonic()>until:raise TimeoutError('nested condition')
        time.sleep(.1)
def face(title,distance=2.2):
    """Eye (x, y, z, yaw) in front of a captured window, looking at its centre. Room
    clients are placed by core, so tests aim at the real plane, not a fixed layout."""
    plane=next(c for c in camera()['captures'] if title in c['title'])
    x,y,z,w=plane['rotation'];n=[2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)];p=plane['position']
    return p[0]+n[0]*distance,p[1],p[2]+n[2]*distance,math.atan2(-n[0],n[2])
def smoke(client,children,args):
    assert 'state.subscribe' in client.hello['methods']
    loaded=client.call('world.load',{'worldId':'org.hyprune.atrium'});print('world loaded:',loaded,flush=True)
    subscription=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    # Enter the world first: clients launched while it is active open floating in its
    # room (windows.world_workspace, 20 by default) and become world surfaces.
    print('toggle:' ,ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")'),flush=True)
    wait_for(lambda:camera()['active'])
    # Real clients and real input within the verified nested Wayland session.
    children.append(envrun('kitty','--title','Hyprune workbench','--override','font_size=17','bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    children.append(envrun('kitty','--title','Hyprune event stream','--override','font_size=13','bash','-c',f'printf "HYPRUNE / IPC 0.1\\nLive state deltas\\n\\n"; tail -n 12 -f {STATE}/events.log',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    (STATE/'events.log').write_text('Waiting for subscription...\n')
    titles=('Hyprune workbench','Hyprune event stream')
    wait_for(lambda:all(any(t in c['title'] for c in camera()['captures']) for t in titles),timeout=20)
    for window in json.loads(ctl('clients','-j')):
        address=window['address']
        ctl('eval',f'hl.dispatch(hl.dsp.window.resize({{ window = "address:{address}", x = 1100, y = 660 }}))')
    time.sleep(1)
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
    # Center the real picking ray on the workbench and click through the Wayland seat.
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    ctl('eval','hl.plugin.hyprune.camera({}, {}, {}, {}, 0)'.format(*face('workbench')))
    time.sleep(.2)
    subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer')],check=True)
    time.sleep(.15)
    focus_state=client.call('state.snapshot',{'subscriptionId':subscription})['snapshot']['state']
    assert next(s for s in focus_state['surfaces'] if s['id']==surface['id'])['focused'], focus_state
    print('surface click: passed',flush=True)
    client.call('movement.set',{'modeId':'com.hyprune.walk'})
    # A click focuses the window, but keys stay world bindings until Use (E) enters typing.
    subprocess.run([str(HERE/'ctl.py'),'env','wtype','-k','e'],check=True)
    wait_for(lambda:camera()['mode']=='typing')
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
    # Keyboard focus to a window in the hidden room brings the room back. Hidden
    # bound, unpulled workspaces still refuse it (-32007; covered by --live-findings).
    client.call('surface.focus',{'surfaceId':after[0]['id'],'intent':'keyboard'})
    shown=lambda:next(m for m in json.loads(ctl('monitors','-j')) if m['name']=='HEADLESS-1')['activeWorkspace']['id']
    wait_for(lambda:shown()==workspace)
    print('hidden room: captures live, keyboard focus returns to the room',flush=True)
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
        ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
        # A fresh plugin warms its shaders first (seconds on a cold NVIDIA cache).
        wait_for(lambda:camera()['active']);assert camera()['error']=='',camera()['error']
    assert ctl('plugin','unload',str(plugin))=='ok'
    print('PASS: rejected reload, modes, anonymous denial, watchdog, 3 active unload/reload cycles',flush=True)

def record(client,materials=False):
    gallery=video_dir()
    stem='hyprune-core-materials' if materials else 'hyprune-core-m0'
    output=gallery/(stem+'.mp4');raw=STATE/'capture.mkv'
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
        if materials:
            record_material_tour(client,pause)
        else:
            record_m0_tour(client,pause,type_keys,sample,started)
    finally:
        recorder.send_signal(signal.SIGINT);recorder.wait(timeout=15);log.close()
    subprocess.run(['ffmpeg','-y','-loglevel','error','-i',str(raw),'-an','-vf','scale=1920:1080:flags=lanczos:in_range=pc:out_range=tv','-r','30','-c:v','libx264','-preset','medium','-crf','22','-pix_fmt','yuv420p','-color_range','tv','-movflags','+faststart',str(output)],check=True)
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(output)]))
    stream=probe['streams'][0];assert stream['codec_name']=='h264' and stream['pix_fmt']=='yuv420p' and stream['width']==1920 and stream['height']==1080
    assert 20<=float(probe['format']['duration'])<=40 and output.stat().st_size<40_000_000,probe['format']
    notes='Original CC0 material laboratory. UV0 tiling/trim sheets, independent UV1 irradiance, transformed normal maps, copper and ceramic roughness samples. GLES3 4x MSAA, 8x anisotropy, key/sky specular. Authenticated IPC 0.2 camera tour in isolated nested Hyprland 0.56.2.' if materials else 'Original CC0 atrium: walking, live terminals and authenticated IPC 0.1 in nested Hyprland.'
    (gallery/(stem+'.json')).write_text(json.dumps({'title':'Hyprune core materials' if materials else 'Hyprune core M0','tag':'core','notes':notes},indent=2)+'\n')
    (STATE/'video-probe.json').write_text(json.dumps(probe,indent=2));print('video:',output,output.stat().st_size,'bytes',flush=True)

def record_m0_tour(client,pause,type_keys,sample,started):
        pause(2)
        ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');pause(2)
        type_keys('-P','w','-s','450','-p','w');pause(.5)
        type_keys('-P','a','-s','350','-p','a');pause(.5)
        # A short look around with relative pointer input, then face the workbench.
        for _ in range(8):subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer'),'12','0'],check=True);pause(.12)
        ctl('eval','hl.plugin.hyprune.camera({}, {}, {}, {}, 0)'.format(*face('workbench',1.9)));pause(.5)
        subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer')],check=True)
        type_keys('-k','e');pause(.3)  # Use: keys go to the terminal from here
        type_keys('-d','55',"printf '\\nHello from inside Hyprune.\\nA live terminal, in a world.\\n'")
        type_keys('-k','Return');pause(2)
        type_keys('-k','Escape')
        ctl('eval','hl.plugin.hyprune.camera(0, 1.7, 4, 0, 0)');pause(1)
        # Show the second live surface and the real IPC stream beside it.
        for _ in range(10):subprocess.run([str(HERE/'ctl.py'),'env',str(ROOT/'build/nested-pointer'),'-10','0'],check=True);pause(.12)
        pause(max(0,27-(time.monotonic()-started)))
        client.call('runtime.exit',{});pause(2)
def record_material_tour(client,pause):
    import math
    client.call('world.load',{'worldId':'org.hyprune.materials'})
    pause(1)
    ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
    client.call('movement.set',{'modeId':'com.hyprune.fly'})
    start=time.monotonic()
    while time.monotonic()-start<26:
        t=time.monotonic()-start
        # Begin beyond the live terminal quads, then inspect floor, trim and the roughness gallery.
        x=1.5*math.sin(t*.22);y=2.2+.6*math.sin(t*.17);z=-1.9-.7*math.sin(t*.13)
        yaw=-x*.13;pitch=.03+.08*math.sin(t*.3)
        sy=math.sin(-yaw/2);cy=math.cos(-yaw/2);sp=math.sin(pitch/2);cp=math.cos(pitch/2)
        client.call('camera.set',{'position':[x,y,z],'orientation':[cy*sp,sy*cp,-sy*sp,cy*cp]})
        pause(.06)
    client.call('runtime.exit',{});pause(1)

def m1(client):
    client.call('world.load',{'worldId':'org.hyprune.materials'})
    ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');time.sleep(.2)
    subscription=next(x['response']['result']['subscriptionId'] for x in client.transcript if x['method']=='state.subscribe')
    def state():return client.call('state.snapshot',{'subscriptionId':subscription})['snapshot']['state']
    pose=client.call('camera.get',{});client.call('camera.set',pose)
    client.call('waypoint.set',{'id':'gallery','label':'Gallery','target':{'zoneId':'gallery'}})
    client.call('route.set',{'points':[{'position':[0,1.7,5]},{'position':[0,1.7,4]}]})
    client.call('autodrive.start',{'speed':4});time.sleep(.8)
    assert not state()['location']['autodrive']
    client.call('travel.start',{'to':{'zoneId':'gallery'},'durationMs':400})
    time.sleep(.55);assert state()['location']['transition']['phase']=='arrived'
    assert any(e['method']=='location.transition' for e in client.events)
    output=state()['runtime']['outputs'][0]
    lease=client.call('shell.claim',{'outputs':[output]})['leaseId']
    client.call('ui.open',{'leaseId':lease,'outputId':output,'panel':'overview'})
    assert state()['shell']['leases'][0]['overlays'][0]['visible']
    client.call('ui.close',{'leaseId':lease,'outputId':output})
    # A long hold opens the overview: longer than input longHoldMs (1000 ms by default since
    # the keymap engine; this check was written for a shorter threshold).
    hold=json.loads((ROOT/'config/input.default.json').read_text())['longHoldMs']+200
    subprocess.run([str(HERE/'ctl.py'),'env','wtype','-P','Tab','-s',str(hold),'-p','Tab'],check=True)
    assert state()['shell']['leases'][0]['overlays'][0]['visible'],'core hold timing'
    client.call('ui.close',{'leaseId':lease,'outputId':output})
    client.call('data.publish',{'sourceId':'org.hyprune.nested.metrics','sequence':1,'values':{'cpuPercent':12,'status':'live'},'ttlMs':200})
    assert len(state()['integrations']['sources'])==1
    time.sleep(.3);assert state()['integrations']['sources']==[]
    client.call('shell.release',{'leaseId':lease})
    client.call('runtime.exit',{})
    print('PASS M1: camera, zone waypoint, route/autodrive, timed warp events, UI leases/hold, publishing/expiry',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--ipc-warp',action='store_true');parser.add_argument('--perf',action='store_true');parser.add_argument('--upscaler',action='store_true');parser.add_argument('--autodrive',action='store_true');parser.add_argument('--static-world',action='store_true');parser.add_argument('--objects',action='store_true');parser.add_argument('--quests',action='store_true');parser.add_argument('--catalog',action='store_true');parser.add_argument('--gizmo',action='store_true');parser.add_argument('--debug-tool',action='store_true');parser.add_argument('--feel',action='store_true');parser.add_argument('--npcs',action='store_true');parser.add_argument('--companion',action='store_true');parser.add_argument('--spawn-spots',action='store_true');parser.add_argument('--lock',action='store_true');parser.add_argument('--screens',action='store_true');parser.add_argument('--screens-shell',action='store_true');parser.add_argument('--import-shell',action='store_true');parser.add_argument('--builder-shell',action='store_true');parser.add_argument('--follow-corridor',action='store_true');parser.add_argument('--route-carry',action='store_true');parser.add_argument('--haze-depth',action='store_true');parser.add_argument('--machines',action='store_true');parser.add_argument('--gauges',action='store_true');parser.add_argument('--data-sources',action='store_true');parser.add_argument('--handover',action='store_true');parser.add_argument('--capture-pose',action='store_true');parser.add_argument('--waypoint',action='store_true');parser.add_argument('--live-take',action='store_true');parser.add_argument('--playable',action='store_true');parser.add_argument('--workspace-homes',action='store_true');parser.add_argument('--long-titles',action='store_true');parser.add_argument('--windows',action='store_true');parser.add_argument('--live-findings',action='store_true');parser.add_argument('--render-fail',action='store_true');parser.add_argument('--placement',action='store_true');parser.add_argument('--reticle',action='store_true');parser.add_argument('--ink',action='store_true');parser.add_argument('--watermark',action='store_true');parser.add_argument('--authoring',action='store_true');parser.add_argument('--climbing',action='store_true');parser.add_argument('--serve',action='store_true');parser.add_argument('--live',action='store_true');parser.add_argument('--record',action='store_true');parser.add_argument('--m3b',action='store_true');parser.add_argument('--m3a',action='store_true');parser.add_argument('--m2',action='store_true');parser.add_argument('--m1',action='store_true');parser.add_argument('--integration',action='store_true');parser.add_argument('--visible',action='store_true');args=parser.parse_args()
    try:run(args)
    except KeyboardInterrupt:print("Nested session stopped.",flush=True)
