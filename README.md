# Heads-up no-limit hold'em bot

A from-scratch game-theory poker bot in C: exact hand evaluation, a chance calculator, a strategy trained with Monte Carlo counterfactual regret minimization (MCCFR), and a test suite.

**It is not the best poker bot in existence.** It crushes rule-based bots, but its result against Slumbot was -400 ± 671 mbb/hand over 3,000 hands, which is too noisy to call. Bots such as AlphaHoldem and GTO Wizard AI have published clear wins over Slumbot.

This is for research and practice. Using a bot or real-time assistance on real-money poker sites is commonly banned and can lead to account closure and seized funds, so there is no site integration here.

## Commands

Run these from this folder. `make` builds everything; the trained strategies are already in `data/`.

```bash
make
```

```bash
make test
```

Chances for a hand (win/tie/lose, equity, final-hand odds, pot odds):

```bash
./bin/odds "As Kd" "Qs Js 2c" --opponents 1 --pot 500 --call 250
```

The trained move for a spot. The last argument is the betting so far: `k` or `c`, `f`, or `r<total chips>`. 100 chips = 1 big blind.

```bash
./bin/bench advise data/hu100bb.policy "As Kd" "Qs Js 2c" "r250 c"
```

Benchmarks and training:

```bash
./bin/bench match data/hu100bb.policy tag 200000 4
```

```bash
./bin/bench lbr data/hu100bb.policy 20000 6
```

```bash
python3 slumbot_client.py data/hu200bb.policy 3000 logs/slumbot.csv
```

```bash
./bin/train 10000 2400 data/hu100bb.policy 8000000 4
```

## Results

All figures are in mbb/hand (1,000 mbb = 1 big blind), with 95% confidence intervals.

| Opponent | Bot's result | Hands |
|---|---|---|
| Random | +1585 ± 75 | 200,000 |
| Call station | +1905 ± 63 | 200,000 |
| Maniac (always raises) | +1880 ± 144 | 200,000 |
| Nit (only strong hands) | +408 ± 28 | 200,000 |
| Pot-odds bot (equity vs a random hand) | +721 ± 60 | 200,000 |
| Tight-aggressive rule bot | +465 ± 56 | 200,000 |
| Itself (sanity check, should be 0) | -6 ± 55 | 100,000 |
| 2-minute training snapshot | +2 ± 43 | 200,000 |
| Local Best Response attacker | +390 ± 329 (the attacker lost) | 19,998 |
| **Slumbot (200BB, separately trained strategy)** | **-400 ± 671** | 3,000 |

How to read these:

- **Baselines:** these prove it beats simple bots, not strong ones. I wrote them myself, and they use the deal-duplication scheme that removes most card luck.
- **Slumbot:** this is the only strong outside opponent, and the interval spans both a sizeable loss and a win. By seat, it was +219 ± 1090 on the small blind and -1019 ± 782 on the big blind.
- **Attacker:** a one-action-lookahead exploit search found nothing, but it is weak, so this does not show the bot is unexploitable.
- **Training snapshot:** 40 minutes of training was not measurably better than 2 minutes, either head-to-head or against the attacker (which lost 749 ± 355 to the snapshot and 390 ± 329 to the final bot). The abstraction may limit strength more than training time does, but I did not test that.

### With river solving

This version re-solves each river decision for 300 iterations at 100BB, then compares against the plain strategy.

| Opponent | Plain strategy | With river solving | Solver hands |
|---|---|---|---|
| Plain strategy | n/a | +121 ± 99 | 39,998 |
| Tight-aggressive rule bot | +465 ± 56 | +442 ± 140 | 39,998 |
| Pot-odds bot | +721 ± 60 | +388 ± 231 | 20,006 |
| Slumbot (200BB, 500 iterations) | -400 ± 671 | -375 ± 607 | 3,000 |

- **Against Slumbot:** the two results can't be told apart at this sample size, and neither shows a win or a loss.
- **Against the plain strategy:** solving wins, but the interval nearly reaches zero.
- **Against rule bots:** it gained nothing, and it did worse against the pot-odds bot. Solving moves play toward balance rather than toward exploiting a specific opponent, which may be why, but I did not test that.
- **Speed:** each river decision takes about 0.4 seconds, so this approach can't run under a 1 ms limit.

## How it works

1. **Evaluator:** ranks any 5 to 7 cards. It matches the exact category counts for all 133,784,560 seven-card hands.
2. **Card abstraction:** 169 preflop hand classes; 150 flop and turn buckets from strength and draw potential; 200 river buckets.
3. **Betting abstraction:** a few bet sizes per street plus all-in, giving about 22,000 decision points at 100BB.
4. **Training:** external-sampling MCCFR with linear discounting: 249M iterations at 100BB and 135M at 200BB, 40 minutes each on this Mac.
5. **Play:** looks up the strategy for the current bucket. Off-tree bets are mapped to nearby sizes with the pseudo-harmonic rule.

## What would make it stronger

In order of expected gain:

1. **Wider real-time solving.** Libratus, DeepStack and Pluribus re-solve during play. This bot has river and turn solvers, but only river solving has been played in matches.
2. **Better abstraction:** cluster hands by equity distribution (k-means with earth mover's distance) instead of fixed bands.
3. **More bet sizes and more compute,** which needs more than this 8 GB machine.
4. **A larger Slumbot sample.** Tens of thousands of hands are needed to get a useful interval.

See [RESEARCH.md](RESEARCH.md) for the survey of existing bots.
