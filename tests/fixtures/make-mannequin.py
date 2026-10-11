#!/usr/bin/env python3
"""Generate the NPC placeholder mannequin: an original, spec-conforming skinned glTF (GLB).

Throwaway prototype content (no external source, released with this repository): a
20-bone humanoid of subdivided boxes, +Z forward, Y up, metres, ~5.4k triangles, a body
and an `accent` material, `root` bone at the feet, in-place clips idle/walk/run/
turn_left/turn_right/look_around/wave/nod. Flags make spec violations for validator tests.

usage: make-mannequin.py OUT.glb [--root-motion] [--centimetres] [--no-idle] [--extra-bones N]
       [--three-materials] [--rotate-x] [--five-weights] [--accent RGB] [--mixamo] [--no-root]
       [--fingers] [--one-node]
"""
import argparse, json, math, struct

p = argparse.ArgumentParser()
p.add_argument('out'); p.add_argument('--root-motion', action='store_true'); p.add_argument('--centimetres', action='store_true')
p.add_argument('--no-idle', action='store_true'); p.add_argument('--extra-bones', type=int, default=0)
p.add_argument('--three-materials', action='store_true'); p.add_argument('--rotate-x', action='store_true')
p.add_argument('--five-weights', action='store_true'); p.add_argument('--accent', default='0.95,0.55,0.15')
p.add_argument('--mixamo', action='store_true', help='Mixamo bone names (mixamorig:Hips ...)')
p.add_argument('--no-root', action='store_true', help='no root bone: the hips are the top (a raw Mixamo export)')
p.add_argument('--fingers', action='store_true', help='add 3 bones per finger (30 more)')
p.add_argument('--one-node', action='store_true', help='a single mesh node, not body/head/visor')
a = p.parse_args()

# Joints: name, parent, local bind translation (metres). +X is the character's left.
J = [('root', None, (0, 0, 0)), ('hips', 'root', (0, .95, 0)), ('spine', 'hips', (0, .12, 0)), ('chest', 'spine', (0, .18, 0)),
     ('neck', 'chest', (0, .22, 0)), ('head', 'neck', (0, .08, 0))]
for side, x in (('l', 1), ('r', -1)):
    J += [(f'upperarm_{side}', 'chest', (.2 * x, .18, 0)), (f'lowerarm_{side}', f'upperarm_{side}', (0, -.28, 0)),
          (f'hand_{side}', f'lowerarm_{side}', (0, -.26, 0)),
          (f'upperleg_{side}', 'hips', (.1 * x, -.05, 0)), (f'lowerleg_{side}', f'upperleg_{side}', (0, -.42, 0)),
          (f'foot_{side}', f'lowerleg_{side}', (0, -.42, 0)), (f'toe_{side}', f'foot_{side}', (0, -.04, .12))]
for i in range(a.extra_bones):
    J.append((f'extra_{i}', 'head', (0, .01, 0)))
if a.fingers:
    for side, x in (('l', 1), ('r', -1)):
        for f, dx in enumerate((-.03, -.015, 0, .015, .03)):
            parent = f'hand_{side}'
            for k in range(3):
                J.append((f'finger{f}{k}_{side}', parent, (dx * x if k == 0 else 0, -.04 if k else -.11, 0))); parent = f'finger{f}{k}_{side}'
if a.no_root:
    J = [(n, None if par == 'root' else par, t) for n, par, t in J if n != 'root']
names = [j[0] for j in J]; index = {n: i for i, n in enumerate(names)}
glob = []
for n, parent, t in J:
    base = glob[index[parent]] if parent else (0, 0, 0)
    glob.append(tuple(base[k] + t[k] for k in range(3)))

# Boxes rigidly bound to a joint: joint, centre offset from the joint, size, material (0 body, 1 accent).
B = [('hips', (0, .05, 0), (.32, .16, .18), 0), ('spine', (0, .09, 0), (.3, .18, .17), 0), ('chest', (0, .11, 0), (.36, .24, .2), 1),
     ('neck', (0, .04, 0), (.08, .08, .08), 0), ('head', (0, .12, .01), (.2, .24, .22), 0), ('head', (0, .14, .12), (.16, .05, .02), 2)]
for s in 'lr':
    B += [(f'upperarm_{s}', (0, -.14, 0), (.09, .28, .09), 0), (f'lowerarm_{s}', (0, -.13, 0), (.08, .26, .08), 0),
          (f'hand_{s}', (0, -.07, 0), (.07, .12, .04), 0), (f'upperleg_{s}', (0, -.21, 0), (.13, .42, .13), 0),
          (f'lowerleg_{s}', (0, -.21, 0), (.11, .42, .11), 0), (f'foot_{s}', (0, -.02, .05), (.1, .06, .22), 1)]
N = 5  # face subdivision: 18 boxes x 6 faces x 2N^2 = 5400 triangles
scale = 100 if a.centimetres else 1
prims = {}  # (mesh node, material) -> (positions, normals, joints, indices)
def node_of(joint, mat):
    if a.one_node:
        return 'body'
    return 'visor' if mat == 2 else 'head' if joint in ('head', 'neck') else 'body'
faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 1, 0), (0, 0, -1)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
         ((0, -1, 0), (0, 0, -1), (1, 0, 0)), ((0, 0, 1), (-1, 0, 0), (0, 1, 0)), ((0, 0, -1), (1, 0, 0), (0, 1, 0))]
for bi, (joint, off, size, mat) in enumerate(B):
    if a.three_materials and bi == 0:
        mat = 3
    pos, nrm, jnt, idx = prims.setdefault((node_of(joint, mat), mat), ([], [], [], []))
    c = tuple(glob[index[joint]][k] + off[k] for k in range(3))
    for n, u, v in faces:
        start = len(pos) // 3
        for i in range(N + 1):
            for j in range(N + 1):
                s_, t_ = i / N - .5, j / N - .5
                pt = [c[k] + (n[k] * .5 + u[k] * s_ + v[k] * t_) * size[k] for k in range(3)]
                pos += [x * scale for x in pt]; nrm += list(n); jnt.append(index[joint])
        for i in range(N):
            for j in range(N):
                q = start + i * (N + 1) + j
                idx += [q, q + N + 1, q + 1, q + 1, q + N + 1, q + N + 2]

# Clips: per joint keyframes (times, rotation as axis-angle degrees about X/Y/Z), hips height.
def quat(axis, deg):
    h = math.radians(deg) / 2; s = math.sin(h)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h)]
X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)
def cycle(period, steps, f):
    times = [period * i / steps for i in range(steps + 1)]
    return times, [f(i / steps) for i in range(steps + 1)]
clips = {}
def gait(period, swing, knee, arm, bob, speed):
    ch = {}
    for s, phase in (('l', 0), ('r', .5)):
        ch[f'upperleg_{s}'] = cycle(period, 16, lambda u, ph=phase: quat(X, -swing * math.sin(2 * math.pi * (u + ph))))
        ch[f'lowerleg_{s}'] = cycle(period, 16, lambda u, ph=phase: quat(X, knee * max(0, math.sin(2 * math.pi * (u + ph + .25)))))
        ch[f'upperarm_{s}'] = cycle(period, 16, lambda u, ph=phase: quat(X, arm * math.sin(2 * math.pi * (u + ph))))
    hips = cycle(period, 16, lambda u: [0, (.95 - bob * abs(math.sin(2 * math.pi * u))) , 0])
    return {'rotation': ch, 'hips': hips, 'speed': speed}
clips['walk'] = gait(1.0, 25, 20, 15, .02, 1.3)
clips['run'] = gait(.66, 40, 45, 30, .04, 3.0)
if not a.no_idle:
    clips['idle'] = {'rotation': {'chest': cycle(2.0, 8, lambda u: quat(X, 2 * math.sin(2 * math.pi * u))),
                                  'head': cycle(2.0, 8, lambda u: quat(Y, 3 * math.sin(2 * math.pi * u)))}}
for name, sign in (('turn_left', 1), ('turn_right', -1)):
    clips[name] = {'rotation': {'hips': cycle(1.0, 8, lambda u, s=sign: quat(Y, s * 90 * (3 * u * u - 2 * u * u * u)))}}
clips['look_around'] = {'rotation': {'head': cycle(3.0, 12, lambda u: quat(Y, 50 * math.sin(2 * math.pi * u)))}}
clips['wave'] = {'rotation': {'upperarm_r': cycle(2.0, 8, lambda u: quat(Z, 150 * math.sin(math.pi * u))),
                              'lowerarm_r': cycle(2.0, 16, lambda u: quat(Z, 25 * math.sin(6 * math.pi * u)))}}
clips['nod'] = {'rotation': {'head': cycle(1.0, 8, lambda u: quat(X, 15 * math.sin(2 * math.pi * u)))}}

# GLB assembly.
blob = bytearray(); views = []; accessors = []
def add(data, fmt, count, ctype, comp, target=None, minmax=None, normalized=False):
    while len(blob) % 4: blob.append(0)
    off = len(blob); blob.extend(struct.pack('<' + fmt * (len(data)), *data))
    v = {'buffer': 0, 'byteOffset': off, 'byteLength': len(blob) - off}
    if target: v['target'] = target
    views.append(v)
    acc = {'bufferView': len(views) - 1, 'componentType': comp, 'count': count, 'type': ctype}
    if minmax: acc['min'], acc['max'] = minmax
    if normalized: acc['normalized'] = True
    accessors.append(acc); return len(accessors) - 1
gltf = {'asset': {'version': '2.0', 'generator': 'hyprune make-mannequin.py'}, 'scene': 0,
        'extras': {'licence': 'Original placeholder generated by hyprune core tests/fixtures/make-mannequin.py'}}
accent = [float(x) for x in a.accent.split(',')]
materials = [{'name': 'body', 'pbrMetallicRoughness': {'baseColorFactor': [.62, .66, .72, 1], 'metallicFactor': 0, 'roughnessFactor': .8}},
             {'name': 'accent', 'pbrMetallicRoughness': {'baseColorFactor': accent + [1], 'metallicFactor': 0, 'roughnessFactor': .7}},
             {'name': 'visor', 'emissiveFactor': [.3, .9, 1.0], 'pbrMetallicRoughness': {'baseColorFactor': [.05, .08, .1, 1], 'metallicFactor': 0, 'roughnessFactor': .3}}]
if a.three_materials:
    materials.append({'name': 'extra', 'pbrMetallicRoughness': {'baseColorFactor': [.2, .2, .2, 1]}})
meshes, mesh_index = [], {}
for (node, mat), (pos, nrm, jnt, idx) in sorted(prims.items()):
    count = len(pos) // 3
    lo = [min(pos[k::3]) for k in range(3)]; hi = [max(pos[k::3]) for k in range(3)]
    attrs = {'POSITION': add(pos, 'f', count, 'VEC3', 5126, 34962, (lo, hi)), 'NORMAL': add(nrm, 'f', count, 'VEC3', 5126, 34962),
             'TEXCOORD_0': add([0.0] * count * 2, 'f', count, 'VEC2', 5126, 34962),
             'JOINTS_0': add(sum(([j, 0, 0, 0] for j in jnt), []), 'B', count, 'VEC4', 5121, 34962),
             'WEIGHTS_0': add([1.0, 0, 0, 0] * count, 'f', count, 'VEC4', 5126, 34962)}
    if a.five_weights:
        attrs['JOINTS_1'] = add([0, 0, 0, 0] * count, 'B', count, 'VEC4', 5121, 34962)
        attrs['WEIGHTS_1'] = add([0.0] * count * 4, 'f', count, 'VEC4', 5126, 34962)
    if node not in mesh_index:
        mesh_index[node] = len(meshes); meshes.append({'name': node, 'primitives': []})
    meshes[mesh_index[node]]['primitives'].append({'attributes': attrs, 'indices': add(idx, 'H', len(idx), 'SCALAR', 5123, 34963), 'material': mat})
gltf['materials'] = materials
gltf['meshes'] = meshes
ibm = []
for g in glob:
    ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -g[0] * scale, -g[1] * scale, -g[2] * scale, 1]
names = [j[0] for j in J]; index = {n: i for i, n in enumerate(names)}
def bone(n):  # Mixamo naming: mixamorig:Hips, mixamorig:LeftUpLeg ...
    if not a.mixamo:
        return n
    side = {'l': 'Left', 'r': 'Right'}
    base, _, s_ = n.partition('_')
    table = {'hips': 'Hips', 'spine': 'Spine', 'chest': 'Spine1', 'neck': 'Neck', 'head': 'Head', 'upperarm': 'Arm', 'lowerarm': 'ForeArm',
             'hand': 'Hand', 'upperleg': 'UpLeg', 'lowerleg': 'Leg', 'foot': 'Foot', 'toe': 'ToeBase', 'root': 'root'}
    if base.startswith('finger'):
        return f'mixamorig:{side[s_]}HandFinger{base[6]}_{base[7]}'
    return n if base == 'root' else 'mixamorig:' + (side[s_] if s_ else '') + table.get(base, base)
skin_root = names[0]
nodes = [{'name': m['name'], 'mesh': i, 'skin': 0} for i, m in enumerate(meshes)]
base_index = len(nodes)
for n, parent, t in J:
    node = {'name': bone(n), 'translation': [x * scale for x in t]}
    kids = [base_index + index[c] for c, par, _ in J if par == n]
    if kids: node['children'] = kids
    nodes.append(node)
rootnode = {'name': 'armature', 'children': [base_index + index[skin_root]]}
if a.rotate_x:
    rootnode['rotation'] = quat(X, -90)
nodes.append(rootnode)
gltf['nodes'] = nodes; gltf['scenes'] = [{'nodes': list(range(len(meshes))) + [len(nodes) - 1]}]
gltf['skins'] = [{'name': 'mannequin', 'joints': [base_index + i for i in range(len(J))], 'skeleton': base_index + index[skin_root],
                  'inverseBindMatrices': add(ibm, 'f', len(J), 'MAT4', 5126)}]
animations = []
for name, clip in clips.items():
    samplers, channels = [], []
    def channel(node, path, times, values, width):
        samplers.append({'input': add(times, 'f', len(times), 'SCALAR', 5126, None, ([min(times)], [max(times)])),
                         'output': add(sum(values, []), 'f', len(values), 'VEC4' if width == 4 else 'VEC3', 5126), 'interpolation': 'LINEAR'})
        channels.append({'sampler': len(samplers) - 1, 'target': {'node': node, 'path': path}})
    for joint, (times, values) in clip.get('rotation', {}).items():
        channel(base_index + index[joint], 'rotation', times, values, 4)
    if 'hips' in clip:
        times, values = clip['hips']
        channel(base_index + index['hips'], 'translation', times, [[v * scale for v in val] for val in values], 3)
    if a.root_motion and name == 'walk':
        target = 'root' if 'root' in index else 'hips'
        channel(base_index + index[target], 'translation', [0, 1], [[0, .95 * scale * (target == 'hips'), 0], [0, .95 * scale * (target == 'hips'), 1.3 * scale]], 3)
    anim = {'name': name, 'samplers': samplers, 'channels': channels}
    if 'speed' in clip:
        anim['extras'] = {'speed': clip['speed']}
    animations.append(anim)
gltf['animations'] = animations
gltf['buffers'] = [{'byteLength': len(blob)}]; gltf['bufferViews'] = views; gltf['accessors'] = accessors
js = json.dumps(gltf, separators=(',', ':')).encode()
js += b' ' * (-len(js) % 4); blob += b'\0' * (-len(blob) % 4)
with open(a.out, 'wb') as f:
    f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob)))
    f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
    f.write(struct.pack('<II', len(blob), 0x004E4942)); f.write(blob)
print(a.out, len(J), 'joints', sum(len(pr[3]) // 3 for pr in prims.values()), 'triangles', len(clips), 'clips', len(meshes), 'mesh nodes')
