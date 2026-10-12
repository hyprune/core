"""RFC-0011 P1 demo recording: CATALOG_STAGE=demo dev/nested/run.sh --catalog.

Lumen Reach with the real shell (HYPRUNE_SHELL_DIR): the Director's Objects tab, Add
puts the server rack and then the armillary in the pocket, tool 5 places them with the
ghost, tool 4 moves and turns the rack, a refused placement, and Remove from the Placed
list. CATALOG_VIDEO names the output (default <state>/catalog/demo.mp4); a timeline and
a still of the Objects tab are written beside it.
"""
import builtins, hashlib, json, math, os, pathlib, signal, subprocess, time
import session as s
from ctl import verified
from m2 import wait, keys

RACK = 'library/com.hyprune.starter@0.1.0/server-rack'
ARMILLARY = 'library/com.hyprune.starter@0.1.0/armillary'


def run(client, children, shell_token, socketpath, archive, world):
    out = pathlib.Path(os.environ.get('CATALOG_VIDEO', str(s.STATE / 'catalog/demo.mp4')))
    out.parent.mkdir(parents=True, exist_ok=True)
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.08)

    def click(button=272):
        pointer('button', button, 1); time.sleep(.12); pointer('button', button, 0); time.sleep(.3)

    def center():
        for _ in range(4):
            xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)
    center()
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    diag = lambda: s.camera()
    ghost = lambda: diag()['objects']['ghost']

    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})')
        time.sleep(.25)

    def glide(eye, yaw, pitch, seconds=1.2):
        cam = diag()
        p0, y0, q0 = cam['position'], cam['yaw'], cam['pitch']
        while yaw - y0 > math.pi: yaw -= 2 * math.pi
        while yaw - y0 < -math.pi: yaw += 2 * math.pi
        n = max(2, int(seconds * 9))
        for k in range(1, n + 1):
            a = (1 - math.cos(math.pi * k / n)) / 2
            p = [p0[i] + (eye[i] - p0[i]) * a for i in range(3)]
            s.ctl('eval', f'hl.plugin.hyprune.camera({p[0]},{p[1]},{p[2]},{y0 + (yaw - y0) * a},{q0 + (pitch - q0) * a})')

    def look_at(eye, target, seconds=1.2):
        d = [target[i] - eye[i] for i in range(3)]
        glide(eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])), seconds)

    # The real shell (this branch's Objects tab), as M3B launches it.
    read, write = os.pipe(); os.write(write, (shell_token + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.22', HYPRUNE_SOCKET=str(socketpath),
                       HYPRUNE_OUTPUT_SCREEN='HEADLESS-1', HYPRUNE_DEV_AUTOMATION='1')
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', '0.22'], env=environment,
                             pass_fds=(read,), stdout=open(s.STATE / 'shell.log', 'w'), stderr=subprocess.STDOUT)
    os.close(read); children.append(shell)

    def childpid():
        assert shell.poll() is None, 'shell exited; see shell.log'
        return builtins.next((int(p) for p in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
                              if pathlib.Path(f'/proc/{p}/comm').read_text().strip() == 'qs'), None)
    qpid = wait(childpid, 30)

    def ui(*args):
        return subprocess.check_output([str(s.HERE / 'ctl.py'), 'env', 'qs', 'ipc', '--pid', str(qpid), 'call', 'preview',
                                        *map(str, args)], text=True).strip()

    def status():
        try:
            return json.loads(ui('status'))
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            return {}
    wait(lambda: status().get('lease'), 30)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    sha = hashlib.sha256(archive.read_bytes()).hexdigest()
    client.call('libraries.install', {'archive': str(archive), 'sha256': sha})

    # Rehearsal (not recorded): find a valid floor spot for each object and a refused one.
    def find(ref, candidates, valid=True):
        client.call('objects.pocket', {'ref': ref})
        for pos, yaw, pitch in candidates:
            camera(pos, yaw, pitch)
            wait(lambda: ghost()['asset'] >= 0, 30); time.sleep(.3)
            print('demo try', ref, pos, ghost()['valid'], ghost()['rule'], ghost()['position'], flush=True)
            if ghost()['valid'] == valid:
                return (pos, yaw, pitch), ghost()['position']
        raise AssertionError(('no spot', ref, ghost()))
    rack_spot, rack_at = find(RACK, [((-8, 1.7, 8), 0, -.35), ((-5, 1.7, 8), 0, -.35)])
    # The armillary is 4.3 m across: the open floor north of the arrival (z = 12) has room.
    arm_spot, arm_at = find(ARMILLARY, [((-4, 2.4, 12), math.pi / 2, -.38), ((0, 2.4, 15.2), 0, -.6), ((4, 2.4, 15.2), 0, -.6),
                                        ((-6, 2.4, 12.5), math.pi / 2, -.3), ((10, 2.4, 13), -math.pi / 2, -.38)])
    refused_spot, _ = find(RACK, [((-2, 1.7, 4), 1.2, -.35), ((-12, 1.7, 4), -1.2, -.35)], valid=False)
    print('demo spots', rack_spot, arm_spot, refused_spot, flush=True)
    client.call('world.load', {'worldId': world})  # drops the rehearsal's pocket
    time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    keys('-k', '1'); time.sleep(.3)

    def director():
        keys('-P', 'Tab', '-s', '1100', '-p', 'Tab')  # a TAB hold opens the Director
        wait(lambda: status().get('director'), 5)

    timeline, t0 = [], [0.]

    def mark(text):
        timeline.append({'time': round(time.monotonic() - t0[0], 2), 'action': text}); print('demo', timeline[-1], flush=True)

    def add(ref, name):
        director(); time.sleep(.4)
        ui('tabNamed', 'objects'); time.sleep(.6)
        ui('objectsView', 'catalog'); time.sleep(.3)
        ui('objectFilter', 'library', '', ''); time.sleep(.8)
        ui('focusNamed', f'objectCard:{ref}'); time.sleep(.8)
        mark(f'Objects tab: Add {name}')
        ui('press', f'objectAdd:{ref}'); time.sleep(1.2)
        ui('close'); wait(lambda: not status().get('director'), 5); time.sleep(.3)
        center()

    camera([-10, 3.2, 14], .35, -.12)
    time.sleep(1.5)
    log = open(s.STATE / 'recorder.log', 'w')
    recorder = s.envrun('wf-recorder', '-o', 'HEADLESS-1', '-r', '30', '-D', '--no-dmabuf', '-c', 'h264_nvenc', '-x', 'bgr0',
                        '-p', 'preset=p4', '-p', 'rc=constqp', '-p', 'qp=16', '-f', str(out), stdout=log, stderr=subprocess.STDOUT)
    children.append(recorder); time.sleep(1.0); t0[0] = time.monotonic()
    still = None
    try:
        mark('Sun Court, Lumen Reach')
        glide([-9, 2.6, 11], .1, -.15, 1.6)
        director(); time.sleep(.4)
        ui('tabNamed', 'objects'); time.sleep(.8)
        mark('Director > Objects: the catalog (world, library and user objects)')
        time.sleep(1.0)
        ui('objectFilter', 'library', '', ''); time.sleep(1.2)
        still = out.with_name(out.stem + '-objects-tab.png')
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(still)], check=True)
        ui('focusNamed', f'objectCard:{RACK}'); time.sleep(1.0)
        mark('Add the server rack: into the pocket, tool 5')
        ui('press', f'objectAdd:{RACK}'); time.sleep(1.4)
        ui('close'); wait(lambda: not status().get('director'), 5); time.sleep(.3)
        center()
        pos, yaw, pitch = rack_spot
        glide(pos, yaw, pitch, 1.6)
        mark('The ghost follows the aim: green where the rack fits')
        time.sleep(1.4)
        glide([pos[0] + .6, pos[1], pos[2]], yaw - .12, pitch, .8)
        glide(pos, yaw, pitch, .8)
        wait(lambda: ghost()['valid'], 5)
        click()
        mark('Placed')
        time.sleep(1.2)
        add(ARMILLARY, 'the armillary')
        pos, yaw, pitch = arm_spot
        glide(pos, yaw, pitch, 1.6)
        mark('The armillary (decorative, no character): tool 5 places it')
        time.sleep(1.0)
        wait(lambda: ghost()['valid'], 5)
        click()
        time.sleep(.6)
        look_at([arm_at[0] - 8.5, 3.8, arm_at[2] + 3.5], [arm_at[0] - .5, 1.8, arm_at[2]], 1.8)
        time.sleep(.8)
        # Tool 4: select the rack, move it along x, turn it.
        keys('-k', '4'); time.sleep(.3)
        look_at([rack_at[0] + .6, 2.6, rack_at[2] + 3.6], [rack_at[0], 1.0, rack_at[2]], 1.4)
        center(); click()
        mark('Tool 4: select the rack')
        time.sleep(.8)

        def aim(part):
            for _ in range(6):
                g = wait(lambda: diag()['gizmo'] if part in diag()['gizmo']['handles'] else None, 5)
                d = [g['handles'][part]['world'][i] - diag()['position'][i] for i in range(3)]
                camera(diag()['position'], math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))
                if diag()['gizmo']['hover'] == part:
                    return diag()['gizmo']
            return diag()['gizmo']

        def slow(dx, dy, seconds=1.4):
            pointer('button', 272, 1); time.sleep(.2)
            n = max(4, int(seconds / .1))
            for _ in range(n):
                pointer(dx / n, dy / n)
            time.sleep(.4); pointer('button', 272, 0); time.sleep(.5)
        g = aim('move-x')
        px, py = g['pivotScreen']
        dx, dy = (.5 - px) * 1920, (.5 - py) * 1080
        n = math.hypot(dx, dy) or 1
        mark('Move along x (placement rules checked on release)')
        slow(-dx / n * 160, -dy / n * 160)
        for _ in range(3):
            if diag()['gizmo']['mode'] == 'rotate':
                break
            pointer('scroll', -1); time.sleep(.2)
        aim('rotate-y')
        mark('Turn it (upright: yaw only)')
        slow(-120, 0)
        time.sleep(.6)
        # A refused placement: the rack again, aimed into a window zone.
        keys('-k', '1'); time.sleep(.2)
        add(RACK, 'a second rack')
        pos, yaw, pitch = refused_spot
        glide(pos, yaw, pitch, 1.6)
        mark('Refused: red ghost, the rule named; nothing is placed')
        time.sleep(1.2)
        click()
        time.sleep(1.6)
        # Remove the first rack from the Placed list.
        rack_id = next(i['id'] for i in state()['objects']['instances'] if i['source'] == 'user' and i['ref'] == RACK)
        look_at([rack_at[0] + 1.5, 2.8, rack_at[2] + 5.5], [rack_at[0] - .5, 1.0, rack_at[2]], 1.6)
        director(); time.sleep(.4)
        ui('tabNamed', 'objects'); time.sleep(.4)
        ui('objectsView', 'placed'); time.sleep(.8)
        ui('objectSelect', rack_id); time.sleep(1.4)
        mark('Placed list: Remove')
        ui('press', 'object-remove'); time.sleep(1.2)
        ui('close'); wait(lambda: not status().get('director'), 5)
        time.sleep(1.0)
        mark('Removed')
        glide([rack_at[0] + 1.5, 3.4, rack_at[2] + 8], .05, -.2, 2.0)
        time.sleep(1.0)
    finally:
        recorder.send_signal(signal.SIGINT); recorder.wait(timeout=30); log.close()
    out.with_suffix('.timeline.json').write_text(json.dumps(timeline, indent=2))
    print('PASS catalog: demo recorded', round(time.monotonic() - t0[0], 1), 's', out, still, flush=True)
    client.call('runtime.exit', {})
