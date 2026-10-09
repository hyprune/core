"""Hostile client strings on the wire: dev/nested/run.sh --long-titles (lifetime flock).

Real clients with ~10k code point multibyte titles/classes, a native bind whose
description exceeds 1024, a placement rule from a long title. Every outgoing
frame is schema-checked by the core audit (HYPRUNE_VALIDATE_IPC=1) for each
connected protocol, and again client-side.
"""
import json,re,shutil,subprocess,time
import session as s
from m2 import wait

LUMEN='com.hyprune.lumen-reach'
PROTOCOLS=['0.1','0.3','0.5','0.8','0.11','0.14']
UNIT='kitty é漢\U0001F600 '
TITLE='LONG-'+UNIT*1100  # ~10k code points, 1-4 byte UTF-8

def grants():
    return [{'token':__import__('secrets').token_hex(32),'client':'org.hyprune.nested.v'+v.replace('.','-'),'capabilities':['state.read']} for v in PROTOCOLS]

def prepare(installed):
    shutil.copytree(s.ROOT.parent/'worlds/dist'/LUMEN,installed/LUMEN,dirs_exist_ok=True)

def acceptance(client,children,args,shell_token,socketpath):
    checks=[]
    def check(name,ok,data=None):
        assert ok,(name,data)
        checks.append(name);print('PASS long titles:',name,flush=True)
    def windows():
        out=subprocess.run([str(s.HERE/'ctl.py'),'hyprctl','clients','-j'],capture_output=True,check=True).stdout
        return json.loads(out.decode('utf-8','replace'))
    olds=[]
    for grant,version in zip(args.extra_grants,PROTOCOLS):
        old=s.Client(str(socketpath),grant['token'],version,grant['client'])
        old.sub=old.call('state.subscribe',{'topics':['state']})['subscriptionId'];olds.append(old)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load',{'worldId':LUMEN})
    sub=client.call('state.subscribe',{'topics':['state']})['subscriptionId']
    def state():return client.call('state.snapshot',{'subscriptionId':sub})['snapshot']['state']
    s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');wait(lambda:s.camera()['active'])
    # A native bind on a Hyprune key, with an exec far past the 1024 description limit.
    s.ctl('eval','hl.bind("w", hl.dsp.exec_cmd("true"), {description='+json.dumps('\u00e9'*3000,ensure_ascii=False)+'})')
    # X11 (XWayland) clients carry ~10k code point titles and 300+ byte classes,
    # one with malformed UTF-8; a Wayland client is capped near 4 KiB per request,
    # so its title is a realistic ~1500 code points.
    fixture=str(s.ROOT/'build/nested-x11-title')
    for cls,extra in [('long-room',''),('long-home',''),('long-invalid',' invalid')]:
        s.ctl('eval','hl.dispatch(hl.dsp.exec_cmd('+json.dumps(fixture+' '+cls+' 1000'+extra)+'))')
    children.append(s.envrun('kitty','--class','long-wayland','--title','LONG-'+UNIT*150,'bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
    for cls,limit in [('long-room',4096),('long-home',4096),('long-invalid',4096),('long-wayland',1000)]:
        w=wait(lambda:next((w for w in windows() if w['class'].startswith(cls) and w['title'].startswith('LONG-')),None),20)
        check(f'{cls} title arrives longer than {limit} code points',len(w['title'])>limit,len(w['title']))
    def long(items):return [x for x in items if x['title'].startswith('LONG-')]
    surfaces=wait(lambda:long(state()['surfaces']) if len(long(state()['surfaces']))>=4 else None,20)
    check('surface titles clamped to 512 code points, prefix kept',all(len(x['title'])==512 and x['title'].endswith('\u2026') and TITLE.startswith(x['title'][:-1]) for x in surfaces),[len(x['title']) for x in surfaces])
    apps=long(state()['apps'])
    check('apps titles and classes clamped',len(apps)>=4 and all(len(a['title'])==512 and len(a['appClass'])<=256 and len(a['icon'])<=256 for a in apps),[(len(a['title']),len(a['appClass'])) for a in apps])
    bad=next(a for a in apps if a['appClass'].startswith('long-invalid'))
    check('malformed client bytes reach the wire only as valid UTF-8',bad['appClass'].isprintable(),repr(bad['appClass'][-4:]))
    homeWindow=next(w for w in windows() if w['class'].startswith('long-home'))
    s.ctl('eval',f'hl.dispatch(hl.dsp.window.move({{workspace="11",follow=false,window="address:{homeWindow["address"]}"}}))')
    home=wait(lambda:next((c for c in s.camera()['captures'] if c['workspaceId']==11 and c['title'].startswith('LONG-')),None),20)
    client.call('surface.pull',{'surfaceId':home['id']});client.call('surface.place',{'surfaceId':home['id']})
    rules=[p['rule'] for p in state()['mounts']['placements']]
    check('placement rule clamped to 256 code points',any(len(r['title'])==256 for r in rules),[len(r['title']) for r in rules])
    listed=client.call('placements.list',{})['placements']
    check('placements.list rule clamped',all(len(p['rule']['title'])<=256 for p in listed) and listed,len(listed))
    steals=[x for x in state()['keymap']['steals'] if x['binding']=='w']
    check('native bind description clamped to 1024',steals and all(len(x['description'])==1024 for x in steals),[len(x['description']) for x in steals])
    # Older negotiated protocols receive the same projections.
    for old in olds:
        snap=old.call('state.snapshot',{'subscriptionId':old.sub})['snapshot']['state']
        titles=[x['title'] for x in snap.get('surfaces',[]) if x['title'].startswith('LONG-')]
        check(f'IPC {old.hello["protocol"]} snapshot titles clamped',titles and all(len(t)==512 for t in titles),[len(t) for t in titles])
    s.ctl('eval','hl.unbind("w")')
    time.sleep(.5)
    # Client-side validation of every received frame, per negotiated version.
    validator=s.ROOT/'dev/validate-wire.py'
    for c in [client]+olds:
        version=c.hello['protocol']
        lines=[json.dumps({'protocol':version,'method':t['method'],'frame':t['response']}) for t in c.transcript]
        lines+=[json.dumps({'protocol':version,'method':'','frame':e}) for e in c.events]
        r=subprocess.run(['python3',str(validator)],input='\n'.join(lines)+'\n',capture_output=True,text=True)
        check(f'IPC {version}: {len(lines)} client frames valid',r.returncode==0,r.stderr[-400:])
    for old in olds:old.close()
    client.call('runtime.exit',{})
    (s.STATE/'long-titles-pending.json').write_text(json.dumps(checks))

def finish():
    pending=s.STATE/'long-titles-pending.json'
    if not pending.exists():return
    checks=json.loads(pending.read_text());pending.unlink()
    log=(s.STATE/'hyprland.log').read_text()
    audit=re.findall(r'IPC schema audit: (\d+) outgoing frames validated',log)
    assert audit and int(audit[-1])>0 and 'IPC SCHEMA ASSERTION' not in log,'core wire audit missing or failed'
    checks.append(f'core wire audit validated {audit[-1]} outgoing frames');print('PASS long titles: core wire audit',audit[-1],'frames',flush=True)
    (s.ROOT/'docs/evidence/long-titles.json').write_text(json.dumps({'nestedOnly':True,'lock':'/tmp/worldshell-nested.lock','protocols':['0.15']+PROTOCOLS,'checks':checks},indent=2)+'\n')
