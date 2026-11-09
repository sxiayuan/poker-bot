// Checks the evaluator against the known counts of all 133,784,560 seven-card hands,
// plus betting-rule and hand-comparison cases.
#include <assert.h>
#include "poker.h"

static int hv(const char *s) {
    int c[7], n = 0;
    for (; *s; s += 3) { c[n++] = card_parse(s); if (!s[2]) break; }
    return eval_hand(c, n);
}

int main(void) {
    static const long expected[9] = {23294460, 58627800, 31433400, 6461620, 6180020, 4047644, 3473184, 224848, 41584};
    long counts[9] = {0};
    int c[7];
    for (c[0] = 0; c[0] < 52; c[0]++) for (c[1] = c[0] + 1; c[1] < 52; c[1]++)
    for (c[2] = c[1] + 1; c[2] < 52; c[2]++) for (c[3] = c[2] + 1; c[3] < 52; c[3]++)
    for (c[4] = c[3] + 1; c[4] < 52; c[4]++) for (c[5] = c[4] + 1; c[5] < 52; c[5]++)
    for (c[6] = c[5] + 1; c[6] < 52; c[6]++) counts[eval_hand(c, 7) >> 20]++;
    int bad = 0;
    for (int i = 0; i < 9; i++) {
        printf("category %d: %ld (expected %ld)\n", i, counts[i], expected[i]);
        if (counts[i] != expected[i]) bad = 1;
    }

    // Ordering, kickers, wheel, board-plays cases
    assert(hv("As Ks Qs Js Ts 2c 3d") > hv("Ks Qs Js Ts 9s 2c 3d"));
    assert(hv("5s 4d 3c 2h Ad Kc Qc") < hv("6s 5d 4c 3h 2d Kc Qc"));
    assert(hv("Ah Ad Kc Qs Jd 2c 3d") > hv("Ah Ad Qc Js 9d 2c 3d"));
    assert(hv("Ks Kd Qc Qd 2h 2c As") == hv("Kh Kc Qs Qh 3h 3c Ad"));
    assert(hv("As Ad Ac Kd Kc Qs Qd") > hv("Ks Kd Kc As Ac Qs Qd"));
    assert(hv("2s 2d 2c 2h As Kd Qc") > hv("As Ad Ac Kd Kc Ks Qd"));
    // A flush must beat trips made from the same seven cards
    assert((hv("As Ks 9s 5s 2s Ad Ac") >> 20) == FLUSH);
    // Straight flush must not be confused with a higher offsuit straight
    assert((hv("9h 8h 7h 6h 5h Ts Jd") >> 20) == STRAIGHT_FLUSH);

    // Betting rules
    State s; st_init(&s, 10000);
    assert(st_to_call(&s) == 50 && s.to_act == 0);
    st_call(&s);                         // SB limps: BB still has the option
    assert(s.street == 0 && s.to_act == 1 && st_to_call(&s) == 0);
    st_call(&s);                         // BB checks: flop, BB acts first
    assert(s.street == 1 && s.to_act == 1 && st_pot(&s) == 200);
    st_raise(&s, 200);
    assert(s.to_act == 0 && st_to_call(&s) == 100 && st_min_raise_to(&s) == 300);
    st_raise(&s, 600);
    assert(st_min_raise_to(&s) == 1000);
    st_call(&s);
    assert(s.street == 2 && s.c[0] == 600 && s.c[1] == 600);
    st_raise(&s, 10000);
    st_call(&s);
    assert(s.done == 2);
    st_init(&s, 10000); st_fold(&s);
    assert(s.done == 1 && s.winner == 1);

    // Equity sanity: AA vs a random hand preflop is about 85%
    Rng r = {1}; int aa[2] = {card_parse("As"), card_parse("Ah")}; double e, i;
    hand_strength(aa, NULL, 0, 200000, &r, &e, &i);
    printf("AA vs random: %.3f (expected ~0.852)\n", e);
    assert(fabs(e - 0.852) < 0.01);
    if (bad) { printf("EVALUATOR MISMATCH\n"); return 1; }
    printf("all tests passed\n");
    return 0;
}
