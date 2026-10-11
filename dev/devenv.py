"""Developer settings: environment variables over ~/.config/hyprune/dev.env.

dev.env is optional, one KEY=VALUE per line (# comments, optional quotes, ~ expanded).
It holds machine-specific values that never belong in a repository:

  HYPRUNE_GALLERY_DIR   where stills and pages for review are published
  HYPRUNE_VIDEO_DIR     where finished demo videos are published
  HYPRUNE_BUILD_HOST    SSH host for remote test/build runs (dev/remote)
  HYPRUNE_BAKE_HOST     SSH host for Blender bakes (worlds), or "local"

An environment variable always wins over the file.
"""
import os, pathlib, sys

KEYS = ('HYPRUNE_GALLERY_DIR', 'HYPRUNE_VIDEO_DIR', 'HYPRUNE_BUILD_HOST', 'HYPRUNE_BAKE_HOST')
_warned = set()


def config_file():
    return pathlib.Path(os.environ.get('XDG_CONFIG_HOME') or pathlib.Path.home() / '.config') / 'hyprune/dev.env'


def read_file(path=None):
    values = {}
    try:
        lines = (path or config_file()).read_text().splitlines()
    except OSError:
        return values
    for line in lines:
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        key, value = line.split('=', 1)
        key, value = key.strip().removeprefix('export ').strip(), value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in '"\'':
            value = value[1:-1]
        values[key] = os.path.expanduser(value)
    return values


def get(key, default=None):
    """The environment's value, else dev.env's, else default."""
    return os.environ.get(key) or read_file().get(key) or default


def apply(env=None):
    """A copy of env (default os.environ) with dev.env's keys filled in where unset, for children."""
    env = dict(os.environ if env is None else env)
    for key, value in read_file().items():
        if key.startswith('HYPRUNE_') and not env.get(key):
            env[key] = value
    return env


def output_dir(key, fallback, what):
    """A configured publishing directory, or fallback with one clear warning (never silent)."""
    value = get(key)
    path = pathlib.Path(value) if value else pathlib.Path(fallback)
    if not value and key not in _warned:
        _warned.add(key)
        print(f'hyprune: {key} is not set (environment or {config_file()}); {what} go to {path}',
              file=sys.stderr, flush=True)
    path.mkdir(parents=True, exist_ok=True)
    return path


def gallery_dir(sub=''):
    """HYPRUNE_GALLERY_DIR[/sub], or the local state directory with a warning."""
    state = pathlib.Path(os.environ.get('XDG_STATE_HOME') or pathlib.Path.home() / '.local/state')
    root = output_dir('HYPRUNE_GALLERY_DIR', state / 'hyprune/gallery', 'gallery stills and pages')
    path = root / sub if sub else root
    path.mkdir(parents=True, exist_ok=True)
    return path


def video_dir():
    """HYPRUNE_VIDEO_DIR, or the local state directory with a warning."""
    state = pathlib.Path(os.environ.get('XDG_STATE_HOME') or pathlib.Path.home() / '.local/state')
    return output_dir('HYPRUNE_VIDEO_DIR', state / 'hyprune/videos', 'finished videos')


if __name__ == '__main__':  # dev/devenv.py [KEY ...]: print the effective values
    for key in sys.argv[1:] or KEYS:
        print(f'{key}={get(key, "")}')
