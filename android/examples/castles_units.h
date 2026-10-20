/* ---- castles_units.h: humanoid units for castles_android.c ----
 *
 * Render-thread game logic, included by castles_android.c after its chunk
 * slot tables (it reads each loaded chunk's nav grid -- see the
 * "Navigation grid" section of castles_world.h -- through them).
 *
 *   - Nav access: walkability/height of any 1-unit nav node in world
 *     space, across loaded chunks, plus the exact terrain surface.
 *   - Flow fields: a move/attack order builds a 160x160x2 nav window
 *     around the order, seeds the goal with distance 0 and hands it to
 *     castles_android.c, which runs the castles_nav.comp relaxation on the
 *     GPU until it converges and copies the distances back here. Units
 *     then just step to their lowest-distance neighbor (field_best_step,
 *     same movement rules as the shader), so a whole group shares one GPU
 *     field and needs no per-unit search at all.
 *   - Units: player squad + castle guards, procedurally generated
 *     appearance (random face colour, hair or helmet, proportions, team
 *     tunic; ~40% are archers), combat, and procedural animation (idle,
 *     walk, sword swing, bow draw/release, ladder climb, death) built as a
 *     hierarchy of transformed boxes.
 *   - Bows: archers shoot real projectiles (castles_ballistics.h) whose
 *     launch force is solved by integrating the flight itself.
 *   - Selection (tap / rectangle) and orders (move / attack).
 *
 * Same squash ARM64 conventions as castles_world.h: no non-zero global
 * initializers, no "x += call()", <= 6 float parameters, plain parallel
 * arrays instead of arrays of structs. */

#ifndef CASTLES_UNITS_H
#define CASTLES_UNITS_H

/* ------------------------------------------------------------------ */
/* Nav access over loaded chunks                                       */
/* ------------------------------------------------------------------ */
static int g_nav_cache_slot = -1, g_nav_cache_cx, g_nav_cache_cz;

static int nav_slot_for(int ccx, int ccz) {
    if (g_nav_cache_slot >= 0 && g_nav_cache_cx == ccx && g_nav_cache_cz == ccz &&
        g_slot_used[g_nav_cache_slot] && g_slot_cx[g_nav_cache_slot] == ccx && g_slot_cz[g_nav_cache_slot] == ccz)
        return g_nav_cache_slot;
    int s = find_slot(ccx, ccz);
    if (s >= 0) { g_nav_cache_slot = s; g_nav_cache_cx = ccx; g_nav_cache_cz = ccz; }
    return s;
}

/* Node at integer world cell (wx,wz) in `layer`: returns flags (0 if
 * unwalkable or not loaded) and writes its height. */
static int nav_node(int layer, int wx, int wz, float *h) {
    int ccx = w_ifloor((float)wx / (float)NAV_C), ccz = w_ifloor((float)wz / (float)NAV_C);
    int s = nav_slot_for(ccx, ccz);
    if (s < 0) { *h = 0.0f; return 0; }
    int lx = wx - ccx * NAV_C, lz = wz - ccz * NAV_C;
    int k = s * (NAV_L * NAV_N) + layer * NAV_N + lz * NAV_C + lx;
    *h = g_slot_navh[k];
    return g_slot_navf[k];
}

/* Exact terrain mesh height (same triangulation as w_mesh_height). */
static float nav_terrain_height(float x, float z) {
    int ccx = w_ifloor(x / W_CHUNK_SIZE), ccz = w_ifloor(z / W_CHUNK_SIZE);
    int s = nav_slot_for(ccx, ccz);
    if (s < 0) return world_ground_height(x, z);
    float lx = x - (float)ccx * W_CHUNK_SIZE, lz = z - (float)ccz * W_CHUNK_SIZE;
    float gx = lx / W_STEP, gz = lz / W_STEP;
    int i = w_ifloor(gx), j = w_ifloor(gz);
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    if (i > W_GRID - 1) i = W_GRID - 1;
    if (j > W_GRID - 1) j = W_GRID - 1;
    float fx = gx - (float)i, fz = gz - (float)j;
    float *hg = g_slot_hgt + s * (W_GRIDV * W_GRIDV);
    float h00 = hg[j * W_GRIDV + i];
    float h10 = hg[j * W_GRIDV + i + 1];
    float h01 = hg[(j + 1) * W_GRIDV + i];
    float h11 = hg[(j + 1) * W_GRIDV + i + 1];
    if (fx >= fz) return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
    return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
}

/* Height a unit stands at on `layer` at (x,z). */
static float nav_surface(int layer, float x, float z) {
    float h;
    int f = nav_node(layer, w_ifloor(x), w_ifloor(z), &h);
    if (layer == 0 && (f & NAV_TERRAIN)) return nav_terrain_height(x, z);
    if (!f && layer == 0) return nav_terrain_height(x, z);
    return h;
}

/* ------------------------------------------------------------------ */
/* Flow fields (GPU-computed; see castles_nav.comp)                    */
/* ------------------------------------------------------------------ */
#define NAVW        160
#define NAVW_N      (NAVW * NAVW * NAV_L)
#define NFIELD      4
#define FIELD_INF   1.0e9f

#define FS_FREE      0
#define FS_UPLOAD    1   /* built on CPU, waiting for GPU upload       */
#define FS_COMPUTING 2   /* GPU relaxation running                     */
#define FS_READY     3   /* distances copied back, units can follow    */

static float g_fh[NFIELD * NAVW_N];
static uint32_t g_ff[NFIELD * NAVW_N];
static float g_fd[NFIELD * NAVW_N];
static int g_field_state[NFIELD];
static int g_field_refs[NFIELD];
static int g_field_ox[NFIELD], g_field_oz[NFIELD];
static float g_field_gx[NFIELD], g_field_gy[NFIELD], g_field_gz[NFIELD];
static float g_field_radius[NFIELD];
static int g_field_partial[NFIELD];      /* goal lies outside the window: a waypoint leg */
static int g_field_age[NFIELD];
static int g_field_counter;

static int g_ddx[4], g_ddz[4];   /* D_N..D_W offsets, filled by units_init */

/* CPU twin of castles_nav.comp: best next node from (x,z,layer) in field
 * f, or 0 if no neighbor is closer to the goal. Window-local coords. */
static int field_best_step(int f, int x, int z, int layer, int *bx, int *bz, int *bl) {
    int WW = NAVW * NAVW, base = f * NAVW_N;
    int i = base + layer * WW + z * NAVW + x;
    if (!(g_ff[i] & NAV_WALK)) return 0;
    float hi = g_fh[i];
    float best = g_fd[i];
    int found = 0, dx, dz, m;
    for (dz = -1; dz <= 1; dz++) {
        for (dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dz == 0) continue;
            int nx = x + dx, nz = z + dz;
            if (nx < 0 || nz < 0 || nx >= NAVW || nz >= NAVW) continue;
            int diag = (dx != 0 && dz != 0);
            for (m = 0; m < NAV_L; m++) {
                int j = base + m * WW + nz * NAVW + nx;
                if (!(g_ff[j] & NAV_WALK)) continue;
                if (w_absf(g_fh[j] - hi) > NAV_MAXSTEP) continue;
                if (diag) {
                    int ja = base + m * WW + z * NAVW + nx;
                    int jb = base + m * WW + nz * NAVW + x;
                    if (!(g_ff[ja] & NAV_WALK) || !(g_ff[jb] & NAV_WALK)) continue;
                    if (w_absf(g_fh[ja] - hi) > NAV_MAXSTEP || w_absf(g_fh[jb] - hi) > NAV_MAXSTEP) continue;
                }
                if (g_fd[j] < best - 0.0005f) { best = g_fd[j]; *bx = nx; *bz = nz; *bl = m; found = 1; }
            }
        }
    }
    if (g_ff[i] & NAV_LADDER) {
        int dir = (int)((g_ff[i] >> 2) & 3u);
        int lx = x + g_ddx[dir], lz = z + g_ddz[dir];
        if (lx >= 0 && lz >= 0 && lx < NAVW && lz < NAVW) {
            for (m = 0; m < NAV_L; m++) {
                if (m == layer) continue;
                int j = base + m * WW + lz * NAVW + lx;
                if ((g_ff[j] & NAV_WALK) && (g_ff[j] & NAV_LADDER) && g_fd[j] < best - 0.0005f) {
                    best = g_fd[j]; *bx = lx; *bz = lz; *bl = m; found = 1;
                }
            }
        }
    }
    return found;
}

static float field_dist_at(int f, float x, float z, int layer) {
    int lx = w_ifloor(x) - g_field_ox[f], lz = w_ifloor(z) - g_field_oz[f];
    if (lx < 0 || lz < 0 || lx >= NAVW || lz >= NAVW) return FIELD_INF;
    return g_fd[f * NAVW_N + layer * NAVW * NAVW + lz * NAVW + lx];
}

/* Builds a field for a goal; centerX/Z is where the window is centered
 * (midpoint of the group and the goal). Returns field index or -1. */
static int field_create(float gx, float gy, float gz, int glayer, float centerX, float centerZ, int groupSize) {
    int f, best = -1, bestAge = 1 << 30;
    for (f = 0; f < NFIELD; f++) {
        if (g_field_refs[f] == 0 && g_field_state[f] != FS_UPLOAD && g_field_state[f] != FS_COMPUTING) {
            if (g_field_age[f] < bestAge) { bestAge = g_field_age[f]; best = f; }
        }
    }
    if (best < 0) {
        /* All busy: recycle the oldest field (its units will re-idle). */
        for (f = 0; f < NFIELD; f++) if (g_field_age[f] < bestAge) { bestAge = g_field_age[f]; best = f; }
    }
    f = best;
    g_field_counter = g_field_counter + 1;
    g_field_age[f] = g_field_counter;
    int ox = w_ifloor(centerX) - NAVW / 2, oz = w_ifloor(centerZ) - NAVW / 2;
    g_field_ox[f] = ox; g_field_oz[f] = oz;
    g_field_partial[f] = !(gx >= (float)(ox + 2) && gz >= (float)(oz + 2) &&
                           gx < (float)(ox + NAVW - 2) && gz < (float)(oz + NAVW - 2));
    g_field_gx[f] = gx; g_field_gy[f] = gy; g_field_gz[f] = gz;
    float radius = 0.7f + 0.55f * w_sqrt((float)groupSize);
    g_field_radius[f] = radius;
    int base = f * NAVW_N, WW = NAVW * NAVW;
    int l, x, z, seeded = 0;
    for (l = 0; l < NAV_L; l++) {
        for (z = 0; z < NAVW; z++) {
            for (x = 0; x < NAVW; x++) {
                float h;
                int fl = nav_node(l, ox + x, oz + z, &h);
                int k = base + l * WW + z * NAVW + x;
                g_fh[k] = h;
                g_ff[k] = (uint32_t)fl;
                g_fd[k] = FIELD_INF;
                if ((fl & NAV_WALK) && l == glayer) {
                    float dx = (float)(ox + x) + 0.5f - gx, dz = (float)(oz + z) + 0.5f - gz;
                    if (dx * dx + dz * dz <= radius * radius && w_absf(h - gy) < 1.6f) { g_fd[k] = 0.0f; seeded++; }
                }
            }
        }
    }
    if (!seeded) {
        /* Goal itself not walkable: seed the nearest walkable node. */
        int bk = -1;
        float bd = 1e30f;
        for (l = 0; l < NAV_L; l++) {
            for (z = 0; z < NAVW; z++) {
                for (x = 0; x < NAVW; x++) {
                    int k = base + l * WW + z * NAVW + x;
                    if (!(g_ff[k] & NAV_WALK)) continue;
                    float dx = (float)(ox + x) + 0.5f - gx, dz = (float)(oz + z) + 0.5f - gz, dy = g_fh[k] - gy;
                    float d = dx * dx + dz * dz + dy * dy;
                    if (d < bd) { bd = d; bk = k; }
                }
            }
        }
        if (bk >= 0) g_fd[bk] = 0.0f;
    }
    g_field_state[f] = FS_UPLOAD;
    g_field_refs[f] = 0;
    return f;
}

/* ------------------------------------------------------------------ */
/* Units (parallel arrays)                                             */
/* ------------------------------------------------------------------ */
#define MAXU        128
#define TEAM_PLAYER 0
#define TEAM_ENEMY  1

#define US_IDLE   0
#define US_WAIT   1   /* waiting for its flow field                    */
#define US_MOVE   2   /* following a flow field                        */
#define US_CHASE  3   /* steering straight at a nearby target          */
#define US_ATTACK 4
#define US_CLIMB  5
#define US_DEAD   6
#define US_SHOOT  7   /* bow: raise, nock, draw, release, recover     */

#define ARCHER_RANGE 26.0f

#define ORD_NONE   0
#define ORD_MOVE   1
#define ORD_ATTACK 2

static int g_un;                                 /* high-water mark      */
static int u_active[MAXU], u_team[MAXU], u_state[MAXU], u_layer[MAXU];
static int u_hp[MAXU], u_maxhp[MAXU];
static float u_x[MAXU], u_y[MAXU], u_z[MAXU], u_yaw[MAXU];
static int u_field[MAXU], u_order[MAXU], u_target[MAXU];
static float u_swing[MAXU], u_phase[MAXU], u_seed[MAXU], u_dead[MAXU], u_hit[MAXU];
static float u_stuck[MAXU];
static int u_selected[MAXU];
static float u_climb_t[MAXU], u_climb_dur[MAXU];
static float u_cfx[MAXU], u_cfy[MAXU], u_cfz[MAXU], u_ctx[MAXU], u_cty[MAXU], u_ctz[MAXU];
static int u_climb_layer[MAXU];
static float u_moved[MAXU];                      /* distance this frame  */
static float u_vx[MAXU], u_vz[MAXU];             /* smoothed velocity    */
static float u_px[MAXU], u_pz[MAXU];             /* last frame position  */
static float u_gx[MAXU], u_gy[MAXU], u_gz[MAXU];   /* order goal           */
static int u_gl[MAXU];
static int u_repath[MAXU];                       /* needs a (next) field */
/* appearance */
static float u_face[MAXU * 3], u_hair[MAXU * 3], u_tunic[MAXU * 3];
static int u_helmet[MAXU];
static int u_archer[MAXU];                       /* bow instead of sword+shield */
static float u_aim[MAXU];                        /* launch elevation (rad)      */
static float u_noshot[MAXU];                     /* >0: no clear shot, melee    */
static float u_scale[MAXU], u_girth[MAXU];

/* Castles already populated with guards (keep positions). */
#define MAX_POP 64
static int g_npop;
static float g_pop_x[MAX_POP], g_pop_z[MAX_POP];
static int g_home_set, g_home_spawned;
static int g_home_cx, g_home_cz;

static void units_init(void) {
    g_ddx[D_N] = 0;  g_ddz[D_N] = -1;
    g_ddx[D_E] = 1;  g_ddz[D_E] = 0;
    g_ddx[D_S] = 0;  g_ddz[D_S] = 1;
    g_ddx[D_W] = -1; g_ddz[D_W] = 0;
    g_nav_cache_slot = -1;
}

static float u_rand01(uint32_t *st) {
    uint32_t x = *st;
    x = x ^ ((x << 13) & 0xFFFFFFFFu);
    x = x ^ (x >> 17);
    x = x ^ ((x << 5) & 0xFFFFFFFFu);
    *st = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

/* HSV (h in [0,1)) to RGB into out[0..2]. */
static void u_hsv(float *out, float h, float s, float v) {
    float h6 = h * 6.0f;
    int sector = w_ifloor(h6);
    float fr = h6 - (float)sector;
    float p = v * (1.0f - s), q = v * (1.0f - s * fr), t = v * (1.0f - s * (1.0f - fr));
    sector = sector % 6;
    if (sector == 0) { out[0] = v; out[1] = t; out[2] = p; }
    else if (sector == 1) { out[0] = q; out[1] = v; out[2] = p; }
    else if (sector == 2) { out[0] = p; out[1] = v; out[2] = t; }
    else if (sector == 3) { out[0] = p; out[1] = q; out[2] = v; }
    else if (sector == 4) { out[0] = t; out[1] = p; out[2] = v; }
    else { out[0] = v; out[1] = p; out[2] = q; }
}

/* Spawns a unit; returns its index or -1. Appearance is procedural from
 * `seed`: any face hue (these are fantasy folk), hair or a helmet,
 * proportions, and a team-tinted tunic. */
static int unit_spawn(int team, float x, float z, int layer, uint32_t seed) {
    int i, idx = -1;
    for (i = 0; i < MAXU; i++) if (!u_active[i]) { idx = i; break; }
    if (idx < 0) return -1;
    if (idx >= g_un) g_un = idx + 1;
    uint32_t st = w_hash(seed * 2654435761u + 12345u) | 1u;
    u_active[idx] = 1;
    u_team[idx] = team;
    u_state[idx] = US_IDLE;
    u_layer[idx] = layer;
    u_maxhp[idx] = team == TEAM_PLAYER ? 6 : 4;
    u_hp[idx] = u_maxhp[idx];
    u_x[idx] = x; u_z[idx] = z;
    u_px[idx] = x; u_pz[idx] = z; u_vx[idx] = 0.0f; u_vz[idx] = 0.0f;
    u_y[idx] = nav_surface(layer, x, z);
    u_yaw[idx] = u_rand01(&st) * 6.2831853f;
    u_field[idx] = -1; u_order[idx] = ORD_NONE; u_target[idx] = -1; u_repath[idx] = 0;
    u_swing[idx] = 0.0f; u_phase[idx] = 0.0f; u_dead[idx] = 0.0f; u_hit[idx] = 0.0f; u_stuck[idx] = 0.0f;
    u_seed[idx] = u_rand01(&st) * 100.0f;
    u_selected[idx] = 0;
    float c[3];
    float hue = u_rand01(&st);
    float sat = 0.30f + 0.45f * u_rand01(&st);
    float val = 0.62f + 0.33f * u_rand01(&st);
    u_hsv(c, hue, sat, val);
    u_face[idx * 3 + 0] = c[0]; u_face[idx * 3 + 1] = c[1]; u_face[idx * 3 + 2] = c[2];
    u_helmet[idx] = u_rand01(&st) < 0.4f;
    u_archer[idx] = u_rand01(&st) < 0.4f;
    u_noshot[idx] = 0.0f;
    u_aim[idx] = 0.2f;
    float hairHue = u_rand01(&st);
    u_hsv(c, hairHue, 0.35f + 0.5f * u_rand01(&st), 0.15f + 0.6f * u_rand01(&st));
    u_hair[idx * 3 + 0] = c[0]; u_hair[idx * 3 + 1] = c[1]; u_hair[idx * 3 + 2] = c[2];
    float tj = 0.85f + 0.3f * u_rand01(&st);
    if (team == TEAM_PLAYER) { u_tunic[idx * 3 + 0] = 0.16f * tj; u_tunic[idx * 3 + 1] = 0.36f * tj; u_tunic[idx * 3 + 2] = 0.86f * tj; }
    else { u_tunic[idx * 3 + 0] = 0.80f * tj; u_tunic[idx * 3 + 1] = 0.16f * tj; u_tunic[idx * 3 + 2] = 0.14f * tj; }
    u_scale[idx] = 0.88f + 0.24f * u_rand01(&st);
    u_girth[idx] = 0.88f + 0.30f * u_rand01(&st);
    return idx;
}

static int unit_alive(int i) { return i >= 0 && i < MAXU && u_active[i] && u_state[i] != US_DEAD; }

static void unit_release_field(int i) {
    int f = u_field[i];
    if (f >= 0 && g_field_refs[f] > 0) g_field_refs[f] = g_field_refs[f] - 1;
    u_field[i] = -1;
}

/* Tries to move unit i to (nx,nz): accepted only onto a walkable node
 * (on its own layer, or another layer within step height -- how units
 * walk from a ramp onto a wall-walk). preferLayer < 0: no preference. */
static int unit_try_move(int i, float nx, float nz, int preferLayer) {
    float hCur;
    int cx = w_ifloor(u_x[i]), cz = w_ifloor(u_z[i]);
    int cf = nav_node(u_layer[i], cx, cz, &hCur);
    if (!cf) hCur = u_y[i];
    int tx = w_ifloor(nx), tz = w_ifloor(nz);
    int k;
    for (k = -1; k < NAV_L; k++) {
        int l = (k < 0) ? (preferLayer >= 0 ? preferLayer : u_layer[i]) : k;
        float h;
        int f = nav_node(l, tx, tz, &h);
        if (!(f & NAV_WALK)) continue;
        if (w_absf(h - hCur) > NAV_MAXSTEP + 0.15f) continue;
        u_x[i] = nx; u_z[i] = nz; u_layer[i] = l;
        return 1;
    }
    return 0;
}

/* Moves unit i by (dx,dz), sliding along obstacles. */
static void unit_walk(int i, float dx, float dz, int preferLayer) {
    float ox = u_x[i], oz = u_z[i];
    if (!unit_try_move(i, ox + dx, oz + dz, preferLayer)) {
        if (!unit_try_move(i, ox + dx, oz, preferLayer)) unit_try_move(i, ox, oz + dz, preferLayer);
    }
    float mx = u_x[i] - ox, mz = u_z[i] - oz;
    float m = w_sqrt(mx * mx + mz * mz);
    float nm = u_moved[i] + m;
    u_moved[i] = nm;
}

static void unit_face(int i, float dx, float dz, float dt) {
    if (dx * dx + dz * dz < 1e-6f) return;
    float target = approx_atan2(dx, -dz);
    float d = wrap_angle(target - u_yaw[i]);
    float maxTurn = 9.0f * dt;
    if (d > maxTurn) d = maxTurn;
    if (d < -maxTurn) d = -maxTurn;
    float ny = wrap_angle(u_yaw[i] + d);
    u_yaw[i] = ny;
}

/* ------------------------------------------------------------------ */
/* Orders                                                              */
/* ------------------------------------------------------------------ */
static int g_order_marker_on;
static float g_order_mx, g_order_my, g_order_mz, g_order_mt;

static int units_selected_count(void) {
    int i, n = 0;
    for (i = 0; i < g_un; i++) if (u_selected[i] && unit_alive(i)) n++;
    return n;
}

/* Creates one field for every unit flagged u_repath that shares unit
 * `lead`'s goal. The window keeps the group >= 24 cells from its edge
 * and reaches as far toward the goal as that allows; a goal beyond it
 * makes this a waypoint leg (see unit_arrive). */
static void dispatch_repath_group(int lead) {
    int i, n = 0;
    float cx = 0.0f, cz = 0.0f;
    for (i = 0; i < g_un; i++) {
        if (!u_repath[i] || !unit_alive(i)) continue;
        if (w_absf(u_gx[i] - u_gx[lead]) > 0.5f || w_absf(u_gz[i] - u_gz[lead]) > 0.5f) continue;
        float ncx = cx + u_x[i], ncz = cz + u_z[i];
        cx = ncx; cz = ncz;
        n++;
    }
    if (n == 0) { u_repath[lead] = 0; return; }
    cx = cx / (float)n; cz = cz / (float)n;
    float reach = (float)(NAVW / 2 - 24);
    float dx = (u_gx[lead] - cx) * 0.5f, dz = (u_gz[lead] - cz) * 0.5f;
    float mx = cx + w_clampf(dx, -reach, reach), mz = cz + w_clampf(dz, -reach, reach);
    int attack = u_order[lead] == ORD_ATTACK;
    int f = field_create(u_gx[lead], u_gy[lead], u_gz[lead], u_gl[lead], mx, mz, attack ? 1 : n);
    /* Units on a recycled field lose it. */
    for (i = 0; i < g_un; i++) {
        if (u_active[i] && u_field[i] == f) { u_field[i] = -1; if (u_state[i] == US_MOVE || u_state[i] == US_WAIT) u_state[i] = US_IDLE; }
    }
    float gx = u_gx[lead], gz = u_gz[lead];
    for (i = 0; i < g_un; i++) {
        if (!u_repath[i] || !unit_alive(i)) continue;
        if (w_absf(u_gx[i] - gx) > 0.5f || w_absf(u_gz[i] - gz) > 0.5f) continue;
        u_repath[i] = 0;
        unit_release_field(i);
        u_field[i] = f;
        g_field_refs[f] = g_field_refs[f] + 1;
        u_state[i] = US_WAIT;
        u_stuck[i] = 0.0f;
    }
    __android_log_print(4, "castles", "field %d: %d units -> (%d,%d,%d) layer %d%s", f, n,
                        (int)gx, (int)u_gy[lead], (int)gz, u_gl[lead], g_field_partial[f] ? " [waypoint leg]" : "");
}

static void dispatch_repaths(void) {
    int i;
    for (i = 0; i < g_un; i++) {
        if (u_repath[i] && unit_alive(i)) dispatch_repath_group(i);
        else u_repath[i] = 0;
    }
}

/* Move (targetUnit < 0) or attack order for every selected player unit. */
static void order_move(float gx, float gy, float gz, int glayer, int targetUnit) {
    int i, n = 0;
    for (i = 0; i < g_un; i++) {
        if (!u_selected[i] || !unit_alive(i) || u_team[i] != TEAM_PLAYER) continue;
        u_gx[i] = gx; u_gy[i] = gy; u_gz[i] = gz; u_gl[i] = glayer;
        u_order[i] = targetUnit >= 0 ? ORD_ATTACK : ORD_MOVE;
        u_target[i] = targetUnit;
        u_repath[i] = 1;
        n++;
    }
    if (n == 0) return;
    dispatch_repaths();
    g_order_marker_on = 1;
    g_order_mx = gx; g_order_my = gy; g_order_mz = gz; g_order_mt = 0.0f;
    __android_log_print(4, "castles", "order %s: %d units", targetUnit >= 0 ? "attack" : "move", n);
}

/* ------------------------------------------------------------------ */
/* Spawning                                                            */
/* ------------------------------------------------------------------ */
/* Collects up to `max` walkable node centers of `layer` within radius r
 * of (x,z) (and with |h - y| < dy) into px/pz; returns the count. */
static int nav_collect(int layer, float x, float z, float y, float r, float *px, float *pz, int max) {
    int n = 0, i, j;
    int r0 = (int)r + 1;
    for (j = -r0; j <= r0 && n < max; j++) {
        for (i = -r0; i <= r0 && n < max; i++) {
            int wx = w_ifloor(x) + i, wz = w_ifloor(z) + j;
            float h;
            int f = nav_node(layer, wx, wz, &h);
            if (!(f & NAV_WALK) || (f & NAV_LADDER)) continue;
            float dx = (float)wx + 0.5f - x, dz = (float)wz + 0.5f - z;
            if (dx * dx + dz * dz > r * r) continue;
            if (w_absf(h - y) > 3.0f) continue;
            px[n] = (float)wx + 0.5f; pz[n] = (float)wz + 0.5f;
            n++;
        }
    }
    return n;
}

/* Called for each castle chunk that gets loaded. The home castle gets
 * the player's squad; every other castle gets guards in the courtyard
 * and on the wall-walks. */
static void units_on_castle_loaded(int slot) {
    float kx = g_slot_keep_x[slot], kz = g_slot_keep_z[slot], ky = g_slot_keep_y[slot];
    int i;
    for (i = 0; i < g_npop; i++) {
        float dx = g_pop_x[i] - kx, dz = g_pop_z[i] - kz;
        if (dx * dx + dz * dz < 1.0f) return;
    }
    if (g_npop >= MAX_POP) return;
    g_pop_x[g_npop] = kx; g_pop_z[g_npop] = kz; g_npop = g_npop + 1;
    float px[400], pz[400];
    uint32_t seed = w_hash2(w_ifloor(kx), w_ifloor(kz), 777u);
    int home = g_home_set && g_slot_cx[slot] == g_home_cx && g_slot_cz[slot] == g_home_cz;
    int n = nav_collect(0, kx, kz, ky, 9.0f, px, pz, 400);
    if (home) {
        int k, made = 0;
        for (k = 0; k < 10 && n > 0; k++) {
            int pick = (int)(w_hash(seed + (uint32_t)k * 31u) % (uint32_t)n);
            if (unit_spawn(TEAM_PLAYER, px[pick], pz[pick], 0, seed + (uint32_t)k) >= 0) made++;
        }
        g_home_spawned = 1;
        __android_log_print(4, "castles", "player squad spawned: %d units at home castle", made);
        return;
    }
    int k, guards = 0;
    for (k = 0; k < 3 && n > 0; k++) {
        int pick = (int)(w_hash(seed + (uint32_t)k * 17u) % (uint32_t)n);
        if (unit_spawn(TEAM_ENEMY, px[pick], pz[pick], 0, seed + 100u + (uint32_t)k) >= 0) guards++;
    }
    int nw = nav_collect(1, kx, kz, ky + WALL_H, 16.0f, px, pz, 400);
    for (k = 0; k < 3 && nw > 0; k++) {
        int pick = (int)(w_hash(seed + (uint32_t)k * 53u + 7u) % (uint32_t)nw);
        if (unit_spawn(TEAM_ENEMY, px[pick], pz[pick], 1, seed + 200u + (uint32_t)k) >= 0) guards++;
    }
    __android_log_print(4, "castles", "castle guards spawned: %d (wall nodes %d)", guards, nw);
}

/* ------------------------------------------------------------------ */
/* Simulation                                                          */
/* ------------------------------------------------------------------ */
static int unit_loaded(int i) {
    int ccx = w_ifloor(u_x[i] / W_CHUNK_SIZE), ccz = w_ifloor(u_z[i] / W_CHUNK_SIZE);
    return nav_slot_for(ccx, ccz) >= 0;
}

static int unit_find_enemy(int i, float range, float maxDy) {
    int j, best = -1;
    float bd = range * range;
    for (j = 0; j < g_un; j++) {
        if (!unit_alive(j) || u_team[j] == u_team[i]) continue;
        float dx = u_x[j] - u_x[i], dz = u_z[j] - u_z[i], dy = u_y[j] - u_y[i];
        if (w_absf(dy) > maxDy) continue;
        float d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = j; }
    }
    return best;
}

/* Starts a ladder climb from unit i's position to node (wx,wz,layer). */
static void unit_start_climb(int i, int wx, int wz, int layer) {
    float h;
    nav_node(layer, wx, wz, &h);
    u_cfx[i] = u_x[i]; u_cfy[i] = u_y[i]; u_cfz[i] = u_z[i];
    u_ctx[i] = (float)wx + 0.5f; u_cty[i] = h; u_ctz[i] = (float)wz + 0.5f;
    u_climb_layer[i] = layer;
    u_climb_t[i] = 0.0f;
    u_climb_dur[i] = 0.4f + w_absf(h - u_y[i]) / 1.7f;
    u_state[i] = US_CLIMB;
    __android_log_print(4, "castles", "unit %d climbing ladder: layer %d -> %d (%d.%d -> %d.%d)", i, u_layer[i], layer,
                        (int)u_cfy[i], (int)(u_cfy[i] * 10.0f) % 10, (int)h, (int)(h * 10.0f) % 10);
    /* Face the wall (direction of travel). */
    u_yaw[i] = approx_atan2(u_ctx[i] - u_x[i], -(u_ctz[i] - u_z[i]));
}

static void unit_arrive(int i) {
    int f = u_field[i];
    if (f >= 0 && g_field_partial[f] && u_order[i] != ORD_NONE) {
        /* Reached the edge of this leg's window: queue the next leg for
         * everyone on this leg at once, so the group keeps sharing one
         * GPU field (the window recenters on the group's new centroid). */
        int j;
        for (j = 0; j < g_un; j++) {
            if (u_field[j] != f || !unit_alive(j) || u_state[j] == US_CLIMB) continue;
            unit_release_field(j);
            u_state[j] = US_WAIT;
            u_repath[j] = 1;
        }
        if (u_field[i] == f) { unit_release_field(i); u_state[i] = US_WAIT; u_repath[i] = 1; }
        return;
    }
    unit_release_field(i);
    u_state[i] = US_IDLE;
    if (u_order[i] == ORD_MOVE) u_order[i] = ORD_NONE;
}

static void unit_follow_field(int i, float dt) {
    int f = u_field[i];
    if (f < 0) { u_state[i] = US_IDLE; return; }
    if (g_field_state[f] != FS_READY) return;
    float speed = 3.6f;
    int lx = w_ifloor(u_x[i]) - g_field_ox[f], lz = w_ifloor(u_z[i]) - g_field_oz[f];
    float gdx = g_field_gx[f] - u_x[i], gdz = g_field_gz[f] - u_z[i];
    if (lx < 0 || lz < 0 || lx >= NAVW || lz >= NAVW) {
        /* Outside the field window: head straight for the goal. */
        float d = w_sqrt(gdx * gdx + gdz * gdz);
        if (d < 0.5f) { unit_arrive(i); return; }
        unit_face(i, gdx, gdz, dt);
        unit_walk(i, gdx / d * speed * dt, gdz / d * speed * dt, -1);
        return;
    }
    float here = g_fd[f * NAVW_N + u_layer[i] * NAVW * NAVW + lz * NAVW + lx];
    if (here <= 0.0f || (g_field_partial[f] && here < 4.0f)) { unit_arrive(i); return; }
    int bx = 0, bz = 0, bl = 0;
    if (!field_best_step(f, lx, lz, u_layer[i], &bx, &bz, &bl)) {
        /* Local minimum (goal unreachable from here) or off the grid. */
        if (here >= FIELD_INF * 0.5f) {
            /* Try the other layer at this spot before giving up. */
            int l2 = 1 - u_layer[i];
            float h2;
            int f2 = nav_node(l2, w_ifloor(u_x[i]), w_ifloor(u_z[i]), &h2);
            if ((f2 & NAV_WALK) && w_absf(h2 - u_y[i]) < NAV_MAXSTEP) { u_layer[i] = l2; return; }
        }
        unit_arrive(i);
        return;
    }
    int wx = bx + g_field_ox[f], wz = bz + g_field_oz[f];
    float th;
    nav_node(bl, wx, wz, &th);
    if (bl != u_layer[i] && w_absf(th - u_y[i]) > NAV_MAXSTEP + 0.2f) {
        unit_start_climb(i, wx, wz, bl);
        return;
    }
    float tx = (float)wx + 0.5f - u_x[i], tz = (float)wz + 0.5f - u_z[i];
    float d = w_sqrt(tx * tx + tz * tz);
    if (d < 0.05f) { u_x[i] = (float)wx + 0.5f; u_z[i] = (float)wz + 0.5f; u_layer[i] = bl; return; }
    unit_face(i, tx, tz, dt);
    float step = speed * dt;
    if (step > d) step = d;
    unit_walk(i, tx / d * step, tz / d * step, bl);
    /* Attack order: switch to direct chase once close to the target. */
    if (u_order[i] == ORD_ATTACK && unit_alive(u_target[i])) {
        int t = u_target[i];
        float ax = u_x[t] - u_x[i], az = u_z[t] - u_z[i];
        float r = ARCHER_RANGE * 0.8f;
        if (u_archer[i] && ax * ax + az * az < r * r && w_absf(u_y[t] - u_y[i]) < 14.0f) {
            unit_release_field(i); u_state[i] = US_SHOOT; u_swing[i] = 0.0f;
            return;
        }
        if (ax * ax + az * az < 36.0f && w_absf(u_y[t] - u_y[i]) < 1.5f) { unit_release_field(i); u_state[i] = US_CHASE; }
    }
    /* Arrival: inside the goal radius, or bumping into an arrived squadmate
     * near the goal. */
    /* Height matters: a ramp can pass right under a wall-walk goal. */
    float gd = w_sqrt(gdx * gdx + gdz * gdz);
    int goalLevel = w_absf(u_y[i] - g_field_gy[f]) < 1.2f;
    if (u_order[i] == ORD_MOVE && goalLevel && gd < g_field_radius[f] * 0.6f) { unit_arrive(i); return; }
    if (u_order[i] == ORD_MOVE && goalLevel && here < g_field_radius[f] + 2.5f) {
        int j;
        for (j = 0; j < g_un; j++) {
            if (j == i || !unit_alive(j) || u_team[j] != TEAM_PLAYER || u_state[j] != US_IDLE) continue;
            float ddx = u_x[j] - u_x[i], ddz = u_z[j] - u_z[i];
            if (ddx * ddx + ddz * ddz < 1.1f && w_absf(u_y[j] - u_y[i]) < 1.0f) { unit_arrive(i); return; }
        }
    }
}

static void unit_damage(int t, int amount) {
    if (!unit_alive(t)) return;
    u_hp[t] = u_hp[t] - amount;
    u_hit[t] = 0.25f;
    if (u_hp[t] <= 0) {
        u_hp[t] = 0;
        unit_release_field(t);
        u_state[t] = US_DEAD;
        u_dead[t] = 0.0f;
        u_selected[t] = 0;
        __android_log_print(4, "castles", "unit %d (%s) defeated", t, u_team[t] == TEAM_PLAYER ? "player" : "enemy");
    }
}

/* ------------------------------------------------------------------ */
/* Bows and arrows (physics in castles_ballistics.h)                   */
/* ------------------------------------------------------------------ */
static int a_team[MAXP], a_flying[MAXP];
static float a_life[MAXP];                      /* seconds since release   */
static float a_dx[MAXP], a_dy[MAXP], a_dz[MAXP];/* orientation (unit)      */
static float g_bal_acc;                          /* fixed-step accumulator  */
static int g_arrows_fired, g_arrows_hit;         /* stats for the log       */

/* Where an archer's arrow leaves the bow (left hand, bow arm raised). */
static void archer_release_point(int i, float *out) {
    float s = u_scale[i];
    float fx = w_sin(u_yaw[i]), fz = -w_cos(u_yaw[i]);
    float rx = w_cos(u_yaw[i]), rz = w_sin(u_yaw[i]);
    out[0] = u_x[i] + fx * 0.35f * s - rx * 0.18f * s;
    out[1] = u_y[i] + 1.47f * s;
    out[2] = u_z[i] + fz * 0.35f * s - rz * 0.18f * s;
}

static void archer_aim_point(int t, float lead, float *out) {
    out[0] = u_x[t] + u_vx[t] * lead;
    out[1] = u_y[t] + 1.15f * u_scale[t];
    out[2] = u_z[t] + u_vz[t] * lead;
}

/* What an arrow at (px,py,pz) runs into: 0 nothing, 1 sticks (height
 * written to *stickY), 2 gone (water / outside the loaded world). Wall
 * tops and faces, ground (incl. ramps and bridge decks), and blocked
 * cells up to 2.2 above the terrain (trees, houses, towers, rocks). */
static int arrow_world_hit(float px, float py, float pz, float *stickY) {
    int ccx = w_ifloor(px / W_CHUNK_SIZE), ccz = w_ifloor(pz / W_CHUNK_SIZE);
    if (nav_slot_for(ccx, ccz) < 0) return 2;
    int wx = w_ifloor(px), wz = w_ifloor(pz);
    float h1, h0;
    int f1 = nav_node(1, wx, wz, &h1);
    if ((f1 & NAV_WALK) && py <= h1 && py > h1 - WALL_H - 1.0f) { *stickY = py; return 1; }
    float hg = nav_terrain_height(px, pz);
    if (hg < 0.0f && py < 0.0f) return 2;
    int f0 = nav_node(0, wx, wz, &h0);
    float ground = ((f0 & NAV_WALK) && !(f0 & NAV_TERRAIN)) ? h0 : hg;
    if (py <= ground) { *stickY = ground; return 1; }
    if (!(f0 & NAV_WALK) && hg > -0.4f && py < hg + 2.2f) { *stickY = py; return 1; }
    return 0;
}

/* Flies launch force f from `from` on the solver's scratch body, with the
 * same integrator and the same world test as a live arrow, until it has
 * covered the target's range: 1 if nothing is in the way. */
static int arrow_path_clear(const float *from, const float *f, const float *to) {
    int sb = BAL_SCRATCH, k;
    float dx = to[0] - from[0], dz = to[2] - from[2];
    float dist = w_sqrt(dx * dx + dz * dz);
    bal_reset(sb, from[0], from[1], from[2], ARROW_MASS);
    bal_add_force(sb, f[0], f[1], f[2]);
    for (k = 0; k < BAL_MAXSTEPS; k++) {
        bal_step(sb);
        float ex = p_x[sb] - from[0], ez = p_z[sb] - from[2];
        if (ex * ex + ez * ez >= dist * dist) return 1;
        float sy;
        if (arrow_world_hit(p_x[sb], p_y[sb], p_z[sb], &sy)) return 0;
    }
    return 0;
}

/* Solves the shot from i at t: launch force into f[3]; 1 if there is a
 * clear arc. Tries elevations from flat to lofted; for each, leads a
 * moving target (solve, predict where the target will be after that
 * shot's flight time, re-solve -- three rounds), then flies the final arc
 * against the world and keeps the first one that gets there unobstructed
 * (e.g. lobbing over a wall instead of into it). */
static int archer_solve(int i, int t, float *from, float *f) {
    float to[3];
    int a, k;
    archer_release_point(i, from);
    int ok = 0;
    for (a = 0; a < BAL_ANGLES && !ok; a++) {
        float lead = 0.0f;
        int solved = 1;
        for (k = 0; k < 3 && solved; k++) {
            archer_aim_point(t, lead, to);
            solved = bal_solve_at(from, to, a, f);
            lead = g_bal_flight_time;
        }
        if (solved && arrow_path_clear(from, f, to)) ok = 1;
    }
    if (!ok) return 0;
    float fh = w_sqrt(f[0] * f[0] + f[2] * f[2]);
    u_aim[i] = approx_atan2(f[1], fh);
    return 1;
}

/* Releases an arrow: the launch force is the one bal_solve computed by
 * integrating this exact flight, applied through bal_add_force() for one
 * physics step. */
static void arrow_fire(int i, int t) {
    float from[3], f[3];
    if (!archer_solve(i, t, from, f)) return;
    int k, a = -1;
    for (k = 0; k < MAXP; k++) if (!p_active[k]) { a = k; break; }
    if (a < 0) {
        /* Recycle the oldest stuck arrow. */
        float oldest = -1.0f;
        for (k = 0; k < MAXP; k++) if (!a_flying[k] && a_life[k] > oldest) { oldest = a_life[k]; a = k; }
        if (a < 0) return;
    }
    bal_reset(a, from[0], from[1], from[2], ARROW_MASS);
    bal_add_force(a, f[0], f[1], f[2]);
    g_arrows_fired = g_arrows_fired + 1;
    p_active[a] = 1;
    a_flying[a] = 1;
    a_team[a] = u_team[i];
    a_life[a] = 0.0f;
    float fm = w_sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    a_dx[a] = f[0] / fm; a_dy[a] = f[1] / fm; a_dz[a] = f[2] / fm;
}

/* Sticks arrow a where it is (it stays visible for a while). */
static void arrow_stick(int a, float y) {
    p_y[a] = y;
    a_flying[a] = 0;
    a_life[a] = 0.0f;
}

/* Advances every flying arrow in fixed BAL_DT steps and resolves hits:
 * enemy units (body cylinder), wall-walk tops and wall faces, ground,
 * blocked cells (trees, houses, towers), and water. */
static void arrows_update(float dt) {
    int k, j;
    for (k = 0; k < MAXP; k++) {
        if (!p_active[k] || a_flying[k]) continue;
        float nl = a_life[k] + dt;
        a_life[k] = nl;
        if (nl > 7.0f) p_active[k] = 0;
    }
    float acc = g_bal_acc + dt;
    if (acc > BAL_DT * 12.0f) acc = BAL_DT * 12.0f;   /* don't spiral after a hitch */
    while (acc >= BAL_DT) {
        float nacc = acc - BAL_DT;
        acc = nacc;
        for (k = 0; k < MAXP; k++) {
            if (!p_active[k] || !a_flying[k]) continue;
            bal_step(k);
            float nl = a_life[k] + BAL_DT;
            a_life[k] = nl;
            float vx = p_vx[k], vy = p_vy[k], vz = p_vz[k];
            float sp = w_sqrt(vx * vx + vy * vy + vz * vz);
            if (sp > 0.01f) { a_dx[k] = vx / sp; a_dy[k] = vy / sp; a_dz[k] = vz / sp; }
            float px = p_x[k], py = p_y[k], pz = p_z[k];
            if (nl > 6.0f) { p_active[k] = 0; continue; }
            /* Units of the other team. */
            int hit = 0;
            for (j = 0; j < g_un && !hit; j++) {
                if (!unit_alive(j) || u_team[j] == a_team[k]) continue;
                float dx = px - u_x[j], dz = pz - u_z[j];
                if (dx * dx + dz * dz > 0.42f * 0.42f) continue;
                if (py < u_y[j] + 0.1f || py > u_y[j] + 1.9f * u_scale[j]) continue;
                unit_damage(j, 1);
                g_arrows_hit = g_arrows_hit + 1;
                p_active[k] = 0;
                hit = 1;
            }
            if (hit) continue;
            float sy;
            int wh = arrow_world_hit(px, py, pz, &sy);
            if (wh == 1) arrow_stick(k, sy);
            else if (wh == 2) p_active[k] = 0;
        }
    }
    g_bal_acc = acc;
}

static void units_update(float dt) {
    int i, j;
    for (i = 0; i < g_un; i++) u_moved[i] = 0.0f;
    for (i = 0; i < g_un; i++) {
        if (!u_active[i]) continue;
        if (!unit_loaded(i)) continue;
        if (u_hit[i] > 0.0f) { float nh = u_hit[i] - dt; u_hit[i] = nh; }
        if (u_state[i] == US_DEAD) {
            float nd = u_dead[i] + dt;
            u_dead[i] = nd;
            if (u_dead[i] > 9.0f) u_active[i] = 0;
            continue;
        }
        if (u_state[i] == US_CLIMB) {
            float nt = u_climb_t[i] + dt / u_climb_dur[i];
            u_climb_t[i] = nt;
            if (nt >= 1.0f) {
                u_x[i] = u_ctx[i]; u_y[i] = u_cty[i]; u_z[i] = u_ctz[i];
                u_layer[i] = u_climb_layer[i];
                u_state[i] = u_field[i] >= 0 ? US_MOVE : US_IDLE;
            } else {
                /* Vertical first, then the small horizontal step at the end. */
                float tv = w_clampf(nt / 0.85f, 0.0f, 1.0f), th2 = w_clampf((nt - 0.7f) / 0.3f, 0.0f, 1.0f);
                u_y[i] = u_cfy[i] + (u_cty[i] - u_cfy[i]) * tv;
                u_x[i] = u_cfx[i] + (u_ctx[i] - u_cfx[i]) * th2;
                u_z[i] = u_cfz[i] + (u_ctz[i] - u_cfz[i]) * th2;
            }
            float np = u_phase[i] + dt * 7.0f;
            u_phase[i] = np;
            continue;
        }
        /* Targets: drop dead ones; acquire nearby enemies when free. */
        if (u_target[i] >= 0 && !unit_alive(u_target[i])) {
            u_target[i] = -1;
            if (u_order[i] == ORD_ATTACK) { u_order[i] = ORD_NONE; unit_release_field(i); u_state[i] = US_IDLE; }
        }
        int busy = (u_order[i] == ORD_MOVE) && (u_state[i] == US_MOVE || u_state[i] == US_WAIT);
        if (u_target[i] < 0 && !busy) {
            float range = u_archer[i] ? ARCHER_RANGE - 2.0f : (u_team[i] == TEAM_ENEMY ? 9.0f : 6.0f);
            int e = unit_find_enemy(i, range, u_archer[i] ? 14.0f : 2.5f);
            if (e >= 0) { u_target[i] = e; u_stuck[i] = 0.0f; if (u_state[i] == US_IDLE) u_state[i] = US_CHASE; }
        }
        int t = u_target[i];
        int shooting = 0;
        if (u_noshot[i] > 0.0f) { float nn = u_noshot[i] - dt; u_noshot[i] = nn; }
        if (t >= 0 && !busy && u_archer[i] && u_noshot[i] <= 0.0f && u_state[i] != US_WAIT && u_state[i] != US_MOVE) {
            /* Archers: shoot from range (also up/down onto walls); melee
             * only at point-blank range. A shot already being drawn is
             * finished even if the target steps out of range. */
            float ax = u_x[t] - u_x[i], az = u_z[t] - u_z[i], ay = u_y[t] - u_y[i];
            float ad = w_sqrt(ax * ax + az * az);
            int pointBlank = ad < 1.6f && w_absf(ay) < 1.2f;
            if (u_state[i] == US_SHOOT && u_swing[i] > 0.0f && u_swing[i] < 0.62f) shooting = 1;
            else if (!pointBlank && ad <= ARCHER_RANGE && w_absf(ay) < 14.0f) shooting = 1;
            if (shooting) {
                if (u_state[i] != US_SHOOT) { u_state[i] = US_SHOOT; u_swing[i] = 0.0f; }
                unit_face(i, ax, az, dt);
            }
        }
        if (t >= 0 && !shooting && !busy && u_state[i] != US_WAIT && u_state[i] != US_MOVE) {
            float dx = u_x[t] - u_x[i], dz = u_z[t] - u_z[i];
            float d = w_sqrt(dx * dx + dz * dz);
            int sameLevel = w_absf(u_y[t] - u_y[i]) < 1.2f;
            if (d < 1.55f && sameLevel) {
                if (u_state[i] != US_ATTACK) { u_state[i] = US_ATTACK; u_swing[i] = 0.0f; }
            } else if (u_state[i] == US_ATTACK && u_swing[i] > 0.0f) {
                /* finish the swing */
            } else {
                u_state[i] = US_CHASE;
            }
            unit_face(i, dx, dz, dt);
            if (u_state[i] == US_CHASE) {
                if (d > 14.0f || !sameLevel) {
                    float ns = u_stuck[i] + dt;
                    u_stuck[i] = ns;
                } else {
                    float ox = u_x[i], oz = u_z[i];
                    unit_walk(i, dx / d * 3.4f * dt, dz / d * 3.4f * dt, -1);
                    float pr = (u_x[i] - ox) * (u_x[i] - ox) + (u_z[i] - oz) * (u_z[i] - oz);
                    if (pr < 1e-6f) { float ns = u_stuck[i] + dt; u_stuck[i] = ns; } else u_stuck[i] = 0.0f;
                }
                if (u_stuck[i] > 1.5f) {
                    /* Can't reach it directly (e.g. it's up on a wall): give up,
                     * unless ordered -- then path to it with a fresh field. */
                    u_target[i] = -1;
                    u_state[i] = US_IDLE;
                    u_stuck[i] = 0.0f;
                }
            }
        }
        if (u_state[i] == US_ATTACK) {
            float prev = u_swing[i];
            float ns = prev + dt / 0.85f;
            u_swing[i] = ns;
            if (prev < 0.5f && ns >= 0.5f && unit_alive(t)) {
                float dx = u_x[t] - u_x[i], dz = u_z[t] - u_z[i];
                if (dx * dx + dz * dz < 2.0f * 2.0f && w_absf(u_y[t] - u_y[i]) < 1.3f) unit_damage(t, 1);
            }
            if (ns >= 1.0f) {
                u_swing[i] = 0.0f;
                if (!unit_alive(t)) { u_state[i] = US_IDLE; u_target[i] = -1; }
            }
        }
        if (u_state[i] == US_SHOOT) {
            float prev = u_swing[i];
            float ns = prev + dt / 1.5f;
            u_swing[i] = ns;
            if (prev == 0.0f && unit_alive(t)) {
                float fr[3], ff[3];
                if (!archer_solve(i, t, fr, ff)) {
                    /* No clear arc from here: fight hand-to-hand for a while. */
                    u_noshot[i] = 3.0f;
                    u_state[i] = US_CHASE;
                    u_swing[i] = 0.0f;
                }
            }
            if (prev < 0.62f && ns >= 0.62f && unit_alive(t)) arrow_fire(i, t);
            if (ns >= 1.0f) {
                u_swing[i] = 0.0f;
                if (!unit_alive(t)) { u_state[i] = US_IDLE; u_target[i] = -1; }
            }
            if (!unit_alive(t) && ns < 0.62f) { u_state[i] = US_IDLE; u_target[i] = -1; u_swing[i] = 0.0f; }
        }
        if (u_state[i] == US_WAIT && u_field[i] >= 0 && g_field_state[u_field[i]] == FS_READY) u_state[i] = US_MOVE;
        if (u_state[i] == US_MOVE) unit_follow_field(i, dt);
    }

    dispatch_repaths();

    /* Separation: keep units from overlapping (same level only). */
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_state[i] == US_CLIMB || !unit_loaded(i)) continue;
        for (j = i + 1; j < g_un; j++) {
            if (!unit_alive(j) || u_state[j] == US_CLIMB) continue;
            float dx = u_x[j] - u_x[i], dz = u_z[j] - u_z[i];
            if (w_absf(dx) > 0.8f || w_absf(dz) > 0.8f || w_absf(u_y[j] - u_y[i]) > 1.0f) continue;
            float d2 = dx * dx + dz * dz;
            if (d2 > 0.64f) continue;
            float d = w_sqrt(d2);
            if (d < 0.01f) { dx = 0.01f * (float)((i % 3) - 1) + 0.005f; dz = 0.007f; d = 0.0122f; }
            float push = (0.8f - d) * 0.5f;
            float px = dx / d * push, pz = dz / d * push;
            unit_walk(i, -px, -pz, -1);
            unit_walk(j, px, pz, -1);
        }
    }

    /* Heights + animation phase. */
    for (i = 0; i < g_un; i++) {
        if (!u_active[i] || u_state[i] == US_CLIMB || !unit_loaded(i)) continue;
        float ty = nav_surface(u_layer[i], u_x[i], u_z[i]);
        if (u_state[i] == US_DEAD && u_dead[i] > 6.0f) { float sink = (u_dead[i] - 6.0f) * 0.4f; float tsy = ty - sink; ty = tsy; }
        float k = w_clampf(dt * 14.0f, 0.0f, 1.0f);
        float ny = u_y[i] + (ty - u_y[i]) * k;
        u_y[i] = ny;
        float np = u_phase[i] + u_moved[i] * 2.6f;
        u_phase[i] = np;
    }
    /* Smoothed velocities (archers lead moving targets with them). */
    if (dt > 0.0001f) {
        for (i = 0; i < g_un; i++) {
            if (!u_active[i]) continue;
            float ivx = (u_x[i] - u_px[i]) / dt, ivz = (u_z[i] - u_pz[i]) / dt;
            if (ivx * ivx + ivz * ivz > 100.0f) { ivx = 0.0f; ivz = 0.0f; }   /* teleports / spawns */
            float k = w_clampf(dt * 6.0f, 0.0f, 1.0f);
            float nvx = u_vx[i] + (ivx - u_vx[i]) * k, nvz = u_vz[i] + (ivz - u_vz[i]) * k;
            u_vx[i] = nvx; u_vz[i] = nvz;
            u_px[i] = u_x[i]; u_pz[i] = u_z[i];
        }
    }
    arrows_update(dt);
    if (g_order_marker_on) {
        float nm = g_order_mt + dt;
        g_order_mt = nm;
        if (nm > 2.5f) g_order_marker_on = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Unit mesh: animated box hierarchy                                   */
/* ------------------------------------------------------------------ */
#define U_MAX_VERTS 120000
static float g_ustage[U_MAX_VERTS * W_VSTRIDE];
static int g_un_verts;
static float g_ucr, g_ucg, g_ucb;

static void u_color(float r, float g, float b) { g_ucr = r; g_ucg = g; g_ucb = b; }
static void u_color3(const float *c) { g_ucr = c[0]; g_ucg = c[1]; g_ucb = c[2]; }

/* 3x4 transforms: m[0..2] X axis, m[3..5] Y axis, m[6..8] Z axis,
 * m[9..11] translation. */
static void m34_mul(float *o, const float *a, const float *b) {
    int c;
    for (c = 0; c < 4; c++) {
        float bx = b[c * 3 + 0], by = b[c * 3 + 1], bz = b[c * 3 + 2];
        int r;
        for (r = 0; r < 3; r++) {
            float v = a[r] * bx + a[3 + r] * by + a[6 + r] * bz;
            if (c == 3) { float vt = v + a[9 + r]; v = vt; }
            o[c * 3 + r] = v;
        }
    }
}

/* o = parent * T(px,py,pz) * Rx(pitch) * Rz(roll) */
static void m34_local(float *o, const float *parent, float px, float py, float pz, float pitch, float roll) {
    float l[12];
    float cp = w_cos(pitch), sp = w_sin(pitch), cr = w_cos(roll), sr = w_sin(roll);
    /* Rx * Rz columns */
    l[0] = cr;       l[1] = sr * cp;  l[2] = sr * sp;
    l[3] = -sr;      l[4] = cr * cp;  l[5] = cr * sp;
    l[6] = 0.0f;     l[7] = -sp;      l[8] = cp;
    l[9] = px;       l[10] = py;      l[11] = pz;
    m34_mul(o, parent, l);
}

/* Unit root: T(x,y,z) * Ry(-yaw) * Rx(fall). Local forward is -z. */
static void m34_root(float *o, int i, float fall) {
    float cy = w_cos(u_yaw[i]), sy = w_sin(u_yaw[i]);
    float r[12], f[12];
    r[0] = cy;   r[1] = 0.0f; r[2] = sy;
    r[3] = 0.0f; r[4] = 1.0f; r[5] = 0.0f;
    r[6] = -sy;  r[7] = 0.0f; r[8] = cy;
    r[9] = u_x[i]; r[10] = u_y[i]; r[11] = u_z[i];
    float cf = w_cos(fall), sf = w_sin(fall);
    f[0] = 1.0f; f[1] = 0.0f; f[2] = 0.0f;
    f[3] = 0.0f; f[4] = cf;   f[5] = sf;
    f[6] = 0.0f; f[7] = -sf;  f[8] = cf;
    f[9] = 0.0f; f[10] = 0.0f; f[11] = 0.0f;
    m34_mul(o, r, f);
}

static void u_vert(const float *p, const float *n) {
    if (g_un_verts >= U_MAX_VERTS) return;
    float *o = g_ustage + g_un_verts * W_VSTRIDE;
    o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
    o[3] = n[0]; o[4] = n[1]; o[5] = n[2];
    o[6] = g_ucr; o[7] = g_ucg; o[8] = g_ucb;
    g_un_verts = g_un_verts + 1;
}

static void u_face_quad(const float *a, const float *b, const float *c, const float *d, const float *n) {
    u_vert(a, n); u_vert(b, n); u_vert(c, n);
    u_vert(a, n); u_vert(c, n); u_vert(d, n);
}

/* Box centered at (cx,cy,cz) with half extents in M's local frame.
 * Normals come straight from M's axes (no per-face sqrt needed). */
static void u_box(const float *M, float cx, float cy, float cz, float hx, float hy, float hz) {
    float p[24], n[3];
    int k;
    for (k = 0; k < 8; k++) {
        float lx = cx + ((k & 1) ? hx : -hx);
        float ly = cy + ((k & 2) ? hy : -hy);
        float lz = cz + ((k & 4) ? hz : -hz);
        p[k * 3 + 0] = M[0] * lx + M[3] * ly + M[6] * lz + M[9];
        p[k * 3 + 1] = M[1] * lx + M[4] * ly + M[7] * lz + M[10];
        p[k * 3 + 2] = M[2] * lx + M[5] * ly + M[8] * lz + M[11];
    }
    /* corners: bit0 x, bit1 y, bit2 z */
    n[0] = M[0]; n[1] = M[1]; n[2] = M[2];
    u_face_quad(p + 3, p + 9, p + 21, p + 15, n);      /* +x */
    n[0] = -M[0]; n[1] = -M[1]; n[2] = -M[2];
    u_face_quad(p + 0, p + 12, p + 18, p + 6, n);      /* -x */
    n[0] = M[3]; n[1] = M[4]; n[2] = M[5];
    u_face_quad(p + 6, p + 18, p + 21, p + 9, n);      /* +y */
    n[0] = -M[3]; n[1] = -M[4]; n[2] = -M[5];
    u_face_quad(p + 0, p + 3, p + 15, p + 12, n);      /* -y */
    n[0] = M[6]; n[1] = M[7]; n[2] = M[8];
    u_face_quad(p + 12, p + 15, p + 21, p + 18, n);    /* +z */
    n[0] = -M[6]; n[1] = -M[7]; n[2] = -M[8];
    u_face_quad(p + 0, p + 6, p + 9, p + 3, n);        /* -z */
}

/* Box of half-width w spanning local points a -> b in M's frame (for
 * bow limbs, strings and arrows, which aren't axis aligned). */
static void u_seg(const float *M, const float *a, const float *b, float w) {
    float d0 = b[0] - a[0], d1 = b[1] - a[1], d2 = b[2] - a[2];
    float len = w_sqrt(d0 * d0 + d1 * d1 + d2 * d2);
    if (len < 1e-5f) return;
    float zx = d0 / len, zy = d1 / len, zz = d2 / len;
    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    if (zy > 0.95f || zy < -0.95f) { ux = 1.0f; uy = 0.0f; }
    /* x = up x z, y = z x x */
    float xx = uy * zz - uz * zy, xy = uz * zx - ux * zz, xz = ux * zy - uy * zx;
    float xl = w_sqrt(xx * xx + xy * xy + xz * xz);
    xx = xx / xl; xy = xy / xl; xz = xz / xl;
    float yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    float L[12], W[12];
    L[0] = xx; L[1] = xy; L[2] = xz;
    L[3] = yx; L[4] = yy; L[5] = yz;
    L[6] = zx; L[7] = zy; L[8] = zz;
    L[9] = (a[0] + b[0]) * 0.5f; L[10] = (a[1] + b[1]) * 0.5f; L[11] = (a[2] + b[2]) * 0.5f;
    m34_mul(W, M, L);
    u_box(W, 0.0f, 0.0f, 0.0f, w, w, len * 0.5f);
}

static void u_set3(float *p, float x, float y, float z) { p[0] = x; p[1] = y; p[2] = z; }

/* Bow in the left hand's frame (the hand is at local y = -0.30 s). Its
 * limbs run along local z (vertical while the bow arm is raised), tips
 * curving back toward the archer (+y); `draw` in [0,1] pulls the
 * string's nocking point back, and an arrow is shown nocked if asked. */
static void u_bow(const float *M, float s, float draw, int nocked, int detail) {
    float h[3], u1[3], u2[3], l1[3], l2[3], nk[3], tip[3];
    float hy = -0.30f * s;
    u_set3(h, 0.0f, hy, 0.0f);
    u_set3(u1, 0.0f, hy + 0.03f, -0.33f);
    u_set3(u2, 0.0f, hy + 0.13f, -0.62f);
    u_set3(l1, 0.0f, hy + 0.03f, 0.33f);
    u_set3(l2, 0.0f, hy + 0.13f, 0.62f);
    u_color(0.42f, 0.27f, 0.13f);
    u_seg(M, h, u1, 0.025f); u_seg(M, u1, u2, 0.02f);
    u_seg(M, h, l1, 0.025f); u_seg(M, l1, l2, 0.02f);
    if (!detail) return;
    u_set3(nk, 0.0f, hy + 0.13f + 0.48f * draw, 0.0f);
    u_color(0.92f, 0.92f, 0.85f);
    u_seg(M, u2, nk, 0.006f); u_seg(M, nk, l2, 0.006f);
    if (nocked) {
        u_set3(tip, 0.0f, nk[1] - 0.80f, 0.0f);
        u_color(0.60f, 0.45f, 0.25f);
        u_seg(M, nk, tip, 0.012f);
        float th[3];
        u_set3(th, 0.0f, tip[1] + 0.07f, 0.0f);
        u_color(0.30f, 0.30f, 0.33f);
        u_seg(M, th, tip, 0.03f);
    }
}

/* Flying / stuck arrows in world space. */
static void arrows_emit(void) {
    float I[12], tip[3], tail[3], head[3], fl[3];
    int k;
    I[0] = 1.0f; I[1] = 0.0f; I[2] = 0.0f; I[3] = 0.0f; I[4] = 1.0f; I[5] = 0.0f;
    I[6] = 0.0f; I[7] = 0.0f; I[8] = 1.0f; I[9] = 0.0f; I[10] = 0.0f; I[11] = 0.0f;
    for (k = 0; k < MAXP; k++) {
        if (!p_active[k]) continue;
        float dx = p_x[k] - g_cam[0], dz = p_z[k] - g_cam[2];
        if (dx * dx + dz * dz > 140.0f * 140.0f) continue;
        u_set3(tip, p_x[k], p_y[k], p_z[k]);
        u_set3(tail, p_x[k] - a_dx[k] * 0.78f, p_y[k] - a_dy[k] * 0.78f, p_z[k] - a_dz[k] * 0.78f);
        u_set3(head, p_x[k] - a_dx[k] * 0.09f, p_y[k] - a_dy[k] * 0.09f, p_z[k] - a_dz[k] * 0.09f);
        u_set3(fl, tail[0] + a_dx[k] * 0.16f, tail[1] + a_dy[k] * 0.16f, tail[2] + a_dz[k] * 0.16f);
        u_color(0.60f, 0.45f, 0.25f);
        u_seg(I, tail, tip, 0.014f);
        u_color(0.30f, 0.30f, 0.33f);
        u_seg(I, head, tip, 0.035f);
        if (a_team[k] == TEAM_PLAYER) u_color(0.35f, 0.55f, 1.0f); else u_color(1.0f, 0.35f, 0.3f);
        u_seg(I, tail, fl, 0.04f);
    }
}

/* Flat ring on the ground (selection / target / order markers). */
static void u_ring(float x, float y, float z, float r, float w) {
    float n[3], a[3], b[3], c[3], d[3];
    int k;
    n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
    for (k = 0; k < 16; k++) {
        float a0 = (float)k * (2.0f * W_PI / 16.0f), a1 = (float)(k + 1) * (2.0f * W_PI / 16.0f);
        float c0 = w_cos(a0), s0 = w_sin(a0), c1 = w_cos(a1), s1 = w_sin(a1);
        float ri = r - w;
        a[0] = x + c0 * ri; a[1] = y; a[2] = z + s0 * ri;
        b[0] = x + c0 * r;  b[1] = y; b[2] = z + s0 * r;
        c[0] = x + c1 * r;  c[1] = y; c[2] = z + s1 * r;
        d[0] = x + c1 * ri; d[1] = y; d[2] = z + s1 * ri;
        u_face_quad(a, b, c, d, n);
    }
}

/* Camera-facing horizontal bar (health). */
static void u_bar(float x, float y, float z, float w, float h) {
    float rx = w_cos(g_yaw), rz = w_sin(g_yaw);
    float n[3], a[3], b[3], c[3], d[3];
    n[0] = -rz; n[1] = 0.6f; n[2] = rx;
    a[0] = x - rx * w; a[1] = y;     a[2] = z - rz * w;
    b[0] = x + rx * w; b[1] = y;     b[2] = z + rz * w;
    c[0] = x + rx * w; c[1] = y + h; c[2] = z + rz * w;
    d[0] = x - rx * w; d[1] = y + h; d[2] = z - rz * w;
    u_face_quad(a, b, c, d, n);
}

/* One humanoid, procedurally animated from its state. */
static void unit_emit(int i, int lod) {
    float s = u_scale[i], g = u_girth[i];
    float t = g_time + u_seed[i];
    float fall = 0.0f;
    if (u_state[i] == US_DEAD) fall = -1.5f * w_clampf(u_dead[i] / 0.55f, 0.0f, 1.0f);

    /* --- pose parameters --- */
    float legL = 0.0f, legR = 0.0f, kneeL = 0.0f, kneeR = 0.0f;
    float armL = 0.08f, armR = 0.08f, elbL = -0.25f, elbR = -0.25f, armRollL = 0.08f, armRollR = -0.08f;
    float bob = 0.0f, lean = 0.0f, twist = 0.0f, headPitch = 0.0f;
    int st = u_state[i];
    int walking = (st == US_MOVE || st == US_CHASE) && u_moved[i] > 0.0005f;
    if (walking) {
        float ph = u_phase[i];
        float sp = w_sin(ph), cp = w_cos(ph);
        legL = sp * 0.62f; legR = -sp * 0.62f;
        kneeL = -w_clampf(w_sin(ph + 1.6f), 0.0f, 1.0f) * 0.9f;
        kneeR = -w_clampf(-w_sin(ph + 1.6f), 0.0f, 1.0f) * 0.9f;
        armL = -sp * 0.5f; armR = sp * 0.5f;
        elbL = -0.35f; elbR = -0.35f;
        bob = w_absf(cp) * 0.05f * s;
        lean = 0.10f;
    } else if (st == US_ATTACK) {
        float tt = u_swing[i];
        float sh, el;
        if (tt < 0.4f) { float k = tt / 0.4f; sh = 0.3f + 2.6f * k; el = -0.2f - 0.9f * k; twist = -0.3f * k; }
        else if (tt < 0.6f) { float k = (tt - 0.4f) / 0.2f; sh = 2.9f - 2.8f * k; el = -1.1f + 1.1f * k; twist = -0.3f + 0.7f * k; }
        else { float k = (tt - 0.6f) / 0.4f; sh = 0.1f + 0.2f * k; el = -0.25f * k; twist = 0.4f - 0.4f * k; }
        armR = sh; elbR = el;
        armL = 0.7f; elbL = -0.9f; armRollL = 0.3f;
        legL = 0.35f; legR = -0.25f; kneeR = -0.2f;
        lean = 0.12f;
    } else if (st == US_SHOOT) {
        /* Raise the bow (0-0.18), draw (0.2-0.62), release at 0.62,
         * follow through, lower (0.8-1). The bow arm points along the
         * solved launch elevation. */
        float tt = u_swing[i];
        float raise = w_clampf(tt / 0.18f, 0.0f, 1.0f);
        float lower = w_clampf((tt - 0.8f) / 0.2f, 0.0f, 1.0f);
        float up = raise * (1.0f - lower);
        float drawn = (tt >= 0.2f && tt < 0.62f) ? (tt - 0.2f) / 0.42f : 0.0f;
        float aim = w_clampf(u_aim[i], -0.6f, 1.0f);
        armL = 0.1f + (1.47f + aim) * up;
        elbL = -0.05f; armRollL = 0.05f;
        armR = 0.1f + (1.35f + aim) * up;
        armRollR = -0.55f * up;
        elbR = -(0.35f + 1.85f * drawn) * up;
        if (tt >= 0.62f && tt < 0.75f) { armRollR = -1.0f * up; elbR = -0.6f * up; }   /* follow-through */
        legL = 0.28f; legR = -0.22f; kneeR = -0.15f;
        headPitch = -aim * 0.5f;
        lean = 0.04f;
    } else if (st == US_CLIMB) {
        float ph = u_phase[i];
        float sp = w_sin(ph);
        armL = 2.5f + sp * 0.35f; armR = 2.5f - sp * 0.35f;
        elbL = -0.5f; elbR = -0.5f;
        legL = 0.7f + sp * 0.5f; legR = 0.7f - sp * 0.5f;
        kneeL = -1.0f; kneeR = -1.0f;
        headPitch = -0.3f;
    } else if (st != US_DEAD) {
        float br = w_sin(t * 1.8f);
        bob = br * 0.012f * s;
        armL = 0.08f + br * 0.04f; armR = 0.08f - br * 0.04f;
        headPitch = w_sin(t * 0.37f) * 0.12f;
    }

    float root[12], hip[12], torso[12], head[12], a1[12], a2[12], l1[12], l2[12];
    m34_root(root, i, fall);
    float hipY = 0.93f * s + bob;
    m34_local(hip, root, 0.0f, hipY, 0.0f, 0.0f, 0.0f);

    float tunic[3];
    tunic[0] = u_tunic[i * 3 + 0]; tunic[1] = u_tunic[i * 3 + 1]; tunic[2] = u_tunic[i * 3 + 2];
    if (u_hit[i] > 0.0f) { tunic[0] = 1.0f; tunic[1] = 0.95f; tunic[2] = 0.9f; }

    /* Legs */
    int side;
    for (side = -1; side <= 1; side = side + 2) {
        float lp = side < 0 ? legL : legR, kp = side < 0 ? kneeL : kneeR;
        m34_local(l1, hip, 0.11f * (float)side * g, -0.02f * s, 0.0f, lp, 0.0f);
        u_color(0.22f, 0.20f, 0.24f);
        u_box(l1, 0.0f, -0.22f * s, 0.0f, 0.075f * g, 0.23f * s, 0.085f * g);
        if (lod) {
            m34_local(l2, l1, 0.0f, -0.45f * s, 0.0f, kp, 0.0f);
            u_box(l2, 0.0f, -0.21f * s, 0.0f, 0.068f * g, 0.22f * s, 0.075f * g);
            u_color(0.30f, 0.20f, 0.12f);
            u_box(l2, 0.0f, -0.44f * s, -0.05f * s, 0.072f * g, 0.04f * s, 0.12f * s);
        }
    }

    /* Torso */
    m34_local(torso, hip, 0.0f, 0.0f, 0.0f, lean, 0.0f);
    if (twist != 0.0f) {
        float tw[12], rot[12];
        float ct = w_cos(twist), stw = w_sin(twist);
        rot[0] = ct; rot[1] = 0.0f; rot[2] = -stw;
        rot[3] = 0.0f; rot[4] = 1.0f; rot[5] = 0.0f;
        rot[6] = stw; rot[7] = 0.0f; rot[8] = ct;
        rot[9] = 0.0f; rot[10] = 0.0f; rot[11] = 0.0f;
        m34_mul(tw, torso, rot);
        int q;
        for (q = 0; q < 12; q++) torso[q] = tw[q];
    }
    u_color3(tunic);
    u_box(torso, 0.0f, 0.29f * s, 0.0f, 0.21f * g, 0.30f * s, 0.12f * g);
    if (lod) {
        u_color(0.32f, 0.22f, 0.12f);
        u_box(torso, 0.0f, 0.03f * s, 0.0f, 0.215f * g, 0.035f * s, 0.125f * g);
    }

    /* Head */
    m34_local(head, torso, 0.0f, 0.62f * s, 0.0f, headPitch, 0.0f);
    u_color3(u_face + i * 3);
    u_box(head, 0.0f, 0.14f * s, 0.0f, 0.125f * s, 0.14f * s, 0.125f * s);
    if (lod) {
        u_color(0.06f, 0.06f, 0.08f);
        u_box(head, -0.05f * s, 0.17f * s, -0.128f * s, 0.022f * s, 0.022f * s, 0.006f);
        u_box(head, 0.05f * s, 0.17f * s, -0.128f * s, 0.022f * s, 0.022f * s, 0.006f);
    }
    if (u_helmet[i]) {
        u_color(0.62f, 0.64f, 0.68f);
        u_box(head, 0.0f, 0.27f * s, 0.0f, 0.14f * s, 0.06f * s, 0.14f * s);
        if (lod) u_box(head, 0.0f, 0.13f * s, -0.135f * s, 0.018f * s, 0.10f * s, 0.012f * s);
    } else {
        u_color3(u_hair + i * 3);
        u_box(head, 0.0f, 0.27f * s, 0.015f * s, 0.135f * s, 0.045f * s, 0.135f * s);
        if (lod) u_box(head, 0.0f, 0.14f * s, 0.12f * s, 0.13f * s, 0.12f * s, 0.025f * s);
    }

    /* Quiver on an archer's back. */
    if (u_archer[i] && lod) {
        u_color(0.40f, 0.26f, 0.14f);
        u_box(torso, 0.09f * g, 0.33f * s, 0.15f * g, 0.06f, 0.21f * s, 0.06f);
        u_color(0.95f, 0.95f, 0.92f);
        u_box(torso, 0.07f * g, 0.58f * s, 0.15f * g, 0.012f, 0.05f, 0.025f);
        u_box(torso, 0.11f * g, 0.59f * s, 0.15f * g, 0.012f, 0.05f, 0.025f);
    }

    /* Arms (right holds the sword, left the shield; archers: bow in the
     * left hand) */
    for (side = -1; side <= 1; side = side + 2) {
        float ap = side < 0 ? armL : armR, ep = side < 0 ? elbL : elbR, ar = side < 0 ? armRollL : armRollR;
        m34_local(a1, torso, 0.27f * (float)side * g, 0.54f * s, 0.0f, ap, ar);
        u_color3(tunic);
        u_box(a1, 0.0f, -0.15f * s, 0.0f, 0.065f * g, 0.16f * s, 0.065f * g);
        m34_local(a2, a1, 0.0f, -0.30f * s, 0.0f, -ep, 0.0f);
        u_color3(u_face + i * 3);
        u_box(a2, 0.0f, -0.14f * s, 0.0f, 0.058f * g, 0.15f * s, 0.058f * g);
        if (u_archer[i]) {
            if (side < 0) {
                float drawn = 0.0f;
                int nocked = 0;
                if (st == US_SHOOT) {
                    float tt = u_swing[i];
                    drawn = (tt >= 0.2f && tt < 0.62f) ? (tt - 0.2f) / 0.42f : 0.0f;
                    nocked = tt >= 0.1f && tt < 0.62f;
                }
                u_bow(a2, s, drawn, nocked, lod);
            }
        } else if (side > 0) {
            /* Sword: grip in the fist, blade pointing forward (-z). */
            u_color(0.35f, 0.24f, 0.12f);
            u_box(a2, 0.0f, -0.31f * s, 0.05f, 0.03f, 0.03f, 0.09f);
            u_color(0.55f, 0.45f, 0.20f);
            u_box(a2, 0.0f, -0.31f * s, -0.05f, 0.12f, 0.025f, 0.025f);
            u_color(0.82f, 0.84f, 0.88f);
            u_box(a2, 0.0f, -0.31f * s, -0.50f, 0.022f, 0.035f, 0.43f);
        } else if (lod) {
            float sc[3];
            sc[0] = u_tunic[i * 3 + 0] * 0.8f; sc[1] = u_tunic[i * 3 + 1] * 0.8f; sc[2] = u_tunic[i * 3 + 2] * 0.8f;
            u_color3(sc);
            u_box(a2, -0.09f * g, -0.16f * s, 0.0f, 0.025f, 0.21f * s, 0.17f * s);
            u_color(0.75f, 0.70f, 0.45f);
            u_box(a2, -0.115f * g, -0.16f * s, 0.0f, 0.006f, 0.05f, 0.05f);
        }
    }

    /* Selection ring + health bar */
    if (st != US_DEAD) {
        if (u_selected[i]) { u_color(0.25f, 1.0f, 0.35f); u_ring(u_x[i], u_y[i] + 0.06f, u_z[i], 0.55f, 0.08f); }
        if (u_selected[i] || u_hp[i] < u_maxhp[i]) {
            float frac = (float)u_hp[i] / (float)u_maxhp[i];
            float top = u_y[i] + 2.05f * s;
            u_color(0.12f, 0.12f, 0.12f);
            u_bar(u_x[i], top, u_z[i], 0.42f, 0.10f);
            if (u_team[i] == TEAM_PLAYER) u_color(0.2f, 0.95f, 0.3f); else u_color(1.0f, 0.25f, 0.2f);
            float rx = w_cos(g_yaw), rz = w_sin(g_yaw);
            float w2 = 0.40f * frac;
            float cxp = u_x[i] - rx * (0.40f - w2), czp = u_z[i] - rz * (0.40f - w2);
            u_bar(cxp, top + 0.015f, czp, w2, 0.07f);
        }
    }
}

/* Builds this frame's unit geometry; returns the vertex count. */
static int units_build_mesh(void) {
    int i;
    g_un_verts = 0;
    float fx = w_sin(g_yaw), fz = -w_cos(g_yaw);
    for (i = 0; i < g_un; i++) {
        if (!u_active[i] || !unit_loaded(i)) continue;
        float dx = u_x[i] - g_cam[0], dz = u_z[i] - g_cam[2];
        float d2 = dx * dx + dz * dz;
        if (d2 > 150.0f * 150.0f) continue;
        if (dx * fx + dz * fz < -6.0f) continue;
        unit_emit(i, d2 < 70.0f * 70.0f);
        /* Targets of selected units get a red ring. */
    }
    for (i = 0; i < g_un; i++) {
        if (!u_selected[i] || !unit_alive(i)) continue;
        int t = u_target[i];
        if (unit_alive(t)) { u_color(1.0f, 0.2f, 0.15f); u_ring(u_x[t], u_y[t] + 0.07f, u_z[t], 0.7f, 0.1f); }
    }
    arrows_emit();
    if (g_order_marker_on) {
        float k = g_order_mt / 2.5f;
        u_color(1.0f, 0.9f, 0.25f);
        u_ring(g_order_mx, g_order_my + 0.08f, g_order_mz, 0.4f + 1.2f * k, 0.12f);
        u_ring(g_order_mx, g_order_my + 0.08f, g_order_mz, 0.25f + 0.5f * k, 0.1f);
    }
    return g_un_verts;
}

/* ------------------------------------------------------------------ */
/* Picking                                                             */
/* ------------------------------------------------------------------ */
static float g_vp[16];      /* last frame's view-projection (column-major) */

/* World point -> screen pixels; returns 0 if behind the camera. */
static int project_point(float x, float y, float z, float *sx, float *sy) {
    float cx = g_vp[0] * x + g_vp[4] * y + g_vp[8] * z + g_vp[12];
    float cy = g_vp[1] * x + g_vp[5] * y + g_vp[9] * z + g_vp[13];
    float cw = g_vp[3] * x + g_vp[7] * y + g_vp[11] * z + g_vp[15];
    if (cw < 0.1f) return 0;
    *sx = (cx / cw * 0.5f + 0.5f) * g_view_w;
    *sy = (cy / cw * 0.5f + 0.5f) * g_view_h;
    return 1;
}

/* Unit under a screen point (nearest to the camera within reach). */
static int pick_unit(float sx, float sy) {
    int i, best = -1;
    float bestScore = 1e30f;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || !unit_loaded(i)) continue;
        float px, py;
        if (!project_point(u_x[i], u_y[i] + 1.0f, u_z[i], &px, &py)) continue;
        float dx = px - sx, dy = py - sy;
        float d2 = dx * dx + dy * dy;
        if (d2 > 60.0f * 60.0f) continue;
        if (d2 < bestScore) { bestScore = d2; best = i; }
    }
    return best;
}

/* Ray from the camera through a screen point against the nav surfaces
 * (wall-walk tops and the ground). Fills hit position + layer. */
/* Unit direction (out[0..2]) of the camera ray through screen point
 * (sx,sy), for the current g_cam / g_yaw / g_pitch (fov 1.05 rad). */
static void screen_ray(float sx, float sy, float *out) {
    float cp = w_cos(g_pitch), spt = w_sin(g_pitch), cy = w_cos(g_yaw), syw = w_sin(g_yaw);
    float fx = cp * syw, fy = spt, fz = -cp * cy;
    float rx = cy, rz = syw;
    float ux = -spt * syw, uy = cp, uz = spt * cy;
    float half = 1.05f * 0.5f;
    float tanH = w_sin(half) / w_cos(half);
    float aspect = g_view_w / g_view_h;
    float nx = 2.0f * sx / g_view_w - 1.0f, ny = 2.0f * sy / g_view_h - 1.0f;
    float dx = fx + rx * (nx * tanH * aspect) - ux * (ny * tanH);
    float dy = fy - uy * (ny * tanH);
    float dz = fz + rz * (nx * tanH * aspect) - uz * (ny * tanH);
    float len = w_sqrt(dx * dx + dy * dy + dz * dz);
    out[0] = dx / len; out[1] = dy / len; out[2] = dz / len;
}

/* Where the ray through (sx,sy) meets the horizontal plane y = py;
 * 0 if it points away from it. */
static int screen_to_plane(float sx, float sy, float py, float *hx, float *hz) {
    float d[3];
    screen_ray(sx, sy, d);
    if (d[1] > -0.02f && g_cam[1] > py) return 0;
    float t = (py - g_cam[1]) / d[1];
    if (t < 0.0f) return 0;
    *hx = g_cam[0] + d[0] * t;
    *hz = g_cam[2] + d[2] * t;
    return 1;
}

static int pick_ground(float sx, float sy, float *hx, float *hy, float *hz, int *hl) {
    float dir[3];
    screen_ray(sx, sy, dir);
    float dx = dir[0], dy = dir[1], dz = dir[2];
    float t;
    for (t = 0.5f; t < 420.0f; t = t + 0.35f) {
        float px = g_cam[0] + dx * t, py = g_cam[1] + dy * t, pz = g_cam[2] + dz * t;
        int wx = w_ifloor(px), wz = w_ifloor(pz);
        float hw;
        int fw = nav_node(1, wx, wz, &hw);
        if ((fw & NAV_WALK) && py <= hw + 0.15f && py >= hw - 2.5f) {
            *hx = px; *hy = hw; *hz = pz; *hl = 1;
            return 1;
        }
        int ccx = w_ifloor(px / W_CHUNK_SIZE), ccz = w_ifloor(pz / W_CHUNK_SIZE);
        if (nav_slot_for(ccx, ccz) < 0) continue;
        float hg = nav_terrain_height(px, pz);
        float h0;
        int f0 = nav_node(0, wx, wz, &h0);
        if ((f0 & NAV_WALK) && !(f0 & NAV_TERRAIN)) hg = h0;   /* ramps, bridges */
        if (hg < 0.0f) hg = 0.0f;                                /* water surface  */
        if (py <= hg) {
            *hx = px; *hy = hg; *hz = pz; *hl = 0;
            return 1;
        }
    }
    return 0;
}

#endif /* CASTLES_UNITS_H */
