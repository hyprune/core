#!/usr/bin/env python3
"""The builder fixture's sky (tests/fixtures/builder/sky/bands.png): a 1024 x 512 equirectangular
image in azimuth bands (u 0-.25 red, .25-.5 green, .5-.75 blue, .75-1 white) with a yellow cap
(v < .15), so tests can check which way core turns a panorama. The rest of the folder is
`hyprune builder create "Fixture Plain"` (SDK), with core's imported barrel copied into objects/
and builder/base.json pointing at this sky at 256 px per face. CC0-1.0, Hyprune contributors."""
import json,pathlib,struct,zlib
root=pathlib.Path(__file__).resolve().parent/'builder'
w,h=1024,512
rows=bytearray()
for y in range(h):
    rows.append(0)
    for x in range(w):
        u=(x+.5)/w
        c=(255,230,0) if (y+.5)/h<.15 else [(220,30,30),(30,200,40),(40,60,230),(240,240,240)][int(u*4)]
        rows+=bytes(c)
chunk=lambda t,d:struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(rows),9))+chunk(b'IEND',b'')
(root/'sky').mkdir(exist_ok=True)
(root/'sky/bands.png').write_bytes(png)
for name in ('builder/base.json','world.json'):
    m=json.loads((root/name).read_text())
    m['atmosphere']['panorama'].update(image='sky/bands.png',maxResolution=256)
    for a in m['attribution']:
        if a['path']=='sky/panorama.jpg': a['path']='sky/bands.png'
    (root/name).write_text(json.dumps(m,indent=2)+'\n')
p=json.loads((root/'builder/project.json').read_text())
p['sky'].update(image='sky/bands.png')
p['placements']=[{'id':'u-00b001','ref':'world/barrel','pose':{'position':[2,0,-4],'rotation':[0,0,0,1]},'scale':1}]
(root/'builder/project.json').write_text(json.dumps(p,indent=2)+'\n')
