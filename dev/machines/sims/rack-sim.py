#!/usr/bin/env python3
"""Server rack simulator: eight units with load waves and the odd outage.

Each unit's load is two phase-shifted sines plus smooth noise; a unit above 0.85 load
is `warn`. Every few minutes one unit goes down (`fail`, load 0) for 20-60 s while
the others pick up its share; a second, rarer outage channel means at most two units
are down at once. Now and then a unit is `off` for a few minutes of maintenance.
Stateless: a pure function of time and seed. Prints one JSON object (see README.md).
"""
import math
import os

import simlib

UNITS = 8
WARN_LOAD = 0.85
# (window seconds, chance per window): each channel takes down at most one unit per window.
OUTAGES = [(240, 0.6), (420, 0.3)]
MAINTENANCE = (1800, 0.35)   # a unit off for 2-5 minutes in some half hours


def outage(t, s, channel, window, chance):
    """The unit (0-based) down at t on this channel, or None."""
    k = math.floor(t / window)
    if simlib.unit(s, "r-out", channel, k) >= chance:
        return None
    length = 20 + 40 * simlib.unit(s, "r-len", channel, k)
    start = k * window + (window - length - 5) * simlib.unit(s, "r-at", channel, k)
    if start <= t < start + length:
        return int(simlib.unit(s, "r-unit", channel, k) * UNITS)
    return None


def maintenance(t, s):
    window, chance = MAINTENANCE
    k = math.floor(t / window)
    if simlib.unit(s, "r-maint", k) >= chance:
        return None
    length = 120 + 180 * simlib.unit(s, "r-mlen", k)
    start = k * window + (window - length - 5) * simlib.unit(s, "r-mat", k)
    if start <= t < start + length:
        return int(simlib.unit(s, "r-munit", k) * UNITS)
    return None


def base_load(t, s, i):
    p1 = 70 + 50 * simlib.unit(s, "r-p1", i)
    p2 = 17 + 13 * simlib.unit(s, "r-p2", i)
    load = 0.54 + 0.08 * (simlib.unit(s, "r-mean", i) - 0.5)
    load += 0.24 * math.sin(2 * math.pi * t / p1 + 6.3 * simlib.unit(s, "r-f1", i))
    load += 0.08 * math.sin(2 * math.pi * t / p2 + 6.3 * simlib.unit(s, "r-f2", i))
    load += 0.07 * simlib.smooth_noise(t, 3, s, "r-n", i)
    return load


def simulate(t):
    s = simlib.seed()
    failed = {u for c, (w, p) in enumerate(OUTAGES) if (u := outage(t, s, c, w, p)) is not None}
    off = maintenance(t, s)
    if off in failed:
        off = None
    missing = len(failed) + (off is not None)
    share = 0.09 * missing  # survivors pick up the missing units' work
    values, loads = {}, []
    for i in range(UNITS):
        if i in failed or i == off:
            load, health = 0.0, "fail" if i in failed else "off"
        else:
            load = round(simlib.clamp(base_load(t, s, i) + share, 0.02, 1.0), 3)
            health = "warn" if load > WARN_LOAD else "ok"
        values[f"health.{i + 1}"] = health
        loads.append(load)
    for i, load in enumerate(loads):
        values[f"load.{i + 1}"] = load
    values["load"] = round(sum(loads) / UNITS, 3)
    values["down"] = len(failed)
    values["name"] = os.environ.get("MACHINES_SIM_NAME", "rack-a")
    return values


if __name__ == "__main__":
    simlib.main(simulate, __doc__.splitlines()[0])
