"""Live tooling acceptance. Executed only inside run.sh's lifetime flock."""
import importlib.util, json, os, pathlib, signal, subprocess, time
import session as s
from ctl import verified


def acceptance(identity,children):
    _,env=verified(); tools=s.ROOT/'dev/live';state=s.STATE/'live';state.mkdir(exist_ok=True)
    def command(name,tty=False):
        environment=env.copy()
        environment['HYPRUNE_DEV_AUTOMATION']='1'
        if os.environ.get('HYPRUNE_SHELL_DIR'): environment['HYPRUNE_SHELL_DIR']=os.environ['HYPRUNE_SHELL_DIR']
        # Verify stale inherited signature is ignored even with an explicit target.
        environment['HYPRLAND_INSTANCE_SIGNATURE']='deliberately-stale'
        if tty:
            environment.pop('WAYLAND_DISPLAY',None);environment.pop('HYPRLAND_INSTANCE_SIGNATURE',None)
        args=[str(tools/name),'--state-dir',str(state)]
        if not tty:args+=['--instance',identity['signature']]
        try:return subprocess.check_output(args,env=environment,text=True,stderr=subprocess.STDOUT,timeout=180)
        except subprocess.CalledProcessError as e:print(e.output,flush=True);raise
    results=[]
    def check(name,value):
        assert value,name;results.append(name);print('PASS live:',name,flush=True)
    # A harmless API-compatible fixture exercises exact old-path recovery.
    fixture=s.STATE/'hypr3d-live-fixture.so';source=s.STATE/'old-plugin.cpp'
    source.write_text('''#include <hyprland/src/plugins/PluginAPI.hpp>
APICALL EXPORT std::string PLUGIN_API_VERSION(){return HYPRLAND_API_VERSION;}
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE){return {"hypr3d","Inert restoration fixture","Hyprune tests","test"};}
APICALL EXPORT void PLUGIN_EXIT(){}
''')
    flags=subprocess.check_output(['pkg-config','--cflags','hyprland'],text=True).split()
    subprocess.run(['c++','-std=c++23','-shared','-fPIC','-fno-gnu-unique',*flags,str(source),'-o',str(fixture)],check=True)
    s.ctl('plugin','load',str(fixture));check('old_fixture_loaded',any(p['name']=='hypr3d' for p in json.loads(s.ctl('plugin','list','-j'))))
    config=s.STATE/'live-qs-config'
    hud=config/'quickshell/hypr3d';keeper=config/'quickshell/rick'
    for path in [hud,keeper]:path.mkdir(parents=True,exist_ok=True);(path/'shell.qml').write_text('import Quickshell\nShellRoot {}\n')
    old=subprocess.Popen(['qs','-p',str(hud)],env={**env,'XDG_CONFIG_HOME':str(config)},stdout=open(state/'fixture-hud.log','w'),stderr=subprocess.STDOUT);children.append(old)
    main=subprocess.Popen(['qs','-c','rick'],env={**env,'XDG_CONFIG_HOME':str(config)},stdout=open(state/'fixture-main.log','w'),stderr=subprocess.STDOUT);children.append(main)
    time.sleep(1);check('fixture_HUD_and_main_running',old.poll() is None and main.poll() is None)
    # Reserve the preferred chord so fallback is proved without rewriting it.
    s.ctl('eval','hl.bind("SUPER + SHIFT + H",hl.dsp.exec_cmd("true"),{description="Keep H binding"})')
    s.ctl('eval','hl.bind("SUPER + SHIFT + N",hl.dsp.exec_cmd("true"),{description="Keep this binding"})')
    before=json.loads(s.ctl('binds','-j'))
    output=command('preflight');(state/'preflight.log').write_text(output);check('preflight_ABI_and_plan','ABI/version matches' in output and 'No installed config' in output)
    check('preflight_read_only',json.loads(s.ctl('binds','-j'))==before and old.poll() is None)
    monitor=next(m for m in json.loads(s.ctl('monitors','-j')) if m['name']=='HEADLESS-1')
    xy=list(map(float,s.ctl('cursorpos').split(',')))
    mover=s.envrun(str(s.ROOT/'build/nested-pointer'),str(monitor['x']+500-xy[0]),str(monitor['y']+400-xy[1]));mover.wait(timeout=5);time.sleep(.2)
    s.ctl('eval','hl.dispatch(hl.dsp.focus({ monitor = "HEADLESS-1" }))')
    try:
        output=command('up');print(output,flush=True);(state/'up.log').write_text(output)
        recorded=json.loads((state/'state.json').read_text())
        (state/'up-record.json').write_text(json.dumps(recorded))
        check('exact_old_path_recorded',recorded['oldUnloaded']==[str(fixture)])
        check('old_HUD_stopped_main_preserved',old.poll() is not None and main.poll() is None)
        check('free_toggle_fallback',recorded['binds'][0]['chord']=='SUPER + SHIFT + F10')
        current=json.loads(s.ctl('plugin','list','-j'));check('exclusive_core',any(p['name']=='hyprune-core' for p in current) and not any(p['name']=='hypr3d' for p in current))
        check('up_stays_on_desktop',not s.camera()['active'])
        check('ready_notice_has_actual_fallback_chord','Hyprune ready · press Super+Shift+F10 to enter' in output)
        s.ctl('eval','hl.plugin.hyprune.toggle("HEADLESS-1")');time.sleep(.5)
        check('toggle_activates_loaded_world',s.camera()['active'])
        spec=importlib.util.spec_from_file_location('live_protocol_test',tools/'live.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        check('latest_launcher_and_common_shell_protocol',f'IPC: launcher {module.core_protocols()[0]}; shell {module.shell_protocol()}' in (state/'launcher.log').read_text())
        check('isolated_default_input',json.loads((pathlib.Path(recorded['runroot'])/'input.json').read_text())==json.loads((s.ROOT/'config/input.default.json').read_text()))
        output=command('status');(state/'status.log').write_text(output);check('status_logs_and_reports','last 20 lines' in output and 'New crash reports' in output)
        if os.environ.get('HYPRUNE_GRAPHICS_PROFILE')=='1':
            from graphics_profile import acceptance as regressions
        else:
            from live_regressions import acceptance as regressions
        regressions(state, recorded, children, check, module.shell_protocol())
        # Toggle via the installed runtime callback, then recover from helper death.
        s.ctl('eval','hl.plugin.hyprune.toggle()');time.sleep(.3);check('toggle_deactivates',not s.camera()['active'])
        s.ctl('eval','hl.plugin.hyprune.toggle()');time.sleep(.3);check('toggle_reactivates',s.camera()['active'])
        check('adopted_terminal_survives_toggle',any(e['title']=='Live-launch-regression' and e['visible'] and not e['collision'] for e in s.camera()['captures']))
        print(command('down',tty=True),flush=True)
        check('healthy_down',json.loads((state/'state.json').read_text())['phase']=='down' and main.poll() is None)
        print(command('up'),flush=True)
        recorded=json.loads((state/'state.json').read_text())
        check('second_up_unique_core_path',recorded['plugin']!=json.loads((state/'up-record.json').read_text())['plugin'] and not s.camera()['active'])
        old_record=next(r for r in recorded['qs'] if r['oldHUD'])
        helper=json.loads((state/'helper.json').read_text());os.kill(helper['pid'],signal.SIGKILL);time.sleep(.15)
        print(command('down',tty=True),flush=True)
        check('TTY_down_after_helper_crash',json.loads((state/'state.json').read_text())['phase']=='down')
        restored=json.loads(s.ctl('plugin','list','-j'));check('old_plugin_restored',any(p['name']=='hypr3d' for p in restored) and not any('hyprune' in p['name'] for p in restored))
        binds=json.loads(s.ctl('binds','-j'));check('runtime_bind_removed_original_kept',not any(b.get('description')=='Hyprune supervised live toggle' for b in binds) and any(b.get('description')=='Keep this binding' for b in binds))
        restored_pid=json.loads((state/'state.json').read_text())['restoredHUD'][str(old_record['pid'])]['pid']
        check('HUD_restarted_main_preserved',pathlib.Path(f'/proc/{restored_pid}').exists() and main.poll() is None)
        print(command('down',tty=True),flush=True);check('down_twice_no_duplicate_HUD',json.loads((state/'state.json').read_text())['restoredHUD'][str(old_record['pid'])]['pid']==restored_pid)
        (state/'final-status.log').write_text(command('status',tty=True))
    finally:
        subprocess.run([str(tools/'down'),'--instance',identity['signature'],'--state-dir',str(state)],env=env,timeout=30)
        if (state/'state.json').exists():
            rec=json.loads((state/'state.json').read_text())
            for restored in rec.get('restoredHUD',{}).values():
                with __import__('contextlib').suppress(ProcessLookupError):os.kill(restored['pid'],signal.SIGTERM)
        s.ctl('plugin','unload',str(fixture))
        # Unmap native nested clients before Hyprland destroys its monitor and
        # workspace controllers (0.56 can crash in fullscreen cleanup otherwise).
        for client in json.loads(s.ctl('clients','-j')):
            s.ctl('eval','hl.dispatch(hl.dsp.window.close({window='+json.dumps('address:'+client['address'])+'}))')
        time.sleep(.4)
    (state/'acceptance.json').write_text(json.dumps({'passed':results,'instance':identity['signature'],'nestedOnly':True},indent=2)+'\n')
    print('PASS live tooling:',len(results),'checks',flush=True)
