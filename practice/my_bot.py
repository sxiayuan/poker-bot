"""Your rehearsal bot. Run it with: python3 run.py my_bot.py:MyBot --variant holdem"""
from sim import Agent, CALL, FOLD, raise_to


class MyBot(Agent):
    def act(self, obs):
        # See the Observation class in sim.py for everything obs contains.
        return CALL if obs.to_call == 0 else FOLD
