"""An AI assistant builds a world through `hyprune mcp` (RFC-0016 phase E1, IPC 0.33):
dev/nested/run.sh --assistant.

Nested only. The real shell at IPC 0.33, the SDK's `hyprune mcp` (HYPRUNE_SDK_DIR) driven over
stdio by this script as an MCP client would (a scripted assistant following the skill's plan:
"a cosy rooftop garden at sunset with three screens and a CPU tower that glows"), the source
helper, a local JSON API for the tower's data. Checks:
 1. Pairing: the MCP server asks when it starts; the HUD banner and the Director's prompt show
    the assistant's name; Always allow pairs it with the assistant profile (remembered).
 2. The garden is built from MCP tools only: a world folder, materials, a roof with parapets,
    a deck, planters, lamps, three workspace screens (homes of 21, 22, 23) and a CPU tower whose
    height and glow follow the API; Hyprune draws `look` (preview.render, a separate process),
    and the checklist is ready.
 3. A server killed mid-build and run again with the same plan (remembered: no prompt) finishes
    with no duplicate placements.
 4. Save is a local commit labelled "Assistant: "; undoing every later step leaves the folder
    equal to its last save.
 5. A paired connection is refused surface.focus, input.set, camera.set and data.publish
    (-32002, profile.assistant); Deny refuses a pairing (pair.denied).
 6. Revoke forgets the approval; Disconnect ends the assistant's in-world access.
 7. No QML runtime errors.
Stills and the clip (assistant-*.png, assistant-build.mp4) in the state dir; evidence
docs/evidence/assistant.json.
"""
import builtins, http.server, json, os, pathlib, queue, signal, socket, struct, subprocess, sys, threading, time
import session as s
from ctl import verified
from m2 import wait, keys
import builder_shell as b

NAME, SLUG = 'Rooftop Garden', 'rooftop-garden'
grants = b.grants


def prepare(installed):
    b.prepare(installed)
    os.environ['HYPRUNE_APPROVALS'] = str(s.STATE / 'approvals.json')
    (s.STATE / 'approvals.json').unlink(missing_ok=True)


class McpError(Exception):
    pass


class Mcp:
    """An MCP client of `hyprune mcp` over stdio (JSON lines)."""
    def __init__(self, env, name='Demo Assistant'):
        self.log = open(s.STATE / 'mcp.log', 'a')
        self.proc = subprocess.Popen([*b.CLI.split(), 'mcp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, env=env, text=True, bufsize=1)
        self.n, self.waiting, self.lock = 0, {}, threading.Lock()
        threading.Thread(target=self.read, daemon=True).start()
        self.init = self.rpc('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {}, 'clientInfo': {'name': name, 'version': '1.0'}})
        self.proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')

    def read(self):
        for line in self.proc.stdout:
            m = json.loads(line)
            with self.lock:
                q = self.waiting.pop(m.get('id'), None)
            if q:
                q.put(m)

    def rpc(self, method, params, timeout=180):
        with self.lock:
            self.n += 1
            ident, q = self.n, queue.Queue()
            self.waiting[ident] = q
        self.proc.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': ident, 'method': method, 'params': params}) + '\n')
        m = q.get(timeout=timeout)
        if 'error' in m:
            raise McpError(m['error'])
        return m['result']

    def tool(self, tool, /, **args):
        r = self.rpc('tools/call', {'name': tool, 'arguments': args})
        print(f'  mcp {tool} {json.dumps(args)[:120]}', 'ERROR ' + json.dumps(r.get('structuredContent'))[:300] if r.get('isError') else '', flush=True)
        if r.get('isError'):
            raise McpError(r['structuredContent']['error'])
        return r

    def data(self, tool, /, **args):
        return self.tool(tool, **args)['structuredContent']

    def kill(self):
        self.proc.kill()
        self.proc.wait()


class Raw:
    """A bare IPC connection that pairs (session.pair) before hello, for the refusal checks."""
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(str(path))
        self.sock.settimeout(150)
        self.n = 0

    def send(self, method, params):
        self.n += 1
        m = json.dumps({'jsonrpc': '2.0', 'id': str(self.n), 'method': method, 'params': params}).encode()
        self.sock.sendall(struct.pack('>I', len(m)) + m)
        return str(self.n)

    def receive(self, ident):
        def exact(n):
            out = b''
            while len(out) < n:
                d = self.sock.recv(n - len(out))
                if not d:
                    raise EOFError('closed')
                out += d
            return out
        while True:
            m = json.loads(exact(struct.unpack('>I', exact(4))[0]))
            if m.get('id') == ident:
                return m

    def call(self, method, params):
        return self.receive(self.send(method, params))


PAIR = {'protocols': ['0.33'], 'client': {'id': 'com.hyprune.mcp', 'name': 'Nested pairing test'}, 'profile': 'assistant'}
MATERIALS = [('roof-concrete', {'colour': '#8d8a86', 'roughness': .9}), ('parapet-stone', {'colour': '#b8b0a4'}), ('deck-wood', {'colour': '#8b5a3c', 'roughness': .7}),
             ('planter-clay', {'colour': '#a5532e'}), ('foliage', {'colour': '#3e7b38', 'roughness': .95}),
             ('lamp-warm', {'colour': '#ffb050', 'glow': {'colour': '#ffb050', 'strength': 3}})]


def blockout():
    """The plan's first passes: (tool, args) with keys, positions in metres, yaws as headings."""
    box = lambda key, size, pos, mat, **kw: ('place', dict(key=key, shape='box', size=size, position=pos, material=mat, ifExists='update', **kw))
    plan = [box('roof', [20, .3, 16], [0, 0, -8], 'roof-concrete'),
            box('parapet-back', [20, 1, .25], [0, .3, -15.875], 'parapet-stone'),
            # Walls may touch, never overlap (core refuses a collider inside another).
            box('parapet-left', [.25, 1, 15.75], [-9.875, .3, -7.875], 'parapet-stone'),
            box('parapet-right', [.25, 1, 15.75], [9.875, .3, -7.875], 'parapet-stone'),
            box('parapet-front-left', [7.625, 1, .25], [-5.9375, .3, -.125], 'parapet-stone'),
            box('parapet-front-right', [7.625, 1, .25], [5.9375, .3, -.125], 'parapet-stone'),
            box('deck', [6, .3, 6], [-6, .3, -12], 'deck-wood'),
            box('bench-deck', [1.8, .45, .5], [-6, .6, -10.2], 'deck-wood')]
    for i, (x, z) in enumerate([(5.5, -14.6), (-1.0, -1.2), (5.0, -1.2)]):
        plan.append(box(f'planter-{i + 1}', [2.6, .6, .8], [x, .3, z], 'planter-clay'))
        plan.append(('place', dict(key=f'shrub-{i + 1}', shape='sphere', size=[1.1], on={'key': f'planter-{i + 1}', 'face': 'top'}, material='foliage', ifExists='update')))
    for i, (x, z) in enumerate([(-9.2, -15.2), (9.2, -15.2), (-9.2, -.8), (9.2, -.8)]):
        plan.append(('place', dict(key=f'lamp-post-{i + 1}', shape='cylinder', size=[.18, 2.4, .18], position=[x, .3, z], material='parapet-stone', ifExists='update')))
        plan.append(('place', dict(key=f'lamp-{i + 1}', shape='sphere', size=[.42], on={'key': f'lamp-post-{i + 1}', 'face': 'top'}, material='lamp-warm', ifExists='update')))
    return plan


def screens(ref):
    return [('place', dict(key='screen-main', ref=ref, position=[0, .3, -15], yaw=0, workspace=21, ifExists='update')),
            ('place', dict(key='screen-west', ref=ref, position=[-8.6, .3, -4], yaw=-90, workspace=22, ifExists='update')),
            ('place', dict(key='screen-east', ref=ref, position=[8.6, .3, -4], yaw=90, workspace=23, ifExists='update'))]


HEDGES = [('place', dict(key=f'hedge-{i + 1}', shape='box', size=[1.6, .9, .6], position=[x, .3, -6.5], material='foliage', ifExists='update'))
          for i, x in enumerate([-2.6, -.8, 1.0, 2.8])]


def acceptance(client, children, args, shell_token, socketpath):
    children.append(b.helper(socketpath))
    out = s.STATE
    checks = []
    folder = pathlib.Path(os.environ['XDG_DATA_HOME']) / 'hyprune/projects' / SLUG

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        (out / 'assistant-checks.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
        assert ok, (name, data)
        print('PASS assistant:', name, json.dumps(data)[:300] if data is not None else '', flush=True)

    def shot(name):
        time.sleep(.8)
        subprocess.run([str(s.HERE / 'ctl.py'), 'shot', str(out / name)], check=True, timeout=20)

    def git(*a):
        return subprocess.check_output(['git', '-C', str(folder), *a], text=True).strip()

    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']

    def state():
        return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']

    # The tower's data: a JSON API whose cpu value the script moves.
    api = {'cpu': 20}

    class Api(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = json.dumps(api).encode()
            self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(body))); self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Api)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_address[1]}/cpu'

    read, write = os.pipe(); os.write(write, (b.SHELL_TOKEN + '\n').encode()); os.close(write)
    _, environment = verified()
    environment.update(HYPRUNE_TOKEN_FD=str(read), HYPRUNE_PROTOCOL='0.33', HYPRUNE_SOCKET=str(socketpath), HYPRUNE_CLI=b.CLI,
                       XDG_DATA_HOME=os.environ['XDG_DATA_HOME'], HYPRUNE_OUTPUT_SCREEN=b.OUTPUT, HYPRUNE_DEV_AUTOMATION='1',
                       HYPRUNE_SHELL_PREFERENCES=str(s.STATE / 'shell-preferences.json'))
    environment.pop('HYPRUNE_PROJECTS_DIR', None)
    shell_dir = pathlib.Path(os.environ.get('HYPRUNE_SHELL_DIR', str(s.ROOT.parent / 'shell')))
    shell_log = s.STATE / 'shell.log'
    shell = subprocess.Popen(['python3', str(shell_dir / 'bridge/launch.py'), '--protocol', '0.33'], env=environment,
                             pass_fds=(read,), stdout=open(shell_log, 'w'), stderr=subprocess.STDOUT)
    os.close(read); children.append(shell)

    def childpid():
        assert shell.poll() is None, 'shell exited; see shell.log'
        return builtins.next((int(q) for q in pathlib.Path(f'/proc/{shell.pid}/task/{shell.pid}/children').read_text().split()
                              if pathlib.Path(f'/proc/{q}/comm').read_text().strip() == 'qs'), None)
    qpid = wait(childpid, 30)

    def ui(*a):
        return subprocess.check_output([str(s.HERE / 'ctl.py'), 'env', 'qs', 'ipc', '--pid', str(qpid), 'call', 'preview', *map(str, a)], text=True).strip()

    def status():
        try:
            return json.loads(ui('status'))
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            return {}

    def assistants():
        return status().get('assistants') or {}

    def press(name):
        return ui('press', name) == 'true'

    def director():
        wait(lambda: s.camera()['active'], 30)
        # Back to world mode first (a reload or the cinema camera can leave the HUD elsewhere).
        wait(lambda: not status().get('warp'), 30)
        if status().get('mode') not in (None, 'world'):
            keys('-P', 'Super_L', '-k', 'Tab', '-p', 'Super_L')
            time.sleep(1)
        # The hold sometimes misses on a loaded host: try a few times, then keep evidence.
        for _ in range(7):
            if status().get('director'):
                break
            keys('-P', 'Tab', '-s', '1150', '-p', 'Tab')
            try:
                wait(lambda: status().get('director'), 6)
            except AssertionError:
                time.sleep(2)
        if not status().get('director'):
            shot('assistant-director-failed.png')
        assert status().get('director'), 'the Director did not open'
        s.ctl('eval', 'hl.dispatch(hl.dsp.cursor.move({x=4,y=4}))')

    def close_director():
        if status().get('director'):
            keys('-k', 'Escape'); wait(lambda: not status().get('director'), 10)

    def instances():
        return [i for i in state()['objects']['instances'] if i.get('source') == 'user']

    s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{ monitor = "{b.OUTPUT}" }}))')
    client.call('world.load', {'worldId': b.LUMEN})
    s.ctl('eval', f'hl.plugin.hyprune.toggle("{b.OUTPUT}")'); wait(lambda: s.camera()['active'], 60)
    wait(lambda: status().get('protocol') == '0.33' and status().get('lease'), 30)

    mcp_env = {**os.environ, 'HYPRUNE_SOCKET': str(socketpath), 'XDG_DATA_HOME': os.environ['XDG_DATA_HOME'],
               'HYPRUNE_SOURCES_CONFIG': str(s.STATE / 'sources.json')}
    mcp_env.pop('HYPRUNE_PROJECTS_DIR', None)

    # 1. Pairing: asked at start; Always allow.
    mcp = Mcp(mcp_env)
    wait(lambda: assistants().get('prompt'), 30)
    shot('assistant-banner.png')
    director()
    ui('tabNamed', 'builder')
    time.sleep(.6)
    shot('assistant-prompt.png')
    assert press('assistant-always')
    wait(lambda: len(assistants().get('live') or []) == 1, 30)
    live = assistants()['live'][0]
    approvals = json.loads((s.STATE / 'approvals.json').read_text())
    check('pairing: the HUD banner and the Director\'s prompt name the assistant; Always allow pairs it (assistant profile, remembered, no token stored)',
          live['agent']['name'] == 'Demo Assistant' and live['remembered'] and approvals['approvals'][0]['agent'] == 'Demo Assistant' and
          'token' not in json.dumps(approvals),
          {'live': live, 'approvals': approvals, 'server': mcp.init.get('serverInfo')})
    close_director()

    # 2. The rooftop garden, from MCP tools only, recorded.
    import cinema
    cam = cinema.Cinema(s)
    raw = out / 'assistant-build.mkv'
    started = time.monotonic()
    created = mcp.data('world_create', name=NAME, sky='sunset', floor='grass', spawn={'position': [0, 0, 6], 'yaw': 0})
    wait(lambda: (state().get('builder') or {}).get('projectId') == SLUG, 60)
    wait(lambda: not status().get('warp') and status().get('phase') == 'arrived', 30)
    cam.hold([14, 9, 10], target=[0, 0, -8])
    time.sleep(1)
    with cam.recording(raw):
        cam.path([{'t': 0, 'eye': [14, 9, 10], 'target': [0, 0, -8]}, {'t': 40, 'eye': [-6, 11, 12], 'target': [0, 0, -8]},
                  {'t': 80, 'eye': [-15, 8, -2], 'target': [0, 0, -8]}])
        for key, patch in MATERIALS:
            mcp.data('material_set', key=key, **patch)
        for tool, args_ in blockout():
            mcp.data(tool, **args_)
        found = mcp.data('catalog_search', query='screen')
        ref = builtins.next(o['ref'] for o in found['objects'] if o['ref'].endswith(b.SCREEN))
        for tool, args_ in screens(ref):
            mcp.data(tool, **args_)
        mcp.data('place', key='cpu-tower', shape='box', size=[.8, 3, .8], position=[4, .3, -9], material='lamp-warm', ifExists='update')
        mcp.data('source_set_http', **{'for': 'cpu-tower', 'url': url, 'map': {'value': {'path': '$.cpu', 'min': 0, 'max': 100}, 'display': '{raw:.0f} %'}, 'intervalS': 1})
        mcp.data('behaviour_set', placement='cpu-tower', behaviour={'key': 'load', 'property': 'scale', 'from': [1, .1, 1], 'to': [1, 1, 1], 'anchor': 'bottom', 'smoothing': .4})
        mcp.data('behaviour_set', placement='cpu-tower', behaviour={'key': 'heat', 'property': 'glow', 'from': .5, 'to': 6, 'colour': '#ffb050'})
        for value in [20, 55, 90, 60, 35]:
            api['cpu'] = value
            time.sleep(1.6)
    cam.stop()  # the cinema camera hands the view back (the Director needs world mode)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(raw), *cinema.ENCODE, str(out / 'assistant-build.mp4')], check=True)
    build_s = round(time.monotonic() - started, 1)
    api['cpu'] = 80
    wait(lambda: any(i.get('key') == 'cpu-tower' and i.get('behaviours') and i['behaviours'][0]['condition'] == 'fresh' and i['behaviours'][0]['value'] == 80 for i in instances()), 30)
    placed = instances()
    keys_ = [i.get('key') for i in placed]
    homes = sorted((i.get('screen') or {}).get('workspace') or 0 for i in placed if i.get('screen'))
    tower = builtins.next(i for i in placed if i.get('key') == 'cpu-tower')
    check('the rooftop garden built from MCP tools: unique keys, three screens home workspaces 21 to 23, the tower follows the API',
          len(keys_) == len(set(keys_)) and len(placed) == 8 + 6 + 8 + 3 + 1 and homes == [21, 22, 23] and
          [b_['value'] for b_ in tower['behaviours']] == [80, 80],
          {'placements': len(placed), 'homes': homes, 'tower': tower['behaviours'], 'seconds': build_s, 'world': created.get('projectId')})
    looks = {}
    for name, frm in [('overview', 'overview'), ('spawn', 'spawn'), ('screen-main', 'screen-main'), ('tower', 'cpu-tower')]:
        r = mcp.tool('look', **{'from': frm, 'size': [1024, 576]})
        image = builtins.next(c for c in r['content'] if c['type'] == 'image')
        import base64
        (out / f'assistant-look-{name}.png').write_bytes(base64.b64decode(image['data']))
        looks[name] = r['structuredContent']
        time.sleep(1.1)  # one preview a second
    checklist = mcp.data('checklist', fresh=True)
    open_items = [i for i in checklist['items'] if i['blocks'] and not i['done']]
    check('look is drawn by Hyprune (preview.render in its own process) and the checklist is ready',
          all(l['renderer'] == 'hyprune' for l in looks.values()) and checklist['ready'],
          {'looks': {k: {'pose': v['pose'], 'ms': v.get('ms')} for k, v in looks.items()}, 'open': open_items,
           'walk': checklist['walk']})
    s.ctl('eval', 'hl.plugin.hyprune.camera(0,1.7,5.5,0,-.05)')
    shot('assistant-world-spawn.png')
    s.ctl('eval', 'hl.plugin.hyprune.camera(13,8,9,-.9,-.42)')
    shot('assistant-world-overview.png')

    # 3. Killed mid-build, run again with the same plan: no duplicates, no prompt (remembered).
    for tool, args_ in HEDGES[:2]:
        mcp.data(tool, **args_)
    mcp.kill()
    mcp = Mcp(mcp_env)
    prompted = False
    for _ in range(20):
        prompted = prompted or bool(assistants().get('prompt'))
        if len(assistants().get('live') or []) == 1:
            break
        time.sleep(.3)
    mcp.data('world_open', project=SLUG)
    for tool, args_ in HEDGES:
        mcp.data(tool, **args_)
    after = instances()
    hedge_keys = sorted(i['key'] for i in after if (i.get('key') or '').startswith('hedge-'))
    check('a server killed mid-build and run again with the same plan finishes with no duplicates (remembered: no prompt)',
          not prompted and hedge_keys == ['hedge-1', 'hedge-2', 'hedge-3', 'hedge-4'] and len(after) == len(placed) + 4,
          {'hedges': hedge_keys, 'placements': len(after), 'prompted': prompted})

    # 4. Save, then undo every later step: the folder equals its last save.
    saved = mcp.data('save', message='Rooftop garden: blockout, screens and the CPU tower')
    mcp.data('edit', key='bench-deck', position=[-5, .6, -10.5], yaw=20)
    mcp.data('material_set', key='foliage', colour='#2f6a2c')
    mcp.data('behaviour_remove', placement='cpu-tower', behaviour='heat')
    mcp.data('place', key='fountain', shape='cylinder', size=[1.4, .5, 1.4], position=[0, .3, -8], material='parapet-stone', ifExists='update')
    mcp.data('remove', key='hedge-4')
    dirty = git('status', '--porcelain')
    undone = mcp.data('undo', steps=5)
    wait(lambda: git('status', '--porcelain') == '', 20)
    check('save is a local commit labelled Assistant:, and undoing every later step leaves the folder equal to its last save',
          git('log', '-1', '--format=%s') == 'Assistant: ' + 'Rooftop garden: blockout, screens and the CPU tower' and dirty != '' and
          len(undone['undone']) == 5 and git('status', '--porcelain') == '',
          {'commit': saved['commit'][:12], 'undone': undone['undone'], 'dirty': dirty.split('\n')[:8]})

    # 5. A paired connection is refused outside the profile; Deny refuses a pairing.
    director()
    ui('tabNamed', 'builder')
    raw = Raw(socketpath)
    ident = raw.send('session.pair', {**PAIR, 'agent': {'name': 'Probe'}})
    wait(lambda: assistants().get('prompt'), 20)
    assert press('assistant-once')
    token = raw.receive(ident)['result']['token']
    hello = raw.call('session.hello', {'protocols': ['0.33'], 'client': {'id': 'com.hyprune.mcp', 'name': 'Probe'}, 'capabilities': ['surface.control', 'input.control'], 'token': token})['result']
    refusals = {m: raw.call(m, p).get('error') for m, p in [('surface.focus', {'surfaceId': 'x'}), ('input.set', {'patch': {}}), ('camera.set', {'position': [0, 1.7, 0]}), ('data.publish', {'source': 'x', 'values': {}})]}
    denied = Raw(socketpath)
    ident = denied.send('session.pair', {**PAIR, 'agent': {'name': 'Unwanted'}})
    wait(lambda: assistants().get('prompt'), 20)
    shot('assistant-prompt-second.png')
    assert press('assistant-deny')
    deny = denied.receive(ident)
    check('a paired connection is refused surface.focus, input.set, camera.set and data.publish (-32002); Deny refuses the pairing',
          hello.get('profile') == 'assistant' and all(e and e['code'] == -32002 and e['data']['rule'] == 'profile.assistant' for e in refusals.values()) and
          deny.get('error', {}).get('data', {}).get('rule') == 'pair.denied',
          {'refusals': refusals, 'deny': deny.get('error'), 'methods': len(hello.get('methods', []))})
    raw.sock.close()

    # 6. Revoke and Disconnect from Connected assistants.
    wait(lambda: len(assistants().get('live') or []) == 1, 10)
    ui('scrollPage', 1)
    time.sleep(.6)
    shot('assistant-connected.png')
    assert press('assistant-revoke')
    wait(lambda: assistants().get('approvals') == 0, 10)
    assert press('assistant-disconnect')
    wait(lambda: not assistants().get('live'), 10)
    try:
        mcp.data('place', key='after-disconnect', shape='cube', size=[1], position=[0, .3, -4])
        refused = None
    except McpError as e:
        refused = e.args[0]
    check('Revoke forgets the approval; Disconnect ends the assistant\'s in-world access (no silent re-pairing)',
          json.loads((s.STATE / 'approvals.json').read_text())['approvals'] == [] and refused and refused['code'] == 'assistant.offline' and
          not any(i.get('key') == 'after-disconnect' for i in instances()),
          {'refused': refused})
    mcp.kill()
    close_director()
    server.shutdown()

    log = shell_log.read_text()
    bad = [x for x in ['ReferenceError', 'TypeError', 'Binding loop', 'Unable to assign', 'is not defined', 'does not match the negotiated protocol'] if x in log]
    check('no QML runtime errors in the shell log', not bad, bad)
    evidence = s.ROOT / 'docs/evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'assistant.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
    print(f'PASS assistant: {len(checks)} checks', flush=True)
