# 60-minute rehearsal playbook

This is practice for No Limit: A Bot Building Tournament. The simulator is built from the [published rules](https://pokerbot.sequoiacap.com/rules.html); it is **not** the official starter kit, and its interface will differ.

**Don't assume you can bring this code to the event.** The rules don't settle whether prepared code or data is allowed, and the homepage says "from scratch". Section 3.4 also names only Astra and Codex as build-phase AI tools. Ask pokerbot@sequoiacap.com first. Rehearsing the process is useful either way.

## Setup

Run these from this folder. They only need Python 3.

```bash
python3 test_sim.py
```

```bash
python3 run.py sample_bot.py:SampleBot --variant holdem --tables 1000
```

Edit [my_bot.py](my_bot.py) and score it. To practice not knowing the variant, have a friend pick one, or pick at random:

```bash
python3 -c "import random; print(random.choice(['holdem', 'omaha', 'shortdeck', 'threecard']))"
```

To test against tougher opponents or an older version of your bot, pass `--field`:

```bash
python3 run.py my_bot.py:MyBot --field nit,tag,shover,sample_bot.py:SampleBot
```

## What the first tests suggest

All runs are about 1,000 tables, with 95% confidence intervals. The average bot scores 31.9 points per table. Scores depend on my **placeholder** points and blinds, and on opponents I wrote, so trust the direction more than the numbers.

| Bot | Mixed field, including weak bots | Tough field |
|---|---|---|
| Folds everything unless checking is free | 37.1 ± 0.9 | 21.7 ± 0.7 |
| Sample bot | 49.4 ± 1.5 | 37.2 ± 1.5 |
| Tight-aggressive rules | 45.5 ± 1.5 | 32.9 ± 1.5 |
| Always shoves good hands | 47.7 ± 2.0 | 30.0 ± 1.7 |
| Pot-odds bot | 39.2 ± 2.3 | not run |

- **Not busting is worth a lot.** Against weak bots, even folding every hand beat the average, probably because reckless bots eliminate each other. Against a field like that, surviving comes before winning chips.
- **The field changes everything.** Folding dropped from above average to the bottom in the tough field, and shoving fell from close to the sample bot to below average. Test against several fields so you don't tune to one.
- **Tables often reach the hand limit.** In the tough field, tables averaged about 200 hands, so final stack sizes and ICM decided much of the score. If the real blinds rise faster, this will change.
- **The sample bot is a target, not an answer.** It is not clearly ahead of the shover in the mixed field, and I did not tune it.

Variant results for the sample bot in the mixed field: Hold'em 49.4, Omaha 46.2, short deck 50.7, three-card 49.7.

## A bot that studies its opponents

[exploit_bot.py](exploit_bot.py) tracks each seat during a table: how often it folds to a raise before and after the flop, how often it enters pots, how aggressive it is, and how strong the hands it raised with were at showdown. Each stat starts from a default guess and moves toward what the seat actually does. The bot uses these stats to steal the blinds when the remaining players fold a lot, to shove wider when short, to bet bigger and thinner into players who rarely fold, and to call tight raisers less.

Four opponent styles were held out of tuning to check for overfitting: a loose-aggressive bot, a bluffer, a trapper and a defender that re-raises frequent stealers. Scores are points per table on fresh deals (about 2,000 tables each, roughly ±1.5):

| Field | Exploit bot | Sample bot |
|---|---|---|
| Mixed (training) | 73.1 | 49.7 |
| Tough (training) | 82.6 | 37.0 |
| Held-out styles | 57.6 | 36.7 |
| Held-out styles plus sample bot | 61.4 | 34.2 |
| Everyone | 65.8 | 44.3 |
| Copies of itself | 32.6 | n/a |

- **Most of the gain is blind stealing** against seats that fold too much. Bluffing after the flop added nothing.
- **Tuning overfit.** [tune.py](tune.py) pushed the bot to steal with almost any hand. That scored 87 on the training fields but did worse on held-out styles and scored only 11 against copies of the untuned bot, so the steal settings were left untuned. Always check a tuned bot with [check.py](check.py) on opponents it never trained on.
- **By variant** (field: everyone, timer on): hold'em 66.2, Omaha 42.3, short deck 61.6, three-card 50.6. Omaha gains the least, probably because the hand-strength score is weakest there.
- **Short-handed play:** watching a table showed the bot folding A-T and K-Q three-handed. Loosening its raise and call thresholds as seats empty gained less than 0.5 points per table, which is within noise, so it's off by default (`short_handed` in `P`). The real cause is the preflop score, which rates A-T (0.46) barely above weak hands; a table ranking all 169 starting hands is the likely fix.
- **Timing:** averages of 11–110 µs, no errors or illegal moves, but 0.02–0.55% timeouts. The machine was heavily loaded by other programs during this run, so those timeouts are likely inflated.

## Speed

The limit is 1 ms per decision, and missing it folds or checks your hand.

| Variant | Average decision | Timeouts |
|---|---|---|
| Hold'em | 5 µs | 0.00% |
| Omaha | 63 µs | 0.07% |
| Short deck | 5 µs | 0.00% |
| Three-card | 12 µs | 0.09% |

This is the sample bot over 300 tables per variant. With heavy jobs running, the same bot averaged 22 to 266 µs and timed out on up to 2.18% of decisions, so machine load alone can cost you hands. Timeouts still happened here, with the slowest decision at about 10 ms, so a low average doesn't rule out occasional spikes. The real sandbox's speed is unknown, so aim well under the limit: under 200 µs on average, with no slow first call.

Pure-Python hand ranking is only fast enough for a handful of simulated runouts per decision. Compute tables before the game when you can, and do only lookups at decision time.

## The 60 minutes

| Minutes | Goal |
|---|---|
| 0-5 | Read the variant, point distribution, blinds and Agent interface. Note what differs from hold'em: deck, hole cards, hand order, betting. |
| 5-15 | **Submit a safe bot:** check when free, otherwise fold, with only legal moves. You now have something active. |
| 15-30 | Add a hand-strength score for this variant, with a preflop lookup table and simple postflop categories. Add tight value betting and submit. |
| 30-40 | Add short-stack push/fold ranges and ICM caution near elimination. Compare against the previous version, and submit only if it wins. |
| 40-50 | Add per-seat tracking of fold and raise rates and adjust for it. Test the variant's edge cases. |
| 50-60 | Stop adding features. Check timing and errors over many tables, and make the final safe submission. |

After the build phase, you can keep submitting before T2-T6, and later rounds count more: T6 is worth 6 of the 21 total weight.

## Checklist before every submission

- Zero errors and zero illegal moves over at least 1,000 tables.
- Timeouts at or near zero, with the average well under the limit.
- A `try/except` around the whole decision that returns a safe move.
- Bet amounts clamped to the legal range and converted to whole numbers.
- Tested when heads-up, short-stacked, all-in, and when raising isn't allowed.
- Beats the previous version on more than one field, by more than the confidence interval.

## Questions to answer from the starter kit

- How are points distributed, and how much do high finishes pay compared with surviving? This sets how tight to play.
- What is the real blind schedule? It decides how often tables reach the 200-hand ICM finish.
- Does your bot see other seats' actions and shown cards? Opponent tracking depends on it.
- Does the 1 ms limit apply to startup or loading? This determines whether large tables are practical.

## Things that aren't allowed

Colluding, copying another team's code, misusing the submission endpoint, and network or AI calls during play. See sections 3.6, 6.1 and 7.1 of the rules.
