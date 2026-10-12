#!/usr/bin/env python3
"""NPC demo video: dev/nested/run.sh dev/nested/npcs_demo.py OUT.mp4

Nested only. Lumen Reach (HYPRUNE_NPCS_LUMEN, else the worlds checkout's dist package)
relabelled world format 0.9 with two starter-library residents on routes in the Sun
Court, one stopping at a bench to sit, plus a user resident spawned on a route drawn
with tool 3 during the recording. The camera glides between them at 1080p60
(wf-recorder, x264). A JSON sidecar next to OUT.mp4 lists the scenes (string values).
"""
import argparse, hashlib, json, math, os, pathlib, shutil, signal, subprocess, sys, time
import session as s
import npcs
from m2 import wait, keys

WORLD = 'com.hyprune.lumen-reach'
LOOP = [[-4, 11], [10, 11], [10, 3], [-4, 3]]
EAST = [[2, -19], [2, -3]]
USER = [[6, 0, -8], [15, 0, -8], [15, 0, -1]]
BENCH = {'position': [-4, 0, 1.2], 'rotation': [0, 0, 0, 1]}


def bench_gltf(path):
    """A plain slatted bench, 1.6 m wide, seat 0.3 m high, with a box collision proxy."""
    blob = bytearray(); views = []; accessors = []; meshes = []; nodes = []

    def box(size, offset, material):
        c = [[offset[i] + size[i] * v / 2 for i, v in enumerate(k)] for k in
             [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]]
        verts = [c[f[j]] for f in [(0, 3, 2, 1), (4, 5, 6, 7), (0, 4, 7, 3), (1, 2, 6, 5), (3, 7, 6, 2), (0, 1, 5, 4)] for j in (0, 1, 2, 0, 2, 3)]
        start = len(blob)
        import struct
        for v in verts: blob.extend(struct.pack('<3f', *v))
        views.append({'buffer': 0, 'byteOffset': start, 'byteLength': len(blob) - start})
        accessors.append({'bufferView': len(views) - 1, 'componentType': 5126, 'count': len(verts), 'type': 'VEC3',
                          'min': [min(v[i] for v in verts) for i in range(3)], 'max': [max(v[i] for v in verts) for i in range(3)]})
        meshes.append({'primitives': [{'attributes': {'POSITION': len(accessors) - 1}, 'material': material}]})
        return len(meshes) - 1
    parts = []
    for i, z in enumerate((-.15, 0, .15)):
        nodes.append({'name': f'slat-{i}', 'mesh': box((1.6, .05, .12), (0, .275, z), 0)}); parts.append(len(nodes) - 1)
    for i, x in enumerate((-.65, .65)):
        nodes.append({'name': f'leg-{i}', 'mesh': box((.1, .25, .42), (x, .125, 0), 1)}); parts.append(len(nodes) - 1)
    nodes.append({'name': 'proxy', 'mesh': box((1.6, .3, .45), (0, .15, 0), 1)})
    gltf = {'asset': {'version': '2.0', 'generator': 'Hyprune NPC demo bench (CC0)'}, 'scene': 0, 'scenes': [{'nodes': parts}],
            'nodes': nodes, 'meshes': meshes, 'accessors': accessors, 'bufferViews': views,
            'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [.55, .36, .2, 1], 'metallicFactor': 0, 'roughnessFactor': .7}},
                          {'pbrMetallicRoughness': {'baseColorFactor': [.18, .19, .21, 1], 'metallicFactor': .6, 'roughnessFactor': .5}}],
            'buffers': [{'uri': path.with_suffix('.bin').name, 'byteLength': len(blob)}]}
    path.write_text(json.dumps(gltf)); path.with_suffix('.bin').write_bytes(bytes(blob))


def prepare(installed):
    source = pathlib.Path(os.environ.get('HYPRUNE_NPCS_LUMEN', str(s.ROOT.parent / 'worlds/dist' / WORLD)))
    target = installed / WORLD
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(source, target)
    (target / 'objects').mkdir(exist_ok=True)
    bench_gltf(target / 'objects/demo-bench.gltf')
    w = json.loads((target / 'world.json').read_text())
    pt = lambda p, t='line': {'position': [p[0], 0, p[1]], 'type': t}
    w['formatVersion'] = '0.9'
    w['objectAssets'] += [{'id': 'blue', 'library': npcs.LIBRARY, 'object': 'resident-blue'},
                          {'id': 'crimson', 'library': npcs.LIBRARY, 'object': 'resident-crimson'},
                          {'id': 'bench', 'scene': 'objects/demo-bench.gltf', 'collision': {'node': 'proxy'},
                           'traits': {'seat': {'anchors': [{'id': 'left', 'position': [-.4, 0, .6], 'yaw': 0, 'height': .3},
                                                           {'id': 'right', 'position': [.4, 0, .6], 'yaw': 0, 'height': .3}]}}}]
    w['paths'] = [{'id': 'court-loop', 'name': 'Court loop', 'use': 'route', 'closed': True, 'points': [pt(p) for p in LOOP],
                   'stops': [{'point': 3, 'seconds': 10, 'seat': 'court-bench/left'}, {'point': 1, 'seconds': 2.5, 'clip': 'wave'}]},
                  {'id': 'east-walk', 'name': 'East walk', 'use': 'route', 'points': [pt(p) for p in EAST]}]
    w['objects'] += [{'id': 'blue-walker', 'asset': 'blue', 'path': 'court-loop', 'phase': .55, 'accent': [.2, .5, .95]},
                     {'id': 'crimson-walker', 'asset': 'crimson', 'path': 'east-walk', 'phase': .1},
                     {'id': 'court-bench', 'asset': 'bench', 'pose': BENCH}]
    (target / 'world.json').write_text(json.dumps(w, indent=1))
    shutil.rmtree(s.STATE / 'datahome', ignore_errors=True)
    (s.STATE / 'datahome').mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    for name in ['editor.json', 'layout.json', 'input.json', 'graphics.json']:
        (s.STATE / name).unlink(missing_ok=True)
    shutil.copy2(npcs.ARCHIVE, s.STATE / npcs.ARCHIVE.name)


def acceptance(client, children, args, shell_token, socketpath):
    video = pathlib.Path(args.output)
    children.append(s.envrun('wtype', '-s', '300000')); time.sleep(.2)
    device = s.envrun(str(s.ROOT / 'build/nested-pointer'), 'serve', stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(device)

    def pointer(*a):
        device.stdin.write(' '.join(map(str, a)) + '\n'); device.stdin.flush()
        assert device.stdout.readline().strip() == 'ok'; time.sleep(.05)

    def click():
        pointer('button', 272, 1); time.sleep(.1); pointer('button', 272, 0); time.sleep(.2)
    diag = lambda: s.camera()['npcs']
    agent = lambda id_: next((a for a in diag()['agents'] if a['id'] == id_), None)

    def camera(pos, yaw, pitch=0.):
        s.ctl('eval', f'hl.plugin.hyprune.camera({pos[0]},{pos[1]},{pos[2]},{yaw},{pitch})')

    def look(eye, target):
        d = [target[i] - eye[i] for i in range(3)]
        camera(eye, math.atan2(d[0], -d[2]), math.atan2(d[1], math.hypot(d[0], d[2])))

    def glide(seconds, path):
        """path(u) -> (eye, target) for u in 0..1, sampled at about 25 Hz."""
        t0 = time.monotonic()
        while (u := (time.monotonic() - t0) / seconds) < 1:
            eye, target = path(u)
            look(eye, target); time.sleep(.04)
    mix = lambda a, b, u: [a[i] + (b[i] - a[i]) * u for i in range(3)]
    ease = lambda u: u * u * (3 - 2 * u)
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    archive = s.STATE / npcs.ARCHIVE.name
    client.call('libraries.install', {'archive': str(archive), 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
    client.call('world.load', {'worldId': WORLD})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")'); wait(lambda: s.camera()['active'], timeout=90); time.sleep(2)
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    client.call('graphics.set', {'patch': {'npc_motion': 'always', 'offload': 'off'}})
    for _ in range(4):
        xy = list(map(float, s.ctl('cursorpos').split(','))); pointer(2560 - xy[0], 540 - xy[1]); time.sleep(.04)
    wait(lambda: len(diag()['agents']) == 2, timeout=30)
    # Start when the blue resident walks the south edge (z = 3) about 6 m from its bench stop.
    wait(lambda: (lambda a: a if abs(a['position'][2] - 3) < .2 and 1.5 < a['position'][0] < 2.5 and a['activity'] == 'walking' else None)(
        agent('blue-walker')), timeout=180)
    look([2, 3.2, 16], [2, .8, 6]); time.sleep(1)
    scenes = []
    rec = s.envrun('wf-recorder', '-o', 'HEADLESS-1', '-r', '60', '-c', 'libx264', '-p', 'preset=veryfast', '-p', 'crf=18',
                   '-f', str(video), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5); t0 = time.monotonic()

    def scene(text, seconds, path):
        start = time.monotonic() - t0
        glide(seconds, path)
        scenes.append({'start': f'{start:.1f}', 'end': f'{time.monotonic() - t0:.1f}', 'text': text})
    # 1. The court: the blue resident walking its loop towards the bench (follow from behind-left).
    def follow(id_, back=3.6, side=1.8, height=2.0):
        def path(u):
            a = agent(id_); p = a['position']; h = a['heading']
            f = [math.sin(h), math.cos(h)]; left = [f[1], -f[0]]
            return [p[0] - f[0] * back + left[0] * side, height, p[2] - f[1] * back + left[1] * side], [p[0] + f[0] * 1.5, 1.0, p[2] + f[1] * 1.5]
        return path
    scene('Two starter-kit residents walk authored routes in the Sun Court (world format 0.9)', 6, follow('blue-walker'))
    # 2. Sitting: from following it, ease round to frame the bench as it approaches, sits
    # and sits on (the shot lasts until it has been seated a few seconds).
    b = BENCH['position']
    bench_eye, bench_target = [b[0] + 2.6, 1.5, b[2] + 3.6], [b[0] - .4, .6, b[2] + .5]
    start = time.monotonic() - t0; seated = None; t1 = time.monotonic()
    while time.monotonic() - t1 < 22:
        u = min(1, (time.monotonic() - t1) / 3)
        eye, target = follow('blue-walker')(0)
        look(mix(eye, bench_eye, ease(u)), mix(target, bench_target, ease(u))); time.sleep(.04)
        a = agent('blue-walker')
        if a['activity'] == 'sitting' and a['clip'] == 'sit_idle' and seated is None:
            seated = time.monotonic()
        if seated and time.monotonic() - seated > 3.5:
            break
    scenes.append({'start': f'{start:.1f}', 'end': f'{time.monotonic() - t0:.1f}',
                   'text': 'At its stop the resident walks to a bench seat anchor and sits (root lifted by the measured seat fit)'})
    # 3. Glance: stand by the east walk; the crimson resident turns its head as it passes.
    def glance(u):
        a = agent('crimson-walker'); p = a['position']
        eye = [p[0] + 1.6, 1.65, p[2] + (2.2 if math.cos(a['heading']) > 0 else -2.2)]
        return eye, [p[0], 1.45, p[2]]
    scene('Within 4 m a resident glances at the player (head look-at, clamped and eased)', 9, glance)
    # 4. A user resident on a freshly drawn route: tool 3, three clicks, finish.
    start = time.monotonic() - t0
    drawn = client.call('paths.draw', {'use': 'route', 'spawn': {'ref': npcs.EMBER, 'accent': [.95, .55, .15]}})
    eye = [10.5, 4.5, -1]
    for p in USER:
        for k in range(20):
            look(mix(eye, [p[0] - 1.2, 3.6, p[2] + 4], k / 19), p); time.sleep(.04)
        eye = [p[0] - 1.2, 3.6, p[2] + 4]
        time.sleep(.5); click(); time.sleep(.6)
    finished = client.call('paths.finish', {}); keys('-k', '1')
    scenes.append({'start': f'{start:.1f}', 'end': f'{time.monotonic() - t0:.1f}',
                   'text': 'Tool 3 draws a new route; finishing it spawns a resident from the starter library on it'})
    user = finished['bound']
    wait(lambda: agent(user) is not None, timeout=10)
    scene('The spawned resident walks its new route', 9, follow(user, 3.2, -1.6, 1.9))
    # 5. Out of view costs no frames; closing wide shot of the court.
    scene('Wide view: characters animate only while in view; paused or out of view they cost no frames', 8,
          lambda u: (mix([-12, 7, 22], [-6, 9, 24], ease(u)), [4, 0, 0]))
    remaining = 60 - (time.monotonic() - t0)
    if remaining > 0:
        scene('The Sun Court', remaining, lambda u: (mix([-6, 9, 24], [0, 10, 25], ease(u)), [4, 0, -2]))
    rec.send_signal(signal.SIGINT)
    for _ in range(600):
        if rec.poll() is not None:
            break
        time.sleep(.1)
    sidecar = {'title': 'NPCs in world format 0.9: residents in the Sun Court', 'world': 'Lumen Reach 0.10.2 (local world 0.9 test variant)',
               'library': 'com.hyprune.starter 0.1.0', 'resolution': '1920x1080', 'fps': '60', 'duration': f'{time.monotonic() - t0:.0f} s',
               'nested': 'yes', 'scenes': '; '.join(f"{x['start']}-{x['end']} s: {x['text']}" for x in scenes)}
    video.with_suffix('.json').write_text(json.dumps(sidecar, indent=1) + '\n')
    print('PASS npcs demo', video, json.dumps(scenes), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, output=str(pathlib.Path(a.output).resolve())))
