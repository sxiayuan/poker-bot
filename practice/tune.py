#!/usr/bin/env python3
"""Tunes ExploitBot's numbers by trying one change at a time and keeping what scores higher.

usage: python3 tune.py [--tables 1500] [--rounds 2] [--jobs 7]

Training uses the original opponents only. Bluffer, trapper, lag and defender are held out,
so check.py can test whether the tuned bot still wins against styles it never trained on.
Every candidate plays the same deals (table t is always seeded with t), which makes small
differences between candidates far less noisy than separate runs would be.
"""
import argparse
import json
import os
import random
from multiprocessing import Pool

import exploit_bot
from run import load
from sim import SEATS, Table, VARIANTS

TRAIN_FIELDS = [
    "station,maniac,random,nit,tag,shover,potodds",
    "nit,tag,shover,sample_bot.py:SampleBot",
    "tag,potodds,shover,station,sample_bot.py:SampleBot",
]
STEPS = {
    "push_depth": 3, "push_base": 0.04, "push_per_player": 0.01, "push_fe": 0.1,
    "open_raise": 0.04, "call_base": 0.04, "call_odds": 0.15, "post_value": 0.05,
    "post_call": 0.04, "thin_value": 0.08, "pressure": 0.08, "steal_fe": 0.1,
    "steal_min": 0.1, "bluff_fe": 0.15, "raiser_adjust": 0.3, "value_size": 0.2,
    "station_size": 0.2,
}


def bot_class(params):
    return type("Tuned", (exploit_bot.ExploitBot,), {"P": dict(exploit_bot.ExploitBot.P, **params)})


def play(job):
    """Total points for the bot over tables [start, start + count) against one field.
    `params` is a dict of ExploitBot numbers, or a file:Class spec for any other bot."""
    params, field, start, count, variant, limit = job
    bot = load(params) if isinstance(params, str) else bot_class(params)
    names = field.split(",")
    classes = {n: load(n) for n in names}
    total = 0.0
    for t in range(start, start + count):
        rng = random.Random(t)
        random.seed(t + 7)
        lineup = [rng.choice(names) for _ in range(SEATS - 1)]
        seat = rng.randrange(SEATS)
        agents = [classes[n](i if i < seat else i + 1) for i, n in enumerate(lineup)]
        agents.insert(seat, bot(seat))
        total += Table(agents, VARIANTS[variant], rng, time_limit=limit).play()[seat]
    return total


def score(pool, params, fields, tables, first=0, variant="holdem", limit=None):
    """Average points per table for each field."""
    chunk = 100
    jobs = [(params, f, first + s, min(chunk, tables - s), variant, limit)
            for f in fields for s in range(0, tables, chunk)]
    totals = pool.map(play, jobs)
    per = len(range(0, tables, chunk))
    return [sum(totals[i * per:(i + 1) * per]) / tables for i in range(len(fields))]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tables", type=int, default=1500)
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--jobs", type=int, default=max((os.cpu_count() or 2) - 1, 1))
    ap.add_argument("--margin", type=float, default=0.3, help="points a change must gain to be kept")
    ap.add_argument("--out", default="tuned_params.json")
    args = ap.parse_args()

    params = {}
    with Pool(args.jobs) as pool:
        best = score(pool, params, TRAIN_FIELDS, args.tables)
        print(f"start: {sum(best) / len(best):.2f}  per field {[round(x, 1) for x in best]}", flush=True)
        for rnd in range(args.rounds):
            for name, step in STEPS.items():
                base = params.get(name, exploit_bot.ExploitBot.P[name])
                for value in (base + step, base - step):
                    trial = dict(params, **{name: round(value, 4)})
                    got = score(pool, trial, TRAIN_FIELDS, args.tables)
                    gain = sum(got) / len(got) - sum(best) / len(best)
                    kept = gain > args.margin
                    print(f"round {rnd + 1} {name}={value:.3f}: {sum(got) / len(got):.2f} ({gain:+.2f}){'  kept' if kept else ''}", flush=True)
                    if kept:
                        params, best = trial, got
                        break
        print(f"final: {sum(best) / len(best):.2f}  per field {[round(x, 1) for x in best]}")
        print(json.dumps(params, indent=1))
        with open(args.out, "w") as f:
            json.dump(params, f, indent=1)


if __name__ == "__main__":
    main()
