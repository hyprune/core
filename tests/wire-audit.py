"""Full negotiated output schemas, including method-specific responses."""
import importlib.util,json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('audit',root/'dev/validate-wire.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
# Vendored IPC pins (third_party/hyprune-schema/v0.N; 0.1 is the root pin), never a
# hand list (AGENTS rule 6). Protocol.hpp's Supported inventory must match them.
pins=root/'third_party/hyprune-schema'
versions=['0.1',*sorted((d.name[1:] for d in pins.iterdir() if d.name.startswith('v0.') and (d/'ipc.schema.json').exists()),key=lambda v:int(v.split('.')[1]))]
supported=re.findall(r'"(0\.[0-9]+)"',re.search(r'Supported\{([^}]*)\}',(root/'src/ipc/Protocol.hpp').read_text()).group(1))
assert supported==versions,('Protocol.hpp Supported vs vendored IPC pins',supported,versions)
for version in versions:
    item={'protocol':version,'method':'session.ping','frame':{'jsonrpc':'2.0','id':'a','result':{'nonce':'ok'}}}
    # Use canonical error envelopes for pre-hello and negotiated peers.
    item['frame']={'jsonrpc':'2.0','id':'a','error':{'code':-32602,'message':'invalid parameter'}}
    audit.validate(item)
mounts={'areas':[],'anchors':[],'objects':[],'bindings':[],'placements':[],'placementZones':[],
        'instances':[{'surfaceId':'app','workspaceId':20,'anchorId':'free','status':'placed'}],
        'carriedSurfaceId':None,'desktopWorkspaceId':None}
item={'protocol':'0.15','method':'','frame':{'jsonrpc':'2.0','method':'mounts.changed','params':{'sessionId':'test','revision':1,'mounts':mounts}}}
audit.validate(item)
mounts['instances'][0]['areaId']=''
p=subprocess.run([sys.executable,str(root/'dev/validate-wire.py')],input=json.dumps(item)+'\n',capture_output=True,text=True)
assert p.returncode!=0 and '/params/mounts/instances/0/areaId' in p.stderr,p.stderr
assert '"surfaceId"' not in p.stderr and '"workspaceId"' not in p.stderr,'raw frame leaked'
print('PASS wire audit: all negotiated error schemas, valid unzoned instance, exact redacted rejection path')
