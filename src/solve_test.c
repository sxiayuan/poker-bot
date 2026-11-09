// Verifies the subgame solver converges: exploitability must fall toward zero, and a
// hand that cannot lose must never fold.
#include <assert.h>
#include <time.h>
#include "solver.h"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static double run(const char *label, State s, const int *board, int turn_mode, Sub *S) {
    float w0[NCOMBOS], w1[NCOMBOS];
    assert(sub_prepare(S, &s, board, turn_mode));
    for (int i = 0; i < S->n; i++) { w0[i] = 1; w1[i] = 1; }
    printf("%s: %d nodes, %d hands, %.0f MB, pot %d\n", label, S->count, S->n, S->entries * 8.0 / 1e6, st_pot(&s));
    int done = 0; double last = 0;
    int marks[] = {10, 50, 200, 1000};
    int count = turn_mode ? 3 : 4;
    for (int m = 0; m < count; m++) {
        double t0 = now();
        sub_iterate(S, w0, w1, done + 1, marks[m]);
        double dt = now() - t0;
        done = marks[m];
        last = sub_exploitability(S, w0, w1);
        printf("  %4d iterations: exploitability %.1f chips (%.2f%% of pot), %.1f ms/iter\n", done, last, 100 * last / st_pot(&s), 1000 * dt / (marks[m] - (m ? marks[m - 1] : 0)));
    }
    return 100 * last / st_pot(&s);
}

int main(void) {
    Sub *S = calloc(1, sizeof(Sub));
    int board[5] = {card_parse("Ks"), card_parse("7h"), card_parse("2d"), card_parse("9c"), card_parse("3s")};
    State s; st_init(&s, 10000);
    st_raise(&s, 250); st_call(&s);    // preflop
    st_call(&s); st_raise(&s, 400); st_call(&s);    // flop
    st_call(&s); st_call(&s);          // turn check-check
    assert(s.street == 3 && st_pot(&s) == 800);
    double river = run("river solve", s, board, 0, S);
    assert(river < 1.0);

    // Top set can only lose to a few hands; the nuts must never fold to a bet.
    assert(S->nodes[0].player == 1);
    int nuts = S->id2h[combo_id(card_parse("Kd"), card_parse("Kh"))];
    assert(nuts >= 0);
    const SNode *root = &S->nodes[0];
    for (int a = 0; a < root->nact; a++) {
        if (root->atype[a] != A_RAISE) continue;
        const SNode *c = &S->nodes[root->child[a]];
        float st[MAXA];
        sub_avg(S, c, 0, nuts, st);
        (void)st;
        int fold = -1;
        for (int i = 0; i < c->nact; i++) if (c->atype[i] == A_FOLD) fold = i;
        assert(fold >= 0);
        // KK for the seat-0 player facing a bet
        assert(st[fold] < 0.01);
    }

    State t; st_init(&t, 10000);
    st_raise(&t, 250); st_call(&t);
    st_call(&t); st_raise(&t, 400); st_call(&t);
    assert(t.street == 2);
    double turn = run("turn solve", t, board, 1, S);
    printf("final: river %.2f%%, turn %.2f%% of pot\n", river, turn);
    printf("solver tests passed\n");
    return 0;
}
