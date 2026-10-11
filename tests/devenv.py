#!/usr/bin/env python3
"""dev/devenv.py: environment over ~/.config/hyprune/dev.env, and a warning, never silence."""
import contextlib, io, os, pathlib, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'dev'))
import devenv

with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    for key in devenv.KEYS + ('XDG_STATE_HOME',):
        os.environ.pop(key, None)
    os.environ['XDG_CONFIG_HOME'] = str(root / 'config')
    os.environ['HOME'] = str(root / 'home')
    os.environ['XDG_STATE_HOME'] = str(root / 'state')
    assert devenv.get('HYPRUNE_BUILD_HOST') is None  # no file: nothing configured
    (root / 'config/hyprune').mkdir(parents=True)
    (root / 'config/hyprune/dev.env').write_text(
        '# comment\n\nHYPRUNE_GALLERY_DIR=~/gallery\nexport HYPRUNE_BUILD_HOST="buildbox"\n'
        "HYPRUNE_BAKE_HOST='local'\nnot a line\n")
    assert devenv.get('HYPRUNE_GALLERY_DIR') == str(root / 'home/gallery')  # ~ expanded
    assert devenv.get('HYPRUNE_BUILD_HOST') == 'buildbox' and devenv.get('HYPRUNE_BAKE_HOST') == 'local'
    os.environ['HYPRUNE_BUILD_HOST'] = 'override'
    assert devenv.get('HYPRUNE_BUILD_HOST') == 'override'  # the environment wins
    child = devenv.apply({'PATH': '/bin'})
    assert child['HYPRUNE_GALLERY_DIR'] == str(root / 'home/gallery') and child['PATH'] == '/bin'
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        assert devenv.gallery_dir('screenshots/x') == root / 'home/gallery/screenshots/x'
        assert err.getvalue() == ''  # configured: no warning
        videos = devenv.video_dir()
        devenv.video_dir()
    assert videos == root / 'state/hyprune/videos' and videos.is_dir()
    warning = err.getvalue()
    assert warning.count('HYPRUNE_VIDEO_DIR is not set') == 1 and str(videos) in warning, warning  # once, naming the fallback
print('PASS devenv: environment over dev.env; unset output dirs warn once with the fallback')
