#!/usr/bin/env python3
"""Scores a bot over many 8-seat sit-and-gos.

usage: python3 run.py my_bot.py:MyBot [--variant holdem] [--tables 500] [--jobs 4]
                      [--field station,maniac,nit,tag,shover,potodds,random]
A field name can also be a file:Class spec, so you can test against older versions.
"""
import argparse
import importlib.util
import math
import os
import random
import sys
from multiprocessing import Pool

from bots import FIELD
from sim import POINTS, SEATS, Stats, Table, VARIANTS


def load(spec):
    if spec in FIELD:
        return FIELD[spec]
    path, name = spec.rsplit(":", 1)
    module_spec = importlib.util.spec_from_file_location(os.path.basename(path)[:-3], path)
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return getattr(module, name)


def play_batch(job):
    bot_spec, field, variant, tables, seed, limit = job
    rng = random.Random(seed)
    random.seed(seed + 1)
    bot = load(bot_spec)
    classes = {name: load(name) for name in field}
    out = {"points": [], "places": [0] * (SEATS + 1), "hands": 0, "stats": Stats(), "field": {}}
    for _ in range(tables):
        names = [rng.choice(field) for _ in range(SEATS - 1)]
        seat = rng.randrange(SEATS)
        lineup = names[:seat] + [bot_spec] + names[seat:]
        agents = [(bot if i == seat else classes[lineup[i]])(i) for i in range(SEATS)]
        table = Table(agents, VARIANTS[variant], rng, time_limit=limit)
        result = table.play()
        out["points"].append(result[seat])
        place = table.place[seat]
        out["places"][int(place) if place == int(place) else 0] += 1
        out["hands"] += table.hand_number
        st = table.stats[seat]
        total = out["stats"]
        total.decisions += st.decisions
        total.timeouts += st.timeouts
        total.errors += st.errors
        total.illegal += st.illegal
        total.total_seconds += st.total_seconds
        total.max_seconds = max(total.max_seconds, st.max_seconds)
        total.last_error = st.last_error or total.last_error
        for i, name in enumerate(lineup):
            if i != seat:
                entry = out["field"].setdefault(name, [0.0, 0])
                entry[0] += result[i]
                entry[1] += 1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bot", help="file.py:ClassName, or a field name")
    ap.add_argument("--variant", default="holdem", choices=sorted(VARIANTS))
    ap.add_argument("--tables", type=int, default=500)
    ap.add_argument("--jobs", type=int, default=max((os.cpu_count() or 2) - 2, 1))
    ap.add_argument("--field", default="station,maniac,random,nit,tag,shover,potodds")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--no-timer", action="store_true", help="disable the 1 ms limit, to see a bot's strength without it")
    args = ap.parse_args()

    field = args.field.split(",")
    limit = None if args.no_timer else 0.001
    per_job = max(args.tables // args.jobs, 1)
    jobs = [(args.bot, field, args.variant, per_job, args.seed * 1000 + j, limit) for j in range(args.jobs)]
    with Pool(args.jobs) as pool:
        results = pool.map(play_batch, jobs)

    points = [x for r in results for x in r["points"]]
    n = len(points)
    mean = sum(points) / n
    se = math.sqrt(sum((x - mean) ** 2 for x in points) / n / n)
    fair = sum(POINTS) / SEATS
    print(f"{args.bot} | variant {args.variant} | {n} tables | {sum(r['hands'] for r in results) / n:.0f} hands per table")
    print(f"Points per table: {mean:.1f} +/- {1.96 * se:.1f} (95% CI). An average bot scores {fair:.1f}.")
    places = [sum(r["places"][p] for r in results) for p in range(SEATS + 1)]
    print("Finishes: " + "  ".join(f"{p}:{100 * places[p] / n:.0f}%" for p in range(1, SEATS + 1)) + f"  ICM at hand limit:{100 * places[0] / n:.0f}%")
    d = sum(r["stats"].decisions for r in results)
    t = sum(r["stats"].timeouts for r in results)
    e = sum(r["stats"].errors for r in results)
    il = sum(r["stats"].illegal for r in results)
    slowest = max(r["stats"].max_seconds for r in results)
    avg = sum(r["stats"].total_seconds for r in results) / max(d, 1)
    print(f"Decisions {d}: average {avg * 1e6:.0f} us, slowest {slowest * 1e6:.0f} us, timeouts {t} ({100 * t / max(d, 1):.2f}%), errors {e}, illegal moves {il}")
    for r in results:
        if r["stats"].last_error:
            print("Last error:", r["stats"].last_error)
            break
    rows = {}
    for r in results:
        for name, (pts, count) in r["field"].items():
            rows.setdefault(name, [0.0, 0])
            rows[name][0] += pts
            rows[name][1] += count
    print("Field average points: " + "  ".join(f"{k} {v[0] / v[1]:.1f}" for k, v in sorted(rows.items(), key=lambda kv: -kv[1][0] / kv[1][1])))


if __name__ == "__main__":
    sys.exit(main())
