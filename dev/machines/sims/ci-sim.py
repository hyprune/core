#!/usr/bin/env python3
"""CI pipeline simulator: a four-stage pipeline (build, test, package, deploy).

Jobs arrive every 5-9 s in busy spells, wait while the build stage is busy, and move
through the stages one job per stage at a time; a job that finished a stage waits
there while the next stage is busy. About one job in ten fails at some stage (the
stage shows `failed` for 3 s); a passed job shows `success` on the deploy stage for
2 s. The overall `status` is
`failed` while a stage shows a failure, `running` while any stage runs, and otherwise the
last outcome. Prints one JSON object (see README.md for the keys).
"""
import simlib

STAGES = [(4, 7), (5, 9), (2, 4), (3, 6)]  # build, test, package, deploy (seconds)
FAIL_WEIGHTS = [0.25, 0.45, 0.1, 0.2]
FAIL_HOLD, OK_HOLD, MAX_QUEUED = 3.0, 2.0, 12
BLOCK_S, PER_CYCLE, GROUPS = 240, 15, 4
TITLE_BASE, TITLE_WRAP = 1000, 100000


class Job:
    __slots__ = ("arrival", "dur", "fail", "start", "done", "leave", "finish", "end")


class Template:
    def __init__(self, jobs):
        self.jobs = jobs
        ok = sum(1 for j in jobs if j.fail is None)
        self.totals = {"started": len(jobs), "succeeded": ok, "failed": len(jobs) - ok}
        last = max(jobs, key=lambda j: j.finish)
        self.last_result = "success" if last.fail is None else "failed"


def build(r, index):
    jobs, free = [], [0.0] * 4
    t = r.uniform(0, 12)
    end = r.uniform(50, 90) if r.random() < 0.15 else r.uniform(150, 195)
    while t < end:
        job = Job()
        job.arrival = t
        job.dur = [r.uniform(*s) for s in STAGES]
        job.fail = None
        if r.random() < 0.10:
            job.fail = r.choices(range(4), FAIL_WEIGHTS)[0]
            job.dur[job.fail] *= r.uniform(0.4, 1.0)
        # Occasionally a push storm: the next job follows quickly and a queue builds.
        t += r.uniform(1.5, 4) if r.random() < 0.12 else r.uniform(5, 9)
        if sum(1 for j in jobs if j.start[0] > job.arrival) >= MAX_QUEUED:
            continue  # the queue is full: this push is dropped
        job.start, job.done, job.leave = [max(job.arrival, free[0])], [], []
        for k in range(4):
            d = job.start[k] + job.dur[k]
            job.done.append(d)
            if job.fail == k or k == 3:
                hold = FAIL_HOLD if job.fail == k else OK_HOLD
                job.leave.append(d + hold)
                free[k] = d + hold
                job.finish, job.end = d, d + hold
                break
            nxt = max(d, free[k + 1])
            job.leave.append(nxt)
            free[k] = nxt
            job.start.append(nxt)
        if job.end > BLOCK_S - 1:
            break  # the pipeline must be empty when the block ends
        jobs.append(job)
    return Template(jobs)


_blocks = None


def blocks():
    global _blocks
    if _blocks is None:
        _blocks = simlib.Blocks("ci", BLOCK_S, PER_CYCLE, GROUPS, build)
    return _blocks


def title(number):
    return f"main #{TITLE_BASE + number % TITLE_WRAP}"


def job_progress(job, x):
    if x >= job.finish:
        return 1.0 if job.fail is None else (job.fail + 1) / 4
    for k in range(len(job.start) - 1, -1, -1):
        if x >= job.start[k]:
            within = 1.0 if x >= job.done[k] else (x - job.start[k]) / job.dur[k]
            return (k + within) / 4
    return 0.0


def simulate(t):
    b = blocks()
    block, x = b.locate(t)
    values = {f"stage.{k + 1}": "pending" for k in range(4)}
    values.update(status="pending", jobsStarted=0, jobsSucceeded=0, jobsFailed=0, queued=0, active=0,
                  progress=0.0, lastJob="", lastResult="none")
    if block < 0:
        return values
    template = b.template(block)
    prefix = b.prefix(block)
    started = [j for j in template.jobs if j.arrival <= x]
    finished = [j for j in started if j.finish <= x]
    for k in range(4):
        for job in started:
            if k < len(job.start) and job.start[k] <= x < job.leave[k]:
                if x < job.done[k]:
                    values[f"stage.{k + 1}"] = "running"
                else:
                    values[f"stage.{k + 1}"] = "failed" if job.fail == k else "success"
    ok = sum(1 for j in finished if j.fail is None)
    values["jobsStarted"] = prefix["started"] + len(started)
    values["jobsSucceeded"] = prefix["succeeded"] + ok
    values["jobsFailed"] = prefix["failed"] + len(finished) - ok
    values["queued"] = sum(1 for j in started if x < j.start[0])
    values["active"] = sum(1 for j in started if x < j.end)
    if started:
        values["progress"] = round(job_progress(started[-1], x), 3)
    elif block > 0:
        values["progress"] = round(job_progress(b.template(block - 1).jobs[-1], float("inf")), 3)
    if values["jobsStarted"]:
        values["lastJob"] = title(values["jobsStarted"])
    if finished:
        last = max(finished, key=lambda j: j.finish)
        values["lastResult"] = "success" if last.fail is None else "failed"
    elif block > 0:
        values["lastResult"] = b.template(block - 1).last_result
    stages = [values[f"stage.{k + 1}"] for k in range(4)]
    if "failed" in stages:
        values["status"] = "failed"
    elif "running" in stages:
        values["status"] = "running"
    elif values["queued"]:
        values["status"] = "pending"
    elif "success" in stages:
        values["status"] = "success"
    elif values["lastResult"] != "none":
        values["status"] = values["lastResult"]  # idle: the last outcome stays up
    return values


if __name__ == "__main__":
    simlib.main(simulate, __doc__.splitlines()[0])
