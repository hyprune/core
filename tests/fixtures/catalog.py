#!/usr/bin/env python3
"""Original procedural CC0 object directories (RFC-0011 object format 0.9) for the
catalog tests: a user folder (a floor lamp, a wall sign, a statue without the movable
trait, an object with wrong bounds) and one library (com.example.kit 1.0.0: a crate and a
bench). No external assets or downloads."""
import json, pathlib, shutil, struct, zlib
root = pathlib.Path(__file__).with_suffix('')
shutil.rmtree(root, ignore_errors=True)

def png(path, w, h, colour):
    raw = b''.join(b'\0' + bytes(colour) * w for _ in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def box(lo, hi):
    c = [[(lo, hi)[k >> i & 1][i] for i in range(3)] for k in range(8)]
    faces = [(0, 2, 6, 4), (1, 5, 7, 3), (0, 4, 5, 1), (2, 3, 7, 6), (0, 1, 3, 2), (4, 6, 7, 5)]
    return [c[f[j]] for f in faces for j in (0, 1, 2, 0, 2, 3)]

def scene(path, parts, proxy=None):
    """parts: [(name, lo, hi, rgba)] boxes as named nodes; proxy: (lo, hi) collision box node 'proxy'."""
    blob, views, accessors, meshes, nodes, materials = bytearray(), [], [], [], [], []
    def mesh(verts, material):
        start = len(blob)
        for v in verts:
            blob.extend(struct.pack('<3f', *v))
        views.append({'buffer': 0, 'byteOffset': start, 'byteLength': len(blob) - start})
        accessors.append({'bufferView': len(views) - 1, 'componentType': 5126, 'count': len(verts), 'type': 'VEC3',
                          'min': [min(v[i] for v in verts) for i in range(3)], 'max': [max(v[i] for v in verts) for i in range(3)]})
        meshes.append({'primitives': [{'attributes': {'POSITION': len(accessors) - 1}, 'material': material}]})
        return len(meshes) - 1
    for name, lo, hi, rgba in parts:
        materials.append({'name': name, 'pbrMetallicRoughness': {'baseColorFactor': rgba, 'metallicFactor': 0, 'roughnessFactor': .8}})
        nodes.append({'name': name, 'mesh': mesh(box(lo, hi), len(materials) - 1)})
    if proxy:
        materials.append({'name': 'proxy', 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1]}})
        nodes.append({'name': 'proxy', 'mesh': mesh(box(*proxy), len(materials) - 1)})
    g = {'asset': {'version': '2.0', 'generator': 'hyprune catalog fixture'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}],
         'nodes': nodes, 'meshes': meshes, 'materials': materials, 'buffers': [{'uri': 'object.bin', 'byteLength': len(blob)}],
         'bufferViews': views, 'accessors': accessors}
    path.mkdir(parents=True, exist_ok=True)
    (path / 'object.gltf').write_text(json.dumps(g))
    (path / 'object.bin').write_bytes(blob)

def obj(path, oid, name, bounds, traits, parts, proxy=None, category='decor', thumbnail=True, **extra):
    scene(path, parts, proxy)
    m = {'formatVersion': '0.9', 'id': oid, 'version': '1.0.0', 'name': name, 'category': category, 'tags': ['test'],
         'scene': 'object.gltf', 'license': 'CC0-1.0', 'attribution': [{'path': '.', 'author': 'Hyprune contributors', 'license': 'CC0-1.0'}],
         'bounds': bounds, 'traits': traits, **extra}
    if thumbnail:
        png(path / 'thumbnail.png', 256, 256, (60, 140, 120))
        m['thumbnail'] = 'thumbnail.png'
    (path / 'object.json').write_text(json.dumps(m, indent=1))

user = root / 'user'
obj(user / 'lamp', 'lamp', 'Floor lamp', {'min': [-.2, 0, -.2], 'max': [.2, 1.6, .2]},
    {'decorative': {'animations': [{'node': 'shade', 'type': 'bob', 'axis': [0, 1, 0], 'amplitude': .01, 'period': 3}]},
     'collidable': {'node': 'proxy'}, 'movable': {'surfaces': ['floor'], 'scale': [.5, 2]}},
    [('stand', [-.05, 0, -.05], [.05, 1.3, .05], [.3, .3, .3, 1]), ('shade', [-.2, 1.3, -.2], [.2, 1.6, .2], [1, .8, .4, 1])],
    proxy=([-.15, 0, -.15], [.15, 1.6, .15]), category='lighting')
obj(user / 'wall-sign', 'wall-sign', 'Wall sign', {'min': [-.4, -.2, 0], 'max': [.4, .2, .05]},
    {'movable': {'surfaces': ['wall']}}, [('board', [-.4, -.2, 0], [.4, .2, .05], [.2, .4, .8, 1])], category='signage')
obj(user / 'statue', 'statue', 'Statue', {'min': [-.3, 0, -.3], 'max': [.3, 1, .3]}, {},
    [('body', [-.3, 0, -.3], [.3, 1, .3], [.8, .8, .8, 1])])
obj(user / 'broken', 'broken', 'Broken bounds', {'min': [-.3, .2, -.3], 'max': [.3, 1, .3]}, {'movable': {}},
    [('body', [-.3, 0, -.3], [.3, 1, .3], [.8, .2, .2, 1])])

kit = root / 'libraries' / 'com.example.kit@1.0.0'
obj(kit / 'objects' / 'crate', 'crate', 'Crate', {'min': [-.4, 0, -.4], 'max': [.4, .8, .4]},
    {'collidable': {'node': 'proxy'}, 'movable': {'surfaces': ['floor']}},
    [('body', [-.4, 0, -.4], [.4, .8, .4], [.6, .4, .2, 1])], proxy=([-.4, 0, -.4], [.4, .8, .4]), category='furniture')
obj(kit / 'objects' / 'bench', 'bench', 'Bench', {'min': [-.9, 0, -.25], 'max': [.9, .45, .25]},
    {'movable': {'surfaces': ['floor']}, 'seat': {'anchors': [{'id': 'left', 'position': [-.4, 0, .5], 'yaw': 0, 'height': .45}]}},
    [('seat', [-.9, 0, -.25], [.9, .45, .25], [.5, .3, .2, 1])], category='furniture')
(kit / 'library.json').write_text(json.dumps({'formatVersion': '0.9', 'id': 'com.example.kit', 'version': '1.0.0', 'name': 'Example kit',
    'license': 'CC0-1.0', 'attribution': [{'path': '.', 'author': 'Hyprune contributors', 'license': 'CC0-1.0'}],
    'objects': ['crate', 'bench']}, indent=1))
(kit / 'LICENSE').write_text('CC0-1.0: https://creativecommons.org/publicdomain/zero/1.0/\n')
(kit / 'ATTRIBUTION.md').write_text('# Example kit\n\nOriginal procedural test objects by Hyprune contributors, CC0-1.0.\n')
print(root)
