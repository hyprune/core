"""The companion demo recording: HYPRUNE_COMPANION_STAGE=demo dev/nested/run.sh --companion.

Lumen Reach at 1920x1080 (nested only): the camera walks backwards along the Sun Court route
looking at the companion following, speeds up so it runs to catch up, warps to the Array
Hall home and turns round to find it coming, stands still while it idles, Talks to it, and
puts it on a drawn route (tool 3 shows the path). COMPANION_VIDEO names the output (default
<shots>/companion-demo.mp4); a timeline, the smoothness gate and four stills are written
beside it. The camera (the player, whom the companion follows) moves in core per presented
frame (cinema.py) and every presented frame is exported: 60 fps.
"""
import json, math, os, pathlib, subprocess, time
import session as s
import cinema
from m2 import wait, keys

LUMEN = 'com.hyprune.lumen-reach'
EYE = 1.7


def run(client, children, out, comp, look):
    video = pathlib.Path(os.environ.get('COMPANION_VIDEO', str(out / 'companion-demo.mp4')))
    video.parent.mkdir(parents=True, exist_ok=True)
    clip = video.with_suffix('.capture.mkv')
    cam = cinema.Cinema(s)
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

    def track(a, b, speed, back=True, lean=.0, ramp=.6):
        """Walks the eye from feet a to b at speed (from rest, back to rest over `ramp` m),
        looking back along the way (the companion follows behind, in view) or ahead."""
        length = math.hypot(b[0] - a[0], b[2] - a[2])
        yaw = math.atan2(b[0] - a[0], -(b[2] - a[2])) + (math.pi if back else 0) + lean
        at = lambda u: [a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u + EYE, a[2] + (b[2] - a[2]) * u]
        r = min(ramp, length / 3) / length
        cruise = length * (1 - 2 * r) / speed
        ramp_s = 2 * r * length / speed  # a ramp from rest averages half the speed
        keys_ = [{'t': 0, 'eye': at(0), 'yaw': yaw, 'pitch': -.12, 'ease': 'in'},
                 {'t': ramp_s, 'eye': at(r), 'yaw': yaw, 'pitch': -.12},
                 {'t': ramp_s + cruise, 'eye': at(1 - r), 'yaw': yaw, 'pitch': -.12, 'ease': 'out'},
                 {'t': 2 * ramp_s + cruise, 'eye': at(1), 'yaw': yaw, 'pitch': -.12}]
        cam.run(keys_, blend_ms=1200)

    def turn(eye, yaw0, yaw1, pitch=-.12, seconds=1.5):
        yaw1 = yaw0 + math.remainder(yaw1 - yaw0, math.tau)
        cam.run([{'t': 0, 'eye': eye, 'yaw': yaw0, 'pitch': pitch, 'ease': 'inout'},
                 {'t': seconds, 'eye': eye, 'yaw': yaw1, 'pitch': pitch}])

    def warp(home):
        cam.stop()  # the warp moves the camera itself
        client.call('workspace.activate', {'id': home})

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

    # A recording keeps one render scale (dynamic resolution would change it frame to frame).
    client.call('graphics.set', {'patch': {'dynamic_resolution': False, 'render_scale': 1}})
    start = line(0.08)
    first = [start[0], EYE, start[2]]
    cam.hold(first, yaw=yaw_to(start, nodes['court-junction']), pitch=-.12)
    wait(lambda: comp()['state']['state'] == 'active' and comp()['brain'] == 'idle', timeout=30); time.sleep(1.5)
    # Begin facing the companion: turn round on the spot.
    c = comp()['position']
    cam.hold(first, yaw=yaw_to(start, c), pitch=-.12); time.sleep(1.5)

    with cam.recording(clip, warmup=.5) as began:
        t0[0] = began
        mark('Drunk Ash, your companion, in Sun Court')
        time.sleep(1.5)
        mark('Walking: it follows behind and to one side')
        track(line(0.08), line(0.45), 1.3)
        shot('companion-still-1-walking.png')
        mark('Faster: it runs to catch up')
        track(line(0.45), line(0.85), 3.4)
        time.sleep(.3); track(line(0.85), line(0.95), 1.0)
        time.sleep(1.5)
        mark('A warp to the Array Hall home: it arrives out of view')
        warp(22); time.sleep(3)
        eye = client.call('camera.get', {})['position']
        cam_ = s.camera(); yaw0 = cam_.get('yaw', 0.)
        c = comp()['position']
        mark('Turning round: it catches up')
        turn(eye, yaw0, yaw_to(eye, c), seconds=2.0)
        time.sleep(2.5)
        shot('companion-still-2-after-warp.png')
        mark('Standing still: it idles')
        time.sleep(4)
        c = comp()['position']
        cam.move_to(eye, target=[c[0], c[1] + 1.2, c[2]], seconds=.8)
        mark('Talk')
        client.call('interaction.choose', {'objectId': 'companion', 'interactionId': 'talk'}); time.sleep(2.2)
        shot('companion-still-3-talk.png')
        time.sleep(1.5)
        keys('-k', 'e'); time.sleep(.6)
        mark('Back in Sun Court: put it on a drawn route (tool 3)')
        warp(21); time.sleep(2.5)
        client.call('npcs.bind', {'id': 'companion', 'path': route, 'phase': 0})
        eye = [4.5, EYE, 7.5]
        keys('-k', '3'); time.sleep(.3)
        cam.move_to(eye, target=[1.5, .6, 2.5], seconds=1.2)
        time.sleep(4.8)
        shot('companion-still-4-route.png')
        time.sleep(3)
        mark('Follow: it leaves the route and comes back')
        client.call('companion.mode', {'mode': 'follow'}); time.sleep(5)
        keys('-k', '1')
    cam.stop()
    cinema.encode([clip], video, trim=.5)
    gate = cinema.gate(video)
    video.with_suffix('.gate.json').write_text(json.dumps(gate, indent=1) + '\n')
    (video.with_suffix('.timeline.json')).write_text(json.dumps(timeline, indent=1) + '\n')
    print('PASS companion: demo recorded', video, cinema.gate_summary(gate), 'export', json.dumps(cam.last_export), flush=True)
