#!/usr/bin/env python3
"""Original CC0 material laboratory: tiled floor, trim wall, copper and ceramic spheres."""
import json, math, pathlib, struct, zlib, runpy, shutil
base=pathlib.Path(__file__).parent
runpy.run_path(str(base/'generate.py'))
root=base/'materials';shutil.copytree(base/'atrium',root,dirs_exist_ok=True)
g=json.loads((root/'scene.gltf').read_text());w=json.loads((root/'world.json').read_text())
w.update(id='org.hyprune.materials',name='Material laboratory',formatVersion='0.2')
w['zones']=[{'id':'gallery','name':'Material gallery','bounds':{'min':[-3,0,-5],'max':[3,4,-3]}}]
w['spawns'][0]['pose']['position']=[0,0,6]
def png(name,fn):
    n=256
    def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    data=b''.join(b'\0'+bytes(v for x in range(n) for v in (*fn(x,y),255)) for y in range(n))
    (root/name).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',n,n,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b''))
def floor(x,y):
    seam=x%128<4 or y%128<4
    grain=int(7*math.sin(x*1.7+y*2.3))
    return (22,30,35) if seam else (105+grain,128+grain,135+grain)
def normal(x,y):
    nx=.28*math.sin(x*math.tau/32);ny=.28*math.sin(y*math.tau/32)
    return tuple(int((v+1)*127.5) for v in (nx,ny,math.sqrt(1-nx*nx-ny*ny)))
def trim(x,y):
    if y<32:return (245,172,45) if (x+y)%64<32 else (35,45,50)
    if y<48 or y>240:return (40,55,65)
    return (int(140+25*math.sin(y*.15)),95,56)
png('tile.png',floor);png('normal.png',normal);png('trim.png',trim)
png('mr.png',lambda x,y:(17,210,255))
g['samplers']=[{'wrapS':10497,'wrapT':10497,'minFilter':9987,'magFilter':9729},{'wrapS':33648,'wrapT':33071,'minFilter':9987,'magFilter':9729}]
for name in ['tile.png','normal.png','trim.png','mr.png']:
    g['images'].append({'uri':name});g['textures'].append({'source':len(g['images'])-1,'sampler':1 if name=='trim.png' else 0})
g['extensionsUsed']=['KHR_texture_transform'];g['extensionsRequired']=['KHR_texture_transform']
def tex(i,scale=(1,1),rotation=0):return {'index':i,'extensions':{'KHR_texture_transform':{'scale':list(scale),'rotation':rotation,'offset':[.125,0],'texCoord':0}}}
for i,m in enumerate(g['materials']):
    p=m['pbrMetallicRoughness'];p.update(metallicFactor=0,roughnessFactor=.75)
    if i<2:p.update(baseColorFactor=[.8,.8,.8,1],baseColorTexture=tex(1));m['normalTexture']=tex(2)
    if i==2:p.update(baseColorFactor=[.5,.6,.65,1],baseColorTexture=tex(1,(4,2)));m['normalTexture']=tex(2,(4,2))
    if i==3:p.update(baseColorFactor=[1,1,1,1],baseColorTexture=tex(3,(2,1)),metallicFactor=.65,roughnessFactor=.35);m['normalTexture']=tex(2,(2,1))
blob=bytearray((root/'geometry.bin').read_bytes())
def accessor(values,components):
    while len(blob)%4:blob.append(0)
    off=len(blob)
    for value in values:blob.extend(struct.pack('<'+'f'*components,*value))
    g['bufferViews'].append({'buffer':0,'byteOffset':off,'byteLength':len(blob)-off})
    g['accessors'].append({'bufferView':len(g['bufferViews'])-1,'componentType':5126,'count':len(values),'type':f'VEC{components}','min':[min(v[c] for v in values) for c in range(components)],'max':[max(v[c] for v in values) for c in range(components)]})
    return len(g['accessors'])-1
for row in range(2):
 for col in range(3):
    pos=[];norm=[];uv=[]
    for lat in range(32):
     for lon in range(64):
      for da,db in [(0,0),(1,0),(1,1),(0,0),(1,1),(0,1)]:
       u=(lon+da)/64;v=(lat+db)/32;a=u*math.tau;b=v*math.pi
       n=(math.sin(b)*math.cos(a),math.cos(b),math.sin(b)*math.sin(a))
       norm.append(n);pos.append(((col-1)*3+n[0]*.85,1.3+row*2+n[1]*.85,-6+n[2]*.85));uv.append((u,v))
    g['materials'].append({'name':f'{"copper" if row else "ceramic"} roughness {col}', 'pbrMetallicRoughness':{'baseColorFactor':[.72,.32,.12,1] if row else [.15,.45,.52,1],'metallicFactor':row,'roughnessFactor':[.15,.4,.85][col],'metallicRoughnessTexture':tex(4)},'normalTexture':{**tex(2,(8,4),.3),'scale':.5}})
    attrs={'POSITION':accessor(pos,3),'NORMAL':accessor(norm,3),'TEXCOORD_0':accessor(uv,2)}
    g['meshes'].append({'primitives':[{'attributes':attrs,'material':len(g['materials'])-1}]})
    g['nodes'].append({'mesh':len(g['meshes'])-1});g['scenes'][0]['nodes'].append(len(g['nodes'])-1)
g['buffers'][0]['byteLength']=len(blob)
(root/'geometry.bin').write_bytes(blob);(root/'scene.gltf').write_text(json.dumps(g));(root/'world.json').write_text(json.dumps(w,indent=2)+'\n')
print(root)
