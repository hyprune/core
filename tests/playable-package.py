#!/usr/bin/env python3
"""Authoritative loader/CLI checks; no compositor or graphics context."""
import json,pathlib,shutil,subprocess,sys,tempfile
checker=pathlib.Path(sys.argv[1]).resolve();fixture=pathlib.Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix='hyprune-bounds-') as tmp:
    root=pathlib.Path(tmp)/'world';shutil.copytree(fixture,root)
    w=json.loads((root/'world.json').read_text());w['formatVersion']='0.6'
    w['playable']={'volumes':[{'type':'box','min':[-10,-1,-10],'max':[10,10,10]}],'maxFlyHeight':8}
    g=json.loads((root/'scene.gltf').read_text())
    # All scene roots explicitly declared as cards; one used sampler clamps.
    w['backdrop']={'farCards':g['scenes'][0]['nodes']}
    g['samplers']=[{'wrapS':33071,'wrapT':10497}]
    for t in g['textures']:t['sampler']=0
    g['materials'][0]['pbrMetallicRoughness']['baseColorTexture']={'index':0}
    def run():
        (root/'world.json').write_text(json.dumps(w));(root/'scene.gltf').write_text(json.dumps(g))
        return subprocess.run([str(checker),str(root)],capture_output=True,text=True)
    p=run();assert p.returncode==0,p.stderr
    assert json.loads(p.stdout)['warnings'] and 'CLAMP_TO_EDGE (S)' in p.stderr,p.stderr
    g['samplers'][0]={'wrapS':10497,'wrapT':33648}
    p=run();assert p.returncode==0 and not json.loads(p.stdout)['warnings'],p.stderr
    g['samplers'][0]['wrapS']=33071;del w['backdrop']
    p=run();assert p.returncode==0 and not json.loads(p.stdout)['warnings'],'undeclared textures not diagnosed'
    w['backdrop']={'farCards':[999999]};assert run().returncode!=0
    w['backdrop']={'farCards':[4294967296]};assert run().returncode!=0
    del w['backdrop'];w['playable']['volumes'][0]['min']=[20,20,20];assert run().returncode!=0
    w['playable']['volumes']=[{'type':'convex','points':[[0,0,0],[1,0,0],[0,1,0],[1,1,0]]}];assert run().returncode!=0
    del w['playable'];p=run();assert p.returncode==0,p.stderr
    # Canonical mounted-area manifests keep their authoring bounds through the
    # normalization path; this used to revalidate everything as an older version.
    area_root=pathlib.Path(tmp)/'areas';shutil.copytree(fixture.parent/'m2',area_root)
    area=json.loads((area_root/'world.json').read_text());area['formatVersion']='0.6'
    area['areas'][0]['playable']={'volumes':[{'type':'box','min':[-20,-1,-20],'max':[20,8,20]}],'maxFlyHeight':6}
    (area_root/'world.json').write_text(json.dumps(area))
    p=subprocess.run([str(checker),str(area_root)],capture_output=True,text=True);assert p.returncode==0,p.stderr
print('PASS package: schema 0.6, bounds validation, declared clamp warnings, repeat/mirror/undeclared quiet, invalid node rejected')
