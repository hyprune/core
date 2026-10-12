#!/usr/bin/env python3
"""Dedicated-room regression; run under flock /tmp/worldshell-nested.lock."""
import argparse
import json
import subprocess
import time

import session as s
import m3b
from m2 import wait, keys

def acceptance(client, children, args, shell_token, socketpath):
    results = {}
    def check(name, value, evidence=None):
        assert value, (name, evidence)
        results[name] = evidence if evidence is not None else True
        print('PASS world workspace:', name, flush=True)
    def windows(): return json.loads(s.ctl('clients', '-j'))
    def monitor(): return next(m for m in json.loads(s.ctl('monitors', '-j')) if m['name']=='HEADLESS-1')
    def workspace(): return monitor()['activeWorkspace']['id']
    def change(id): s.ctl('eval', f'hl.dispatch(hl.dsp.focus({{workspace="{id}"}}))'); wait(lambda:workspace()==id)
    def launch(title, binding=False):
        command = f'kitty --class room-regression --title {title} bash --noprofile --norc'
        if binding:
            s.ctl('eval', 'hl.bind("SUPER + F8", hl.dsp.exec_cmd('+json.dumps(command)+'))')
            keys('-M','logo','-P','Super_L','-s','40','-k','F8','-p','Super_L','-m','logo')
            s.ctl('eval', 'hl.unbind("SUPER + F8")')
        else:
            children.append(s.envrun('kitty','--class','room-regression','--title',title,'bash','--noprofile','--norc',stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
        return wait(lambda:next((w for w in windows() if w['title']==title),None))
    def toggle(active):
        s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")')
        wait(lambda:s.camera()['active']==active)
    def geometry(w): return {k:w[k] for k in ['at','size','floating','workspace']}
    mapped_log = s.STATE/'mapped-windows.log'
    mapped_log.unlink(missing_ok=True)
    s.ctl('eval','hl.on("window.open", function(w) local f=assert(io.open('+json.dumps(str(mapped_log))+',"a")); f:write(w.title..":"..tostring(w.floating)..":"..tostring(w.workspace.id).."\\n"); f:close() end)')
    change(10)
    a = launch('Before-room-A'); b = launch('Before-room-B'); time.sleep(.3)
    before = {w['address']:geometry(w) for w in windows()}
    check('baseline_tiled', all(not w['floating'] for w in windows()))
    client.call('world.load',{'worldId':'com.hyprune.lumen-reach'})
    toggle(True)
    check('default_room_20_on_selected_output',workspace()==20 and s.camera()['previousWorkspace']==10)
    spawned = launch('Room-keybind', True)
    check('launch_is_floating_on_room',spawned['floating'] and spawned['workspace']['id']==20,spawned)
    check('floating_at_initial_map_before_capture',wait(lambda:'Room-keybind:true:20' in mapped_log.read_text()))
    plane = wait(lambda:next((e for e in s.camera()['captures'] if e['title']=='Room-keybind' and e['visible']),None))
    check('launch_adopted_clear_of_geometry',not plane['collision'],plane)
    check('other_workspace_geometry_unchanged',all(geometry(w)==before[w['address']] for w in windows() if w['address'] in before))
    check('other_workspace_not_adopted',not any(e['title'].startswith('Before-room') for e in s.camera()['captures']))
    # Move focus to another workspace while active: scope must follow the room,
    # not the active monitor or the last launch on that monitor.
    # 21-24 are Lumen Reach's default area homes (workspace_base 20 + slot); 30 is outside 21-29.
    change(30)
    other = launch('Other-workspace', True)
    check('other_workspace_launch_stays_tiled',not other['floating'] and other['workspace']['id']==30)
    time.sleep(.3)
    check('other_workspace_launch_not_adopted',not any(e['title']=='Other-workspace' for e in s.camera()['captures']))
    change(20)
    s.ctl('eval','hl.window_rule({name="room-test-redirect",match={title="^Redirected-room$"},workspace="30 silent"})')
    redirected=launch('Redirected-room',True)
    check('explicit_workspace_rule_stays_tiled',redirected['workspace']['id']==30 and not redirected['floating'],redirected)
    toggle(False); check('exit_returns_previous_workspace',workspace()==10)
    toggle(True); check('reenter_returns_room',workspace()==20)
    check('adopted_identity_survives_toggle',wait(lambda:any(e['id']==plane['id'] and e['visible'] for e in s.camera()['captures'])))
    # The native event listener must survive a config reload, with no generated
    # rule/config edit required before the next client is mapped.
    s.ctl('reload');time.sleep(.3)
    reloaded=launch('After-config-reload',True)
    check('scoped_float_policy_survives_reload',reloaded['floating'] and reloaded['workspace']['id']==20)
    toggle(False)
    change(20)
    inactive=launch('Inactive-room',True)
    check('float_policy_disabled_on_exit',not inactive['floating'])
    change(10)
    s.ctl('eval','hl.plugin.hyprune.config({windows={world_workspace=18}})');time.sleep(.2)
    toggle(True)
    check('configured_room',workspace()==18)
    custom=launch('Configured-room',True)
    check('configured_room_floats',custom['floating'] and custom['workspace']['id']==18)
    client.call('workspace.bind',{'id':10,'worldId':'com.hyprune.lumen-reach','areaId':'sun-court'})
    time.sleep(.5)
    client.call('workspace.focus',{'id':10});wait(lambda:workspace()==10)
    client.call('workspace.unfocus',{});wait(lambda:workspace()==18)
    check('workspace_focus_exit_returns_room',True)
    client.call('workspace.focus',{'id':10});wait(lambda:workspace()==10)
    toggle(False);check('toggle_from_focus_exits_and_restores',workspace()==10)
    toggle(True);wait(lambda:workspace()==18)
    # New config is used on the next activation; the current return target/policy
    # must not change under an active user.
    s.ctl('eval','hl.plugin.hyprune.config({windows={world_workspace=19}})');time.sleep(.2)
    check('active_workspace_config_is_deferred',s.camera()['worldWorkspace']==18 and workspace()==18)
    toggle(False);check('configured_room_return',workspace()==10)
    toggle(True);check('next_activation_uses_changed_room',workspace()==19)
    # Explicitly test direct unload cleanup, in this verified nested instance.
    # The directory can contain prior unique copies; use the exact current copy.
    plugin = max(s.STATE.glob('hyprune-*.so'),key=lambda p:p.stat().st_mtime_ns)
    s.ctl('plugin','unload',str(plugin));wait(lambda:workspace()==10)
    change(19)
    unloaded=launch('After-unload',True)
    check('unload_restores_workspace_and_removes_listener',not unloaded['floating'])
    (s.STATE/'world-workspace-acceptance.json').write_text(json.dumps(results,indent=2)+'\n')
    print('PASS dedicated world workspace:',len(results),'checks',flush=True)

if __name__ == '__main__':
    fixture=s.state_dir('world-workspace');fixture.mkdir(exist_ok=True)
    (fixture/'layout.json').write_text('{"version":1,"bindings":[],"placements":[]}\n')
    m3b.acceptance=acceptance
    s.run(argparse.Namespace(state='world-workspace',m3b=True,m3a=False,m2=False,m1=False,integration=False,live=False,serve=False,record=False,visible=False))
