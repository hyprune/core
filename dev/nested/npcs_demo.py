#!/usr/bin/env python3
"""NPC demo video: dev/nested/run.sh dev/nested/npcs_demo.py OUT.mp4

Nested only. Lumen Reach (HYPRUNE_NPCS_LUMEN, else the worlds checkout's dist package)
relabelled world format 0.9 with two starter-library residents on routes in the Sun
Court, one stopping at a bench to sit, plus a user resident spawned on a route drawn
with tool 3 during the recording. The camera follows and glides between them, moved
by core per presented frame (cinema.py), and every presented frame is exported at 1080p60.
A JSON sidecar next to OUT.mp4 lists the scenes and the smoothness gate (string values).
"""
import argparse, hashlib, json, math, os, pathlib, shutil, subprocess, sys, time
import session as s
import cinema
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

    cam = cinema.Cinema(s)
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
    # 1. The court: the blue resident walking its loop towards the bench (follow from behind-left).
    follow = lambda id_, back=3.6, side=1.8, height=2.0, blend_ms=0: cam.follow(id_, back=back, side=side, height=height, ahead=1.5,
                                                                              look_height=1.0, half_life=.4, blend_ms=blend_ms)
    follow('blue-walker'); time.sleep(1)
    scenes = []
    clip = video.with_suffix('.capture.mkv')
    rec = cam.recording(clip, warmup=.5); t0 = rec.__enter__()

    def scene(text, seconds, start_shot):
        start = time.monotonic() - t0
        start_shot(); time.sleep(seconds)
        scenes.append({'start': f'{start:.1f}', 'end': f'{time.monotonic() - t0:.1f}', 'text': text})
    scene('Two starter-kit residents walk authored routes in the Sun Court (world format 0.9)', 6, lambda: None)
    # 2. Sitting: from following it, ease round to frame the bench as it approaches, sits
    # and sits on (the shot lasts until it has been seated a few seconds).
    b = BENCH['position']
    bench_eye, bench_target = [b[0] + 2.6, 1.5, b[2] + 3.6], [b[0] - .4, .6, b[2] + .5]
    start = time.monotonic() - t0; seated = None; t1 = time.monotonic()
    cam.hold(bench_eye, bench_target, blend_ms=3000)
    while time.monotonic() - t1 < 22:
        time.sleep(.1)
        a = agent('blue-walker')
        if a['activity'] == 'sitting' and a['clip'] == 'sit_idle' and seated is None:
            seated = time.monotonic()
        if seated and time.monotonic() - seated > 3.5:
            break
    scenes.append({'start': f'{start:.1f}', 'end': f'{time.monotonic() - t0:.1f}',
                   'text': 'At its stop the resident walks to a bench seat anchor and sits (root lifted by the measured seat fit)'})
    # 3. Glance: stand by the east walk, ahead of it; the crimson resident turns its head as it passes.
    scene('Within 4 m a resident glances at the player (head look-at, clamped and eased)', 9,
          lambda: cam.follow('crimson-walker', back=-2.2, side=1.6, height=1.65, ahead=0, look_height=1.45, half_life=.35,
                             heading_half_life=.8, blend_ms=1500))
    # 4. A user resident on a freshly drawn route: tool 3, three clicks, finish.
    start = time.monotonic() - t0
    drawn = client.call('paths.draw', {'use': 'route', 'spawn': {'ref': npcs.EMBER, 'accent': [.95, .55, .15]}})
    cam.move_to([10.5, 4.5, -1], target=USER[0], seconds=1.2)
    for p in USER:
        cam.move_to([p[0] - 1.2, 3.6, p[2] + 4], target=p, seconds=.9)
        time.sleep(.5); click(); time.sleep(.6)
    finished = client.call('paths.finish', {}); keys('-k', '1')
    scenes.append({'start': f'{start:.1f}', 'end': f'{time.monotonic() - t0:.1f}',
                   'text': 'Tool 3 draws a new route; finishing it spawns a resident from the starter library on it'})
    user = finished['bound']
    wait(lambda: agent(user) is not None, timeout=10)
    scene('The spawned resident walks its new route', 9, lambda: follow(user, 3.2, -1.6, 1.9, blend_ms=1500))
    # 5. Out of view costs no frames; closing wide shot of the court.
    remaining = max(8.5, 60 - (time.monotonic() - t0) - 8)
    scene('Wide view: characters animate only while in view; paused or out of view they cost no frames', 8,
          lambda: cam.path([{'t': 0, 'eye': [-12, 7, 22], 'target': [4, 0, 0], 'ease': 'in'},
                            {'t': 8, 'eye': [-6, 9, 24], 'target': [4, 0, -1]},
                            {'t': 8 + remaining, 'eye': [0, 10, 25], 'target': [4, 0, -2]}], blend_ms=1200))
    scene('The Sun Court', remaining, lambda: None)
    rec.__exit__(None, None, None)
    cam.stop()
    cinema.encode([clip], video, trim=.5)
    gate = cinema.gate(video)
    cinema.sidecar(video.with_suffix('.json'), {
        'title': 'NPCs in world format 0.9: residents in the Sun Court', 'world': 'Lumen Reach (local world 0.9 test variant)',
        'library': npcs.ARCHIVE.name, 'resolution': '1920x1080', 'fps': '60', 'duration': f'{gate["frames"] / 60:.0f} s',
        'nested': 'yes', 'smoothness': cinema.gate_summary(gate),
        'scenes': '; '.join(f"{x['start']}-{x['end']} s: {x['text']}" for x in scenes)})
    print('PASS npcs demo', video, cinema.gate_summary(gate), json.dumps(scenes), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    npcs.prepare = prepare
    npcs.acceptance = acceptance
    s.run(argparse.Namespace(npcs=True, m3b=False, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False,
                             record=False, visible=False, output=str(pathlib.Path(a.output).resolve())))
