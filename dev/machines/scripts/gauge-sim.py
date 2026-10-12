#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Gauge simulator: a 0-100 value that steps to a new seeded level every few seconds.

The value is piecewise constant: step k (counted from MACHINES_SIM_EPOCH) lasts
`--period` seconds (default 6) and has its own seeded value. With `--steps N` the
sequence has N steps and then repeats, or, with `--hold`, keeps the last value for good
(so a test can check that a gauge settles and stops drawing new frames). Before the
epoch the first step shows. Deterministic like the other simulators (MACHINES_SIM_TIME,
_SHIFT, _SEED, _EPOCH; `--at`, `--timeline`). Prints one JSON object:
{"value", "display", "label", "level"} with level warn at 75 and fail at 90.
"""
import argparse
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "sims"))
import simlib  # noqa: E402

WARN, FAIL = 75, 90


def level(value):
    return "fail" if value >= FAIL else "warn" if value >= WARN else "ok"


def step(t, period, steps, hold):
    """The step index at time t."""
    k = max(0, math.floor((t - simlib.epoch()) / period))
    if steps:
        k = min(k, steps - 1) if hold else k % steps
    return k


def simulate(t, args):
    k = step(t, args.period, args.steps, args.hold)
    value = round(100 * simlib.unit(simlib.seed(), "gauge", args.label, k), 1)
    return {"value": value, "display": f"{math.floor(value + 0.5)}{args.unit}", "label": args.label, "level": level(value)}


def _positive(text):
    value = float(text)
    if not value > 0 or not math.isfinite(value):
        raise argparse.ArgumentTypeError("must be a positive number")
    return value


def _count(text):
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError("must be 0 or more")
    return value


def add_arguments(parser):
    parser.add_argument("--label", default="SIM", help="the label key (default SIM)")
    parser.add_argument("--unit", default="%", help="appended to the display text (default %%)")
    parser.add_argument("--period", type=_positive, default=6.0, help="seconds per step (default 6)")
    parser.add_argument("--steps", type=_count, default=0,
                        help="steps in the sequence (default 0: endless)")
    parser.add_argument("--hold", action="store_true",
                        help="after --steps steps keep the last value instead of repeating")


if __name__ == "__main__":
    simlib.main(simulate, __doc__.splitlines()[0], add_arguments)
