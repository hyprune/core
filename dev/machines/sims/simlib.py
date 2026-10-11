"""Shared helpers for the machine simulators.

Every simulator is a pure function of a time (Unix seconds), a seed and an epoch, so
running one on an interval tells a continuous story and any moment can be reproduced:

- MACHINES_SIM_TIME   overrides the current time (Unix seconds, float)
- MACHINES_SIM_SEED   picks the story (default "hyprune")
- MACHINES_SIM_EPOCH  start of the story in Unix seconds (default 0); counters count
                      from here and are 0 before it

Simulators with counters cut time into fixed blocks. A block is played from a
template that starts and ends with an empty machine, so a block never depends on
the one before it. Templates come in groups; each cycle of blocks plays one group's
templates in a seeded order. A counter at any time is then closed-form: whole groups
x group total + the cycles and blocks before it + the partial block.
"""
import argparse
import hashlib
import json
import math
import os
import random
import sys
import time

DEFAULT_SEED = "hyprune"


def seed():
    return os.environ.get("MACHINES_SIM_SEED", DEFAULT_SEED)


def epoch():
    return float(os.environ.get("MACHINES_SIM_EPOCH", "0"))


def now():
    value = os.environ.get("MACHINES_SIM_TIME")
    return float(value) if value is not None else time.time()


def rng(*parts):
    """A deterministic random.Random for a tuple of parts (string seeding is stable)."""
    return random.Random(":".join(str(p) for p in parts))


def unit(*parts):
    """A deterministic float in [0, 1) for a tuple of parts."""
    digest = hashlib.blake2b(":".join(str(p) for p in parts).encode(), digest_size=8).digest()
    return int.from_bytes(digest, "big") / 2**64


def smooth_noise(t, period, *parts):
    """Value noise in [-1, 1]: seeded points every `period` s, cosine-interpolated."""
    k = math.floor(t / period)
    f = t / period - k
    a = unit(*parts, k) * 2 - 1
    b = unit(*parts, k + 1) * 2 - 1
    w = (1 - math.cos(math.pi * f)) / 2
    return a + (b - a) * w


def clamp(x, lo, hi):
    return lo if x < lo else hi if x > hi else x


class Blocks:
    """Closed-form block schedule over seeded templates.

    build(r, index) -> template object with:
      .totals   dict of counter totals for the whole block
      .events   any data the simulator needs
    """

    def __init__(self, name, block_s, per_cycle, groups, build):
        self.name, self.block_s, self.per_cycle, self.groups = name, block_s, per_cycle, groups
        s = seed()
        self.templates = [[build(rng(name, s, "template", g, i), i) for i in range(per_cycle)]
                          for g in range(groups)]
        self.group_totals = [self._sum(t.totals for t in group) for group in self.templates]
        self.super_total = self._sum(self.group_totals)
        self._orders = {}

    @staticmethod
    def _sum(items):
        total = {}
        for item in items:
            for k, v in item.items():
                total[k] = total.get(k, 0) + v
        return total

    def order(self, cycle):
        if cycle not in self._orders:
            order = list(range(self.per_cycle))
            rng(self.name, seed(), "order", cycle).shuffle(order)
            self._orders[cycle] = order
        return self._orders[cycle]

    def template(self, block):
        cycle, position = divmod(block, self.per_cycle)
        return self.templates[cycle % self.groups][self.order(cycle)[position]]

    def locate(self, t):
        """(block index, seconds into the block); block < 0 before the epoch."""
        offset = t - epoch()
        block = math.floor(offset / self.block_s)
        return block, offset - block * self.block_s

    def prefix(self, block):
        """Counter totals of all blocks before `block` (block >= 0)."""
        cycle, position = divmod(block, self.per_cycle)
        supers, group = divmod(cycle, self.groups)
        total = {k: v * supers for k, v in self.super_total.items()}
        for g in range(group):
            for k, v in self.group_totals[g].items():
                total[k] += v
        order = self.order(cycle)
        templates = self.templates[cycle % self.groups]
        for i in range(position):
            for k, v in templates[order[i]].totals.items():
                total[k] += v
        return total


def main(simulate, description):
    """Command line shared by every simulator."""
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--at", type=float, metavar="SECONDS",
                        help="print the state at this Unix time instead of now")
    parser.add_argument("--timeline", nargs=3, type=float, metavar=("FROM", "TO", "STEP"),
                        help="print one JSON line per STEP seconds from FROM to TO (inclusive)")
    parser.add_argument("--with-time", action="store_true",
                        help="add the simulated time as \"_t\" to each line (timeline only)")
    args = parser.parse_args()
    if args.timeline:
        start, end, step = args.timeline
        if step <= 0:
            parser.error("STEP must be positive")
        out = sys.stdout
        count = int(math.floor((end - start) / step + 1e-9)) + 1
        for i in range(max(0, count)):
            t = start + i * step
            values = simulate(t)
            if args.with_time:
                values = {"_t": round(t, 3), **values}
            out.write(json.dumps(values, separators=(",", ":")) + "\n")
        return
    t = args.at if args.at is not None else now()
    print(json.dumps(simulate(t), separators=(",", ":")))
