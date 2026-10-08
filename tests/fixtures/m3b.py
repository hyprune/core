"""CC0 M3B laboratory: M3A stairs plus an independently editable solid object."""
import runpy,pathlib,shutil,json,struct
base=pathlib.Path(__file__).parent;runpy.run_path(str(base/'m3a.py'));root=base/'m3b';shutil.copytree(base/'m3a',root,dirs_exist_ok=True)
g=json.loads((root/'scene.gltf').read_text());w=json.loads((root/'world.json').read_text());blob=bytearray((root/'geometry.bin').read_bytes());points=[]
p=[(2+(-.5 if i&1==0 else .5),1.8+(-.5 if i&2==0 else .5),0+(-.5 if i&4==0 else .5))for i in range(8)]
for a,b,c,d in [(0,1,3,2),(4,6,7,5),(0,4,5,1),(2,3,7,6),(0,2,6,4),(1,5,7,3)]:points.extend([p[a],p[b],p[c],p[a],p[c],p[d]])
off=len(blob)
for v in points:blob.extend(struct.pack('<fff',*v))
g['bufferViews'].append({'buffer':0,'byteOffset':off,'byteLength':len(blob)-off});g['accessors'].append({'bufferView':len(g['bufferViews'])-1,'componentType':5126,'count':len(points),'type':'VEC3','min':[1.5,1.3,-.5],'max':[2.5,2.3,.5]})
g['materials'].append({'doubleSided':True,'pbrMetallicRoughness':{'baseColorFactor':[.9,.55,.15,1],'metallicFactor':.2,'roughnessFactor':.35}})
g['meshes'].append({'primitives':[{'attributes':{'POSITION':len(g['accessors'])-1},'material':len(g['materials'])-1}]});g['nodes'].append({'mesh':len(g['meshes'])-1});g['scenes'][0]['nodes'].append(len(g['nodes'])-1);w['collision'].append({'node':len(g['nodes'])-1,'shape':'mesh','layer':'static'})
g['buffers'][0]['byteLength']=len(blob);w.update(id='org.hyprune.m3b',name='Keymap v2 interaction laboratory')
# A tool mount behind the solid cube verifies that use tolerance cannot bypass occlusion.
hidden=json.loads(json.dumps(w['anchors'][1]));hidden['id']='occluded-window';hidden['pose']['position']=[2,1.8,-4.6];hidden['size']=[1.4,.9];w['anchors'].append(hidden);w['areas'][0]['toolAnchors'].append(hidden['id'])
(root/'geometry.bin').write_bytes(blob);(root/'scene.gltf').write_text(json.dumps(g));(root/'world.json').write_text(json.dumps(w,indent=2)+'\n')
