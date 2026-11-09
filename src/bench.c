// Benchmarks the CFR bot: duplicate-deal matches against baseline bots, and a
// Local Best Response attack (Lisy & Bowling 2017) that estimates a lower bound on
// how much the bot can be exploited. Results are in mbb/hand (1000 mbb = 1 big blind).
//
// usage: bench match <a> <b> <hands> [threads]    a,b: file.policy or a baseline name
//        bench lbr <file.policy> <hands> [threads]
//        bench serve <agent>                      line protocol used by slumbot_client.py
//    An agent can also be solve:<file.policy>:<from street, 2=turn 3=river>:<iterations>,
//    which plays the blueprint early and re-solves in real time from that street on.
//        bench advise <file.policy> "<hole>" "<board>" "<actions>"
//              actions from the start of the hand: k/c, f, r<raise-to total chips>, e.g. "r250 c k"
#include <pthread.h>
#include "solver.h"

#define THRESH "data/thresholds.bin"
enum { B_CFR, B_RANDOM, B_CALL, B_RAISE, B_NIT, B_ODDS, B_TAG, B_SOLVER };
typedef struct { int kind; const Policy *P; int from, iters; } Agent;
typedef struct { Agent ag; CfrView v; SolverView *sv; } Player;

static double eq_vs_random(const int *hole, const int *board, int street, int n, Rng *r) {
    double e, i;
    hand_strength(hole, board, street ? street + 2 : 0, n, r, &e, &i);
    return e;
}
static Decision raise_pot(const State *s, double f) {
    Decision d = {A_CALL, 0};
    if (st_can_raise(s)) { d.type = A_RAISE; d.to = st_raise_to_frac(s, f); }
    return d;
}
static Decision baseline(int kind, const State *s, const int *h, const int *b, Rng *r) {
    Decision call = {A_CALL, 0}, fold = {A_FOLD, 0};
    int tc = st_to_call(s);
    double odds = tc ? (double)tc / (st_pot(s) + tc) : 0;
    switch (kind) {
    case B_RANDOM: {
        double u = rng_float(r);
        if (tc && u < 0.25) return fold;
        if (u < 0.75) return call;
        return raise_pot(s, 0.5 + rng_float(r));
    }
    case B_CALL: return call;
    case B_RAISE: return raise_pot(s, 1.0);
    case B_NIT: {  // plays only strong hands, bets them, never bluffs
        double e = eq_vs_random(h, b, s->street, 200, r);
        if (s->street == 0) return e > 0.68 ? raise_pot(s, 1.0) : (tc ? fold : call);
        if (e > 0.85) return raise_pot(s, 0.75);
        if (e * 0.75 > odds) return call;
        return tc ? fold : call;
    }
    case B_ODDS: {  // classic "calculate the chances": pure equity vs a random hand and pot odds
        double e = eq_vs_random(h, b, s->street, 200, r);
        if (e > 0.75) return raise_pot(s, 1.0);
        if (e >= odds) return call;
        return fold;
    }
    case B_TAG: {  // tight-aggressive rules with equity discounted when facing bets, plus some bluffs
        double e = eq_vs_random(h, b, s->street, 200, r);
        if (s->street == 0) {
            if (e > 0.60) return raise_pot(s, tc > 100 ? 1.0 : 0.75);
            if (e > 0.48 && odds < 0.34) return call;
            return tc ? fold : call;
        }
        if (e > 0.82) return raise_pot(s, 0.75);
        if (tc == 0) {
            if (e > 0.62 || rng_float(r) < 0.15) return raise_pot(s, 0.6);
            return call;
        }
        double disc = e * (odds > 0.3 ? 0.75 : 0.88);
        return disc > odds ? call : fold;
    }
    }
    return call;
}
static Decision agent_decide(Player *p, const State *s, const int *h, const int *b, Rng *r) {
    if (p->ag.kind == B_CFR) return cfr_decide(&p->v, s, h, b, r);
    if (p->ag.kind == B_SOLVER) return sv_decide(p->sv, s, h, b, r);
    return baseline(p->ag.kind, s, h, b, r);
}
static void player_reset(Player *p) {
    if (p->ag.kind == B_CFR) cfr_reset(&p->v, p->ag.P);
    if (p->ag.kind == B_SOLVER) {
        if (!p->sv) p->sv = calloc(1, sizeof(SolverView));
        sv_reset(p->sv, p->ag.P, p->ag.from, p->ag.iters);
    }
}
static void player_observe(Player *p, const State *before, Decision d, int own, Rng *r) {
    if (p->ag.kind == B_CFR) cfr_observe(&p->v, before, d, own, r);
    if (p->ag.kind == B_SOLVER) sv_observe(p->sv, before, d, own, r);
}
static void apply(State *s, Decision d) {
    if (d.type == A_FOLD) {
        if (st_to_call(s) == 0) st_call(s);  // never fold when checking is free
        else st_fold(s);
    }
    else if (d.type == A_CALL) st_call(s);
    else st_raise(s, d.to);
}
static int play_hand(Player *a, Player *b, int stack, const int h[2][2], const int *board, Rng *r) {
    // a sits in seat 0 (small blind); returns a's chips won
    State s; st_init(&s, stack);
    Player *seats[2] = {a, b};
    player_reset(a); player_reset(b);
    while (!s.done) {
        int p = s.to_act;
        Decision d = agent_decide(seats[p], &s, h[p], board, r);
        if (d.type == A_RAISE && !st_can_raise(&s)) d.type = A_CALL;
        State before = s;
        apply(&s, d);
        for (int q = 0; q < 2; q++) player_observe(seats[q], &before, d, q == p, r);
    }
    return showdown_payoff(&s, h[0], h[1], board, 0);
}
static void deal(Rng *r, int h[2][2], int *board) {
    uint64_t u = 0; int d[9], k = 0;
    while (k < 9) { int x = rng_int(r, 52); if (u >> x & 1) continue; u |= 1ULL << x; d[k++] = x; }
    h[0][0] = d[0]; h[0][1] = d[1]; h[1][0] = d[2]; h[1][1] = d[3];
    for (int i = 0; i < 5; i++) board[i] = d[4 + i];
}

// ---------- match runner ----------
typedef struct { Agent a, b; int stack; long hands; uint64_t seed; double sum, sq; long n; } MatchJob;
static void *match_worker(void *arg) {
    MatchJob *j = (MatchJob *)arg;
    Rng r = {j->seed};
    Player A = {j->a, {0}, NULL}, B = {j->b, {0}, NULL};
    for (long i = 0; i < j->hands; i += 2) {
        int h[2][2], board[5];
        deal(&r, h, board);
        // Duplicate: replay the same cards with the players swapped, so each player
        // holds both hands from both seats' perspective. Removes most card luck.
        int x = play_hand(&A, &B, j->stack, h, board, &r);
        int y = -play_hand(&B, &A, j->stack, h, board, &r);
        double v = (x + y) / 2.0;
        j->sum += v; j->sq += v * v; j->n++;
    }
    return NULL;
}
static Agent parse_agent(const char *s) {
    Agent a = {B_CFR, NULL, 0, 0};
    if (!strncmp(s, "solve:", 6)) {
        char file[400]; int from, iters;
        if (sscanf(s + 6, "%399[^:]:%d:%d", file, &from, &iters) != 3 || from < 2 || from > 3) {
            fprintf(stderr, "expected solve:<file.policy>:<2|3>:<iterations>\n"); exit(1);
        }
        a.kind = B_SOLVER; a.from = from; a.iters = iters;
        a.P = policy_load(file, THRESH);
        if (!a.P) exit(1);
        return a;
    }
    static const char *names[] = {"", "random", "callstation", "maniac", "nit", "potodds", "tag"};
    for (int i = 1; i < 7; i++) if (!strcmp(s, names[i])) { a.kind = i; return a; }
    a.P = policy_load(s, THRESH);
    if (!a.P) exit(1);
    return a;
}
static void report(const char *label, double sum, double sq, long n) {
    double mean = sum / n, var = sq / n - mean * mean, se = sqrt(var / n);
    // chips -> mbb: 100 chips = 1 BB, so 1 chip = 10 mbb
    printf("%-40s %+9.1f mbb/hand  +/- %.1f (95%% CI, %ld duplicate pairs)\n", label, mean * 10, 1.96 * se * 10, n);
    fflush(stdout);
}
static int run_match(const char *na, const char *nb, long hands, int threads) {
    Agent a = parse_agent(na), b = parse_agent(nb);
    int stack = a.P ? a.P->tree.stack : b.P ? b.P->tree.stack : 10000;
    pthread_t th[64]; MatchJob j[64];
    for (int t = 0; t < threads; t++) {
        memset(&j[t], 0, sizeof j[t]);
        j[t].a = a; j[t].b = b; j[t].stack = stack; j[t].hands = hands / threads; j[t].seed = 1000 + t;
        pthread_create(&th[t], NULL, match_worker, &j[t]);
    }
    double sum = 0, sq = 0; long n = 0;
    for (int t = 0; t < threads; t++) { pthread_join(th[t], NULL); sum += j[t].sum; sq += j[t].sq; n += j[t].n; }
    char label[200]; snprintf(label, sizeof label, "%s vs %s", na, nb);
    report(label, sum, sq, n);
    return 0;
}

// ---------- Local Best Response ----------
// LBR tracks the bot's range from its actual policy. At each decision it
// computes its equity against that range and picks the best of fold / call / bet sizes,
// assuming the opponent's fold chance is what the policy says and that the hand then
// checks down. Any profit is a real, demonstrated weakness; a loss proves nothing,
// because this attacker only looks one action ahead.
typedef struct { const Policy *P; long hands; uint64_t seed; double sum, sq; long n; } LbrJob;
typedef struct { int a, b; double w; int bucket; } Combo;

static int lbr_buckets(Combo *C, int nc, const Policy *P, int street, const int *board, Rng *r) {
    for (int i = 0; i < nc; i++) {
        if (C[i].w <= 0) continue;
        int h[2] = {C[i].a, C[i].b};
        C[i].bucket = bucket_of(&P->T, street, h, board, 100, r);
    }
    return 0;
}
static double lbr_equity(const Combo *C, int nc, const int *hole, const int *board, int nb, Rng *r) {
    double total = 0;
    for (int i = 0; i < nc; i++) total += C[i].w > 0 ? C[i].w : 0;
    if (total <= 0) return 0.5;
    double win = 0; const int S = 1500;
    uint64_t base = (1ULL << hole[0]) | (1ULL << hole[1]);
    for (int i = 0; i < nb; i++) base |= 1ULL << board[i];
    for (int it = 0; it < S; it++) {
        double u = rng_float(r) * total; int k = nc - 1;
        for (int i = 0; i < nc; i++) { if (C[i].w > 0) { u -= C[i].w; if (u <= 0) { k = i; break; } } }
        uint64_t used = base | (1ULL << C[k].a) | (1ULL << C[k].b);
        int m[7] = {hole[0], hole[1]}, o[7] = {C[k].a, C[k].b};
        for (int i = 0; i < 5; i++) {
            int x;
            if (i < nb) x = board[i];
            else { do x = rng_int(r, 52); while (used >> x & 1); used |= 1ULL << x; }
            m[2 + i] = x; o[2 + i] = x;
        }
        int a = eval_hand(m, 7), b = eval_hand(o, 7);
        win += a > b ? 1 : a == b ? 0.5 : 0;
    }
    return win / S;
}
static void lbr_update_range(Combo *C, int nc, const Policy *P, const Node *n, int action) {
    double pr[MAXA];
    for (int i = 0; i < nc; i++) {
        if (C[i].w <= 0) continue;
        policy_probs(P, n, C[i].bucket, pr);
        C[i].w *= pr[action];
    }
}
static void *lbr_worker(void *arg) {
    LbrJob *j = (LbrJob *)arg;
    Rng r = {j->seed};
    const Policy *P = j->P;
    const float SIZES[3] = {0.5f, 1.0f, 1e9f};
    Combo *C = malloc(1326 * sizeof(Combo));
    for (long hand = 0; hand < j->hands; hand++) {
        int h[2][2], board[5];
        deal(&r, h, board);
        int lbr_seat = hand & 1, bot_seat = 1 - lbr_seat;
        int nc = 0;
        uint64_t dead = (1ULL << h[lbr_seat][0]) | (1ULL << h[lbr_seat][1]);
        for (int a = 0; a < 52; a++) for (int b = a + 1; b < 52; b++) {
            if ((dead >> a & 1) || (dead >> b & 1)) continue;
            C[nc].a = a; C[nc].b = b; C[nc].w = 1; C[nc].bucket = preflop_index((int[]){a, b}); nc++;
        }
        State s; st_init(&s, P->tree.stack);
        CfrView v; cfr_reset(&v, P);
        int bucket_street = 0;
        while (!s.done) {
            int nb = s.street ? s.street + 2 : 0;
            if (s.street > 0 && bucket_street != s.street) {
                for (int i = 0; i < nc; i++) {  // remove combos blocked by the new board cards
                    for (int k = 0; k < nb; k++) if (C[i].a == board[k] || C[i].b == board[k]) C[i].w = 0;
                }
                lbr_buckets(C, nc, P, s.street, board, &r);
                bucket_street = s.street;
            }
            Decision d;
            State before = s;
            if (s.to_act == bot_seat) {
                d = cfr_decide(&v, &s, h[bot_seat], board, &r);
                if (d.type == A_RAISE && !st_can_raise(&s)) d.type = A_CALL;
                if (v.node >= 0 && v.forced >= 0) lbr_update_range(C, nc, P, &P->tree.n[v.node], v.forced);
                apply(&s, d);
                cfr_observe(&v, &before, d, 1, &r);
                continue;
            }
            d.type = A_CALL; d.to = 0;
            {
                double eq = lbr_equity(C, nc, h[lbr_seat], board, nb, &r);
                int tc = st_to_call(&s), pot = st_pot(&s);
                double best = eq * (pot + tc) - tc;
                if (tc > 0 && best < 0) { d.type = A_FOLD; best = 0; }
                if (st_can_raise(&s) && v.node >= 0 && P->tree.n[v.node].type == N_PLAYER) {
                    for (int k = 0; k < 3; k++) {
                        int to = SIZES[k] >= 1e8f ? s.stack : st_raise_to_frac(&s, SIZES[k]);
                        Rng r2 = r;
                        const Node *cur = &P->tree.n[v.node];
                        int a = map_raise(cur, &s, to, &r2);
                        if (a < 0) continue;
                        const Node *nx = &P->tree.n[cur->child[a]];
                        double fold = 0, tot = 0, pr[MAXA];
                        if (nx->type == N_PLAYER) {
                            for (int i = 0; i < nc; i++) {
                                if (C[i].w <= 0) continue;
                                policy_probs(P, nx, C[i].bucket, pr);
                                tot += C[i].w;
                                if (nx->atype[0] == A_FOLD) fold += C[i].w * pr[0];
                            }
                        }
                        double f = tot > 0 ? fold / tot : 0;
                        int add = to - s.c[s.to_act];
                        int opp_add = to - s.c[1 - s.to_act];
                        double u = f * pot + (1 - f) * (eq * (pot + add + opp_add) - add);
                        if (u > best) { best = u; d.type = A_RAISE; d.to = to; }
                    }
                }
            }
            apply(&s, d);
            cfr_observe(&v, &before, d, 0, &r);
        }
        double x = showdown_payoff(&s, h[0], h[1], board, lbr_seat);
        j->sum += x; j->sq += x * x; j->n++;
    }
    free(C);
    return NULL;
}
static int run_lbr(const char *path, long hands, int threads) {
    Policy *P = policy_load(path, THRESH);
    if (!P) return 1;
    pthread_t th[64]; LbrJob j[64];
    for (int t = 0; t < threads; t++) {
        memset(&j[t], 0, sizeof j[t]);
        j[t].P = P; j[t].hands = hands / threads; j[t].seed = 555 + t;
        pthread_create(&th[t], NULL, lbr_worker, &j[t]);
    }
    double sum = 0, sq = 0; long n = 0;
    for (int t = 0; t < threads; t++) { pthread_join(th[t], NULL); sum += j[t].sum; sq += j[t].sq; n += j[t].n; }
    double mean = sum / n, se = sqrt((sq / n - mean * mean) / n);
    printf("LBR attacker vs %-24s %+9.1f mbb/hand  +/- %.1f (95%% CI, %ld hands)\n", path, mean * 10, 1.96 * se * 10, n);
    return 0;
}

// ---------- line protocol for external matches ----------
// In:  "new <seat> <c1> <c2>"  |  "board <cards...>"
//      "act <seat> f|c|r<raise_to_total>"  (applies an action by either seat)
//      "go"  (asks the bot to move)
// Out: "f", "c" or "r<raise_to_total>" in reply to "go"; "ok" otherwise
static int run_serve(const char *name) {
    Player me = {parse_agent(name), {0}, NULL};
    if (!me.ag.P) { fprintf(stderr, "serve needs a trained agent\n"); return 1; }
    Rng r = {20261031};
    State s; int hole[2] = {0, 0}, board[5] = {0}, nb = 0, seat = 0;
    int have_pending = 0;
    char line[256];
    setvbuf(stdout, NULL, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        char *cmd = strtok(line, " \n");
        if (!cmd) continue;
        if (!strcmp(cmd, "new")) {
            seat = atoi(strtok(NULL, " \n"));
            hole[0] = card_parse(strtok(NULL, " \n"));
            hole[1] = card_parse(strtok(NULL, " \n"));
            st_init(&s, me.ag.P->tree.stack); player_reset(&me); nb = 0; have_pending = 0;
            puts("ok");
        } else if (!strcmp(cmd, "board")) {
            char *t; nb = 0;
            while ((t = strtok(NULL, " \n")) && nb < 5) board[nb++] = card_parse(t);
            puts("ok");
        } else if (!strcmp(cmd, "go")) {
            Decision d = agent_decide(&me, &s, hole, board, &r);
            if (d.type == A_RAISE && !st_can_raise(&s)) d.type = A_CALL;
            if (d.type == A_FOLD && st_to_call(&s) == 0) d.type = A_CALL;
            have_pending = 1;
            if (d.type == A_FOLD) puts("f");
            else if (d.type == A_CALL) puts("c");
            else printf("r%d\n", d.to);
        } else if (!strcmp(cmd, "act")) {
            int who = atoi(strtok(NULL, " \n"));
            char *a = strtok(NULL, " \n");
            Decision d = {A_CALL, 0};
            if (a[0] == 'f') d.type = A_FOLD;
            else if (a[0] == 'r') { d.type = A_RAISE; d.to = atoi(a + 1); }
            State before = s;
            int own = who == seat && have_pending;
            apply(&s, d);
            player_observe(&me, &before, d, own, &r);
            have_pending = 0;
            puts("ok");
        }
    }
    return 0;
}

// ---------- advice for one spot ----------
static int run_advise(const char *path, const char *hole_s, const char *board_s, const char *line) {
    Policy *P = policy_load(path, THRESH);
    if (!P) return 1;
    Rng r = {20261031};
    int hole[2] = {card_parse(hole_s), card_parse(hole_s + 3)}, board[5], nb = 0;
    for (const char *c = board_s; *c && nb < 5; c++) {
        if (*c == ' ') continue;
        board[nb++] = card_parse(c); c++;
    }
    State s; st_init(&s, P->tree.stack);
    int node = 0;
    char buf[512]; snprintf(buf, sizeof buf, "%s", line);
    for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) {
        const Node *n = &P->tree.n[node];
        if (s.done || node < 0 || n->type != N_PLAYER) { printf("That line leaves the bot's betting tree.\n"); return 1; }
        Decision d = {A_CALL, 0};
        int a = -1;
        if (t[0] == 'f') d.type = A_FOLD;
        else if (t[0] == 'r') { d.type = A_RAISE; d.to = atoi(t + 1); }
        if (d.type == A_RAISE) a = map_raise(n, &s, d.to, &r);
        else for (int i = 0; i < n->nact; i++) if (n->atype[i] == d.type) a = i;
        if (a < 0) { printf("That line leaves the bot's betting tree.\n"); return 1; }
        apply(&s, d);
        node = n->child[a];
    }
    if (s.done) { printf("The hand is already over after that line.\n"); return 1; }
    int need = s.street ? s.street + 2 : 0;
    if (nb != need) { printf("This spot is on street %d, which needs %d board cards (got %d).\n", s.street, need, nb); return 1; }
    const Node *n = &P->tree.n[node];
    int b = bucket_of(&P->T, s.street, hole, board, 5000, &r);
    double e, ih, pr[MAXA];
    hand_strength(hole, board, need, 100000, &r, &e, &ih);
    policy_probs(P, n, b, pr);
    int tc = st_to_call(&s);
    printf("Pot %d chips, %d to call (100 chips = 1 big blind). Equity vs a random hand: %.1f%%", st_pot(&s), tc, 100 * e);
    if (tc) printf("; pot odds need %.1f%%", 100.0 * tc / (st_pot(&s) + tc));
    printf("\nTrained strategy for this spot:\n");
    for (int a = 0; a < n->nact; a++) {
        if (n->atype[a] == A_FOLD) printf("  fold                  %5.1f%%\n", 100 * pr[a]);
        else if (n->atype[a] == A_CALL) printf("  %-21s %5.1f%%\n", tc ? "call" : "check", 100 * pr[a]);
        else if (n->frac[a] >= ALLIN_FRAC) printf("  all-in                %5.1f%%\n", 100 * pr[a]);
        else printf("  raise to %-6d        %5.1f%%\n", st_raise_to_frac(&s, n->frac[a]), 100 * pr[a]);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 5 && !strcmp(argv[1], "match")) return run_match(argv[2], argv[3], atol(argv[4]), argc > 5 ? atoi(argv[5]) : 1);
    if (argc >= 4 && !strcmp(argv[1], "lbr")) return run_lbr(argv[2], atol(argv[3]), argc > 4 ? atoi(argv[4]) : 1);
    if (argc >= 3 && !strcmp(argv[1], "serve")) return run_serve(argv[2]);
    if (argc >= 5 && !strcmp(argv[1], "advise")) return run_advise(argv[2], argv[3], argv[4], argc > 5 ? argv[5] : "");
    fprintf(stderr, "usage: bench match <a> <b> <hands> [threads] | bench lbr <policy> <hands> [threads] | bench serve <policy> | bench advise <policy> <hole> <board> [actions]\n");
    return 1;
}
