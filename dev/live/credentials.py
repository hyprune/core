"""Private one-shot renewal, no token files or public issuance socket."""
import json,os,secrets,select,socket,struct,time

class CredentialBroker:
    def __init__(self,channel,helper,child_pid):
        self.channel=channel;self.helper=helper;self.child_pid=child_pid
        self.buffer=b'';self.count=0;self.window=time.monotonic();self.burst=0
        channel.setsockopt(socket.SOL_SOCKET,socket.SO_PASSCRED,1)
        helper.settimeout(3)

    def poll(self,timeout=.25):
        readable,_,_=select.select([self.channel,self.helper],[],[],timeout)
        if self.helper in readable:return False # EOF or unsolicited helper data: fail closed.
        if self.channel not in readable:return True
        data,ancillary,flags,_=self.channel.recvmsg(64,socket.CMSG_SPACE(12))
        if not data:return False
        credentials=[struct.unpack('3i',value[:12]) for level,kind,value in ancillary if level==socket.SOL_SOCKET and kind==socket.SCM_CREDENTIALS]
        if flags & (socket.MSG_TRUNC|socket.MSG_CTRUNC) or credentials!=[(self.child_pid,os.getuid(),os.getgid())]:return False
        self.buffer+=data
        if len(self.buffer)>64:return False
        while b'\n' in self.buffer:
            line,self.buffer=self.buffer.split(b'\n',1)
            now=time.monotonic()
            if now-self.window>=1:self.window=now;self.burst=0
            if line!=b'renew' or self.count>=4096 or self.burst>=8:return False
            self.count+=1;self.burst+=1
            token=secrets.token_hex(32)
            self.helper.send(json.dumps({'client':'com.hyprune.shell','pid':self.child_pid,'token':token}).encode())
            if json.loads(self.helper.recv(1024))!={'ok':True}:return False
            self.channel.sendall((token+'\n').encode())
        return True
