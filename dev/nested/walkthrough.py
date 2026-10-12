#!/usr/bin/env python3
"""Record a Lumen Reach walkthrough on HEADLESS-1 with the route/autodrive API.

  HYPRUNE_NESTED_4K=1 dev/nested/isolated.py dev/nested/walkthrough.py OUT.mkv

The Sun Court arrival walks along the authored walking graph into Array Hall
at the High preset, in walk mode, then pans across the hall. wf-recorder
captures HEADLESS-1 to OUT: NVENC constant-QP by default (RGB in, so no CPU
colour conversion keeps 4K at 30 fps), HYPRUNE_WALKTHROUGH_CODEC=libx264 for
CPU. Encoding for the gallery is a separate step. Nested only.
"""
import argparse, json, math, os, pathlib, signal, subprocess, time
import session as s
import m3b
from m2 import wait, pose

LUMEN = 'com.hyprune.lumen-reach'
PATH = ['arrival', 'court-junction', 'court-wall', 'hall-entry', 'hall-arrival', 'hall-center']
SPEED = float(os.environ.get('HYPRUNE_WALKTHROUGH_SPEED', '1.9'))


def acceptance(client, children, args, shell_token, socketpath):
    out = pathlib.Path(args.output)
    manifest = json.loads((s.STATE / 'worlds' / LUMEN / 'world.json').read_text())
    nodes = {n['id']: n['position'] for n in manifest['extensions']['com.hyprune.lumen-reach.routes']['nodes']}
    points = [nodes[n] for n in PATH]
    client.call('world.load', {'worldId': LUMEN})
    sub = client.call('state.subscribe', {'topics': ['state']})['subscriptionId']
    def state(): return client.call('state.snapshot', {'subscriptionId': sub})['snapshot']['state']
    client.call('graphics.set', {'patch': {'quality': 'high'}})
    s.ctl('eval', 'hl.plugin.hyprune.toggle("HEADLESS-1")')
    wait(lambda: state()['runtime']['active'])
    client.call('movement.set', {'modeId': 'com.hyprune.walk'})
    first, second = points[0], points[1]
    yaw = math.atan2(second[0] - first[0], -(second[2] - first[2]))
    pose(client, [first[0], first[1] + 1.7, first[2]], yaw, -.03)
    time.sleep(4)  # uploads and shader warm-up finish before the first recorded frame
    graphics = client.call('graphics.get', {})
    log = open(s.STATE / 'recorder.log', 'w')
    codec = os.environ.get('HYPRUNE_WALKTHROUGH_CODEC', 'h264_nvenc')
    quality = ['-x', 'bgr0', '-p', 'preset=p4', '-p', 'rc=constqp', '-p', 'qp=16'] if 'nvenc' in codec else \
              ['-x', 'yuv420p', '-p', 'preset=ultrafast', '-p', 'crf=14']
    recorder = s.envrun('wf-recorder', '-o', 'HEADLESS-1', '-r', '30', '-D', '--no-dmabuf', '-c', codec, *quality,
                        '-f', str(out), stdout=log, stderr=subprocess.STDOUT)
    children.append(recorder)
    started = time.monotonic(); samples = []
    def hold(seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            c = s.camera(); samples.append({'t': round(time.monotonic() - started, 2), 'position': c['position'], 'frameMs': c['timings'].get('frameMs')})
            time.sleep(.2)
    try:
        hold(2.5)
        client.call('route.set', {'points': [{'position': [p[0], p[1] + 1.7, p[2]]} for p in points[1:]]})
        client.call('autodrive.start', {'speed': SPEED})
        until = time.monotonic() + 42
        while time.monotonic() < until and (time.monotonic() - started < 6 or state()['location']['autodrive']):
            hold(.5)
        hold(1)
        # Close on a slow pan across the hall instead of the wall ahead.
        c = s.camera(); here = state()['movement']['position']; start_yaw = c.get('yaw', 0)
        for i in range(1, 61):
            pose(client, here, start_yaw - .9 * (1 - math.cos(math.pi * i / 60)) / 2, -.03); time.sleep(.05)
        hold(1.5)
    finally:
        recorder.send_signal(signal.SIGINT); recorder.wait(timeout=30); log.close()
    end = state()['movement']['position']
    report = {'output': str(out), 'seconds': round(time.monotonic() - started, 1), 'route': PATH, 'speed': SPEED,
              'end': end, 'target': points[-1], 'graphics': graphics.get('performance', {}).get('effective'), 'samples': samples}
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0', '-show_entries',
                                                'stream=nb_read_frames,width,height', '-of', 'json', str(out)]))['streams'][0]
    report.update(frames=int(probe['nb_read_frames']), size=[probe['width'], probe['height']], codec=codec)
    report['captureFps'] = round(report['frames'] / report['seconds'], 2)
    out.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    assert report['captureFps'] > 28, ('capture could not keep 30 fps', report['captureFps'])
    distance = math.dist([end[0], end[2]], [points[-1][0], points[-1][2]])
    print('WALKTHROUGH', json.dumps({'seconds': report['seconds'], 'endDistance': round(distance, 2), 'captureFps': report['captureFps']}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    os.environ['HYPRUNE_NESTED_PROTOCOL'] = '0.15'
    m3b.acceptance = acceptance
    s.run(argparse.Namespace(state='walkthrough', m3b=True, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False, record=False, visible=False, output=a.output))
