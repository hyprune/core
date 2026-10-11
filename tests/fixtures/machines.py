#!/usr/bin/env python3
"""Original procedural CC0 machines fixture (world format 0.8 plus the machines
prototype files, RFC-0013 draft): a small "mini-conveyor" machine asset with two
palette lamps (a repeated input), a spinner, a value-driven lift (bob), an alarm
toggle, a crate stack, a label panel, a track with a stop and a spawn, two effect
anchors; a "token" item asset and a "spark" effect asset; two placed instances
and the bindings file. No external assets or downloads."""
import json, pathlib, struct
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
    def box(self, size, material, offset=(0,0,0)):
        c=[[offset[i]+size[i]*v/2 for i,v in enumerate(k)] for k in
           [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
        verts=[c[f[j]] for f in [(0,3,2,1),(4,5,6,7),(0,4,7,3),(1,2,6,5),(3,7,6,2),(0,1,5,4)] for j in (0,1,2,0,2,3)]
        attributes={'POSITION':self.accessor(verts,'<3f','VEC3',len(verts),([min(v[i] for v in verts) for i in range(3)],[max(v[i] for v in verts) for i in range(3)]))}
        self.meshes.append({'primitives':[{'attributes':attributes,'material':material}]}); return len(self.meshes)-1
    def node(self, **n): self.nodes.append(n); return len(self.nodes)-1
    def write(self, path, scene, materials, generator):
        g={'asset':{'version':'2.0','generator':generator},'scene':0,'scenes':[{'nodes':scene}],'nodes':self.nodes,'meshes':self.meshes,
           'materials':materials,'buffers':[{'uri':path.with_suffix('.bin').name,'byteLength':len(self.blob)}],'bufferViews':self.views,'accessors':self.accessors}
        path.write_text(json.dumps(g)); path.with_suffix('.bin').write_bytes(self.blob)

def pbr(name, c): return {'name':name,'pbrMetallicRoughness':{'baseColorFactor':[*c,1],'metallicFactor':0,'roughnessFactor':.7}}

# Machine asset, front +Z, origin at the base centre. Materials: 0 frame, 1 lamp_1, 2 lamp_2, 3 crate, 4 panel.
m=Gltf()
base=m.node(name='base',mesh=m.box((4,.8,1),0,(0,.4,0)))
lamp1=m.node(name='lamp_1',mesh=m.box((.2,.2,.2),1),translation=[-1,1.2,0])
lamp2=m.node(name='lamp_2',mesh=m.box((.2,.2,.2),2),translation=[1,1.2,0])
spinner=m.node(name='spinner',mesh=m.box((.6,.1,.1),0),translation=[0,1.5,0])
lift=m.node(name='lift',mesh=m.box((.5,.1,.5),0),translation=[1.6,1,0])
alarm=m.node(name='alarm',mesh=m.box((.15,.3,.15),2),translation=[-1.8,1.1,0])
crate=m.node(name='crate',mesh=m.box((.25,.25,.25),3),translation=[-1.6,.925,.3])
panel=m.node(name='panel',mesh=m.box((1,.25,.02),4),translation=[0,1.9,0])
m.write(root/'objects/mini-conveyor.gltf',[base,lamp1,lamp2,spinner,lift,alarm,crate,panel],
        [pbr('frame',(.2,.2,.22)),pbr('lamp_1',(.5,.5,.5)),pbr('lamp_2',(.5,.5,.5)),pbr('crate',(.6,.4,.2)),pbr('panel',(.05,.05,.06))],
        'Hyprune original CC0 machines fixture')
t=Gltf()
token=t.node(name='token',mesh=t.box((.2,.12,.2),0,(0,.06,0)))
t.write(root/'objects/token.gltf',[token],[pbr('token',(.8,.8,.8))],'Hyprune original CC0 machines fixture')
s=Gltf()
spark=s.node(name='spark',mesh=s.box((.05,.05,.05),0))
s.write(root/'objects/spark.gltf',[spark],[{'name':'spark','pbrMetallicRoughness':{'baseColorFactor':[1,1,1,1]},'extensions':{'KHR_materials_unlit':{}}}],
        'Hyprune original CC0 machines fixture')

w=Gltf()
floor=w.node(name='floor',mesh=w.box((30,.4,30),0,(0,-.2,0)))
w.write(root/'scene.gltf',[floor],[pbr('floor',(.6,.6,.6))],'Hyprune original CC0 machines fixture')

lamp_palette=[{'id':'idle','colour':[.3,.3,.32],'emission':.2},{'id':'running','colour':[1,.6,.2],'emission':3},
              {'id':'ok','colour':[.3,1,.6],'emission':3},{'id':'fail','colour':[1,.2,.1],'emission':4},
              {'id':'stale','colour':[.25,.25,.25],'emission':.1}]
attribution=[{'path':'.','author':'Hyprune contributors','license':'CC0-1.0'}]
machine={'formatVersion':'0.11','id':'mini-conveyor','version':'1.0.0','name':'Mini conveyor','category':'machines',
  'scene':'mini-conveyor.gltf','license':'CC0-1.0','attribution':attribution,'bounds':{'min':[-2,0,-.5],'max':[2,2.05,.5]},
  'traits':{'decorative':{'animations':[{'node':'spinner','type':'spin','axis':[0,1,0],'pivot':[0,1.5,0],'period':2},
                                        {'node':'lift','type':'bob','axis':[0,1,0],'amplitude':.4,'period':4}]},
    'machine':{'primary':'status','positions':[{'node':'lift','range':[.75,1.25]}],
      'inputs':[{'id':'status','type':'enum','values':['idle','running','ok','fail'],'default':'idle','label':'Status','explain':'Lamps and spinner.'},
                {'id':'stage','type':'enum','values':['idle','running','ok','fail'],'default':'idle','repeat':2,'label':'Stage','explain':'Per lamp.'},
                {'id':'started','type':'event','label':'Started','explain':'A token per start.'},
                {'id':'queued','type':'int','min':0,'max':12,'default':0,'label':'Queued','explain':'Crates.'},
                {'id':'progress','type':'number','min':0,'max':1,'default':0,'label':'Progress','explain':'Lift height.'},
                {'id':'name','type':'text','maxLength':12,'label':'Name','explain':'Panel.'}],
      'drives':[{'input':'stage.{n}','target':'state:lamp_{n}'},
                {'input':'status','target':'animation:spinner','map':{'cases':[{'equals':'running','to':1}],'else':0}},
                {'input':'status','target':'visible:alarm','map':{'cases':[{'equals':'fail','to':True}],'else':False}},
                {'input':'status','target':'effect:cheer','map':{'becomes':'ok','from':'running'}},
                {'input':'status','target':'emission:lamp_1','map':{'cases':[{'equals':'fail','to':2}],'else':1}},
                {'input':'started','target':'spawn:in'},
                {'input':'queued','target':'count:waiting'},
                {'input':'progress','target':'position:lift'},
                {'input':'name','target':'label:sign'}]}},
  'slots':{'states':[{'id':'lamp_1','materials':['lamp_1'],'palette':lamp_palette},{'id':'lamp_2','nodes':['lamp_2'],'palette':lamp_palette}],
           'labels':[{'id':'sign','pose':{'position':[0,1.9,.011],'rotation':[0,0,0,1]},'size':[1,.25],'maxChars':12}],
           'tracks':[{'id':'belt','speed':1,'points':[{'position':[-2,.8,0],'type':'line'},{'position':[0,.8,0],'type':'line'},{'position':[2,.8,0],'type':'line'}],
                      'items':{'assets':['token'],'max':4,'spacing':.5,'overflow':'drop-oldest'},'sink':True,'stops':[{'point':1,'seconds':1}]}],
           'spawns':[{'id':'in','pose':{'position':[-2,.8,0],'rotation':[0,0,0,1]},'track':'belt','maxRate':2}],
           'stacks':[{'id':'waiting','node':'crate','cap':6,'layout':{'grid':[3,2,1],'spacing':[.3,.3,.3],'fill':'xyz'}}],
           'toggles':[{'id':'alarm','nodes':['alarm']}],
           'effects':[{'id':'cheer','pose':{'position':[0,2.2,0],'rotation':[0,0,0,1]},'kind':'burst','colour':[.3,1,.6],'durationMs':1000}]}}
(root/'objects/mini-conveyor.json').write_text(json.dumps(machine,indent=1)+'\n')
token_manifest={'formatVersion':'0.11','id':'token','version':'1.0.0','name':'Token','category':'machines','scene':'token.gltf','license':'CC0-1.0',
  'attribution':attribution,'bounds':{'min':[-.1,0,-.1],'max':[.1,.12,.1]},'traits':{},
  'slots':{'states':[{'id':'token','materials':['token'],'palette':[{'id':'idle','colour':[.5,.5,.5],'emission':0},{'id':'running','colour':[1,.6,.2],'emission':1}]}]}}
(root/'objects/token.json').write_text(json.dumps(token_manifest,indent=1)+'\n')

animations=machine['traits']['decorative']['animations']
world={'formatVersion':'0.8','id':'org.hyprune.machines','version':'1.0.0','name':'Machines laboratory','license':'CC0-1.0',
   'scene':'scene.gltf','defaultSpawn':'arrival','spawns':[{'id':'arrival','pose':{'position':[0,0,6],'rotation':[0,0,0,1]}}],
   'collision':[{'node':floor,'shape':'mesh','layer':'static'}],
   'objectAssets':[{'id':'mini-conveyor','scene':'objects/mini-conveyor.gltf','animations':animations},
                   {'id':'token','scene':'objects/token.gltf'},
                   {'id':'spark','scene':'objects/spark.gltf','lighting':{'mode':'unlit'}}],
   'objects':[{'id':'machine-a','asset':'mini-conveyor','pose':{'position':[-3,0,0],'rotation':[0,0,0,1]}},
              {'id':'machine-b','asset':'mini-conveyor','pose':{'position':[3,0,0],'rotation':[0,.7071068,0,.7071068]}},
              {'id':'plain','asset':'mini-conveyor','pose':{'position':[0,0,-5],'rotation':[0,0,0,1]}}],
   'attribution':attribution}
(root/'world.json').write_text(json.dumps(world,indent=2)+'\n')
src='org.hyprune.test.line'
bindings={'worldId':'org.hyprune.machines','staleMs':3000,'effectAsset':'spark',
  'manifests':{'mini-conveyor':'objects/mini-conveyor.json','token':'objects/token.json'},
  'instances':{
    'machine-a':{'inputs':{
      'status':{'source':src,'key':'status','map':{'cases':[{'equals':'success','to':'ok'},{'in':['failed','error'],'to':'fail'},
                                                          {'in':['pending','running'],'to':'running'}],'else':'idle'}},
      'stage':{'source':src,'key':'stage.{n}','map':{'cases':[{'equals':'success','to':'ok'},{'equals':'failed','to':'fail'},{'equals':'running','to':'running'}],'else':'idle'}},
      'started':{'source':src,'key':'jobsStarted','map':{'counter':True}},
      'queued':{'source':src,'key':'depth','map':{'scale':{'from':[0,100],'to':[0,6]},'round':'floor'}},
      'progress':{'source':src,'key':'progress'},
      'name':{'source':src,'key':'lastJob'}}},
    'machine-b':{'inputs':{'status':{'source':'org.hyprune.test.other','key':'status'}}}}}
(root/'machines.bindings.json').write_text(json.dumps(bindings,indent=1)+'\n')
(root/'LICENSE').write_text('Original procedural geometry dedicated to the public domain under CC0 1.0.\nhttps://creativecommons.org/publicdomain/zero/1.0/\n')
(root/'ATTRIBUTION.md').write_text('All assets generated by tests/fixtures/machines.py; Hyprune contributors; CC0-1.0. No third-party assets.\n')
print(root)
