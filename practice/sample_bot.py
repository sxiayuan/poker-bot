"""A reference bot showing tournament-specific ideas: short-stack push/fold, tightening
when busting is expensive, and per-seat opponent tracking. Practice against it and beat it."""
from bots import bet, strength
from sim import Agent, CALL, FOLD, raise_to


class SampleBot(Agent):
    def __init__(self, seat):
        super().__init__(seat)
        self.raises = {}
        self.actions = {}
        self.folds = {}

    def on_action(self, seat, street, action, amount):
        self.actions[seat] = self.actions.get(seat, 0) + 1
        if action == "raise":
            self.raises[seat] = self.raises.get(seat, 0) + 1
        elif action == "fold":
            self.folds[seat] = self.folds.get(seat, 0) + 1

    def _aggression(self, seat):
        """Share of a seat's actions that are raises; 0.2 until there is enough data."""
        seen = self.actions.get(seat, 0)
        return (self.raises.get(seat, 0) + 2) / (seen + 10)

    def _last_raiser(self, obs):
        for seat, _, action, _ in reversed(obs.history):
            if action == "raise":
                return seat
        return None

    def act(self, obs):
        s = strength(obs)
        my_chips = obs.stacks[self.seat] + obs.bets[self.seat]
        depth = my_chips / obs.big_blind
        players = sum(not f for f in obs.folded)
        alive = sum(c > 0 or b > 0 for c, b in zip(obs.stacks, obs.bets))
        odds = obs.to_call / max(obs.pot + obs.to_call, 1)

        # Trust raises less from seats that raise constantly.
        raiser = self._last_raiser(obs)
        discount = 0.0
        if raiser is not None and raiser != self.seat:
            discount = max(0.0, 0.25 - self._aggression(raiser)) * 0.5

        # Short stack: shove or fold, wider with fewer players left to act.
        if obs.street == "preflop" and depth <= 12:
            need = 0.42 + players * 0.015 + depth * 0.004
            if s > need and obs.can_raise:
                return raise_to(obs.max_raise_to)
            if s > need:
                return CALL
            return CALL if obs.to_call == 0 else FOLD

        # Bubble pressure: a call risking much of our stack needs a stronger hand.
        risk = obs.to_call / max(my_chips, 1)
        pressure = 0.12 * risk if alive > 2 else 0.0

        if obs.street == "preflop":
            if s > 0.62 and obs.can_raise and obs.to_call <= my_chips * 0.15:
                return bet(obs, 0.8)
            if s > 0.45 + odds * 0.4 + discount + pressure:
                return CALL
            return CALL if obs.to_call == 0 else FOLD

        if s > 0.6 and obs.can_raise and risk < 0.5:
            return bet(obs, 0.65)
        if s > odds + 0.12 + discount + pressure:
            return CALL
        return CALL if obs.to_call == 0 else FOLD
