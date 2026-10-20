/* ---- castles_ballistics.h: arrow physics for castles_units.h ----
 *
 * Projectiles are point masses integrated at a fixed 120 Hz step:
 *
 *   each step:  bal_add_force(gravity) + bal_add_force(drag)   (env forces)
 *               a = F/m;  v += a*dt;  p += v*dt;  F = 0        (semi-implicit Euler)
 *
 * Forces only ever enter through bal_add_force(), which accumulates them
 * until the next integration step clears the accumulator. A bow shot is
 * one bal_add_force() of the launch force at release, acting for exactly
 * one step (an impulse of F*dt).
 *
 * Aiming (bal_solve) finds the launch force needed to hit a point at a
 * given range by running THE SAME integrator -- same step function, same
 * dt, same start state -- on a scratch body, bisecting on the force
 * magnitude until the simulated arc passes through the aim point. No
 * closed-form approximation is involved (there isn't one with quadratic
 * drag), so a live arrow launched with the solved force follows the
 * predicted arc step for step and arrives at the aim point to within the
 * bisection tolerance.
 *
 * Pure C (only the math helpers from castles_world.h), so the host
 * checker castles_world_hosttest.c builds it too; bal_selftest() is
 * logged on device and printed on host for comparison. */

#ifndef CASTLES_BALLISTICS_H
#define CASTLES_BALLISTICS_H

#define BAL_DT        (1.0f / 120.0f)
#define BAL_G         9.81f
#define ARROW_MASS    0.06f      /* kg                               */
#define ARROW_DRAG    0.00012f   /* N per (m/s)^2, quadratic drag     */
#define BAL_FMAX      420.0f     /* strongest bow: ~58 m/s at release */
#define BAL_MAXSTEPS  720        /* 6 s of flight                     */
#define MAXP          160        /* live projectiles                  */
#define BAL_SCRATCH   MAXP       /* extra body used by the solver     */

static int   p_active[MAXP + 1];
static float p_x[MAXP + 1], p_y[MAXP + 1], p_z[MAXP + 1];
static float p_vx[MAXP + 1], p_vy[MAXP + 1], p_vz[MAXP + 1];
static float p_fx[MAXP + 1], p_fy[MAXP + 1], p_fz[MAXP + 1];
static float p_mass[MAXP + 1];
static float g_bal_flight_time;   /* time to the target of the last solved shot */

static void bal_reset(int i, float x, float y, float z, float mass) {
    p_x[i] = x; p_y[i] = y; p_z[i] = z;
    p_vx[i] = 0.0f; p_vy[i] = 0.0f; p_vz[i] = 0.0f;
    p_fx[i] = 0.0f; p_fy[i] = 0.0f; p_fz[i] = 0.0f;
    p_mass[i] = mass;
}

/* Accumulates a force (N) on body i until the next integration step. */
static void bal_add_force(int i, float fx, float fy, float fz) {
    float nx = p_fx[i] + fx, ny = p_fy[i] + fy, nz = p_fz[i] + fz;
    p_fx[i] = nx; p_fy[i] = ny; p_fz[i] = nz;
}

/* Gravity and quadratic air drag (-c |v| v) for this step. */
static void bal_env_forces(int i) {
    float vx = p_vx[i], vy = p_vy[i], vz = p_vz[i];
    float speed = w_sqrt(vx * vx + vy * vy + vz * vz);
    float k = -ARROW_DRAG * speed;
    bal_add_force(i, 0.0f, -BAL_G * p_mass[i], 0.0f);
    bal_add_force(i, k * vx, k * vy, k * vz);
}

/* One fixed step: integrate the accumulated forces, then clear them. */
static void bal_integrate(int i) {
    float inv = 1.0f / p_mass[i];
    float nvx = p_vx[i] + p_fx[i] * inv * BAL_DT;
    float nvy = p_vy[i] + p_fy[i] * inv * BAL_DT;
    float nvz = p_vz[i] + p_fz[i] * inv * BAL_DT;
    p_vx[i] = nvx; p_vy[i] = nvy; p_vz[i] = nvz;
    float nx = p_x[i] + nvx * BAL_DT, ny = p_y[i] + nvy * BAL_DT, nz = p_z[i] + nvz * BAL_DT;
    p_x[i] = nx; p_y[i] = ny; p_z[i] = nz;
    p_fx[i] = 0.0f; p_fy[i] = 0.0f; p_fz[i] = 0.0f;
}

/* The full per-step update every arrow (and the solver) uses. */
static void bal_step(int i) {
    bal_env_forces(i);
    bal_integrate(i);
}

/* Flies the scratch body from `from` with launch force (fx,fy,fz) until
 * it has covered horizontal distance `dist`; returns its height there
 * minus `ty` (linearly interpolated within the crossing step), or a big
 * negative number if it comes down short. */
static float bal_height_error(const float *from, float fx, float fy, float fz, float dist, float ty) {
    int s = BAL_SCRATCH, k;
    bal_reset(s, from[0], from[1], from[2], ARROW_MASS);
    bal_add_force(s, fx, fy, fz);
    float prevD = 0.0f, prevY = from[1];
    for (k = 0; k < BAL_MAXSTEPS; k++) {
        bal_step(s);
        float dx = p_x[s] - from[0], dz = p_z[s] - from[2];
        float d = w_sqrt(dx * dx + dz * dz);
        if (d >= dist) {
            float t = (dist - prevD) / (d - prevD);
            float y = prevY + (p_y[s] - prevY) * t;
            g_bal_flight_time = ((float)k + t) * BAL_DT;
            return y - ty;
        }
        if (p_y[s] < ty - 40.0f) break;
        prevD = d; prevY = p_y[s];
    }
    return -1.0e6f;
}

/* Launch force (written to out[0..2]) that puts an arrow fired from
 * `from` through `to`. Tries elevations from flat to lofted and keeps
 * the flattest that reaches; for each, bisects the force magnitude
 * against the integrator until the arc's height error at the target's
 * range is < 0.5 mm. Returns 1 on success, 0 if out of range; the
 * shot's flight time to the target is left in g_bal_flight_time. */
#define BAL_ANGLES 6

/* Solves for one elevation (index 0..BAL_ANGLES-1: 5, 15, ... 55 degrees
 * above the horizontal). Returns 1 and the force if that arc reaches. */
static int bal_solve_at(const float *from, const float *to, int a, float *out) {
    float dx = to[0] - from[0], dz = to[2] - from[2];
    float dist = w_sqrt(dx * dx + dz * dz);
    if (dist < 0.3f) return 0;
    float hx = dx / dist, hz = dz / dist;
    {
        float ang = (5.0f + 10.0f * (float)a) * (W_PI / 180.0f);
        float ca = w_cos(ang), sa = w_sin(ang);
        float ux = hx * ca, uy = sa, uz = hz * ca;
        float hiF = BAL_FMAX;
        if (bal_height_error(from, ux * hiF, uy * hiF, uz * hiF, dist, to[1]) < 0.0f) return 0;
        float loF = 0.0f, f = hiF;
        int it;
        for (it = 0; it < 48; it++) {
            float mid = 0.5f * (loF + hiF);
            float e = bal_height_error(from, ux * mid, uy * mid, uz * mid, dist, to[1]);
            f = mid;     /* g_bal_flight_time now belongs to this force */
            if (e > -0.0005f && e < 0.0005f) break;    /* the force just verified */
            if (e < 0.0f) loF = mid; else hiF = mid;
        }
        out[0] = ux * f; out[1] = uy * f; out[2] = uz * f;
        return 1;
    }
}

/* Flattest elevation that reaches (no obstacle awareness -- callers that
 * care about walls validate each candidate, see archer_solve). */
static int bal_solve(const float *from, const float *to, float *out) {
    int a;
    for (a = 0; a < BAL_ANGLES; a++) if (bal_solve_at(from, to, a, out)) return 1;
    return 0;
}

/* Integer checksums for host vs device comparison: for a few shots,
 * solved force (mN) and the miss distance (micrometres) of a live
 * re-simulation through the target's range. */
static int bal_selftest(int *out) {
    int n = 0, k;
    for (k = 0; k < 4; k++) {
        float from[3], to[3], f[3];
        from[0] = 0.0f; from[1] = 1.5f; from[2] = 0.0f;
        float dist = (k == 0) ? 10.0f : (k == 1) ? 20.0f : (k == 2) ? 28.0f : 15.0f;
        float dh = (k == 0) ? 0.0f : (k == 1) ? 2.0f : (k == 2) ? -4.0f : 6.0f;
        to[0] = dist * 0.6f; to[1] = 1.5f + dh; to[2] = -dist * 0.8f;
        if (!bal_solve(from, to, f)) { out[n] = -1; n++; out[n] = -1; n++; continue; }
        float mag = w_sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        out[n] = (int)(mag * 1000.0f); n++;
        float e = bal_height_error(from, f[0], f[1], f[2], dist, to[1]);
        out[n] = (int)(e * 1000000.0f); n++;
    }
    return n;
}

#endif /* CASTLES_BALLISTICS_H */
