"""CC0 visibility lab: a front room and a dense occluded lower room."""
import json,pathlib,shutil,runpy
base=pathlib.Path(__file__).parent
runpy.run_path(str(base/'m3b.py'))
root=base/'visibility';shutil.copytree(base/'m3b',root,dirs_exist_ok=True)
w=json.loads((root/'world.json').read_text());g=json.loads((root/'scene.gltf').read_text())
original=len(g['nodes']);hidden=[]
for i in range(384):
 hidden.append(len(g['nodes']))
 g['nodes'].append({'mesh':len(g['meshes'])-1,'translation':[(i%16)*.22-4,-9-(i//16)*.15,-4]})
 g['scenes'][0]['nodes'].append(hidden[-1])
w.update(formatVersion='0.5',id='org.hyprune.visibility',name='Visibility laboratory')
w['visibility']={'areas':[
 {'id':'upper','bounds':{'min':[-30,-1,-30],'max':[30,10,30]},'nodes':list(range(original)),'visibleFrom':['lower']},
 {'id':'lower','bounds':{'min':[-30,-20,-30],'max':[30,-1,30]},'nodes':hidden,'visibleFrom':[]}]}
w.setdefault('atmosphere',{})
# Inherit the fixture's full atmosphere; the ink extension is optional.
w['atmosphere']['outline']={'enabled':True,'colour':[.02,.025,.04],'widthPx':2,'depthThreshold':.02,'normalThreshold':.2}
(root/'world.json').write_text(json.dumps(w,indent=2)+'\n');(root/'scene.gltf').write_text(json.dumps(g))
