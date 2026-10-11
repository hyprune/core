#!/usr/bin/env python3
"""Message queue simulator: depth that grows and drains in waves.

Arrivals follow slow waves (1.5-5.5 items/s). Depth rides a few smooth waves between
roughly 0 and 55; every so often the consumer stalls and a backlog builds to 300-480
items, then the consumer catches up at 2-3x the arrival rate. `oldestAgeS` is the age
of the oldest item (FIFO: how far back the last `depth` arrivals reach), so it climbs
during a backlog: `level` is `warn` above 30 s and `fail` above 90 s. Stateless: a pure
function of time and seed. Prints one JSON object (see README.md for the keys).
"""
import math

import simlib

WINDOW_S = 420          # at most one backlog per window
SPIKE_CHANCE = 0.45
WARN_AGE, FAIL_AGE = 30, 90
MAX_DEPTH = 500


def arrivals(t, s):
    """Arrival rate (items/s), 1.5-5.5."""
    wave = 1.1 * math.sin(2 * math.pi * t / 173 + simlib.unit(s, "q-a") * 6.3)
    wave += 0.5 * math.sin(2 * math.pi * t / 61 + simlib.unit(s, "q-b") * 6.3)
    return simlib.clamp(3.4 + wave + 0.4 * simlib.smooth_noise(t, 7, s, "q-n"), 1.5, 5.5)


def spike(t, s):
    """Backlog on top of the normal depth: a stall (rise) then a catch-up (drain)."""
    k = math.floor(t / WINDOW_S)
    total = 0.0
    for w in (k - 1, k):  # a backlog never outlives the window after its own
        if simlib.unit(s, "q-spike", w) >= SPIKE_CHANCE:
            continue
        peak = 300 + 180 * simlib.unit(s, "q-peak", w)
        rise_rate = 2.0 + 1.0 * simlib.unit(s, "q-rise", w)     # stalled consumer: depth grows
        drain_rate = 6.0 + 4.0 * simlib.unit(s, "q-drain", w)   # consumer catches up
        rise, drain = peak / rise_rate, peak / drain_rate
        start = w * WINDOW_S + (WINDOW_S - rise - drain - 10) * simlib.unit(s, "q-start", w)
        x = t - start
        if 0 <= x < rise:
            total += peak * (x / rise) ** 1.1
        elif rise <= x < rise + drain:
            f = (x - rise) / drain
            total += peak * (1 - f) ** 1.3
    return total


def depth(t, s):
    normal = 24 + 16 * math.sin(2 * math.pi * t / 97 + simlib.unit(s, "q-c") * 6.3)
    normal += 9 * math.sin(2 * math.pi * t / 41 + simlib.unit(s, "q-d") * 6.3)
    normal += 5 * simlib.smooth_noise(t, 5, s, "q-e")
    return simlib.clamp(max(0.0, normal) + spike(t, s), 0, MAX_DEPTH)


def rates(t, s):
    """(in, out): out = arrivals - growth, clamped at 0 (then in = growth)."""
    slope = depth(t, s) - depth(t - 1, s)
    rate_in = arrivals(t, s)
    rate_out = rate_in - slope
    if rate_out < 0:
        rate_out, rate_in = 0.0, slope
    return rate_in, rate_out


def oldest_age(t, s, d):
    """Seconds back until the arrivals add up to the current depth (at most 600 s)."""
    if d < 1:
        return 0.0
    total, back = 0.0, 0
    while total < d and back < 600:
        rate_in, _ = rates(t - back, s)
        if total + rate_in >= d:
            return back + (d - total) / rate_in
        total += rate_in
        back += 1
    return float(back)


def simulate(t):
    s = simlib.seed()
    d = depth(t, s)
    rate_in, rate_out = rates(t, s)
    age = oldest_age(t, s, d)
    level = "fail" if age > FAIL_AGE else "warn" if age > WARN_AGE else "ok"
    return {"depth": int(round(d)), "oldestAgeS": round(age, 1), "rateIn": round(rate_in, 2),
            "rateOut": round(rate_out, 2), "level": level}


if __name__ == "__main__":
    simlib.main(simulate, __doc__.splitlines()[0])
