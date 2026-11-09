// Shared engine: cards, hand evaluator, equity, abstraction buckets, betting
// rules, betting tree, and policy lookup. Heads-up no-limit Texas hold'em.
#ifndef POKER_H
#define POKER_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------- RNG ----------
typedef struct { uint64_t s; } Rng;
static inline uint64_t rng_u64(Rng *r) {  // splitmix64
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static inline uint32_t rng_int(Rng *r, uint32_t n) { return (uint32_t)(((rng_u64(r) >> 32) * (uint64_t)n) >> 32); }
static inline double rng_float(Rng *r) { return (rng_u64(r) >> 11) * (1.0 / 9007199254740992.0); }

// ---------- Cards: 0..51, rank = c>>2 (0=2 .. 12=A), suit = c&3 ----------
static inline int card_parse(const char *s) {
    const char *ranks = "23456789TJQKA", *suits = "cdhs";
    const char *r = strchr(ranks, s[0] >= 'a' ? s[0] - 32 : s[0]);
    const char *u = strchr(suits, s[1] <= 'Z' ? s[1] + 32 : s[1]);
    if (!s[0] || !s[1] || !r || !u) return -1;
    return (int)(r - ranks) * 4 + (int)(u - suits);
}
static inline void card_str(int c, char *out) {
    out[0] = "23456789TJQKA"[c >> 2]; out[1] = "cdhs"[c & 3]; out[2] = 0;
}

// ---------- Hand evaluator: higher is better, works for 5..7 cards ----------
enum { HIGH_CARD, PAIR, TWO_PAIR, TRIPS, STRAIGHT, FLUSH, FULL_HOUSE, QUADS, STRAIGHT_FLUSH };
static inline int straight_high(uint32_t m) {  // returns high rank or -1
    uint32_t w = (m << 1) | ((m >> 12) & 1);   // bit 0 = wheel ace
    uint32_t s = w & (w >> 1) & (w >> 2) & (w >> 3) & (w >> 4);
    return s ? (31 - __builtin_clz(s)) + 3 : -1;
}
static inline int top_bits(uint32_t m, int n) {  // packs the n highest ranks as nibbles
    int k = 0;
    for (int i = 0; i < n; i++) {
        int r = m ? 31 - __builtin_clz(m) : 0;
        k = (k << 4) | r;
        if (m) m &= ~(1u << r);
    }
    return k;
}
#define HV(cat, key) (((cat) << 20) | (key))
static inline int eval_hand(const int *c, int n) {
    uint32_t su[4] = {0, 0, 0, 0}, one = 0, two = 0, three = 0, four = 0;
    for (int i = 0; i < n; i++) {
        uint32_t b = 1u << (c[i] >> 2);
        su[c[i] & 3] |= b;
        four |= three & b; three |= two & b; two |= one & b; one |= b;
    }
    for (int s = 0; s < 4; s++)
        if (__builtin_popcount(su[s]) >= 5) {
            int h = straight_high(su[s]);
            if (h >= 0) return HV(STRAIGHT_FLUSH, h);
            if (!four && !(three & ~four && (two & ~(1u << (31 - __builtin_clz(three)))))) return HV(FLUSH, top_bits(su[s], 5));
        }
    if (four) {
        int q = 31 - __builtin_clz(four);
        return HV(QUADS, (q << 4) | top_bits(one & ~(1u << q), 1));
    }
    if (three) {
        int t = 31 - __builtin_clz(three);
        uint32_t rest = two & ~(1u << t);
        if (rest) return HV(FULL_HOUSE, (t << 4) | (31 - __builtin_clz(rest)));
    }
    int st = straight_high(one);
    if (st >= 0) return HV(STRAIGHT, st);
    if (three) {
        int t = 31 - __builtin_clz(three);
        return HV(TRIPS, (t << 8) | top_bits(one & ~(1u << t), 2));
    }
    if (two) {
        int p1 = 31 - __builtin_clz(two);
        uint32_t r2 = two & ~(1u << p1);
        if (r2) {
            int p2 = 31 - __builtin_clz(r2);
            return HV(TWO_PAIR, (p1 << 8) | (p2 << 4) | top_bits(one & ~(1u << p1) & ~(1u << p2), 1));
        }
        return HV(PAIR, (p1 << 12) | top_bits(one & ~(1u << p1), 3));
    }
    return HV(HIGH_CARD, top_bits(one, 5));
}

// ---------- Equity features ----------
// ehs = chance of beating a random hand at showdown; ihs = chance of being ahead now.
static inline void hand_strength(const int *hole, const int *board, int nb, int samples, Rng *r, double *ehs, double *ihs) {
    uint64_t used = (1ULL << hole[0]) | (1ULL << hole[1]);
    for (int i = 0; i < nb; i++) used |= 1ULL << board[i];
    int cards[7]; cards[0] = hole[0]; cards[1] = hole[1];
    for (int i = 0; i < nb; i++) cards[2 + i] = board[i];
    int ocards[7];
    for (int i = 0; i < nb; i++) ocards[2 + i] = board[i];
    if (nb == 5) {  // river: enumerate all opponent hands exactly
        int me = eval_hand(cards, 7);
        double w = 0; int n = 0;
        for (int a = 0; a < 52; a++) {
            if (used >> a & 1) continue;
            ocards[0] = a;
            for (int b = a + 1; b < 52; b++) {
                if (used >> b & 1) continue;
                ocards[1] = b;
                int o = eval_hand(ocards, 7);
                w += me > o ? 1 : me == o ? 0.5 : 0; n++;
            }
        }
        *ehs = *ihs = w / n;
        return;
    }
    int me_now = nb ? eval_hand(cards, 2 + nb) : 0;
    double w = 0, wn = 0;
    for (int i = 0; i < samples; i++) {
        uint64_t u = used;
        int d[7], k = 0;
        int need = 2 + (5 - nb);
        while (k < need) { int x = rng_int(r, 52); if (u >> x & 1) continue; u |= 1ULL << x; d[k++] = x; }
        ocards[0] = d[0]; ocards[1] = d[1];
        if (nb) {
            int on = eval_hand(ocards, 2 + nb);
            wn += me_now > on ? 1 : me_now == on ? 0.5 : 0;
        }
        for (int j = 2; j < need; j++) { cards[2 + nb + j - 2] = d[j]; ocards[2 + nb + j - 2] = d[j]; }
        int m = eval_hand(cards, 7), o = eval_hand(ocards, 7);
        w += m > o ? 1 : m == o ? 0.5 : 0;
    }
    *ehs = w / samples;
    *ihs = nb ? wn / samples : *ehs;
}

// ---------- Card abstraction ----------
#define NE 30   // strength bands on flop/turn
#define NP 5    // draw-potential bands per strength band
#define NRIV 200
#define NB_PRE 169
static const int STREET_BUCKETS[4] = {NB_PRE, NE * NP, NE * NP, NRIV};
#define BUCKET_SAMPLES 200

typedef struct {
    float ehs_q[2][NE - 1];        // street 1,2
    float pot_q[2][NE][NP - 1];
    float riv_q[NRIV - 1];
} Thresholds;

static inline int preflop_index(const int *h) {
    int a = h[0] >> 2, b = h[1] >> 2, hi = a > b ? a : b, lo = a > b ? b : a;
    return (h[0] & 3) == (h[1] & 3) ? hi * 13 + lo : lo * 13 + hi;
}
static inline int q_lookup(const float *q, int n, float x) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = (lo + hi) / 2; if (x > q[m]) lo = m + 1; else hi = m; }
    return lo;
}
static inline int bucket_of(const Thresholds *T, int street, const int *hole, const int *board, int samples, Rng *r) {
    if (street == 0) return preflop_index(hole);
    double e, i;
    hand_strength(hole, board, street + 2, samples, r, &e, &i);
    if (street == 3) return q_lookup(T->riv_q, NRIV - 1, (float)e);
    int s = street - 1;
    int be = q_lookup(T->ehs_q[s], NE - 1, (float)e);
    int bp = q_lookup(T->pot_q[s][be], NP - 1, (float)(e - i));
    return be * NP + bp;
}

// ---------- Betting rules ----------
#define SB 50
#define BB 100
typedef struct {
    int stack, street, to_act, c[2], nacts, raises, last_inc;
    int done, winner;  // done: 0 live, 1 fold, 2 showdown; winner valid when fold
} State;

static inline void st_init(State *s, int stack) {
    memset(s, 0, sizeof *s);
    s->stack = stack; s->c[0] = SB; s->c[1] = BB; s->last_inc = BB;
}
static inline int st_level(const State *s) { return s->c[0] > s->c[1] ? s->c[0] : s->c[1]; }
static inline int st_to_call(const State *s) { return st_level(s) - s->c[s->to_act]; }
static inline int st_pot(const State *s) { return s->c[0] + s->c[1]; }
static inline int st_can_raise(const State *s) { return st_level(s) < s->stack && s->c[s->to_act] < s->stack; }
static inline int st_min_raise_to(const State *s) {
    int m = st_level(s) + s->last_inc;
    return m > s->stack ? s->stack : m;
}
static inline void st_fold(State *s) { s->done = 1; s->winner = 1 - s->to_act; }
static inline void st_call(State *s) {
    s->c[s->to_act] = st_level(s);
    s->nacts++;
    if (s->nacts >= 2) {
        if (s->c[0] >= s->stack || s->street == 3) { s->done = 2; return; }
        s->street++; s->to_act = 1; s->nacts = 0; s->raises = 0; s->last_inc = BB;
        return;
    }
    if (st_level(s) >= s->stack) { s->done = 2; return; }  // limp cannot be all-in in practice
    s->to_act = 1 - s->to_act;
}
static inline void st_raise(State *s, int to) {
    int lvl = st_level(s), mn = st_min_raise_to(s);
    if (to < mn) to = mn;
    if (to > s->stack) to = s->stack;
    if (to - lvl >= s->last_inc) s->last_inc = to - lvl;
    s->c[s->to_act] = to;
    s->nacts++; s->raises++;
    s->to_act = 1 - s->to_act;
}
static inline int st_raise_to_frac(const State *s, double f) {
    int lvl = st_level(s);
    int to = lvl + (int)(f * (st_pot(s) + st_to_call(s)) + 0.5);
    int mn = st_min_raise_to(s);
    if (to < mn) to = mn;
    if (to > s->stack) to = s->stack;
    return to;
}

// ---------- Betting tree (action abstraction) ----------
enum { N_PLAYER, N_FOLD, N_SHOWDOWN };
enum { A_FOLD, A_CALL, A_RAISE };
#define MAXA 7
#define ALLIN_FRAC 1e9f
typedef struct {
    uint8_t type, player, street, nact, fold_winner;
    int offset, c[2];
    int child[MAXA];
    uint8_t atype[MAXA];
    float frac[MAXA];
} Node;

typedef struct {
    Node *n; int count, cap;
    int entries;  // total (bucket, action) slots
    int stack;
} Tree;

// Bet sizes are pot fractions, chosen by street and number of prior raises.
// An all-in is always offered in addition to these.
static inline int sizes_for(int street, int raises, float *out) {
    static const float P0[] = {0.75f, 1.5f}, P1[] = {1.0f, 2.0f}, P2[] = {0.75f};
    static const float F0[] = {0.33f, 0.75f, 1.25f}, F1[] = {0.75f, 1.5f};
    const float *p = NULL; int k = 0;
    if (street == 0) {
        if (raises == 0) { p = P0; k = 2; }
        else if (raises == 1) { p = P1; k = 2; }
        else if (raises == 2) { p = P2; k = 1; }
    } else {
        if (raises == 0) { p = F0; k = 3; }
        else if (raises == 1) { p = F1; k = 2; }
    }
    for (int i = 0; i < k; i++) out[i] = p[i];
    return k;
}
#define MAX_RAISES_PRE 5
#define MAX_RAISES_POST 3

static int tree_new_node(Tree *t) {
    if (t->count == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 1 << 16;
        t->n = (Node *)realloc(t->n, t->cap * sizeof(Node));
    }
    memset(&t->n[t->count], 0, sizeof(Node));
    return t->count++;
}
static int tree_build_rec(Tree *t, State s) {
    int id = tree_new_node(t);
    t->n[id].c[0] = s.c[0]; t->n[id].c[1] = s.c[1]; t->n[id].street = s.street;
    if (s.done) {
        t->n[id].type = s.done == 1 ? N_FOLD : N_SHOWDOWN;
        t->n[id].fold_winner = s.winner;
        return id;
    }
    int nact = 0;
    uint8_t at[MAXA]; float fr[MAXA]; int to[MAXA];
    if (st_to_call(&s) > 0) { at[nact] = A_FOLD; fr[nact] = 0; to[nact] = 0; nact++; }
    at[nact] = A_CALL; fr[nact] = 0; to[nact] = 0; nact++;
    int maxr = s.street == 0 ? MAX_RAISES_PRE : MAX_RAISES_POST;
    if (st_can_raise(&s) && s.raises < maxr) {
        float sz[4];
        int k = sizes_for(s.street, s.raises, sz);
        for (int i = 0; i < k; i++) {
            int x = st_raise_to_frac(&s, sz[i]);
            if (x >= s.stack * 0.8) continue;  // effectively an all-in
            int dup = 0;
            for (int j = 0; j < nact; j++) if (at[j] == A_RAISE && to[j] == x) dup = 1;
            if (dup) continue;
            at[nact] = A_RAISE; fr[nact] = sz[i]; to[nact] = x; nact++;
        }
        at[nact] = A_RAISE; fr[nact] = ALLIN_FRAC; to[nact] = s.stack; nact++;
    }
    int street = s.street, player = s.to_act;
    int entries = STREET_BUCKETS[street] * nact;
    int off = t->entries; t->entries += entries;
    t->n[id].type = N_PLAYER; t->n[id].player = player; t->n[id].nact = nact; t->n[id].offset = off;
    for (int i = 0; i < nact; i++) { t->n[id].atype[i] = at[i]; t->n[id].frac[i] = fr[i]; }
    for (int i = 0; i < nact; i++) {
        State c = s;
        if (at[i] == A_FOLD) st_fold(&c);
        else if (at[i] == A_CALL) st_call(&c);
        else st_raise(&c, to[i]);
        int ch = tree_build_rec(t, c);
        t->n[id].child[i] = ch;  // realloc-safe: index after the recursion
    }
    return id;
}
static inline void tree_build(Tree *t, int stack) {
    memset(t, 0, sizeof *t);
    t->stack = stack;
    State s; st_init(&s, stack);
    tree_build_rec(t, s);
}

// ---------- Trained policy ----------
typedef struct { Tree tree; uint8_t *p; Thresholds T; } Policy;

static inline int load_thresholds(Thresholds *T, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(T, sizeof *T, 1, f) == 1;
    fclose(f);
    return ok;
}
static inline Policy *policy_load(const char *path, const char *thresh_path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return NULL; }
    int hdr[2];
    if (fread(hdr, sizeof(int), 2, f) != 2) { fclose(f); return NULL; }
    Policy *P = (Policy *)calloc(1, sizeof(Policy));
    tree_build(&P->tree, hdr[0]);
    if (P->tree.entries != hdr[1]) { fprintf(stderr, "policy does not match the tree\n"); fclose(f); return NULL; }
    P->p = (uint8_t *)malloc(hdr[1]);
    size_t got = fread(P->p, 1, hdr[1], f);
    fclose(f);
    if (got != (size_t)hdr[1] || !load_thresholds(&P->T, thresh_path)) { fprintf(stderr, "bad policy or thresholds\n"); return NULL; }
    return P;
}
// Action probabilities at a node/bucket; rarely used actions are purified away.
static inline void policy_probs(const Policy *P, const Node *n, int bucket, double *out) {
    const uint8_t *q = P->p + n->offset + bucket * n->nact;
    double sum = 0;
    for (int a = 0; a < n->nact; a++) { out[a] = q[a] >= 13 ? q[a] : 0; sum += out[a]; }
    if (sum <= 0) { for (int a = 0; a < n->nact; a++) out[a] = 1.0 / n->nact; return; }
    for (int a = 0; a < n->nact; a++) out[a] /= sum;
}
// Maps a real bet (as a pot fraction) to an abstract action, randomizing between the
// two neighbouring sizes with the pseudo-harmonic rule. Returns -1 if no raise exists.
static inline double node_action_frac(const Node *n, int a, const State *s) {
    double full = (double)(s->stack - st_level(s)) / (st_pot(s) + st_to_call(s));
    return n->frac[a] >= ALLIN_FRAC ? full : n->frac[a];
}
static inline int map_raise(const Node *n, const State *s, int raise_to, Rng *r) {
    double x = (double)(raise_to - st_level(s)) / (st_pot(s) + st_to_call(s));
    int lo = -1, hi = -1; double fa = -1, fb = 1e18;
    for (int a = 0; a < n->nact; a++) {
        if (n->atype[a] != A_RAISE) continue;
        double f = node_action_frac(n, a, s);
        if (f <= x && f > fa) { fa = f; lo = a; }
        if (f >= x && f < fb) { fb = f; hi = a; }
    }
    if (lo < 0 && hi < 0) return -1;
    if (lo < 0) return hi;
    if (hi < 0 || lo == hi) return lo;
    double pa = (fb - x) * (1 + fa) / ((fb - fa) * (1 + x));
    return rng_float(r) < pa ? lo : hi;
}

// ---------- A decision and the CFR agent's per-hand view ----------
typedef struct { int type, to; } Decision;  // to = raise-to total for A_RAISE

typedef struct {
    const Policy *P;
    int node;        // -1 once the real game has left the abstract tree
    int cached_street, bucket;
    int forced;      // abstract action index picked for own move, or -1
} CfrView;

static inline void cfr_reset(CfrView *v, const Policy *P) { v->P = P; v->node = 0; v->cached_street = -1; v->forced = -1; }

static inline Decision fallback_decide(const State *s, const int *hole, const int *board, Rng *r) {
    Decision d = {A_CALL, 0};
    int tc = st_to_call(s);
    if (tc == 0) return d;
    double e, i;
    hand_strength(hole, board, s->street ? s->street + 2 : 0, 200, r, &e, &i);
    if (e * 0.85 < (double)tc / (st_pot(s) + tc)) d.type = A_FOLD;
    return d;
}
static inline Decision cfr_decide(CfrView *v, const State *s, const int *hole, const int *board, Rng *r) {
    v->forced = -1;
    const Tree *t = &v->P->tree;
    if (v->node < 0 || t->n[v->node].type != N_PLAYER || t->n[v->node].street != s->street || t->n[v->node].player != s->to_act)
        { v->node = -1; return fallback_decide(s, hole, board, r); }
    const Node *n = &t->n[v->node];
    if (v->cached_street != s->street) {
        v->bucket = bucket_of(&v->P->T, s->street, hole, board, BUCKET_SAMPLES, r);
        v->cached_street = s->street;
    }
    double pr[MAXA];
    policy_probs(v->P, n, v->bucket, pr);
    double u = rng_float(r), acc = 0;
    int a = n->nact - 1;
    for (int i = 0; i < n->nact; i++) { acc += pr[i]; if (u < acc) { a = i; break; } }
    v->forced = a;
    Decision d;
    d.type = n->atype[a]; d.to = 0;
    if (d.type == A_RAISE) {
        if (!st_can_raise(s)) { d.type = A_CALL; return d; }
        d.to = n->frac[a] >= ALLIN_FRAC ? s->stack : st_raise_to_frac(s, n->frac[a]);
    }
    return d;
}
// Call for every action in the hand with the state from just before it was applied.
// Returns the abstract action taken, or -1 if the hand has left the tree.
static inline int cfr_observe(CfrView *v, const State *s, Decision d, int own, Rng *r) {
    if (v->node < 0) return -1;
    const Node *n = &v->P->tree.n[v->node];
    if (n->type != N_PLAYER) { v->node = -1; return -1; }
    int a = -1;
    if (own && v->forced >= 0) a = v->forced;
    else if (d.type == A_RAISE) a = map_raise(n, s, d.to, r);
    else for (int i = 0; i < n->nact; i++) if (n->atype[i] == d.type) a = i;
    v->node = a < 0 ? -1 : n->child[a];
    return a;
}

// Plays out a dealt hand's payoff for seat `p` once the state is terminal.
static inline int showdown_payoff(const State *s, const int *h0, const int *h1, const int *board, int p) {
    if (s->done == 1) return s->winner == p ? s->c[1 - p] : -s->c[p];
    int a[7], b[7];
    a[0] = h0[0]; a[1] = h0[1]; b[0] = h1[0]; b[1] = h1[1];
    for (int i = 0; i < 5; i++) { a[2 + i] = board[i]; b[2 + i] = board[i]; }
    int x = eval_hand(a, 7), y = eval_hand(b, 7);
    if (x == y) return 0;
    int w = x > y ? 0 : 1;
    return w == p ? s->c[1 - p] : -s->c[p];
}
#endif
