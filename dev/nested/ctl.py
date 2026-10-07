#!/usr/bin/env python3
"""The only permitted hyprctl entry point. Refuse unverified or outer sessions."""
import json, os, pathlib, subprocess, sys
HERE=pathlib.Path(__file__).resolve().parent
STATE=HERE/'.state/session.json'
def verified():
    path=pathlib.Path(os.environ.get('HYPRUNE_NESTED_STATE_DIR', str(HERE/'.state')))/'session.json'
    state=json.loads(path.read_text());pid=state['pid']
    cmd=pathlib.Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
    assert state['config'].encode() in cmd and b'Hyprland' in pathlib.Path(os.fsdecode(cmd[0])).name.encode(), 'PID is not our nested compositor'
    assert pathlib.Path(f'/proc/{pid}/stat').read_text().split()[21]==state['startTime'], 'PID was reused'
    assert state['signature']!=state['outerSignature'], 'refusing outer signature'
    assert state['wayland']!=state['outerWayland'], 'refusing outer Wayland socket'
    lock=pathlib.Path(state['runtime'])/'hypr'/state['signature']/'hyprland.lock'
    lines=lock.read_text().splitlines()
    assert lines[0]==str(pid) and lines[1]==state['wayland'], 'nested lock identity mismatch'
    assert (lock.parent/'.socket.sock').is_socket(), 'nested control socket missing'
    env=os.environ.copy();env.update(HYPRUNE_VERIFIED_NESTED="1",XDG_RUNTIME_DIR=state['runtime'],HYPRLAND_INSTANCE_SIGNATURE=state['signature'],WAYLAND_DISPLAY=state['wayland'])
    return state,env
def main():
    state,env=verified();args=sys.argv[1:]
    if not args:raise ValueError('use hyprctl, env, or shot')
    if args[0]=='hyprctl':
        if any(a in ('-i','--instance','instances') or a.startswith('--instance=') for a in args[1:]):raise ValueError('instance override forbidden')
        os.execvpe('hyprctl',['hyprctl','-i',state['signature'],*args[1:]],env)
    elif args[0]=='env':os.execvpe(args[1],args[1:],env)
    elif args[0]=='shot':os.execvpe('grim',['grim','-o','HEADLESS-1',args[1]],env)
    else:raise ValueError('unknown ctl command')
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,KeyError,AssertionError) as e:sys.exit(f'ctl refused: {e}')
