#!/usr/bin/env python3
"""Short AC-gated runs; no settings changes, only own child process group."""
import json, os, pathlib, select, signal, subprocess, sys, time

def online():
    return pathlib.Path('/sys/class/power_supply/AC0/online').read_text().strip() == '1'

def main():
    output, *command = sys.argv[1:]
    seconds=float(os.environ.get('HYPRUNE_DGPU_SECONDS','55'))
    if not 1 <= seconds <= 55: raise SystemExit('watchdog budget must be 1..55 seconds')
    if not command or not online():
        raise SystemExit('No command or AC offline: refused')
    # Continuous telemetry avoids synchronous nvidia-smi startup on the AC loop.
    meter=subprocess.Popen(['nvidia-smi','--query-gpu=power.draw,pstate,temperature.gpu','--format=csv,noheader,nounits','--loop-ms=500'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    os.set_blocking(meter.stdout.fileno(),False)
    log=open(output+'.stdout','w');child=None;samples=[];reason=None;buffer=b'';start=time.monotonic();last=start
    try:
        while True:
            if not online(): reason='AC offline';break
            elapsed=time.monotonic()-start
            if elapsed>seconds: reason=f'{seconds:g} second watchdog';break
            if time.monotonic()-last>5 or meter.poll() is not None:reason='power telemetry unavailable';break
            if select.select([meter.stdout],[],[],.1)[0]:
                buffer+=os.read(meter.stdout.fileno(),4096)
                while b'\n' in buffer:
                    line,buffer=buffer.split(b'\n',1)
                    power,pstate,temp=line.decode().strip().split(', ')
                    sample=dict(seconds=elapsed,watts=float(power),pstate=pstate,celsius=float(temp),ac=True);samples.append(sample);last=time.monotonic()
                    if sample['watts']>50 or sample['celsius']>75:reason='power/temperature ceiling';break
                if reason:break
            # Start only after the first valid telemetry sample and AC check.
            if child is None and samples and online():child=subprocess.Popen(command,start_new_session=True,stdout=log,stderr=subprocess.STDOUT)
            if child is not None and child.poll() is not None:break
    finally:
        if child is not None and child.poll() is None:
            os.killpg(child.pid,signal.SIGTERM)
            try:child.wait(timeout=12)
            except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
        meter.terminate();meter.wait(timeout=5)
        code=child.returncode if child is not None else None
        pathlib.Path(output).write_text(json.dumps(dict(reason=reason,returncode=code,samples=samples),indent=2)+'\n');log.close()
    print(json.dumps(dict(reason=reason,returncode=code,samples=len(samples))))
    return 1 if reason else code
if __name__=='__main__': sys.exit(main())
