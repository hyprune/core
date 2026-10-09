"""Pure selection/recovery tests; no real-session calls."""
import importlib.util, pathlib, tempfile, types, unittest
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('live',pathlib.Path(__file__).with_name('live.py'));live=importlib.util.module_from_spec(spec);spec.loader.exec_module(live)

class Tests(unittest.TestCase):
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
            p=pathlib.Path(d)/'state.json'
            live.atomic(p,{'phase':'starting','runroot':d,'session':{},'processes':[]})
            for _ in range(2):
                with self.assertRaisesRegex(RuntimeError,'Recovery pending'):live.down(types.SimpleNamespace(),p)
                self.assertEqual(live.json.loads(p.read_text())['phase'],'recovery-pending')
    def test_ABI_mismatch_refuses(self):
        with patch.object(live,'query',return_value={'commit':'wrong','tag':'v0.56.2','abiHash':'wrong'}),patch.object(live,'run',return_value='{"commit":"right","tag":"v0.56.2","abiHash":"right"}'):
            with self.assertRaisesRegex(RuntimeError,'ABI/version mismatch'):live.abi({})
    def test_ambiguous_or_missing_socket_refuses(self):
        with patch.dict(live.os.environ,{'WAYLAND_DISPLAY':'missing'}),patch.object(live,'instances',return_value=[]):
            with self.assertRaises(RuntimeError):live.select(types.SimpleNamespace(instance=None))

if __name__=='__main__':unittest.main()
