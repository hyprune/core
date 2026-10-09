"""Multiworld live-launcher acceptance; called only by the lifetime-locked harness."""
import csv,io,json,pathlib,shutil,subprocess,time
import session as s
from ctl import verified
from m2 import wait

# Deliberately start with the non-default world to catch a hard-coded bootstrap ID.
IDS=['com.hyprune.switchyard','com.hyprune.lumen-reach']

def fixtures(logs):
    packages=[]
    for index,ident in enumerate(IDS):
        # Add distinct declarative skins to isolated copies, never authored packages.
        target=logs/'world-fixtures'/('package '+str(index))
        if target.exists():shutil.rmtree(target)
        shutil.copytree(s.ROOT.parent/'worlds/dist'/ident,target)
        manifest=json.loads((target/'world.json').read_text())
        manifest.setdefault('extensions',{})['com.hyprune.hud-style']={'theme':'hud/test.json'}
        (target/'hud').mkdir(exist_ok=True)
        (target/'hud/test.json').write_text(json.dumps({'version':1,'colors':{'accent':['#ffcc88','#88ddff'][index]}}))
        (target/'world.json').write_text(json.dumps(manifest))
        packages.append(target)
    return packages


def acceptance(logs,recorded,children,check,expected_protocol):
    _,env=verified()
    children.append(s.envrun('wtype','-s','300000'));time.sleep(.2)
    def qpid():
        records=[]
        for line in (logs/'shell.log').read_text().splitlines():
            try:
                event=json.loads(line)
                if event.get('type')=='presentation.peer':records.append(event['pid'])
            except ValueError:pass
        return records[-1] if records else 0
    def ui(*args):
        return subprocess.check_output(['qs','ipc','--pid',str(qpid()),'call','preview',*map(str,args)],env=env,text=True,timeout=5).strip()
    def status():return json.loads(ui('status'))
    wait(lambda:status().get('lease'))
    ui('worldStyles','true')
    wait(lambda:status()['worldStyle']==IDS[0])
    check('first_selected_world_starts',status()['world']==IDS[0])
    check('all_worlds_recorded_in_order',[w['id'] for w in recorded['worlds']]==IDS)
    for item in recorded['worlds']:
        installed=pathlib.Path(item['installed'])
        check('installed_'+item['id'],json.loads((installed/'world.json').read_text())['id']==item['id'] and installed.joinpath('hud/test.json').is_file())
    for log in ['preflight.log','up.log','status.log']:
        check('selected_worlds_in_'+log,all(ident in (logs/log).read_text() for ident in IDS))
    device=s.envrun(str(s.ROOT/'build/nested-pointer'),'serve',stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True);children.append(device)
    def pointer(*args):
        device.stdin.write(' '.join(map(str,args))+'\n');device.stdin.flush();assert device.stdout.readline().strip()=='ok';time.sleep(.1)
    monitor=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    samples=[]
    for index in [1,0]:
        if not status()['director']:
            subprocess.run(['wtype','-P','Tab','-s','1100','-p','Tab'],env=env,check=True)
        wait(lambda:status()['director']);ui('tab',1);time.sleep(.7)
        # Click the real Places destination buttons. OCR locates visible labels,
        # avoiding a test-only world-load backdoor or hard-coded screen geometry.
        shot=logs/('destinations-'+str(index)+'.png')
        subprocess.run(['grim','-o','HEADLESS-1',str(shot)],env=env,check=True)
        tsv=subprocess.check_output(['tesseract',str(shot),'stdout','--psm','11','tsv'],text=True,stderr=subprocess.DEVNULL)
        rows=list(csv.DictReader(io.StringIO(tsv),delimiter='\t'))
        text=' '.join(row['text'] for row in rows)
        check('Places_lists_both_worlds_'+str(index),'Lumen' in text and 'Switchyard' in text)
        buttons=sorted([r for r in rows if r['text'].lower() in ('travel','destination')],key=lambda r:int(r['left']))
        assert buttons,'No visible destination action'
        # OCR can skip the selected light button; use its column title and the
        # common action row to locate either equally sized destination card.
        title=recorded['worlds'][index]['name'].split()[0]
        heading=next(row for row in rows if row['text']==title)
        box=buttons[0]
        x=monitor['x']+(int(heading['left'])+int(heading['width'])/2)/monitor['scale']
        y=monitor['y']+(int(box['top'])+int(box['height'])/2)/monitor['scale']
        old=list(map(float,s.ctl('cursorpos').split(',')));pointer(x-old[0],y-old[1]);pointer('button',272)
        wait(lambda:status()['world']==IDS[index] and not status()['warp'],timeout=35)
        wait(lambda:status()['worldStyle']==IDS[index])
        check('Places_world_load_and_HUD_style_'+str(index),s.camera()['active'] and status()['worldStyles'])
        samples.append({k:status()[k] for k in ['world','worldStyle','worldStyles','protocol']})
    (s.ROOT/'docs/evidence/live-worlds.json').write_text(json.dumps({'nestedOnly':True,'worlds':IDS,'styleFixtures':'temporary package copies','switches':samples},indent=2)+'\n')
