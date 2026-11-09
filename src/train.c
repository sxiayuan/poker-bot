// Trains a blueprint strategy with external-sampling MCCFR plus linear discounting.
// usage: train <stack_chips> <seconds> <out.policy> [pool_deals] [threads] [snapshot_seconds]
#include <pthread.h>
#include <time.h>
#include "poker.h"

#define THRESH_FILE "data/thresholds.bin"
#define THRESH_SAMPLES 200000

typedef struct { uint8_t pre[2], b[2][3]; int8_t win; } Deal;

static Tree tree;
static float *R, *A;
static Deal *pool;
static long pool_n;
static Thresholds T;
static int nthreads;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void deal_cards(Rng *r, int *h0, int *h1, int *board) {
    uint64_t u = 0; int d[9], k = 0;
    while (k < 9) { int x = rng_int(r, 52); if (u >> x & 1) continue; u |= 1ULL << x; d[k++] = x; }
    h0[0] = d[0]; h0[1] = d[1]; h1[0] = d[2]; h1[1] = d[3];
    for (int i = 0; i < 5; i++) board[i] = d[4 + i];
}

// ---------- thresholds ----------
static int cmpf(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }
// More resolution near the top of the strength range, where decisions are most sensitive.
static double skew_q(int i, int n) { return 1.0 - pow(1.0 - (double)i / n, 1.5); }

static void *thresh_worker(void *arg) {
    float **a = (float **)arg;
    float *eh = a[0], *pt = a[1];
    long street = (long)a[2], lo = (long)a[3], hi = (long)a[4];
    Rng r = {(uint64_t)street * 77777 + lo + 1};
    for (long i = lo; i < hi; i++) {
        int h0[2], h1[2], b[5]; double e, ih;
        deal_cards(&r, h0, h1, b);
        hand_strength(h0, b, (int)street + 2, 400, &r, &e, &ih);
        eh[i] = (float)e; pt[i] = (float)(e - ih);
    }
    return NULL;
}
static void build_thresholds(void) {
    int n = THRESH_SAMPLES;
    typedef struct { float e, p; } EP;
    float *eh = malloc(n * sizeof(float)), *pt = malloc(n * sizeof(float));
    EP *pairs = malloc(n * sizeof(EP));
    for (long st = 1; st <= 3; st++) {
        pthread_t th[64]; float *args[64][5];
        for (int t = 0; t < nthreads; t++) {
            args[t][0] = eh; args[t][1] = pt; args[t][2] = (float *)st;
            args[t][3] = (float *)(long)(n * (long)t / nthreads); args[t][4] = (float *)(long)(n * (long)(t + 1) / nthreads);
            pthread_create(&th[t], NULL, thresh_worker, args[t]);
        }
        for (int t = 0; t < nthreads; t++) pthread_join(th[t], NULL);
        float *sorted = malloc(n * sizeof(float));
        memcpy(sorted, eh, n * sizeof(float));
        qsort(sorted, n, sizeof(float), cmpf);
        if (st == 3) {
            for (int i = 1; i < NRIV; i++) T.riv_q[i - 1] = sorted[(int)(skew_q(i, NRIV) * (n - 1))];
        } else {
            float *eq = T.ehs_q[st - 1];
            for (int i = 1; i < NE; i++) eq[i - 1] = sorted[(int)(skew_q(i, NE) * (n - 1))];
            for (int be = 0; be < NE; be++) {
                int m = 0;
                for (int i = 0; i < n; i++) if (q_lookup(eq, NE - 1, eh[i]) == be) sorted[m++] = pt[i];
                qsort(sorted, m, sizeof(float), cmpf);
                for (int j = 1; j < NP; j++) T.pot_q[st - 1][be][j - 1] = m ? sorted[(int)((double)j / NP * (m - 1))] : 0;
            }
        }
        free(sorted);
    }
    free(eh); free(pt); free(pairs);
    FILE *f = fopen(THRESH_FILE, "wb");
    if (!f) { perror(THRESH_FILE); exit(1); }
    fwrite(&T, sizeof T, 1, f);
    fclose(f);
}

// ---------- deal pool ----------
typedef struct { long lo, hi; } Span;
static void *pool_worker(void *arg) {
    Span *s = (Span *)arg;
    Rng r = {(uint64_t)s->lo * 31 + 99};
    for (long i = s->lo; i < s->hi; i++) {
        int h[2][2], b[5];
        deal_cards(&r, h[0], h[1], b);
        Deal *d = &pool[i];
        for (int p = 0; p < 2; p++) {
            d->pre[p] = preflop_index(h[p]);
            for (int st = 1; st <= 3; st++) d->b[p][st - 1] = bucket_of(&T, st, h[p], b, BUCKET_SAMPLES, &r);
        }
        int a[7] = {h[0][0], h[0][1], b[0], b[1], b[2], b[3], b[4]};
        int c[7] = {h[1][0], h[1][1], b[0], b[1], b[2], b[3], b[4]};
        int x = eval_hand(a, 7), y = eval_hand(c, 7);
        d->win = x > y ? 0 : x < y ? 1 : 2;
    }
    return NULL;
}
static void build_pool(void) {
    pool = malloc(pool_n * sizeof(Deal));
    pthread_t th[64]; Span sp[64];
    for (int t = 0; t < nthreads; t++) {
        sp[t].lo = pool_n * t / nthreads; sp[t].hi = pool_n * (t + 1) / nthreads;
        pthread_create(&th[t], NULL, pool_worker, &sp[t]);
    }
    for (int t = 0; t < nthreads; t++) pthread_join(th[t], NULL);
}

// ---------- MCCFR ----------
static inline int bucket_at(const Deal *d, int player, int street) {
    return street == 0 ? d->pre[player] : d->b[player][street - 1];
}
static inline void regret_match(const float *reg, int n, float *out) {
    float s = 0;
    for (int i = 0; i < n; i++) if (reg[i] > 0) s += reg[i];
    if (s <= 0) { for (int i = 0; i < n; i++) out[i] = 1.0f / n; return; }
    for (int i = 0; i < n; i++) out[i] = reg[i] > 0 ? reg[i] / s : 0;
}
static float traverse(int id, const Deal *d, int p, Rng *r) {
    const Node *n = &tree.n[id];
    if (n->type == N_FOLD) return n->fold_winner == p ? n->c[1 - p] : -n->c[p];
    if (n->type == N_SHOWDOWN) return d->win == 2 ? 0 : d->win == p ? n->c[1 - p] : -n->c[p];
    int na = n->nact, idx = n->offset + bucket_at(d, n->player, n->street) * na;
    float st[MAXA];
    regret_match(&R[idx], na, st);
    if (n->player == p) {
        float u[MAXA], ev = 0;
        for (int a = 0; a < na; a++) { u[a] = traverse(n->child[a], d, p, r); ev += st[a] * u[a]; }
        for (int a = 0; a < na; a++) R[idx + a] += u[a] - ev;
        return ev;
    }
    for (int a = 0; a < na; a++) A[idx + a] += st[a];
    double x = rng_float(r), acc = 0;
    int pick = na - 1;
    for (int a = 0; a < na; a++) { acc += st[a]; if (x < acc) { pick = a; break; } }
    return traverse(n->child[pick], d, p, r);
}

typedef struct { uint64_t seed; long iters; } Job;
static void *worker(void *arg) {
    Job *j = (Job *)arg;
    Rng r = {j->seed};
    for (long i = 0; i < j->iters; i++) {
        const Deal *d = &pool[rng_u64(&r) % (uint64_t)pool_n];
        traverse(0, d, 0, &r);
        traverse(0, d, 1, &r);
    }
    return NULL;
}

static void save_policy(const char *path) {
    uint8_t *q = malloc(tree.entries);
    for (int id = 0; id < tree.count; id++) {
        const Node *n = &tree.n[id];
        if (n->type != N_PLAYER) continue;
        int nb = STREET_BUCKETS[n->street], na = n->nact;
        for (int b = 0; b < nb; b++) {
            int idx = n->offset + b * na;
            double s = 0;
            for (int a = 0; a < na; a++) s += A[idx + a];
            for (int a = 0; a < na; a++) q[idx + a] = s > 0 ? (uint8_t)(255.0 * A[idx + a] / s + 0.5) : (uint8_t)(255 / na);
        }
    }
    char tmp[512]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { perror(tmp); exit(1); }
    int hdr[2] = {tree.stack, tree.entries};
    fwrite(hdr, sizeof(int), 2, f);
    fwrite(q, 1, tree.entries, f);
    fclose(f);
    rename(tmp, path);
    free(q);
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: train <stack_chips> <seconds> <out.policy> [pool_deals] [threads] [snapshot_seconds]\n"); return 1; }
    int stack = atoi(argv[1]);
    double seconds = atof(argv[2]);
    const char *out = argv[3];
    pool_n = argc > 4 ? atol(argv[4]) : 8000000;
    nthreads = argc > 5 ? atoi(argv[5]) : 7;
    double snap = argc > 6 ? atof(argv[6]) : 0;

    tree_build(&tree, stack);
    long play = 0;
    for (int i = 0; i < tree.count; i++) play += tree.n[i].type == N_PLAYER;
    printf("tree: %d nodes, %ld decision nodes, %d entries, %.0f MB tables\n", tree.count, play, tree.entries, tree.entries * 8.0 / 1e6);
    if (seconds <= 0) return 0;  // size check only
    if (tree.entries > 200000000) { fprintf(stderr, "tables too large for this machine\n"); return 1; }
    R = calloc(tree.entries, sizeof(float));
    A = calloc(tree.entries, sizeof(float));

    double t0 = now();
    if (!load_thresholds(&T, THRESH_FILE)) { build_thresholds(); printf("thresholds built in %.0fs\n", now() - t0); }
    t0 = now();
    build_pool();
    printf("pool of %ld deals built in %.0fs\n", pool_n, now() - t0);
    fflush(stdout);

    t0 = now();
    long total = 0; int round = 0;
    const long block = 200000L * nthreads;
    double next_log = 30, next_snap = snap;
    while (now() - t0 < seconds) {
        pthread_t th[64]; Job jobs[64];
        for (int t = 0; t < nthreads; t++) {
            jobs[t].seed = ((uint64_t)round << 20) + t + 12345; jobs[t].iters = block / nthreads;
            pthread_create(&th[t], NULL, worker, &jobs[t]);
        }
        for (int t = 0; t < nthreads; t++) pthread_join(th[t], NULL);
        total += block; round++;
        double el = now() - t0;
        if (el < seconds * 0.5) {  // linear discount across the first half of training
            float k = (float)round / (round + 1);
            for (int i = 0; i < tree.entries; i++) { R[i] *= k; A[i] *= k; }
        }
        if (el > next_log) { printf("t=%.0fs iters=%ld (%.0f/s)\n", el, total, total / el); fflush(stdout); next_log += 60; }
        if (snap > 0 && el > next_snap) {
            char p[512]; snprintf(p, sizeof p, "%s.%lds", out, (long)next_snap);
            save_policy(p); next_snap = 1e18;  // one early snapshot, used as a weaker benchmark opponent
        }
    }
    save_policy(out);
    printf("done: %ld iterations in %.0fs -> %s\n", total, now() - t0, out);
    return 0;
}
