#!/usr/bin/env python3
"""A stand-in for the SDK's `hyprune` CLI (registry commands, JSON contract) for tests/content.cpp.
FAKE_CLI_LOG appends each argv as a JSON line; FAKE_PLAN_CHANGE=1 makes a --yes run plan another hash."""
import json, os, sys

args = sys.argv[1:]
if os.environ.get('FAKE_CLI_LOG'):
    with open(os.environ['FAKE_CLI_LOG'], 'a') as log:
        log.write(json.dumps(args) + '\n')
        if os.environ.get('HYPRUNE_SOCKET'):
            log.write('SESSION SOCKET LEAKED\n')
SHA = '5d2f0c6e3b9a41d8e7c2b0f4a6d9e1c3b5a7f9e2d4c6b8a0f1e3d5c7b9a2e4f6'


def out(obj):
    print(json.dumps(obj), flush=True)


def plan(sha=SHA, source_change=None):
    return {'packages': [{'id': 'io.github.ada.tidepool', 'type': 'world', 'tag': 'v1.2.0', 'version': '1.2.0', 'formatVersion': '0.9',
                          'replaces': None, 'sourceId': 'community', 'sourceLabel': 'Community registry', 'creator': 'ada',
                          'repo': 'https://github.com/ada/tidepool-world', 'license': 'CC-BY-SA-4.0', 'licenceNote': 'declared',
                          'bytes': 100, 'expandedBytes': 200, 'sha256': sha, 'url': 'https://example.org/t.tar.zst', 'action': 'install',
                          'requires': [], 'extraCliField': 1}],
            'sourceChange': source_change, 'override': False, 'diskFreeAfter': 1000}


if args[:1] == ['installed']:
    out({'installs': [{'id': 'com.hyprune.street', 'type': 'library', 'sourceId': 'official', 'display': 'Official', 'sourceState': 'enabled',
                       'tag': 'v1.1.0', 'version': '1.1.0', 'previous': None, 'pinned': False, 'pending': None, 'managed': 'user',
                       'removedFromSource': False, 'newerTag': 'v1.2.0', 'license': 'CC-BY-4.0', 'sha256': SHA, 'path': '/x/street'}]})
elif args[:2] == ['source', 'list']:
    out({'sources': [{'id': 'official', 'kind': 'official', 'label': 'Official', 'display': 'Official',
                      'url': 'https://hyprune.github.io/registry/v0.1/official.json', 'enabled': True, 'builtin': True, 'system': False,
                      'lastRefresh': None, 'serial': None, 'entries': 0, 'error': None},
                     {'id': 'community', 'kind': 'community', 'label': 'Community registry', 'display': 'Community registry',
                      'url': 'https://hyprune.github.io/registry/v0.1/community.json', 'enabled': True, 'builtin': True, 'system': False,
                      'lastRefresh': '2026-10-16T07:00:00Z', 'serial': 3, 'entries': 2, 'error': None}]})
elif args[:1] == ['browse']:
    version = {'tag': 'v1.2.0', 'version': '1.2.0', 'formatVersion': '0.9', 'license': 'CC-BY-SA-4.0', 'bytes': 100, 'expandedBytes': 200,
               'published': '2026-10-01', 'status': 'available', 'prerelease': False, 'loadable': True, 'requires': []}
    entry = lambda i, name, tags: {'sourceId': 'community', 'display': 'Community registry', 'tier': 'community', 'id': i, 'type': 'world',
                                   'name': name, 'summary': 'A ' + name.lower(), 'description': None, 'creator': 'ada', 'repo': None,
                                   'tags': tags, 'latest': version, 'licenceNote': 'declared', 'thumbnail': None, 'installed': None,
                                   'update': None, 'versions': [version]}
    out({'entries': [entry('io.github.ada.tidepool', 'Tidepool', ['water']), entry('io.github.ada.dunes', 'Dunes', ['sand'])]})
elif len(args) > 2 and args[1] == 'add':
    yes, plan_only = '--yes' in args, '--plan' in args
    out({'event': 'stage', 'stage': 'resolving'})
    change = {'id': 'io.github.ada.tidepool', 'from': {'sourceId': 'official', 'display': 'Official'},
              'to': {'sourceId': 'community', 'display': 'Community registry'}} if any('io.github.ada.moved' in a for a in args) else None
    out({'event': 'plan', 'plan': plan('0' * 64 if yes and os.environ.get('FAKE_PLAN_CHANGE') else SHA, change)})
    if plan_only:
        out({'event': 'done', 'result': {'action': 'plan'}})
    else:
        for stage in ('downloading', 'verifying', 'extracting', 'validating', 'committing'):
            out({'event': 'stage', 'stage': stage})
            if stage == 'downloading':
                out({'event': 'progress', 'bytes': 100, 'total': 100})
        out({'event': 'done', 'result': {'id': 'io.github.ada.tidepool', 'type': 'world', 'tag': 'v1.2.0', 'action': 'install'}})
elif args[:1] == ['update']:
    out({'event': 'stage', 'stage': 'resolving'})
    out({'event': 'done', 'result': {'action': 'update', 'results': []}})
elif args[:1] == ['rollback']:
    out({'event': 'error', 'error': {'rule': 'world.active', 'message': 'load another world first'}})
    sys.exit(3)
elif args[:2] == ['source', 'label']:
    out({'id': args[2], 'label': args[3]})
elif args[:2] == ['source', 'remove']:
    print('hyprune: not.found: no source ' + args[2], file=sys.stderr)
    sys.exit(3)
elif args[:1] == ['pin']:
    print('something unexpected', file=sys.stderr)
    sys.exit(1)
else:
    out({'event': 'done', 'result': {'args': args}})
