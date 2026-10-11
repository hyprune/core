#!/usr/bin/env python3
"""Deployment simulator: a release train to one environment.

A deploy cycle starts every 60-90 s: `queued` for 5-10 s, `building` (progress 0-0.4),
`rolling` (0.4-1.0), then `live` for 15-25 s. About 15% of cycles fail while rolling:
`failed` holds for 8 s with the progress where it stopped, then the release waits
`queued` for the next cycle (a retry of the same version). `version` is the version in
flight, or the live one; it advances with each successful deploy, counted by `deploys`.
Prints one JSON object (see README.md for the keys).
"""
import bisect
import os

import simlib

BLOCK_S, CYCLES, PER_CYCLE, GROUPS = 600, 8, 6, 4
FAIL_CHANCE, FAIL_HOLD = 0.15, 8.0


class Template:
    def __init__(self, cycles):
        self.cycles = cycles
        self.starts = [c["start"] for c in cycles]
        self.totals = {"deploys": sum(1 for c in cycles if c["fail"] is None)}


def build(r, index):
    raw = [r.uniform(60, 90) for _ in range(CYCLES)]
    scale = BLOCK_S / sum(raw)
    cycles, start = [], 0.0
    for length in (x * scale for x in raw):
        queued, live = r.uniform(5, 10), r.uniform(15, 25)
        work = max(20.0, length - queued - live)
        building = work * r.uniform(0.35, 0.45)
        fail = work - building
        fail = (fail * r.uniform(0.25, 0.85)) if r.random() < FAIL_CHANCE else None
        cycles.append({"start": start, "length": length, "queued": queued, "building": building,
                       "rolling": work - building, "fail": fail})
        start += length
    return Template(cycles)


_blocks = None


def blocks():
    global _blocks
    if _blocks is None:
        _blocks = simlib.Blocks("deploy", BLOCK_S, PER_CYCLE, GROUPS, build)
    return _blocks


def version(k):
    n = k % 9000
    return f"v{2 + n // 900}.{(n // 30) % 30}.{n % 30}"


def ease(f):
    return f * f * (3 - 2 * f)


def phase(cycle, x):
    """(phase, progress, succeeded) at x seconds into the cycle."""
    x -= cycle["start"]
    q, b, r = cycle["queued"], cycle["building"], cycle["rolling"]
    if x < q:
        return "queued", 0.0, False
    if x < q + b:
        return "building", 0.4 * ease((x - q) / b), False
    if cycle["fail"] is not None:
        stop = q + b + cycle["fail"]
        if x < stop:
            return "rolling", 0.4 + 0.6 * (x - q - b) / r, False
        if x < stop + FAIL_HOLD:
            return "failed", 0.4 + 0.6 * cycle["fail"] / r, False
        return "queued", 0.0, False
    if x < q + b + r:
        return "rolling", 0.4 + 0.6 * (x - q - b) / r, False
    return "live", 1.0, True


def simulate(t):
    b = blocks()
    block, x = b.locate(t)
    environment = os.environ.get("MACHINES_SIM_ENVIRONMENT", "production")
    if block < 0:
        return {"phase": "queued", "progress": 0.0, "version": version(1), "environment": environment,
                "deploys": 0}
    template = b.template(block)
    i = bisect.bisect_right(template.starts, x) - 1
    deploys = b.prefix(block)["deploys"] + sum(1 for c in template.cycles[:i] if c["fail"] is None)
    name, progress, succeeded = phase(template.cycles[i], x)
    deploys += succeeded
    return {"phase": name, "progress": round(progress, 3),
            "version": version(deploys if succeeded else deploys + 1),
            "environment": environment, "deploys": deploys}


if __name__ == "__main__":
    simlib.main(simulate, __doc__.splitlines()[0])
