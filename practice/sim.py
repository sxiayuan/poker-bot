"""Practice simulator for 8-bot poker sit-and-gos with ICM scoring.

This is NOT the official starter kit. It is built from the published rules, and the
values marked PLACEHOLDER are guesses because the organizers announce them on event day.
"""
import itertools
import time
from dataclasses import dataclass

# PLACEHOLDER: the real point distribution is announced at the start of the build phase.
POINTS = [100, 60, 40, 25, 15, 10, 5, 0]
# PLACEHOLDER: the rules only say blinds start at 10/20 and rise every 25 hands.
BLINDS = [(10, 20), (15, 30), (25, 50), (40, 80), (60, 120), (100, 200), (150, 300), (250, 500)]
HANDS_PER_LEVEL = 25
MAX_HANDS = 200
START_STACK = 2000
SEATS = 8
TIME_LIMIT = 0.001  # seconds per decision

RANKS = "23456789TJQKA"
SUITS = "cdhs"
STREETS = ("preflop", "flop", "turn", "river")
HIGH, PAIR, TWO_PAIR, TRIPS, STRAIGHT, FLUSH, FULL_HOUSE, QUADS, STRAIGHT_FLUSH = range(9)
CATEGORY_NAMES = ["high card", "pair", "two pair", "trips", "straight", "flush", "full house", "quads", "straight flush"]


# ---------- hand ranking ----------
def _straight_high(mask, short_deck):
    """Highest straight in a 13-bit rank mask, or -1. Bit 0 is a deuce, bit 12 an ace."""
    for high in range(12, 3, -1):
        if (mask >> (high - 4)) & 0b11111 == 0b11111:
            return high
    if short_deck:
        if mask & 0b1000011110000 == 0b1000011110000:  # A-6-7-8-9
            return 7
    elif mask & 0b1000000001111 == 0b1000000001111:    # A-2-3-4-5
        return 3
    return -1


def rank_hand(cards, short_deck=False):
    """Ranks the best five of 5-7 cards ("As" style). A higher tuple is a better hand."""
    counts = [0] * 13
    suit_masks = {"c": 0, "d": 0, "h": 0, "s": 0}
    for card in cards:
        r = RANKS.index(card[0])
        counts[r] += 1
        suit_masks[card[1]] |= 1 << r
    flush_mask = next((m for m in suit_masks.values() if m.bit_count() >= 5), 0)
    flush_cat, full_cat = (FULL_HOUSE, FLUSH) if short_deck else (FLUSH, FULL_HOUSE)
    if flush_mask:
        high = _straight_high(flush_mask, short_deck)
        if high >= 0:
            return (STRAIGHT_FLUSH, high)
    by_count = sorted(((c, r) for r, c in enumerate(counts) if c), reverse=True)
    top_count, top_rank = by_count[0]
    if top_count == 4:
        return (QUADS, top_rank, max(r for c, r in by_count[1:]))
    has_full = top_count == 3 and len(by_count) > 1 and by_count[1][0] >= 2
    full = (full_cat, top_rank, by_count[1][1]) if has_full else None
    flush = None
    if flush_mask:
        flush = (flush_cat, *[r for r in range(12, -1, -1) if flush_mask >> r & 1][:5])
    if full and flush:
        return max(full, flush)
    if full or flush:
        return full or flush
    mask = sum(1 << r for r, c in enumerate(counts) if c)
    high = _straight_high(mask, short_deck)
    if high >= 0:
        return (STRAIGHT, high)
    singles = [r for c, r in by_count if c == 1]
    if top_count == 3:
        return (TRIPS, top_rank, *singles[:2])
    if top_count == 2:
        if by_count[1][0] == 2:
            kicker = max(r for c, r in by_count[2:])
            return (TWO_PAIR, top_rank, by_count[1][1], kicker)
        return (PAIR, top_rank, *singles[:3])
    return (HIGH, *singles[:5])


@dataclass(frozen=True)
class Variant:
    name: str
    hole_cards: int = 2
    must_use_two: bool = False  # Omaha: exactly two hole and three board cards
    short_deck: bool = False

    def deck(self):
        ranks = RANKS[4:] if self.short_deck else RANKS
        return [r + s for r in ranks for s in SUITS]

    def best(self, hole, board):
        if self.must_use_two:
            return max(rank_hand(list(h) + list(b)) for h in itertools.combinations(hole, 2) for b in itertools.combinations(board, 3))
        return rank_hand(list(hole) + list(board), self.short_deck)


VARIANTS = {
    "holdem": Variant("holdem"),
    "omaha": Variant("omaha", hole_cards=4, must_use_two=True),
    "shortdeck": Variant("shortdeck", short_deck=True),
    "threecard": Variant("threecard", hole_cards=3),
}


# ---------- ICM ----------
def icm(stacks, prizes):
    """Independent Chip Model (Malmuth-Harville): each player's expected share of prizes."""
    n = len(stacks)
    prizes = list(prizes) + [0] * (n - len(prizes))
    memo = {}

    def value(mask, place):
        # Expected prize for each player in `mask`, with places from `place` down still open.
        if mask in memo:
            return memo[mask]
        members = [i for i in range(n) if mask >> i & 1]
        out = [0.0] * n
        total = sum(stacks[i] for i in members)
        if total > 0 and place < n:
            for i in members:
                p = stacks[i] / total
                out[i] += p * prizes[place]
                rest = value(mask & ~(1 << i), place + 1)
                for j in members:
                    if j != i:
                        out[j] += p * rest[j]
        memo[mask] = out
        return out

    return value((1 << n) - 1, 0)


# ---------- what a bot sees ----------
@dataclass
class Observation:
    seat: int
    hole: tuple
    board: tuple
    street: str
    variant: Variant
    pot: int             # all chips in the middle, including this street's bets
    to_call: int         # chips needed to call (already capped by your stack)
    min_raise_to: int    # smallest legal raise-to total for this street
    max_raise_to: int    # raise-to total that puts you all-in
    can_raise: bool
    stacks: tuple        # chips behind per seat; 0 for busted or all-in seats
    bets: tuple          # chips bet this street per seat
    folded: tuple        # True for seats out of this hand or the table
    button: int
    small_blind: int
    big_blind: int
    hand_number: int
    history: tuple       # (seat, street, "fold"|"check"|"call"|"raise", raise-to or 0) this hand
    points: tuple        # points for each finishing place


FOLD = ("fold", 0)
CALL = ("call", 0)   # also means check when nothing is owed


def raise_to(amount):
    return ("raise", int(amount))


class Agent:
    """Bots subclass this. A new instance is created per table, so state resets per table."""

    def __init__(self, seat):
        self.seat = seat

    def act(self, obs):
        return CALL

    def on_action(self, seat, street, action, amount):
        """Called for every action by any seat, including your own."""

    def on_showdown(self, shown, hand_number):
        """Called with {seat: hole cards} for hands that reached a showdown."""


@dataclass
class Stats:
    decisions: int = 0
    timeouts: int = 0
    errors: int = 0
    illegal: int = 0
    max_seconds: float = 0.0
    total_seconds: float = 0.0
    last_error: str = ""


# ---------- table ----------
class Table:
    def __init__(self, agents, variant, rng, time_limit=TIME_LIMIT, points=POINTS):
        self.agents = agents
        self.variant = variant
        self.rng = rng
        self.time_limit = time_limit
        self.points = list(points)
        self.n = len(agents)
        self.stacks = [START_STACK] * self.n
        self.button = rng.randrange(self.n)
        self.hand_number = 0
        self.stats = [Stats() for _ in agents]
        self.result = [0.0] * self.n
        self.place = [0.0] * self.n

    def _next_live(self, seat):
        for k in range(1, self.n + 1):
            s = (seat + k) % self.n
            if self.stacks[s] > 0:
                return s
        raise RuntimeError("no live seat")

    def play(self):
        """Plays the table to its end; returns each seat's points."""
        remaining = self.n
        while True:
            live = [s for s in range(self.n) if self.stacks[s] > 0]
            if len(live) == 1:
                self.result[live[0]] = self.points[0]
                self.place[live[0]] = 1
                return self.result
            if self.hand_number >= MAX_HANDS:
                shares = icm([self.stacks[s] for s in live], self.points[:len(live)])
                for s, share in zip(live, shares):
                    self.result[s] = share
                    self.place[s] = 0  # finished by ICM at the hand limit
                return self.result
            before = list(self.stacks)
            self._play_hand(live)
            total = sum(self.stacks)
            if total != START_STACK * self.n:
                raise RuntimeError(f"chips not conserved: {total}")
            busted = [s for s in live if self.stacks[s] == 0]
            # More chips at the start of the hand finishes higher; exact ties share places.
            for chips in sorted({before[s] for s in busted}):
                group = [s for s in busted if before[s] == chips]
                places = range(remaining - len(group) + 1, remaining + 1)
                share = sum(self.points[p - 1] for p in places) / len(group)
                for s in group:
                    self.result[s] = share
                    self.place[s] = sum(places) / len(group)
                remaining -= len(group)

    def _blinds(self):
        level = min(self.hand_number // HANDS_PER_LEVEL, len(BLINDS) - 1)
        return BLINDS[level]

    def _play_hand(self, live):
        n = self.n
        sb, bb = self._blinds()
        self.hand_number += 1
        self.button = self._next_live(self.button)
        if len(live) == 2:
            sb_seat = self.button
        else:
            sb_seat = self._next_live(self.button)
        bb_seat = self._next_live(sb_seat)
        deck = self.variant.deck()
        self.rng.shuffle(deck)
        holes = {}
        for s in live:
            holes[s] = tuple(deck.pop() for _ in range(self.variant.hole_cards))
        board = []
        folded = [s not in holes for s in range(n)]
        contrib = [0] * n
        bets = [0] * n
        history = []

        def post(seat, amount):
            amount = min(amount, self.stacks[seat])
            self.stacks[seat] -= amount
            bets[seat] += amount
            contrib[seat] += amount

        post(sb_seat, sb)
        post(bb_seat, bb)
        first = self._next_live(bb_seat)
        for i, street in enumerate(STREETS):
            if i > 0:
                board.extend(deck.pop() for _ in range(3 if i == 1 else 1))
                bets = [0] * n
                first = self._next_seat_in_hand(self.button, folded)
            self._betting(street, first, holes, board, folded, contrib, bets, history, sb, bb, post)
            if sum(not f for f in folded) == 1:
                break
        while len(board) < 5:
            board.append(deck.pop())
        self._award(holes, board, folded, contrib)

    def _next_seat_in_hand(self, seat, folded):
        for k in range(1, self.n + 1):
            s = (seat + k) % self.n
            if not folded[s]:
                return s
        return seat

    def _betting(self, street, first, holes, board, folded, contrib, bets, history, sb, bb, post):
        n = self.n
        current = max(bets)
        last_raise = bb
        order = [(first + k) % n for k in range(n)]
        order = [s for s in order if not folded[s]]
        needs = {s for s in order if self.stacks[s] > 0}
        acted_since_full = set()
        while True:
            able = [s for s in order if not folded[s] and self.stacks[s] > 0]
            if sum(not f for f in folded) <= 1:
                return
            if len(able) == 1 and bets[able[0]] >= current:
                return
            if not able or not needs:
                return
            seat = next((s for s in order if s in needs and not folded[s] and self.stacks[s] > 0), None)
            if seat is None:
                return
            # rotate the action so betting continues clockwise from this seat
            idx = order.index(seat)
            order = order[idx + 1:] + order[:idx + 1]
            to_call = min(current - bets[seat], self.stacks[seat])
            max_to = bets[seat] + self.stacks[seat]
            can_raise = max_to > current and seat not in acted_since_full
            min_to = min(current + last_raise, max_to)
            obs = Observation(
                seat=seat, hole=holes[seat], board=tuple(board), street=street, variant=self.variant,
                pot=sum(contrib), to_call=to_call, min_raise_to=min_to, max_raise_to=max_to, can_raise=can_raise,
                stacks=tuple(self.stacks), bets=tuple(bets), folded=tuple(folded), button=self.button,
                small_blind=sb, big_blind=bb, hand_number=self.hand_number, history=tuple(history),
                points=tuple(self.points))
            kind, amount = self._ask(seat, obs)
            needs.discard(seat)
            if kind == "fold":
                folded[seat] = True
                name, shown = "fold", 0
            elif kind == "raise":
                increase = amount - current
                if increase >= last_raise:  # a full raise reopens betting for everyone
                    last_raise = increase
                    acted_since_full = set()
                post(seat, amount - bets[seat])
                current = amount
                needs = {s for s in order if not folded[s] and s != seat and self.stacks[s] > 0}
                name, shown = "raise", amount
            else:
                name, shown = ("check" if to_call == 0 else "call"), 0
                post(seat, to_call)
            acted_since_full.add(seat)
            history.append((seat, street, name, shown))
            for agent in self.agents:
                self._notify(agent, seat, street, name, shown)

    def _notify(self, agent, seat, street, name, amount):
        try:
            agent.on_action(seat, street, name, amount)
        except Exception as err:
            self.stats[agent.seat].last_error = f"on_action: {err!r}"

    def _ask(self, seat, obs):
        """Runs one timed decision. Errors, timeouts and illegal moves fold or check."""
        fallback = CALL if obs.to_call == 0 else FOLD
        st = self.stats[seat]
        start = time.perf_counter()
        try:
            action = self.agents[seat].act(obs)
        except Exception as err:
            st.decisions += 1
            st.errors += 1
            st.last_error = repr(err)
            return fallback
        elapsed = time.perf_counter() - start
        st.decisions += 1
        st.total_seconds += elapsed
        st.max_seconds = max(st.max_seconds, elapsed)
        if self.time_limit and elapsed > self.time_limit:
            st.timeouts += 1
            return fallback
        try:
            kind, amount = action
        except (TypeError, ValueError):
            st.illegal += 1
            return fallback
        if kind == "fold":
            return fallback  # folding when checking is free just checks
        if kind == "call":
            return CALL
        if kind == "raise" and obs.can_raise and isinstance(amount, int) and obs.min_raise_to <= amount <= obs.max_raise_to:
            return ("raise", amount)
        st.illegal += 1
        return fallback

    def _award(self, holes, board, folded, contrib):
        in_hand = [s for s in holes if not folded[s]]
        levels = sorted({c for c in contrib if c > 0})
        pots = []  # (amount, eligible seats)
        prior = 0
        for level in levels:
            amount = sum(min(c, level) - min(c, prior) for c in contrib)
            eligible = [s for s in in_hand if contrib[s] >= level]
            if eligible:
                pots.append([amount, eligible])
            else:
                pots[-1][0] += amount  # dead money joins the pot below it
            prior = level
        if len(in_hand) == 1:
            self.stacks[in_hand[0]] += sum(contrib)
            return
        ranks = {s: self.variant.best(holes[s], board) for s in in_hand}
        for amount, eligible in pots:
            best = max(ranks[s] for s in eligible)
            winners = [s for s in eligible if ranks[s] == best]
            winners.sort(key=lambda s: (s - self.button - 1) % self.n)  # odd chips go left of the button
            share, odd = divmod(amount, len(winners))
            for i, s in enumerate(winners):
                self.stacks[s] += share + (1 if i < odd else 0)
        shown = {s: holes[s] for s in in_hand}
        for agent in self.agents:
            try:
                agent.on_showdown(shown, self.hand_number)
            except Exception as err:
                self.stats[agent.seat].last_error = f"on_showdown: {err!r}"
