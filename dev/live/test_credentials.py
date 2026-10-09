"""Renewal broker tests: real UNIX credentials; no compositor/socket discovery."""
import json,os,socket,subprocess,sys,threading,unittest
from credentials import CredentialBroker

class Tests(unittest.TestCase):
    def test_fresh_tokens_and_scoped_requests(self):
        a,b=socket.socketpair();h,d=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        with a,b,h,d:
            broker=CredentialBroker(a,h,os.getpid());requests=[]
            def helper():
                for _ in range(2):
                    requests.append(json.loads(d.recv(1024)));d.send(b'{"ok":true}')
            thread=threading.Thread(target=helper);thread.start()
            tokens=[]
            for _ in range(2):
                b.sendall(b'renew\n');self.assertTrue(broker.poll(.1));tokens.append(b.recv(128).strip())
            thread.join(2)
            self.assertNotEqual(*tokens)
            for token,request in zip(tokens,requests):
                self.assertRegex(token.decode(),r'^[a-f0-9]{64}$')
                self.assertEqual(request,{'client':'com.hyprune.shell','pid':os.getpid(),'token':token.decode()})
    def test_only_actual_launched_process_can_request(self):
        a,b=socket.socketpair();h,d=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        with a,b,h,d:
            broker=CredentialBroker(a,h,os.getpid())
            child=subprocess.Popen([sys.executable,'-c','import os,sys;os.write(int(sys.argv[1]),b"renew\\n")',str(b.fileno())],pass_fds=(b.fileno(),))
            child.wait();self.assertFalse(broker.poll(.1))
            d.setblocking(False)
            with self.assertRaises(BlockingIOError):d.recv(100)
    def test_request_limits_and_eof(self):
        for data in [b'not-renew\n',b'x'*65]:
            a,b=socket.socketpair();h,d=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
            with a,b,h,d:
                broker=CredentialBroker(a,h,os.getpid());b.sendall(data)
                self.assertTrue(not broker.poll(.1) or not broker.poll(.1))
        a,b=socket.socketpair();h,d=socket.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET)
        with a,b,h,d:
            broker=CredentialBroker(a,h,os.getpid());broker.count=4096;b.sendall(b'renew\n');self.assertFalse(broker.poll(.1))
if __name__=='__main__':unittest.main()
