"""RFC-0012 P1 (IPC 0.21): dev/nested/run.sh --quests.

Nested only (isolated harness on headless hosts). Lumen Reach, IPC 0.21, with the
focus clock scaled (HYPRUNE_FOCUS_TIME_SCALE) so whole intervals take seconds.
Checks:
1. Contract: 0.21 hello, quest.control, the markers/quests/focus state keys, homes as markers.
2. TAB tap vs hold: a tap reveals markers without opening the Director; a hold opens
   the Director and reveals nothing; a tap with Shift (sprint) held still reveals.
3. Reveal timing: hold 4 s, a 600 ms fade (frames keep coming), then gone; the
   markers.reveal method; reduced motion is instant.
4. Clutter caps on a crowded scene: <= 12 in view, <= 6 edge arrows, <= 5 labels.
5. The tracked step: an edge arrow off-screen, a small diamond in view, none with
   markers.persistent off.
6. Quest page round trip through the IPC: create, list, get, update, mark, track,
   abandon, resume, reach, delete, with quest.progress events and the private file.
7. Focus credit: credit on the target workspace, the grace period after leaving it,
   idle, a linked focus objective counted, history without titles.
Evidence: docs/evidence/quests.json and stills in the state dir.
"""
import json, math, os, shutil, stat, subprocess, time
import session as s
from m2 import keys, wait

LUMEN = 'com.hyprune.lumen-reach'
SCALE = 20


def prepare(installed):
    source = s.ROOT.parent / 'worlds/dist' / LUMEN
    if not (source / 'world.json').is_file():
        raise RuntimeError('Build Lumen Reach (worlds/dist) first')
    shutil.copytree(source, installed / LUMEN, dirs_exist_ok=True)
    # Quest files live under the state dir; guidance.json under a private config home.
    shutil.rmtree(s.STATE / 'statehome', ignore_errors=True)
    shutil.rmtree(s.STATE / 'confighome', ignore_errors=True)
    os.environ['XDG_CONFIG_HOME'] = str(s.STATE / 'confighome')
    os.environ['HYPRUNE_FOCUS_TIME_SCALE'] = str(SCALE)


def acceptance(client, children, args, shell_token, socketpath):
    results = []

    def check(name, ok, data=None):
        assert ok, (name, data)
        results.append({'name': name, 'evidence': data})
        print('PASS quests:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    def diag():
        return s.camera()['markers']

    def layout():
        return diag()['layout']

    def shot(name):
        path = s.STATE / name
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(path)], check=True)
        return str(path)

    def camera(x, y, z, yaw, pitch=0):
        s.ctl('eval', f'hl.plugin.hyprune.camera({x},{y},{z},{yaw},{pitch})')
        time.sleep(.3)

    def events(method):
        found = [e for e in client.events if e.get('method') == method]
        return found

    # 1. Contract.
    hello = client.hello
    check('IPC 0.21 with quest.control and the new methods and events',
          hello['protocol'] == '0.21' and 'quest.control' in hello['capabilities'] and
          {'markers.reveal', 'quests.create', 'focus.start', 'guidance.get'} <= set(hello['methods']) and
          {'quest.progress', 'focus.phase'} <= set(hello['events']), {'protocol': hello['protocol']})
    s.ctl('eval', 'hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    client.call('world.load', {'worldId': LUMEN})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: s.camera()['active'])
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    time.sleep(1)
    st = state()
    homes = sorted(m['id'] for m in st['markers']['list'] if m['kind'] == 'home')
    check('state carries markers, quests and focus; area homes are markers',
          st['quests']['enabled'] and st['focus']['state'] == 'idle' and st['markers']['reveal'] is None and
          homes == ['home:array-hall', 'home:rime-vault', 'home:signal-rise', 'home:sun-court'] and
          st['markers']['persistent'] == 'edge',
          {'homes': homes, 'list': len(st['markers']['list'])})
    sun = next(m for m in st['markers']['list'] if m['id'] == 'home:sun-court')
    check('a home marker sits on its primary wall top + 0.3 m',
          abs(sun['position'][1] - (10.1 + 4.5 + .3)) < .01 and sun['label'].startswith('Sun Court'), sun)

    # 2. TAB tap vs hold. A shell lease (as the shell holds) lets the hold open the Director.
    output = st['runtime']['outputs'][0]
    lease = client.call('shell.claim', {'outputs': [output]})['leaseId']
    # Stand in Sun Court looking at its wall (homes in view).
    camera(-8, 1.7, 12, 0, .15)
    client.events.clear()
    keys('-k', 'Tab')
    reveal = wait(lambda: state()['markers']['reveal'])
    time.sleep(.3)
    check('a TAB tap reveals markers and does not open the Director',
          reveal['reason'] == 'tap' and reveal['holdMs'] == 4000 and reveal['fadeMs'] == 600 and
          not events('ui.opened') and layout()['inView'] >= 1, {'reveal': reveal, 'layout': layout()['inView']})
    tap_shot = shot('quests-tap-reveal.png')
    # Timing: full during the hold, fading at hold end, gone after the fade.
    now = lambda: int(time.monotonic() * 1000)
    started = reveal['startedAtMs']
    held = [round(i['alpha'] / (.5 if i['dimmed'] else 1), 3) for i in layout()['items']]
    time.sleep(max(0, (started + 120 + 90 + 4000 + 300 - now()) / 1000))
    mid = layout()
    fading = [round(i['alpha'] / (.5 if i['dimmed'] else 1), 3) for i in mid['items']]
    f0 = s.camera()['frames']; time.sleep(.15); f1 = s.camera()['frames']
    time.sleep(max(0, (started + 120 + 90 + 4000 + 600 + 400 - now()) / 1000))
    after = layout()
    check('reveal holds, fades over 600 ms with frames forced, then ends',
          held and min(held) == 1 and fading and all(0 < a < 1 for a in fading) and f1 > f0 and
          not after['items'] and state()['markers']['reveal'] is None,
          {'hold': held[:4], 'fading': fading[:4], 'framesDuringFade': f1 - f0})
    # Hold: the Director opens, nothing is revealed.
    client.events.clear()
    keys('-P', 'Tab', '-s', '1400', '-p', 'Tab')
    opened = wait(lambda: state()['editor']['mode'] == 'menu' and events('ui.opened'), 5)
    check('a TAB hold opens the Director and reveals nothing',
          opened and state()['markers']['reveal'] is None, {'opened': opened[0]['params']})
    client.call('ui.close', {'leaseId': lease, 'outputId': output})
    wait(lambda: state()['editor']['mode'] == 'world', 5)
    time.sleep(.5)
    keys('-P', 'Control_L', '-k', 'Tab', '-p', 'Control_L')
    check('a tap with Ctrl (sprint) held still reveals', wait(lambda: state()['markers']['reveal'], 3)['reason'] == 'tap')
    client.call('markers.reveal', {'durationMs': 1500})
    r = state()['markers']['reveal']
    check('markers.reveal method restarts the reveal', r['reason'] == 'request' and r['holdMs'] == 1500, r)
    time.sleep(2.5)
    # Reduced motion: instant in and out.
    client.call('graphics.set', {'patch': {'reduced_motion': True}})
    time.sleep(.3)
    keys('-k', 'Tab')
    r = wait(lambda: state()['markers']['reveal'], 3)
    time.sleep(.2)
    instant = [round(i['alpha'] / (.5 if i['dimmed'] else 1), 3) for i in layout()['items']]
    time.sleep(max(0, (r['startedAtMs'] + 4000 + 150 - now()) / 1000))
    gone = layout()['items']
    check('reduced motion: no fade (all at full alpha, gone right after the hold)',
          r['fadeMs'] == 0 and instant and min(instant) == 1 and not gone, {'alphas': instant[:4]})
    client.call('graphics.set', {'patch': {'reduced_motion': False}})

    # 6. Quest page round trip through the IPC (the Quests page's calls).
    client.events.clear()
    ref = client.call('quests.create', {'quest': {'title': 'Release notes', 'summary': 'Write and publish them.',
                                                  'steps': [{'title': 'Draft'}, {'title': 'Review'}]}})['questRef']
    check('quests.create returns a personal questRef and tracks it',
          ref == {'source': 'personal', 'id': 'release-notes'} and state()['quests']['tracked'] == ref, ref)
    listed = client.call('quests.list', {})['quests']
    check('quests.list shows it', [q['questRef'] for q in listed] == [ref] and listed[0]['step']['title'] == 'Draft', listed)
    client.call('quests.update', {'questRef': ref, 'quest': {'title': 'Release notes 0.21', 'steps': [
        {'id': 'step-1', 'title': 'Draft'}, {'id': 'step-2', 'title': 'Review'}, {'title': 'Publish'}]}})
    detail = client.call('quests.get', {'questRef': ref})['quest']
    check('quests.update edits title and steps', detail['title'] == 'Release notes 0.21' and len(detail['steps']) == 3, detail['steps'])
    client.call('quests.mark', {'questRef': ref, 'stepId': 'step-1', 'objectiveId': 'done'})
    wait(lambda: any(e['params']['kind'] == 'step' for e in events('quest.progress')), 3)
    step_event = next(e['params'] for e in events('quest.progress') if e['params']['kind'] == 'step')
    check('marking a step sends quest.progress (step, next step)',
          step_event['stepTitle'] == 'Draft' and step_event['nextStepTitle'] == 'Review' and 'revision' in step_event, step_event)
    try:
        client.call('quests.mark', {'questRef': ref, 'stepId': 'step-3', 'objectiveId': 'done'})
        closed = False
    except RuntimeError as e:
        closed = e.args[0]['code'] == -32004
    check('a later step of a sequential quest is not open (-32004)', closed)
    client.call('quests.track', {'questRef': None})
    check('untrack', state()['quests']['tracked'] is None)
    client.call('quests.abandon', {'questRef': ref})
    client.call('quests.resume', {'questRef': ref})
    check('abandon and resume keep progress and re-track',
          state()['quests']['tracked'] == ref and state()['quests']['active'][0]['progress'] == [1, 3])
    for step in ['step-2', 'step-3']:
        client.call('quests.mark', {'questRef': ref, 'stepId': step, 'objectiveId': 'done'})
    wait(lambda: any(e['params']['kind'] == 'completed' for e in events('quest.progress')), 3)
    check('the last step completes the quest; it is untracked',
          client.call('quests.get', {'questRef': ref})['quest']['state'] == 'completed' and state()['quests']['tracked'] is None)
    # Reach: a quest at Array Hall completes when the eye enters the area.
    hall = client.call('quests.create', {'quest': {'title': 'Visit the hall', 'steps': [
        {'title': 'Go to Array Hall', 'target': {'areaId': 'array-hall'}, 'objectives': [{'id': 'go', 'type': 'reach'}]}]}})['questRef']
    camera(8, 1.7, -40, 0)
    wait(lambda: client.call('quests.get', {'questRef': hall})['quest']['state'] == 'completed', 5)
    check('a reach objective completes on entering its area', True)
    client.call('quests.delete', {'questRef': hall})
    check('quests.delete removes it', all(q['questRef'] != hall for q in client.call('quests.list', {})['quests']))
    personal = s.STATE / 'statehome/hyprune/quests/personal.json'
    wait(lambda: personal.exists(), 8)
    mode = stat.S_IMODE(personal.stat().st_mode)
    on_disk = json.loads(personal.read_text())
    check('personal quests persist privately (0600, debounced)',
          mode == 0o600 and on_disk['version'] == 1 and any(q['id'] == 'release-notes' for q in on_disk['quests']), oct(mode))

    # 4. Clutter caps: 20 quest targets in front (20-58 m), 10 behind, one tracked.
    camera(-8, 1.7, 12, 0, 0)
    eye = (-8, 1.7, 12)
    tracked = client.call('quests.create', {'quest': {'title': 'Tracked errand', 'steps': [
        {'title': 'Back at the gate', 'target': {'position': [eye[0] + 4, 2.0, eye[2] + 30], 'worldId': LUMEN}}]}})['questRef']
    client.call('quests.track', {'questRef': tracked})
    for i in range(20):
        x, y, z = eye[0] + (i % 5 - 2) * 6, 1.7 + (i // 5) * 2.5, eye[2] - 20 - i * 2
        client.call('quests.create', {'quest': {'title': f'Errand {i + 1}', 'steps': [
            {'title': f'Spot {i + 1}', 'target': {'position': [x, y, z], 'worldId': LUMEN}}]}})
    for i in range(9):
        client.call('quests.create', {'quest': {'title': f'Behind {i + 1}', 'steps': [
            {'title': f'Rear {i + 1}', 'target': {'position': [eye[0] + (i - 4) * 4, 1.7, eye[2] + 12 + i], 'worldId': LUMEN}}]}})
    time.sleep(.8)
    keys('-k', 'Tab')
    wait(lambda: state()['markers']['reveal'], 3)
    time.sleep(.6)
    crowd = layout()
    clutter_shot = shot('quests-clutter-caps.png')
    kinds = {}
    for i in crowd['items']:
        kinds[i['kind']] = kinds.get(i['kind'], 0) + 1
    check('clutter caps: <= 12 in view, <= 6 edge arrows, <= 5 labels',
          crowd['inView'] == 12 and 1 <= crowd['arrows'] <= 6 and crowd['labels'] <= 5 and
          sum(1 for i in crowd['items'] if i['label']) == crowd['labels'],
          {'inView': crowd['inView'], 'arrows': crowd['arrows'], 'labels': crowd['labels'], 'stacks': crowd['stacks'],
           'kinds': kinds, 'listed': len(state()['markers']['list'])})
    labelled = [i for i in crowd['items'] if i['label']]
    check('the tracked step is a labelled tier-1 edge arrow',
          any(i['tracked'] and i['arrow'] and i['label'] for i in crowd['items']), [i['id'] for i in labelled])
    time.sleep(5)

    # 5. Persistent tracked marker without a reveal.
    persistent = layout()['items']
    check('off-screen tracked step keeps an edge arrow (no reveal)',
          len(persistent) == 1 and persistent[0]['arrow'] and persistent[0]['tracked'], persistent)
    arrow_shot = shot('quests-tracked-arrow.png')
    camera(-8, 1.7, 12, math.pi, 0)
    time.sleep(.3)
    ahead = layout()['items']
    check('in view beyond 25 m it is a small unlabelled diamond',
          len(ahead) == 1 and ahead[0]['small'] and not ahead[0]['arrow'] and not ahead[0]['label'], ahead)
    client.call('guidance.set', {'patch': {'markers.persistent': 'off'}})
    time.sleep(.5)
    check('markers.persistent off hides it', not layout()['items'] and state()['markers']['persistent'] == 'off')
    client.call('guidance.set', {'patch': {'markers.persistent': None}})
    for q in client.call('quests.list', {})['quests']:
        if q['title'].startswith(('Errand', 'Behind')):
            client.call('quests.delete', {'questRef': q['questRef']})

    # 7. Focus credit (desktop): a window on workspace 2 is the target; workspace 3 is elsewhere.
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: not s.camera()['active'])
    for ws, title in [(2, 'Focus-target'), (3, 'Elsewhere')]:
        s.ctl('eval', 'hl.window_rule({name="q-' + title + '",match={title="^' + title + '$"},workspace="' + str(ws) + ' silent"})')
        children.append(s.envrun('kitty', '--title', title, 'bash', '--noprofile', '--norc',
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    wait(lambda: {'Focus-target', 'Elsewhere'} <= {c['title'] for c in json.loads(s.ctl('clients', '-j'))}, 20)

    def go(ws):
        s.ctl('dispatch', f'hl.dsp.focus({{ workspace = "{ws}" }})')
        time.sleep(.2)

    def typing(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            keys('-k', 'space')
            time.sleep(.25)

    def focus():
        return state()['focus']

    linked = client.call('quests.create', {'quest': {'title': 'Deep work', 'steps': [
        {'title': 'One focus session', 'objectives': [{'id': 'work', 'type': 'focus', 'sessions': 1, 'minutes': 1}]}]}})['questRef']
    go(2)
    client.events.clear()
    started = client.call('focus.start', {'target': {'kind': 'workspace', 'workspaceId': 2}, 'questRef': linked,
                                          'objectiveId': 'work',
                                          'overrides': {'work_min': 5, 'grace_s': 30, 'idle_s': 60, 'cycles': 1}})
    f = focus()
    check('focus.start: a work interval with the HUD timer fields',
          f['state'] == 'work' and f['sessionId'] == started['sessionId'] and f['phaseEndsAtMs'] - f['phaseStartedAtMs'] == 15000
          and f['target']['workspaceId'] == 2 and f['calm'] and wait(lambda: state()['markers']['calm'], 3), f)
    typing(3)
    a = focus()
    check('credit accrues with keyboard focus on the target and recent input', a['crediting'] and a['creditedMs'] >= 2000, a['creditedMs'])
    go(3)
    typing(3)
    b = focus()
    gained = b['creditedMs'] - a['creditedMs']
    check('leaving the target credits only the 30 s grace (1.5 s scaled)', not b['crediting'] and 1000 <= gained <= 2700, gained)
    go(2)
    typing(.6)
    c0 = focus()['creditedMs']
    time.sleep(4.5)
    c = focus()
    idle_gain = c['creditedMs'] - c0
    check('idle beyond focus.idle_s (3 s scaled) stops credit', not c['crediting'] and idle_gain <= 3600, idle_gain)
    typing(1.5)
    check('input resumes credit', focus()['crediting'])
    ended = wait(lambda: state() and next((e['params'] for e in events('focus.phase') if e['params']['phase'] == 'ended'), None), 15)
    check('the last interval ends the session (completed)', ended['outcome'] == 'completed' and focus()['state'] == 'idle', ended)
    q = client.call('quests.get', {'questRef': linked})['quest']
    check('the linked focus objective counts the completed interval',
          q['state'] == 'completed' and q['steps'][0]['objectives'][0]['done'] == 1, q['steps'][0]['objectives'][0])
    history = client.call('focus.history', {'days': 1})['intervals']
    raw = (s.STATE / 'statehome/hyprune/focus/history.jsonl').read_text()
    check('history: one work line, workspace and quest IDs, no titles',
          history and history[0]['phase'] == 'work' and history[0]['workspaceId'] == 2 and
          history[0]['questId'] == 'personal/deep-work' and 'Focus-target' not in raw and 'Deep work' not in raw, history[0])
    # Pause / skip / stop: no penalty, no failure.
    client.call('focus.start', {'overrides': {'work_min': 5, 'cycles': 2}})
    client.call('focus.pause', {})
    p = focus()
    client.call('focus.resume', {})
    client.call('focus.skip', {})
    k = focus()
    client.call('focus.stop', {})
    phases = [e['params']['phase'] for e in events('focus.phase')][-4:]
    check('pause, resume, skip and stop', p['state'] == 'paused' and p['pausedState'] == 'work' and k['state'] == 'break'
          and focus()['state'] == 'idle' and phases == ['paused', 'work', 'break', 'ended'], phases)

    evidence = {'nestedOnly': True, 'world': LUMEN, 'protocol': '0.21', 'focusTimeScale': SCALE,
                'stills': [os.path.basename(p) for p in (tap_shot, clutter_shot, arrow_shot)], 'checks': results}
    (s.ROOT / 'docs/evidence/quests.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('PASS quests:', len(results), 'checks', flush=True)
    client.call('runtime.exit', {})
