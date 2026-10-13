# SPDX-License-Identifier: MIT
"""Machines demo recorder (nested only), run by dev/nested/machines.py:

  HYPRUNE_NESTED_4K=1 HYPRUNE_MACHINES_WORLD=<package> HYPRUNE_MACHINES_DEMO=dev/machines/demo.py \\
  HYPRUNE_MACHINES_SHOTS=<shots.json> HYPRUNE_MACHINES_OUT=<out.mkv> dev/nested/run.sh --machines

The session loads the package with its bindings and runs the source helper with every
simulator. Core flies the camera along the shots file's keyframes per presented frame
(cinema.py: centripetal Catmull-Rom through positions timed by arc length; a key's
"target" is looked at, else its yaw and pitch) at the High preset, and every presented
frame of HEADLESS-1 is exported (NVENC constant QP; HYPRUNE_MACHINES_CODEC=libx264 for
CPU; HYPRUNE_MACHINES_FPS, default 30 at 4K and 60 otherwise). Every 0.25 s it logs the
machines diagnostics, so a cut can be chosen around events afterwards (<out>.json), with
the smoothness gate.

shots.json: {"warmup": 8, "keys": [{"t": 0, "position": [x, y, z], "yaw": rad, "pitch": rad}, ...],
             "holdUntil": {"machine": id, "input": id, "value": v, "timeout": s}  (optional, before recording)}
HYPRUNE_MACHINES_STILLS=<dir> with "stills": [{"name", "position", "yaw", "pitch", "publish"?: {...}}]
takes stills instead of a film.
"""
import json, math, os, pathlib, subprocess, sys, time


def catmull(p0, p1, p2, p3, u):
    return [0.5 * ((2 * b) + (-a + c) * u + (2 * a - 5 * b + 4 * c - d) * u * u + (-a + 3 * b - 3 * c + d) * u ** 3)
            for a, b, c, d in zip(p0, p1, p2, p3)]


def aim(key):
    """A key or still with a "target" point gets yaw and pitch looking at it (yaw 0 looks along -Z)."""
    if 'target' in key:
        d = [b - a for a, b in zip(key['position'], key['target'])]
        key = dict(key, yaw=math.atan2(d[0], -d[2]), pitch=math.atan2(d[1], math.hypot(d[0], d[2])))
    return key


def pose_at(keys, t):
    if t <= keys[0]['t']:
        k = keys[0]; return k['position'], k['yaw'], k['pitch']
    if t >= keys[-1]['t']:
        k = keys[-1]; return k['position'], k['yaw'], k['pitch']
    i = max(j for j in range(len(keys) - 1) if keys[j]['t'] <= t)
    a, b = keys[i], keys[i + 1]
    u = (t - a['t']) / (b['t'] - a['t'])
    p0 = keys[max(i - 1, 0)]['position']; p3 = keys[min(i + 2, len(keys) - 1)]['position']
    position = catmull(p0, a['position'], b['position'], p3, u)
    e = u * u * (3 - 2 * u)  # yaw and pitch ease between keys
    yaw = a['yaw'] + math.remainder(b['yaw'] - a['yaw'], 2 * math.pi) * e
    pitch = a['pitch'] + (b['pitch'] - a['pitch']) * e
    return position, yaw, pitch


def track(keys):
    """Shots keys as a core camera track: positions, and targets or yaw/pitch."""
    out = []
    for k in keys:
        key = {'t': k['t'], 'eye': k['position']}
        if 'target' in k:
            key['target'] = k['target']
        else:
            key.update(yaw=k['yaw'], pitch=k['pitch'])
        if 'ease' in k:
            key['ease'] = k['ease']
        out.append(key)
    return out


def run(client, s, source):
    shots = json.loads(pathlib.Path(os.environ['HYPRUNE_MACHINES_SHOTS']).read_text())
    client.call('graphics.set', {'patch': {'quality': 'high'}})
    def camera(p, yaw, pitch):
        s.ctl('eval', f'hl.plugin.hyprune.camera({p[0]},{p[1]},{p[2]},{yaw},{pitch})')
    def machines():
        return s.camera()['machines']
    keys = [aim(k) for k in shots.get('keys', [])]
    shots['stills'] = [aim(k) for k in shots.get('stills', [])]
    if not keys:
        keys = [dict(shots['stills'][0], t=0)]
    camera(*pose_at(keys, 0))
    time.sleep(shots.get('warmup', 8))  # uploads, shader warm-up, notices fade, sims fill the machines
    if os.environ.get('HYPRUNE_MACHINES_STILLS'):
        return stills(client, s, shots, camera, machines)
    hold = shots.get('holdUntil')
    if hold:
        until = time.monotonic() + hold.get('timeout', 120)
        while time.monotonic() < until:
            m = next(x for x in machines()['instances'] if x['id'] == hold['machine'])
            if m['inputs'][hold['input']]['value'] == hold['value']:
                break
            time.sleep(.25)
    # HYPRUNE_MACHINES_RECORD_AT (Unix seconds): the film's t = 0, so simulator stories
    # slid with MACHINES_SIM_SHIFT put their events where the camera is.
    record_at = float(os.environ.get('HYPRUNE_MACHINES_RECORD_AT', '0'))
    if record_at:
        late = time.time() - (record_at - 1)
        if late > 0:
            print(f'MACHINES DEMO: {late:.1f} s late for RECORD_AT', flush=True)
        time.sleep(max(0, record_at - 1 - time.time()))
    out = pathlib.Path(os.environ['HYPRUNE_MACHINES_OUT'])
    sys.path.insert(0, str(s.HERE))
    import cinema
    codec = os.environ.get('HYPRUNE_MACHINES_CODEC', 'h264_nvenc')
    encoder = ['-c:v', 'h264_nvenc', '-preset', 'p5', '-rc', 'constqp', '-qp', '14'] if 'nvenc' in codec else \
              ['-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '12', '-pix_fmt', 'yuv420p']
    width = next(m for m in json.loads(s.ctl('monitors', '-j')) if m['name'] == 'HEADLESS-1')['width']
    fps = int(os.environ.get('HYPRUNE_MACHINES_FPS', '30' if width > 1920 else '60'))
    cam = cinema.Cinema(s)
    cam.hold(keys[0]['position'], keys[0].get('target'), keys[0].get('yaw'), keys[0].get('pitch', 0.))
    recorder = cam.recording(out, encoder=encoder, out_fps=fps if fps != 60 else None)
    started = recorder.__enter__(); timeline = []; last = -1
    duration = keys[-1]['t']
    cam.path(track(keys))
    try:
        while True:
            t = time.monotonic() - started
            # Core counts the path's time in presented frames: end when it says so.
            if t > duration + .5 and (cam.status()['done'] or t > duration + 15):
                break
            if t - last >= .25:
                last = t
                m = machines()
                timeline.append({'t': round(t, 2), 'pieces': m['pieces'], 'instances': {
                    x['id']: {'states': x['states'], 'inputs': {k: v['value'] for k, v in x['inputs'].items()},
                              'tracks': [{'live': tr['live'], 'spawned': tr['spawned']} for tr in x['tracks']],
                              'stale': x['stale'], 'inView': x['inView']} for x in m['instances']}})
            time.sleep(.05)
    finally:
        recorder.__exit__(None, None, None)
        cam.stop()
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0', '-show_entries',
                                                'stream=nb_read_frames,width,height', '-of', 'json', str(out)]))['streams'][0]
    report = {'output': str(out), 'seconds': round(time.monotonic() - started, 1), 'frames': int(probe['nb_read_frames']),
              'size': [probe['width'], probe['height']], 'codec': codec,
              'graphics': client.call('graphics.get', {}).get('performance', {}).get('effective'), 'timeline': timeline,
              'export': cam.last_export, 'smoothness': cinema.gate(out)}
    out.with_suffix('.json').write_text(json.dumps(report) + '\n')
    print('MACHINES DEMO', json.dumps({k: report[k] for k in ('output', 'seconds', 'frames', 'size')}),
          cinema.gate_summary(report['smoothness']), flush=True)


def stills(client, s, shots, camera, machines):
    out = pathlib.Path(os.environ['HYPRUNE_MACHINES_STILLS']); out.mkdir(parents=True, exist_ok=True)
    sequence = [int(time.time() * 1000)]
    for still in shots['stills']:
        if still.get('remove'):
            client.call('data.remove', {'sourceId': still['remove']})
        if still.get('publish'):
            sequence[0] = max(sequence[0] + 1, int(time.time() * 1000))
            client.call('data.publish', {'sourceId': still['publish']['sourceId'], 'sequence': sequence[0],
                                         'values': still['publish']['values'], 'ttlMs': 60000})
        camera(still['position'], still['yaw'], still['pitch'])
        time.sleep(still.get('settle', 1.5))
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / f"{still['name']}.png")], check=True)
        print('still', still['name'], flush=True)
    (out / 'machines.json').write_text(json.dumps(machines(), indent=1) + '\n')
