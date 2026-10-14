#!/usr/bin/env python3
"""hyprune-walk-check: probes run with core's walker against a package (no compositor)."""
import json,pathlib,subprocess,sys,tempfile
checker=pathlib.Path(sys.argv[1]).resolve();fixture=pathlib.Path(sys.argv[2]).resolve()
probes=[
    # Run (30 m/s, 20 fps frames) square into the east wall: stopped, standing, nothing crossed.
    {'id':'wall','from':[0,1.7,4],'to':[20,1.7,4],'mode':'walk','speed':30,'dt':.05,'seconds':2},
    {'id':'glancing','from':[0,1.7,4],'to':[20,1.7,-2],'mode':'walk','speed':10,'dt':1/60,'seconds':3},
    # Autodrive to a floor point arrives there.
    {'id':'drive','from':[0,1.7,4],'to':[2,1.7,0],'mode':'autodrive','speed':3.5,'seconds':4},
    # Outside the room there is no floor: the start is reported unsupported.
    {'id':'outside','from':[30,1.7,4],'to':[40,1.7,4],'mode':'walk','speed':4,'seconds':.5}]
with tempfile.NamedTemporaryFile('w',suffix='.json') as f:
    json.dump(probes,f);f.flush()
    p=subprocess.run([str(checker),str(fixture),f.name],capture_output=True,text=True)
assert p.returncode==0,p.stderr
r={x['id']:x for x in map(json.loads,p.stdout.splitlines())}
assert set(r)=={'wall','glancing','drive','outside'},r
for k in ['wall','glancing']:
    x=r[k];assert x['startGrounded'] and x['grounded'] and x['crossed'] is None and x['end'][0]<7 and abs(x['end'][1]-1.7)<.02,x
assert abs(r['drive']['end'][0]-2)<.1 and abs(r['drive']['end'][2])<.1 and r['drive']['grounded'],r['drive']
assert not r['outside']['startGrounded'] and not r['outside']['grounded'] and r['outside']['minY']<0,r['outside']
assert subprocess.run([str(checker)],capture_output=True).returncode==2
print('PASS walk-check: wall stops a 30 m/s run, glancing slide, autodrive arrives, unsupported start reported')
