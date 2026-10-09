#!/usr/bin/env python3
"""Pinned world schemas match their recorded schema-repo bytes, and the embedded
offline copy (src/world/SchemaData.hpp) is regenerated from them."""
import hashlib,json,pathlib,subprocess,sys,tempfile,shutil
root=pathlib.Path(__file__).resolve().parent.parent
for pin in sorted((root/'third_party/hyprune-schema').glob('*/WORLD-SOURCE.json')):
    source=json.loads(pin.read_text())
    for name,expected in source['sha256'].items():
        actual=hashlib.sha256((pin.parent/name).read_bytes()).hexdigest()
        if actual!=expected: sys.exit(f'{pin.parent.name}/{name}: pinned bytes changed (schema {source["commit"]})')
with tempfile.TemporaryDirectory() as tmp:
    copy=pathlib.Path(tmp)/'core';(copy/'dev').mkdir(parents=True);(copy/'src/world').mkdir(parents=True)
    shutil.copy(root/'dev/embed-schema.py',copy/'dev');shutil.copytree(root/'third_party',copy/'third_party',symlinks=True,ignore=lambda d,f:[x for x in f if x in ('basisu','meshoptimizer','fidelityfx-fsr')])
    subprocess.run([sys.executable,str(copy/'dev/embed-schema.py')],check=True)
    if (copy/'src/world/SchemaData.hpp').read_bytes()!=(root/'src/world/SchemaData.hpp').read_bytes():
        sys.exit('src/world/SchemaData.hpp is stale: run dev/embed-schema.py')
print('PASS pinned world schemas and embedded copy')
