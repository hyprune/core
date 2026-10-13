"""The companion demo recording: HYPRUNE_COMPANION_STAGE=demo dev/nested/run.sh --companion.

Lumen Reach at 1920x1080 (nested only): the camera walks backwards along the Sun Court route
looking at the companion following, speeds up so it runs to catch up, warps to the Array
Hall home and turns round to find it coming, stands still while it idles, Talks to it, and
puts it on a drawn route (tool 3 shows the path). COMPANION_VIDEO names the output (default
<shots>/companion-demo.mp4); a timeline and four stills are written beside it.
"""
import json, math, os, pathlib, signal, subprocess, time
import session as s
from m2 import wait, keys, pose

LUMEN = 'com.hyprune.lumen-reach'


def run(client, children, out, comp, look):
    video = pathlib.Path(os.environ.get('COMPANION_VIDEO', str(out / 'companion-demo.mp4')))
    video.parent.mkdir(parents=True, exist_ok=True)
    timeline, t0 = [], [0.]

    def mark(text):
        timeline.append({'t': round(time.monotonic() - t0[0], 2), 'text': text}); print('demo:', text, flush=True)

    def shot(name):
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True)

    def yaw_to(eye, target):
        return math.atan2(target[0] - eye[0], -(target[2] - eye[2]))

    def pitch_to(eye, target):
        d = [target[i] - eye[i] for i in range(3)]
        return math.atan2(d[1], math.hypot(d[0], d[2]))

    def track(points, speed, back=True, lean=.0, fps=30):
        """Walks the eye along points (feet) at speed, looking back along the way (the companion
        follows behind, in view) or ahead; lean turns the view sideways (radians)."""
        legs = [(points[i], points[i + 1]) for i in range(len(points) - 1)]
        for a, b in legs:
            length = math.hypot(b[0] - a[0], b[2] - a[2])
            steps = max(1, int(length / speed * fps))
            heading = math.atan2(b[0] - a[0], -(b[2] - a[2]))
            yaw = heading + (math.pi if back else 0) + lean
            for k in range(1, steps + 1):
                t = k / steps; begun = time.monotonic()
                p = [a[i] + (b[i] - a[i]) * t for i in range(3)]
                pose(client, [p[0], p[1] + 1.7, p[2]], yaw, -.12)
                time.sleep(max(0, 1 / fps - (time.monotonic() - begun)))

    def turn(eye, yaw0, yaw1, pitch=-.12, seconds=1.5, fps=30):
        while yaw1 - yaw0 > math.pi: yaw1 -= 2 * math.pi
        while yaw1 - yaw0 < -math.pi: yaw1 += 2 * math.pi
        n = int(seconds * fps)
        for k in range(1, n + 1):
            a = (1 - math.cos(math.pi * k / n)) / 2
            pose(client, eye, yaw0 + (yaw1 - yaw0) * a, pitch); time.sleep(1 / fps)

    manifest = json.loads((s.STATE / 'worlds' / LUMEN / 'world.json').read_text())
    nodes = {n['id']: n['position'] for n in manifest['extensions']['com.hyprune.lumen-reach.routes']['nodes']}
    line = lambda t: [nodes['arrival'][0] + (nodes['court-junction'][0] - nodes['arrival'][0]) * t, 0,
                      nodes['arrival'][2] + (nodes['court-junction'][2] - nodes['arrival'][2]) * t]
    # Rehearsal (not recorded): the drawn route for the last beat, checked walkable.
    user = [[-1.5, 0, 6.5], [2.5, 0, 4.0], [3.5, 0, 0.5], [1.0, 0, -1.5]]
    made = client.call('paths.set', {'path': {'name': 'Court stroll', 'use': 'route', 'closed': False,
                                              'points': [{'position': p, 'type': 'line'} for p in user]}})
    route = f"user/{made['id']}"
    listed = next(p for p in client.call('paths.list', {})['paths'] if p['ref'] == route)
    print('demo route', route, listed['walkable'], listed['problems'][:2], flush=True)

    start = line(0.08)
    pose(client, [start[0], 1.7, start[2]], yaw_to(start, nodes['court-junction']), -.12)
    wait(lambda: comp()['state']['state'] == 'active' and comp()['brain'] == 'idle', timeout=30); time.sleep(1.5)
    # Begin facing the companion: turn round on the spot.
    c = comp()['position']
    pose(client, [start[0], 1.7, start[2]], yaw_to(start, c), -.12); time.sleep(1.5)

    log = open(s.STATE / 'recorder.log', 'w')
    recorder = s.envrun('wf-recorder', '-o', 'HEADLESS-1', '-r', '30', '-D', '--no-dmabuf', '-c', 'h264_nvenc', '-x', 'bgr0',
                        '-p', 'preset=p5', '-p', 'rc=constqp', '-p', 'qp=18', '-f', str(video), stdout=log, stderr=subprocess.STDOUT)
    children.append(recorder); time.sleep(1.0); t0[0] = time.monotonic()
    try:
        mark('Drunk Ash, your companion, in Sun Court')
        time.sleep(1.5)
        mark('Walking: it follows behind and to one side')
        track([line(0.08), line(0.45)], 1.3)
        shot('companion-still-1-walking.png')
        mark('Faster: it runs to catch up')
        track([line(0.45), line(0.85)], 3.4)
        time.sleep(.3); track([line(0.85), line(0.95)], 1.0)
        time.sleep(1.5)
        mark('A warp to the Array Hall home: it arrives out of view')
        client.call('workspace.activate', {'id': 22}); time.sleep(3)
        eye = client.call('camera.get', {})['position']
        cam = s.camera(); yaw0 = cam.get('yaw', 0.)
        c = comp()['position']
        mark('Turning round: it catches up')
        turn(eye, yaw0, yaw_to(eye, c), seconds=2.0)
        time.sleep(2.5)
        shot('companion-still-2-after-warp.png')
        mark('Standing still: it idles, no frames while nothing moves')
        time.sleep(4)
        c = comp()['position']; eye = client.call('camera.get', {})['position']
        pose(client, eye, yaw_to(eye, c), pitch_to(eye, [c[0], c[1] + 1.2, c[2]])); time.sleep(.5)
        mark('Talk')
        client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'talk'}); time.sleep(2.2)
        shot('companion-still-3-talk.png')
        time.sleep(1.5)
        keys('-k', 'e'); time.sleep(.6)
        mark('Back in Sun Court: put it on a drawn route (tool 3)')
        client.call('workspace.activate', {'id': 21}); time.sleep(2.5)
        client.call('npcs.bind', {'id': 'companion', 'path': route, 'phase': 0})
        eye = [4.5, 1.7, 7.5]
        keys('-k', '3'); time.sleep(.3)
        pose(client, eye, yaw_to(eye, [1.5, 0, 2.5]), pitch_to(eye, [1.5, .6, 2.5])); time.sleep(6)
        shot('companion-still-4-route.png')
        time.sleep(3)
        mark('Follow: it leaves the route and comes back')
        client.call('companion.mode', {'mode': 'follow'}); time.sleep(5)
        keys('-k', '1')
    finally:
        recorder.send_signal(signal.SIGINT); recorder.wait(timeout=30); log.close()
    (video.with_suffix('.timeline.json')).write_text(json.dumps(timeline, indent=1) + '\n')
    print('PASS companion: demo recorded', round(time.monotonic() - t0[0], 1), 's', video, flush=True)
