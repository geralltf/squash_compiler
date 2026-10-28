/* castles_wormhole.h - a traversable wormhole between an enemy castle and
 * the player's castle, simulated in WH_DIM (100) spatial dimensions.
 *
 * The geometry lives in castles_wormhole_nd.h (tested for 2 .. 1000
 * dimensions by castles_wormhole_hosttest.c):
 *   - The (N+1)-dimensional Ellis-Bronnikov wormhole
 *     ds^2 = -dt^2 + dl^2 + (l^2 + b^2) dOmega_{N-1}^2. Travellers follow
 *     its geodesics as full N-vectors (RK4, 60 Hz). Light rays use the
 *     exact planar form of the same geodesics: a great circle of R^N
 *     whose angle is integrated, lifted to N dimensions for drawing.
 *   - The sheets are joined by the N-dimensional Mobius map
 *     M(x) = Q x / |x|^2 (inversion, then rotation Q in O(N)).
 *   - Q changes between events by linear interpolation of its
 *     plane-rotation angles.
 *   - The portal at the throat shows the Mobius automorphism of the
 *     N-ball, between two interpolated rotations.
 *
 * The world's ground is the plane of dimensions 0 and 1. Travellers step
 * in from the ground, with their path tilted by WH_CHI into an extra
 * dimension (a different one for each). They climb out of the ground
 * plane into the other 98 dimensions, which is drawn as height, and come
 * back to it. The shooting method picks the angular momentum whose path
 * sweeps exactly pi from rim to rim, and a geodesic on a sphere returns
 * to its starting plane after pi. They come out of mouth B on the
 * ground, by the Mobius map, facing the player's town centre.
 *
 * In the world: castles.vert lowers everything within WORM_RIM of a
 * mouth down the embedding funnel z(r) = b acosh(r / b). That shape is
 * the same in every dimension, since the radial part of the metric does
 * not depend on N. The shader also twists it into a vortex. Opening and
 * closing interpolate the funnel's strength and throat radius linearly
 * (smoothstepped).
 *
 * Every WH_PERIOD seconds three soldiers of the nearest enemy castle step
 * into mouth A outside its walls and out of mouth B near the player's
 * castle, then march on the player's town centre. */

#include "android/examples/castles_wormhole_nd.h"

#define WH_PERIOD   30.0f
#define WH_RIM      16.0f     /* must match WORM_RIM in castles.vert        */
#define WH_B_OPEN   2.8f      /* throat radius when fully open              */
#define WH_B_IDLE   0.9f
#define WH_S_IDLE_B 0.16f     /* the player-side mouth idles as a dimple    */
#define WH_OPEN_T   2.5f      /* opening / closing time (s)                 */
#define WH_HZ       60.0f     /* fixed step (RK4); castles_wormhole_hosttest.c checks it */
#define WH_NP       32        /* light rays                                 */
#define WH_C        7.0f      /* speed of light in the toy (world units/s) */
#define WH_MAXTP    12        /* teleported soldiers alive at once          */
#define WH_NT       3         /* travellers per event                       */
#define WH_STAGGER  0.8f      /* seconds between them stepping in           */
#define WH_LP0      6.5f      /* travellers' inward speed at the rim        */
#define WH_CHI      1.2f      /* tilt of their paths out of the ground plane */
#define WH_HSCALE   1.6f      /* height drawn per unit out of the plane     */

static int g_wh_placed;
static float g_wh_x[2], g_wh_z[2];      /* mouth 0 = A (enemy side), 1 = B (player side) */
static float g_wh_s[2], g_wh_b[2];      /* opening strength 0..1, throat radius          */
static float g_wh_clock;                /* time until the next event                     */
static float g_wh_ev;                   /* time since the event started, -1 when idle    */
static float g_wh_acc;                  /* fixed-step accumulator                        */
static int g_wh_from;                   /* enemy castle mouth A stands outside           */
static int g_wh_a_on;                   /* mouth A placed                                */
static int g_wh_events;
static int g_wh_dim;                    /* spatial dimensions simulated (<= WH_DIM_MAX)  */
static float g_wh_Lpi, g_wh_tt;         /* angular momentum sweeping pi; its transit (s) */
/* Mobius rotation Q: plane angles, interpolated from qa to qb each event */
static float g_wh_qa[WND_NANG], g_wh_qb[WND_NANG], g_wh_q[WND_NANG], g_wh_qp[WND_NANG];
static float g_wh_phi0;                 /* entry angle at A toward the player            */
/* the travellers: N-dimensional geodesic states */
static int g_wh_u[WH_NT];               /* unit index, -1 none                           */
static int g_wh_tst[WH_NT];             /* 0 waiting, 1 on the geodesic, 2 done          */
static float g_wt_s[WH_NT * WND_S];
static float g_wt_E[WH_NT], g_wt_L[WH_NT];
static int g_wh_crossed[WH_NT];
static float g_wx[WH_DIM_MAX], g_wy[WH_DIM_MAX], g_wa[WH_DIM_MAX];   /* scratch N-vectors */
static float g_wev[WH_DIM_MAX];         /* view axis through the extra dimensions        */
static float g_wh_close_t;              /* > 0: closing countdown                       */
/* light rays */
static int wp_on[WH_NP], wp_src[WH_NP];
/* each ray on its great circle n = cos(psi) n0 + sin(psi) t0 of R^N:
 * {l, l', psi}, angular momentum, and the circle's two basis vectors */
static float wp_st[WH_NP * 3], wp_L[WH_NP], wp_n0[WH_NP * WH_DIM_MAX], wp_t0[WH_NP * WH_DIM_MAX];
static float wp_h[WH_NP], wp_age[WH_NP];
static uint32_t g_wh_rng;
/* drawing lattice: WH_NA angles, ground heights every 0.5 out to the rim */
#define WH_NA   48
#define WH_NRH  33
static int g_whm;           /* mouth being drawn */
static float g_wh_ca[WH_NA], g_wh_sa[WH_NA];
static float g_wh_ht[2 * WH_NRH * WH_NA];
static int g_wh_ht_ok[2], g_wh_ht_row[2];
static float g_wh_ht_x[2], g_wh_ht_z[2];

/* Height of the drawn terrain surface (exact mesh triangulation where the
 * chunk is loaded; castle plateaus blended in). */
static float wh_ground(float x, float z) { return nav_terrain_height(x, z); }

static float wh_smooth(float t) {
    t = w_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float wh_rand(void) {
    g_wh_rng = g_wh_rng * 1664525u + 1013904223u;
    return (float)(g_wh_rng >> 8) * (1.0f / 16777216.0f);
}

static float wh_angle(float dx, float dz) { return approx_atan2_w(dz, dx); }

/* How rough the ground under a mouth at (x,z) is (height range over the
 * rim), or -1 if it would swallow water, castle walls or buildings. */
static float wh_site_rough(float x, float z) {
    float lo = 1e9f, hi = -1e9f;
    int k, j;
    for (j = 0; j < 4; j++) {
        float rr = (float)j * 6.0f;
        for (k = 0; k < (j == 0 ? 1 : 12); k++) {
            float a = (float)k * (2.0f * W_PI / 12.0f);
            float px = x + rr * w_cos(a), pz = z + rr * w_sin(a);
            float h = w_height(px, pz);
            if (h < W_SEA_LEVEL + 0.6f) return -1.0f;
            WSite *st = w_site_near(px, pz, 0.0f);
            if (st) {                                       /* on a castle plateau: keep off the walls */
                float ex = px - st->cx, ez = pz - st->cz;
                if (ex * ex + ez * ez < (st->R + 5.0f) * (st->R + 5.0f)) return -1.0f;
            }
            if (h < lo) lo = h;
            if (h > hi) hi = h;
        }
    }
    for (k = 0; k < g_hb_n; k++) {
        if (g_hb_dead[k]) continue;
        float dx = g_hb_x[k] - x, dz = g_hb_z[k] - z;
        if (dx * dx + dz * dz < 22.0f * 22.0f) return -1.0f;
    }
    return hi - lo;
}

/* A mouth from rmin out from (cx,cz): the flattest open ground, near
 * (distance weighs `wdist` per unit) and as close as possible to the
 * direction `dir`. */
static int wh_find_site(float cx, float cz, float dir, float rmin, float wdist, float *ox, float *oz) {
    float best = 1e9f;
    int ri, k;
    *ox = cx + (rmin + 6.0f) * w_cos(dir); *oz = cz + (rmin + 6.0f) * w_sin(dir);
    for (ri = 0; ri < 10; ri++) {
        float rr = rmin + 4.0f * (float)ri;
        for (k = 0; k < 24; k++) {
            float a = (float)k * (2.0f * W_PI / 24.0f);
            float da = wrap_angle(a - dir);
            if (da < 0.0f) da = -da;
            float score = (rr - rmin) * wdist + da * 2.0f;
            if (score >= best) continue;
            float x = cx + rr * w_cos(a), z = cz + rr * w_sin(a);
            float rough = wh_site_rough(x, z);
            if (rough < 0.0f) continue;
            float sc2 = score + rough * 0.9f;
            if (sc2 >= best) continue;
            best = sc2; *ox = x; *oz = z;
        }
    }
    return best < 1e8f;
}

static int wh_faction_ok(int f) {
    return f > 0 && f < F_MAX && f_on[f] && f_tc[f] >= 0 && !g_hb_dead[f_tc[f]];
}

/* Nearest living enemy castle to the player's town centre, or -1. */
static int wh_pick_source(void) {
    int f, best = -1;
    float bd = 1e18f;
    for (f = 1; f < F_MAX; f++) {
        if (!wh_faction_ok(f)) continue;
        float dx = g_hb_x[f_tc[f]] - g_hb_x[f_tc[0]], dz = g_hb_z[f_tc[f]] - g_hb_z[f_tc[0]];
        float d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = f; }
    }
    return best;
}

static void wormhole_reset(void) {
    int i;
    g_wh_placed = 0; g_wh_ev = -1.0f; g_wh_close_t = 0.0f; g_wh_acc = 0.0f;
    g_wh_a_on = 0;
    g_wh_clock = WH_PERIOD; g_wh_events = 0; g_wh_from = -1;
    g_wh_s[0] = 0.0f; g_wh_s[1] = 0.0f; g_wh_b[0] = WH_B_IDLE; g_wh_b[1] = WH_B_IDLE;
    g_wh_rng = 0x9e3779b9u;
#ifdef CASTLES_DEBUG_WORM
    g_wh_clock = 8.0f;
#endif
    for (i = 0; i < WH_NP; i++) wp_on[i] = 0;
    g_wh_ht_ok[0] = 0; g_wh_ht_ok[1] = 0;
    for (i = 0; i < WH_NT; i++) { g_wh_u[i] = -1; g_wh_tst[i] = 2; }
    for (i = 0; i < WND_NANG; i++) { g_wh_qa[i] = 0.0f; g_wh_qb[i] = 0.0f; g_wh_q[i] = 0.0f; }
    g_wh_dim = WH_DIM;
    /* shooting: the angular momentum whose path sweeps pi from rim to rim */
    float g[5];
    g[0] = w_sqrt((WH_RIM - 1.0f) * (WH_RIM - 1.0f) - WH_B_OPEN * WH_B_OPEN);
    g[1] = w_sqrt((WH_RIM - 1.5f) * (WH_RIM - 1.5f) - WH_B_OPEN * WH_B_OPEN);
    g[2] = WH_LP0; g[3] = WH_B_OPEN; g[4] = 1.0f / WH_HZ;
    g_wh_Lpi = wnd_shoot(g, W_PI);
    g_wh_tt = 0.0f;
    float sw = wnd_sweep(g_wh_Lpi, g, &g_wh_tt);
    __android_log_print(4, "castles", "wormhole: %d dimensions; shooting gives L = %.4f, sweeping %.5f rad (pi) rim to rim in %.2f s",
                        g_wh_dim, (double)g_wh_Lpi, (double)sw, (double)g_wh_tt);
}

/* Mouth A: right outside the walls of enemy castle f, on the side facing
 * the player (the rim must clear the walls, so the centre sits at least
 * the wall radius + WH_RIM + a margin out from the castle's centre). */
static void wh_place_a(int f) {
    g_wh_a_on = 0;
    g_wh_from = -1;
    if (!wh_faction_ok(f) || f_tc[0] < 0) return;
    float ex = g_hb_x[f_tc[f]], ez = g_hb_z[f_tc[f]];
    float px = g_hb_x[f_tc[0]], pz = g_hb_z[f_tc[0]];
    float cx = ex, cz = ez, rmin = 30.0f, wallR = 0.0f;
    WSite *st = &f_site[f];                          /* the castle (its walls' outer ring is R) */
    if (st->R > 0.0f) { cx = st->cx; cz = st->cz; wallR = st->R; rmin = st->R + 23.0f; }
    int found = wh_find_site(cx, cz, wh_angle(px - cx, pz - cz), rmin, 0.35f, &g_wh_x[0], &g_wh_z[0]);
    g_wh_from = f;
    g_wh_a_on = 1;
#ifdef CASTLES_DEBUG_WORM_A
    g_rts_tx = g_wh_x[0]; g_rts_tz = g_wh_z[0]; g_rts_dist = 60.0f;
#endif
    float dx = g_wh_x[0] - cx, dz = g_wh_z[0] - cz;
    __android_log_print(4, "castles", "wormhole: mouth A at (%.1f, %.1f) (%s), outside enemy castle %d: rim %.1f beyond its walls",
                        (double)g_wh_x[0], (double)g_wh_z[0], found ? "open ground" : "fallback", f,
                        (double)(w_sqrt(dx * dx + dz * dz) - WH_RIM - wallR));
}

/* Mouth B: out from the player's town centre, toward the nearest enemy. */
static void wh_place(void) {
    if (f_tc[0] < 0) return;
    float px = g_hb_x[f_tc[0]], pz = g_hb_z[f_tc[0]];
    int f = wh_pick_source();
    float dir = 0.7f;
    if (f > 0) dir = wh_angle(g_hb_x[f_tc[f]] - px, g_hb_z[f_tc[f]] - pz);
    int found = wh_find_site(px, pz, dir, 34.0f, 0.08f, &g_wh_x[1], &g_wh_z[1]);
    g_wh_x[0] = g_wh_x[1] + 1000.0f; g_wh_z[0] = g_wh_z[1];        /* until A is placed */
    g_wh_placed = 1;
#ifdef CASTLES_DEBUG_WORM
    g_rts_tx = g_wh_x[1]; g_rts_tz = g_wh_z[1]; g_rts_dist = 42.0f;
#endif
    wh_place_a(f);
    __android_log_print(4, "castles", "wormhole: mouth B at (%.1f, %.1f) (%s), %.1f from the player's town centre",
                        (double)g_wh_x[1], (double)g_wh_z[1], found ? "open ground" : "fallback",
                        (double)w_sqrt((g_wh_x[1] - px) * (g_wh_x[1] - px) + (g_wh_z[1] - pz) * (g_wh_z[1] - pz)));
}

static int wh_teleported_alive(void) {
    int i, n = 0;
    for (i = 0; i < g_un; i++) if (unit_alive(i) && u_wh[i] == 2) n++;
    return n;
}

static void wh_start_event(void) {
    int f = wh_pick_source();
    if (f < 0 || f_tc[0] < 0 || g_hb_dead[f_tc[0]]) return;
    if (wh_teleported_alive() >= WH_MAXTP) return;
    if (f != g_wh_from || !g_wh_a_on) wh_place_a(f);
    if (!g_wh_a_on) return;
    g_wh_ev = 0.0f;
    g_wh_close_t = 0.0f;
    /* the Mobius rotation for this event: the ground-plane angle so a path
     * entering A at phi0 (and sweeping pi) leaves B toward the town centre,
     * fresh random angles in the 49 other planes; interpolated from the
     * last one while the mouths open */
    float px = g_hb_x[f_tc[0]], pz = g_hb_z[f_tc[0]];
    g_wh_phi0 = wh_angle(px - g_wh_x[0], pz - g_wh_z[0]);
    float thb = wh_angle(px - g_wh_x[1], pz - g_wh_z[1]);
    int k;
    for (k = 0; k < WND_NANG; k++) { g_wh_qa[k] = g_wh_q[k]; g_wh_qb[k] = (wh_rand() * 2.0f - 1.0f) * W_PI; }
    g_wh_qb[0] = g_wh_qa[0] + wrap_angle(thb - (g_wh_phi0 + W_PI) - g_wh_qa[0]);
    for (k = 0; k < WH_NT; k++) { g_wh_u[k] = -1; g_wh_tst[k] = 0; }
    g_wh_events = g_wh_events + 1;
    __android_log_print(4, "castles", "wormhole event %d: mouth A opens outside enemy castle %d at (%.1f, %.1f)",
                        g_wh_events, f, (double)g_wh_x[0], (double)g_wh_z[0]);
}

static int wh_in_transit(void) {
    int k, n = 0;
    for (k = 0; k < WH_NT; k++) if (g_wh_tst[k] == 1) n++;
    return n;
}

/* Traveller k steps onto mouth A's rim from the ground plane at angle
 * phi0 + {0, +0.4, -0.4}, its path tilted by WH_CHI into extra dimension
 * 2 + (37k + 11 * event) mod (N - 2). Each path is a great circle through
 * the ground plane, so after sweeping pi it is back in it, opposite the
 * way in. Q's ground-plane rotation turns that toward the town centre,
 * fanned by the same offsets. */
static void wh_launch(int k) {
    int f = g_wh_from, D = g_wh_dim;
    g_wh_tst[k] = 2;
    if (!wh_faction_ok(f) || f_tc[0] < 0) return;
    if (wh_teleported_alive() + wh_in_transit() >= WH_MAXTP) return;
    float b = WH_B_OPEN;
    float off = k == 1 ? 0.4f : (k == 2 ? -0.4f : 0.0f);
    float a = g_wh_phi0 + off;
    float chi = D >= 3 ? WH_CHI : 0.0f;
    float lv[4];
    wv_zero(g_wx, D); wv_zero(g_wy, D);
    g_wx[0] = w_cos(a); g_wx[1] = w_sin(a);
    g_wy[0] = -w_sin(a) * w_cos(chi); g_wy[1] = w_cos(a) * w_cos(chi);
    int hk = D >= 3 ? 2 + (37 * k + 11 * g_wh_events) % (D - 2) : 0;
    if (D >= 3) g_wy[hk] = w_sin(chi);
    lv[0] = -w_sqrt((WH_RIM - 1.0f) * (WH_RIM - 1.0f) - b * b); lv[1] = WH_LP0; lv[2] = g_wh_Lpi; lv[3] = b;
    float *st = g_wt_s + k * WND_S;
    wnd_init(st, D, g_wx, g_wy, lv);
    g_wt_E[k] = wnd_E(st, b, D); g_wt_L[k] = wnd_L(st, b, D);
    int u = unit_spawn(TEAM_ENEMY, g_wh_x[0] + (WH_RIM - 1.0f) * g_wx[0], g_wh_z[0] + (WH_RIM - 1.0f) * g_wx[1], 0,
                       w_hash2(g_wh_events * 3 + k, f, 77u), UC_RANDOM, f);
    if (u < 0) return;
    u_wh[u] = 1;
    u_role[u] = R_WAVE;
    u_state[u] = US_MOVE;
    u_y[u] = wh_ground(u_x[u], u_z[u]);
    g_wh_u[k] = u;
    g_wh_tst[k] = 1;
    g_wh_crossed[k] = 0;
    __android_log_print(4, "castles", "wormhole: soldier %d (%d of %d) of castle %d steps into the funnel: %d-D geodesic, L=%.3f, tilted %.2f rad into dimension %d",
                        u, k + 1, WH_NT, f, D, (double)g_wh_Lpi, (double)chi, hk);
}

/* Places traveller k at its geodesic state: ground position from
 * dimensions 0 and 1, height from its distance out of the ground plane
 * (the vertex shader then bends it down the funnel with the ground). */
static float wh_place_traveller(int k) {
    int u = g_wh_u[k], D = g_wh_dim;
    int m = wnd_place(g_wt_s + k * WND_S, 0, WH_B_OPEN, g_wh_q, D, g_wx);
    float nx = g_wh_x[m] + g_wx[0], nz = g_wh_z[m] + g_wx[1];
    float out = wnd_out_of_plane(g_wx, D);
    /* heading: the change in position (finite difference), facing as walkers do */
    float dx = nx - u_x[u], dz = nz - u_z[u];
    if (dx * dx + dz * dz > 1e-6f && dx * dx + dz * dz < 4.0f) u_yaw[u] = approx_atan2(dx, -dz);
    float np = u_phase[u] + w_sqrt(dx * dx + dz * dz) * 2.6f;     /* walking, for the animation */
    if (dx * dx + dz * dz < 4.0f) u_phase[u] = np;
    u_px[u] = u_x[u]; u_pz[u] = u_z[u];
    u_x[u] = nx; u_z[u] = nz;
    u_y[u] = wh_ground(nx, nz) + WH_HSCALE * out;
    u_layer[u] = 0;
    return out;
}

static void wh_traveller_step(int k, float h) {
    if (g_wh_tst[k] != 1) return;
    int u = g_wh_u[k], D = g_wh_dim;
    float *st = g_wt_s + k * WND_S;
    if (!unit_alive(u) || u_wh[u] != 1) { g_wh_u[k] = -1; g_wh_tst[k] = 2; return; }
    float was = st[0];
    wnd_step(st, WH_B_OPEN, D, h);
    if (was < 0.0f && st[0] >= 0.0f && !g_wh_crossed[k]) {
        g_wh_crossed[k] = 1;
        float out = wh_place_traveller(k);
        __android_log_print(4, "castles", "wormhole: soldier %d through the throat at t=%.2f s, %.2f out of the ground plane; drift of l'^2+L^2/r^2 %.1e, of L %.1e",
                            u, (double)g_wh_ev, (double)out,
                            (double)((wnd_E(st, WH_B_OPEN, D) - g_wt_E[k]) / g_wt_E[k]), (double)((wnd_L(st, WH_B_OPEN, D) - g_wt_L[k]) / g_wt_L[k]));
        return;
    }
    if (st[0] > w_sqrt((WH_RIM - 1.5f) * (WH_RIM - 1.5f) - WH_B_OPEN * WH_B_OPEN)) {
        /* out of mouth B's funnel, back in the ground plane: join the attack */
        float out = wh_place_traveller(k);
        u_wh[u] = 2;
        u_y[u] = wh_ground(u_x[u], u_z[u]);
        float gx, gz;
        building_door(f_tc[0], 7.0f, &gx, &gz);
        ai_send(u, gx + (float)(k - 1) * 1.5f, gz);
        __android_log_print(4, "castles", "wormhole: soldier %d emerges from mouth B at (%.1f, %.1f), %.4f out of the ground plane, and marches on the town centre",
                            u, (double)u_x[u], (double)u_z[u], (double)out);
        g_wh_u[k] = -1;
        g_wh_tst[k] = 2;
        return;
    }
    wh_place_traveller(k);
}

/* A light ray falls in from the rim of mouth `src`, starting in the ground
 * plane and heading off at a random tilt into a random extra direction,
 * with random impact parameter h: rays with h < b reach the other mouth,
 * the rest turn back at r = h. */
static void wh_spawn_ray(int i) {
    int src = wh_rand() < 0.5f ? 0 : 1, D = g_wh_dim, j;
    if (g_wh_s[src] < 0.05f) src = 1 - src;
    float b = g_wh_b[src];
    float r0 = WH_RIM - 0.5f;
    float h = wh_rand() * 1.6f * b;
    if (h > r0 * 0.95f) h = r0 * 0.95f;
    float a = wh_rand() * 2.0f * W_PI, psi = wh_rand() * 0.5f * W_PI;
    wv_zero(g_wx, D); wv_zero(g_wy, D);
    g_wx[0] = w_cos(a); g_wx[1] = w_sin(a);
    for (j = 2; j < D; j++) g_wy[j] = wh_rand() * 2.0f - 1.0f;
    wv_normalize(g_wy, D);                             /* random extra direction */
    for (j = 2; j < D; j++) { float t = g_wy[j] * w_sin(psi); g_wy[j] = t; }
    if (D < 3) psi = 0.0f;
    g_wy[0] = -w_sin(a) * w_cos(psi); g_wy[1] = w_cos(a) * w_cos(psi);
    wv_copy(wp_n0 + i * WH_DIM_MAX, g_wx, D);
    wv_copy(wp_t0 + i * WH_DIM_MAX, g_wy, D);
    wp_st[i * 3 + 0] = -w_sqrt(r0 * r0 - b * b);
    wp_st[i * 3 + 1] = WH_C * w_sqrt(1.0f - h * h / (r0 * r0));       /* null: l'^2 + L^2/r^2 = c^2 */
    wp_st[i * 3 + 2] = 0.0f;
    wp_L[i] = WH_C * h;
    wp_src[i] = src;
    wp_h[i] = h;
    wp_age[i] = 0.0f;
    wp_on[i] = 1;
}

static void wh_rays_step(float h) {
    int i;
    float smax = g_wh_s[0] > g_wh_s[1] ? g_wh_s[0] : g_wh_s[1];
    int want = (int)(smax * (float)WH_NP);
    for (i = 0; i < WH_NP; i++) {
        if (!wp_on[i]) { if (i < want && wh_rand() < 0.1f) wh_spawn_ray(i); continue; }
        float *st = wp_st + i * 3;
        wnd_red_step(st, wp_L[i], g_wh_b[wp_src[i]], h);
        float na = wp_age[i] + h;
        wp_age[i] = na;
        float l = st[0];
        if ((l * l > (WH_RIM - 0.5f) * (WH_RIM - 0.5f) && l * st[1] > 0.0f && na > 0.5f) || na > 12.0f) wp_on[i] = 0;
    }
}

static void wormhole_update(float dt) {
    if (!g_wh_placed) { if (f_tc[0] >= 0 && g_hb_n > 0) wh_place(); if (!g_wh_placed) return; }
    if (g_wh_ev < 0.0f && g_wh_a_on && !wh_faction_ok(g_wh_from)) {
        int nf = wh_pick_source();                   /* its castle fell: move A to the next one */
        if (nf > 0) wh_place_a(nf); else g_wh_a_on = 0;
    }
    float sA = g_wh_a_on ? WH_S_IDLE_B : 0.0f, sB = WH_S_IDLE_B;
    float nclk = g_wh_clock - dt;                    /* one event every WH_PERIOD, start to start */
    g_wh_clock = nclk;
    if (g_wh_ev >= 0.0f) {
        float ne = g_wh_ev + dt;
        g_wh_ev = ne;
        float o = wh_smooth(g_wh_ev / WH_OPEN_T);
        if (g_wh_close_t > 0.0f) {
            float nc = g_wh_close_t - dt;
            g_wh_close_t = nc;
            o = wh_smooth(g_wh_close_t / WH_OPEN_T);
            if (g_wh_close_t <= 0.0f) { g_wh_ev = -1.0f; o = 0.0f; }
        }
        sA = WH_S_IDLE_B + (1.0f - WH_S_IDLE_B) * o; sB = sA;
        if (g_wh_close_t <= 0.0f)                    /* Q: linear interpolation while opening */
            wq_lerp(g_wh_q, g_wh_qa, g_wh_qb, g_wh_dim, wh_smooth(g_wh_ev / WH_OPEN_T));
        if (g_wh_close_t <= 0.0f && g_wh_ev >= 0.0f) {
            int k, busy = 0;
            for (k = 0; k < WH_NT; k++) {
                if (g_wh_tst[k] == 0 && g_wh_ev >= WH_OPEN_T + WH_STAGGER * (float)k) wh_launch(k);
                if (g_wh_tst[k] != 2) busy = 1;
            }
            if (!busy) g_wh_close_t = WH_OPEN_T;        /* all through (or lost): close */
        }
    } else if (g_wh_clock <= 0.0f) {
        g_wh_clock = WH_PERIOD;
        wh_start_event();
    }
    /* the vortex opens and closes by linear interpolation of strength and throat */
    g_wh_s[0] = sA; g_wh_s[1] = sB;
    g_wh_b[0] = WH_B_IDLE + (WH_B_OPEN - WH_B_IDLE) * w_clampf((sA - WH_S_IDLE_B) / (1.0f - WH_S_IDLE_B), 0.0f, 1.0f);
    g_wh_b[1] = WH_B_IDLE + (WH_B_OPEN - WH_B_IDLE) * (sB - WH_S_IDLE_B) / (1.0f - WH_S_IDLE_B);
    if (wh_in_transit() > 0 && g_wh_b[0] < WH_B_OPEN) g_wh_b[0] = WH_B_OPEN;
    float na = g_wh_acc + dt;
    if (na > 0.25f) na = 0.25f;
    g_wh_acc = na;
    while (g_wh_acc >= 1.0f / WH_HZ) {
        int k;
        for (k = 0; k < WH_NT; k++) wh_traveller_step(k, 1.0f / WH_HZ);
        wh_rays_step(1.0f / WH_HZ);
        float nr = g_wh_acc - 1.0f / WH_HZ;
        g_wh_acc = nr;
    }
}

/* Where castles.vert draws ground-level point (x,z): the same vortex
 * twist about each open mouth (the funnel only lowers it). The minimap
 * uses this so units in bent space show where they are seen. */
static void wormhole_view_xz(float x, float z, float *o) {
    int m;
    o[0] = x; o[1] = z;
    if (!g_wh_placed) return;
    for (m = 0; m < 2; m++) {
        float s = g_wh_s[m];
        if (s <= 0.001f) continue;
        float dx = o[0] - g_wh_x[m], dz = o[1] - g_wh_z[m];
        float r = w_sqrt(dx * dx + dz * dz);
        if (r >= WH_RIM) continue;
        float u = 1.0f - r / WH_RIM;
        float tw = s * (2.6f * u * u + 0.25f * u * w_sin(g_time * 1.9f + r * 0.6f));
        float ca = w_cos(tw), sa = w_sin(tw);
        o[0] = g_wh_x[m] + ca * dx - sa * dz;
        o[1] = g_wh_z[m] + sa * dx + ca * dz;
    }
}

#ifdef CASTLES_DEBUG_WORM
static long g_whc_upd, g_whc_emit, g_whc_max, g_whc_last;
static int g_whc_n;
/* Logs the wormhole's average and worst per-frame cost every 5 s. */
static void wormhole_cost(long upd, long emit, long now) {
    g_whc_upd = g_whc_upd + upd; g_whc_emit = g_whc_emit + emit; g_whc_n = g_whc_n + 1;
    if (upd + emit > g_whc_max) g_whc_max = upd + emit;
    if (g_whc_last == 0) g_whc_last = now;
    if (now - g_whc_last < 5000000) return;
    int rays = 0, i;
    for (i = 0; i < WH_NP; i++) rays = rays + wp_on[i];
    __android_log_print(4, "castles", "wormhole cost (%d-D): update %ld us + draw %ld us per frame on average, worst %ld us; %d rays, %d in transit",
                        g_wh_dim, g_whc_upd / g_whc_n, g_whc_emit / g_whc_n, g_whc_max, rays, wh_in_transit());
    g_whc_upd = 0; g_whc_emit = 0; g_whc_n = 0; g_whc_max = 0; g_whc_last = now;
}
#endif

/* Push constants for castles.vert: {x, z, strength, throat} per mouth. */
static void wormhole_push(float *w) {
    int m;
    for (m = 0; m < 2; m++) {
        w[m * 4 + 0] = g_wh_x[m]; w[m * 4 + 1] = g_wh_z[m];
        w[m * 4 + 2] = g_wh_placed ? g_wh_s[m] : 0.0f;
        w[m * 4 + 3] = g_wh_b[m];
    }
}

/* ---- drawing (flat, at ground level: the shader bends it) ---- */
/* Everything sits on one polar lattice per mouth: WH_NA angles, and a
 * ground-height table every 0.5 out to the rim, refreshed a few angles
 * per frame. That avoids thousands of terrain lookups and sin/cos calls a
 * frame. */

static void wh_ht_refresh(int m, int rows) {
    int k, j;
    if (!g_wh_ht_ok[m] || g_wh_ht_x[m] != g_wh_x[m] || g_wh_ht_z[m] != g_wh_z[m]) {
        if (g_wh_ca[1] == 0.0f)
            for (k = 0; k < WH_NA; k++) { g_wh_ca[k] = w_cos((float)k * (2.0f * W_PI / (float)WH_NA)); g_wh_sa[k] = w_sin((float)k * (2.0f * W_PI / (float)WH_NA)); }
        g_wh_ht_x[m] = g_wh_x[m]; g_wh_ht_z[m] = g_wh_z[m];
        rows = WH_NA;                                   /* moved: rebuild it all */
        g_wh_ht_ok[m] = 1;
    }
    for (k = 0; k < rows; k++) {
        int a = (g_wh_ht_row[m] + k) % WH_NA;
        for (j = 0; j < WH_NRH; j++) {
            float r = 0.5f * (float)j;
            g_wh_ht[(m * WH_NA + a) * WH_NRH + j] = wh_ground(g_wh_x[m] + r * g_wh_ca[a], g_wh_z[m] + r * g_wh_sa[a]);
        }
    }
    g_wh_ht_row[m] = (g_wh_ht_row[m] + rows) % WH_NA;
}

/* Lattice point at radius r on angle index a (any integer), lifted. */
static void wh_pt(float r, int a, float lift, float *p) {
    a = ((a % WH_NA) + WH_NA) % WH_NA;
    float fr = w_clampf(r * 2.0f, 0.0f, (float)(WH_NRH - 1) - 0.001f);
    int j = (int)fr;
    float t = fr - (float)j;
    const float *h = g_wh_ht + (g_whm * WH_NA + a) * WH_NRH;
    p[0] = g_wh_x[g_whm] + r * g_wh_ca[a];
    p[2] = g_wh_z[g_whm] + r * g_wh_sa[a];
    p[1] = h[j] + (h[j + 1] - h[j]) * t + lift;
}

/* Polar quad (r0..r1) x (angle indices a0..a1), facing up. */
static void wh_quad(float r0, float r1, int a0, int a1, float lift) {
    float p0[3], p1[3], p2[3], p3[3], n[3];
    n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
    wh_pt(r0, a0, lift, p0); wh_pt(r1, a0, lift, p1);
    wh_pt(r1, a1, lift, p2); wh_pt(r0, a1, lift, p3);
    u_face_quad(p0, p1, p2, p3, n);
}

/* A spoke along angle index a from r0 to r1, half-width w. */
static void wh_spoke(float r0, float r1, int a, float w, float lift) {
    float p0[3], p1[3], p2[3], p3[3], n[3];
    n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
    wh_pt(r0, a, lift, p0); wh_pt(r1, a, lift, p1);
    a = ((a % WH_NA) + WH_NA) % WH_NA;
    float ox = -g_wh_sa[a] * w, oz = g_wh_ca[a] * w;
    p2[0] = p1[0] + ox; p2[1] = p1[1]; p2[2] = p1[2] + oz;
    p3[0] = p0[0] + ox; p3[1] = p0[1]; p3[2] = p0[2] + oz;
    p0[0] = p0[0] - ox; p0[2] = p0[2] - oz;
    p1[0] = p1[0] - ox; p1[2] = p1[2] - oz;
    u_face_quad(p0, p1, p2, p3, n);
}

/* The space-time grid of the funnel: rings and spokes. */
static void wh_grid(float s, float b) {
    int k, j;
    float rim = WH_RIM - 0.3f;
    for (k = 1; k <= 8; k++) {
        float r = b + (rim - b) * (float)k / 8.0f;
        float g = (float)k / 8.0f;
        u_color((0.55f + 0.5f * (1.0f - g)) * s, (0.30f + 0.5f * g) * s, 1.2f * s);
        for (j = 0; j < WH_NA; j++) wh_quad(r - 0.06f, r + 0.06f, j, j + 1, 0.07f);
    }
    for (j = 0; j < WH_NA; j += 2) {
        for (k = 0; k < 6; k++) {
            float r0 = b + (rim - b) * (float)k / 6.0f, r1 = b + (rim - b) * (float)(k + 1) / 6.0f;
            float g = (float)k / 6.0f;
            u_color((0.9f - 0.4f * g) * s, (0.5f + 0.4f * g) * s, 1.3f * s);
            wh_spoke(r0, r1, j, 0.07f, 0.08f);
        }
    }
}

/* The throat: the unit disc |zeta| < 1 of the ground plane, sent through
 * the N-ball's Mobius automorphism phi_a, with a circling the origin and
 * leaning into the extra dimensions, then through the rotation Q(t). Q(t)
 * swings back and forth by linear interpolation between this event's Q
 * and the last one. Colour: a checker in the image's ground-plane angle
 * and radius, tinted by how far it reaches into the extra dimensions. */
static void wh_portal(float s, float b) {
    float t = g_time;
    int D = g_wh_dim, k, j;
    wv_copy(g_wa, g_wev, D);
    for (j = 0; j < D; j++) { float q = g_wa[j] * 0.33f; g_wa[j] = q; }
    g_wa[0] = 0.45f * w_cos(t * 0.9f); g_wa[1] = 0.45f * w_sin(t * 0.9f);
    float aa = wv_dot(g_wa, g_wa, D);
    wq_lerp(g_wh_qp, g_wh_qa, g_wh_qb, D, 0.5f + 0.5f * w_sin(t * 0.6f));
    wq_apply(g_wa, g_wy, D, g_wh_qp, 1.0f);           /* Q a, once for every cell */
    float qa0 = g_wy[0], qa1 = g_wy[1], qah = wnd_out_of_plane(g_wy, D);
    float cq = w_cos(g_wh_qp[0]), sq = w_sin(g_wh_qp[0]);
    float cf[2];
    for (k = 0; k < 6; k++) {
        float r0 = b * (float)k / 6.0f, r1 = b * (float)(k + 1) / 6.0f;
        float rc = ((float)k + 0.5f) / 6.0f;
        for (j = 0; j < WH_NA; j += 2) {
            float x0 = rc * g_wh_ca[(j + 1) % WH_NA], x1 = rc * g_wh_sa[(j + 1) % WH_NA];
            /* Q phi_a(x) = c1 Q x + c2 Q a (wb_plane_coef) */
            wb_plane_coef(x0, x1, g_wa, aa, cf);
            float y0 = cf[0] * (cq * x0 - sq * x1) + cf[1] * qa0;
            float y1 = cf[0] * (sq * x0 + cq * x1) + cf[1] * qa1;
            float hid = w_absf(cf[1]) * qah;
            float m = w_sqrt(y0 * y0 + y1 * y1);
            float ang = approx_atan2_w(y1, y0);
            int c1 = w_ifloor((ang + W_PI) * (8.0f / (2.0f * W_PI)));
            int c2 = w_ifloor(m * 5.0f);
            float glow = 1.0f - m * 0.6f;
            if (((c1 + c2) & 1) == 0) u_color((1.2f + glow - hid) * s, (0.7f + glow + 1.5f * hid) * s, 2.0f * s);
            else u_color((0.35f + 0.8f * hid) * s, 0.05f * s, (0.6f + 0.4f * glow) * s);
            wh_quad(r0, r1, j, j + 2, 0.12f);
        }
    }
}

static float wp_st_through(int i) { return wp_st[i * 3] > 0.0f ? 1.0f : 0.0f; }

static void wh_spark(float x, float y, float z, float e) {
    float M[12];
    M[0] = 1.0f; M[1] = 0.0f; M[2] = 0.0f; M[3] = 0.0f; M[4] = 1.0f; M[5] = 0.0f;
    M[6] = 0.0f; M[7] = 0.0f; M[8] = 1.0f; M[9] = x; M[10] = y; M[11] = z;
    u_box(M, 0.0f, 0.0f, 0.0f, e, e, e);
}

static void wormhole_emit(void) {
    if (!g_wh_placed) return;
    int m, i, D = g_wh_dim;
    wnd_view_axis(g_wev, D, g_time);
    for (m = 0; m < 2; m++) {
        float s = g_wh_s[m];
        if (s <= 0.01f) continue;
        float dx = g_wh_x[m] - g_cam[0], dz = g_wh_z[m] - g_cam[2];
        if (dx * dx + dz * dz > 170.0f * 170.0f) continue;
        g_whm = m;
        wh_ht_refresh(m, 4);
        float b = g_wh_b[m];
        float sv = 0.35f + 0.65f * s;
        wh_grid(sv, b);
        wh_portal(sv, b);
        u_color(1.4f * sv, 0.8f * sv, 2.0f * sv);             /* event horizon-like rim of the throat */
        for (i = 0; i < WH_NA; i++) wh_quad(b - 0.05f, b + 0.25f, i, i + 1, 0.14f);
    }
    /* light rays on their N-dimensional geodesics: ground position from
     * dimensions 0 and 1, height from the distance out of the ground plane,
     * colour from which way along the view axis through the extra ones */
    for (i = 0; i < WH_NP; i++) {
        if (!wp_on[i]) continue;
        wnd_circle(wp_n0 + i * WH_DIM_MAX, wp_t0 + i * WH_DIM_MAX, wp_st[i * 3 + 2], D, g_wy);
        int mm = wnd_place_n(wp_st[i * 3], g_wy, wp_src[i], g_wh_b[wp_src[i]], g_wh_q, D, g_wx);
        float x = g_wh_x[mm] + g_wx[0], z = g_wh_z[mm] + g_wx[1];
        float dx = x - g_cam[0], dz = z - g_cam[2];
        if (dx * dx + dz * dz > 150.0f * 150.0f) continue;
        float out = wnd_out_of_plane(g_wx, D);
        float side = out > 0.01f ? wv_dot(g_wx, g_wev, D) / out : 0.0f;
        float through = wp_st_through(i);
        if (wp_h[i] < g_wh_b[wp_src[i]]) u_color(1.6f + 0.4f * side, 1.3f - 0.6f * through - 0.3f * side, 2.2f - 0.4f * side);
        else u_color(0.7f + 0.5f * side, 1.7f, 2.0f - 0.5f * side);   /* turned back by the barrier */
        wh_spark(x, wh_ground(x, z) + 0.5f + WH_HSCALE * out, z, 0.11f);
    }
}
