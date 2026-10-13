#!/usr/bin/env python3
"""Record a Lumen Reach walkthrough on HEADLESS-1 with the route/autodrive API.

  HYPRUNE_NESTED_4K=1 dev/nested/isolated.py dev/nested/walkthrough.py OUT.mkv

The Sun Court arrival walks along the authored walking graph into Array Hall
at the High preset, in walk mode, then pans across the hall (an eased core camera
path). Every presented frame of HEADLESS-1 is exported to OUT (cinema.py): NVENC
constant-QP by default (RGB in, converted on the GPU), HYPRUNE_WALKTHROUGH_CODEC=libx264
for CPU; HYPRUNE_WALKTHROUGH_FPS keeps 60 (default) or 30 frames a second. Encoding
for the gallery is a separate step. Nested only.
"""
import argparse, json, math, os, pathlib, subprocess, time
import session as s
import cinema
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
    codec = os.environ.get('HYPRUNE_WALKTHROUGH_CODEC', 'h264_nvenc')
    encoder = ['-c:v', 'h264_nvenc', '-preset', 'p4', '-rc', 'constqp', '-qp', '16'] if 'nvenc' in codec else \
              ['-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '14', '-pix_fmt', 'yuv420p']
    fps = int(os.environ.get('HYPRUNE_WALKTHROUGH_FPS', '30'))
    cam = cinema.Cinema(s)
    recorder = cam.recording(out, encoder=encoder, out_fps=fps if fps != 60 else None)
    started = recorder.__enter__(); samples = []
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
        c = s.camera(); here = c['position']; start_yaw = c.get('yaw', 0)
        cam.run([{'t': 0, 'eye': here, 'yaw': start_yaw, 'pitch': -.03, 'ease': 'inout'},
                 {'t': 3, 'eye': here, 'yaw': start_yaw - .9, 'pitch': -.03}])
        hold(1.5)
    finally:
        recorder.__exit__(None, None, None)
        cam.stop()
    end = state()['movement']['position']
    report = {'output': str(out), 'seconds': round(time.monotonic() - started, 1), 'route': PATH, 'speed': SPEED,
              'end': end, 'target': points[-1], 'graphics': graphics.get('performance', {}).get('effective'), 'samples': samples}
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0', '-show_entries',
                                                'stream=nb_read_frames,width,height', '-of', 'json', str(out)]))['streams'][0]
    report.update(frames=int(probe['nb_read_frames']), size=[probe['width'], probe['height']], codec=codec)
    report['captureFps'] = round(report['frames'] / report['seconds'], 2)
    report['export'] = cam.last_export
    report['smoothness'] = cinema.gate(out)
    out.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    assert report['export']['written'] == report['export']['grabbed'], ('export lost frames', report['export'])
    distance = math.dist([end[0], end[2]], [points[-1][0], points[-1][2]])
    print('WALKTHROUGH', json.dumps({'seconds': report['seconds'], 'endDistance': round(distance, 2), 'captureFps': report['captureFps'],
                                     'smoothness': cinema.gate_summary(report['smoothness'])}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); a = parser.parse_args()
    os.environ['HYPRUNE_NESTED_PROTOCOL'] = '0.15'
    m3b.acceptance = acceptance
    s.run(argparse.Namespace(state='walkthrough', m3b=True, m3a=False, m2=False, m1=False, integration=False, live=False, serve=False, record=False, visible=False, output=a.output))
