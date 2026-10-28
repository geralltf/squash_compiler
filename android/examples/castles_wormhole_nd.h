/* castles_wormhole_nd.h - the wormhole's geometry in N dimensions.
 *
 * Pure mathematics with no game dependencies (only w_sqrt, w_sin, w_cos),
 * so castles_wormhole_hosttest.c runs exactly this code for any N.
 * Everything is O(N) per operation, and the dimension is a runtime
 * argument up to WH_DIM_MAX, so supporting more dimensions only means
 * raising WH_DIM / WH_DIM_MAX.
 *
 * Space: the (N+1)-dimensional Ellis-Bronnikov wormhole,
 *     ds^2 = -dt^2 + dl^2 + r(l)^2 dOmega_{N-1}^2,   r(l)^2 = l^2 + b^2,
 * with N spatial dimensions. A point is (l, n) with n a unit N-vector on
 * the sphere S^{N-1}: l < 0 on the sheet it set off on, l > 0 on the
 * other.
 *
 * Geodesics: from the Lagrangian (l'^2 + r^2 |n'|^2) / 2 with |n| = 1,
 *     l'' = l |n'|^2,
 *     n'' = -(2 l l' / r^2) n' - |n'|^2 n,
 * integrated as N-vectors (state {l, l', n[N], n'[N]}) with RK4, then
 * projected back onto |n| = 1, n . n' = 0. Conserved: the angular
 * momentum L = r^2 |n'| and l'^2 + L^2 / r^2 (= c^2 for light). By the
 * O(N) symmetry each path stays in the 2-plane spanned by n(0) and n'(0),
 * and l(tau) follows the reduced equation l'' = L^2 l / r^4, the same in
 * every dimension. The host test checks all three for N up to 1000.
 *
 * Mobius maps: in N >= 3 dimensions the conformal maps are exactly the
 * compositions of sphere inversions and similarities (Liouville). The two
 * sheets are joined at the throat sphere |zeta| = 1 (zeta = position / b)
 * by
 *     M(x) = Q x / |x|^2,
 * an inversion followed by an orthogonal Q. A path that reaches the
 * throat at direction n comes out of the other mouth at direction Q n
 * (and back through Q^-1). Here Q is a product of rotations in the
 * disjoint coordinate planes (0,1), (2,3), (4,5), ..., so the world's
 * ground plane (dimensions 0 and 1) is carried onto itself. Because the
 * plane rotations commute, interpolating their angles linearly moves Q
 * along a one-parameter path through orthogonal matrices.
 *
 * The portal at the throat uses the Mobius automorphism of the unit
 * N-ball (Ahlfors), sending a to 0:
 *     phi_a(x) = ((1 - |a|^2)(x - a) - |x - a|^2 a) / (1 - 2 x.a + |x|^2 |a|^2),
 * which in 2D is (z - a) / (1 - conj(a) z) up to a rotation.
 *
 * Drawing it in 3D: ground x, z are dimensions 0 and 1. Height is the
 * distance out of that plane, into the other N - 2 dimensions. A colour
 * shows which way, along a view axis that turns slowly through them. */

#ifndef WH_DIM
#define WH_DIM 100
#endif
#ifndef WH_DIM_MAX
#define WH_DIM_MAX WH_DIM
#endif
#define WND_S      (2 + 2 * WH_DIM_MAX)     /* floats per geodesic state */
#define WND_NANG   (WH_DIM_MAX / 2 + 1)     /* plane-rotation angles     */

/* ---- vectors ---- */
static float wv_dot(const float *a, const float *b, int n) {
    float s = 0.0f;
    int i;
    for (i = 0; i < n; i++) { float t = s + a[i] * b[i]; s = t; }
    return s;
}

static void wv_copy(float *d, const float *s, int n) {
    int i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static void wv_zero(float *d, int n) {
    int i;
    for (i = 0; i < n; i++) d[i] = 0.0f;
}

/* y += a x */
static void wv_axpy(float *y, float a, const float *x, int n) {
    int i;
    for (i = 0; i < n; i++) { float t = y[i] + a * x[i]; y[i] = t; }
}

static float wv_norm(const float *a, int n) { return w_sqrt(wv_dot(a, a, n)); }

static void wv_normalize(float *a, int n) {
    float m = wv_norm(a, n);
    int i;
    if (m <= 0.0f) return;
    for (i = 0; i < n; i++) { float t = a[i] / m; a[i] = t; }
}

/* ---- geodesics: s = {l, l', n[D], n'[D]} ---- */
static float g_wnd_k1[WND_S], g_wnd_k2[WND_S], g_wnd_k3[WND_S], g_wnd_k4[WND_S], g_wnd_t[WND_S];

static void wnd_deriv(const float *s, float b, int D, float *d) {
    const float *n = s + 2, *v = s + 2 + D;
    float l = s[0], lp = s[1];
    float w2 = wv_dot(v, v, D);
    float r2 = l * l + b * b;
    float damp = 2.0f * l * lp / r2;
    int i;
    d[0] = lp;
    d[1] = l * w2;
    for (i = 0; i < D; i++) {
        d[2 + i] = v[i];
        d[2 + D + i] = -damp * v[i] - w2 * n[i];
    }
}

/* One RK4 step of length h, then back onto the constraint surface. */
static void wnd_step(float *s, float b, int D, float h) {
    int m = 2 + 2 * D, i;
    wnd_deriv(s, b, D, g_wnd_k1);
    for (i = 0; i < m; i++) g_wnd_t[i] = s[i] + 0.5f * h * g_wnd_k1[i];
    wnd_deriv(g_wnd_t, b, D, g_wnd_k2);
    for (i = 0; i < m; i++) g_wnd_t[i] = s[i] + 0.5f * h * g_wnd_k2[i];
    wnd_deriv(g_wnd_t, b, D, g_wnd_k3);
    for (i = 0; i < m; i++) g_wnd_t[i] = s[i] + h * g_wnd_k3[i];
    wnd_deriv(g_wnd_t, b, D, g_wnd_k4);
    for (i = 0; i < m; i++) {
        float ns = s[i] + h * (g_wnd_k1[i] + 2.0f * g_wnd_k2[i] + 2.0f * g_wnd_k3[i] + g_wnd_k4[i]) / 6.0f;
        s[i] = ns;
    }
    wv_normalize(s + 2, D);
    wv_axpy(s + 2 + D, -wv_dot(s + 2 + D, s + 2, D), s + 2, D);
}

/* Start at (l, l') on the sphere point n0 moving along the unit tangent
 * t0 (n0 . t0 = 0) with angular momentum L. */
static void wnd_init(float *s, int D, const float *n0, const float *t0, const float *lv) {
    /* lv = {l, l', L, b} */
    float r2 = lv[0] * lv[0] + lv[3] * lv[3];
    int i;
    s[0] = lv[0]; s[1] = lv[1];
    for (i = 0; i < D; i++) { s[2 + i] = n0[i]; s[2 + D + i] = lv[2] / r2 * t0[i]; }
}

static float wnd_L(const float *s, float b, int D) {
    return (s[0] * s[0] + b * b) * wv_norm(s + 2 + D, D);
}

/* l'^2 + L^2 / r^2 */
static float wnd_E(const float *s, float b, int D) {
    float r2 = s[0] * s[0] + b * b;
    return s[1] * s[1] + r2 * wv_dot(s + 2 + D, s + 2 + D, D);
}

/* The reduced (plane) equations, for the shooting method: {l, l', phi}. */
static void wnd_red_step(float *s, float L, float b, float h) {
    float k[12], t[3];
    int q, j;
    for (j = 0; j < 4; j++) {
        const float *x = s;
        if (j > 0) {
            float c = j == 3 ? h : 0.5f * h;
            for (q = 0; q < 3; q++) t[q] = s[q] + c * k[(j - 1) * 3 + q];
            x = t;
        }
        float r2 = x[0] * x[0] + b * b;
        k[j * 3 + 0] = x[1];
        k[j * 3 + 1] = L * L * x[0] / (r2 * r2);
        k[j * 3 + 2] = L / r2;
    }
    for (q = 0; q < 3; q++) {
        float ns = s[q] + h * (k[q] + 2.0f * k[3 + q] + 2.0f * k[6 + q] + k[9 + q]) / 6.0f;
        s[q] = ns;
    }
}

/* Angle swept from l = -l0 (speed lp0) until l > lx, or -1 if the path
 * turns back. g = {l0, lx, lp0, b, h}. */
static float wnd_sweep(float L, const float *g, float *tout) {
    float s[3];
    float t = 0.0f;
    s[0] = -g[0]; s[1] = g[2]; s[2] = 0.0f;
    while (s[0] < g[1]) {
        wnd_red_step(s, L, g[3], g[4]);
        float nt = t + g[4];
        t = nt;
        if (s[1] < 0.0f && s[0] < 0.0f) return -1.0f;
        if (t > 60.0f) return -1.0f;
    }
    if (tout) *tout = t;
    return s[2];
}

/* Shooting: the angular momentum whose path sweeps exactly `target`
 * radians from rim to rim (pi: it comes back to the plane it left,
 * opposite where it went in). Bisection; the sweep grows with L up to
 * the capture limit. */
static float wnd_shoot(const float *g, float target) {
    float r02 = g[0] * g[0] + g[3] * g[3];
    float cap = g[3] * g[2] / w_sqrt(1.0f - g[3] * g[3] / r02);
    float lo = 0.0f, hi = cap * 0.9999f;
    int it;
    for (it = 0; it < 60; it++) {
        float mid = 0.5f * (lo + hi);
        float sw = wnd_sweep(mid, g, 0);
        if (sw < 0.0f || sw > target) hi = mid; else lo = mid;
    }
    return 0.5f * (lo + hi);
}

/* ---- Mobius maps ---- */
/* y = Q x (sign +1) or Q^-1 x (sign -1); ang[j] rotates plane (2j, 2j+1).
 * x and y may be the same array. */
static void wq_apply(const float *x, float *y, int D, const float *ang, float sign) {
    int j;
    for (j = 0; 2 * j + 1 < D; j++) {
        float a = sign * ang[j];
        float c = w_cos(a), sn = w_sin(a);
        float x0 = x[2 * j], x1 = x[2 * j + 1];
        y[2 * j] = c * x0 - sn * x1;
        y[2 * j + 1] = sn * x0 + c * x1;
    }
    if (D & 1) y[D - 1] = x[D - 1];
}

/* Linear interpolation of the transformation: angles from a to b. */
static void wq_lerp(float *out, const float *a, const float *b, int D, float t) {
    int j;
    for (j = 0; 2 * j + 1 < D; j++) out[j] = a[j] + (b[j] - a[j]) * t;
}

/* M(x) = Q x / |x|^2 and its inverse M^-1(y) = Q^-1 y / |y|^2. */
static void wm_apply(const float *x, float *y, int D, const float *ang, float sign) {
    float q = wv_dot(x, x, D);
    int i;
    wq_apply(x, y, D, ang, sign);
    for (i = 0; i < D; i++) { float t = y[i] / q; y[i] = t; }
}

/* Ball automorphism phi_a (|a| < 1). */
static void wb_apply(const float *x, const float *a, float *y, int D) {
    float xa = wv_dot(x, a, D), xx = wv_dot(x, x, D), aa = wv_dot(a, a, D);
    float dxa = xx - 2.0f * xa + aa;                     /* |x - a|^2 */
    float den = 1.0f - 2.0f * xa + xx * aa;
    int i;
    for (i = 0; i < D; i++) y[i] = ((1.0f - aa) * (x[i] - a[i]) - dxa * a[i]) / den;
}

/* phi_a at a point x = (x0, x1, 0, ...) of the ground plane. The formula
 * is c1 x + c2 a with scalars c1, c2 that need only x . a = x0 a0 + x1 a1,
 * |x|^2 and |a|^2, so it costs O(1) per point once |a|^2 is known. Then
 * Q phi_a(x) = c1 (Q x) + c2 (Q a): Q x is a rotation of the plane, and
 * Q a is computed once for all points. out = {c1, c2}. */
static void wb_plane_coef(float x0, float x1, const float *a, float aa, float *out) {
    float xa = x0 * a[0] + x1 * a[1], xx = x0 * x0 + x1 * x1;
    float dxa = xx - 2.0f * xa + aa;
    float den = 1.0f - 2.0f * xa + xx * aa;
    out[0] = (1.0f - aa) / den;
    out[1] = (-(1.0f - aa) - dxa) / den;
}

/* World offset X (world units) of geodesic state s that set off from
 * mouth `src`, and the mouth it is at now. Before the throat: X = r n.
 * After: the Mobius identification carries the direction n to Q n (from
 * mouth 0) or Q^-1 n (from mouth 1), i.e. X / b = M((b / r) n). */
static int wnd_place_n(float l, const float *n, int src, float b, const float *ang, int D, float *X) {
    float r = w_sqrt(l * l + b * b);
    int i;
    if (l <= 0.0f) {
        for (i = 0; i < D; i++) X[i] = r * n[i];
        return src;
    }
    wq_apply(n, X, D, ang, src == 0 ? 1.0f : -1.0f);
    for (i = 0; i < D; i++) { float t = X[i] * r; X[i] = t; }
    return 1 - src;
}

static int wnd_place(const float *s, int src, float b, const float *ang, int D, float *X) {
    return wnd_place_n(s[0], s + 2, src, b, ang, D, X);
}

/* Planar form of a geodesic: by the O(N) symmetry it stays on the great
 * circle n(psi) = cos(psi) n0 + sin(psi) t0, with psi' = L / r^2 and
 * l'' = L^2 l / r^4 (wnd_red_step). Exact, not an approximation; the host
 * test checks it against the full N-vector integration. */
static void wnd_circle(const float *n0, const float *t0, float psi, int D, float *n) {
    float c = w_cos(psi), sn = w_sin(psi);
    int i;
    for (i = 0; i < D; i++) n[i] = c * n0[i] + sn * t0[i];
}

/* Distance of X out of the ground plane (dimensions 0, 1). */
static float wnd_out_of_plane(const float *X, int D) {
    float s = wv_dot(X, X, D) - X[0] * X[0] - X[1] * X[1];
    return s > 0.0f ? w_sqrt(s) : 0.0f;
}

/* The view axis through the extra dimensions at time t (zero if D < 3). */
static void wnd_view_axis(float *e, int D, float t) {
    int j;
    wv_zero(e, D);
    if (D < 3) return;
    for (j = 2; j < D; j++) e[j] = w_cos(0.13f * t + 2.3999632f * (float)j);
    wv_normalize(e, D);
}
