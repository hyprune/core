"""Clamped client strings validate against every negotiated schema (code points, as maxLength counts)."""
import importlib.util,json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('audit',root/'dev/validate-wire.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
lines=subprocess.run([sys.argv[1],'--frames'],check=True,capture_output=True,text=True).stdout.splitlines()
items=[json.loads(l) for l in lines]
assert len(items)==16,len(items)
for item in items:audit.validate(item)
# The boundary is tight: one more code point than the clamp is rejected.
item=items[-2];title=item['frame']['params']['changes']['surfaces'][0]['title']
assert len(title)==512,len(title)
item['frame']['params']['changes']['surfaces'][0]['title']=title+'x'
try:audit.validate(item)
except AssertionError as e:assert 'maxLength' in str(e),e
else:raise AssertionError('513 code points accepted')
print('PASS wire text: 10k multibyte titles clamped and valid for IPC 0.1-0.15')
