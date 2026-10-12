#!/usr/bin/env python3
"""Original procedural CC0 NPC laboratory (world format 0.9, RFC-0011 P2): a walled floor
with a block in the middle, a step, a bench object asset with a seat anchor, a world seat
on a low ledge, a keepClear box, two routes (a closed loop and an open line) and a generic
path, and the generated mannequin (make-mannequin.py) as a package character directory:
two route-bound characters and a standing one with a permanent seat. No external assets."""
import json, pathlib, shutil, struct, subprocess, sys
here = pathlib.Path(__file__).resolve().parent
root = here / 'npcs'
shutil.rmtree(root, ignore_errors=True)
(root / 'objects').mkdir(parents=True)
(root / 'characters/mannequin').mkdir(parents=True)


class Gltf:
    def __init__(self): self.blob = bytearray(); self.views = []; self.accessors = []; self.meshes = []; self.nodes = []
    def accessor(self, data, fmt, kind, count, minmax=None):
        start = len(self.blob)
        for item in data: self.blob.extend(struct.pack(fmt, *item))
        self.views.append({'buffer': 0, 'byteOffset': start, 'byteLength': len(self.blob) - start})
        a = {'bufferView': len(self.views) - 1, 'componentType': 5126, 'count': count, 'type': kind}
        if minmax: a['min'], a['max'] = minmax
        self.accessors.append(a); return len(self.accessors) - 1
    def box(self, size, material, offset=(0, 0, 0)):
        c = [[offset[i] + size[i] * v / 2 for i, v in enumerate(k)] for k in
             [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]]
        verts = [c[f[j]] for f in [(0, 3, 2, 1), (4, 5, 6, 7), (0, 4, 7, 3), (1, 2, 6, 5), (3, 7, 6, 2), (0, 1, 5, 4)] for j in (0, 1, 2, 0, 2, 3)]
        mm = ([min(v[i] for v in verts) for i in range(3)], [max(v[i] for v in verts) for i in range(3)])
        self.meshes.append({'primitives': [{'attributes': {'POSITION': self.accessor(verts, '<3f', 'VEC3', len(verts), mm)}, 'material': material}]})
        return len(self.meshes) - 1
    def node(self, **n): self.nodes.append(n); return len(self.nodes) - 1
    def write(self, path, scene, materials, extra={}):
        g = {'asset': {'version': '2.0', 'generator': 'Hyprune original CC0 NPC fixture'}, 'scene': 0, 'scenes': [{'nodes': scene}],
             'nodes': self.nodes, 'meshes': self.meshes, 'materials': materials, 'accessors': self.accessors, 'bufferViews': self.views,
             'buffers': [{'uri': path.with_suffix('.bin').name, 'byteLength': len(self.blob)}], **extra}
        path.write_text(json.dumps(g)); path.with_suffix('.bin').write_bytes(bytes(self.blob))


def pbr(c): return {'pbrMetallicRoughness': {'baseColorFactor': c, 'metallicFactor': 0, 'roughnessFactor': .8}}


# The scene: a 30 m floor with walls round it, a block in the middle (routes go round it), a
# 0.2 m step on the east side and a 0.3 m ledge on the west (the world seat sits on it).
w = Gltf()
parts = [w.node(name='floor', mesh=w.box((30, .4, 30), 0, (0, -.2, 0))),
         w.node(name='block', mesh=w.box((2, 2, 2), 1), translation=[0, 1, 0]),
         w.node(name='step', mesh=w.box((3, .2, 6), 1), translation=[9, .1, 0]),
         w.node(name='ledge', mesh=w.box((1, .3, 3), 1), translation=[-9, .15, 6])]
for i, (size, at) in enumerate([((30, 3, .4), (0, 1.5, 15)), ((30, 3, .4), (0, 1.5, -15)), ((.4, 3, 30), (15, 1.5, 0)), ((.4, 3, 30), (-15, 1.5, 0))]):
    parts.append(w.node(name=f'wall-{i}', mesh=w.box(size, 1), translation=list(at)))
w.write(root / 'scene.gltf', parts, [pbr([.55, .58, .6, 1]), pbr([.8, .78, .74, 1])])

# The bench: a seat object asset (inline traits), with a collision proxy.
b = Gltf()
seat = b.node(name='seat', mesh=b.box((1.6, .45, .5), 0, (0, .225, 0)))
proxy = b.node(name='proxy', mesh=b.box((1.6, .45, .5), 0, (0, .225, 0)))
b.write(root / 'objects/bench.gltf', [seat, proxy], [pbr([.5, .35, .2, 1])])

# The character: the generated mannequin as a package object directory.
glb = root / 'characters/mannequin/object.glb'
subprocess.run([sys.executable, str(here / 'make-mannequin.py'), str(glb)], check=True, stdout=subprocess.DEVNULL)
data = glb.read_bytes(); n = struct.unpack('<I', data[12:16])[0]; g = json.loads(data[20:20 + n])
pos = [g['accessors'][p['attributes']['POSITION']] for m in g['meshes'] for p in m['primitives']]
lo = [min(a['min'][i] for a in pos) for i in range(3)]; hi = [max(a['max'][i] for a in pos) for i in range(3)]
walk = next(a for a in g['animations'] if a['name'] == 'walk').get('extras', {}).get('speed', 1.3)
(root / 'characters/mannequin/object.json').write_text(json.dumps({
    'formatVersion': '0.9', 'id': 'mannequin', 'version': '1.0.0', 'name': 'Mannequin', 'category': 'characters',
    'scene': 'object.glb', 'license': 'CC0-1.0', 'attribution': [{'path': '.', 'author': 'Hyprune contributors', 'license': 'CC0-1.0'}],
    'bounds': {'min': [round(v, 4) for v in lo], 'max': [round(v, 4) for v in hi]},
    'traits': {'character': {'clips': {'idle': {}, 'walk': {'speed': walk}, 'wave': {}, 'nod': {}}, 'height': round(hi[1] - lo[1], 3),
                             'interactions': [{'id': 'hello', 'label': 'Talk', 'action': {'type': 'say', 'lines': ['Hello.']}}]}}}, indent=1))

pt = lambda x, z, y=0, t='line': {'position': [x, y, z], 'type': t}
world = {
    'formatVersion': '0.9', 'id': 'org.hyprune.npcs', 'version': '1.0.0', 'name': 'NPC laboratory', 'license': 'CC0-1.0',
    'scene': 'scene.gltf', 'defaultSpawn': 'start',
    'spawns': [{'id': 'start', 'pose': {'position': [0, 0, 10], 'rotation': [0, 0, 0, 1]}}],
    'collision': [{'node': i, 'shape': 'mesh', 'layer': 'static'} for i in range(len(parts))],
    'zones': [{'id': 'court', 'name': 'Court', 'bounds': {'min': [-15, 0, -15], 'max': [15, 4, 15]}}],
    'attribution': [{'path': '.', 'author': 'Hyprune contributors', 'license': 'CC0-1.0'}],
    'playable': {'volumes': [{'type': 'box', 'min': [-14.5, -1, -14.5], 'max': [14.5, 6, 14.5]}], 'maxFlyHeight': 5},
    'objectAssets': [{'id': 'mannequin', 'dir': 'characters/mannequin'},
                     {'id': 'bench', 'scene': 'objects/bench.gltf', 'collision': {'node': 'proxy'},
                      'traits': {'seat': {'anchors': [{'id': 'left', 'position': [-.4, 0, .6], 'yaw': 0, 'height': .45},
                                                      {'id': 'right', 'position': [.4, 0, .6], 'yaw': 0, 'height': .45}]}}}],
    'paths': [
        {'id': 'loop', 'name': 'Court loop', 'use': 'route', 'closed': True,
         'points': [pt(-5, 5), pt(5, 5), pt(5, -5), pt(-5, -5)],
         'stops': [{'point': 1, 'seconds': 2, 'clip': 'wave'}, {'point': 2, 'seconds': 4, 'seat': 'bench-1/left'},
                   {'point': 3, 'seconds': 2, 'face': {'position': [0, 1, 0]}}]},
        {'id': 'line', 'use': 'route', 'points': [pt(-10, -9), pt(-2, -9, 0, 'curve'), pt(4, -11, 0, 'arc')]},
        {'id': 'to-step', 'use': 'route', 'points': [pt(3, 2), pt(10, 2)]},
        {'id': 'view', 'points': [pt(-12, 12), pt(12, 12)]}],
    'seats': [{'id': 'ledge-seat', 'pose': {'position': [-8.2, 0, 6], 'rotation': [0, -.7071068, 0, .7071068]}, 'height': .3}],
    'keepClear': [{'id': 'door', 'min': [-1, 0, 13], 'max': [1, 2.5, 15]}],
    'objects': [
        {'id': 'walker-1', 'asset': 'mannequin', 'path': 'loop', 'phase': 0, 'accent': [.2, .5, .9],
         'interactions': [{'id': 'talk', 'label': 'Talk', 'action': {'type': 'say', 'lines': ['The court loop is 40 m.', 'Mind the block.']}},
                          {'id': 'where', 'label': 'Mark the step', 'action': {'type': 'waypoint', 'target': {'position': [9, 0, 0]}}},
                          {'id': 'show', 'label': 'Show me the step', 'prompt': 'Follow me', 'action': {'type': 'guide', 'path': 'to-step'}}]},
        {'id': 'walker-2', 'asset': 'mannequin', 'path': 'line', 'phase': .5, 'reverse': True, 'speed': 1.2},
        {'id': 'sitter', 'asset': 'mannequin', 'pose': {'position': [-7.5, 0, 6], 'rotation': [0, -.7071068, 0, .7071068]}, 'seat': 'ledge-seat'},
        {'id': 'bench-1', 'asset': 'bench', 'pose': {'position': [6.2, 0, -5], 'rotation': [0, -.7071068, 0, .7071068]}}]}
(root / 'world.json').write_text(json.dumps(world, indent=1))
print(root)
