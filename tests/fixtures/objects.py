#!/usr/bin/env python3
"""Original procedural CC0 world-objects laboratory (world format 0.8): one
armillary-like object asset (spinning ring group, tilted ring with a phase,
orbiting moon, bobbing buoy, invisible collision proxy) placed three times with
different phases on a lightmapped floor. No external assets or downloads."""
import json, math, pathlib, struct, zlib
root = pathlib.Path(__file__).with_suffix(''); root.mkdir(exist_ok=True); (root/'objects').mkdir(exist_ok=True)

class Gltf:
    def __init__(self): self.blob=bytearray(); self.views=[]; self.accessors=[]; self.meshes=[]; self.nodes=[]
    def accessor(self, data, fmt, kind, count, minmax=None):
        start=len(self.blob)
        for item in data: self.blob.extend(struct.pack(fmt,*item))
        self.views.append({'buffer':0,'byteOffset':start,'byteLength':len(self.blob)-start})
        a={'bufferView':len(self.views)-1,'componentType':5126,'count':count,'type':kind}
        if minmax: a['min'],a['max']=minmax
        self.accessors.append(a); return len(self.accessors)-1
    def box(self, size, material, offset=(0,0,0), uv1=False):
        """Axis-aligned box (flat-shaded triangle list) in node-local space; returns a mesh index."""
        c=[[offset[i]+size[i]*v/2 for i,v in enumerate(k)] for k in
           [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
        verts=[c[f[j]] for f in [(0,3,2,1),(4,5,6,7),(0,4,7,3),(1,2,6,5),(3,7,6,2),(0,1,5,4)] for j in (0,1,2,0,2,3)]
        attributes={'POSITION':self.accessor(verts,'<3f','VEC3',len(verts),([min(v[i] for v in verts) for i in range(3)],[max(v[i] for v in verts) for i in range(3)]))}
        if uv1:  # planar XZ lightmap coordinates over the box
            uvs=[((v[0]-offset[0])/size[0]+.5,(v[2]-offset[2])/size[2]+.5) for v in verts]
            attributes['TEXCOORD_1']=self.accessor(uvs,'<2f','VEC2',len(uvs))
        self.meshes.append({'primitives':[{'attributes':attributes,'material':material}]}); return len(self.meshes)-1
    def node(self, **n): self.nodes.append(n); return len(self.nodes)-1
    def write(self, path, scene, materials, generator, extra={}):
        g={'asset':{'version':'2.0','generator':generator},'scene':0,'scenes':[{'nodes':scene}],'nodes':self.nodes,'meshes':self.meshes,
           'materials':materials,'buffers':[{'uri':path.with_suffix('.bin').name,'byteLength':len(self.blob)}],'bufferViews':self.views,'accessors':self.accessors,**extra}
        path.write_text(json.dumps(g)); path.with_suffix('.bin').write_bytes(self.blob)

def pbr(c, emit=.5): return {'pbrMetallicRoughness':{'baseColorFactor':c,'metallicFactor':0,'roughnessFactor':.7},'emissiveFactor':[c[0]*emit,c[1]*emit,c[2]*emit]}
def png(path, w, h, pixel):
    raw=b''.join(b'\0'+bytes(v for x in range(w) for v in pixel(x,y)) for y in range(h))
    chunk=lambda t,d: struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))

# The object asset: rest pose centred on its base, 1.5 m pivot height.
a=Gltf()
base=a.node(name='base',mesh=a.box((.8,.6,.8),0,(0,.3,0)))
frame=[a.node(name=f'ring-equator-{i}',mesh=a.box(s,1,o)) for i,(s,o) in enumerate([((.08,.08,1.6),(0,0,.0)),((.08,1.6,.08),(0,0,0)),((.08,.08,.08),(0,.8,0)),((.08,.08,.08),(0,-.8,0))])]
ring=a.node(name='ring-equator',translation=[0,1.5,0],children=frame)
tilted=a.node(name='ring-tilted',mesh=a.box((1.9,.08,.08),2),translation=[0,1.5,0],rotation=[0,.1494,0,.9888])
moon=a.node(name='moon',mesh=a.box((.3,.3,.3),3),translation=[1.2,1.5,0])
buoy=a.node(name='buoy',mesh=a.box((.25,.25,.25),4),translation=[0,2.6,0])
proxy=a.node(name='proxy',mesh=a.box((.9,.7,.9),0,(0,.35,0)))  # not in the scene graph: collision only
a.write(root/'objects/armillary.gltf',[base,ring,tilted,moon,buoy],
        [pbr((.3,.32,.36,1),.2),pbr((.95,.62,.15,1)),pbr((.2,.75,.9,1)),pbr((.92,.3,.35,1)),pbr((.55,.85,.35,1))],
        'Hyprune original CC0 armillary object fixture')

# The static world: a lightmapped floor (warm baked light), a pillar, a box behind the spawn.
w=Gltf()
floor=w.node(name='floor',mesh=w.box((40,.4,40),0,(0,-.2,0),uv1=True))
pillar=w.node(name='pillar',mesh=w.box((.5,4,.5),1),translation=[-6,2,-6])
behind=w.node(name='behind',mesh=w.box((1.2,1.2,1.2),1),translation=[0,.6,8])
png(root/'lightmap.png',8,8,lambda x,y:(255,170,90))
w.write(root/'scene.gltf',[floor,pillar,behind],[pbr((.6,.6,.6,1),0),pbr((.85,.85,.85,1),0)],'Hyprune original CC0 objects fixture',
        {'images':[{'uri':'lightmap.png','mimeType':'image/png'}],'textures':[{'source':0}]})
animations=[{'node':'ring-equator','type':'spin','axis':[1,0,0],'pivot':[0,1.5,0],'period':4},
            {'node':'ring-tilted','type':'spin','axis':[-.955,0,.295],'pivot':[0,1.5,0],'period':6,'phase':.3},
            {'node':'moon','type':'orbit','axis':[0,1,0],'pivot':[0,1.5,0],'period':8},
            {'node':'buoy','type':'bob','axis':[0,1,0],'amplitude':.15,'period':2}]
world={'formatVersion':'0.8','id':'org.hyprune.objects','version':'1.0.0','name':'World objects laboratory','license':'CC0-1.0',
   'scene':'scene.gltf','defaultSpawn':'arrival',
   'spawns':[{'id':'arrival','pose':{'position':[0,0,0],'rotation':[0,0,0,1]}}],
   'collision':[{'node':floor,'shape':'mesh','layer':'static'},{'node':pillar,'shape':'mesh','layer':'static'}],
   'lightmaps':[{'material':0,'texture':0,'texCoord':1,'encoding':'srgb','intensity':1.5}],
   'objectAssets':[{'id':'armillary','scene':'objects/armillary.gltf','animations':animations,'collision':{'node':'proxy'},'lighting':{'mode':'probe'}}],
   'objects':[{'id':'west','asset':'armillary','pose':{'position':[-3,0,-6],'rotation':[0,0,0,1]},'phase':0},
              {'id':'centre','asset':'armillary','pose':{'position':[0,0,-6],'rotation':[0,0,0,1]},'phase':.33},
              {'id':'east','asset':'armillary','pose':{'position':[3,0,-6],'rotation':[0,.3826834,0,.9238795]},'phase':.66,'scale':1.2}],
   # A dark ink outline (world 0.5 style) so the outline is checked on moving objects.
   'atmosphere':{'fog':{'color':[.028,.045,.065],'density':.008,'heightFalloff':0,'baseHeight':0,'excludeUnlit':True},
                 'sky':{'zenith':[.18,.3,.52],'horizon':[.55,.61,.65],'ground':[.055,.07,.085],
                        'sun':{'direction':[.3,1,.5],'color':[1,.83,.6],'intensity':3,'angularRadius':.025}},
                 'exposure':1,'toneMapping':'aces','bloom':{'strength':.12,'threshold':1,'radius':3},
                 'outline':{'enabled':True,'colour':[.02,.025,.04],'widthPx':2,'depthThreshold':.02,'normalThreshold':.2}},
   'attribution':[{'path':'.','author':'Hyprune contributors','license':'CC0-1.0'}]}
(root/'world.json').write_text(json.dumps(world,indent=2)+'\n')
(root/'LICENSE').write_text('Original procedural geometry dedicated to the public domain under CC0 1.0.\nhttps://creativecommons.org/publicdomain/zero/1.0/\n')
(root/'ATTRIBUTION.md').write_text('All assets generated by tests/fixtures/objects.py; Hyprune contributors; CC0-1.0. No third-party assets.\n')
print(root)
