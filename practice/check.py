#!/usr/bin/env python3
"""Checks the tuned bot on deals and opponents it was never tuned on.

usage: python3 check.py [--tables 2000] [--params tuned_params.json]

Uses table seeds from one million up, which tune.py never plays, and compares the tuned
bot, the untuned ExploitBot and the sample bot on the same deals. An average bot scores
31.9 points per table.
"""
import argparse
import json
import os
from multiprocessing import Pool

import tune

HELD_OUT = {
    "held-out styles": "bluffer,trapper,lag,defender",
    "held-out + sample": "bluffer,trapper,lag,defender,tag,sample_bot.py:SampleBot",
    "everyone": "station,maniac,random,nit,tag,shover,potodds,lag,bluffer,trapper,defender,sample_bot.py:SampleBot",
    "copies of itself": "exploit_bot.py:ExploitBot",
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tables", type=int, default=2000)
    ap.add_argument("--params", default="tuned_params.json")
    ap.add_argument("--jobs", type=int, default=max((os.cpu_count() or 2) - 1, 1))
    args = ap.parse_args()
    with open(args.params) as f:
        tuned = json.load(f)

    first = 10 ** 6
    fields = tune.TRAIN_FIELDS + list(HELD_OUT.values())
    labels = ["train: mixed", "train: tough", "train: middle"] + list(HELD_OUT)
    with Pool(args.jobs) as pool:
        rows = {
            "tuned": tune.score(pool, tuned, fields, args.tables, first),
            "untuned": tune.score(pool, {}, fields, args.tables, first),
            "sample": tune.score(pool, "sample_bot.py:SampleBot", fields, args.tables, first),
        }
        print(f"{'field':<22}" + "".join(f"{k:>10}" for k in rows))
        for i, label in enumerate(labels):
            print(f"{label:<22}" + "".join(f"{rows[k][i]:>10.1f}" for k in rows))
        print("\nOther variants (field: everyone)")
        for variant in ("omaha", "shortdeck", "threecard"):
            got = tune.score(pool, tuned, [HELD_OUT["everyone"]], args.tables // 2, first, variant)
            print(f"  {variant:<10} tuned {got[0]:.1f}")


if __name__ == "__main__":
    main()
