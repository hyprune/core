#!/usr/bin/env python3
"""Original CC0 M2 laboratory, independent of the release world's authoring pipeline."""
import json, math, pathlib, runpy, shutil, struct, zlib
base=pathlib.Path(__file__).parent
m=runpy.run_path(str(base/'materials.py'))
root=base/'m2';shutil.copytree(base/'materials',root,dirs_exist_ok=True)
g=json.loads((root/'scene.gltf').read_text());w=json.loads((root/'world.json').read_text());blob=bytearray((root/'geometry.bin').read_bytes())
w.update(id='org.hyprune.m2',name='M2 mount and light laboratory',formatVersion='0.3')
w['spawns']=[{'id':'entry','pose':{'position':[0,0,4],'rotation':[0,0,0,1]}},{'id':'hall','pose':{'position':[3,0,1],'rotation':[0,0,0,1]}}]
def anchor(id,pos,size,kind,role,rot=[0,0,0,1]):return {'id':id,'pose':{'position':pos,'rotation':rot},'size':size,'screenType':'com.hyprune.window','mount':{'type':kind,'role':role,'interaction':{'mounted-workspace':'focus-in','tool-mount':'in-place','data-object':'none'}[role]}}
wall='sun-court-slot-01-facade-wall-01';desk='sun-court-slot-01-operator-desk-01';spare='sun-court-slot-01-lectern-01';hall='array-hall-slot-02-hanging-wall-01';hd='array-hall-slot-02-banner-01'
w['anchors']=[anchor(wall,[0,3,-13.8],[8,4.5],'facade-wall','mounted-workspace'),anchor(desk,[-2.7,1.7,-1.6],[3.2,1.8],'operator-desk','tool-mount'),anchor(spare,[2.8,1.4,-1.6],[2.4,1.35],'lectern','tool-mount',[math.sin(-.22),0,0,math.cos(-.22)]),anchor(hall,[3,3,-9],[4,2.25],'hanging-wall','mounted-workspace'),anchor(hd,[5,2,-4],[1.6,2.4],'banner','tool-mount')]
w['areas']=[{'id':'sun-court','workspaceSlot':'01','primaryAnchor':wall,'toolAnchors':[desk,spare],'dataAnchors':[],'theme':'Sun Court','spawnId':'entry','freePlacementZones':['sun-court-free']},{'id':'array-hall','workspaceSlot':'02','primaryAnchor':hall,'toolAnchors':[hd],'dataAnchors':[],'theme':'Array Hall','spawnId':'hall'}]
w['placementZones']=[{'id':'sun-court-free','bounds':{'min':[-2,.6,1],'max':[2,3,3]},'windowSizes':[[1.6,.9],[3.2,1.8]],'clearance':.15}]
w['atmosphere']={'fog':{'color':[.18,.27,.33],'density':.022,'heightFalloff':.16,'baseHeight':0,'excludeUnlit':True},'sky':{'zenith':[.08,.2,.5],'horizon':[.62,.76,.9],'ground':[.12,.065,.035],'sun':{'direction':[.3,.8,.4],'color':[1,.58,.22],'intensity':12,'angularRadius':.08}},'exposure':.9,'toneMapping':'aces','bloom':{'strength':.18,'threshold':1,'radius':3},'environment':{'faces':[f'env-{i}.png' for i in range(6)],'encoding':'srgb','intensity':3,'rotation':0,'maxResolution':128}}
# Panorama faces include original architectural ribs and warm practical strips.
def png(path,fn,n=128):
 def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
 data=b''.join(b'\0'+bytes(c for x in range(n) for c in fn(x,y,n)) for y in range(n))
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',n,n,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b''))
for i in range(6):
 def color(x,y,n):
  if i==2:return (145,185,240,255)
  if i==3:return (45,34,27,255)
  if x%32<3 and y>18:return (245,158,48,255)
  if y<28:return (140,186,235,255)
  if 35<y<45:return (85,140,170,255)
  return (35+int(20*math.sin(x*.05)),48,60,255)
 png(root/f'env-{i}.png',color)
g['materials'][1]['pbrMetallicRoughness'].update(metallicFactor=.4,roughnessFactor=.13)
g['materials'][4].update(emissiveFactor=[.2,1,.6],extensions={'KHR_materials_emissive_strength':{'emissiveStrength':8}})
g['extensionsUsed'].append('KHR_materials_emissive_strength');g['extensionsRequired'].append('KHR_materials_emissive_strength')
def mesh(points,material):
 attrs={}
 for name,values,k in [('POSITION',points,3),('TEXCOORD_0',[(0,0),(1,0),(1,1),(0,0),(1,1),(0,1)],2)]:
  off=len(blob)
  for v in values:blob.extend(struct.pack('<'+'f'*k,*v))
  g['bufferViews'].append({'buffer':0,'byteOffset':off,'byteLength':len(blob)-off});g['accessors'].append({'bufferView':len(g['bufferViews'])-1,'componentType':5126,'count':len(values),'type':f'VEC{k}','min':[min(v[c] for v in values) for c in range(k)],'max':[max(v[c] for v in values) for c in range(k)]});attrs[name]=len(g['accessors'])-1
 g['meshes'].append({'primitives':[{'attributes':attrs,'material':material}]});g['nodes'].append({'mesh':len(g['meshes'])-1});g['scenes'][0]['nodes'].append(len(g['nodes'])-1)
for x,z,color,alpha in [(-3,-4,[.08,.65,.88],.32),(-3,-4.4,[.8,.32,.1],.35),(3,-4,[.08,.7,.3],1)]:
 mat={'name':'glazing' if alpha<1 else 'foliage mask','doubleSided':True,'alphaMode':'BLEND' if alpha<1 else 'MASK','pbrMetallicRoughness':{'baseColorFactor':[*color,alpha],'metallicFactor':0,'roughnessFactor':.22}}
 if alpha==1:
  png(root/'foliage.png',lambda x,y,n:(40,170,70,255 if (x//16+y//16)%2 else 0));g['images'].append({'uri':'foliage.png'});g['textures'].append({'source':len(g['images'])-1});mat['pbrMetallicRoughness']['baseColorTexture']={'index':len(g['textures'])-1}
 g['materials'].append(mat);mesh([(x-1,1,z),(x+1,1,z),(x+1,4,z),(x-1,1,z),(x+1,4,z),(x-1,4,z)],len(g['materials'])-1)
g['buffers'][0]['byteLength']=len(blob);(root/'geometry.bin').write_bytes(blob);(root/'scene.gltf').write_text(json.dumps(g));(root/'world.json').write_text(json.dumps(w,indent=2)+'\n')
print(root)
