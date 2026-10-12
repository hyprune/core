#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""System gauges: CPU, RAM, GPU or VRAM use as one JSON object for a gauge machine.

    system-gauges.py cpu|ram|gpu|vram [--gpu auto|nvidia|amd|intel]

Prints {"value": 0-100, "display": "...", "label": "CPU"|"RAM"|"GPU"|"VRAM",
"level": "ok"|"warn"|"fail"} (warn from 75, fail from 90) and always exits 0: anything it
cannot read shows as value 0 with a display such as "n/a".

- cpu: busy share of all CPUs since the previous run (/proc/stat, remembered in
  $XDG_RUNTIME_DIR/hyprune-system-gauges.json); the first run samples for 0.25 s.
- ram: MemTotal - MemAvailable from /proc/meminfo; display in GB.
- gpu, vram: the discrete GPU when there is one (NVIDIA, then AMD, then Intel).
  A runtime-suspended GPU is never woken: it shows "asleep". NVIDIA is read with
  nvidia-smi, and only while one of your processes has the GPU open (otherwise "idle"),
  so polling never keeps a laptop's dGPU from suspending. AMD reads amdgpu's sysfs.
  Intel shows its current clock as a share of the maximum ("~35%"); its memory is
  "shared".
Standard library only.
"""
import argparse
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

WARN, FAIL = 75, 90
LABELS = {"cpu": "CPU", "ram": "RAM", "gpu": "GPU", "vram": "VRAM"}
VENDORS = {"0x10de": "nvidia", "0x1002": "amd", "0x8086": "intel"}
PRIORITY = ["nvidia", "amd", "intel"]
STATE_NAME = "hyprune-system-gauges.json"
CPU_SAMPLE_S, CPU_STATE_MIN_S, CPU_STATE_MAX_S = 0.25, 0.2, 120.0
NVIDIA_QUERY = ["nvidia-smi", "--query-gpu=utilization.gpu,memory.used,memory.total",
                "--format=csv,noheader,nounits"]
NVIDIA_TIMEOUT_S = 3.0
GIB = 1024 ** 3


def level(value):
    return "fail" if value >= FAIL else "warn" if value >= WARN else "ok"


def gauge(kind, value, display):
    value = round(min(100.0, max(0.0, float(value))), 1)
    return {"value": value, "display": display, "label": LABELS[kind], "level": level(value)}


def idle(kind, display):
    """A gauge with nothing to show (asleep, idle, n/a, shared)."""
    return {"value": 0, "display": display, "label": LABELS[kind], "level": "ok"}


def read(path, default=None):
    try:
        return pathlib.Path(path).read_text().strip()
    except (OSError, UnicodeDecodeError):
        return default


def read_int(path):
    try:
        return int(read(path))
    except (TypeError, ValueError):
        return None


def pct(value):
    """Whole percent, halves rounded up (the value itself keeps one decimal)."""
    return f"{math.floor(round(value, 1) + 0.5)}%"


def gb(n_bytes):
    return f"{n_bytes / GIB:.1f} GB"


# --- CPU -----------------------------------------------------------------------------

def cpu_times(root):
    """(total, idle) jiffies of the aggregate cpu line."""
    fields = read(root / "proc/stat", "").splitlines()[0].split()
    if not fields or fields[0] != "cpu":
        raise ValueError("no cpu line")
    values = [int(x) for x in fields[1:9]]  # user nice system idle iowait irq softirq steal
    return sum(values), values[3] + (values[4] if len(values) > 4 else 0)


def busy(before, after):
    total, idle_time = after[0] - before[0], after[1] - before[1]
    return None if total <= 0 else 100.0 * (1 - idle_time / total)


def state_file():
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    return pathlib.Path(runtime) / STATE_NAME if runtime and os.path.isdir(runtime) else None


def load_state(path):
    try:
        return json.loads(path.read_text()) if path else {}
    except (OSError, ValueError):
        return {}


def save_state(path, state):
    if not path:
        return
    temp = path.with_name(f".{path.name}.{os.getpid()}")
    try:
        fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as stream:
            json.dump(state, stream)
        os.replace(temp, path)
    except OSError:
        try:
            temp.unlink()
        except OSError:
            pass


def cpu(root, state_path, sleep=time.sleep, clock=time.monotonic):
    now, at = cpu_times(root), clock()
    state = load_state(state_path)
    previous = state.get("cpu")
    usage = None
    if isinstance(previous, dict) and CPU_STATE_MIN_S <= at - previous.get("at", -1e9) <= CPU_STATE_MAX_S:
        try:
            usage = busy((int(previous["total"]), int(previous["idle"])), now)
        except (KeyError, TypeError, ValueError):
            usage = None
    if usage is None:  # first run, a stale state or a reboot: sample briefly
        sleep(CPU_SAMPLE_S)
        before, now, at = now, cpu_times(root), clock()
        usage = busy(before, now) or 0.0
    state["cpu"] = {"total": now[0], "idle": now[1], "at": at}
    save_state(state_path, state)
    return gauge("cpu", usage, pct(usage))


# --- RAM -----------------------------------------------------------------------------

def ram(root):
    info = {}
    for line in read(root / "proc/meminfo", "").splitlines():
        name, _, rest = line.partition(":")
        parts = rest.split()
        if parts and parts[0].isdigit():
            info[name] = int(parts[0]) * 1024
    total = info.get("MemTotal")
    if not total:
        return idle("ram", "n/a")
    available = info.get("MemAvailable")
    if available is None:
        available = info.get("MemFree", 0) + info.get("Buffers", 0) + info.get("Cached", 0)
    used = max(0, total - available)
    return gauge("ram", 100.0 * used / total, gb(used))


# --- GPU -----------------------------------------------------------------------------

def gpus(root):
    """Display controllers on the PCI bus: [{vendor, path}], preferred first."""
    found = []
    for device in sorted((root / "sys/bus/pci/devices").glob("*")):
        if not (read(device / "class", "") or "").startswith("0x03"):
            continue
        vendor = VENDORS.get(read(device / "vendor", ""))
        if vendor:
            # Prefer the vendor order, then a GPU that is not the boot display (a dGPU).
            found.append({"vendor": vendor, "path": device, "boot": read(device / "boot_vga") == "1"})
    found.sort(key=lambda g: (PRIORITY.index(g["vendor"]), g["boot"]))
    return found


def suspended(device):
    return read(device / "power/runtime_status") == "suspended"


def nvidia_in_use(root):
    """True when one of this user's processes has an NVIDIA GPU node (/dev/nvidiaN) open."""
    pattern = re.compile(r"/dev/nvidia[0-9]+")
    for process in (root / "proc").glob("[0-9]*"):
        try:
            descriptors = os.scandir(process / "fd")
        except OSError:
            continue
        with descriptors:
            for entry in descriptors:
                try:
                    if pattern.fullmatch(os.readlink(entry.path)):
                        return True
                except OSError:
                    continue
    return False


def nvidia(kind, root, devices, run, which):
    if any(suspended(d["path"]) for d in devices if d["vendor"] == "nvidia"):
        return idle(kind, "asleep")
    if not which("nvidia-smi"):
        return idle(kind, "n/a")
    if not nvidia_in_use(root):
        return idle(kind, "idle")  # querying would keep the GPU from suspending
    try:
        result = run(NVIDIA_QUERY, capture_output=True, text=True, timeout=NVIDIA_TIMEOUT_S, stdin=subprocess.DEVNULL)
        busy_percent, used, total = (float(x) for x in result.stdout.splitlines()[0].split(","))
    except (OSError, subprocess.SubprocessError, ValueError, IndexError):
        return idle(kind, "n/a")
    if kind == "gpu":
        return gauge(kind, busy_percent, pct(busy_percent))
    if total <= 0:
        return idle(kind, "n/a")
    return gauge(kind, 100.0 * used / total, gb(used * 1024 * 1024))


def amd(kind, device):
    if suspended(device):
        return idle(kind, "asleep")
    if kind == "gpu":
        value = read_int(device / "gpu_busy_percent")
        return idle(kind, "n/a") if value is None else gauge(kind, value, pct(value))
    used, total = read_int(device / "mem_info_vram_used"), read_int(device / "mem_info_vram_total")
    if used is None or not total:
        return idle(kind, "n/a")
    return gauge(kind, 100.0 * used / total, gb(used))


def intel(kind, device):
    if kind == "vram":
        return idle(kind, "shared")
    if suspended(device):
        return idle(kind, "asleep")
    # Clock share, not busy time: the closest sysfs offers without perf counters.
    for card in sorted(device.glob("drm/card[0-9]*")):
        actual, top = read_int(card / "gt_act_freq_mhz"), read_int(card / "gt_max_freq_mhz")  # i915
        if actual is not None and top:
            return gauge(kind, 100.0 * actual / top, "~" + pct(100.0 * actual / top))
    for gt in sorted(device.glob("tile*/gt*/freq0")):  # xe
        actual, top = read_int(gt / "act_freq"), read_int(gt / "max_freq")
        if actual is not None and top:
            return gauge(kind, 100.0 * actual / top, "~" + pct(100.0 * actual / top))
    return idle(kind, "n/a")


def gpu(kind, root, choice="auto", run=subprocess.run, which=shutil.which):
    devices = gpus(root)
    if choice != "auto":
        devices = [d for d in devices if d["vendor"] == choice]
    if not devices:
        return idle(kind, "n/a")
    first = devices[0]
    if first["vendor"] == "nvidia":
        return nvidia(kind, root, devices, run, which)
    if first["vendor"] == "amd":
        return amd(kind, first["path"])
    return intel(kind, first["path"])


def measure(kind, root=pathlib.Path("/"), choice="auto", state_path=None, **hooks):
    try:
        if kind == "cpu":
            return cpu(root, state_path)
        if kind == "ram":
            return ram(root)
        return gpu(kind, root, choice, **hooks)
    except Exception:  # never a traceback: a gauge shows n/a instead
        return idle(kind, "n/a")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("kind", choices=sorted(LABELS))
    parser.add_argument("--gpu", choices=["auto"] + PRIORITY, default="auto",
                        help="which GPU to read (default: the discrete one)")
    args = parser.parse_args(argv)
    print(json.dumps(measure(args.kind, choice=args.gpu, state_path=state_file()), separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
