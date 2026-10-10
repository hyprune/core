#!/usr/bin/env python3
"""Default replay owns a new nested compositor. Attached IPC requires an explicit socket/grant."""
import argparse,json,os,pathlib,signal,socket,struct,subprocess,sys,tempfile,time,shutil
ROOT=pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'dev/nested'))

def parse():
    p=argparse.ArgumentParser();p.add_argument('command',choices=['replay','record-input']);p.add_argument('file_or_action');p.add_argument('name',nargs='?')
    p.add_argument('--assert',dest='checks');p.add_argument('--camera',action='store_true');p.add_argument('--smooth-ms',type=float,default=0)
    p.add_argument('--video');p.add_argument('--socket');p.add_argument('--token-fd',type=int);p.add_argument('--client-id',default='org.hyprune.dev')
    p.add_argument('--include-app-keys',action='store_true');return p.parse_args()
class IPC:
    def __init__(self,args):
        self.fd=socket.socket(socket.AF_UNIX);self.fd.settimeout(30);self.fd.connect(args.socket);self.seq=0
        token=os.read(args.token_fd,8192).decode().strip() if args.token_fd is not None else None
        hello={'protocols':['0.12'],'client':{'id':args.client_id,'name':'Hyprune replay CLI'},'capabilities':['state.read','input.control']}
        if token:hello['token']=token
        result=self.call('session.hello',hello)
        if 'input.control' not in result['capabilities']:raise RuntimeError('A fresh launch grant with input.control is required')
    def call(self,method,params):
        self.seq+=1;ident=str(self.seq);data=json.dumps({'jsonrpc':'2.0','id':ident,'method':method,'params':params}).encode();self.fd.sendall(struct.pack('>I',len(data))+data)
        def exact(n):
            b=b''
            while len(b)<n:
                d=self.fd.recv(n-len(b))
                if not d:raise EOFError('IPC closed')
                b+=d
            return b
        while True:
            n=struct.unpack('>I',exact(4))[0]
            if n>4*1024*1024:raise ValueError('IPC size limit')
            result=json.loads(exact(n))
            if result.get('id')!=ident:continue
            if 'error' in result:raise RuntimeError(result['error'])
            return result['result']
def operate(client,args):
    if args.command=='record-input':
        action=args.file_or_action
        if action not in ['start','stop','mark','status']:raise ValueError('record-input start|stop|mark NAME|status')
        params={'includeAppKeys':True} if action=='start' and args.include_app_keys else {'name':args.name} if action=='mark' else {}
        result=client.call('recording.'+action,params)
    else:
        checks=json.loads(pathlib.Path(args.checks).read_text()) if args.checks else {}
        client.call('replay.start',{'path':str(pathlib.Path(args.file_or_action).resolve()),'mode':'camera' if args.camera else 'input','smoothingMs':args.smooth_ms,'assertions':checks})
        deadline=time.monotonic()+660
        while True:
            result=client.call('replay.status',{})
            if not result['playing']:break
            if time.monotonic()>deadline:client.call('replay.stop',{});raise TimeoutError('replay deadline')
            time.sleep(.05)
    print(json.dumps(result),flush=True)
    if result['failures'] or result['error']:raise RuntimeError('Recording/replay validation failed')
def main():
    args=parse()
    if args.socket:
        if args.video:raise ValueError('--video requires an owned nested compositor')
        operate(IPC(args),args);return
    if args.command!='replay':raise ValueError('record-input requires --socket and a fresh --token-fd grant; or bind record.toggle')
    if os.environ.get('HYPRUNE_REPLAY_LOCKED')!='1':
        env=os.environ.copy();env['HYPRUNE_REPLAY_LOCKED']='1'
        raise SystemExit(subprocess.call(['flock','/tmp/worldshell-nested.lock',sys.executable,__file__,*sys.argv[1:]],env=env))
    import session as s,m3b
    from m2 import wait
    path=pathlib.Path(args.file_or_action).resolve();header=json.loads(path.open().readline())
    args.file_or_action=str(path)
    def acceptance(client,children,_args,_token,_socket):
        client.call('world.load',{'worldId':header['worldId']});s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
        time.sleep(.2)
        recorder=None;temp=None
        if args.video:
            temp=pathlib.Path(tempfile.mkdtemp(prefix='hyprune-replay-video-',dir='/tmp'))
            recorder=s.envrun('wf-recorder','-o','HEADLESS-1','-r','60','-D','--no-dmabuf','-c','libx264','-p','preset=ultrafast','-f',str(temp/'raw.mp4'),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(recorder);time.sleep(.3)
        operate(client,args)
        if recorder:
            recorder.send_signal(signal.SIGINT);recorder.wait(timeout=10)
            subprocess.run(['ffmpeg','-y','-loglevel','error','-i',str(temp/'raw.mp4'),'-c:v','libx264','-preset','medium','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',str(temp/'tour.mp4')],check=True)
            dest=pathlib.Path(args.video).resolve();dest.parent.mkdir(parents=True,exist_ok=True)
            # A videos/ directory at a watched git repo root stages outside that repo.
            stage_dir=dest.parents[2] if dest.parent.name=='videos' and (dest.parent.parent/'.git').exists() else dest.parent
            staging=stage_dir/('.'+dest.name+'.complete');shutil.copyfile(temp/'tour.mp4',staging);os.replace(staging,dest)
    fixture=s.HERE/'.m3b-state';fixture.mkdir(exist_ok=True)
    for name in ['input.json','layout.json','editor.json']:(fixture/name).unlink(missing_ok=True)
    os.environ['HYPRUNE_NESTED_PROTOCOL']='0.12';m3b.acceptance=acceptance
    s.run(argparse.Namespace(m3b=True,m3a=False,m2=False,m1=False,integration=False,live=False,serve=False,record=False,visible=False))
if __name__=='__main__':
    try:main()
    except Exception as e:print(str(e),file=sys.stderr);sys.exit(1)
