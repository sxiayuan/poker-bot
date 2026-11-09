// Real-time subgame solver. From the current real betting state it builds a finer
// betting tree over every possible hand, runs vector-form CFR+ with both players'
// ranges, and plays the result. This is unsafe re-solving: it trusts the ranges
// inferred from the blueprint rather than using a gadget that guarantees no extra
// exploitability, so measure it rather than assuming it helps.
#ifndef SOLVER_H
#define SOLVER_H
#include "poker.h"

#define NCOMBOS 1326
#define N_CHANCE 3
#define MAX_ENTRIES 40000000L
#define ARENA_FLOATS (4L << 20)
#define RANGE_FLOOR 0.002f

static inline int combo_id(int a, int b) { return a < b ? b * (b - 1) / 2 + a : a * (a - 1) / 2 + b; }

typedef struct {
    uint8_t type, player, nact, fold_winner;
    int c[2];
    long offset, block;
    int child[MAXA], to[MAXA];
    uint8_t atype[MAXA];
} SNode;

typedef struct {
    SNode *nodes; int count, cap;
    long entries, cap_entries;
    float *R, *A;
    int n, a[NCOMBOS], b[NCOMBOS], id2h[NCOMBOS];
    int nc, cards[52], *rank, *order, cnt[52];
    uint8_t *valid;
    float *arena; long top;
    int turn_mode, overflow;
} Sub;

static inline float *sub_push(Sub *S, long k) {
    float *p = S->arena + S->top;
    S->top += k;
    if (S->top > ARENA_FLOATS) { fprintf(stderr, "solver arena overflow\n"); exit(1); }
    return p;
}

// Bet sizes as pot fractions. `lr` counts raises made since this street's subtree began.
static inline int sub_sizes(const Sub *S, int street, int lr, float *out) {
    if (S->turn_mode) {
        if (street == 3) { if (lr == 0) { out[0] = 0.75f; return 1; } return 0; }
        if (lr == 0) { out[0] = 0.5f; out[1] = 1.0f; return 2; }
        if (lr == 1) { out[0] = 0.75f; return 1; }
        return 0;
    }
    if (lr == 0) { out[0] = 0.33f; out[1] = 0.66f; out[2] = 1.0f; out[3] = 1.5f; return 4; }
    if (lr == 1) { out[0] = 0.75f; return 1; }
    return 0;
}
static int sub_new(Sub *S) {
    if (S->count == S->cap) {
        S->cap = S->cap ? S->cap * 2 : 1024;
        S->nodes = (SNode *)realloc(S->nodes, S->cap * sizeof(SNode));
    }
    memset(&S->nodes[S->count], 0, sizeof(SNode));
    return S->count++;
}
static int sub_build(Sub *S, State s, int lr, int in_river);
static int sub_chance(Sub *S, State s) {  // deals the river; s is the state after the turn closes
    int id = sub_new(S);
    S->nodes[id].type = N_CHANCE;
    long base = S->entries;
    int ch;
    if (s.done) {
        ch = sub_new(S);
        S->nodes[ch].type = N_SHOWDOWN; S->nodes[ch].c[0] = s.c[0]; S->nodes[ch].c[1] = s.c[1];
    } else ch = sub_build(S, s, 0, 1);
    long block = S->entries - base;
    S->entries = base + block * S->nc;
    S->nodes[id].child[0] = ch; S->nodes[id].block = block;
    return id;
}
static int sub_build(Sub *S, State s, int lr, int in_river) {
    if (s.done == 1) {
        int id = sub_new(S);
        S->nodes[id].type = N_FOLD; S->nodes[id].fold_winner = s.winner;
        S->nodes[id].c[0] = s.c[0]; S->nodes[id].c[1] = s.c[1];
        return id;
    }
    if (s.done == 2 && (s.street == 3 || in_river)) {
        int id = sub_new(S);
        S->nodes[id].type = N_SHOWDOWN; S->nodes[id].c[0] = s.c[0]; S->nodes[id].c[1] = s.c[1];
        return id;
    }
    if (!in_river && (s.done == 2 || s.street == 3) && S->nc > 1) return sub_chance(S, s);
    int id = sub_new(S);
    int nact = 0;
    uint8_t at[MAXA]; int to[MAXA];
    if (st_to_call(&s) > 0) { at[nact] = A_FOLD; to[nact] = 0; nact++; }
    at[nact] = A_CALL; to[nact] = 0; nact++;
    if (st_can_raise(&s)) {
        float sz[4];
        int k = sub_sizes(S, s.street, lr, sz);
        for (int i = 0; i < k && nact < MAXA - 1; i++) {
            int x = st_raise_to_frac(&s, sz[i]);
            if (x >= s.stack * 0.85) continue;
            int dup = 0;
            for (int j = 0; j < nact; j++) if (at[j] == A_RAISE && to[j] == x) dup = 1;
            if (!dup) { at[nact] = A_RAISE; to[nact] = x; nact++; }
        }
        if (lr < 3) { at[nact] = A_RAISE; to[nact] = s.stack; nact++; }
    }
    long off = S->entries;
    S->entries += (long)S->n * nact;
    if (S->entries > MAX_ENTRIES) S->overflow = 1;
    SNode *N = &S->nodes[id];
    N->type = N_PLAYER; N->player = s.to_act; N->nact = nact; N->offset = off;
    for (int i = 0; i < nact; i++) { N->atype[i] = at[i]; N->to[i] = to[i]; }
    for (int i = 0; i < nact; i++) {
        State c = s;
        if (at[i] == A_FOLD) st_fold(&c);
        else if (at[i] == A_CALL) st_call(&c);
        else st_raise(&c, to[i]);
        int ch = sub_build(S, c, at[i] == A_RAISE ? lr + 1 : lr, in_river);
        S->nodes[id].child[i] = ch;
    }
    return id;
}

// Sets up hands and the sorted showdown order for each possible river.
static void sub_cards(Sub *S, const int *board, int nb) {
    uint64_t dead = 0;
    for (int i = 0; i < nb; i++) dead |= 1ULL << board[i];
    S->n = 0;
    for (int i = 0; i < NCOMBOS; i++) S->id2h[i] = -1;
    for (int b = 1; b < 52; b++) for (int a = 0; a < b; a++) {
        if ((dead >> a & 1) || (dead >> b & 1)) continue;
        S->id2h[combo_id(a, b)] = S->n;
        S->a[S->n] = a; S->b[S->n] = b; S->n++;
    }
    S->nc = 0;
    if (nb == 5) S->cards[S->nc++] = -1;
    else for (int c = 0; c < 52; c++) if (!(dead >> c & 1)) S->cards[S->nc++] = c;
    long need = (long)S->nc * S->n;
    S->rank = (int *)realloc(S->rank, need * sizeof(int));
    S->order = (int *)realloc(S->order, need * sizeof(int));
    S->valid = (uint8_t *)realloc(S->valid, need);
    int cards[7];
    for (int i = 0; i < nb; i++) cards[2 + i] = board[i];
    for (int ci = 0; ci < S->nc; ci++) {
        int rc = S->cards[ci];
        if (rc >= 0) cards[2 + nb] = rc;
        int *rk = S->rank + (long)ci * S->n, *ord = S->order + (long)ci * S->n, m = 0;
        uint8_t *va = S->valid + (long)ci * S->n;
        for (int h = 0; h < S->n; h++) {
            va[h] = rc < 0 || (S->a[h] != rc && S->b[h] != rc);
            if (!va[h]) { rk[h] = 0; continue; }
            cards[0] = S->a[h]; cards[1] = S->b[h];
            rk[h] = eval_hand(cards, 7);
            // insertion sort is too slow for ~1000 hands per river; collect, then sort below
            ord[m++] = h;
        }
        S->cnt[ci] = m;
        // Shell sort by rank
        for (int gap = m / 2; gap > 0; gap /= 2)
            for (int i = gap; i < m; i++) {
                int x = ord[i], j = i;
                while (j >= gap && rk[ord[j - gap]] > rk[x]) { ord[j] = ord[j - gap]; j -= gap; }
                ord[j] = x;
            }
    }
}

// Showdown: every hand's value against the opponent's reach, with card removal, in O(n).
static void eval_showdown(const Sub *S, const SNode *N, const float *ro, int ci, float *out) {
    memset(out, 0, S->n * sizeof(float));
    const int *ord = S->order + (long)ci * S->n, *rk = S->rank + (long)ci * S->n;
    int m = S->cnt[ci];
    float amt = (float)N->c[0];
    float card[52] = {0}, total = 0;
    for (int i = 0; i < m;) {
        int j = i;
        while (j < m && rk[ord[j]] == rk[ord[i]]) j++;
        for (int k = i; k < j; k++) { int h = ord[k]; out[h] += amt * (total - card[S->a[h]] - card[S->b[h]]); }
        for (int k = i; k < j; k++) { int h = ord[k]; float r = ro[h]; total += r; card[S->a[h]] += r; card[S->b[h]] += r; }
        i = j;
    }
    memset(card, 0, sizeof card); total = 0;
    for (int i = m - 1; i >= 0;) {
        int j = i;
        while (j >= 0 && rk[ord[j]] == rk[ord[i]]) j--;
        for (int k = i; k > j; k--) { int h = ord[k]; out[h] -= amt * (total - card[S->a[h]] - card[S->b[h]]); }
        for (int k = i; k > j; k--) { int h = ord[k]; float r = ro[h]; total += r; card[S->a[h]] += r; card[S->b[h]] += r; }
        i = j;
    }
}
static void eval_fold(const Sub *S, const SNode *N, int p, const float *ro, int ci, float *out) {
    float amt = N->fold_winner == p ? (float)N->c[1 - p] : -(float)N->c[p];
    float card[52] = {0}, total = 0;
    const uint8_t *va = S->valid + (long)ci * S->n;
    for (int h = 0; h < S->n; h++) { float r = ro[h]; total += r; card[S->a[h]] += r; card[S->b[h]] += r; }
    for (int h = 0; h < S->n; h++)
        out[h] = va[h] ? amt * (total - card[S->a[h]] - card[S->b[h]] + ro[h]) : 0;
}
static inline float rivers_per_pair(const Sub *S) { return (float)(S->nc - 4 + 0); }

static void sub_cfr(Sub *S, int id, int p, const float *rp, const float *ro, int ci, long shift, float t, float *out) {
    const SNode *N = &S->nodes[id];
    int n = S->n;
    if (N->type == N_FOLD) { eval_fold(S, N, p, ro, ci, out); return; }
    if (N->type == N_SHOWDOWN) { eval_showdown(S, N, ro, ci, out); return; }
    long mark = S->top;
    if (N->type == N_CHANCE) {
        float *rp2 = sub_push(S, n), *ro2 = sub_push(S, n), *u = sub_push(S, n);
        memset(out, 0, n * sizeof(float));
        for (int c = 0; c < S->nc; c++) {
            const uint8_t *va = S->valid + (long)c * n;
            for (int h = 0; h < n; h++) { rp2[h] = va[h] ? rp[h] : 0; ro2[h] = va[h] ? ro[h] : 0; }
            sub_cfr(S, N->child[0], p, rp2, ro2, c, shift + c * N->block, t, u);
            for (int h = 0; h < n; h++) out[h] += u[h];
        }
        float d = 1.0f / rivers_per_pair(S);
        for (int h = 0; h < n; h++) out[h] *= d;
        S->top = mark;
        return;
    }
    int na = N->nact;
    float *R = S->R + N->offset + shift, *A = S->A + N->offset + shift;
    float *sig = sub_push(S, (long)n * na), *next = sub_push(S, n), *u = sub_push(S, (long)n * na);
    for (int h = 0; h < n; h++) {
        const float *rh = R + (long)h * na;
        float sum = 0;
        for (int a = 0; a < na; a++) if (rh[a] > 0) sum += rh[a];
        float *sh = sig + (long)h * na;
        if (sum > 0) for (int a = 0; a < na; a++) sh[a] = rh[a] > 0 ? rh[a] / sum : 0;
        else for (int a = 0; a < na; a++) sh[a] = 1.0f / na;
    }
    memset(out, 0, n * sizeof(float));
    if (N->player == p) {
        for (int a = 0; a < na; a++) {
            for (int h = 0; h < n; h++) next[h] = rp[h] * sig[(long)h * na + a];
            sub_cfr(S, N->child[a], p, next, ro, ci, shift, t, u + (long)a * n);
            for (int h = 0; h < n; h++) out[h] += sig[(long)h * na + a] * u[(long)a * n + h];
        }
        for (int h = 0; h < n; h++) {
            float *rh = R + (long)h * na, *ah = A + (long)h * na, w = t * rp[h];
            for (int a = 0; a < na; a++) {
                float v = rh[a] + u[(long)a * n + h] - out[h];
                rh[a] = v > 0 ? v : 0;  // CFR+ floor
                ah[a] += w * sig[(long)h * na + a];
            }
        }
    } else {
        for (int a = 0; a < na; a++) {
            for (int h = 0; h < n; h++) next[h] = ro[h] * sig[(long)h * na + a];
            sub_cfr(S, N->child[a], p, rp, next, ci, shift, t, u);
            for (int h = 0; h < n; h++) out[h] += u[h];
        }
    }
    S->top = mark;
}

static inline void sub_avg(const Sub *S, const SNode *N, long shift, int h, float *out) {
    const float *A = S->A + N->offset + shift + (long)h * N->nact;
    float sum = 0;
    for (int a = 0; a < N->nact; a++) sum += A[a];
    for (int a = 0; a < N->nact; a++) out[a] = sum > 0 ? A[a] / sum : 1.0f / N->nact;
}

// Prepares a solve from `root`. Returns 0 if the tree would be too big to hold in memory.
static int sub_prepare(Sub *S, const State *root, const int *board, int turn_mode) {
    int nb = root->street + 2;
    S->count = 0; S->entries = 0; S->top = 0; S->overflow = 0; S->turn_mode = turn_mode;
    if (!S->arena) S->arena = (float *)malloc(ARENA_FLOATS * sizeof(float));
    sub_cards(S, board, nb);
    sub_build(S, *root, 0, nb == 5);
    if (S->overflow || S->entries > MAX_ENTRIES) return 0;
    if (S->entries > S->cap_entries) {
        S->R = (float *)realloc(S->R, S->entries * sizeof(float));
        S->A = (float *)realloc(S->A, S->entries * sizeof(float));
        S->cap_entries = S->entries;
    }
    memset(S->R, 0, S->entries * sizeof(float));
    memset(S->A, 0, S->entries * sizeof(float));
    return 1;
}
static void sub_iterate(Sub *S, const float *w0, const float *w1, int from, int to) {
    float *out = sub_push(S, S->n);
    for (int t = from; t <= to; t++)
        for (int p = 0; p < 2; p++)
            sub_cfr(S, 0, p, p ? w1 : w0, p ? w0 : w1, 0, 0, (float)t, out);
    S->top = 0;
}

// Best-response values against the averaged strategy, used to verify convergence.
static void sub_br(Sub *S, int id, int p, const float *ro, int ci, long shift, float *out) {
    const SNode *N = &S->nodes[id];
    int n = S->n;
    if (N->type == N_FOLD) { eval_fold(S, N, p, ro, ci, out); return; }
    if (N->type == N_SHOWDOWN) { eval_showdown(S, N, ro, ci, out); return; }
    long mark = S->top;
    float *next = sub_push(S, n), *u = sub_push(S, n);
    if (N->type == N_CHANCE) {
        memset(out, 0, n * sizeof(float));
        for (int c = 0; c < S->nc; c++) {
            const uint8_t *va = S->valid + (long)c * n;
            for (int h = 0; h < n; h++) next[h] = va[h] ? ro[h] : 0;
            sub_br(S, N->child[0], p, next, c, shift + c * N->block, u);
            for (int h = 0; h < n; h++) out[h] += u[h];
        }
        float d = 1.0f / rivers_per_pair(S);
        for (int h = 0; h < n; h++) out[h] *= d;
    } else if (N->player == p) {
        for (int h = 0; h < n; h++) out[h] = -1e30f;
        for (int a = 0; a < N->nact; a++) {
            sub_br(S, N->child[a], p, ro, ci, shift, u);
            for (int h = 0; h < n; h++) if (u[h] > out[h]) out[h] = u[h];
        }
    } else {
        memset(out, 0, n * sizeof(float));
        float st[MAXA];
        float *sig = sub_push(S, (long)n * N->nact);
        for (int h = 0; h < n; h++) { sub_avg(S, N, shift, h, st); for (int a = 0; a < N->nact; a++) sig[(long)h * N->nact + a] = st[a]; }
        for (int a = 0; a < N->nact; a++) {
            for (int h = 0; h < n; h++) next[h] = ro[h] * sig[(long)h * N->nact + a];
            sub_br(S, N->child[a], p, next, ci, shift, u);
            for (int h = 0; h < n; h++) out[h] += u[h];
        }
    }
    S->top = mark;
}
// Exploitability in chips per hand pair: 0 at a perfect equilibrium of this subgame.
static double sub_exploitability(Sub *S, const float *w0, const float *w1) {
    float *out = sub_push(S, S->n);
    double total = 0, pairs = 0;
    for (int p = 0; p < 2; p++) {
        const float *wp = p ? w1 : w0, *wo = p ? w0 : w1;
        sub_br(S, 0, p, wo, 0, 0, out);
        for (int h = 0; h < S->n; h++) total += (double)wp[h] * out[h];
    }
    for (int h = 0; h < S->n; h++) for (int o = 0; o < S->n; o++)
        if (S->a[h] != S->a[o] && S->a[h] != S->b[o] && S->b[h] != S->a[o] && S->b[h] != S->b[o]) pairs += (double)w0[h] * w1[o];
    S->top = 0;
    return total / pairs;
}

// ---------- Agent that plays blueprint early and re-solves from a chosen street ----------
typedef struct {
    const Policy *P;
    int from, iters;
    CfrView v;
    float w[2][NCOMBOS];
    int log_node[64], log_act[64], log_n, log_done;
    int buckets[3][NCOMBOS], bucket_ok[3];
    Sub S;
    int cur;     // node in the latest solve matching the real game, or -1
    int pending; // action picked by the latest solve, or -1
    int hero;
} SolverView;

static void sv_reset(SolverView *sv, const Policy *P, int from, int iters) {
    sv->P = P; sv->from = from; sv->iters = iters;
    cfr_reset(&sv->v, P);
    for (int p = 0; p < 2; p++) for (int i = 0; i < NCOMBOS; i++) sv->w[p][i] = 1;
    sv->log_n = sv->log_done = 0;
    sv->bucket_ok[0] = sv->bucket_ok[1] = sv->bucket_ok[2] = 0;
    sv->cur = -1; sv->pending = -1;
}
static void sv_replay(SolverView *sv, const int *board, Rng *r) {
    const Policy *P = sv->P;
    for (; sv->log_done < sv->log_n; sv->log_done++) {
        const Node *n = &P->tree.n[sv->log_node[sv->log_done]];
        int act = sv->log_act[sv->log_done], st = n->street, nb = st ? st + 2 : 0;
        uint64_t dead = 0;
        for (int i = 0; i < nb; i++) dead |= 1ULL << board[i];
        if (st && !sv->bucket_ok[st - 1]) {
            for (int y = 1; y < 52; y++) for (int x = 0; x < y; x++) {
                if ((dead >> x & 1) || (dead >> y & 1)) continue;
                int h[2] = {x, y};
                sv->buckets[st - 1][combo_id(x, y)] = bucket_of(&P->T, st, h, board, 100, r);
            }
            sv->bucket_ok[st - 1] = 1;
        }
        float *w = sv->w[n->player];
        double pr[MAXA];
        for (int y = 1; y < 52; y++) for (int x = 0; x < y; x++) {
            int id = combo_id(x, y);
            if ((dead >> x & 1) || (dead >> y & 1)) { w[id] = 0; continue; }
            if (w[id] <= 0) continue;
            int h[2] = {x, y};
            policy_probs(P, n, st ? sv->buckets[st - 1][id] : preflop_index(h), pr);
            w[id] *= (float)pr[act];
        }
    }
}
static Decision sv_decide(SolverView *sv, const State *s, const int *hole, const int *board, Rng *r) {
    sv->pending = -1;
    if (s->street < sv->from) return cfr_decide(&sv->v, s, hole, board, r);
    sv_replay(sv, board, r);
    Sub *S = &sv->S;
    sv->hero = s->to_act;
    if (!sub_prepare(S, s, board, s->street == 2)) return cfr_decide(&sv->v, s, hole, board, r);
    float w[2][NCOMBOS];
    for (int p = 0; p < 2; p++) {
        double mx = 0;
        for (int h = 0; h < S->n; h++) { float x = sv->w[p][combo_id(S->a[h], S->b[h])]; if (x > mx) mx = x; }
        float fl = mx > 0 ? (float)(mx * RANGE_FLOOR) : 1;
        for (int h = 0; h < S->n; h++) { float x = sv->w[p][combo_id(S->a[h], S->b[h])]; w[p][h] = x > fl ? x : fl; }
    }
    sub_iterate(S, w[0], w[1], 1, sv->iters);
    const SNode *N = &S->nodes[0];
    int me = S->id2h[combo_id(hole[0], hole[1])];
    float st[MAXA];
    sub_avg(S, N, 0, me, st);
    double u = rng_float(r), acc = 0;
    int a = N->nact - 1;
    for (int i = 0; i < N->nact; i++) { acc += st[i]; if (u < acc) { a = i; break; } }
    // Our own range narrows by what the solved strategy does with each hand.
    for (int h = 0; h < S->n; h++) {
        float sh[MAXA];
        sub_avg(S, N, 0, h, sh);
        sv->w[s->to_act][combo_id(S->a[h], S->b[h])] *= sh[a];
    }
    sv->pending = a;
    sv->cur = N->child[a];
    Decision d = {N->atype[a], N->to[a]};
    return d;
}
// Call for every action, with the state from just before it was applied.
static void sv_observe(SolverView *sv, const State *s, Decision d, int own, Rng *r) {
    int node = sv->v.node;
    int solved_own = own && sv->pending >= 0;
    if (solved_own) sv->v.forced = -1;
    int act = cfr_observe(&sv->v, s, d, own, r);
    if (solved_own) { sv->pending = -1; return; }  // range already updated in sv_decide
    Sub *S = &sv->S;
    if (sv->cur >= 0 && S->nodes[sv->cur].type == N_PLAYER && S->nodes[sv->cur].player == s->to_act) {
        // Opponent moved inside the latest solve: read their range from that solution.
        const SNode *N = &S->nodes[sv->cur];
        int a = -1, best = 1 << 30;
        for (int i = 0; i < N->nact; i++) {
            if (N->atype[i] != d.type) continue;
            int gap = d.type == A_RAISE ? abs(N->to[i] - d.to) : 0;
            if (gap < best) { best = gap; a = i; }
        }
        if (a >= 0) {
            float st[MAXA];
            for (int h = 0; h < S->n; h++) {
                sub_avg(S, N, 0, h, st);
                sv->w[s->to_act][combo_id(S->a[h], S->b[h])] *= st[a];
            }
            sv->cur = N->child[a];
            return;
        }
    }
    sv->cur = -1;
    if (node >= 0 && act >= 0 && sv->log_n < 64) {
        sv->log_node[sv->log_n] = node;
        sv->log_act[sv->log_n] = act;
        sv->log_n++;
    }
}
#endif
