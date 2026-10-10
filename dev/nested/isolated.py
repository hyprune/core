#!/usr/bin/env python3
"""Run session.py inside a private headless parent compositor.

Used by run.sh when no parent Wayland session exists (headless hosts, SSH) or
when HYPRUNE_NESTED_ISOLATE=1. Each instance gets its own short runtime dir,
parent socket, lock file and state root, so several can run in parallel and
none touches a desktop session, DRM output or /tmp/worldshell-nested.lock.

Environment:
  HYPRUNE_NESTED_PARENT       auto (default), kwin or sway
  HYPRUNE_NESTED_PARENT_SIZE  parent output, default 3840x2160
  HYPRUNE_NESTED_STATE_ROOT   state root; default dev/nested/.instances/<id>
  HYPRUNE_NESTED_KEEP_RUNTIME 1 keeps the runtime dir (logs) after exit
Usage: isolated.py [session.py args] | isolated.py dev/nested/<script>.py [args]

  HYPRUNE_NESTED_GPU          auto (default), all, or a render node / PCI
                              address / index from /sys/class/drm/renderD*

GPU choice: with several render nodes and bwrap installed, auto gives each
instance the GPU with the fewest live instances and runs the whole instance
(parent, Hyprland, clients) in a mount namespace whose /dev/dri holds only that
GPU's render node (all /dev/nvidia* nodes stay, NVENC needs them). EGL, GBM and the
parent's dmabuf feedback then all agree, and no instance sees a card (KMS)
node. `all` keeps every GPU visible (cross-GPU offload tests). The choice
and nvidia-smi load samples (whole-GPU, 1 Hz) go to instance.json.

sway 1.12 advertises xdg_wm_base v5, but aquamarine 0.15 binds v6 and aborts
("invalid version for global xdg_wm_base"), so auto prefers kwin_wayland's
virtual backend. sway stays selectable for wlroots builds that offer v6.
"""
import fcntl, json, os, pathlib, secrets, shutil, signal, subprocess, sys, time
HERE=pathlib.Path(__file__).resolve().parent
# Nested Hyprland puts <runtime>/hypr/<62-char signature>/.socket2.sock next
# to the runtime dir; sun_path holds 107 bytes, so the instance dir stays short.
SOCKET_TAIL=len('/hypr/')+62+len('/.socket2.sock')
SOCKET_MAX=107

def runtime_base():
    for base in [os.environ.get('XDG_RUNTIME_DIR'),f'/run/user/{os.getuid()}','/tmp']:
        if base and os.path.isdir(base) and os.access(base,os.W_OK):yield base

def make_runtime():
    for base in runtime_base():
        for _ in range(20):
            path=pathlib.Path(base)/('hn'+secrets.token_hex(2))
            if len(str(path))+SOCKET_TAIL>SOCKET_MAX:break
            try:path.mkdir(mode=0o700)
            except FileExistsError:continue
            return path
    raise RuntimeError('no runtime base short enough for nested Hyprland sockets')

def gpus():
    found=[]
    for path in sorted(pathlib.Path('/sys/class/drm').glob('renderD*'),key=lambda p:int(p.name[7:])):
        pci=os.path.basename(os.path.realpath(path/'device'));minor=None
        info=pathlib.Path('/proc/driver/nvidia/gpus')/pci/'information'
        if info.is_file():
            minor=next((int(l.split(':')[1]) for l in info.read_text().splitlines() if l.startswith('Device Minor:')),None)
        vendor=(path/'device/vendor').read_text().strip() if (path/'device/vendor').is_file() else ''
        found.append({'index':len(found),'node':path.name,'pci':pci,'vendor':vendor,'nvidiaMinor':minor})
    return found

def choose_gpu(base):
    want=os.environ.get('HYPRUNE_NESTED_GPU','auto');found=gpus()
    if want=='all' or len(found)<2:return None
    if want!='auto':
        match=[g for g in found if want in (g['node'],g['pci'],str(g['index']),'/dev/dri/'+g['node'])]
        if not match:raise RuntimeError(f'HYPRUNE_NESTED_GPU={want}: no such render node')
        return match[0]
    if not shutil.which('bwrap'):return None
    live={g['node']:0 for g in found}
    for claim in pathlib.Path(base).glob('hn*/gpu'):
        try:node,pid=claim.read_text().split()
        except (OSError,ValueError):continue
        if node in live and pathlib.Path(f'/proc/{pid}').exists():live[node]+=1
    return min(found,key=lambda g:live[g['node']])

def sandbox(gpu,runtime):
    # bwrap's user namespace shows root-owned /tmp/.X11-unix as nobody's, and Hyprland
    # then refuses to create X11 sockets there (no Xwayland). Each instance gets its own.
    x11=runtime/'x11';x11.mkdir(mode=0o1777,exist_ok=True)
    args=['bwrap','--dev-bind','/','/','--dev','/dev','--dev-bind','/dev/shm','/dev/shm','--bind',str(x11),'/tmp/.X11-unix','--die-with-parent']
    nodes=['/dev/dri/'+gpu['node']]
    if gpu['nvidiaMinor'] is not None:
        # Only the render node steers EGL/GBM/the parent. Every /dev/nvidiaN stays:
        # NVENC (wf-recorder, ffmpeg) won't open an encode session without them all.
        nodes+=sorted(str(d) for d in pathlib.Path('/dev').glob('nvidia[0-9]*'))+[d for d in ('/dev/nvidiactl','/dev/nvidia-modeset','/dev/nvidia-uvm','/dev/nvidia-uvm-tools') if os.path.exists(d)]
    for node in nodes:args+=['--dev-bind',node,node]
    return args

def gpu_name(pci):
    if not shutil.which('nvidia-smi'):return ''
    result=subprocess.run(['nvidia-smi','--query-gpu=name','--format=csv,noheader','-i',pci],capture_output=True,text=True)
    return result.stdout.strip() if result.returncode==0 else ''

def load_summary(path):
    rows={}
    for line in path.read_text().splitlines() if path.is_file() else []:
        try:bus,util,memory,power=[v.strip() for v in line.split(',')];rows.setdefault(bus.lower(),[]).append((float(util),float(memory),float(power)))
        except ValueError:continue
    return {bus:{'samples':len(r),'utilMeanPct':round(sum(x[0] for x in r)/len(r),1),'utilMaxPct':max(x[0] for x in r),
                 'memMaxMiB':max(x[1] for x in r),'powerMeanW':round(sum(x[2] for x in r)/len(r),1)} for bus,r in rows.items()}

def parent_command(kind,runtime,socket,size):
    width,height=size.split('x')
    if kind=='kwin':
        return ['kwin_wayland','--virtual','--width',width,'--height',height,'--socket',socket,'--no-lockscreen','--no-global-shortcuts','--no-kactivities'],{'QT_QPA_PLATFORM':'offscreen','KWIN_WAYLAND_NO_PERMISSION_CHECKS':'1'}
    config=runtime/'sway.conf'
    config.write_text(f'output HEADLESS-1 mode {size} position 0 0\ndefault_border none\n')
    return ['sway','-c',str(config)],{'WLR_BACKENDS':'headless','WLR_RENDERER':os.environ.get('WLR_RENDERER','gles2'),'WLR_LIBINPUT_NO_DEVICES':'1'}

def choose_parent():
    kind=os.environ.get('HYPRUNE_NESTED_PARENT','auto')
    if kind!='auto':return kind
    for name,binary in [('kwin','kwin_wayland'),('sway','sway')]:
        if shutil.which(binary):return name
    raise RuntimeError('no headless parent compositor (install kwin_wayland or sway)')

def clean_env(runtime):
    env={k:v for k,v in os.environ.items() if k not in ('WAYLAND_DISPLAY','DISPLAY','HYPRLAND_INSTANCE_SIGNATURE','SWAYSOCK','I3SOCK','WAYLAND_SOCKET','DBUS_SESSION_BUS_ADDRESS')}
    env['XDG_RUNTIME_DIR']=str(runtime);return env

def main():
    if os.environ.get('HYPRUNE_NESTED_RUNTIME'):  # re-executed inside the GPU sandbox
        runtime=pathlib.Path(os.environ.pop('HYPRUNE_NESTED_RUNTIME'));gpu=json.loads(os.environ.pop('HYPRUNE_NESTED_GPU_INFO','null'))
    else:
        base=next(runtime_base());lock=open(pathlib.Path(base)/'hyprune-nested.lock','w');fcntl.flock(lock,fcntl.LOCK_EX)
        runtime=make_runtime();gpu=choose_gpu(runtime.parent)
        (runtime/'gpu').write_text(f"{gpu['node'] if gpu else 'all'} {os.getpid()}\n");lock.close()
        if gpu:
            os.environ.update(HYPRUNE_NESTED_RUNTIME=str(runtime),HYPRUNE_NESTED_GPU_INFO=json.dumps(gpu))
            os.execvp('bwrap',sandbox(gpu,runtime)+[sys.executable,str(pathlib.Path(__file__).resolve()),*sys.argv[1:]])
    ident=runtime.name
    root=pathlib.Path(os.environ.get('HYPRUNE_NESTED_STATE_ROOT',str(HERE/'.instances'/ident)))
    root.mkdir(parents=True,exist_ok=True)
    kind=choose_parent();size=os.environ.get('HYPRUNE_NESTED_PARENT_SIZE','3840x2160')
    env=clean_env(runtime);command,extra=parent_command(kind,runtime,'wayland-parent',size);env.update(extra)
    if kind=='sway':env.pop('WAYLAND_DISPLAY',None)
    log=open(root/'parent.log','w')
    parent=subprocess.Popen(command,env=env,cwd=str(runtime),stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    code=1;sampler=None
    try:
        socket=None
        for _ in range(100):
            if parent.poll() is not None:raise RuntimeError(f'{kind} parent exited; see {root}/parent.log')
            names=['wayland-parent'] if kind=='kwin' else sorted(p.name for p in runtime.glob('wayland-*') if not p.name.endswith('.lock'))
            socket=next((n for n in names if (runtime/n).is_socket()),None)
            if socket:break
            time.sleep(.1)
        if not socket:raise RuntimeError(f'{kind} parent socket timed out')
        child=clean_env(runtime);child.update(WAYLAND_DISPLAY=socket,HYPRUNE_NESTED_STATE_ROOT=str(root),HYPRUNE_NESTED_INSTANCE=ident)
        if gpu:gpu['name']=gpu_name(gpu['pci'])
        info={'instance':ident,'runtime':str(runtime),'stateRoot':str(root),'parent':kind,'parentPid':parent.pid,'parentSize':size,'parentWayland':socket,'args':sys.argv[1:],'gpu':gpu or 'all'}
        (root/'instance.json').write_text(json.dumps(info,indent=2)+'\n')
        print(f"isolated: {kind} {size} instance {ident} gpu {gpu['node']+' '+gpu['pci'] if gpu else 'all'} state {root}",flush=True)
        if shutil.which('nvidia-smi'):
            sampler=subprocess.Popen(['nvidia-smi','--query-gpu=pci.bus_id,utilization.gpu,memory.used,power.draw','--format=csv,noheader,nounits','-lms','1000'],stdout=open(root/'gpu-load.csv','w'),stderr=subprocess.DEVNULL)
        lock=str(runtime/'nested.lock')
        # A dev/nested script that drives session.run itself (offload_acceptance.py, ...) may come first.
        script,rest=(sys.argv[1],sys.argv[2:]) if sys.argv[1:] and sys.argv[1].endswith('.py') and os.path.isfile(sys.argv[1]) else (str(HERE/'session.py'),sys.argv[1:])
        code=subprocess.call(['flock',lock,sys.executable,script,*rest],env=child)
        if sampler:sampler.terminate();sampler.wait()
        info['gpuLoad']=load_summary(root/'gpu-load.csv');info['exitCode']=code
        (root/'instance.json').write_text(json.dumps(info,indent=2)+'\n')
    finally:
        if sampler and sampler.poll() is None:sampler.terminate()
        os.killpg(parent.pid,signal.SIGTERM) if parent.poll() is None else None
        try:parent.wait(timeout=8)
        except subprocess.TimeoutExpired:os.killpg(parent.pid,signal.SIGKILL);parent.wait()
        log.close()
        # Hyprland's own log lives in the runtime dir; keep it with the artifacts.
        for path in runtime.glob('hypr/*/hyprland.log'):shutil.copy2(path,root/f'hyprland-{path.parent.name[-10:]}.log')
        if os.environ.get('HYPRUNE_NESTED_KEEP_RUNTIME')!='1':shutil.rmtree(runtime,ignore_errors=True)
    return code

if __name__=='__main__':sys.exit(main())
