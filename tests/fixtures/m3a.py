"""Original CC0 discrete-stair acceptance fixture, extending the M2 laboratory."""
import runpy,pathlib,shutil,json,struct
base=pathlib.Path(__file__).parent
runpy.run_path(str(base/'m2.py'))
root=base/'m3a';shutil.copytree(base/'m2',root,dirs_exist_ok=True)
g=json.loads((root/'scene.gltf').read_text());w=json.loads((root/'world.json').read_text());blob=bytearray((root/'geometry.bin').read_bytes());points=[]
def quad(a,b,c,d):points.extend([a,b,c,a,c,d])
for i in range(4):
 z=1-i*.7;h=(i+1)*.25;last=i*.25
 quad((-5,h,z),(-3,h,z),(-3,h,z-.7),(-5,h,z-.7))
 quad((-5,last,z),(-3,last,z),(-3,h,z),(-5,h,z))
quad((-5,0,1),(-5,1,-1.8),(-5,0,-1.8),(-5,0,1))
off=len(blob)
for v in points:blob.extend(struct.pack('<fff',*v))
g['bufferViews'].append({'buffer':0,'byteOffset':off,'byteLength':len(blob)-off});g['accessors'].append({'bufferView':len(g['bufferViews'])-1,'componentType':5126,'count':len(points),'type':'VEC3','min':[min(v[j] for v in points) for j in range(3)],'max':[max(v[j] for v in points) for j in range(3)]})
g['materials'].append({'pbrMetallicRoughness':{'baseColorFactor':[.35,.45,.55,1],'metallicFactor':0,'roughnessFactor':.7}})
g['meshes'].append({'primitives':[{'attributes':{'POSITION':len(g['accessors'])-1},'material':len(g['materials'])-1}]});g['nodes'].append({'mesh':len(g['meshes'])-1});g['scenes'][0]['nodes'].append(len(g['nodes'])-1);w['collision'].append({'node':len(g['nodes'])-1,'shape':'mesh','layer':'static'})
g['buffers'][0]['byteLength']=len(blob);w.update(id='org.hyprune.m3a',name='Hands-on interaction laboratory')
(root/'geometry.bin').write_bytes(blob);(root/'scene.gltf').write_text(json.dumps(g));(root/'world.json').write_text(json.dumps(w,indent=2)+'\n')
