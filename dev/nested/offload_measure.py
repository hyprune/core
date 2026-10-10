#!/usr/bin/env python3
"""Offload frame-time measurement: Lumen Reach, scripted orbit, offload off then on.

Run isolated (dev/nested/isolated.py dev/nested/offload_measure.py OUT.json) and
pick the topology with HYPRUNE_NESTED_GPU: one node = compositor and renderer
on that GPU; `all` = the compositor follows the parent and the renderer takes
the first NVIDIA node, so on two-GPU hosts they may differ (recorded below).
Needs an admitted power profile; no real-session control.
"""
import argparse, json, math, os, pathlib, statistics, subprocess, sys, time
import session as s
import m3b
from m2 import wait, pose

SECONDS = float(os.environ.get('HYPRUNE_MEASURE_SECONDS', '12'))


def gpu_sample():
    out = subprocess.run(['nvidia-smi', '--query-gpu=pci.bus_id,utilization.gpu,power.draw,temperature.gpu', '--format=csv,noheader,nounits'],
                         capture_output=True, text=True).stdout
    return {l.split(',')[0].strip().lower(): [float(v) for v in l.split(',')[1:]] for l in out.splitlines() if l.count(',') == 3}


def dri_nodes(pid):
    try:
        return sorted({os.readlink(f'/proc/{pid}/fd/{fd}') for fd in os.listdir(f'/proc/{pid}/fd') if os.readlink(f'/proc/{pid}/fd/{fd}').startswith('/dev/dri/')})
    except OSError:
        return []


def stats(values):
    values = [v for v in values if isinstance(v, (int, float))]
    if not values:
        return None
    values.sort()
    return {'n': len(values), 'median': round(statistics.median(values), 3), 'p95': round(values[min(len(values) - 1, int(len(values) * .95))], 3),
            'mean': round(statistics.fmean(values), 3), 'max': round(values[-1], 3)}


def phase(client, mode, perf):
    patch = {'offload': mode}
    if os.environ.get('HYPRUNE_MEASURE_CAP_W'):  # only builds whose policy allows more than 80 W
        patch['offload_power_cap_w'] = float(os.environ['HYPRUNE_MEASURE_CAP_W'])
    client.call('graphics.set', {'patch': patch})
    if mode == 'on':
        try:
            wait(lambda: perf()['activeRenderer'] == 'offload', timeout=25)
        except Exception:
            return {'mode': mode, 'error': 'offload never became active', 'performance': perf()}
    else:
        wait(lambda: perf()['activeRenderer'] == 'in-process')
    time.sleep(1)
    frames, cpu, gpu, delivered, active, worker, gpus = [], [], [], [], [], {}, []
    start = time.monotonic(); last_gpu = 0
    while (t := time.monotonic() - start) < SECONDS:
        yaw = .6 * math.sin(t * .5); pitch = .08 * math.sin(t * .9)
        pose(client, [-8 + 1.5 * math.sin(t * .3), 2.0, 12 - 2 * math.sin(t * .2)], yaw, pitch)
        c = s.camera(); timing = c.get('timings', {})
        frames.append(timing.get('frameMs')); cpu.append(timing.get('cpuRenderMs')); delivered.append(timing.get('deliveredFPS'))
        gpu.append(sum(timing.get('gpuPassMs') or []) or None)
        p = perf(); active.append(p['activeRenderer'])
        for key, value in (p.get('workerTimings') or {}).items():
            if isinstance(value, (int, float)):
                worker.setdefault(key, []).append(value)
        if time.monotonic() - last_gpu > 1:
            gpus.append(gpu_sample()); last_gpu = time.monotonic()
        time.sleep(.05)
    per_gpu = {}
    for sample in gpus:
        for bus, (util, watts, celsius) in sample.items():
            per_gpu.setdefault(bus, {'util': [], 'watts': [], 'celsius': []})
            per_gpu[bus]['util'].append(util); per_gpu[bus]['watts'].append(watts); per_gpu[bus]['celsius'].append(celsius)
    p = perf()
    return {'mode': mode, 'seconds': SECONDS, 'activeFraction': round(active.count('offload') / max(1, len(active)), 3),
            'frameMs': stats(frames), 'cpuRenderMs': stats(cpu), 'gpuPassMsSum': stats(gpu), 'deliveredFPS': stats(delivered),
            'worker': {k: stats(v) for k, v in worker.items()},
            'gpus': {bus: {k: stats(v) for k, v in d.items()} for bus, d in per_gpu.items()},
            'reason': p.get('reason'), 'effective': {k: p.get('effective', {}).get(k) for k in ('offload', 'target_fps', 'offload_power_cap_w')},
            'costsMs': p.get('costsMs'), 'pid': p.get('pid')}


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output)
    client.call('world.load', {'worldId': 'com.hyprune.lumen-reach'})
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    def perf(): return client.call('graphics.get', {})['performance']
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: state()['runtime']['active'])
    client.call('movement.set', {'modeId': 'com.hyprune.fly'})
    pose(client, [-8, 2, 12]); time.sleep(2)
    info = json.loads((s.STATE / 'session.json').read_text())
    result = {'renderer': s.camera().get('timings', {}).get('renderer'), 'compositorNodes': dri_nodes(info['pid']),
              'gpuEnv': os.environ.get('HYPRUNE_NESTED_GPU', 'auto'), '4k': os.environ.get('HYPRUNE_NESTED_4K') == '1', 'phases': []}
    for mode in ['off', 'on', 'off']:
        result['phases'].append(phase(client, mode, perf))
        if mode == 'on':
            pid = result['phases'][-1].get('pid') or perf().get('pid')
            result['workerNodes'] = dri_nodes(pid) if pid else []
        out.write_text(json.dumps(result, indent=2) + '\n')
    client.call('graphics.set', {'patch': {'offload': 'off'}})
    on = result['phases'][1]
    print('MEASURE', json.dumps({'compositor': result['compositorNodes'], 'worker': result.get('workerNodes'),
                                 'offFrameMs': (result['phases'][0]['frameMs'] or {}).get('median'),
                                 'onFrameMs': (on.get('frameMs') or {}).get('median'), 'onActive': on.get('activeFraction'),
                                 'error': on.get('error')}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    os.environ['HYPRUNE_NESTED_PROTOCOL'] = '0.10'
    m3b.acceptance = acceptance
    s.run(argparse.Namespace(m3b=True, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False, record=False, visible=False, output=a.output))
