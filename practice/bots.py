"""Opponent styles for the practice field. They approximate what quick builds might do;
nobody knows the real field until the first tournament runs."""
import random

from sim import Agent, CALL, FOLD, RANKS, raise_to, rank_hand


def preflop_strength(obs):
    """Rough 0-1 score from high cards, pairs and suitedness; works for any hole-card count."""
    ranks = sorted((RANKS.index(c[0]) for c in obs.hole), reverse=True)
    suits = [c[1] for c in obs.hole]
    score = (ranks[0] + ranks[1]) / 24 * 0.55
    if len(set(ranks)) < len(ranks):
        score += 0.25 + ranks[0] / 12 * 0.2
    if len(set(suits)) < len(suits):
        score += 0.07
    if ranks[0] - ranks[1] == 1:
        score += 0.05
    return min(score, 1.0)


def postflop_strength(obs):
    """Rough 0-1 score: how much the hole cards improve on what the board alone shows."""
    mine = obs.variant.best(obs.hole, obs.board)
    if len(obs.board) >= 5:
        table = rank_hand(list(obs.board[:5]), obs.variant.short_deck)
    else:
        table = (0,)
    gain = max(mine[0] - table[0], 0) if len(obs.board) >= 5 else mine[0]
    top = RANKS.index(max(obs.board, key=lambda c: RANKS.index(c[0]))[0])
    score = 0.15 + min(gain, 5) * 0.16 + mine[0] * 0.03
    if mine[0] == 1 and mine[1] >= top:
        score += 0.2  # top pair or an overpair
    return min(score, 1.0)


def strength(obs):
    return preflop_strength(obs) if obs.street == "preflop" else postflop_strength(obs)


def bet(obs, fraction):
    """Raise-to total for a pot-fraction bet, kept legal."""
    target = obs.bets[obs.seat] + obs.to_call + int(fraction * (obs.pot + obs.to_call))
    return raise_to(max(obs.min_raise_to, min(target, obs.max_raise_to)))


class CallingStation(Agent):
    def act(self, obs):
        if obs.street == "preflop" and strength(obs) < 0.2 and obs.to_call > obs.big_blind:
            return FOLD
        return CALL


class Maniac(Agent):
    def act(self, obs):
        if obs.can_raise and random.random() < 0.6:
            return bet(obs, 1.0)
        return CALL


class RandomBot(Agent):
    def act(self, obs):
        u = random.random()
        if u < 0.3:
            return FOLD
        if u < 0.8 or not obs.can_raise:
            return CALL
        return bet(obs, 0.5 + random.random())


class Nit(Agent):
    def act(self, obs):
        s = strength(obs)
        if s > 0.72 and obs.can_raise:
            return bet(obs, 0.75)
        if s > 0.6:
            return CALL
        return CALL if obs.to_call == 0 else FOLD


class TightAggressive(Agent):
    def act(self, obs):
        s = strength(obs)
        odds = obs.to_call / max(obs.pot + obs.to_call, 1)
        if s > 0.62 and obs.can_raise:
            return bet(obs, 0.7)
        if s > 0.4 + odds * 0.5:
            return CALL
        if obs.to_call == 0 and obs.can_raise and random.random() < 0.12:
            return bet(obs, 0.5)
        return CALL if obs.to_call == 0 else FOLD


class AllInOrFold(Agent):
    """A common shortcut: shove good hands, fold the rest."""

    def act(self, obs):
        if strength(obs) > 0.55 and obs.can_raise:
            return raise_to(obs.max_raise_to)
        if strength(obs) > 0.55:
            return CALL
        return CALL if obs.to_call == 0 else FOLD


class PotOdds(Agent):
    """The classic 'calculate the chances': acts on its score against the pot odds."""

    def act(self, obs):
        s = strength(obs)
        odds = obs.to_call / max(obs.pot + obs.to_call, 1)
        if s > 0.8 and obs.can_raise:
            return bet(obs, 1.0)
        return CALL if s >= odds or obs.to_call == 0 else FOLD


class LooseAggressive(Agent):
    """Plays many hands and bets them hard; folds weak hands to big raises."""

    def act(self, obs):
        s = strength(obs)
        risk = obs.to_call / max(obs.stacks[obs.seat] + obs.bets[obs.seat], 1)
        if s > 0.5 and obs.can_raise and random.random() < 0.7:
            return bet(obs, 0.9)
        if obs.to_call == 0 and obs.can_raise and random.random() < 0.35:
            return bet(obs, 0.6)
        if s > 0.3 + risk * 0.5:
            return CALL
        return CALL if obs.to_call == 0 else FOLD


class Bluffer(Agent):
    """Tight-aggressive, but bets whenever checked to and often bluff-raises."""

    def act(self, obs):
        s = strength(obs)
        odds = obs.to_call / max(obs.pot + obs.to_call, 1)
        if s > 0.62 and obs.can_raise:
            return bet(obs, 0.75)
        if obs.can_raise and obs.street != "preflop" and (obs.to_call == 0 or random.random() < 0.15):
            return bet(obs, 0.7)
        if s > 0.42 + odds * 0.5:
            return CALL
        return CALL if obs.to_call == 0 else FOLD


class Trapper(Agent):
    """Slow-plays strong hands by calling, then raises big on the river; folds weak hands."""

    def act(self, obs):
        s = strength(obs)
        odds = obs.to_call / max(obs.pot + obs.to_call, 1)
        if s > 0.7:
            if obs.street == "river" and obs.can_raise:
                return bet(obs, 1.0)
            return CALL
        if s > 0.5 + odds * 0.5:
            return CALL
        return CALL if obs.to_call == 0 else FOLD


class Defender(Agent):
    """Tight-aggressive, but tracks who raises a lot before the flop and re-raises them light."""

    def __init__(self, seat):
        super().__init__(seat)
        self.spots = {}
        self.raises = {}

    def on_action(self, seat, street, action, amount):
        if street == "preflop":
            self.spots[seat] = self.spots.get(seat, 0) + 1
            self.raises[seat] = self.raises.get(seat, 0) + (action == "raise")

    def act(self, obs):
        s = strength(obs)
        odds = obs.to_call / max(obs.pot + obs.to_call, 1)
        raiser = next((seat for seat, _, a, _ in reversed(obs.history) if a == "raise"), None)
        if obs.street == "preflop" and raiser is not None and raiser != obs.seat:
            loose = (self.raises.get(raiser, 0) + 1) / (self.spots.get(raiser, 0) + 6) > 0.3
            if loose and s > 0.42 and obs.can_raise:
                return bet(obs, 1.2)
            if loose and s > 0.36:
                return CALL
        if s > 0.62 and obs.can_raise:
            return bet(obs, 0.7)
        if s > 0.4 + odds * 0.5:
            return CALL
        return CALL if obs.to_call == 0 else FOLD


FIELD = {
    "station": CallingStation,
    "maniac": Maniac,
    "random": RandomBot,
    "nit": Nit,
    "tag": TightAggressive,
    "shover": AllInOrFold,
    "potodds": PotOdds,
    "lag": LooseAggressive,
    "bluffer": Bluffer,
    "trapper": Trapper,
    "defender": Defender,
}
