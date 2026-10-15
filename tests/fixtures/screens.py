#!/usr/bin/env python3
"""Workspace screen fixtures (object format 0.11) for the nested --screens, --screens-shell and
--builder-shell suites: tests/fixtures/screens/<id>/{object.json, object.glb, thumbnail.png}
for workspace-screen-{wall,stand}-{16x9,21x9}. They stand in for the starter kit's screens
(same ids, anchor sizes and mount types) until a starter release ships them; set
HYPRUNE_SCREENS_DIR to test those instead. Plain boxes: a bezel with an accent strip, a dark
panel, a pillar and base for the stands, and a collision proxy behind the screen plane. Original, CC0-1.0,
Hyprune contributors. No external assets."""
import json, pathlib, struct, sys, zlib

ROOT = pathlib.Path(__file__).resolve().parent / 'screens'
SIZES = {'16x9': (3.2, 1.8), '21x9': (4.2, 1.8)}
BORDER, DEPTH, PANEL_Z = .08, .085, .06
MATERIALS = [
    {'name': 'frame', 'pbrMetallicRoughness': {'baseColorFactor': [.045, .05, .056, 1], 'roughnessFactor': .38, 'metallicFactor': .7}},
    {'name': 'panel', 'pbrMetallicRoughness': {'baseColorFactor': [.008, .009, .011, 1], 'roughnessFactor': .25, 'metallicFactor': 0}},
    {'name': 'accent', 'pbrMetallicRoughness': {'baseColorFactor': [.25, .85, 1, 1], 'roughnessFactor': .4, 'metallicFactor': 0},
     'emissiveFactor': [.25, .85, 1]},
]


def box(lo, hi):
    """Triangles (position, normal) of an axis-aligned box, outward faces."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    faces = [((1, 0, 0), [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]),
             ((-1, 0, 0), [(x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0)]),
             ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
             ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
             ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
             ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)])]
    return [(p, n) for n, q in faces for p in (q[0], q[1], q[2], q[0], q[2], q[3])]


def screen(kind, ratio):
    w, h = SIZES[ratio]
    cy = 0 if kind == 'wall' else 1.55  # wall screens hang centred on the origin; stands put the panel at eye height
    hw, hh = w / 2 + BORDER, h / 2 + BORDER
    frame = (box((-hw, cy - hh, 0), (-w / 2, cy + hh, DEPTH)) + box((w / 2, cy - hh, 0), (hw, cy + hh, DEPTH)) +
             box((-w / 2, cy + h / 2, 0), (w / 2, cy + hh, DEPTH)) + box((-w / 2, cy - hh, 0), (w / 2, cy - h / 2, DEPTH)) +
             box((-w / 2, cy - h / 2, 0), (w / 2, cy + h / 2, PANEL_Z - .01)))
    # The collision proxy stays behind the screen plane, so aiming at the screen finds the workspace.
    proxy = box((-hw, cy - hh, 0), (hw, cy + hh, PANEL_Z - .01))
    if kind == 'stand':
        stand = box((-.07, .04, .0), (.07, cy - hh, .05)) + box((-.45, 0, -.25), (.45, .04, .35))
        frame += stand
        proxy += stand
    accent = box((-w / 4, cy - hh, DEPTH - .02), (w / 4, cy - hh + .015, DEPTH + .001))
    panel = box((-w / 2, cy - h / 2, PANEL_Z - .01), (w / 2, cy + h / 2, PANEL_Z - .005))
    points = [p for p, _ in frame + accent + panel]
    lo = [round(min(p[j] for p in points), 4) for j in range(3)]
    hi = [round(max(p[j] for p in points), 4) for j in range(3)]
    return {'bezel': [(frame, 0), (accent, 2)], 'panel': [(panel, 1)], 'proxy': [(proxy, None)]}, lo, hi, (w, h), cy


def glb(meshes):
    blob, views, accessors, gltf_meshes, nodes = bytearray(), [], [], [], []

    def add(data, target, count, kind, extra):
        while len(blob) % 4:
            blob.append(0)
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data), 'target': target})
        blob.extend(data)
        accessors.append({'bufferView': len(views) - 1, 'componentType': 5126 if kind != 'SCALAR' else 5125,
                          'count': count, 'type': kind, **extra})
        return len(accessors) - 1

    for name, primitives in meshes.items():
        prims = []
        for triangles, material in primitives:
            pos = [c for p, _ in triangles for c in p]
            nor = [c for _, n in triangles for c in n]
            n = len(triangles)
            xs = [[p[j] for p, _ in triangles] for j in range(3)]
            prim = {'attributes': {
                'POSITION': add(struct.pack(f'<{len(pos)}f', *pos), 34962, n, 'VEC3',
                                {'min': [min(v) for v in xs], 'max': [max(v) for v in xs]}),
                'NORMAL': add(struct.pack(f'<{len(nor)}f', *nor), 34962, n, 'VEC3', {})},
                'indices': add(struct.pack(f'<{n}I', *range(n)), 34963, n, 'SCALAR', {})}
            if material is not None:
                prim['material'] = material
            prims.append(prim)
        gltf_meshes.append({'name': name, 'primitives': prims})
        nodes.append({'name': name, 'mesh': len(gltf_meshes) - 1})
    while len(blob) % 4:
        blob.append(0)
    doc = {'asset': {'version': '2.0', 'generator': 'hyprune core tests/fixtures/screens.py'},
           'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': gltf_meshes,
           'materials': MATERIALS, 'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(blob)}]}
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    return (struct.pack('<III', 0x46546C67, 2, 28 + len(text) + len(blob)) + struct.pack('<II', len(text), 0x4E4F534A) + text +
            struct.pack('<II', len(blob), 0x004E4942) + bytes(blob))


def thumbnail(ratio):
    """256 x 256: the screen (frame border, dark panel, accent strip) centred on a neutral ground."""
    sw, sh = (232, 131) if ratio == '16x9' else (232, 99)
    x0, y0 = (256 - sw) // 2, (256 - sh) // 2
    rows = bytearray()
    for y in range(256):
        rows.append(0)
        for x in range(256):
            u, v = x - x0, y - y0
            if not (0 <= u < sw and 0 <= v < sh):
                rows += bytes((150, 156, 162))
                continue
            edge = u < 6 or v < 6 or u >= sw - 6 or v >= sh - 6
            accent = v >= sh - 4 and sw // 4 <= u < 3 * sw // 4
            rows += bytes((64, 216, 255) if accent else (40, 44, 50) if edge else (6, 8, 12))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 256, 256, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b''))


def write(root=ROOT):
    for kind in ('wall', 'stand'):
        for ratio in SIZES:
            oid = f'workspace-screen-{kind}-{ratio}'
            meshes, lo, hi, (w, h), cy = screen(kind, ratio)
            folder = root / oid
            folder.mkdir(parents=True, exist_ok=True)
            (folder / 'object.glb').write_bytes(glb(meshes))
            (folder / 'thumbnail.png').write_bytes(thumbnail(ratio))
            aspect = ratio.replace('x', ':')
            manifest = {
                'formatVersion': '0.11', 'id': oid, 'version': '1.0.0',
                'name': f"{'Wall' if kind == 'wall' else 'Standing'} screen {aspect}",
                'description': ('A slim bezel that hangs on a wall and shows a workspace.' if kind == 'wall' else
                                'A screen on a pillar that stands on the floor and shows a workspace.'),
                'category': 'signage', 'tags': ['screen', 'workspace', 'display'] + (['ultrawide'] if ratio == '21x9' else ['widescreen']),
                'scene': 'object.glb', 'license': 'CC0-1.0',
                'attribution': [{'path': '.', 'author': 'Hyprune contributors', 'license': 'CC0-1.0',
                                 'source': 'https://github.com/hyprune/core'}],
                'bounds': {'min': lo, 'max': hi}, 'thumbnail': 'thumbnail.png', 'lighting': {'mode': 'probe'},
                'traits': {
                    'mount': {'anchors': [{'id': 'screen', 'role': 'mounted-workspace',
                                           'mountType': 'hanging-wall' if kind == 'wall' else 'kiosk-pillar',
                                           'pose': {'position': [0, cy, PANEL_Z], 'rotation': [0, 0, 0, 1]}, 'size': [w, h],
                                           'screenType': 'com.hyprune.window', 'frameNode': 'bezel'}]},
                    'collidable': {'node': 'proxy'},
                    'movable': {'surfaces': ['wall' if kind == 'wall' else 'floor'], 'scale': [.5, 2]}}}
            (folder / 'object.json').write_text(json.dumps(manifest, indent=1) + '\n')
    return root


if __name__ == '__main__':
    print(write(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT))
