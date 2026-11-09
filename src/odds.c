// Calculates a hand's chances: win/tie/lose, equity, final-hand odds, and pot odds.
// usage: odds <hole cards> [board cards] [--opponents N] [--pot chips --call chips]
// example: odds "As Kd" "Qs Js 2c" --opponents 1
#include "poker.h"

#define SAMPLES 1000000
static const char *NAMES[9] = {"High card", "Pair", "Two pair", "Three of a kind", "Straight", "Flush", "Full house", "Four of a kind", "Straight flush"};

static int parse_cards(const char *s, int *out) {
    int n = 0;
    while (*s) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        int c = card_parse(s);
        if (c < 0) return -1;
        out[n++] = c; s += 2;
    }
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: odds \"As Kd\" \"Qs Js 2c\" [--opponents N] [--pot chips --call chips]\n"); return 1; }
    int hole[2], board[5], nb = 0, opps = 1, pot = 0, call = 0;
    if (parse_cards(argv[1], hole) != 2) { fprintf(stderr, "need exactly 2 hole cards\n"); return 1; }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--opponents") && i + 1 < argc) opps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pot") && i + 1 < argc) pot = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--call") && i + 1 < argc) call = atoi(argv[++i]);
        else {
            nb = parse_cards(argv[i], board);
            if (nb < 0 || nb == 1 || nb == 2 || nb > 5) { fprintf(stderr, "board must have 0, 3, 4 or 5 valid cards\n"); return 1; }
        }
    }
    if (opps < 1 || opps > 9) { fprintf(stderr, "opponents must be 1-9\n"); return 1; }
    uint64_t used = (1ULL << hole[0]) | (1ULL << hole[1]);
    if (hole[0] == hole[1]) { fprintf(stderr, "duplicate card\n"); return 1; }
    for (int i = 0; i < nb; i++) {
        if (used >> board[i] & 1) { fprintf(stderr, "duplicate card\n"); return 1; }
        used |= 1ULL << board[i];
    }

    Rng r = {20261031};
    long cat[9] = {0}; double win = 0, tie = 0, share = 0;
    for (long it = 0; it < SAMPLES; it++) {
        uint64_t u = used;
        int b[5], c[7], o[7];
        for (int i = 0; i < nb; i++) b[i] = board[i];
        for (int i = nb; i < 5; i++) { int x; do x = rng_int(&r, 52); while (u >> x & 1); u |= 1ULL << x; b[i] = x; }
        c[0] = hole[0]; c[1] = hole[1];
        for (int i = 0; i < 5; i++) { c[2 + i] = b[i]; o[2 + i] = b[i]; }
        int me = eval_hand(c, 7), best = 0, ties = 0;
        cat[me >> 20]++;
        for (int p = 0; p < opps; p++) {
            for (int k = 0; k < 2; k++) { int x; do x = rng_int(&r, 52); while (u >> x & 1); u |= 1ULL << x; o[k] = x; }
            int v = eval_hand(o, 7);
            if (v > best) { best = v; ties = v == me; }
            else if (v == best && v == me) ties++;
        }
        if (me > best) { win++; share += 1; }
        else if (me == best) { tie++; share += 1.0 / (ties + 1); }
    }
    double n = SAMPLES;
    printf("Against %d random hand%s (%d simulated runouts):\n", opps, opps > 1 ? "s" : "", SAMPLES);
    printf("  Win %.1f%%  Tie %.1f%%  Lose %.1f%%  Equity %.1f%%\n", 100 * win / n, 100 * tie / n, 100 * (n - win - tie) / n, 100 * share / n);
    printf("Final hand chances:\n");
    for (int i = 8; i >= 0; i--) printf("  %-16s %5.1f%%\n", NAMES[i], 100.0 * cat[i] / n);
    if (call > 0) {
        double need = 100.0 * call / (pot + call);
        printf("Pot odds: calling %d into %d needs %.1f%% equity\n", call, pot, need);
    }
    return 0;
}
