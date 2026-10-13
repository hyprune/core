#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Demo source: plausible made-up values for a machine's inputs, as one JSON object.

    demo-source.py KEY=TYPE[:ARGS] ...

A newly placed machine starts with this as its data source (the shell writes the entry), so
it shows something live before the user wires it to a real system. One argument per input
key (repeats expanded: health.1, health.2 ...):

    cpu=number:0:100      a number in [min, max] (one decimal; two for ranges under 10)
    jobs=int:0:40         a whole number in [min, max]
    state=enum:ok|warn|fail   one of the values, mostly the first
    online=bool           true most of the time
    build=text:12         a short label, at most 12 characters (default 64)
    deployed=event        a counter that goes up every 20-45 s (each increase is an event)

Values drift smoothly with the clock (two slow sine waves per key, seeded by the key), so
consecutive runs look like a live system and nothing is remembered between runs.
DEMO_SOURCE_TIME overrides the clock (seconds since the epoch) for tests. Standard library
only; always exits 0 (an unreadable spec becomes an "_error" key).
"""
import hashlib
import json
import math
import os
import sys
import time

WORDS = ["demo", "idle", "busy", "build", "deploy", "queue", "ready", "sync"]


def unit(key, salt):
    """A stable number in [0, 1) for a key."""
    digest = hashlib.sha256(f"{key}/{salt}".encode()).digest()
    return int.from_bytes(digest[:4], "big") / 2**32


def wave(key, t):
    """A smooth value in [0, 1] for a key at time t: a slow swell plus a quicker ripple."""
    slow = 40 + 80 * unit(key, "p1")
    quick = 7 + 12 * unit(key, "p2")
    v = .5 + .35 * math.sin(2 * math.pi * (t / slow + unit(key, "f1"))) + .15 * math.sin(2 * math.pi * (t / quick + unit(key, "f2")))
    return min(1.0, max(0.0, v))


def number(text, default):
    try:
        return float(text) if text != "" else default
    except ValueError:
        return default


def value(key, kind, args, t):
    u = wave(key, t)
    if kind in ("number", "int"):
        lo = number(args[0] if args else "", 0.0)
        hi = number(args[1] if len(args) > 1 else "", 100.0 if kind == "number" else 10.0)
        if hi < lo:
            lo, hi = hi, lo
        v = lo + u * (hi - lo)
        if kind == "int":
            return int(round(v))
        return round(v, 2 if hi - lo < 10 else 1)
    if kind == "enum":
        values = [v for v in (args[0] if args else "").split("|") if v] or ["ok"]
        # Mostly the first (the normal state), now and then the others in order.
        index = 0 if u < .65 else min(len(values) - 1, 1 + int((u - .65) / .35 * (len(values) - 1)))
        return values[index]
    if kind == "bool":
        return u > .2
    if kind == "text":
        limit = max(1, int(number(args[0], 64))) if args else 64
        word = WORDS[int(unit(key, "w") * len(WORDS))]
        return f"{word} {int(u * 99) + 1}"[:limit]
    if kind == "event":
        period = 20 + 25 * unit(key, "e")
        return int(t // period)
    raise ValueError(f"unknown type {kind}")


def main(argv):
    t = float(os.environ.get("DEMO_SOURCE_TIME", time.time()))
    out, errors = {}, []
    for spec in argv:
        key, _, rest = spec.partition("=")
        kind, *args = rest.split(":", 2) if rest else [""]
        if not key or key.startswith("_"):
            errors.append(f"bad spec {spec!r}")
            continue
        try:
            out[key] = value(key, kind, args, t)
        except ValueError as e:
            errors.append(f"{key}: {e}")
    if errors:
        out["_error"] = "; ".join(errors)[:200]
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
