"""Pure selection/recovery tests; no real-session calls."""
import hashlib, importlib.util, pathlib, tempfile, threading, types, unittest
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('live',pathlib.Path(__file__).with_name('live.py'));live=importlib.util.module_from_spec(spec);spec.loader.exec_module(live)

class Tests(unittest.TestCase):
    def test_replaces_nothing_unless_configured(self):
        with tempfile.TemporaryDirectory() as d,patch.dict(live.os.environ,{'XDG_CONFIG_HOME':d},clear=False):
            for key in ['HYPRUNE_LIVE_REPLACE_PLUGIN','HYPRUNE_LIVE_REPLACE_HUD']:live.os.environ.pop(key,None)
            self.assertEqual(live.replaced(),('',''))
            self.assertFalse(live.is_replaced_plugin('legacy3d'))
            self.assertFalse(live.is_replaced_hud(['qs','-p',d+'/quickshell/legacy3d']))
            self.assertEqual(live.plugin_paths({'runtime':d,'pid':live.os.getpid()}),[])
            # dev.env configures the swap; the environment wins over it.
            (pathlib.Path(d)/'hyprune').mkdir();(pathlib.Path(d)/'hyprune/dev.env').write_text('HYPRUNE_LIVE_REPLACE_PLUGIN=Legacy3D\nHYPRUNE_LIVE_REPLACE_HUD=quickshell/legacy3d/\n')
            self.assertEqual(live.replaced(),('legacy3d','quickshell/legacy3d'))
            self.assertTrue(live.is_replaced_plugin('legacy3d-renderer'))
            self.assertFalse(live.is_replaced_plugin('hyprune-core'))
            self.assertTrue(live.is_replaced_hud(['qs','-p',d+'/quickshell/legacy3d']))
            self.assertFalse(live.is_replaced_hud(['qs','-c','main']))
            self.assertFalse(live.is_replaced_hud(['qs','-p',d+'/quickshell/other']))
            with patch.dict(live.os.environ,{'HYPRUNE_LIVE_REPLACE_PLUGIN':'other'}):self.assertEqual(live.replaced()[0],'other')

    def test_world_selection_is_generic_ordered_and_read_only(self):
        self.assertEqual(live.world().name,'com.hyprune.lumen-reach')
        with tempfile.TemporaryDirectory() as d,patch.object(live,'run',return_value='{}') as checker:
            dirs=[pathlib.Path(d)/'second package',pathlib.Path(d)/'first']
            for p,ident in zip(dirs,['org.example.second','org.example.first']):
                p.mkdir();(p/'world.json').write_text(live.json.dumps({'id':ident,'name':ident}))
            before=sorted(str(p.relative_to(d)) for p in pathlib.Path(d).rglob('*'))
            result=live.world_packages(dirs)
            self.assertEqual([v['id'] for v in result],['org.example.second','org.example.first'])
            self.assertEqual(checker.call_count,2)
            self.assertEqual(before,sorted(str(p.relative_to(d)) for p in pathlib.Path(d).rglob('*')))
            with patch.object(live,'world',return_value=dirs[1]):self.assertEqual(live.world_packages()[0]['id'],'org.example.first')
            result[0]['installed']=str(dirs[0]);result[1]['installed']=str(dirs[1])
            self.assertEqual(live.shell_world_args(result),['--world-package',str(dirs[0]/'world.json'),'--world-package',str(dirs[1]/'world.json')])

    def test_world_selection_rejects_duplicates_and_invalid_packages(self):
        with tempfile.TemporaryDirectory() as d,patch.object(live,'run',return_value='{}'):
            p=pathlib.Path(d);manifest=p/'world.json'
            with self.assertRaisesRegex(RuntimeError,'Missing world'):live.world_packages([p])
            for ident in ['../escape','/absolute','..',None]:
                manifest.write_text(live.json.dumps({'id':ident}))
                with self.assertRaisesRegex(RuntimeError,'Invalid world package ID'):live.world_packages([p])
            manifest.write_text('{"id":"org.example.valid"}')
            with self.assertRaisesRegex(RuntimeError,'Duplicate world ID'):live.world_packages([p,p])
            with patch.object(live,'run',side_effect=live.subprocess.CalledProcessError(1,['check'],output='missing mesh')):
                with self.assertRaisesRegex(RuntimeError,'missing mesh'):live.world_packages([p])

    def test_inactive_shell_ready_without_lease(self):
        self.assertFalse(live.shell_bootstrapped('old log text'))
        self.assertFalse(live.shell_bootstrapped('{"type":"connection","status":"live"}\n{"type":"presentation.peer","pid":42}\n{"type":"connection","status":"disconnected"}'))
        self.assertFalse(live.shell_bootstrapped('{"type":"connection","status":"live"}'))
        self.assertTrue(live.shell_bootstrapped('{"type":"connection","status":"live"}\n{"type":"presentation.peer","pid":42}'))

    def test_live_protocol_follows_cpp_inventory(self):
        protocols=live.core_protocols()
        with patch.object(live.socket, 'socket'), patch.object(live.RPC, 'call', return_value={'protocol':protocols[0]}) as call:
            rpc=live.RPC('/unused/nested-test', 'test-only')
        self.assertEqual(call.call_args.args[1]['protocols'], protocols)
        self.assertEqual(rpc.hello['protocol'], protocols[0])
        with tempfile.TemporaryDirectory() as d:
            header=pathlib.Path(d)/'Protocol.hpp'
            header.write_text('inline constexpr std::array Supported{"0.9", "0.10", "0.99"};')
            self.assertEqual(live.core_protocols(header), ['0.99','0.10','0.9'])
            with patch.object(live,'core_protocols',return_value=live.core_protocols(header)):
                self.assertEqual(live.shell_protocol(['0.9','0.10']), '0.10')
                self.assertEqual(live.shell_protocol(['0.9','0.99']), '0.99')
                self.assertEqual(live.shell_protocol(['0.9','0.10','0.99'], maximum='0.10'), '0.10')
                with self.assertRaisesRegex(RuntimeError,'no common protocol'):live.shell_protocol(['1.0'])
            header.write_text('not an inventory')
            with self.assertRaises(RuntimeError):live.core_protocols(header)

    def test_binding_selection_preserves_collision(self):
        self.assertEqual(live.choose_bind([{'key':'h','modmask':65},{'key':'n','modmask':65}]),'SUPER + SHIFT + F10')
        self.assertEqual(live.choose_bind([{'key':'h','modmask':65},{'key':'n','modmask':64}]),'SUPER + SHIFT + F10')
        self.assertEqual(live.choose_bind([{'key':'h','modmask':65},{'key':'n','modmask':0,'ignoreMods':True}]),'SUPER + SHIFT + F10')
        self.assertEqual(live.choose_bind([{'key':'h','modmask':65},{'keycode':49,'modmask':65}]),'SUPER + SHIFT + F10')
        self.assertEqual(live.choose_bind([{'key':'h','modmask':65},{'keycode':57,'modmask':65}]),'SUPER + SHIFT + F10')
        self.assertEqual(live.choose_bind([{'key':'h','modmask':65},{'key':'code:49','modmask':65}]),'SUPER + SHIFT + F10')
    def test_ready_message_uses_actual_chord(self):
        self.assertEqual(live.ready_message('SUPER + SHIFT + F10'), 'Hyprune ready · press Super+Shift+F10 to enter')
        self.assertEqual(live.ready_message('SUPER + SHIFT + H'), 'Hyprune ready · press Super+Shift+H to enter')

    def test_owner_H_preferred(self):
        self.assertEqual(live.choose_bind([]),'SUPER + SHIFT + H')
        self.assertEqual(live.choose_bind([{'key':'H','modmask':65}]),'SUPER + SHIFT + N')
    def test_no_free_chord_fails_closed(self):
        with self.assertRaises(RuntimeError):live.choose_bind([{'key':k,'modmask':65} for k in ['h','n','F10','F11','F8']])
    def test_process_identity_reuse(self):
        with patch.object(live,'process',return_value={'start':'new'}):self.assertFalse(live.alive({'pid':123,'start':'old'}))
        with patch.object(live,'process',return_value=None),patch.object(live.os,'kill') as kill:
            live.stop({'pid':123,'start':'old'});kill.assert_not_called()
    def test_down_without_state_is_noop(self):
        with tempfile.TemporaryDirectory() as d,patch.object(live,'select') as select:
            live.down(types.SimpleNamespace(),pathlib.Path(d)/'missing');select.assert_not_called()
    def test_down_twice_is_noop(self):
        with tempfile.TemporaryDirectory() as d,patch.object(live,'select') as select:
            p=pathlib.Path(d)/'state.json';live.atomic(p,{'phase':'down'});live.down(types.SimpleNamespace(),p);select.assert_not_called()
            self.assertEqual(p.stat().st_mode&0o777,0o600)
    def test_default_ignores_inherited_signature(self):
        records=[{'instance':'correct','wl_socket':'wayland-test','pid':123},{'instance':'stale','wl_socket':'other','pid':456}]
        args=types.SimpleNamespace(instance=None)
        with patch.dict(live.os.environ,{'WAYLAND_DISPLAY':'wayland-test','HYPRLAND_INSTANCE_SIGNATURE':'stale'}),patch.object(live,'instances',return_value=records),patch.object(live,'process',return_value={'pid':123,'start':'1'}),patch.object(live.pathlib.Path,'read_text',return_value='123\nwayland-test\n'),patch.object(live.pathlib.Path,'is_socket',return_value=True):
            self.assertEqual(live.select(args)['signature'],'correct')
    def test_partial_state_without_compositor_keeps_recovery(self):
        with tempfile.TemporaryDirectory() as d,patch.object(live,'select',side_effect=RuntimeError('compositor down')):
            p=pathlib.Path(d)/'state.json';(pathlib.Path(d)/'worlds').mkdir()
            live.atomic(p,{'phase':'starting','runroot':d,'session':{},'processes':[]})
            for _ in range(2):
                with self.assertRaisesRegex(RuntimeError,'Recovery pending'):live.down(types.SimpleNamespace(),p)
                self.assertEqual(live.json.loads(p.read_text())['phase'],'recovery-pending')
            self.assertTrue((pathlib.Path(d)/'worlds').is_dir())
    def test_restored_down_releases_runtime_copies(self):
        session={'signature':'nested','start':'1'}
        with tempfile.TemporaryDirectory() as d,patch.object(live,'select',return_value=session),patch.object(live,'query',return_value=[]):
            root=pathlib.Path(d);(root/'worlds/w').mkdir(parents=True);(root/'hyprune-core.so').write_text('');(root/'core.log').write_text('kept')
            p=root/'state.json'
            live.atomic(p,{'phase':'up','runroot':d,'session':session,'processes':[],'binds':[],'oldUnloaded':[],'qs':[],'restoredHUD':{}})
            live.down(types.SimpleNamespace(),p)
            self.assertEqual(live.json.loads(p.read_text())['phase'],'down')
            self.assertFalse((root/'worlds').exists() or (root/'hyprune-core.so').exists())
            self.assertTrue((root/'core.log').exists())
    def test_restored_down_removes_the_runtime_run(self):
        session={'signature':'nested','start':'1'}
        with tempfile.TemporaryDirectory() as d,patch.object(live,'select',return_value=session),patch.object(live,'query',return_value=[]):
            live_root=pathlib.Path(d)/'runtime/hyprune-live';runroot=live_root/'nested/1';(runroot/'worlds/w').mkdir(parents=True)
            (runroot/'ready.json').write_text('{}');(live_root/'nested/bootstrap.json').write_text('{}')
            p=pathlib.Path(d)/'state/state.json'
            live.atomic(p,{'phase':'up','runroot':str(runroot),'session':session,'processes':[],'binds':[],'oldUnloaded':[],'qs':[],'restoredHUD':{}})
            live.down(types.SimpleNamespace(),p)
            self.assertEqual(live.json.loads(p.read_text())['phase'],'down')
            self.assertFalse(live_root.exists())
    def test_ABI_mismatch_refuses(self):
        with patch.object(live,'query',return_value={'commit':'wrong','tag':'v0.56.2','abiHash':'wrong'}),patch.object(live,'run',return_value='{"commit":"right","tag":"v0.56.2","abiHash":"right"}'):
            with self.assertRaisesRegex(RuntimeError,'ABI/version mismatch'):live.abi({})
    def test_ambiguous_or_missing_socket_refuses(self):
        with patch.dict(live.os.environ,{'WAYLAND_DISPLAY':'missing'}),patch.object(live,'instances',return_value=[]):
            with self.assertRaises(RuntimeError):live.select(types.SimpleNamespace(instance=None))

    def test_sources_config_path_even_without_the_file(self):
        # The helper always starts, idle until sources.json appears.
        with tempfile.TemporaryDirectory() as d,patch.dict(live.os.environ,{'XDG_CONFIG_HOME':d}):
            self.assertEqual(live.sources_config(),pathlib.Path(d)/'hyprune/sources.json')
            self.assertFalse(live.sources_config().exists())
        grants=live.launch_grants({'org.hyprune.live':'a'*64,'com.hyprune.shell':'b'*64,'com.hyprune.sources':'c'*64})
        self.assertEqual([g['client'] for g in grants],['org.hyprune.live','com.hyprune.shell','com.hyprune.sources'])
        self.assertEqual(grants[2],{'token':'c'*64,'client':'com.hyprune.sources','capabilities':['data.publish']})
        self.assertEqual(grants[1]['capabilities'],live.GRANTS)
        self.assertTrue(live.SOURCE_HELPER.is_file())

    def test_source_helper_in_a_checkout_and_a_release_archive(self):
        self.assertEqual(live.find_source_helper(),live.ROOT/'dev/machines/source-helper.py')
        with tempfile.TemporaryDirectory() as d:
            # dev/release/build.sh: hyprune-core/live/dev/live/live.py beside hyprune-core/libexec.
            archive=pathlib.Path(d)/'hyprune-core';(archive/'live/dev/live').mkdir(parents=True)
            self.assertIsNone(live.find_source_helper(archive/'live'))
            helper=archive/'libexec/hyprune/source-helper';helper.parent.mkdir(parents=True);helper.write_text('')
            self.assertEqual(live.find_source_helper(archive/'live'),helper)
            self.assertEqual(live.helper_path(helper,'/usr/bin'),f'{archive}/bin:/usr/bin')
            self.assertEqual(live.helper_path(helper,''),f'{archive}/bin')
        self.assertEqual(live.helper_path(live.ROOT/'dev/machines/source-helper.py','/usr/bin'),'/usr/bin')

    def test_source_helper_gets_its_token_and_renewals_privately(self):
        fake='\n'.join(['import hashlib,os,sys',
            'args=sys.argv[1:];fd=int(args[args.index("--token-fd")+1])',
            'def line():',
            '    b=b""',
            '    while not b.endswith(b"\\n"):',
            '        c=os.read(fd,1)',
            '        if not c:sys.exit(3)',
            '        b+=c',
            '    return hashlib.sha256(b.strip()).hexdigest()',
            'first=line();os.write(fd,b"renew\\n");second=line()',
            'print("args",args,"fd-env","HYPRUNE_TOKEN_FD" in os.environ,"first",first,"second",second,flush=True)'])
        with tempfile.TemporaryDirectory() as d:
            d=pathlib.Path(d);(d/'fake-helper.py').write_text(fake)
            grants,daemon=live.socket.socketpair(live.socket.AF_UNIX,live.socket.SOCK_SEQPACKET)
            token='ab'*32;requests=[]
            def issuer():
                requests.append(live.json.loads(daemon.recv(1024)));daemon.send(b'{"ok":true}')
            thread=threading.Thread(target=issuer);thread.start()
            with grants,daemon:
                child,broker=live.start_sources(d/'sources.json',d/'control.sock',token,{'PATH':live.os.environ.get('PATH',''),'HYPRUNE_TOKEN_FD':'9'},d,grants,helper=d/'fake-helper.py')
                deadline=live.time.monotonic()+10
                while child.poll() is None and live.time.monotonic()<deadline:broker.poll(.1)
                child.wait(5);thread.join(5);broker.channel.close()
            log=(d/'source-helper.log').read_text()
            self.assertEqual(child.returncode,0,log)
            self.assertNotIn(token,log)
            self.assertIn('first '+hashlib.sha256(token.encode()).hexdigest(),log)
            self.assertEqual(len(requests),1)
            self.assertEqual({k:requests[0][k] for k in ('client','pid')},{'client':'com.hyprune.sources','pid':child.pid})
            self.assertIn('second '+hashlib.sha256(requests[0]['token'].encode()).hexdigest(),log)
            self.assertIn("'--token-refresh'",log);self.assertIn("'--socket', '"+str(d/'control.sock')+"'",log)
            self.assertIn('fd-env False',log)

    def test_source_helper_failure_leaves_the_shell_alone(self):
        class Broken:
            channel=types.SimpleNamespace(close=lambda:None)
            def poll(self,timeout):raise OSError('daemon gone')
        feeds=live.SourcesSupervisor(types.SimpleNamespace(poll=lambda:None),Broken())
        with patch('builtins.print'):feeds.poll()
        self.assertIsNone(feeds.broker);feeds.poll();feeds.close()

if __name__=='__main__':unittest.main()
