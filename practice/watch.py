#!/usr/bin/env python3
"""Plays one table and prints every hand as it happens.

usage: python3 watch.py exploit_bot.py:ExploitBot [--delay 0.4] [--hands 200]
                        [--variant holdem] [--field nit,tag,...] [--seed 1] [--show-all]

Your bot is marked with *. By default you see only your bot's cards and cards shown at
showdown; --show-all reveals everyone's cards. Press Ctrl+C to stop.
"""
import argparse
import random
import time

from run import load
from sim import SEATS, STREETS, Table, VARIANTS


class WatchedTable(Table):
    def __init__(self, agents, variant, rng, names, me, delay, hands, show_all):
        super().__init__(agents, variant, rng, time_limit=0.001)
        self.names, self.me, self.delay, self.hands, self.show_all = names, me, delay, hands, show_all

    def label(self, seat):
        return f"{'*' if seat == self.me else ' '}{seat} {self.names[seat]}"

    def play(self):
        try:
            return super().play()
        except StopIteration:
            return None

    def _play_hand(self, live):
        if self.hand_number >= self.hands:
            raise StopIteration
        sb, bb = self._blinds()
        print(f"\n=== Hand {self.hand_number + 1}   blinds {sb}/{bb}   {len(live)} players left")
        print("    " + "   ".join(f"{self.label(s).strip()}: {self.stacks[s]}" for s in live))
        self.street = None
        self.before = list(self.stacks)
        super()._play_hand(live)
        out = [s for s in live if self.stacks[s] == 0]
        for s in out:
            print(f"    !! {self.label(s).strip()} is eliminated")
        mine = self.stacks[self.me] - self.before[self.me]
        if self.me in live:
            print(f"    your bot: {self.stacks[self.me]} chips ({mine:+d})")
        time.sleep(self.delay)

    def _ask(self, seat, obs):
        if obs.street != self.street:
            self.street = obs.street
            board = " ".join(obs.board) if obs.board else ""
            print(f"  -- {obs.street} {board}   pot {obs.pot}")
        timeouts = self.stats[seat].timeouts
        kind, amount = super()._ask(seat, obs)
        if kind == "fold":
            what = "folds"
        elif kind == "raise":
            what = f"raises to {amount}" + (" (all-in)" if amount == obs.max_raise_to else "")
        elif obs.to_call == 0:
            what = "checks"
        else:
            what = f"calls {obs.to_call}"
        if self.stats[seat].timeouts > timeouts:
            what += "  [TIMED OUT]"
        cards = f" [{' '.join(obs.hole)}]" if seat == self.me or self.show_all else ""
        note = ""
        agent = self.agents[seat]
        if seat == self.me and hasattr(agent, "fold_equity") and obs.can_raise:
            note = f"   (thinks everyone folds to a raise {agent.fold_equity(obs):.0%})"
        print(f"    {self.label(seat):<22}{cards:<14} {what}{note}")
        time.sleep(self.delay)
        return kind, amount

    def _award(self, holes, board, folded, contrib):
        before = list(self.stacks)
        in_hand = [s for s in holes if not folded[s]]
        super()._award(holes, board, folded, contrib)
        if len(in_hand) > 1:
            print(f"  -- showdown {' '.join(board)}")
            for s in in_hand:
                print(f"    {self.label(s):<22} [{' '.join(holes[s])}]")
        for s in holes:
            won = self.stacks[s] - before[s]
            if won > 0:
                print(f"    {self.label(s).strip()} wins {won}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bot", help="file.py:ClassName")
    ap.add_argument("--variant", default="holdem", choices=sorted(VARIANTS))
    ap.add_argument("--field", default="station,maniac,random,nit,tag,shover,potodds,lag,bluffer,trapper,defender")
    ap.add_argument("--delay", type=float, default=0.4, help="seconds between actions; 0 prints instantly")
    ap.add_argument("--hands", type=int, default=200)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--show-all", action="store_true", help="show every player's cards")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    random.seed(args.seed)
    field = args.field.split(",")
    me = rng.randrange(SEATS)
    names = [rng.choice(field) for _ in range(SEATS)]
    names[me] = args.bot.split(":")[-1]
    agents = [(load(args.bot) if i == me else load(names[i]))(i) for i in range(SEATS)]
    table = WatchedTable(agents, VARIANTS[args.variant], rng, names, me, args.delay, args.hands, args.show_all)
    try:
        result = table.play()
    except KeyboardInterrupt:
        print("\nstopped")
        return
    if result is None:
        print(f"\nStopped after {args.hands} hands. Your bot has {table.stacks[me]} chips.")
        return
    print("\n=== Final points")
    for s in sorted(range(SEATS), key=lambda s: -result[s]):
        print(f"    {table.label(s):<22} {result[s]:.1f}")


if __name__ == "__main__":
    main()
