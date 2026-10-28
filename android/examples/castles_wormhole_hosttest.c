/* Host test for castles_wormhole_nd.h: the N-dimensional wormhole maths,
 * run for dimensions 2 .. 1000 (the game uses 100).
 *
 *   gcc -O2 -I. -o /tmp/wh_test android/examples/castles_wormhole_hosttest.c -lm && /tmp/wh_test
 *
 * Exit status 0 when every check passes. */
#include <stdio.h>
#include <math.h>
#include <time.h>

#define WH_DIM_MAX 1024
static float w_sqrt(float x) { return x > 0.0f ? sqrtf(x) : 0.0f; }
static float w_sin(float x) { return sinf(x); }
static float w_cos(float x) { return cosf(x); }
#include "android/examples/castles_wormhole_nd.h"

/* the game's constants (castles_wormhole.h) */
#define RIM 16.0f
#define B   2.8f
#define LP0 6.5f
#define HZ  60.0f     /* the game's fixed step */
#define PI_F 3.14159265f

static int g_fail, g_checks;
static void check(int ok, const char *what, int D, double val, double lim) {
    g_checks++;
    if (!ok) { g_fail++; printf("  FAIL D=%d %s: %.3g (limit %.3g)\n", D, what, val, lim); }
}

static unsigned g_rng = 12345u;
static float frand(void) { g_rng = g_rng * 1664525u + 1013904223u; return (float)(g_rng >> 8) / 16777216.0f * 2.0f - 1.0f; }
static void rand_vec(float *v, int D) { int i; for (i = 0; i < D; i++) v[i] = frand(); }

static float S[WND_S], X[WH_DIM_MAX], Y[WH_DIM_MAX], Z[WH_DIM_MAX], U[WH_DIM_MAX], V[WH_DIM_MAX];
static float N0[WH_DIM_MAX], T0[WH_DIM_MAX], A1[WND_NANG], A2[WND_NANG], AL[WND_NANG];

/* a traveller like the game's: starts in the ground plane, tangent tilted
 * by chi into extra dimension `hk` */
static void traveller(int D, float L, float chi, int hk, float phi0) {
    float l0 = sqrtf((RIM - 1.0f) * (RIM - 1.0f) - B * B);
    float lv[4];
    wv_zero(N0, D); wv_zero(T0, D);
    N0[0] = cosf(phi0); N0[1] = sinf(phi0);
    T0[0] = -sinf(phi0) * cosf(chi); T0[1] = cosf(phi0) * cosf(chi);
    if (D >= 3) T0[hk] = sinf(chi); else { T0[0] = -sinf(phi0); T0[1] = cosf(phi0); }
    lv[0] = -l0; lv[1] = LP0; lv[2] = L; lv[3] = B;
    wnd_init(S, D, N0, T0, lv);
}

static void test_dim(int D, float L, const float *g) {
    float lx = g[1], h = 1.0f / HZ;
    float chi = D >= 3 ? 1.2f : 0.0f;
    int hk = D >= 3 ? 2 + (7 % (D - 2)) : 0;
    traveller(D, L, chi, hk, 0.3f);
    float E0 = wnd_E(S, B, D), L0 = wnd_L(S, B, D);
    float red[3];
    red[0] = S[0]; red[1] = S[1]; red[2] = 0.0f;
    float dE = 0, dL = 0, dRad = 0, dPlane = 0, dNorm = 0, dTan = 0, dCirc = 0;
    int steps = 0, crossed = 0;
    float throatA[WH_DIM_MAX], throatB[WH_DIM_MAX];
    while (S[0] < lx && steps < 20000) {
        float was = S[0];
        if (was <= 0.0f) { int i; float r = sqrtf(S[0] * S[0] + B * B); for (i = 0; i < D; i++) throatA[i] = r * S[2 + i] / B; }
        wnd_step(S, B, D, h);
        wnd_red_step(red, L, B, h);
        steps++;
        if (was <= 0.0f && S[0] > 0.0f && !crossed) {
            /* just through: where the Mobius map put us on the far sheet */
            crossed = 1;
            int m = wnd_place(S, 0, B, A1, D, X);
            int i;
            for (i = 0; i < D; i++) throatB[i] = X[i] / B;
            /* the point just before, mapped by M, must land next to it */
            wm_apply(throatA, Y, D, A1, 1.0f);
            float d = 0; for (i = 0; i < D; i++) d += (Y[i] - throatB[i]) * (Y[i] - throatB[i]);
            check(m == 1, "crossing lands on mouth B", D, m, 1);
            check(sqrtf(d) < 0.05f, "throat continuity |M(x_A) - x_B|", D, sqrtf(d), 0.05);
            /* general sheet relation X_B / b = M((b / r) n) */
            float r = sqrtf(S[0] * S[0] + B * B);
            for (i = 0; i < D; i++) Z[i] = B / r * S[2 + i];
            wm_apply(Z, Y, D, A1, 1.0f);
            d = 0; for (i = 0; i < D; i++) d += (Y[i] - X[i] / B) * (Y[i] - X[i] / B);
            check(sqrtf(d) < 1e-4f, "far sheet = M(inverted point)", D, sqrtf(d), 1e-4);
        }
        float e = fabsf(wnd_E(S, B, D) - E0) / E0; if (e > dE) dE = e;
        float lr = fabsf(wnd_L(S, B, D) - L0) / L0; if (lr > dL) dL = lr;
        float rd = fabsf(S[0] - red[0]); if (rd > dRad) dRad = rd;
        /* planar form: n = cos(psi) n0 + sin(psi) t0 with psi from the reduced equations */
        { float nc[WH_DIM_MAX]; int q; float e2 = 0; wnd_circle(N0, T0, red[2], D, nc);
          for (q = 0; q < D; q++) e2 += (nc[q] - S[2 + q]) * (nc[q] - S[2 + q]);
          if (sqrtf(e2) > dCirc) dCirc = sqrtf(e2); }
        /* stays in span(n0, t0) */
        float a = wv_dot(S + 2, N0, D), bb = wv_dot(S + 2, T0, D), pl = 0; int i;
        for (i = 0; i < D; i++) { float q = S[2 + i] - a * N0[i] - bb * T0[i]; pl += q * q; }
        if (sqrtf(pl) > dPlane) dPlane = sqrtf(pl);
        float nn = fabsf(wv_norm(S + 2, D) - 1.0f); if (nn > dNorm) dNorm = nn;
        float tn = fabsf(wv_dot(S + 2, S + 2 + D, D)); if (tn > dTan) dTan = tn;
    }
    /* swept pi: back in the ground plane, opposite where it went in */
    int i; float end = 0;
    for (i = 0; i < D; i++) end += (S[2 + i] + N0[i]) * (S[2 + i] + N0[i]);
    float outp = wnd_out_of_plane(S + 2, D);
    printf("D=%4d steps %d | drift: E %.1e  L %.1e  |n| %.1e  n.n' %.1e | radial vs reduced %.1e | off-plane %.1e | end vs -n0 %.1e, out of ground plane %.1e\n",
           D, steps, dE, dL, dNorm, dTan, dRad, dPlane, sqrtf(end), outp);
    check(crossed, "crossed the throat", D, crossed, 1);
    check(dE < 1e-4f, "energy l'^2 + L^2/r^2 conserved", D, dE, 1e-4);
    check(dL < 1e-4f, "angular momentum conserved", D, dL, 1e-4);
    check(dRad < 1e-3f, "radial motion = reduced (dimension-free) equation", D, dRad, 1e-3);
    check(dPlane < 1e-4f, "path stays in its 2-plane (O(N) symmetry)", D, dPlane, 1e-4);
    check(dCirc < 1e-3f, "planar form (light rays) = full N-vector path", D, dCirc, 1e-3);
    if (D == 100) printf("       planar form vs full N-vector integration: %.1e\n", dCirc);
    check(sqrtf(end) < 3e-3f, "sweep pi: ends at -n0", D, sqrtf(end), 3e-3);
    check(outp < 3e-3f, "ends back in the ground plane", D, outp, 3e-3);
}

static void test_mobius(int D) {
    int k, i;
    float wq = 0, inv = 0, conf = 0, lerp = 0, ball = 0, ballin = 0, balla = 0, ballconf = 0, planefast = 0;
    for (k = 0; k < 20; k++) {
        rand_vec(X, D);
        float r = 0.3f + 1.4f * (frand() * 0.5f + 0.5f);
        wv_normalize(X, D);
        for (i = 0; i < D; i++) X[i] *= r;
        /* |M(x)| = 1/|x|, M^-1(M(x)) = x */
        wm_apply(X, Y, D, A1, 1.0f);
        float e = fabsf(wv_norm(Y, D) - 1.0f / r) * r; if (e > wq) wq = e;
        wm_apply(Y, Z, D, A1, -1.0f);
        float d = 0; for (i = 0; i < D; i++) d += (Z[i] - X[i]) * (Z[i] - X[i]);
        if (sqrtf(d) / r > inv) inv = sqrtf(d) / r;
        /* conformal: J u . J w = lambda^2 u . w, lambda = 1/|x|^2 (finite differences) */
        rand_vec(U, D); rand_vec(V, D); wv_normalize(U, D); wv_normalize(V, D);
        float eps = 1e-3f, ju[WH_DIM_MAX], jv[WH_DIM_MAX];
        for (i = 0; i < D; i++) Z[i] = X[i] + eps * U[i];
        wm_apply(Z, ju, D, A1, 1.0f);
        for (i = 0; i < D; i++) Z[i] = X[i] - eps * U[i];
        wm_apply(Z, T0, D, A1, 1.0f);
        for (i = 0; i < D; i++) ju[i] = (ju[i] - T0[i]) / (2 * eps);
        for (i = 0; i < D; i++) Z[i] = X[i] + eps * V[i];
        wm_apply(Z, jv, D, A1, 1.0f);
        for (i = 0; i < D; i++) Z[i] = X[i] - eps * V[i];
        wm_apply(Z, T0, D, A1, 1.0f);
        for (i = 0; i < D; i++) jv[i] = (jv[i] - T0[i]) / (2 * eps);
        float lam2 = 1.0f / (r * r * r * r);
        float c1 = fabsf(wv_dot(ju, jv, D) - lam2 * wv_dot(U, V, D)) / lam2;
        float c2 = fabsf(wv_dot(ju, ju, D) - lam2) / lam2;
        if (c1 > conf) conf = c1;
        if (c2 > conf) conf = c2;
        /* interpolated Q(t) stays orthogonal and hits both ends */
        float t = frand() * 0.5f + 0.5f;
        wq_lerp(AL, A1, A2, D, t);
        wq_apply(X, Y, D, AL, 1.0f);
        e = fabsf(wv_norm(Y, D) - r) / r; if (e > lerp) lerp = e;
        wq_apply(U, ju, D, AL, 1.0f); wq_apply(V, jv, D, AL, 1.0f);
        e = fabsf(wv_dot(ju, jv, D) - wv_dot(U, V, D)); if (e > lerp) lerp = e;
        wq_lerp(AL, A1, A2, D, 0.0f); wq_apply(X, Y, D, AL, 1.0f); wq_apply(X, Z, D, A1, 1.0f);
        d = 0; for (i = 0; i < D; i++) d += (Y[i] - Z[i]) * (Y[i] - Z[i]); if (sqrtf(d) / r > lerp) lerp = sqrtf(d) / r;
        wq_lerp(AL, A1, A2, D, 1.0f); wq_apply(X, Y, D, AL, 1.0f); wq_apply(X, Z, D, A2, 1.0f);
        d = 0; for (i = 0; i < D; i++) d += (Y[i] - Z[i]) * (Y[i] - Z[i]); if (sqrtf(d) / r > lerp) lerp = sqrtf(d) / r;
        /* ball automorphism: sphere -> sphere, inside -> inside, a -> 0, conformal */
        float a[WH_DIM_MAX];
        rand_vec(a, D); wv_normalize(a, D); for (i = 0; i < D; i++) a[i] *= 0.6f;
        rand_vec(X, D); wv_normalize(X, D);
        wb_apply(X, a, Y, D);
        e = fabsf(wv_norm(Y, D) - 1.0f); if (e > ball) ball = e;
        for (i = 0; i < D; i++) X[i] *= 0.7f;
        wb_apply(X, a, Y, D);
        if (wv_norm(Y, D) >= 1.0f) ballin = 1.0f;
        wb_apply(a, a, Y, D);
        e = wv_norm(Y, D); if (e > balla) balla = e;
        for (i = 0; i < D; i++) Z[i] = X[i] + eps * U[i];
        wb_apply(Z, a, ju, D);
        for (i = 0; i < D; i++) Z[i] = X[i] - eps * U[i];
        wb_apply(Z, a, T0, D);
        for (i = 0; i < D; i++) ju[i] = (ju[i] - T0[i]) / (2 * eps);
        for (i = 0; i < D; i++) Z[i] = X[i] + eps * V[i];
        wb_apply(Z, a, jv, D);
        for (i = 0; i < D; i++) Z[i] = X[i] - eps * V[i];
        wb_apply(Z, a, T0, D);
        for (i = 0; i < D; i++) jv[i] = (jv[i] - T0[i]) / (2 * eps);
        /* the O(1) ground-plane path agrees with the general one: Q phi_a(x) */
        {
            float px = frand() * 0.7f, pz = frand() * 0.7f, cf[2], qa[WH_DIM_MAX], qx[2];
            wv_zero(Z, D); Z[0] = px; Z[1] = pz;
            wb_apply(Z, a, Y, D); wq_apply(Y, Y, D, A1, 1.0f);
            wb_plane_coef(px, pz, a, wv_dot(a, a, D), cf);
            wq_apply(a, qa, D, A1, 1.0f);
            qx[0] = cosf(A1[0]) * px - sinf(A1[0]) * pz; qx[1] = sinf(A1[0]) * px + cosf(A1[0]) * pz;
            float dd = 0;
            for (i = 0; i < D; i++) { float ref = cf[1] * qa[i] + (i < 2 ? cf[0] * qx[i] : 0.0f); dd += (ref - Y[i]) * (ref - Y[i]); }
            if (sqrtf(dd) > planefast) planefast = sqrtf(dd);
        }
        float l2 = wv_dot(ju, ju, D);
        e = fabsf(wv_dot(ju, jv, D) / l2 - wv_dot(U, V, D)); if (e > ballconf) ballconf = e;
        e = fabsf(wv_dot(jv, jv, D) / l2 - 1.0f); if (e > ballconf) ballconf = e;
    }
    printf("       Mobius M: |M(x)|=1/|x| %.1e  M^-1 M = id %.1e  conformal %.1e | lerp Q(t) orthogonal+ends %.1e | ball: sphere %.1e  a->0 %.1e  conformal %.1e%s\n",
           wq, inv, conf, lerp, ball, balla, ballconf, ballin > 0 ? "  INSIDE->OUTSIDE!" : "");
    check(wq < 1e-5f, "|M(x)| = 1/|x|", D, wq, 1e-5);
    check(inv < 1e-5f, "M^-1(M(x)) = x", D, inv, 1e-5);
    check(conf < 2e-2f, "M conformal (J^T J = lambda^2 I)", D, conf, 2e-2);
    check(lerp < 1e-5f, "interpolated Q orthogonal, Q(0)=Q1, Q(1)=Q2", D, lerp, 1e-5);
    check(ball < 1e-5f, "ball automorphism keeps the sphere", D, ball, 1e-5);
    check(ballin == 0.0f, "ball automorphism keeps the inside", D, ballin, 0);
    check(balla < 1e-6f, "phi_a(a) = 0", D, balla, 1e-6);
    check(ballconf < 2e-2f, "ball automorphism conformal", D, ballconf, 2e-2);
    check(planefast < 1e-5f, "O(1) ground-plane portal map = general N-D map", D, planefast, 1e-5);
    if (D == 100) printf("       ground-plane portal shortcut vs general map: %.1e\n", planefast);
}

int main(void) {
    int dims[] = { 2, 3, 4, 5, 7, 16, 33, 100, 257, 1000 };
    int nd = (int)(sizeof dims / sizeof dims[0]), k, j;
    float g[5];
    g[0] = sqrtf((RIM - 1.0f) * (RIM - 1.0f) - B * B);
    g[1] = sqrtf((RIM - 1.5f) * (RIM - 1.5f) - B * B);
    g[2] = LP0; g[3] = B; g[4] = 1.0f / HZ;
    float L = wnd_shoot(g, PI_F), tt = 0;
    float sw = wnd_sweep(L, g, &tt);
    printf("shooting: L = %.5f sweeps %.6f rad (pi %.6f) in %.2f s\n", L, sw, PI_F, tt);
    /* the exit is detected on a step, so the swept angle is known to within
     * one step's sweep at the rim, L / r^2 * h (the end direction itself is
     * checked per dimension below) */
    float stepsw = L / ((RIM - 1.5f) * (RIM - 1.5f)) / HZ;
    check(fabsf(sw - PI_F) < 1.5f * stepsw, "shooting hits pi (to one step)", 0, fabsf(sw - PI_F), 1.5 * stepsw);
    /* 2D sanity: the ball automorphism has the complex formula's modulus */
    {
        float a[2] = { 0.3f, -0.4f }, x[2] = { 0.5f, 0.2f }, y[2];
        wb_apply(x, a, y, 2);
        float nr = x[0] - a[0], ni = x[1] - a[1];
        float dr = 1.0f - (a[0] * x[0] + a[1] * x[1]), di = -(a[0] * x[1] - a[1] * x[0]);
        float m = sqrtf((nr * nr + ni * ni) / (dr * dr + di * di));
        printf("2D: |phi_a| %.6f vs |(z-a)/(1-conj(a)z)| %.6f\n", wv_norm(y, 2), m);
        check(fabsf(wv_norm(y, 2) - m) < 1e-5f, "2D ball map = complex disc map", 2, fabsf(wv_norm(y, 2) - m), 1e-5);
    }
    for (k = 0; k < nd; k++) {
        int D = dims[k];
        for (j = 0; j < WND_NANG; j++) { A1[j] = frand() * 3.14159f; A2[j] = frand() * 3.14159f; }
        test_dim(D, L, g);
        test_mobius(D);
    }
    /* cost at the game's 100 dimensions: one traveller-second, one ray-second */
    {
        int D = 100, n;
        traveller(D, L, 1.2f, 9, 0.0f);
        clock_t c0 = clock();
        for (n = 0; n < 120 * 200; n++) { wnd_step(S, B, D, 1.0f / HZ); if (S[0] > 10.0f) traveller(D, L, 1.2f, 9, 0.0f); }
        double us = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e6 / 200.0;
        printf("cost at D=100: %.0f us per simulated second per path at 120 Hz (host, -O2)\n", us);
    }
    printf("%d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
