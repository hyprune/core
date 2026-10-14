"""Remote runner and nightly helpers: make -n parsing, which world release a run installs,
the pinned library inputs and the harness scripts' arguments."""
import hashlib, importlib.machinery, importlib.util, pathlib, tempfile, unittest

HERE = pathlib.Path(__file__).resolve().parent


def load(name):
    loader = importlib.machinery.SourceFileLoader(name.replace('-', '_'), str(HERE / name))
    module = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
    loader.exec_module(module)
    return module


nightly, run = load('nightly'), load('run')


class DryRun(unittest.TestCase):
    def test_recursive_make_keeps_its_commands_only(self):
        # worlds `make -n build` (nightly 2026-10-12 ran "make[1]: Entering directory ..." as a command).
        text = ("python3 switchyard/materials/prepare.py\n"
                "make blueprint\n"
                "make[1]: Entering directory '/w/worlds-build'\n"
                "python3 tools/derive-blueprint.py dist/com.hyprune.switchyard --in-place\n"
                "make[1]: Leaving directory '/w/worlds-build'\n"
                "\n"
                "node tools/gltf/budgets.mjs\n")
        self.assertEqual(nightly.dry_run(text, pathlib.Path('/w/worlds-build')),
                         ['python3 switchyard/materials/prepare.py',
                          'python3 tools/derive-blueprint.py dist/com.hyprune.switchyard --in-place',
                          'node tools/gltf/budgets.mjs'])

    def test_sub_make_elsewhere_runs_there(self):
        text = ("make -C tools all\n"
                "make[1]: Entering directory '/w/worlds-build/tools'\n"
                "npm ci\n"
                "make[1]: Leaving directory '/w/worlds-build/tools'\n"
                "echo done\n")
        self.assertEqual(nightly.dry_run(text, '/w/worlds-build'), ["cd /w/worlds-build/tools && npm ci", 'echo done'])


class WorldRelease(unittest.TestCase):
    pins = {'version': '0.10.2', 'published': True}

    def test_default_keeps_an_install(self):
        self.assertIsNone(run.wanted_tag('lumen-reach', self.pins, '0.9.0'))
        self.assertEqual(run.wanted_tag('lumen-reach', self.pins, None), 'lumen-reach-v0.10.2')

    def test_pinned_replaces_an_old_install(self):
        self.assertEqual(run.wanted_tag('lumen-reach', self.pins, '0.9.0', pinned_only=True), 'lumen-reach-v0.10.2')
        # Same version installed: ensure_world sees it and downloads nothing.
        self.assertEqual(run.wanted_tag('lumen-reach', self.pins, '0.10.2', pinned_only=True), 'lumen-reach-v0.10.2')

    def test_unpublished_pin_keeps_the_install(self):
        unpublished = {'version': '0.11.0', 'published': False}
        self.assertIsNone(run.wanted_tag('lumen-reach', unpublished, '0.10.2', pinned_only=True))
        with self.assertRaises(RuntimeError):
            run.wanted_tag('lumen-reach', unpublished, None, pinned_only=True)


class Inputs(unittest.TestCase):
    def test_fetch_checks_the_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'src.tar.zst'
            src.write_bytes(b'library')
            good = hashlib.sha256(b'library').hexdigest()
            dest = pathlib.Path(tmp) / 'cache/lib.tar.zst'
            self.assertEqual(run.fetch(src.as_uri(), dest, good).read_bytes(), b'library')
            with self.assertRaises(RuntimeError):
                run.fetch(src.as_uri(), pathlib.Path(tmp) / 'cache/other.tar.zst', '0' * 64)
            self.assertFalse((pathlib.Path(tmp) / 'cache/other.tar.zst').exists())
            src.unlink()  # cached with the right hash: no download
            self.assertEqual(run.fetch(src.as_uri(), dest, good), dest)

    def test_scripts_get_their_output_argument(self):
        with tempfile.TemporaryDirectory() as tmp:
            tree, out = pathlib.Path(tmp) / 'core', pathlib.Path(tmp) / 'out'
            (tree / 'dev/nested').mkdir(parents=True)
            (tree / 'dev/nested/a.py').write_text("parser.add_argument('output'); a = parser.parse_args()")
            (tree / 'dev/nested/npcs_demo.py').write_text("parser.add_argument('output')")
            (tree / 'dev/nested/b.py').write_text("s.run(argparse.Namespace())")
            self.assertEqual(run.script_args(tree, 'a.py', out), [str(out / 'output')])
            self.assertTrue((out / 'output').is_dir())
            self.assertEqual(run.script_args(tree, 'npcs_demo.py', out), [str(out / 'output/npcs-demo.mp4')])
            self.assertEqual(run.script_args(tree, 'b.py', out), [])


if __name__ == '__main__':
    unittest.main()
