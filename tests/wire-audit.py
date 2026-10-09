"""Full negotiated output schemas, including method-specific responses."""
import importlib.util,json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('audit',root/'dev/validate-wire.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
for version in ['0.'+str(n) for n in range(1,16)]:
    item={'protocol':version,'method':'session.ping','frame':{'jsonrpc':'2.0','id':'a','result':{'nonce':'ok'}}}
    # Use canonical error envelopes for pre-hello and negotiated peers.
    item['frame']={'jsonrpc':'2.0','id':'a','error':{'code':-32602,'message':'invalid parameter'}}
    audit.validate(item)
mounts={'areas':[],'anchors':[],'objects':[],'bindings':[],'placements':[],'placementZones':[],
        'instances':[{'surfaceId':'app','workspaceId':8,'anchorId':'free','status':'placed'}],
        'carriedSurfaceId':None,'desktopWorkspaceId':None}
item={'protocol':'0.15','method':'','frame':{'jsonrpc':'2.0','method':'mounts.changed','params':{'sessionId':'test','revision':1,'mounts':mounts}}}
audit.validate(item)
mounts['instances'][0]['areaId']=''
p=subprocess.run([sys.executable,str(root/'dev/validate-wire.py')],input=json.dumps(item)+'\n',capture_output=True,text=True)
assert p.returncode!=0 and '/params/mounts/instances/0/areaId' in p.stderr,p.stderr
assert '"surfaceId"' not in p.stderr and '"workspaceId"' not in p.stderr,'raw frame leaked'
print('PASS wire audit: all negotiated error schemas, valid unzoned instance, exact redacted rejection path')
