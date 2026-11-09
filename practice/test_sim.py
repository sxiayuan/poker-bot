#!/usr/bin/env python3
"""Checks the simulator: hand rankings, ICM, side pots, betting rules, and scoring."""
import itertools
import random
import time

from bots import FIELD
from sim import (Agent, CALL, FOLD, POINTS, RANKS, SUITS, START_STACK, Table, VARIANTS, icm, rank_hand, raise_to)

HOLDEM = VARIANTS["holdem"]


class Scripted(Agent):
    """Plays a fixed list of actions, then checks or folds."""

    def __init__(self, seat, script=()):
        super().__init__(seat)
        self.script = list(script)
        self.seen = []

    def act(self, obs):
        self.seen.append(obs)
        return self.script.pop(0) if self.script else (CALL if obs.to_call == 0 else FOLD)


class FixedDeck(random.Random):
    """Deals a chosen deck: the cards listed are popped first, so they are dealt in order."""

    def __init__(self, cards):
        super().__init__(0)
        self.cards = cards

    def shuffle(self, deck):
        rest = [c for c in deck if c not in self.cards]
        deck[:] = rest + list(reversed(self.cards))

    def randrange(self, n):
        return n - 1  # the button moves to seat 0 on the first hand


def table(n, stacks, scripts, deck):
    agents = [Scripted(i, scripts[i] if i < len(scripts) else ()) for i in range(n)]
    t = Table(agents, HOLDEM, FixedDeck(deck), time_limit=None, points=POINTS[:n])
    t.stacks = list(stacks)
    return t, agents


def test_five_card_counts():
    expected = {0: 1302540, 1: 1098240, 2: 123552, 3: 54912, 4: 10200, 5: 5108, 6: 3744, 7: 624, 8: 40}
    deck = [r + s for r in RANKS for s in SUITS]
    counts = {i: 0 for i in range(9)}
    for hand in itertools.combinations(deck, 5):
        counts[rank_hand(hand)[0]] += 1
    assert counts == expected, counts


def test_best_of_seven_matches_brute_force():
    rng = random.Random(7)
    deck = [r + s for r in RANKS for s in SUITS]
    short = [c for c in deck if c[0] in RANKS[4:]]
    for _ in range(4000):
        hand = rng.sample(deck, 7)
        assert rank_hand(hand) == max(rank_hand(c) for c in itertools.combinations(hand, 5))
        hand = rng.sample(short, 7)
        assert rank_hand(hand, True) == max(rank_hand(c, True) for c in itertools.combinations(hand, 5))


def test_rank_order():
    assert rank_hand("Ah Kh Qh Jh Th".split()) > rank_hand("Kh Qh Jh Th 9h".split())
    assert rank_hand("5s 4d 3c 2h Ad".split()) < rank_hand("6s 5d 4c 3h 2d".split())
    assert rank_hand("As Ad Kc Qs Jd".split()) > rank_hand("As Ad Qc Js 9d".split())
    # Short deck: a flush beats a full house, and A-6-7-8-9 is a straight
    assert rank_hand("Ah Th 9h 7h 6h".split(), True) > rank_hand("Ks Kd Kc Qs Qd".split(), True)
    assert rank_hand("Ah 6d 7c 8s 9d".split(), True)[0] == 4
    # Omaha must use exactly two hole cards: four spades in hand and one on board is no flush
    omaha = VARIANTS["omaha"]
    assert omaha.best("As Ks Qs Js".split(), "2s 7d 9c 3h 4d".split())[0] != 5
    assert omaha.best("As Ks Qd Jd".split(), "2s 7s 9s 3h 4d".split())[0] == 5


def test_icm():
    a = icm([50, 30, 20], [100])
    assert all(abs(x - y) < 1e-9 for x, y in zip(a, [50, 30, 20]))
    b = icm([50, 30, 20], [50, 30, 20])
    assert abs(b[0] - 38.393) < 0.01, b
    assert abs(sum(b) - 100) < 1e-9
    c = icm([10] * 8, POINTS)
    assert all(abs(x - sum(POINTS) / 8) < 1e-9 for x in c)


def test_side_pots_and_uncalled_bet():
    # Seat 0 is the button and short; seat 1 is medium; seat 2 is big and has the worst hand.
    # Cards are dealt to seats 0, 1, 2 in order, then to the board.
    deck = ["Qs", "Qd", "As", "Ad", "Ks", "Kd", "2c", "7d", "9h", "3s", "4c"]
    t, _ = table(3, [100, 300, 1000], [[raise_to(100)], [raise_to(300)], [CALL]], deck)
    t._play_hand([0, 1, 2])
    # Main pot 300 and side pot 400 both go to seat 1's aces.
    assert t.stacks == [0, 700, 700], t.stacks


def test_short_stack_wins_only_main_pot():
    deck = ["As", "Ad", "Qs", "Qd", "Ks", "Kd", "2c", "7d", "9h", "3s", "4c"]
    t, _ = table(3, [100, 300, 1000], [[raise_to(100)], [raise_to(300)], [CALL]], deck)
    t._play_hand([0, 1, 2])
    # Seat 0's aces win 300; seat 2's kings win the 400 side pot from seat 1's queens.
    assert t.stacks == [300, 0, 1100], t.stacks


def test_split_pot():
    deck = ["2s", "3d", "2d", "3s", "4h", "4d", "As", "Ks", "Qd", "Jc", "Th"]
    # The board makes a broadway straight for everyone.
    t, _ = table(3, [100, 100, 100], [[CALL], [CALL], [CALL]], deck)
    t._play_hand([0, 1, 2])
    assert t.stacks == [100, 100, 100], t.stacks


def test_short_all_in_does_not_reopen_betting():
    # Button raises to 100, SB goes all-in for 120 (not a full raise), BB folds.
    # The button may call but may not re-raise.
    deck = ["As", "Ad", "Qs", "Qd", "Ks", "Kd", "2c", "7d", "9h", "3s", "4c"]
    t, agents = table(3, [1000, 120, 1000], [[raise_to(100), CALL], [raise_to(120)], [FOLD]], deck)
    t._play_hand([0, 1, 2])
    second_look = agents[0].seen[1]
    assert second_look.to_call == 20 and not second_look.can_raise


def test_heads_up_order_and_big_blind_option():
    deck = ["As", "Ad", "Qs", "Qd", "2c", "7d", "9h", "3s", "4c"]
    t, agents = table(2, [1000, 1000], [[CALL], []], deck)
    t._play_hand([0, 1])
    # The button posts the small blind and acts first preflop.
    assert agents[0].seen[0].street == "preflop" and agents[0].seen[0].to_call == 10
    # The big blind gets an option, then acts first postflop.
    assert agents[1].seen[0].street == "preflop" and agents[1].seen[0].to_call == 0
    assert agents[1].seen[1].street == "flop"


def test_timeouts_errors_and_illegal_moves_fold_or_check():
    class Slow(Agent):
        def act(self, obs):
            time.sleep(0.003)
            return raise_to(obs.max_raise_to)

    class Crash(Agent):
        def act(self, obs):
            raise ValueError("boom")

    class Illegal(Agent):
        def act(self, obs):
            return raise_to(1)

    for cls, field in ((Slow, "timeouts"), (Crash, "errors"), (Illegal, "illegal")):
        agents = [cls(0), FIELD["station"](1)]
        t = Table(agents, HOLDEM, random.Random(3))
        t.play()
        assert getattr(t.stats[0], field) > 0, field
        assert t.stats[0].decisions > 0


def test_whole_tables_conserve_chips_and_points():
    names = list(FIELD)
    for variant in VARIANTS.values():
        rng = random.Random(11)
        random.seed(11)
        ended_by_limit = False
        for _ in range(30):
            agents = [FIELD[rng.choice(names)](i) for i in range(8)]
            t = Table(agents, variant, rng, time_limit=None)
            result = t.play()  # raises if chips are ever not conserved
            assert abs(sum(result) - sum(POINTS)) < 1e-6, (variant.name, result)
            assert sum(t.stacks) == START_STACK * 8
            ended_by_limit |= t.hand_number >= 200
        print(f"  {variant.name}: ok, a table reached the hand limit: {ended_by_limit}")


def test_hand_limit_uses_icm():
    class Folder(Agent):
        def act(self, obs):
            return FOLD

    agents = [Folder(0), Folder(1)]
    t = Table(agents, HOLDEM, random.Random(1), time_limit=None, points=[100, 0])
    result = t.play()
    assert abs(sum(result) - 100) < 1e-6
    assert t.hand_number == 200 or min(t.stacks) == 0


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            start = time.time()
            fn()
            print(f"PASS {name} ({time.time() - start:.1f}s)")
    print("all simulator tests passed")
