#!/usr/bin/env python3
"""Debug helper: one redacted path per failure, never tokens or frame payloads."""
import json,sys
from pathlib import Path
from jsonschema import Draft202012Validator
from referencing import Registry,Resource
ROOT=Path(__file__).resolve().parents[1]/'third_party/hyprune-schema'
registry=Registry();documents={}
for path in ROOT.rglob('*.schema.json'):
    doc=json.loads(path.read_text());documents[doc['$id']]=doc
    registry=registry.with_resource(doc['$id'],Resource.from_contents(doc))
validators={}
def validate(item):
    version=item['protocol'];method=item['method'];frame=item['frame']
    base='https://hyprune.com/schema/'+('v0' if version=='0.1' else 'v'+version)+'/ipc.schema.json'
    doc=documents[base]
    if 'error' in frame:definition='errorResponse'
    elif 'method' in frame:definition=frame['method']+'.notification'
    else:definition=method+'.response'
    if definition not in doc.get('$defs',{}):
        # Legacy notification names differ; match its discriminator without
        # weakening any branches or rewriting the canonical contract.
        matches=[k for k,v in doc.get('$defs',{}).items() if v.get('properties',{}).get('method',{}).get('const')==frame.get('method') and 'method' in frame]
        definition=next(iter(matches),None)
    key=(base,definition)
    if key not in validators:validators[key]=Draft202012Validator({'$ref':base+('#/$defs/'+definition if definition else '')},registry=registry)
    errors=list(validators[key].iter_errors(frame))
    if errors:
        def leaves(error):
            return [leaf for child in error.context for leaf in leaves(child)] if error.context else [error]
        error=max([leaf for e in errors for leaf in leaves(e)],key=lambda e:len(e.absolute_path))
        where='/'+ '/'.join(map(str,error.absolute_path))
        # A missing property's name is schema vocabulary, not payload: name it.
        missing=[k for k in error.validator_value if isinstance(error.instance,dict) and k not in error.instance] if error.validator=='required' else []
        raise AssertionError(f'protocol {version} {frame.get("method",method) or "error"} at {where}: {error.validator}'+(f' {missing}' if missing else ''))
if __name__=='__main__':
    import os,time
    trace=open(os.environ['HYPRUNE_WIRE_AUDIT_TRACE'],'a',buffering=1) if os.environ.get('HYPRUNE_WIRE_AUDIT_TRACE') else None
    for line in sys.stdin.buffer:
        started=time.monotonic()
        if trace:  # method and size only (never payloads), written before validating: the last line names a frame that hangs
            head=json.loads(line);trace.write(f'{head["protocol"]} {head["frame"].get("method",head["method"]) or "error"} {len(line)}\n')
        try:validate(json.loads(line))
        except Exception as error:
            print('IPC SCHEMA ASSERTION: '+str(error),file=sys.stderr,flush=True)
            sys.stdout.write('!');sys.stdout.flush();sys.exit(1)
        took=time.monotonic()-started
        if took>2:  # core gives each frame 15 s: name the slow ones (method and size only, never payloads)
            item=json.loads(line);print(f'IPC schema audit: {item["protocol"]} {item["frame"].get("method",item["method"])} took {took:.1f} s ({len(line)} bytes)',file=sys.stderr,flush=True)
        sys.stdout.write('.');sys.stdout.flush()
