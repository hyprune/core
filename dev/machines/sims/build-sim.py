#!/usr/bin/env python3
"""Build server simulator: one build at a time.

`status` cycles `idle` -> `running` (8-20 s) -> `success` or `failed` (about 1 in 5),
showing the result for 4-8 s before going idle for 6-25 s. Now and then a burst of
3-6 quick builds (3-6 s each) passes in a row. `successes`/`failures` count results,
`streak` is the run of consecutive successes, `lastJob` names the current or latest
build. Prints one JSON object (see README.md for the keys).
"""
import simlib

BLOCK_S, PER_CYCLE, GROUPS = 600, 6, 4
FAIL_CHANCE, BURST_CHANCE = 0.2, 0.12
TITLE_BASE, TITLE_WRAP = 100, 100000


class Template:
    def __init__(self, builds):
        self.builds = builds
        ok = sum(1 for b in builds if b["ok"])
        self.totals = {"started": len(builds), "successes": ok, "failures": len(builds) - ok}
        trailing = 0
        for b in reversed(builds):
            if not b["ok"]:
                break
            trailing += 1
        self.trailing_ok, self.all_ok = trailing, trailing == len(builds)


def build(r, index):
    builds, t = [], r.uniform(2, 10)
    while True:
        if r.random() < BURST_CHANCE:
            batch = [(r.uniform(3, 6), True, r.uniform(2, 3), r.uniform(1, 3))
                     for _ in range(r.randint(3, 6))]
        else:
            batch = [(r.uniform(8, 20), r.random() >= FAIL_CHANCE, r.uniform(4, 8), r.uniform(6, 25))]
        added = []
        for run, ok, hold, idle in batch:
            added.append({"start": t, "end": t + run, "ok": ok, "hold": t + run + hold, "quick": len(batch) > 1})
            t += run + hold + idle
        if added[-1]["hold"] > BLOCK_S - 1:
            break  # the server is idle when the block ends
        builds.extend(added)
    return Template(builds)


_blocks = None


def blocks():
    global _blocks
    if _blocks is None:
        _blocks = simlib.Blocks("build", BLOCK_S, PER_CYCLE, GROUPS, build)
    return _blocks


def simulate(t):
    b = blocks()
    block, x = b.locate(t)
    values = {"status": "idle", "progress": 0.0, "successes": 0, "failures": 0, "streak": 0, "lastJob": ""}
    if block < 0:
        return values
    template = b.template(block)
    prefix = b.prefix(block)
    started = [x_ for x_ in template.builds if x_["start"] <= x]
    done = [x_ for x_ in started if x_["end"] <= x]
    ok = sum(1 for x_ in done if x_["ok"])
    values["successes"] = prefix["successes"] + ok
    values["failures"] = prefix["failures"] + len(done) - ok
    streak = 0
    for x_ in reversed(done):
        if not x_["ok"]:
            break
        streak += 1
    if streak == len(done):  # no failure in this block yet: the run continues from earlier blocks
        back = block - 1
        while back >= 0 and block - back <= 200:
            previous = b.template(back)
            streak += previous.trailing_ok
            if not previous.all_ok:
                break
            back -= 1
    values["streak"] = streak
    number = prefix["started"] + len(started)
    if number:
        values["lastJob"] = f"web #{TITLE_BASE + number % TITLE_WRAP}"
    if started:
        current = started[-1]
        if x < current["end"]:
            values["status"] = "running"
            values["progress"] = round((x - current["start"]) / (current["end"] - current["start"]), 3)
        elif x < current["hold"]:
            values["status"] = "success" if current["ok"] else "failed"
            values["progress"] = 1.0
    return values


if __name__ == "__main__":
    simlib.main(simulate, __doc__.splitlines()[0])
