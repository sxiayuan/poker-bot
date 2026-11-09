# Poker bot research notes (October 2026)

## Summary

The strongest bots do not work out hand odds and choose a move from them. They compute a game-theoretic strategy through self-play, then refine it during play. Odds are only one input.

"Better than all existing bots" can't be shown at home. Several top systems aren't available to play, and each result comes from a different format and opponent.

## Existing bots

| Bot | Year | Game | Method | Reported result |
|---|---|---|---|---|
| Cepheus | 2015 | Heads-up limit | CFR+ across 4,800 CPUs for 68 days | Essentially solved; about 0.986 mbb/game exploitable (figure from a secondary survey) |
| DeepStack | 2017 | Heads-up no-limit | Real-time solving with neural value networks | +492 mbb/game vs professionals (+486 with variance reduction) |
| Libratus | 2017 | Heads-up no-limit | Blueprint, subgame solving, self-improvement | +147 mbb/hand over 120,000 hands vs top pros (secondary source) |
| Pluribus | 2019 | 6-player no-limit | Blueprint plus limited-lookahead search | +48 ± 25 mbb/game over 10,000 hands vs pros |
| ReBeL | 2020 | Heads-up no-limit | Self-play RL plus search | Beat a top professional; listed at 4.5 BB/100 vs Slumbot |
| AlphaHoldem | 2022 | Heads-up no-limit | End-to-end RL, no search, 2.9 ms per decision | +111.56 mbb/hand vs Slumbot, +16.91 vs DeepStack |
| GTO Wizard AI | 2026 | Heads-up no-limit | Equilibrium solving plus deep learning | +19.4 ± 4.1 BB/100 vs Slumbot (reported by its own authors) |
| LLM agents | 2025-26 | Various | Prompted or fine-tuned language models | Mixed. Frontier models fell far below the GTO Wizard baseline in a zero-shot test; PokerSkill claims wins with a framework, as an unreviewed preprint |

These numbers can't be ranked directly: table size, opponents, stack depth and sample size all differ. A 6-player win rate and a heads-up win rate measure different things.

## What the winners have in common

1. **Counterfactual regret minimization (CFR) or a learned equivalent.** The bot plays itself repeatedly and shifts toward actions it regrets not taking. In two-player zero-sum games, the average strategy approaches a Nash equilibrium, which can't lose in expectation apart from any remaining exploitability.
2. **Abstraction.** No-limit hold'em is far too large to solve directly, so similar hands and bet sizes are grouped, or a neural network generalizes across them.
3. **Real-time search.** The fixed "blueprint" is refined for the actual situation during play. This is the main difference between this project and the systems above.
4. **Mixed strategies.** Good play randomizes, for example bluffing some of the time, so that it can't be read.
5. **Variance-controlled evaluation.** Poker is noisy enough that tens of thousands of hands, or techniques such as AIVAT and duplicate deals, are needed for a reliable result.

## Equilibrium versus win rate

An equilibrium bot is hard to beat, but it doesn't maximize profit against weak players. A bot that models a specific opponent can win more from that opponent, at the risk of being exploited itself. "Highest win rate" therefore depends on who is at the table. Against bad rule-based bots, this project's win rates look large because those bots are bad.

## Open-source starting points

- **OpenSpiel** (DeepMind): CFR, CFR+ and Monte Carlo CFR variants, plus poker games.
- **RLCard:** a card-game RL toolkit with built-in CFR, DQN and NFSP agents.
- **PokerRL and Deep CFR** (Steinberger): neural CFR, but they pin very old dependencies.
- **ReBeL** (Meta): official implementation.
- **DecisionHoldem:** blueprint with real-time depth-limited solving.

## Public benchmarks

- **Slumbot:** free API, 200BB deep. Used in this project.
- **GTO Wizard Benchmark:** a public API and evaluation framework announced in March 2026. Not tried here.

## Real-money sites

Sites treat bots and real-time assistance as cheating. GGPoker banned 40 accounts in 2020 and confiscated $1,175,305 from 13 of them, and has said it can apply permanent bans and confiscation. PokerStars told a player in 2009 that running a bot broke its terms, and it seized the funds. These are news reports rather than current terms of service, so check each operator's current terms. Bots also take money from other players, which is why sites have refunded victims.

## Sources

- [Wikipedia: Libratus](https://en.wikipedia.org/wiki/Libratus)
- [DeepStack paper](https://arxiv.org/pdf/1701.01724v2)
- [Poker AI research timeline](https://www.deucescracked.com/blog/poker-ai-research-timeline-data)
- [AlphaHoldem, AAAI 2022](https://ojs.aaai.org/index.php/AAAI/article/view/20394)
- [ReBeL paper](https://arxiv.org/pdf/2007.13544v1)
- [Slumbot benchmark leaderboard](https://www.wizwand.com/sota/heads-up-no-limit-hold-em-poker-on-slumbot-benchmark-agent)
- [GTO Wizard Benchmark](https://arxiv.org/html/2603.23660)
- [PokerSkill preprint](https://arxiv.org/html/2605.30094v1)
- [Beyond Game Theory Optimal](https://arxiv.org/pdf/2509.23747v1)
- [CFR+ paper, IJCAI 2015](https://www.cs.ualberta.ca/~games/poker/publications/2015-ijcai-cfrplus.pdf)
- [Survey on Game Theory Optimal Poker](https://arxiv.org/pdf/2401.06168)
- [OpenSpiel](https://arxiv.org/pdf/1908.09453)
- [RLCard](https://arxiv.org/pdf/1910.04376)
- [PokerRL](https://pypi.org/project/PokerRL)
- [Deep CFR code](https://www.github.com/TinkeringCode/Deep-CFR)
- [awesome-poker list](https://www.github.com/apehex/awesome-poker)
- [GGPoker bans, PokerNews](https://www.pokernews.com/news/2020/09/ggpoker-responds-cheating-scandal-38050.htm)
- [PokerStars bot case](https://mattmazur.com/2009/06/13/improving-pokerstarss-bot-detection-techniques/)
- [888poker refunds](https://pokerfuse.com/news/poker-room-news/216636-888-clamps-down-bots-100000-reimbursed-players/)
