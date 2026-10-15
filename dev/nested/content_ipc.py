#!/usr/bin/env python3
"""RFC-0015 content over IPC 0.32 end to end (nested only): dev/nested/run.sh dev/nested/content_ipc.py

hypruned runs the SDK CLI ($HYPRUNE_CLI, default `node ../sdk/dist/src/cli.js`, built) as its job
engine. Network: refreshes the official and community indexes (hyprune.github.io) and installs
the official optional companions library from its GitHub release, then removes it. The dev
build's IPC schema audit checks every frame against the vendored 0.32 schema. XDG config, data,
state and cache are private to the instance. Results: <state>/content-ipc.json.
"""
import json, os, pathlib, shutil, sys, time
from types import SimpleNamespace
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import session as s, gauges
from m2 import wait

LIBRARY = 'com.hyprune.companions'


def prepare(installed):
    for name in ('datahome', 'confighome'):
        shutil.rmtree(s.STATE / name, ignore_errors=True)
        (s.STATE / name).mkdir(parents=True)
    os.environ['XDG_DATA_HOME'] = str(s.STATE / 'datahome')
    os.environ['XDG_CONFIG_HOME'] = str(s.STATE / 'confighome')
    if 'HYPRUNE_CLI' not in os.environ:
        cli = s.ROOT.parent / 'sdk/dist/src/cli.js'
        if not cli.is_file():
            raise SystemExit(f'content_ipc needs the built SDK CLI at {cli} (or HYPRUNE_CLI)')
        os.environ['HYPRUNE_CLI'] = f'node {cli}'


def acceptance(client, children, args, shell_token, socketpath):
    checks = []

    def check(name, ok, data=None):
        checks.append({'name': name, 'ok': bool(ok), 'evidence': data})
        assert ok, (name, data)
        print('PASS content:', name, json.dumps(data)[:240] if data is not None else '', flush=True)
    hello = client.hello
    check('0.32 advertises the content methods and grants content.control',
          hello['protocol'] == '0.32' and 'content.install' in hello['methods'] and 'registry.list' in hello['methods'] and
          'content.control' in hello['capabilities'], {k: hello[k] for k in ('protocol', 'capabilities')})
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    state = lambda: client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    content = lambda: state().get('content')
    wait(lambda: content() and len(content()['sources']) >= 2, 60)
    check('state carries content: the built-in sources', [x['id'] for x in content()['sources']][:2] == ['official', 'community'],
          content()['sources'])

    def job(jid):
        return next((j for j in content()['jobs'] if j['jobId'] == jid), None)

    def finished(jid, timeout=240):
        return wait(lambda: (lambda j: j if j and j['stage'] in ('done', 'failed', 'cancelled', 'confirm') else None)(job(jid)), timeout)
    refresh = client.call('sources.refresh', {})['jobId']
    j = finished(refresh)
    check('sources.refresh: a job that fetches the indexes', j['stage'] == 'done' and j['kind'] == 'source.refresh', j)
    found = wait(lambda: client.call('registry.list', {'query': 'companions'})['entries'], 60)
    entry = next(e for e in found if e['id'] == LIBRARY)
    check('registry.list finds the official companions library', entry['sourceId'] == 'official' and entry['type'] == 'library',
          {k: entry[k] for k in ('sourceId', 'id', 'name', 'tier')})
    page = client.call('registry.list', {'limit': 1})
    check('registry.list pages', len(page['entries']) == 1 and page['cursor'] == '1', page['cursor'])
    detail = client.call('registry.entry', {'sourceId': 'official', 'id': LIBRARY})
    check('registry.entry: not installed yet', detail['installed'] is None, detail['entry']['latest'])
    install = client.call('content.install', {'source': {'entry': {'sourceId': 'official', 'id': LIBRARY}}})['jobId']
    j = finished(install)
    check('content.install stops at the plan', j['stage'] == 'confirm' and j['plan']['packages'][0]['id'] == LIBRARY and
          j['plan']['packages'][0]['licenceNote'] == 'reviewed', j['plan'])
    client.call('content.confirm', {'jobId': install, 'accept': True})
    j = finished(install, 600)
    check('confirmed: downloaded, verified and committed', j['stage'] == 'done', {k: j[k] for k in ('stage', 'progress', 'error', 'result')})
    wait(lambda: any(i['id'] == LIBRARY for i in content()['installs']), 60)
    record = next(i for i in content()['installs'] if i['id'] == LIBRARY)
    check('installs lists it from the official source', record['sourceId'] == 'official' and record['managed'] == 'user', record)
    notices = [e['params'] for e in client.events if e.get('method') == 'runtime.notice']
    check('content.installed notice', any(n['code'] == 'content.installed' for n in notices), notices[-3:])
    catalog = wait(lambda: [l for l in client.call('libraries.list', {})['libraries'] if l['id'] == LIBRARY], 30)
    check('core sees the library (directory watch)', bool(catalog), catalog)
    remove = client.call('content.remove', {'id': LIBRARY})['jobId']
    j = finished(remove)
    check('content.remove', j['stage'] == 'done', j)
    wait(lambda: not any(i['id'] == LIBRARY for i in content()['installs']), 60)
    old = s.Client(str(socketpath), None, '0.31', client_id='org.hyprune.nested-old')
    check('0.31 peers: no content methods or key', 'content.install' not in old.hello['methods'], old.hello['protocol'])
    (s.STATE / 'content-ipc.json').write_text(json.dumps({'nestedOnly': True, 'protocol': '0.32', 'checks': checks}, indent=1) + '\n')
    print('PASS content: RFC-0015 over IPC 0.32,', len(checks), 'checks', flush=True)
    client.call('runtime.exit', {})


gauges.prepare = prepare
gauges.acceptance = acceptance
s.run(SimpleNamespace(serve=False, record=False, m1=False, m2=False, m3a=False, m3b=False, live=False, integration=False, visible=False,
                      authoring=False, climbing=False, gauges=True, content=True, protocol='0.32'))
