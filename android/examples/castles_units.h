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
static int g_nav_cache_slot, g_nav_cache_cx, g_nav_cache_cz;   /* slot set to -1 in units_init */

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
#define NFIELD      20     /* orders + the town-centre, gathering and fishing fields (player + on-screen AI castles) */
#define FIELD_INF   1.0e9f

#define FS_FREE      0
#define FS_UPLOAD    1   /* built on CPU, waiting for GPU upload       */
#define FS_COMPUTING 2   /* GPU relaxation running                     */
#define FS_READY     3   /* distances copied back, units can follow    */

/* CPU copies of every field (heights, flags, distances): 4 MB with 20
 * fields, so heap-allocated in units_init -- squash puts zero-initialised
 * static arrays in .data, which would bloat the APK by that much. */
void *malloc(unsigned long sz);
static float *g_fh;
static uint32_t *g_ff;
static float *g_fd;
static int g_field_state[NFIELD];
static int g_field_refs[NFIELD];
static int g_field_ox[NFIELD], g_field_oz[NFIELD];
static float g_field_gx[NFIELD], g_field_gy[NFIELD], g_field_gz[NFIELD];
static float g_field_radius[NFIELD];
static int g_field_partial[NFIELD];      /* goal lies outside the window: a waypoint leg */
static int g_field_age[NFIELD];
static int g_field_water[NFIELD];        /* a boats' field: open water is the walkable surface */
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
static int g_field_mode_water;   /* field_create builds a water field (set around the call) */
static int g_field_mode_ground;  /* ... or a ground-only one (siege engines: no wall-walks, no ladders) */
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
    g_field_water[f] = g_field_mode_water;
    float radius = 0.7f + 0.55f * w_sqrt((float)groupSize);
    g_field_radius[f] = radius;
    int base = f * NAVW_N, WW = NAVW * NAVW;
    int l, x, z, seeded = 0;
    for (l = 0; l < NAV_L; l++) {
        for (z = 0; z < NAVW; z++) {
            for (x = 0; x < NAVW; x++) {
                float h;
                int fl = nav_node(l, ox + x, oz + z, &h);
                if (g_field_mode_water) {       /* boats: water cells only, all at sea level */
                    fl = (l == 0 && (fl & NAV_WATER)) ? NAV_WALK : 0;
                    h = W_SEA_LEVEL;
                }
                if (g_field_mode_ground) fl = l == 0 ? (fl & ~(NAV_LADDER | 12)) : 0;
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
#define MAXU        420
#define TEAM_PLAYER 0
#define TEAM_ENEMY  1
#define TEAM_WILD   2   /* untamed dragons: everyone's enemy */

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
/* Unit classes. Swordsmen and archers as before; macemen are nimble --
 * faster on foot and much quicker to strike, but fragile; peasants are
 * unarmed townsfolk who live in the houses of the home castle. */
#define UC_SWORD   0
#define UC_BOW     1
#define UC_MACE    2
#define UC_PEASANT 3
#define UC_BOAT    4   /* fishing boat: sails open water, fishes, never fights */
#define UC_CATAPULT 5  /* siege engines (castles_siege.h) */
#define UC_RAM      6
#define UC_TREB     7
#define UC_DRAGON   8  /* flies; wild or tamed by peasants (castles_dragons.h) */
#define UC_RANDOM  (-1)
static int u_class[MAXU];
static float u_speed[MAXU];                      /* walk speed (units/s)        */
static float u_atk[MAXU];                        /* melee cycle (s)             */
/* peasant routine: home -> town centre -> back home along its trail */
#define PS_HOME    0
#define PS_TO_TC   1
#define PS_AT_TC   2
#define PS_RETURN  3
#define PS_MANUAL  4   /* under the player's orders (moves like a soldier, never fights) */
#define PS_G_GO    5   /* gathering: walking to the job's resource site   */
#define PS_G_NEAR  6   /*            last steps straight to its tree/rock */
#define PS_G_WORK  7   /*            chopping / quarrying                 */
#define PS_G_DROP  8   /*            carrying the load to the town centre */
#define PS_B_GO    9   /* building: walking to the construction site      */
#define PS_B_WORK 10   /*           hammering away at it                  */
#define PS_T_GO   11   /* taming a dragon: walking to its roost           */
#define PS_T_WORK 12   /*                  calming it                     */
#define TRAIL_MAX  96
static int u_pstate[MAXU], u_trail_n[MAXU];
static float u_ptimer[MAXU], u_home_x[MAXU], u_home_z[MAXU];
static float u_trail_x[MAXU * TRAIL_MAX], u_trail_z[MAXU * TRAIL_MAX];
static float u_trail_d[MAXU];
static int u_job[MAXU], u_carry[MAXU], u_carry_t[MAXU];   /* gather job, load, load type */
static int u_bsite[MAXU];                                  /* construction site being built */
static int u_own[MAXU];      /* faction (0 = the player, 1.. = AI castles, -1 = unowned guards) */
static int u_role[MAXU];     /* AI work role (R_*), see castles_ai.h */
static int u_amove[MAXU];    /* attack-move: fight whatever it meets on the way to its goal */
static float u_dmgb[MAXU];   /* damage bonus from advancements */
static int u_ld[MAXU];       /* on loaded land this frame (set at the top of units_update) */
static int u_st[MAXU];       /* siege: structure target (> 0 castle piece + 1, < 0 -(building + 1)) */
static int u_sstate[MAXU];   /* trebuchet: packed / setting up / set up / packing */
static float u_st_t[MAXU];   /* trebuchet set-up / pack-up progress */
static int u_spend[MAXU];    /* trebuchet: a move waits for packing up */
/* dragons (castles_dragons.h) */
static int u_dstate[MAXU];
static float u_dalt[MAXU], u_dhx[MAXU], u_dhz[MAXU], u_dt[MAXU], u_dbreath[MAXU], u_dcool[MAXU];
static float u_dtame[MAXU], u_dtame_t[MAXU], u_dbank[MAXU], u_dvel[MAXU], u_deat[MAXU], u_dtick[MAXU], u_dhunt[MAXU];
#define R_NONE      0
#define R_WOOD      1
#define R_STONE     2
#define R_BUILD     3
#define R_FISH      4
#define R_ARMY      5   /* trained soldier waiting at home for the next wave */
#define R_GARRISON  6   /* castle guard: stays home                          */
#define R_WAVE      7   /* marching on / fighting the player                */
static int faction_at_site(int tx, int tz);
static void ai_unit_trained(int f, int u);
static uint32_t u_res_id[MAXU];
static float u_res_x[MAXU], u_res_z[MAXU], u_res_r[MAXU], u_work_t[MAXU];
static float u_aim[MAXU];                        /* launch elevation (rad)      */
static float u_noshot[MAXU];                     /* >0: no clear shot, melee    */
static float u_scale[MAXU], u_girth[MAXU];

/* Castles already populated with guards (keep positions). */
#define MAX_POP 64
static int g_npop;
static float g_pop_x[MAX_POP], g_pop_z[MAX_POP];
static int g_home_set, g_home_spawned;
static int g_home_cx, g_home_cz;

static int g_tc_field;                   /* persistent flow field to the TC door (-1 none; set in units_init: squash drops non-zero global initializers) */
static int g_sel_building;               /* selected home building, or -1 (set in units_init) */

/* ---- factions ---- */
/* Faction 0 is the player; 1.. are AI-run enemy castles (castles_ai.h).
 * Each has its own stockpile, advancements, buildings (g_hb_own) and
 * units (u_own). */
#define F_MAX 4
static int f_on[F_MAX];
static int f_wood[F_MAX], f_stone[F_MAX], f_food[F_MAX];
static float f_food_acc[F_MAX], f_wood_acc[F_MAX], f_stone_acc[F_MAX];
static int f_tech[F_MAX];                /* researched advancements (1 << T_*) */
static int f_tc[F_MAX];                  /* town-centre building index */
static int f_houses[F_MAX];
static WSite f_site[F_MAX];              /* AI castles' sites (castles_ai.h) */

/* ---- advancements (T_*, costs, times: castles_planner.h) ---- */
#include "android/examples/castles_planner.h"

static int has_tech(int own, int t) { return own >= 0 && own < F_MAX && (f_tech[own] & (1 << t)) != 0; }


/* Called at the start of every render session (the app may be left and
 * resumed, the screen rotated): per-session bits every time, the game's
 * starting state only the first time (g_units_ready; a restart clears it). */
static int g_units_ready;
static void units_init(void) {
    g_ddx[D_N] = 0;  g_ddz[D_N] = -1;
    g_ddx[D_E] = 1;  g_ddz[D_E] = 0;
    g_ddx[D_S] = 0;  g_ddz[D_S] = 1;
    g_ddx[D_W] = -1; g_ddz[D_W] = 0;
    g_nav_cache_slot = -1;
    if (g_units_ready) return;
    g_units_ready = 1;
    g_tc_field = -1;
    if (!g_fh) {
        g_fh = (float *)malloc((unsigned long)NFIELD * (unsigned long)NAVW_N * 4ul);
        g_ff = (uint32_t *)malloc((unsigned long)NFIELD * (unsigned long)NAVW_N * 4ul);
        g_fd = (float *)malloc((unsigned long)NFIELD * (unsigned long)NAVW_N * 4ul);
    }
    g_sel_building = -1;
    int f;
    for (f = 0; f < F_MAX; f++) { f_on[f] = 0; f_tc[f] = -1; f_wood[f] = 150; f_stone[f] = 100; f_food[f] = 120; }
    f_on[0] = 1; f_tc[0] = 0;
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
static int unit_spawn(int team, float x, float z, int layer, uint32_t seed, int cls, int own) {
    int i, idx = -1;
    for (i = 0; i < MAXU; i++) if (!u_active[i]) { idx = i; break; }
    if (idx < 0) return -1;
    if (idx >= g_un) g_un = idx + 1;
    uint32_t st = w_hash(seed * 2654435761u + 12345u) | 1u;
    u_active[idx] = 1;
    u_team[idx] = team;
    u_state[idx] = US_IDLE;
    u_layer[idx] = layer;
    if (cls == UC_RANDOM) {
        float r = u_rand01(&st);
        cls = r < 0.4f ? UC_SWORD : (r < 0.72f ? UC_BOW : UC_MACE);
    }
    u_class[idx] = cls;
    if (cls == UC_SWORD)     { u_maxhp[idx] = 6; u_speed[idx] = 3.6f; u_atk[idx] = 0.85f; }
    else if (cls == UC_BOW)  { u_maxhp[idx] = 5; u_speed[idx] = 3.5f; u_atk[idx] = 0.85f; }
    else if (cls == UC_MACE) { u_maxhp[idx] = 4; u_speed[idx] = 4.8f; u_atk[idx] = 0.50f; }
    else if (cls == UC_BOAT) { u_maxhp[idx] = 6; u_speed[idx] = 5.2f; u_atk[idx] = 1.0f; }
    else if (cls == UC_CATAPULT) { u_maxhp[idx] = 9; u_speed[idx] = 2.3f; u_atk[idx] = 4.5f; }
    else if (cls == UC_RAM) { u_maxhp[idx] = 16; u_speed[idx] = 2.5f; u_atk[idx] = 2.2f; }
    else if (cls == UC_TREB) { u_maxhp[idx] = 10; u_speed[idx] = 1.7f; u_atk[idx] = 9.0f; }
    else if (cls == UC_DRAGON) { u_maxhp[idx] = 45; u_speed[idx] = 9.0f; u_atk[idx] = 1.0f; }
    else                     { u_maxhp[idx] = 3; u_speed[idx] = 2.6f; u_atk[idx] = 0.9f; }
    if (team != TEAM_PLAYER && own <= 0 && u_maxhp[idx] > 3) u_maxhp[idx] = u_maxhp[idx] - 1;
    u_own[idx] = own; u_role[idx] = 0; u_amove[idx] = 0; u_dmgb[idx] = 0.0f;
    u_st[idx] = 0; u_sstate[idx] = 0; u_st_t[idx] = 0.0f; u_spend[idx] = 0;
    if (cls == UC_SWORD || cls == UC_BOW || cls == UC_MACE) {
        if (has_tech(own, T_ARMOR)) u_maxhp[idx] = u_maxhp[idx] + 2;
        if (has_tech(own, T_DRILL)) { float ns2 = u_speed[idx] * 1.1f; u_speed[idx] = ns2; }
        if (cls == UC_BOW ? has_tech(own, T_FLETCHING) : has_tech(own, T_FORGING)) u_dmgb[idx] = 1.0f;
    }
    u_hp[idx] = u_maxhp[idx];
    u_pstate[idx] = PS_HOME; u_ptimer[idx] = 0.0f; u_trail_n[idx] = 0; u_trail_d[idx] = 0.0f;
    u_home_x[idx] = x; u_home_z[idx] = z;
    u_job[idx] = -1; u_carry[idx] = 0; u_carry_t[idx] = 0; u_work_t[idx] = 0.0f; u_bsite[idx] = -1;
    u_x[idx] = x; u_z[idx] = z;
    u_px[idx] = x; u_pz[idx] = z; u_vx[idx] = 0.0f; u_vz[idx] = 0.0f;
    u_y[idx] = cls == UC_BOAT ? W_SEA_LEVEL : nav_surface(layer, x, z);
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
    u_archer[idx] = cls == UC_BOW;
    u_noshot[idx] = 0.0f;
    u_aim[idx] = 0.2f;
    float hairHue = u_rand01(&st);
    u_hsv(c, hairHue, 0.35f + 0.5f * u_rand01(&st), 0.15f + 0.6f * u_rand01(&st));
    u_hair[idx * 3 + 0] = c[0]; u_hair[idx * 3 + 1] = c[1]; u_hair[idx * 3 + 2] = c[2];
    float tj = 0.85f + 0.3f * u_rand01(&st);
    if (cls == UC_PEASANT) {
        /* earthy homespun, not team colours (a team sash is drawn) */
        float pr = u_rand01(&st);
        if (pr < 0.33f) { u_tunic[idx * 3 + 0] = 0.52f * tj; u_tunic[idx * 3 + 1] = 0.38f * tj; u_tunic[idx * 3 + 2] = 0.22f * tj; }
        else if (pr < 0.66f) { u_tunic[idx * 3 + 0] = 0.40f * tj; u_tunic[idx * 3 + 1] = 0.46f * tj; u_tunic[idx * 3 + 2] = 0.26f * tj; }
        else { u_tunic[idx * 3 + 0] = 0.70f * tj; u_tunic[idx * 3 + 1] = 0.62f * tj; u_tunic[idx * 3 + 2] = 0.42f * tj; }
    } else if (team == TEAM_PLAYER) { u_tunic[idx * 3 + 0] = 0.16f * tj; u_tunic[idx * 3 + 1] = 0.36f * tj; u_tunic[idx * 3 + 2] = 0.86f * tj; }
    else { u_tunic[idx * 3 + 0] = 0.80f * tj; u_tunic[idx * 3 + 1] = 0.16f * tj; u_tunic[idx * 3 + 2] = 0.14f * tj; }
    if (cls == UC_MACE) {   /* macemen: darker leather over the team colour */
        u_tunic[idx * 3 + 0] = u_tunic[idx * 3 + 0] * 0.75f + 0.08f;
        u_tunic[idx * 3 + 1] = u_tunic[idx * 3 + 1] * 0.75f + 0.05f;
        u_tunic[idx * 3 + 2] = u_tunic[idx * 3 + 2] * 0.75f + 0.02f;
    }
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
    if (u_class[i] == UC_BOAT) {
        float hb;
        if (!(nav_node(0, w_ifloor(nx), w_ifloor(nz), &hb) & NAV_WATER)) return 0;
        u_x[i] = nx; u_z[i] = nz; u_layer[i] = 0;
        return 1;
    }
    float hCur;
    int cx = w_ifloor(u_x[i]), cz = w_ifloor(u_z[i]);
    int cf = nav_node(u_layer[i], cx, cz, &hCur);
    if (!cf) hCur = u_y[i];
    int tx = w_ifloor(nx), tz = w_ifloor(nz);
    int k;
    int ground = u_class[i] == UC_CATAPULT || u_class[i] == UC_RAM || u_class[i] == UC_TREB;
    for (k = -1; k < NAV_L; k++) {
        int l = (k < 0) ? (preferLayer >= 0 ? preferLayer : u_layer[i]) : k;
        if (ground && l != 0) continue;
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

/* Units sharing a field must move alike: 0 on foot, 1 boats, 2 siege
 * engines (ground only). */
static int move_group(int i) {
    int c = u_class[i];
    if (c == UC_BOAT) return 1;
    if (c == UC_CATAPULT || c == UC_RAM || c == UC_TREB) return 2;
    return 0;
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
        if (move_group(i) != move_group(lead)) continue;
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
    g_field_mode_water = move_group(lead) == 1;
    g_field_mode_ground = move_group(lead) == 2;
    int f = field_create(u_gx[lead], u_gy[lead], u_gz[lead], u_gl[lead], mx, mz, attack ? 1 : n);
    g_field_mode_water = 0; g_field_mode_ground = 0;
    /* Units on a recycled field lose it. */
    for (i = 0; i < g_un; i++) {
        if (u_active[i] && u_field[i] == f) { u_field[i] = -1; if (u_state[i] == US_MOVE || u_state[i] == US_WAIT) u_state[i] = US_IDLE; }
    }
    float gx = u_gx[lead], gz = u_gz[lead];
    int grp = move_group(lead);
    for (i = 0; i < g_un; i++) {
        if (!u_repath[i] || !unit_alive(i)) continue;
        if (w_absf(u_gx[i] - gx) > 0.5f || w_absf(u_gz[i] - gz) > 0.5f) continue;
        if (move_group(i) != grp) continue;
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

static int g_order_soldiers_only;   /* order_move leaves peasants alone (they were just sent to gather) */
static void dragon_order_attack(int d, int t);
static void dragon_order_fly(int d, float x, float z);
static int g_order_boat_ok;         /* boats in the selection sail to g_order_bx/bz (open water) */
static float g_order_bx, g_order_bz;
static void peasant_to_manual(int i);
static void snap_walkable(float *x, float *z, float y);
static int snap_water(float *x, float *z);

/* Move (targetUnit < 0) or attack order for every selected player unit. */
static void order_move(float gx, float gy, float gz, int glayer, int targetUnit) {
    int i, n = 0;
    for (i = 0; i < g_un; i++) {
        if (!u_selected[i] || !unit_alive(i) || u_team[i] != TEAM_PLAYER) continue;
        if (u_class[i] == UC_PEASANT || u_class[i] == UC_BOAT) {
            /* peasants and boats just go there (no fighting); a move
             * order takes them off any job */
            if (g_order_soldiers_only) continue;
            if (u_class[i] == UC_BOAT && !g_order_boat_ok) continue;
            peasant_to_manual(i);
        }
        if (u_class[i] == UC_DRAGON) {
            /* tamed dragons fly: no field */
            u_amove[i] = 0; u_dhunt[i] = 0.0f;
            if (targetUnit >= 0) dragon_order_attack(i, targetUnit); else dragon_order_fly(i, gx, gz);
            n++;
            continue;
        }
        int sg = u_class[i] == UC_CATAPULT || u_class[i] == UC_RAM || u_class[i] == UC_TREB;
        if (sg) {
            u_st[i] = 0; u_amove[i] = 0;
            /* a catapult bombards a unit from range rather than walking up to it */
            if (targetUnit >= 0 && u_class[i] != UC_RAM) { u_target[i] = targetUnit; u_order[i] = ORD_NONE; unit_release_field(i); u_state[i] = US_IDLE; n++; continue; }
            u_target[i] = -1;
            glayer = 0;
        }
        int attack = targetUnit >= 0 && u_class[i] != UC_PEASANT && u_class[i] != UC_BOAT && !sg;
        u_gx[i] = gx; u_gy[i] = gy; u_gz[i] = gz; u_gl[i] = sg ? 0 : glayer;
        if (u_class[i] == UC_BOAT) { u_gx[i] = g_order_bx; u_gy[i] = W_SEA_LEVEL; u_gz[i] = g_order_bz; u_gl[i] = 0; }
        u_order[i] = attack ? ORD_ATTACK : ORD_MOVE;
        u_target[i] = attack ? targetUnit : -1;
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
/* ---- the home castle (render-thread copy of its site and layout) ---- */
static WSite g_hsite;
#define HB_MAX 640               /* home castle + AI castles + docks + built */
static int g_hb_n;
static int g_hb_t[HB_MAX];
static float g_hb_x[HB_MAX], g_hb_z[HB_MAX], g_hb_yaw[HB_MAX], g_hb_hx[HB_MAX], g_hb_hz[HB_MAX];
static int g_hb_own[HB_MAX];     /* faction owning it */
static int g_hb_hp[HB_MAX], g_hb_maxhp[HB_MAX], g_hb_dead[HB_MAX];
static uint32_t g_hb_id[HB_MAX]; /* the generator's id for it (castle layout / world dock) */
static int g_hb_ps[HB_MAX];      /* built ones: their g_ps_* entry once finished, else -1 */
static int building_maxhp(int type);
static void el_register_site(int own, WSite *s);
static int g_rs_tech[HB_MAX];    /* advancement being researched there (+1), 0 = none */
static float g_rs_t[HB_MAX];
/* Also in g_hb_*: fishing docks (world docks as their chunks load, and
 * player-built ones) and buildings the peasants build; those stand at
 * their own ground height and work only once finished. */
static float g_hb_y[HB_MAX];
static int g_hb_done[HB_MAX];
static int g_house_peasant[HB_MAX];      /* peasant living there (+1), 0 = none  */
static float g_peasant_timer;
static float g_tc_field_age;
/* barracks training queues (index = building) */
#define BQ_MAX 5
static int g_bq_n[HB_MAX];
static int g_bq_cls[HB_MAX * BQ_MAX];
static float g_bq_t[HB_MAX];

static float train_time_base(int cls) {
    if (cls == UC_BOAT) return 10.0f;
    if (cls == UC_CATAPULT) return 14.0f;
    if (cls == UC_RAM) return 12.0f;
    if (cls == UC_TREB) return 20.0f;
    return cls == UC_PEASANT ? 5.0f : (cls == UC_MACE ? 6.0f : (cls == UC_BOW ? 9.0f : 8.0f));
}

/* Training time for faction own (Drill speeds up soldiers). */
static float train_time_own(int own, int cls) {
    float t = train_time_base(cls);
    if ((cls == UC_SWORD || cls == UC_BOW || cls == UC_MACE) && has_tech(own, T_DRILL)) { float t2 = t * 0.65f; t = t2; }
    return t;
}

/* Training time of the unit at the head of building b's queue. */
static float bq_time(int b) { return train_time_own(g_hb_own[b], g_bq_cls[b * BQ_MAX]); }

/* ---- stockpile + costs ---- */

/* Training cost of a unit class: out[0] wood, out[1] stone, out[2] food. */
static void unit_cost(int cls, int *out) {
    out[0] = 0; out[1] = 0; out[2] = 0;
    if (cls == UC_PEASANT) out[2] = 15;
    else if (cls == UC_SWORD) { out[0] = 10; out[2] = 20; }
    else if (cls == UC_BOW) { out[0] = 15; out[2] = 20; }
    else if (cls == UC_MACE) { out[0] = 5; out[1] = 5; out[2] = 25; }
    else if (cls == UC_BOAT) out[0] = 30;
    else if (cls == UC_CATAPULT) { out[0] = 90; out[1] = 30; }
    else if (cls == UC_RAM) { out[0] = 70; out[1] = 10; }
    else if (cls == UC_TREB) { out[0] = 140; out[1] = 60; }
}

static int can_afford(int own, const int *c) { return f_wood[own] >= c[0] && f_stone[own] >= c[1] && f_food[own] >= c[2]; }

static void pay(int own, const int *c) {
    f_wood[own] = f_wood[own] - c[0]; f_stone[own] = f_stone[own] - c[1]; f_food[own] = f_food[own] - c[2];
}

/* Advancement: where it is researched, cost (wood, stone, food), time,
 * prerequisite (-1 none). */
static int tech_building(int t) {
    if (t == T_TOOLS) return BT_TOWNCENTER;
    if (t == T_NETS) return BT_DOCK;
    if (t == T_DRILL) return BT_BARRACKS;
    return BT_SMITH;
}

/* Can faction own start researching t at building b right now (ignoring
 * cost)? */
static int tech_available(int b, int t) {
    int own = g_hb_own[b];
    if (!g_hb_done[b] || g_hb_t[b] != tech_building(t) || g_rs_tech[b]) return 0;
    if (has_tech(own, t)) return 0;
    if (tech_prereq(t) >= 0 && !has_tech(own, tech_prereq(t))) return 0;
    return 1;
}

static void tech_apply_existing(int own, int t);

/* Starts research of t at b, paying for it: 1, 0 (not available) or -1
 * (can't afford). */
static int research_start(int b, int t) {
    int c[3], own;
    if (b < 0 || b >= g_hb_n || !tech_available(b, t)) return 0;
    own = g_hb_own[b];
    int k;
    for (k = 0; k < g_hb_n; k++) if (g_hb_own[k] == own && g_rs_tech[k] == t + 1) return 0;   /* already under way */
    tech_cost(t, c);
    if (!can_afford(own, c)) return -1;
    pay(own, c);
    g_rs_tech[b] = t + 1; g_rs_t[b] = 0.0f;
    __android_log_print(4, "castles", "faction %d researching %s at building %d", own, tech_name(t), b);
    return 1;
}

static void research_update(float dt) {
    int b;
    for (b = 0; b < g_hb_n; b++) {
        if (!g_rs_tech[b]) continue;
        float nt = g_rs_t[b] + dt;
        g_rs_t[b] = nt;
        int t = g_rs_tech[b] - 1;
        if (nt < tech_time(t)) continue;
        int own = g_hb_own[b];
        g_rs_tech[b] = 0;
        if (own >= 0 && own < F_MAX) { f_tech[own] = f_tech[own] | (1 << t); tech_apply_existing(own, t); }
        __android_log_print(4, "castles", "faction %d advanced: %s", own, tech_name(t));
    }
}

/* Unit-stat advancements also upgrade the units already in the field. */
static void tech_apply_existing(int own, int t) {
    int i;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_own[i] != own) continue;
        int c = u_class[i];
        if (c != UC_SWORD && c != UC_BOW && c != UC_MACE) continue;
        if (t == T_ARMOR) { u_maxhp[i] = u_maxhp[i] + 2; u_hp[i] = u_hp[i] + 2; }
        if (t == T_DRILL) { float ns2 = u_speed[i] * 1.1f; u_speed[i] = ns2; }
        if (t == T_FORGING && c != UC_BOW) u_dmgb[i] = 1.0f;
        if (t == T_FLETCHING && c == UC_BOW) u_dmgb[i] = 1.0f;
    }
}

/* Bow range of unit i. */
static float unit_range(int i) { return ARCHER_RANGE + (has_tech(u_own[i], T_FLETCHING) ? 4.0f : 0.0f); }

/* Door point of building b (in front of its street-facing side). */
static void building_door(int b, float extra, float *x, float *z) {
    /* buildings face along yaw - pi/2 rotated frame: front = +local z */
    float c = w_cos(g_hb_yaw[b] - 0.5f * W_PI), sn = w_sin(g_hb_yaw[b] - 0.5f * W_PI);
    float lz = g_hb_hz[b] + extra;
    *x = g_hb_x[b] - lz * sn;
    *z = g_hb_z[b] + lz * c;
}

static void units_home_init(void) {
    int htx, htz, b;
    g_hb_n = 0;
    if (!site_find_home(&htx, &htz, &g_hsite)) return;
    g_hb_n = site_layout(&g_hsite, g_hb_t, g_hb_x, g_hb_z, g_hb_yaw, g_hb_hx, g_hb_hz);
    f_houses[0] = 0;
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_t[b] == BT_HOUSE) f_houses[0]++;
        g_house_peasant[b] = 0;
        g_bq_n[b] = 0; g_bq_t[b] = 0.0f;
        g_hb_y[b] = g_hsite.base; g_hb_done[b] = 1;
        g_hb_own[b] = 0; g_rs_tech[b] = 0;
        g_hb_maxhp[b] = building_maxhp(g_hb_t[b]); g_hb_hp[b] = g_hb_maxhp[b]; g_hb_dead[b] = 0;
        g_hb_id[b] = el_id_xz(g_hb_x[b], g_hb_z[b]); g_hb_ps[b] = -1;
    }
    f_tc[0] = 0;
    el_register_site(0, &g_hsite);
    g_home_set = 1;
    g_home_cx = w_ifloor(g_hsite.cx / W_CHUNK_SIZE); g_home_cz = w_ifloor(g_hsite.cz / W_CHUNK_SIZE);
}

/* Adds a building to g_hb_* (done = 0 while under construction);
 * returns its index or -1 when full. */
static int g_add_own;     /* owner for home_add_building (squash: six float parameters at most) */
static int home_add_building(int type, float x, float z, float y, float yaw, float hx, float hz, int done) {
    int b = g_hb_n;
    if (b >= HB_MAX) return -1;
    g_hb_t[b] = type; g_hb_x[b] = x; g_hb_z[b] = z; g_hb_y[b] = y; g_hb_yaw[b] = yaw;
    g_hb_hx[b] = hx; g_hb_hz[b] = hz; g_hb_done[b] = done;
    g_hb_own[b] = g_add_own; g_rs_tech[b] = 0;
    g_hb_maxhp[b] = building_maxhp(type); g_hb_hp[b] = g_hb_maxhp[b]; g_hb_dead[b] = 0;
    g_hb_id[b] = el_id_xz(x, z); g_hb_ps[b] = -1;
    g_house_peasant[b] = 0; g_bq_n[b] = 0; g_bq_t[b] = 0.0f;
    g_hb_n = b + 1;
    return b;
}

/* A dock's g_hb_* footprint from its shore anchor: centred so it covers
 * the shed (behind the anchor) and the pier; its "door" is the pier end. */
static int home_add_dock(float x, float z, float y, float yaw, int done) {
    float dx = w_cos(yaw), dz = w_sin(yaw);
    int b = home_add_building(BT_DOCK, x + dx * 1.3f, z + dz * 1.3f, y, yaw, 2.9f, 3.7f, done);
    if (b >= 0) g_hb_id[b] = el_id_xz(x, z);          /* world docks: the generator's anchor */
    return b;
}

/* World docks of a freshly loaded slot (once each). */
static void units_register_docks(int slot) {
    int k, b, n = g_slot_ndock[slot];
    if (!g_home_set) return;
    for (k = 0; k < n; k++) {
        int q = slot * DOCK_OUT_MAX + k, dup = 0;
        float yaw = g_slot_dock_yaw[q];
        float cx = g_slot_dock_x[q] + w_cos(yaw) * 1.3f, cz = g_slot_dock_z[q] + w_sin(yaw) * 1.3f;
        for (b = 0; b < g_hb_n; b++)
            if (g_hb_t[b] == BT_DOCK && w_absf(g_hb_x[b] - cx) < 1.0f && w_absf(g_hb_z[b] - cz) < 1.0f) dup = 1;
        if (dup) continue;
        g_add_own = 0;
        if (home_add_dock(g_slot_dock_x[q], g_slot_dock_z[q], g_slot_dock_y[q], yaw, 1) >= 0)
            __android_log_print(4, "castles", "fishing dock found at %d,%d", (int)cx, (int)cz);
    }
}

/* Population of faction own: the town centre houses 10, every house 4
 * more. */
static int pop_limit_of(int own) { return 10 + 4 * f_houses[own]; }

static int pop_count_of(int own) {
    int i, n = 0;
    for (i = 0; i < g_un; i++) if (unit_alive(i) && u_own[i] == own) n++;
    return n;
}

static int pop_queued_of(int own) {
    int b, n = 0;
    for (b = 0; b < g_hb_n; b++) if (g_hb_own[b] == own) n = n + g_bq_n[b];
    return n;
}

static int pop_limit(void) { return pop_limit_of(0); }
static int pop_count(void) { return pop_count_of(0); }
static int pop_queued(void) { return pop_queued_of(0); }

/* Queues a unit at building b (soldiers at a barracks, peasants at the
 * town centre, boats at a dock), paying its cost; 0 if the queue or
 * population is full, -1 if the stockpile can't pay. */
static int barracks_train(int b, int cls) {
    if (b < 0 || b >= g_hb_n || !g_hb_done[b]) return 0;
    if (g_hb_t[b] == BT_BARRACKS) { if (cls != UC_SWORD && cls != UC_BOW && cls != UC_MACE) return 0; }
    else if (g_hb_t[b] == BT_WORKSHOP) { if (cls != UC_CATAPULT && cls != UC_RAM && cls != UC_TREB) return 0; }
    else if (g_hb_t[b] == BT_TOWNCENTER) { if (cls != UC_PEASANT) return 0; }
    else if (g_hb_t[b] == BT_DOCK) { if (cls != UC_BOAT) return 0; }
    else return 0;
    int own = g_hb_own[b];
    if (g_bq_n[b] >= BQ_MAX) return 0;
    if (pop_count_of(own) + pop_queued_of(own) >= pop_limit_of(own)) return 0;
    int cost[3];
    unit_cost(cls, cost);
    if (!can_afford(own, cost)) return -1;
    pay(own, cost);
    g_bq_cls[b * BQ_MAX + g_bq_n[b]] = cls;
    g_bq_n[b] = g_bq_n[b] + 1;
    return 1;
}

/* Called for each castle chunk that gets loaded (the chunk holding the
 * castle's centre). The home castle gets the player's squad on its
 * town-centre plaza; every other castle a garrison scaled to its size,
 * in the courtyards and on the wall-walks of all its rings. */
static void units_on_castle_loaded(int slot) {
    float kx = g_slot_keep_x[slot], kz = g_slot_keep_z[slot], ky = g_slot_keep_y[slot];
    int i;
    for (i = 0; i < g_npop; i++) {
        float dx = g_pop_x[i] - kx, dz = g_pop_z[i] - kz;
        if (dx * dx + dz * dz < 1.0f) return;
    }
    if (g_npop >= MAX_POP) return;
    g_pop_x[g_npop] = kx; g_pop_z[g_npop] = kz; g_npop = g_npop + 1;
    float px[600], pz[600];
    uint32_t seed = w_hash2(w_ifloor(kx), w_ifloor(kz), 777u);
    int home = g_home_set && g_slot_cx[slot] == g_home_cx && g_slot_cz[slot] == g_home_cz;
    if (home) {
        /* plaza in front of the town centre */
        float dxp, dzp;
        building_door(0, 6.0f, &dxp, &dzp);
        int n = nav_collect(0, dxp, dzp, ky, 6.0f, px, pz, 600);
        int k, made = 0;
        for (k = 0; k < 10 && n > 0; k++) {
            int pick = (int)(w_hash(seed + (uint32_t)k * 31u) % (uint32_t)n);
            int cls = k < 4 ? UC_SWORD : (k < 7 ? UC_BOW : UC_MACE);
            if (unit_spawn(TEAM_PLAYER, px[pick], pz[pick], 0, seed + (uint32_t)k, cls, 0) >= 0) made++;
        }
        g_home_spawned = 1;
        __android_log_print(4, "castles", "player squad spawned: %d units at home castle (%d houses, pop limit %d)",
                            made, f_houses[0], pop_limit());
        return;
    }
    WSite site;
    w_site_compute(w_ifloor(kx / SITE_TILE), w_ifloor(kz / SITE_TILE), &site);
    if (!site.valid) return;
    int gown = faction_at_site(site.tx, site.tz);    /* an AI castle's own garrison, or unowned */
    int guards = 0, k;
    int want = 4 + (int)(site.R / 14.0f);
    int n = nav_collect(0, kx, kz, ky, 14.0f, px, pz, 600);
    for (k = 0; k < want / 2 && n > 0; k++) {
        int pick = (int)(w_hash(seed + (uint32_t)k * 17u) % (uint32_t)n);
        int gu = unit_spawn(TEAM_ENEMY, px[pick], pz[pick], 0, seed + 100u + (uint32_t)k, UC_RANDOM, gown);
        if (gu >= 0) { guards++; u_role[gu] = gown > 0 ? R_GARRISON : 0; }
    }
    /* wall-walk nodes of every ring (heights differ per ring) */
    int nw = 0, r;
    for (r = 0; r < site.rings && nw < 500; r++) {
        int got = nav_collect(1, kx, kz, site_ring_top(&site, r), site_ring_r(&site, r) + 4.0f, px + nw, pz + nw, 600 - nw);
        nw = nw + got;
    }
    for (k = 0; k < want - want / 2 && nw > 0; k++) {
        int pick = (int)(w_hash(seed + (uint32_t)k * 53u + 7u) % (uint32_t)nw);
        int cls = (k & 1) ? UC_BOW : UC_RANDOM;          /* archers favour the walls */
        int gu = unit_spawn(TEAM_ENEMY, px[pick], pz[pick], 1, seed + 200u + (uint32_t)k, cls, gown);
        if (gu >= 0) { guards++; u_role[gu] = gown > 0 ? R_GARRISON : 0; }
    }
    __android_log_print(4, "castles", "castle guards spawned: %d (castle radius %d, rings %d, wall nodes %d)",
                        guards, (int)site.R, site.rings, nw);
}

/* ---- peasants ---- */
/* A persistent GPU flow field to the town-centre door, shared by every
 * peasant (rebuilt every so often so newly streamed chunks count). */
static void peasants_field_update(float dt) {
    float na = g_tc_field_age + dt;
    g_tc_field_age = na;
    if (!g_home_spawned || g_hb_n == 0) return;
    if (g_tc_field >= 0 && g_tc_field_age < 30.0f) return;
    if (g_tc_field >= 0 && (g_field_state[g_tc_field] == FS_UPLOAD || g_field_state[g_tc_field] == FS_COMPUTING)) return;
    float dx, dz;
    building_door(0, 1.8f, &dx, &dz);
    if (g_tc_field >= 0) g_field_refs[g_tc_field] = 0;
    g_tc_field = field_create(dx, g_hsite.base, dz, 0, g_hsite.cx, g_hsite.cz, 6);
    g_field_refs[g_tc_field] = 1000;          /* never recycled by orders */
    g_tc_field_age = 0.0f;
}

/* Moves in by one house per few seconds while population allows. */
static void peasants_spawn(float dt) {
    if (!g_home_spawned) return;
    float nt = g_peasant_timer + dt;
    g_peasant_timer = nt;
    if (g_peasant_timer < 3.0f) return;
    g_peasant_timer = 0.0f;
    if (pop_count() + pop_queued() >= pop_limit()) return;
    int b;
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_t[b] != BT_HOUSE || !g_hb_done[b] || g_hb_own[b] != 0) continue;
        if (g_house_peasant[b] && unit_alive(g_house_peasant[b] - 1) && u_class[g_house_peasant[b] - 1] == UC_PEASANT) continue;
        float x, z;
        building_door(b, 0.9f, &x, &z);
        float h;
        int ccx = w_ifloor(x / W_CHUNK_SIZE), ccz = w_ifloor(z / W_CHUNK_SIZE);
        if (nav_slot_for(ccx, ccz) < 0) continue;
        snap_walkable(&x, &z, g_hb_y[b]);
        if (!(nav_node(0, w_ifloor(x), w_ifloor(z), &h) & NAV_WALK)) continue;
        int u = unit_spawn(TEAM_PLAYER, x, z, 0, w_hash2(b, 99, 5u), UC_PEASANT, 0);
        if (u < 0) return;
        g_house_peasant[b] = u + 1;
        u_home_x[u] = x; u_home_z[u] = z;
        u_ptimer[u] = 2.0f + (float)(b % 7);
        return;
    }
}

static void peasant_trail_push(int i) {
    int n = u_trail_n[i];
    if (n >= TRAIL_MAX) {
        /* full: keep every other point */
        int k;
        for (k = 0; k < TRAIL_MAX / 2; k++) {
            u_trail_x[i * TRAIL_MAX + k] = u_trail_x[i * TRAIL_MAX + k * 2];
            u_trail_z[i * TRAIL_MAX + k] = u_trail_z[i * TRAIL_MAX + k * 2];
        }
        n = TRAIL_MAX / 2;
    }
    u_trail_x[i * TRAIL_MAX + n] = u_x[i];
    u_trail_z[i * TRAIL_MAX + n] = u_z[i];
    u_trail_n[i] = n + 1;
}

/* ---- resources + gathering ---- */
#define WOOD_PER_TREE   30
#define STONE_PER_ROCK  45
#define CARRY_MAX        8   /* a peasant's load            */
#define CARRY_BOAT      12   /* a fishing boat's hold       */
#define FISH_PER_SHOAL  60
#define HARVEST_MAX    256
static int g_hv_n;                               /* partly harvested resources */
static uint32_t g_hv_id[HARVEST_MAX];
static int g_hv_taken[HARVEST_MAX];

static int res_capacity(uint32_t id) {
    if (id & 0x80000000u) return STONE_PER_ROCK;
    return (id & 0x40000000u) ? FISH_PER_SHOAL : WOOD_PER_TREE;
}

static int res_remaining(uint32_t id) {
    int k;
    if (w_res_depleted(id)) return 0;
    for (k = 0; k < g_hv_n; k++) if (g_hv_id[k] == id) return res_capacity(id) - g_hv_taken[k];
    return res_capacity(id);
}

/* Queues the loaded chunk holding (x,z) for regeneration. */
static void request_rebuild_at(float x, float z) {
    int cx = w_ifloor(x / W_CHUNK_SIZE), cz = w_ifloor(z / W_CHUNK_SIZE), k;
    for (k = 0; k < g_rebuild_n; k++) if (g_rebuild_cx[k] == cx && g_rebuild_cz[k] == cz) return;
    if (g_rebuild_n >= REBUILD_MAX) return;
    g_rebuild_cx[g_rebuild_n] = cx; g_rebuild_cz[g_rebuild_n] = cz;
    g_rebuild_n = g_rebuild_n + 1;
}

/* Takes up to `want` from resource id (at x,z); returns the amount taken.
 * The last unit removes it from the world: its id goes on the depleted
 * list and its chunk is regenerated without it. */
static int res_take(uint32_t id, float x, float z, int want) {
    int k, slot = -1;
    if (w_res_depleted(id)) return 0;
    for (k = 0; k < g_hv_n; k++) if (g_hv_id[k] == id) { slot = k; break; }
    if (slot < 0) {
        if (g_hv_n >= HARVEST_MAX) return 0;
        slot = g_hv_n;
        g_hv_id[slot] = id; g_hv_taken[slot] = 0;
        g_hv_n = slot + 1;
    }
    int left = res_capacity(id) - g_hv_taken[slot];
    int got = want < left ? want : left;
    g_hv_taken[slot] = g_hv_taken[slot] + got;
    if (g_hv_taken[slot] >= res_capacity(id) && g_ndepleted < DEPL_MAX) {
        g_depleted[g_ndepleted] = id;
        g_ndepleted = g_ndepleted + 1;
        g_hv_n = g_hv_n - 1;
        g_hv_id[slot] = g_hv_id[g_hv_n]; g_hv_taken[slot] = g_hv_taken[g_hv_n];
        if (!(id & 0x40000000u)) request_rebuild_at(x, z);       /* shoals have no mesh */
        __android_log_print(4, "castles", "%s at %d,%d used up (%d removed so far)",
                            (id & 0x80000000u) ? "rock" : ((id & 0x40000000u) ? "fish shoal" : "tree"),
                            (int)x, (int)z, g_ndepleted);
    }
    return got;
}

/* Nearest loaded resource of `type` (-1 any) with something left,
 * within r of (x,z); returns slot*RES_MAX+k or -1. */
static int res_find_near(float x, float z, float r, int type) {
    int sl, k, best = -1;
    float bd = r * r;
    for (sl = 0; sl < SLOTS; sl++) {
        if (!g_slot_used[sl] || g_slot_nres[sl] == 0) continue;
        float ox = (float)g_slot_cx[sl] * W_CHUNK_SIZE, oz = (float)g_slot_cz[sl] * W_CHUNK_SIZE;
        if (x < ox - r || x > ox + W_CHUNK_SIZE + r || z < oz - r || z > oz + W_CHUNK_SIZE + r) continue;
        int n = g_slot_nres[sl];
        for (k = 0; k < n; k++) {
            int q = sl * RES_MAX + k;
            if (type >= 0 && g_slot_res_t[q] != type) continue;
            float dx = g_slot_res_x[q] - x, dz = g_slot_res_z[q] - z;
            float d = dx * dx + dz * dz;
            if (d >= bd) continue;
            if (res_remaining(g_slot_res_id[q]) <= 0) continue;
            bd = d; best = q;
        }
    }
    return best;
}

/* Gives peasant i resource q as its target. */
static void peasant_set_res(int i, int q) {
    u_res_id[i] = g_slot_res_id[q];
    u_res_x[i] = g_slot_res_x[q]; u_res_z[i] = g_slot_res_z[q]; u_res_r[i] = g_slot_res_r[q];
}

/* Nearest walkable ground cell centre to (x,z) within 4 units (x,z
 * unchanged if it is walkable itself or nothing is found). */
static void snap_walkable(float *x, float *z, float y) {
    float h, px[96], pz[96];
    if (nav_node(0, w_ifloor(*x), w_ifloor(*z), &h) & NAV_WALK) return;
    int n = nav_collect(0, *x, *z, y, 4.0f, px, pz, 96), k, best = -1;
    float bd = 1e30f;
    for (k = 0; k < n; k++) {
        float dx = px[k] - *x, dz = pz[k] - *z;
        if (dx * dx + dz * dz < bd) { bd = dx * dx + dz * dz; best = k; }
    }
    if (best >= 0) { *x = px[best]; *z = pz[best]; }
}

/* Nearest boat-navigable water cell centre to (x,z) within 7 units;
 * 0 if there is none. */
static int snap_water(float *x, float *z) {
    int i, j, bi = 0, bj = 0, found = 0;
    float bd = 1e30f, h;
    int cx = w_ifloor(*x), cz = w_ifloor(*z);
    for (j = -7; j <= 7; j++) {
        for (i = -7; i <= 7; i++) {
            if (!(nav_node(0, cx + i, cz + j, &h) & NAV_WATER)) continue;
            float dx = (float)(cx + i) + 0.5f - *x, dz = (float)(cz + j) + 0.5f - *z;
            float d = dx * dx + dz * dz;
            if (d < bd) { bd = d; bi = i; bj = j; found = 1; }
        }
    }
    if (!found) return 0;
    *x = (float)(cx + bi) + 0.5f; *z = (float)(cz + bj) + 0.5f;
    return 1;
}

/* Gather jobs: one per resource site the player sent peasants (wood,
 * stone) or boats (fish) to. Each pins a GPU field to the site; loads go
 * back to the drop point -- the town-centre door on the shared
 * town-centre field (or the job's own field when the site lies outside
 * that one's window), or for boats a dock's pier end on the job's own
 * water field. Fields are pinned with refs and re-validated by age,
 * since field_create may still recycle a pinned field when every one is
 * busy. */
#define GJ_MAX 10
static int g_gj_on[GJ_MAX], g_gj_type[GJ_MAX], g_gj_water[GJ_MAX], g_gj_own[GJ_MAX];
static float g_gj_x[GJ_MAX], g_gj_z[GJ_MAX], g_gj_y[GJ_MAX], g_gj_age[GJ_MAX], g_gj_dx[GJ_MAX], g_gj_dz[GJ_MAX];
static int g_gj_f[GJ_MAX], g_gj_fage[GJ_MAX], g_gj_drop[GJ_MAX], g_gj_dage[GJ_MAX];
static int g_gj_dropb[GJ_MAX];       /* land jobs: the building loads go to (town centre or a stockpile) */

/* Where faction own's peasants unload wood / stone gathered at (x,z):
 * the door of the town centre or of a finished stockpile, whichever is
 * nearest the resource. Returns the building, -1 if there is none. */
static int nearest_drop_building(int own, float x, float z, float *dx, float *dz) {
    int b, best = -1;
    float bd = 1e30f;
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_own[b] != own || !g_hb_done[b] || g_hb_dead[b]) continue;
        if (g_hb_t[b] != BT_STOCKPILE && b != f_tc[own]) continue;
        float ex, ez;
        building_door(b, g_hb_t[b] == BT_STOCKPILE ? 1.2f : 1.8f, &ex, &ez);
        float d = (ex - x) * (ex - x) + (ez - z) * (ez - z);
        if (d < bd) { bd = d; best = b; *dx = ex; *dz = ez; }
    }
    return best;
}

static int field_valid(int f, int age) { return f >= 0 && g_field_age[f] == age; }

static void field_unpin(int f, int age) { if (field_valid(f, age)) g_field_refs[f] = 0; }

/* Builds (or rebuilds) job j's fields. */
static void gather_job_fields(int j) {
    float dx = g_gj_dx[j], dz = g_gj_dz[j];
    float mx = (g_gj_x[j] + dx) * 0.5f, mz = (g_gj_z[j] + dz) * 0.5f;
    field_unpin(g_gj_f[j], g_gj_fage[j]);
    g_field_mode_water = g_gj_water[j];
    int f = field_create(g_gj_x[j], g_gj_y[j], g_gj_z[j], 0, mx, mz, g_gj_water[j] ? 16 : 12);
    g_field_refs[f] = 1000;
    g_gj_f[j] = f; g_gj_fage[j] = g_field_age[f];
    field_unpin(g_gj_drop[j], g_gj_dage[j]);
    g_gj_drop[j] = -1;
    /* the shared town-centre field covers land sites inside its window */
    float tox = g_hsite.cx - (float)(NAVW / 2) + 6.0f, toz = g_hsite.cz - (float)(NAVW / 2) + 6.0f;
    float span = (float)NAVW - 12.0f;
    int inTc = g_gj_own[j] == 0 && !g_gj_water[j] && g_gj_dropb[j] == f_tc[0] && g_gj_x[j] > tox && g_gj_x[j] < tox + span && g_gj_z[j] > toz && g_gj_z[j] < toz + span;
    if (!inTc) {
        int d = field_create(dx, g_gj_water[j] ? W_SEA_LEVEL : g_hb_y[g_gj_dropb[j] >= 0 ? g_gj_dropb[j] : f_tc[g_gj_own[j]]], dz, 0, mx, mz, 6);
        g_field_refs[d] = 1000;
        g_gj_drop[j] = d; g_gj_dage[j] = g_field_age[d];
    }
    g_field_mode_water = 0;
    g_gj_age[j] = 0.0f;
}

static void gather_job_end(int j) {
    if (!g_gj_on[j]) return;
    g_gj_on[j] = 0;
    field_unpin(g_gj_f[j], g_gj_fage[j]);
    field_unpin(g_gj_drop[j], g_gj_dage[j]);
}

/* Drop-off field for job j (-1 while none is usable). */
static int gather_drop_field(int j) {
    if (j >= 0 && j < GJ_MAX && g_gj_on[j] && g_gj_drop[j] >= 0) return field_valid(g_gj_drop[j], g_gj_dage[j]) ? g_gj_drop[j] : -1;
    if (j >= 0 && j < GJ_MAX && (g_gj_water[j] || g_gj_own[j] != 0)) return -1;
    return g_tc_field;
}

static int unit_is_gatherer(int i) { return u_class[i] == UC_PEASANT || u_class[i] == UC_BOAT; }

/* Upkeep: end empty jobs, rebuild lost or stale fields. */
static void gather_jobs_update(float dt) {
    int j, i;
    for (j = 0; j < GJ_MAX; j++) {
        if (!g_gj_on[j]) continue;
        int members = 0;
        for (i = 0; i < g_un; i++) if (unit_alive(i) && unit_is_gatherer(i) && u_pstate[i] >= PS_G_GO && u_pstate[i] <= PS_G_DROP && u_job[i] == j) members++;
        if (members == 0) { gather_job_end(j); continue; }
        float na = g_gj_age[j] + dt;
        g_gj_age[j] = na;
        /* land jobs unload at the nearest drop point: re-pick it when a
         * stockpile has been finished or the chosen one is gone */
        if (!g_gj_water[j]) {
            float ndx, ndz;
            int nb = nearest_drop_building(g_gj_own[j], g_gj_x[j], g_gj_z[j], &ndx, &ndz);
            if (nb >= 0 && nb != g_gj_dropb[j]) {
                g_gj_dropb[j] = nb; g_gj_dx[j] = ndx; g_gj_dz[j] = ndz;
                gather_job_fields(j);
                __android_log_print(4, "castles", "gather job %d now unloads at %s %d", j, g_hb_t[nb] == BT_STOCKPILE ? "stockpile" : "town centre", nb);
                continue;
            }
        }
        int lost = !field_valid(g_gj_f[j], g_gj_fage[j]) || (g_gj_drop[j] >= 0 && !field_valid(g_gj_drop[j], g_gj_dage[j]));
        if (lost || na > 45.0f) gather_job_fields(j);
    }
}

/* Moves job j's site to the nearest resource of its type within 40 of
 * the old one; 0 if there is none (the job then ends). */
static int gather_job_relocate(int j) {
    int q = res_find_near(g_gj_x[j], g_gj_z[j], 40.0f, g_gj_type[j]);
    if (q < 0) return 0;
    g_gj_x[j] = g_slot_res_x[q]; g_gj_z[j] = g_slot_res_z[q]; g_gj_y[j] = g_slot_res_y[q];
    gather_job_fields(j);
    return 1;
}

static void peasant_to_manual(int i) {
    u_pstate[i] = PS_MANUAL;
    u_job[i] = -1;
    u_state[i] = US_IDLE;
    u_stuck[i] = 0.0f;
}

/* Where faction own's boats unload: the pier end of its finished dock
 * nearest (x,z), moved onto open water. 0 if there is none. */
static int nearest_dock_drop(int own, float x, float z, float *dx, float *dz) {
    int b, best = -1;
    float bd = 1e30f;
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_t[b] != BT_DOCK || !g_hb_done[b] || g_hb_own[b] != own) continue;
        float ex = g_hb_x[b] - x, ez = g_hb_z[b] - z;
        if (ex * ex + ez * ez < bd) { bd = ex * ex + ez * ez; best = b; }
    }
    if (best < 0) return 0;
    building_door(best, 1.4f, dx, dz);
    return snap_water(dx, dz);
}

/* A gather job of faction own for resource q (an existing one on the
 * same patch, else a free slot; the player may also take over its own
 * oldest). -1 if there is none, or no drop point (boats need a dock). */
static int gather_job_for(int own, int q) {
    int i, j, job = -1;
    int type = g_slot_res_t[q], water = type == RES_FOOD;
    float x = g_slot_res_x[q], z = g_slot_res_z[q];
    for (j = 0; j < GJ_MAX && job < 0; j++) {
        if (!g_gj_on[j] || g_gj_type[j] != type || g_gj_own[j] != own) continue;
        float dx = g_gj_x[j] - x, dz = g_gj_z[j] - z;
        if (dx * dx + dz * dz < 12.0f * 12.0f) job = j;
    }
    if (job >= 0) return job;
    float ddx, ddz;
    if (water) { if (!nearest_dock_drop(own, x, z, &ddx, &ddz)) return -1; }
    int dropb = -1;
    if (!water) { dropb = nearest_drop_building(own, x, z, &ddx, &ddz); if (dropb < 0) return -1; }
    for (j = 0; j < GJ_MAX && job < 0; j++) if (!g_gj_on[j]) job = j;
    if (job < 0) {
        if (own != 0) return -1;
        for (j = 0; j < GJ_MAX && job < 0; j++) if (g_gj_own[j] == 0) job = j;
        if (job < 0) return -1;
        for (i = 0; i < g_un; i++) if (unit_is_gatherer(i) && u_job[i] == job && u_pstate[i] >= PS_G_GO && u_pstate[i] <= PS_G_DROP) peasant_to_manual(i);
        gather_job_end(job);
    }
    g_gj_on[job] = 1; g_gj_type[job] = type; g_gj_water[job] = water; g_gj_own[job] = own;
    g_gj_x[job] = x; g_gj_z[job] = z; g_gj_y[job] = g_slot_res_y[q];
    g_gj_dx[job] = ddx; g_gj_dz[job] = ddz; g_gj_dropb[job] = dropb;
    g_gj_f[job] = -1; g_gj_drop[job] = -1;
    gather_job_fields(job);
    return job;
}

static int carry_max(int i) {
    if (u_class[i] == UC_BOAT) return has_tech(u_own[i], T_NETS) ? 18 : CARRY_BOAT;
    return CARRY_MAX;
}

/* Puts gatherer i on job (working resource q). */
static void gather_assign_unit(int i, int job, int q) {
    unit_release_field(i);
    u_order[i] = ORD_NONE; u_repath[i] = 0;
    if (u_carry[i] > 0 && u_carry_t[i] != g_gj_type[job]) u_carry[i] = 0;    /* drops the other load */
    u_job[i] = job;
    peasant_set_res(i, q);
    u_pstate[i] = u_carry[i] >= carry_max(i) ? PS_G_DROP : PS_G_GO;
    u_state[i] = US_IDLE;
    u_stuck[i] = 0.0f;
}

/* Sends every selected player gatherer that fits resource q -- peasants
 * to trees and rocks, fishing boats to fish shoals -- to work it;
 * returns how many went. Others in the selection are left to order_move. */
static int order_gather(int q) {
    int i, n = 0;
    int type = g_slot_res_t[q], cls = type == RES_FOOD ? UC_BOAT : UC_PEASANT, water = type == RES_FOOD;
    for (i = 0; i < g_un; i++) if (u_selected[i] && unit_alive(i) && u_own[i] == 0 && u_class[i] == cls) n++;
    if (n == 0 || g_hb_n == 0) return 0;
    int job = gather_job_for(0, q);
    if (job < 0) {
        __android_log_print(4, "castles", water ? "fishing: no finished dock to bring the catch to" : "gathering: no job slot free");
        return 0;
    }
    for (i = 0; i < g_un; i++) {
        if (!u_selected[i] || !unit_alive(i) || u_own[i] != 0 || u_class[i] != cls) continue;
        gather_assign_unit(i, job, q);
    }
    float x = g_slot_res_x[q], z = g_slot_res_z[q];
    g_order_marker_on = 1;
    g_order_mx = x; g_order_my = g_slot_res_y[q]; g_order_mz = z; g_order_mt = 0.0f;
    __android_log_print(4, "castles", "%s at %d,%d: %d %s (job %d)", type == RES_WOOD ? "gather wood" : (type == RES_STONE ? "gather stone" : "fish"),
                        (int)x, (int)z, n, water ? "boats" : "peasants", job);
    return n;
}

static void unit_start_climb(int i, int wx, int wz, int layer);

/* Walks unit i one frame down field f (climbing ladders on the way).
 * Returns 1 once within `arrive` of the goal, 0 while walking, -1 while
 * the field isn't ready. Off the field's window (or where it has no
 * route) it heads straight for the goal. u_stuck accumulates while the
 * unit makes no progress. */
static int unit_walk_field(int i, int f, float arrive, float dt) {
    if (f < 0 || g_field_state[f] != FS_READY) return -1;
    float tx = g_field_gx[f] - u_x[i], tz = g_field_gz[f] - u_z[i];
    int pl = -1;
    int lx = w_ifloor(u_x[i]) - g_field_ox[f], lz = w_ifloor(u_z[i]) - g_field_oz[f];
    if (lx >= 0 && lz >= 0 && lx < NAVW && lz < NAVW) {
        float here = g_fd[f * NAVW_N + u_layer[i] * NAVW * NAVW + lz * NAVW + lx];
        if (here <= arrive) return 1;
        int bx = 0, bz = 0, bl = 0;
        if (here < FIELD_INF * 0.5f && field_best_step(f, lx, lz, u_layer[i], &bx, &bz, &bl)) {
            int wx = bx + g_field_ox[f], wz = bz + g_field_oz[f];
            float th;
            nav_node(bl, wx, wz, &th);
            if (!g_field_water[f] && bl != u_layer[i] && w_absf(th - u_y[i]) > NAV_MAXSTEP + 0.2f) { unit_start_climb(i, wx, wz, bl); return 0; }
            tx = (float)wx + 0.5f - u_x[i]; tz = (float)wz + 0.5f - u_z[i];
            pl = bl;
        }
    }
    float d = w_sqrt(tx * tx + tz * tz);
    if (pl < 0 && d < arrive) return 1;
    if (d < 0.01f) return 0;
    u_state[i] = US_MOVE;
    unit_face(i, tx, tz, dt);
    float step = u_speed[i] * dt;
    if (step > d) step = d;
    float ox = u_x[i], oz = u_z[i];
    unit_walk(i, tx / d * step, tz / d * step, pl);
    float mx = u_x[i] - ox, mz = u_z[i] - oz;
    if (mx * mx + mz * mz < 1e-6f) { float ns = u_stuck[i] + dt; u_stuck[i] = ns; } else u_stuck[i] = 0.0f;
    return 0;
}

/* Gathering: walk (or sail) to the site, pick a tree / rock / shoal,
 * work it a load at a time, carry each load to the drop point, repeat;
 * move on to the next one of the patch as they run out. */
static void peasant_gather_update(int i, float dt) {
    int j = u_job[i], ps = u_pstate[i];
    int jobOk = j >= 0 && j < GJ_MAX && g_gj_on[j];
    int boat = u_class[i] == UC_BOAT;
    if (!jobOk && ps != PS_G_DROP) { peasant_to_manual(i); return; }
    if (ps == PS_G_GO) {
        float rx = u_res_x[i] - u_x[i], rz = u_res_z[i] - u_z[i];
        float rd = w_sqrt(rx * rx + rz * rz);
        if (rd < u_res_r[i] + 6.0f || u_stuck[i] > 2.0f) { u_pstate[i] = PS_G_NEAR; u_stuck[i] = 0.0f; return; }
        int f = field_valid(g_gj_f[j], g_gj_fage[j]) ? g_gj_f[j] : -1;
        int r = unit_walk_field(i, f, 1.5f, dt);
        if (r < 0) u_state[i] = US_IDLE;
        if (r == 1) { u_pstate[i] = PS_G_NEAR; u_stuck[i] = 0.0f; }
        return;
    }
    if (ps == PS_G_NEAR) {
        if (res_remaining(u_res_id[i]) <= 0) {
            int q = res_find_near(u_x[i], u_z[i], 14.0f, g_gj_type[j]);
            if (q < 0) {
                if (gather_job_relocate(j)) { q = res_find_near(g_gj_x[j], g_gj_z[j], 4.0f, g_gj_type[j]); }
                if (q < 0) { if (u_carry[i] > 0) u_pstate[i] = PS_G_DROP; else peasant_to_manual(i); return; }
                peasant_set_res(i, q);
                u_pstate[i] = PS_G_GO;
                return;
            }
            peasant_set_res(i, q);
        }
        float tx = u_res_x[i] - u_x[i], tz = u_res_z[i] - u_z[i];
        float d = w_sqrt(tx * tx + tz * tz);
        float reach = boat ? u_res_r[i] - 0.6f : u_res_r[i] + 0.95f;
        if (d < reach || (u_stuck[i] > 1.2f && d < u_res_r[i] + 3.0f)) {
            u_pstate[i] = PS_G_WORK; u_work_t[i] = 0.0f; u_state[i] = US_IDLE; u_stuck[i] = 0.0f;
            return;
        }
        if (u_stuck[i] > 2.5f) {
            /* can't get at this one: try another of the patch */
            int q = res_find_near(u_x[i], u_z[i], 10.0f, g_gj_type[j]);
            if (q >= 0 && g_slot_res_id[q] != u_res_id[i]) peasant_set_res(i, q);
            else u_pstate[i] = PS_G_GO;
            u_stuck[i] = 0.0f;
            return;
        }
        u_state[i] = US_MOVE;
        unit_face(i, tx, tz, dt);
        float step = u_speed[i] * (boat ? 0.6f : 1.0f) * dt;
        float ox = u_x[i], oz = u_z[i];
        unit_walk(i, tx / d * step, tz / d * step, 0);
        float mx = u_x[i] - ox, mz = u_z[i] - oz;
        if (mx * mx + mz * mz < 1e-6f) { float ns = u_stuck[i] + dt; u_stuck[i] = ns; } else u_stuck[i] = 0.0f;
        return;
    }
    if (ps == PS_G_WORK) {
        u_state[i] = US_IDLE;
        unit_face(i, u_res_x[i] - u_x[i], u_res_z[i] - u_z[i], dt);
        float prev = u_work_t[i];
        float nt = prev + dt;
        u_work_t[i] = nt;
        float period = boat ? (has_tech(u_own[i], T_NETS) ? 0.65f : 0.9f) : (has_tech(u_own[i], T_TOOLS) ? 0.85f : 1.1f);
        if (w_ifloor(nt / period) > w_ifloor(prev / period)) {
            int got = res_take(u_res_id[i], u_res_x[i], u_res_z[i], 1);
            if (got > 0) {
                if (u_carry[i] > 0 && u_carry_t[i] != g_gj_type[j]) u_carry[i] = 0;
                u_carry[i] = u_carry[i] + got;
                u_carry_t[i] = g_gj_type[j];
            }
            if (u_carry[i] >= carry_max(i)) { u_pstate[i] = PS_G_DROP; u_stuck[i] = 0.0f; return; }
            if (got == 0) { u_pstate[i] = PS_G_NEAR; u_stuck[i] = 0.0f; return; }   /* used up: next one */
        }
        return;
    }
    /* PS_G_DROP */
    float dx, dz;
    if (jobOk) { dx = g_gj_dx[j]; dz = g_gj_dz[j]; }
    else if (boat) { if (!nearest_dock_drop(u_own[i], u_x[i], u_z[i], &dx, &dz)) { peasant_to_manual(i); return; } }
    else if (u_own[i] < 0 || nearest_drop_building(u_own[i], u_x[i], u_z[i], &dx, &dz) < 0) { peasant_to_manual(i); return; }
    float ex = dx - u_x[i], ez = dz - u_z[i];
    float nearR = boat ? 3.5f : 2.6f;
    int r = 0;
    if (ex * ex + ez * ez < nearR * nearR) r = 1;
    else {
        r = unit_walk_field(i, gather_drop_field(j), 2.2f, dt);
        if (r < 0) {
            /* no field yet (or lost it): head straight there */
            float d = w_sqrt(ex * ex + ez * ez);
            u_state[i] = US_MOVE;
            unit_face(i, ex, ez, dt);
            float ox = u_x[i], oz = u_z[i];
            unit_walk(i, ex / d * u_speed[i] * dt, ez / d * u_speed[i] * dt, -1);
            if ((u_x[i] - ox) * (u_x[i] - ox) + (u_z[i] - oz) * (u_z[i] - oz) < 1e-6f) { float ns = u_stuck[i] + dt; u_stuck[i] = ns; }
            r = 0;
        }
        if (u_stuck[i] > 3.0f && ex * ex + ez * ez < 6.0f * 6.0f) r = 1;
    }
    if (r == 1) {
        /* the player's loads go into the stockpile; AI castles' income
         * is counted at a steady rate instead (castles_ai.h), so the
         * loads their peasants carry when on screen are just for show */
        if (u_carry[i] > 0 && u_own[i] == 0) {
            if (u_carry_t[i] == RES_WOOD) f_wood[0] = f_wood[0] + u_carry[i];
            else if (u_carry_t[i] == RES_STONE) f_stone[0] = f_stone[0] + u_carry[i];
            else f_food[0] = f_food[0] + u_carry[i];
            __android_log_print(4, "castles", "%s %d delivered %d %s (wood %d, stone %d, food %d)", boat ? "boat" : "peasant", i, u_carry[i],
                                u_carry_t[i] == RES_WOOD ? "wood" : (u_carry_t[i] == RES_STONE ? "stone" : "fish"), f_wood[0], f_stone[0], f_food[0]);
        }
        u_carry[i] = 0;
        u_stuck[i] = 0.0f;
        if (jobOk) u_pstate[i] = PS_G_GO; else peasant_to_manual(i);
    }
}

/* ---- construction ---- */
/* Peasants build what the player places: houses, barracks and docks
 * (which join g_hb_* and start working when finished) and castle walls,
 * towers and gatehouses. A site's cost is paid when it is placed; its
 * builders' work (seconds) completes it, shared between them. Finished
 * structures go on the generator's list (castles_structures.h) and the
 * chunks under them are regenerated with the real meshes + navigation;
 * until then the site is drawn as foundations and scaffolding. */
#define BS_MAX 48
static int g_bs_n;
static int g_bs_kind[BS_MAX], g_bs_done[BS_MAX], g_bs_hb[BS_MAX], g_bs_own[BS_MAX];
static float g_bs_x[BS_MAX], g_bs_z[BS_MAX], g_bs_x2[BS_MAX], g_bs_z2[BS_MAX], g_bs_y[BS_MAX], g_bs_lo[BS_MAX];
static float g_bs_yaw[BS_MAX], g_bs_work[BS_MAX], g_bs_need[BS_MAX], g_bs_fade[BS_MAX];

/* Cost of a structure: out[0] wood, out[1] stone (walls: by length). */
static void struct_cost(int kind, float len, int *out) {
    out[0] = 0; out[1] = 0; out[2] = 0;
    if (kind == ST_HOUSE) out[0] = 30;
    else if (kind == ST_BARRACKS) { out[0] = 60; out[1] = 40; }
    else if (kind == ST_DOCK) out[0] = 50;
    else if (kind == ST_WALL) out[1] = 2 * (int)(len + 0.5f);
    else if (kind == ST_TOWER) { out[0] = 10; out[1] = 40; }
    else if (kind == ST_GATE) { out[0] = 20; out[1] = 50; }
    else if (kind == ST_SMITH) { out[0] = 50; out[1] = 40; }
    else if (kind == ST_WORKSHOP) { out[0] = 100; out[1] = 60; }
    else if (kind == ST_STOCKPILE) out[0] = 30;
}

/* Builder-seconds of work a structure takes. */
static float struct_work(int kind, float len) {
    if (kind == ST_HOUSE) return 20.0f;
    if (kind == ST_BARRACKS) return 45.0f;
    if (kind == ST_DOCK) return 30.0f;
    if (kind == ST_WALL) return 4.0f + 1.6f * len;
    if (kind == ST_TOWER) return 30.0f;
    if (kind == ST_SMITH) return 30.0f;
    if (kind == ST_WORKSHOP) return 40.0f;
    if (kind == ST_STOCKPILE) return 12.0f;
    return 40.0f;
}

/* Rectangle footprint of a structure in its m_xform-style frame (origin
 * x,z; angle *a): local x in [r0,r2], z in [r1,r3]. 0 for round / wall
 * footprints. `yard`: include a barracks' training yard. */
static int struct_rect(int kind, float yaw, int yard, float *a, float *r) {
    if (kind == ST_HOUSE) { *a = yaw - 0.5f * W_PI; r[0] = -PS_HOUSE_HX; r[1] = -PS_HOUSE_HZ; r[2] = PS_HOUSE_HX; r[3] = PS_HOUSE_HZ; return 1; }
    if (kind == ST_BARRACKS) { *a = yaw - 0.5f * W_PI; r[0] = -PS_BARR_HX; r[1] = -PS_BARR_HZ; r[2] = PS_BARR_HX; r[3] = PS_BARR_HZ + (yard ? 3.8f : 0.0f); return 1; }
    if (kind == ST_DOCK) { *a = yaw - 0.5f * W_PI; r[0] = -0.9f; r[1] = -2.4f; r[2] = 2.9f; r[3] = DOCK_PIER; return 1; }
    if (kind == ST_SMITH) { *a = yaw - 0.5f * W_PI; r[0] = -PS_SMITH_HX; r[1] = -PS_SMITH_HZ; r[2] = PS_SMITH_HX; r[3] = PS_SMITH_HZ + (yard ? 2.2f : 0.0f); return 1; }
    if (kind == ST_STOCKPILE) { *a = yaw - 0.5f * W_PI; r[0] = -PS_SP_HX; r[1] = -PS_SP_HZ; r[2] = PS_SP_HX; r[3] = PS_SP_HZ + (yard ? 1.0f : 0.0f); return 1; }
    if (kind == ST_WORKSHOP) { *a = yaw - 0.5f * W_PI; r[0] = -PS_WS_HX; r[1] = -PS_WS_HZ; r[2] = PS_WS_HX; r[3] = PS_WS_HZ + (yard ? 1.6f : 0.0f); return 1; }
    if (kind == ST_GATE) {
        float hw = PS_GATE_HALF + 2.0f * PS_GATE_TR;
        *a = yaw + 0.5f * W_PI; r[0] = -hw; r[1] = -PS_GATE_TR; r[2] = hw; r[3] = PS_GATE_TR; return 1;
    }
    return 0;
}

/* Distance from (px,pz) to a structure's footprint (<= 0 inside);
 * g = {x, z, x2, z2, yaw} (squash: at most six float parameters). */
static float struct_dist(int kind, const float *g, float px, float pz) {
    float x = g[0], z = g[1], x2 = g[2], z2 = g[3], yaw = g[4];
    float a, r[4];
    if (kind == ST_WALL) return st_seg_dist(px, pz, x, z, x2, z2) - (PS_WALL_T * 0.5f + 0.25f);
    if (kind == ST_TOWER) { float dx = px - x, dz = pz - z; return w_sqrt(dx * dx + dz * dz) - PS_TOWER_R; }
    struct_rect(kind, yaw, 0, &a, r);
    float c = w_cos(a), sn = w_sin(a), dx = px - x, dz = pz - z;
    float lx = dx * c + dz * sn, lz = -dx * sn + dz * c;
    float ox = lx < r[0] ? r[0] - lx : (lx > r[2] ? lx - r[2] : 0.0f);
    float oz = lz < r[1] ? r[1] - lz : (lz > r[3] ? lz - r[3] : 0.0f);
    if (ox > 0.0f || oz > 0.0f) return w_sqrt(ox * ox + oz * oz);
    float in = lx - r[0];
    if (r[2] - lx < in) in = r[2] - lx;
    if (lz - r[1] < in) in = lz - r[1];
    if (r[3] - lz < in) in = r[3] - lz;
    return -in;
}

/* Distance from (px,pz) to build site s2's footprint. */
static float site_dist(int s2, float px, float pz) {
    float g[5];
    g[0] = g_bs_x[s2]; g[1] = g_bs_z[s2]; g[2] = g_bs_x2[s2]; g[3] = g_bs_z2[s2]; g[4] = g_bs_yaw[s2];
    return struct_dist(g_bs_kind[s2], g, px, pz);
}

/* Where builders head for: the centre (walls: the midpoint). */
static void struct_goal(int s2, float *gx, float *gz) {
    if (g_bs_kind[s2] == ST_WALL) { *gx = (g_bs_x[s2] + g_bs_x2[s2]) * 0.5f; *gz = (g_bs_z[s2] + g_bs_z2[s2]) * 0.5f; return; }
    float a, r[4];
    if (struct_rect(g_bs_kind[s2], g_bs_yaw[s2], 0, &a, r)) {
        float cx = (r[0] + r[2]) * 0.5f, cz = (r[1] + r[3]) * 0.5f;
        *gx = g_bs_x[s2] + cx * w_cos(a) - cz * w_sin(a);
        *gz = g_bs_z[s2] + cx * w_sin(a) + cz * w_cos(a);
        return;
    }
    *gx = g_bs_x[s2]; *gz = g_bs_z[s2];
}

/* Sends peasant i to build site s2 (on the order fields). */
static void builder_assign(int i, int s2) {
    float gx, gz;
    struct_goal(s2, &gx, &gz);
    unit_release_field(i);
    u_pstate[i] = PS_B_GO;
    u_bsite[i] = s2;
    u_job[i] = -1;
    u_gx[i] = gx; u_gy[i] = g_bs_y[s2]; u_gz[i] = gz; u_gl[i] = 0;
    u_order[i] = ORD_MOVE; u_target[i] = -1;
    u_repath[i] = 1;
    u_state[i] = US_WAIT;
    u_stuck[i] = 0.0f;
}

/* Nearest unfinished site within 40 of (x,z), or -1. */
static int build_site_near(float x, float z) {
    int k, best = -1;
    float bd = 40.0f * 40.0f;
    for (k = 0; k < g_bs_n; k++) {
        if (g_bs_done[k] || g_bs_own[k] != 0) continue;
        float gx, gz;
        struct_goal(k, &gx, &gz);
        float dx = gx - x, dz = gz - z;
        if (dx * dx + dz * dz < bd) { bd = dx * dx + dz * dz; best = k; }
    }
    return best;
}

static void el_register_ps(int own, int p);
static void build_complete(int s2) {
    int k = g_bs_kind[s2], i, p = g_nps;
    g_bs_done[s2] = 1;
    g_bs_fade[s2] = 2.5f;          /* scaffold stays up while the chunks rebuild */
    if (p < MAXPS) {
        g_ps_kind[p] = k;
        g_ps_team[p] = g_bs_own[s2] == 0 ? 0 : 1;
        g_ps_x[p] = g_bs_x[s2]; g_ps_z[p] = g_bs_z[s2]; g_ps_x2[p] = g_bs_x2[s2]; g_ps_z2[p] = g_bs_z2[s2];
        g_ps_y[p] = g_bs_y[s2]; g_ps_lo[p] = g_bs_lo[s2]; g_ps_yaw[p] = g_bs_yaw[s2];
        g_nps = p + 1;                         /* last: the generator reads up to g_nps */
        float bb[4];
        st_bounds(k, g_bs_x[s2], g_bs_z[s2], g_bs_x2[s2], g_bs_z2[s2], bb);
        int cx0 = w_ifloor(bb[0] / W_CHUNK_SIZE), cx1 = w_ifloor(bb[2] / W_CHUNK_SIZE);
        int cz0 = w_ifloor(bb[1] / W_CHUNK_SIZE), cz1 = w_ifloor(bb[3] / W_CHUNK_SIZE), cx, cz;
        for (cz = cz0; cz <= cz1; cz++)
            for (cx = cx0; cx <= cx1; cx++) request_rebuild_at(((float)cx + 0.5f) * W_CHUNK_SIZE, ((float)cz + 0.5f) * W_CHUNK_SIZE);
    }
    int b = g_bs_hb[s2];
    if (p < MAXPS && p < g_nps) {
        if (b >= 0 && b < g_hb_n) g_hb_ps[b] = p;
        el_register_ps(g_bs_own[s2], p);
    }
    if (b >= 0 && b < g_hb_n) {
        g_hb_done[b] = 1;
        if (g_hb_t[b] == BT_HOUSE && g_hb_own[b] >= 0) f_houses[g_hb_own[b]] = f_houses[g_hb_own[b]] + 1;
    }
    __android_log_print(4, "castles", "faction %d finished building: %s at %d,%d (pop limit %d)", g_bs_own[s2],
                        k == ST_HOUSE ? "house" : (k == ST_BARRACKS ? "barracks" : (k == ST_DOCK ? "dock" : (k == ST_WALL ? "wall" : (k == ST_TOWER ? "tower" : (k == ST_SMITH ? "blacksmith" : (k == ST_WORKSHOP ? "siege workshop" : (k == ST_STOCKPILE ? "stockpile" : "gatehouse"))))))),
                        (int)g_bs_x[s2], (int)g_bs_z[s2], pop_limit_of(g_bs_own[s2]));
    /* builders move on to the next site nearby, or wait (AI builders
     * are reassigned by their castle's AI) */
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_class[i] != UC_PEASANT || (u_pstate[i] != PS_B_GO && u_pstate[i] != PS_B_WORK) || u_bsite[i] != s2) continue;
        if (u_own[i] != 0) { unit_release_field(i); u_order[i] = ORD_NONE; peasant_to_manual(i); continue; }
        int nx = build_site_near(u_x[i], u_z[i]);
        if (nx >= 0) builder_assign(i, nx); else { unit_release_field(i); u_order[i] = ORD_NONE; peasant_to_manual(i); }
    }
}

/* PS_B_GO check, every frame: close enough to the footprint to work? */
static void builder_go(int i, float dt) {
    int s2 = u_bsite[i];
    if (u_own[i] != 0 && (s2 < 0 || s2 >= g_bs_n || g_bs_done[s2])) { unit_release_field(i); u_order[i] = ORD_NONE; peasant_to_manual(i); return; }
    if (s2 < 0 || s2 >= g_bs_n || g_bs_done[s2]) {
        int nx = build_site_near(u_x[i], u_z[i]);
        if (nx >= 0) builder_assign(i, nx); else { unit_release_field(i); u_order[i] = ORD_NONE; peasant_to_manual(i); }
        return;
    }
    float d = site_dist(s2, u_x[i], u_z[i]);
    if (d < 1.3f) {
        unit_release_field(i);
        u_order[i] = ORD_NONE; u_repath[i] = 0;
        u_pstate[i] = PS_B_WORK; u_state[i] = US_IDLE; u_work_t[i] = 0.0f;
        return;
    }
    if (u_state[i] == US_IDLE) {
        /* the field ended short of the footprint: walk straight in */
        float gx, gz;
        struct_goal(s2, &gx, &gz);
        float tx = gx - u_x[i], tz = gz - u_z[i];
        float dd = w_sqrt(tx * tx + tz * tz);
        if (dd < 0.05f) return;
        unit_face(i, tx, tz, dt);
        float ox = u_x[i], oz = u_z[i];
        unit_walk(i, tx / dd * u_speed[i] * dt, tz / dd * u_speed[i] * dt, 0);
        if ((u_x[i] - ox) * (u_x[i] - ox) + (u_z[i] - oz) * (u_z[i] - oz) < 1e-6f) { float ns = u_stuck[i] + dt; u_stuck[i] = ns; }
        if (u_stuck[i] > 2.0f) {
            if (d < 4.0f) { u_pstate[i] = PS_B_WORK; u_work_t[i] = 0.0f; }
            else peasant_to_manual(i);
        }
    }
}

static void builder_work(int i, float dt) {
    int s2 = u_bsite[i];
    u_state[i] = US_IDLE;
    if (s2 < 0 || s2 >= g_bs_n || g_bs_done[s2]) { builder_go(i, dt); return; }
    float gx, gz;
    struct_goal(s2, &gx, &gz);
    unit_face(i, gx - u_x[i], gz - u_z[i], dt);
    float nt = u_work_t[i] + dt;
    u_work_t[i] = nt;
    if (g_bs_own[s2] != 0) return;          /* AI sites progress at their castle's build rate */
    float nw = g_bs_work[s2] + dt;
    g_bs_work[s2] = nw;
    if (nw >= g_bs_need[s2]) build_complete(s2);
}

/* ---- placement (the player choosing where to build) ---- */
static int g_pl_valid;
static float g_pl_x, g_pl_z, g_pl_x2, g_pl_z2, g_pl_y, g_pl_lo, g_pl_yaw;
static const char *g_pl_why;

/* Snaps (x,z) to a tower / gatehouse centre or wall end within 3;
 * returns the site snapped to (its base height joins the wall-walks
 * up), or -1. */
static int place_snap(float *x, float *z) {
    int k;
    for (k = 0; k < g_bs_n; k++) {
        int kk = g_bs_kind[k];
        if (kk != ST_TOWER && kk != ST_WALL && kk != ST_GATE) continue;
        float dx = g_bs_x[k] - *x, dz = g_bs_z[k] - *z;
        if (dx * dx + dz * dz < 9.0f) { *x = g_bs_x[k]; *z = g_bs_z[k]; return k; }
        if (kk == ST_WALL) {
            dx = g_bs_x2[k] - *x; dz = g_bs_z2[k] - *z;
            if (dx * dx + dz * dz < 9.0f) { *x = g_bs_x2[k]; *z = g_bs_z2[k]; return k; }
        }
    }
    return -1;
}

/* Is there a tower / gatehouse / wall end at (x,z) (a wall may run into it)? */
static int place_joint(float x, float z) {
    int k;
    for (k = 0; k < g_bs_n; k++) {
        int kk = g_bs_kind[k];
        if (kk == ST_TOWER || kk == ST_GATE) { if (w_absf(g_bs_x[k] - x) < 0.01f && w_absf(g_bs_z[k] - z) < 0.01f) return 1; }
        if (kk == ST_WALL) {
            if (w_absf(g_bs_x[k] - x) < 0.01f && w_absf(g_bs_z[k] - z) < 0.01f) return 1;
            if (w_absf(g_bs_x2[k] - x) < 0.01f && w_absf(g_bs_z2[k] - z) < 0.01f) return 1;
        }
    }
    return 0;
}

/* Checks a placement of `kind` at (ax,az) (walls: to bx,bz) and fills
 * g_pl_*: derived position / height / orientation and whether it can be
 * built there (g_pl_why says why not). */
static void place_check(int kind, float ax, float az, float bx, float bz) {
    g_pl_valid = 0; g_pl_why = "";
    g_pl_x = ax; g_pl_z = az; g_pl_x2 = bx; g_pl_z2 = bz; g_pl_yaw = 0.0f;
    float hcx = g_hsite.cx, hcz = g_hsite.cz;
    int snapA = -1, snapB = -1;
    if (kind == ST_WALL) {
        snapA = place_snap(&g_pl_x, &g_pl_z); snapB = place_snap(&g_pl_x2, &g_pl_z2);
        float ex = g_pl_x2 - g_pl_x, ez = g_pl_z2 - g_pl_z;
        float len = w_sqrt(ex * ex + ez * ez);
        if (len > 40.0f) { g_pl_x2 = g_pl_x + ex / len * 40.0f; g_pl_z2 = g_pl_z + ez / len * 40.0f; }
        if (len < 3.0f) { g_pl_why = "wall too short"; return; }
    } else if (kind == ST_HOUSE || kind == ST_BARRACKS || kind == ST_SMITH || kind == ST_WORKSHOP || kind == ST_STOCKPILE) {
        g_pl_yaw = approx_atan2_w(g_cam[2] - az, g_cam[0] - ax);   /* front toward the viewer */
    } else if (kind == ST_GATE) {
        float dx = hcx - ax, dz = hcz - az;
        g_pl_yaw = (dx * dx + dz * dz > 1.0f) ? approx_atan2_w(dz, dx) : 0.0f;   /* passage toward the castle */
        snapA = place_snap(&g_pl_x, &g_pl_z);
    } else if (kind == ST_TOWER) {
        snapA = place_snap(&g_pl_x, &g_pl_z);
    } else if (kind == ST_DOCK) {
        /* face the open water: the direction (of 24) whose pier line is
         * water furthest in, from a dry anchor */
        float h;
        int k, best = -1, bestScore = 0;
        if (!(nav_node(0, w_ifloor(ax), w_ifloor(az), &h) & NAV_WALK)) { g_pl_why = "dock: tap the shore"; return; }
        for (k = 0; k < 24; k++) {
            float a = (float)k * (2.0f * W_PI / 24.0f), dx = w_cos(a), dz = w_sin(a);
            int score = 0, m;
            for (m = 0; m < 4; m++) {
                float t = 2.4f + 1.2f * (float)m;
                if (nav_node(0, w_ifloor(ax + dx * t), w_ifloor(az + dz * t), &h) & NAV_WATER) score++;
            }
            /* boats must be able to reach the pier end */
            if (!(nav_node(0, w_ifloor(ax + dx * DOCK_PIER + dx * 1.5f), w_ifloor(az + dz * DOCK_PIER + dz * 1.5f), &h) & NAV_WATER)) score = 0;
            if (score > bestScore) { bestScore = score; best = k; }
        }
        if (best < 0 || bestScore < 2) { g_pl_why = "dock: needs deep water close to the shore"; return; }
        g_pl_yaw = (float)best * (2.0f * W_PI / 24.0f);
    }
    /* sample the footprint */
    float bb[4];
    st_bounds(kind, g_pl_x, g_pl_z, g_pl_x2, g_pl_z2, bb);
    int i0 = w_ifloor(bb[0]), i1 = w_ifloor(bb[2]), j0 = w_ifloor(bb[1]), j1 = w_ifloor(bb[3]), i, j, k;
    float lo = 1e9f, hi = -1e9f, ya = 0.0f;
    int cells = 0;
    float da, dr[4];
    int rect = struct_rect(kind, g_pl_yaw, 1, &da, dr);
    float dc = w_cos(da), ds = w_sin(da);
    int jointA = kind == ST_WALL && place_joint(g_pl_x, g_pl_z), jointB = kind == ST_WALL && place_joint(g_pl_x2, g_pl_z2);
    for (j = j0; j <= j1; j++) {
        for (i = i0; i <= i1; i++) {
            float px = (float)i + 0.5f, pz = (float)j + 0.5f;
            int inside;
            if (rect) {
                float lx = (px - g_pl_x) * dc + (pz - g_pl_z) * ds, lz = -(px - g_pl_x) * ds + (pz - g_pl_z) * dc;
                inside = lx >= dr[0] - 0.3f && lx <= dr[2] + 0.3f && lz >= dr[1] - 0.3f && lz <= dr[3] + 0.3f;
                if (kind == ST_DOCK && lz > 0.2f) continue;          /* the pier: over water */
            } else {
                float gg[5];
                gg[0] = g_pl_x; gg[1] = g_pl_z; gg[2] = g_pl_x2; gg[3] = g_pl_z2; gg[4] = g_pl_yaw;
                inside = struct_dist(kind, gg, px, pz) <= 0.3f;
            }
            if (!inside) continue;
            if (jointA && (px - g_pl_x) * (px - g_pl_x) + (pz - g_pl_z) * (pz - g_pl_z) < 2.8f * 2.8f) continue;
            if (jointB && (px - g_pl_x2) * (px - g_pl_x2) + (pz - g_pl_z2) * (pz - g_pl_z2) < 2.8f * 2.8f) continue;
            if (snapA >= 0 && kind != ST_WALL) {
                /* a tower / gatehouse on a wall's end stands over the wall */
                int onWall = 0;
                for (k = 0; k < g_bs_n; k++) if (g_bs_kind[k] == ST_WALL && site_dist(k, px, pz) < 0.6f) onWall = 1;
                if (onWall) continue;
            }
            float h;
            int f = nav_node(0, i, j, &h);
            if (!(f & NAV_WALK) || (f & NAV_LADDER)) { g_pl_why = "blocked (water, trees, rocks or buildings)"; return; }
            if (f & NAV_TERRAIN) h = nav_terrain_height(px, pz);
            if (h < lo) lo = h;
            if (h > hi) hi = h;
            float na = ya + h;
            ya = na;
            cells++;
            for (k = 0; k < g_bs_n; k++) {
                if (g_bs_done[k] && g_bs_fade[k] <= 0.0f) continue;
                if (site_dist(k, px, pz) < 0.4f) {
                    if ((jointA || jointB) && kind == ST_WALL && g_bs_kind[k] != ST_WALL) continue;
                    if (snapA >= 0 && kind != ST_WALL && g_bs_kind[k] == ST_WALL) continue;
                    g_pl_why = "overlaps another construction"; return;
                }
            }
        }
    }
    if (cells == 0) { g_pl_why = "not on loaded ground"; return; }
    if (hi - lo > (kind == ST_WALL ? 3.2f : 2.2f)) { g_pl_why = "ground too steep"; return; }
    g_pl_lo = lo;
    g_pl_y = (kind == ST_DOCK) ? nav_terrain_height(g_pl_x, g_pl_z) : hi;
    /* joined to a tower / gatehouse / wall: share the higher base so the
     * wall-walks meet at the same height */
    if (snapA >= 0 && g_bs_y[snapA] > g_pl_y) g_pl_y = g_bs_y[snapA];
    if (snapB >= 0 && g_bs_y[snapB] > g_pl_y) g_pl_y = g_bs_y[snapB];
    (void)ya;
    g_pl_valid = 1;
}

/* Creates a construction site for faction own from the g_pl_*
 * geometry, paying for it. Returns the site, -1 (full) or -2 (can't
 * afford). */
static int build_site_create(int own, int kind) {
    int i;
    if (g_bs_n >= BS_MAX) return -1;
    float ex = g_pl_x2 - g_pl_x, ez = g_pl_z2 - g_pl_z;
    float len = w_sqrt(ex * ex + ez * ez);
    int cost[3];
    struct_cost(kind, len, cost);
    if (!can_afford(own, cost)) return -2;
    int hb = -1;
    g_add_own = own;
    if (kind == ST_HOUSE) hb = home_add_building(BT_HOUSE, g_pl_x, g_pl_z, g_pl_y, g_pl_yaw, PS_HOUSE_HX, PS_HOUSE_HZ, 0);
    else if (kind == ST_BARRACKS) hb = home_add_building(BT_BARRACKS, g_pl_x, g_pl_z, g_pl_y, g_pl_yaw, PS_BARR_HX, PS_BARR_HZ, 0);
    else if (kind == ST_SMITH) hb = home_add_building(BT_SMITH, g_pl_x, g_pl_z, g_pl_y, g_pl_yaw, PS_SMITH_HX, PS_SMITH_HZ, 0);
    else if (kind == ST_WORKSHOP) hb = home_add_building(BT_WORKSHOP, g_pl_x, g_pl_z, g_pl_y, g_pl_yaw, PS_WS_HX, PS_WS_HZ, 0);
    else if (kind == ST_STOCKPILE) hb = home_add_building(BT_STOCKPILE, g_pl_x, g_pl_z, g_pl_y, g_pl_yaw, PS_SP_HX, PS_SP_HZ, 0);
    else if (kind == ST_DOCK) hb = home_add_dock(g_pl_x, g_pl_z, g_pl_y, g_pl_yaw, 0);
    g_add_own = 0;
    if ((kind == ST_HOUSE || kind == ST_BARRACKS || kind == ST_DOCK || kind == ST_SMITH || kind == ST_WORKSHOP || kind == ST_STOCKPILE) && hb < 0) return -1;
    pay(own, cost);
    int s2 = g_bs_n;
    g_bs_kind[s2] = kind; g_bs_done[s2] = 0; g_bs_hb[s2] = hb; g_bs_own[s2] = own;
    g_bs_x[s2] = g_pl_x; g_bs_z[s2] = g_pl_z; g_bs_x2[s2] = g_pl_x2; g_bs_z2[s2] = g_pl_z2;
    g_bs_y[s2] = g_pl_y; g_bs_lo[s2] = g_pl_lo; g_bs_yaw[s2] = g_pl_yaw;
    g_bs_work[s2] = 0.0f; g_bs_need[s2] = struct_work(kind, len); g_bs_fade[s2] = 0.0f;
    g_bs_n = s2 + 1;
    /* unfinished pieces it joins rise to the same base (walls / towers /
     * gatehouses meet at one wall-walk height) */
    if (kind == ST_WALL || kind == ST_TOWER || kind == ST_GATE) {
        int k;
        for (k = 0; k < s2; k++) {
            int kk = g_bs_kind[k];
            if (g_bs_done[k] || (kk != ST_WALL && kk != ST_TOWER && kk != ST_GATE) || g_bs_y[k] >= g_pl_y) continue;
            int touch = 0;
            if (w_absf(g_bs_x[k] - g_pl_x) < 0.01f && w_absf(g_bs_z[k] - g_pl_z) < 0.01f) touch = 1;
            if (kind == ST_WALL && w_absf(g_bs_x[k] - g_pl_x2) < 0.01f && w_absf(g_bs_z[k] - g_pl_z2) < 0.01f) touch = 1;
            if (kk == ST_WALL && w_absf(g_bs_x2[k] - g_pl_x) < 0.01f && w_absf(g_bs_z2[k] - g_pl_z) < 0.01f) touch = 1;
            if (kk == ST_WALL && kind == ST_WALL && w_absf(g_bs_x2[k] - g_pl_x2) < 0.01f && w_absf(g_bs_z2[k] - g_pl_z2) < 0.01f) touch = 1;
            if (touch) g_bs_y[k] = g_pl_y;
        }
    }
    __android_log_print(4, "castles", "faction %d started building kind %d at %d,%d (cost %d wood %d stone; left %d/%d/%d)",
                        own, kind, (int)g_pl_x, (int)g_pl_z, cost[0], cost[1], f_wood[own], f_stone[own], f_food[own]);
    (void)i;
    return s2;
}

/* Starts construction of the checked placement with the selected
 * peasants. Returns 1, 0 (not valid / no peasants / full) or -1 (can't
 * afford). */
static int build_start(int kind) {
    int i, n = 0;
    if (!g_pl_valid) return 0;
    for (i = 0; i < g_un; i++) if (u_selected[i] && unit_alive(i) && u_own[i] == 0 && u_class[i] == UC_PEASANT) n++;
    if (n == 0) return 0;
    int s2 = build_site_create(0, kind);
    if (s2 == -2) return -1;
    if (s2 < 0) return 0;
    for (i = 0; i < g_un; i++) {
        if (!u_selected[i] || !unit_alive(i) || u_own[i] != 0 || u_class[i] != UC_PEASANT) continue;
        builder_assign(i, s2);
    }
    dispatch_repaths();
    return 1;
}

/* Peasant routine: idle at home, walk to the town centre on the shared
 * field (recording a breadcrumb trail), idle on the plaza, then walk the
 * trail back home. */
static void peasant_update(int i, float dt) {
    if (u_pstate[i] >= PS_G_GO && u_pstate[i] <= PS_G_DROP) { peasant_gather_update(i, dt); return; }
    if (u_pstate[i] == PS_B_WORK) { builder_work(i, dt); return; }
    float nt = u_ptimer[i] - dt;
    u_ptimer[i] = nt;
    int ps = u_pstate[i];
    if (ps == PS_HOME || ps == PS_AT_TC) {
        u_state[i] = US_IDLE;
        if (u_ptimer[i] > 0.0f) return;
        if (ps == PS_HOME) {
            if (g_tc_field < 0 || g_field_state[g_tc_field] != FS_READY) { u_ptimer[i] = 2.0f; return; }
            u_pstate[i] = PS_TO_TC;
            u_trail_n[i] = 0; u_trail_d[i] = 0.0f;
            peasant_trail_push(i);
        } else {
            u_pstate[i] = PS_RETURN;
        }
        return;
    }
    if (ps == PS_TO_TC) {
        int f = g_tc_field;
        if (f < 0 || g_field_state[f] != FS_READY) return;
        int lx = w_ifloor(u_x[i]) - g_field_ox[f], lz = w_ifloor(u_z[i]) - g_field_oz[f];
        int bx = 0, bz = 0, bl = 0;
        float here = field_dist_at(f, u_x[i], u_z[i], u_layer[i]);
        if (here < 2.5f || lx < 0 || lz < 0 || lx >= NAVW || lz >= NAVW ||
            !field_best_step(f, lx, lz, u_layer[i], &bx, &bz, &bl) || bl != u_layer[i]) {
            u_pstate[i] = PS_AT_TC;
            u_ptimer[i] = 5.0f + (float)((i * 7) % 9);
            return;
        }
        float tx = (float)(bx + g_field_ox[f]) + 0.5f - u_x[i], tz = (float)(bz + g_field_oz[f]) + 0.5f - u_z[i];
        float d = w_sqrt(tx * tx + tz * tz);
        if (d < 0.01f) return;
        u_state[i] = US_MOVE;
        unit_face(i, tx, tz, dt);
        float step = u_speed[i] * dt;
        if (step > d) step = d;
        float ox = u_x[i], oz = u_z[i];
        unit_walk(i, tx / d * step, tz / d * step, 0);
        float mx = u_x[i] - ox, mz = u_z[i] - oz;
        float nd = u_trail_d[i] + w_sqrt(mx * mx + mz * mz);
        u_trail_d[i] = nd;
        if (nd > 1.0f) { u_trail_d[i] = 0.0f; peasant_trail_push(i); }
        return;
    }
    /* PS_RETURN: walk the trail backwards */
    int n = u_trail_n[i];
    if (n <= 0) {
        u_pstate[i] = PS_HOME;
        u_ptimer[i] = 10.0f + (float)((i * 13) % 15);
        return;
    }
    float tx = u_trail_x[i * TRAIL_MAX + n - 1] - u_x[i], tz = u_trail_z[i * TRAIL_MAX + n - 1] - u_z[i];
    float d = w_sqrt(tx * tx + tz * tz);
    if (d < 0.35f) { u_trail_n[i] = n - 1; u_stuck[i] = 0.0f; return; }
    u_state[i] = US_MOVE;
    unit_face(i, tx, tz, dt);
    float step = u_speed[i] * dt;
    if (step > d) step = d;
    float ox = u_x[i], oz = u_z[i];
    unit_walk(i, tx / d * step, tz / d * step, 0);
    float mx = u_x[i] - ox, mz = u_z[i] - oz;
    if (mx * mx + mz * mz < 1e-6f) {
        float ns = u_stuck[i] + dt;
        u_stuck[i] = ns;
        if (ns > 1.5f) { u_trail_n[i] = n - 1; u_stuck[i] = 0.0f; }   /* skip a blocked crumb */
    }
}

/* Barracks / town centre: advance each queue; a finished unit appears
 * at the door, on the nearest walkable cell, ready for orders. */
static void barracks_update(float dt) {
    int b;
    for (b = 0; b < g_hb_n; b++) {
        if ((g_hb_t[b] != BT_BARRACKS && g_hb_t[b] != BT_TOWNCENTER && g_hb_t[b] != BT_DOCK && g_hb_t[b] != BT_WORKSHOP) || g_bq_n[b] == 0) continue;
        float nt = g_bq_t[b] + dt;
        g_bq_t[b] = nt;
        int cls = g_bq_cls[b * BQ_MAX], own = g_hb_own[b];
        float tt = bq_time(b);
        if (g_bq_t[b] < tt) continue;
        if (pop_count_of(own) >= pop_limit_of(own)) { g_bq_t[b] = tt; continue; }   /* wait for room */
        float x, z;
        building_door(b, 1.4f, &x, &z);
        /* where the land isn't loaded (an AI castle far away) the unit
         * simply appears at the door; it is placed properly once seen */
        int here = nav_slot_for(w_ifloor(x / W_CHUNK_SIZE), w_ifloor(z / W_CHUNK_SIZE)) >= 0;
        if (cls == UC_BOAT) { if (here && !snap_water(&x, &z)) { g_bq_t[b] = tt; continue; } }
        else if (here) snap_walkable(&x, &z, g_hb_y[b]);
        int u = unit_spawn(own == 0 ? TEAM_PLAYER : TEAM_ENEMY, x, z, 0, w_hash2(b, (int)(g_time * 10.0f), 9u), cls, own);
        if (u >= 0) {
            float fx, fz;
            building_door(b, 6.0f, &fx, &fz);
            u_yaw[u] = approx_atan2(fx - x, -(fz - z));
            if (cls == UC_PEASANT || cls == UC_BOAT) peasant_to_manual(u);
            if (own > 0) ai_unit_trained(own, u);
            if (own == 0)
                __android_log_print(4, "castles", "%s %d trained a %s at %d,%d (pop %d/%d)",
                                    g_hb_t[b] == BT_TOWNCENTER ? "town centre" : (g_hb_t[b] == BT_DOCK ? "dock" : (g_hb_t[b] == BT_WORKSHOP ? "workshop" : "barracks")), b,
                                    cls == UC_SWORD ? "swordsman" : (cls == UC_BOW ? "archer" : (cls == UC_MACE ? "maceman" : (cls == UC_BOAT ? "fishing boat" :
                                    (cls == UC_CATAPULT ? "catapult" : (cls == UC_RAM ? "ram" : (cls == UC_TREB ? "trebuchet" : "peasant")))))),
                                    (int)x, (int)z, pop_count(), pop_limit());
        }
        int k;
        for (k = 1; k < g_bq_n[b]; k++) g_bq_cls[b * BQ_MAX + k - 1] = g_bq_cls[b * BQ_MAX + k];
        g_bq_n[b] = g_bq_n[b] - 1;
        g_bq_t[b] = 0.0f;
    }
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
        if (!u_ld[j] || !unit_alive(j) || u_team[j] == u_team[i] || u_class[j] == UC_BOAT) continue;
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
    float speed = u_speed[i];
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
        float r = unit_range(i) * 0.8f;
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
static int a_team[MAXP], a_flying[MAXP], a_dmg[MAXP];
static int a_kind[MAXP];     /* 0 arrow, 1 catapult stone, 2 trebuchet stone */
static int a_sdmg[MAXP];     /* stones: damage to structures */
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
    out[1] = u_class[t] == UC_DRAGON ? u_y[t] : u_y[t] + 1.15f * u_scale[t];
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
    a_kind[a] = 0;
    a_dmg[a] = 1 + (int)u_dmgb[i];
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
static void siege_impact(int a, float x, float y, float z);
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
            if (nl > (a_kind[k] ? 12.0f : 6.0f)) { p_active[k] = 0; continue; }
            /* Units of the other team. */
            int hit = 0;
            float hr = a_kind[k] ? 0.9f : 0.42f;
            for (j = 0; j < g_un && !hit; j++) {
                if (!unit_alive(j) || u_team[j] == a_team[k]) continue;
                float dx = px - u_x[j], dz = pz - u_z[j];
                if (u_class[j] == UC_DRAGON) {           /* a big target, centred on its body */
                    if (dx * dx + dz * dz > 2.4f * 2.4f || py < u_y[j] - 1.4f || py > u_y[j] + 1.6f) continue;
                } else {
                if (dx * dx + dz * dz > hr * hr) continue;
                if (py < u_y[j] + 0.1f || py > u_y[j] + 1.9f * u_scale[j]) continue;
                }
                if (a_kind[k]) siege_impact(k, px, py, pz);
                else unit_damage(j, a_dmg[k]);
                g_arrows_hit = g_arrows_hit + 1;
                p_active[k] = 0;
                hit = 1;
            }
            if (hit) continue;
            float sy;
            int wh = arrow_world_hit(px, py, pz, &sy);
            if (wh && a_kind[k]) siege_impact(k, px, py, pz);          /* a stone smashes where it lands */
            if (wh == 1) arrow_stick(k, sy);
            else if (wh == 2) p_active[k] = 0;
        }
    }
    g_bal_acc = acc;
}

#include "android/examples/castles_siege.h"
#include "android/examples/castles_dragons.h"
#include "android/examples/castles_ai.h"

static void units_update(float dt) {
    int i, j;
    for (i = 0; i < g_un; i++) { u_moved[i] = 0.0f; u_ld[i] = u_active[i] && unit_loaded(i); }
    peasants_field_update(dt);
    peasants_spawn(dt);
    barracks_update(dt);
    research_update(dt);
    gather_jobs_update(dt);
    ai_update(dt);
    for (i = 0; i < g_bs_n; i++) if (g_bs_fade[i] > 0.0f) { float nf = g_bs_fade[i] - dt; g_bs_fade[i] = nf; }
    dragons_respawn(dt);
    for (i = 0; i < g_un; i++) {
        if (!u_active[i]) continue;
        if (u_class[i] == UC_DRAGON) { dragon_update(i, dt); continue; }      /* flies on or off screen */
        if (!u_ld[i]) {
            /* off screen: frozen, except attack waves on the march */
            if (u_amove[i] && u_state[i] != US_DEAD && u_state[i] != US_CLIMB) ai_travel(i, dt);
            continue;
        }
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
        if (is_siege(i)) { siege_update(i, dt); continue; }
        if (u_class[i] == UC_PEASANT && (u_pstate[i] == PS_T_GO || u_pstate[i] == PS_T_WORK)) { peasant_tame_update(i, dt); continue; }
        if (u_class[i] == UC_PEASANT || u_class[i] == UC_BOAT) {
            /* townsfolk and boats never fight: under orders they move
             * like soldiers, otherwise they follow their routine / job */
            if (u_pstate[i] == PS_MANUAL || u_pstate[i] == PS_B_GO) {
                if (u_state[i] == US_WAIT && u_field[i] >= 0 && g_field_state[u_field[i]] == FS_READY) u_state[i] = US_MOVE;
                if (u_state[i] == US_MOVE) unit_follow_field(i, dt);
                if (u_pstate[i] == PS_B_GO) builder_go(i, dt);
            } else {
                peasant_update(i, dt);
            }
            continue;
        }
        /* Targets: drop dead ones; acquire nearby enemies when free. */
        if (u_target[i] >= 0 && !unit_alive(u_target[i])) {
            u_target[i] = -1;
            if (u_order[i] == ORD_ATTACK) { u_order[i] = ORD_NONE; unit_release_field(i); u_state[i] = US_IDLE; }
        }
        if (u_amove[i]) {
            /* attack-move: a unit that marched in from off screen needs
             * a field; after a fight (or giving up a chase) it marches on */
            if (u_state[i] == US_MOVE && u_field[i] < 0 && !u_repath[i]) { u_repath[i] = 1; u_state[i] = US_WAIT; }
            if (u_target[i] < 0 && u_state[i] == US_IDLE) {
                float gdx = u_gx[i] - u_x[i], gdz = u_gz[i] - u_z[i];
                if (gdx * gdx + gdz * gdz > 6.0f * 6.0f) { u_order[i] = ORD_MOVE; u_repath[i] = 1; u_state[i] = US_WAIT; }
            }
        }
        int busy = (u_order[i] == ORD_MOVE) && (u_state[i] == US_MOVE || u_state[i] == US_WAIT) && !u_amove[i];
        if (u_target[i] < 0 && !busy) {
            float range = u_archer[i] ? unit_range(i) - 2.0f : (u_team[i] == TEAM_ENEMY ? 9.0f : 6.0f);
            int e = unit_find_enemy(i, range, u_archer[i] ? 14.0f : 2.5f);
            if (e >= 0) {
                u_target[i] = e; u_stuck[i] = 0.0f;
                if (u_state[i] == US_IDLE) u_state[i] = US_CHASE;
                else if (u_amove[i] && (u_state[i] == US_MOVE || u_state[i] == US_WAIT)) { unit_release_field(i); u_repath[i] = 0; u_state[i] = US_CHASE; }
            }
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
            else if (!pointBlank && ad <= unit_range(i) && w_absf(ay) < 14.0f) shooting = 1;
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
                    unit_walk(i, dx / d * u_speed[i] * 0.95f * dt, dz / d * u_speed[i] * 0.95f * dt, -1);
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
            float ns = prev + dt / u_atk[i];
            u_swing[i] = ns;
            if (prev < 0.5f && ns >= 0.5f && unit_alive(t)) {
                float dx = u_x[t] - u_x[i], dz = u_z[t] - u_z[i];
                if (dx * dx + dz * dz < 2.0f * 2.0f && w_absf(u_y[t] - u_y[i]) < 1.3f) unit_damage(t, 1 + (int)u_dmgb[i]);
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
        if (!u_ld[i] || !unit_alive(i) || u_state[i] == US_CLIMB || u_class[i] == UC_DRAGON) continue;
        int bi = u_class[i] == UC_BOAT;
        for (j = i + 1; j < g_un; j++) {
            if (!u_ld[j] || !unit_alive(j) || u_state[j] == US_CLIMB || u_class[j] == UC_DRAGON) continue;
            if ((u_class[j] == UC_BOAT) != bi) continue;            /* boats only jostle boats */
            float sep = bi ? 1.7f : ((is_siege(i) || is_siege(j)) ? 1.4f : 0.8f);
            float dx = u_x[j] - u_x[i], dz = u_z[j] - u_z[i];
            if (w_absf(dx) > sep || w_absf(dz) > sep || w_absf(u_y[j] - u_y[i]) > 1.0f) continue;
            float d2 = dx * dx + dz * dz;
            if (d2 > sep * sep) continue;
            float d = w_sqrt(d2);
            if (d < 0.01f) { dx = 0.01f * (float)((i % 3) - 1) + 0.005f; dz = 0.007f; d = 0.0122f; }
            float push = (sep - d) * 0.5f;
            float px = dx / d * push, pz = dz / d * push;
            unit_walk(i, -px, -pz, -1);
            unit_walk(j, px, pz, -1);
        }
    }

    /* Heights + animation phase. */
    for (i = 0; i < g_un; i++) {
        if (!u_ld[i] || u_state[i] == US_CLIMB || u_class[i] == UC_DRAGON) continue;
        float ty = nav_surface(u_layer[i], u_x[i], u_z[i]);
        if (u_class[i] == UC_BOAT) ty = W_SEA_LEVEL + 0.05f * w_sin(g_time * 1.7f + u_seed[i]);
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
        if (a_kind[k]) {                 /* a stone, tumbling */
            float sz = a_kind[k] == 2 ? 0.42f : 0.3f, M[12];
            float sp = a_life[k] * 5.0f;
            M[0] = w_cos(sp); M[1] = w_sin(sp); M[2] = 0.0f;
            M[3] = -w_sin(sp); M[4] = w_cos(sp); M[5] = 0.0f;
            M[6] = 0.0f; M[7] = 0.0f; M[8] = 1.0f;
            M[9] = p_x[k]; M[10] = p_y[k]; M[11] = p_z[k];
            u_color(0.52f, 0.51f, 0.49f);
            u_box(M, 0.0f, 0.0f, 0.0f, sz, sz * 0.85f, sz * 0.9f);
            continue;
        }
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

/* World-space frame at (x,y,z) turned by angle a about +y, in the
 * castle generator's convention (local x = (cos a, sin a)). */
static void u_frame(float *M, float x, float y, float z, float a) {
    float c = w_cos(a), sn = w_sin(a);
    M[0] = c; M[1] = 0.0f; M[2] = sn;
    M[3] = 0.0f; M[4] = 1.0f; M[5] = 0.0f;
    M[6] = -sn; M[7] = 0.0f; M[8] = c;
    M[9] = x; M[10] = y; M[11] = z;
}

/* Fishing boat: planked hull, mast + team sail, a fisherman at the
 * stern; while fishing a net is spread on the water around it, and its
 * catch shows as crates of fish. */
static void boat_emit(int i, int lod) {
    float root[12], hull[12];
    float t = g_time + u_seed[i];
    m34_root(root, i, 0.0f);
    m34_local(hull, root, 0.0f, 0.0f, 0.0f, 0.035f * w_sin(t * 1.3f), 0.05f * w_sin(t * 1.7f));
    u_color(0.45f, 0.30f, 0.17f);
    u_box(hull, 0.0f, 0.12f, 0.1f, 0.62f, 0.28f, 1.35f);
    u_color(0.40f, 0.26f, 0.14f);
    u_box(hull, 0.0f, 0.16f, -1.45f, 0.38f, 0.24f, 0.25f);
    u_box(hull, 0.0f, 0.22f, -1.78f, 0.16f, 0.18f, 0.14f);
    u_color(0.30f, 0.20f, 0.11f);
    u_box(hull, 0.6f, 0.43f, 0.0f, 0.05f, 0.04f, 1.45f);
    u_box(hull, -0.6f, 0.43f, 0.0f, 0.05f, 0.04f, 1.45f);
    u_color(0.58f, 0.44f, 0.28f);
    u_box(hull, 0.0f, 0.36f, 0.1f, 0.56f, 0.03f, 1.25f);
    /* mast + sail in team colours */
    u_color(0.36f, 0.25f, 0.14f);
    u_box(hull, 0.0f, 1.55f, -0.35f, 0.05f, 1.2f, 0.05f);
    if (u_team[i] == TEAM_PLAYER) u_color(0.86f, 0.88f, 0.92f); else u_color(0.92f, 0.86f, 0.80f);
    u_box(hull, 0.0f, 1.75f, -0.30f, 0.62f, 0.75f, 0.02f);
    if (u_team[i] == TEAM_PLAYER) u_color(0.16f, 0.36f, 0.86f); else u_color(0.80f, 0.16f, 0.14f);
    u_box(hull, 0.0f, 1.75f, -0.32f, 0.64f, 0.14f, 0.02f);
    /* fisherman */
    if (lod) {
        float fpitch = u_pstate[i] == PS_G_WORK ? 0.4f * w_sin(u_work_t[i] * 7.0f) : 0.0f;
        float fm[12];
        m34_local(fm, hull, 0.0f, 0.4f, 0.95f, fpitch, 0.0f);
        u_color(0.55f, 0.40f, 0.24f);
        u_box(fm, 0.0f, 0.35f, 0.0f, 0.17f, 0.30f, 0.12f);
        u_color3(u_face + i * 3);
        u_box(fm, 0.0f, 0.78f, 0.0f, 0.11f, 0.12f, 0.11f);
        u_color(0.86f, 0.76f, 0.42f);
        u_box(fm, 0.0f, 0.92f, 0.0f, 0.2f, 0.02f, 0.2f);
    }
    /* catch: crates of silver fish amidships */
    if (u_carry[i] > 0) {
        int c, nc = (u_carry[i] + 3) / 4;
        for (c = 0; c < nc && c < 3; c++) {
            u_color(0.50f, 0.34f, 0.18f);
            u_box(hull, 0.0f, 0.5f, 0.55f - 0.45f * (float)c, 0.25f, 0.12f, 0.18f);
            u_color(0.78f, 0.82f, 0.86f);
            u_box(hull, 0.0f, 0.63f, 0.55f - 0.45f * (float)c, 0.2f, 0.03f, 0.14f);
        }
    }
    /* net cast on the water while fishing: floats on a ring + cords */
    if (u_pstate[i] == PS_G_WORK) {
        float k = 0.5f + 0.5f * w_sin(u_work_t[i] * 1.1f);
        float r = 1.6f + 1.0f * k;
        float cx = u_x[i] + w_sin(u_yaw[i]) * 0.0f, cz = u_z[i];
        u_color(0.85f, 0.82f, 0.70f);
        u_ring(cx, W_SEA_LEVEL + 0.04f, cz, r, 0.06f);
        u_ring(cx, W_SEA_LEVEL + 0.04f, cz, r * 0.6f, 0.04f);
        int q;
        float I[12];
        u_frame(I, 0.0f, 0.0f, 0.0f, 0.0f);
        for (q = 0; q < 8; q++) {
            float a = (float)q * (W_PI / 4.0f) + t * 0.05f;
            float pa[3], pb[3];
            u_set3(pa, cx, W_SEA_LEVEL + 0.05f, cz);
            u_set3(pb, cx + w_cos(a) * r, W_SEA_LEVEL + 0.05f, cz + w_sin(a) * r);
            u_seg(I, pa, pb, 0.02f);
            u_color(0.95f, 0.55f, 0.20f);
            u_box(I, pb[0], pb[1] + 0.05f, pb[2], 0.1f, 0.07f, 0.1f);
            u_color(0.85f, 0.82f, 0.70f);
        }
    }
    if (u_selected[i]) { u_color(0.25f, 1.0f, 0.35f); u_ring(u_x[i], u_y[i] + 0.1f, u_z[i], 1.9f, 0.1f); }
    if (u_selected[i] || u_hp[i] < u_maxhp[i]) {
        float frac = (float)u_hp[i] / (float)u_maxhp[i];
        float top = u_y[i] + 3.0f;
        u_color(0.12f, 0.12f, 0.12f);
        u_bar(u_x[i], top, u_z[i], 0.6f, 0.12f);
        u_color(0.2f, 0.95f, 0.3f);
        float rx = w_cos(g_yaw), rz = w_sin(g_yaw);
        float w2 = 0.58f * frac;
        u_bar(u_x[i] - rx * (0.58f - w2), top + 0.015f, u_z[i] - rz * (0.58f - w2), w2, 0.08f);
    }
}

#include "android/examples/castles_siege_draw.h"
#include "android/examples/castles_dragons_draw.h"

/* One humanoid (or boat, or siege engine), procedurally animated from its state. */
static void unit_emit(int i, int lod) {
    if (u_class[i] == UC_DRAGON) { dragon_emit(i, lod); return; }
    if (is_siege(i)) { if (u_state[i] != US_DEAD) siege_emit(i, lod); return; }
    if (u_class[i] == UC_BOAT) { if (u_state[i] != US_DEAD) boat_emit(i, lod); return; }
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
    } else if (u_class[i] == UC_PEASANT && (u_pstate[i] == PS_G_WORK || u_pstate[i] == PS_B_WORK || u_pstate[i] == PS_T_WORK)) {
        /* chop / quarry / hammer: two-handed overhead swing */
        float cyc = u_work_t[i] / (u_pstate[i] == PS_B_WORK ? 0.7f : 1.1f);
        float tt = cyc - (float)w_ifloor(cyc);
        float sh;
        if (tt < 0.6f) { float k = tt / 0.6f; sh = 0.3f + 2.4f * k; lean = -0.06f * k; }
        else if (tt < 0.8f) { float k = (tt - 0.6f) / 0.2f; sh = 2.7f - 2.5f * k; lean = -0.06f + 0.36f * k; }
        else { float k = (tt - 0.8f) / 0.2f; sh = 0.2f + 0.1f * k; lean = 0.3f - 0.3f * k; }
        armR = sh; armL = sh * 0.92f; elbR = -0.3f; elbL = -0.45f; armRollL = 0.45f; armRollR = -0.2f;
        legL = 0.32f; legR = -0.26f; kneeL = -0.15f; kneeR = -0.22f;
    } else if (st == US_ATTACK && u_class[i] == UC_MACE) {
        /* mace: high overhead wind-up, fast smash with a lunge and a hop */
        float tt = u_swing[i];
        float sh, el;
        if (tt < 0.35f) { float k = tt / 0.35f; sh = 0.4f + 2.8f * k; el = -0.4f - 1.0f * k; lean = -0.05f * k; }
        else if (tt < 0.5f) { float k = (tt - 0.35f) / 0.15f; sh = 3.2f - 3.3f * k; el = -1.4f + 1.4f * k; lean = 0.35f * k; }
        else { float k = (tt - 0.5f) / 0.5f; sh = -0.1f + 0.4f * k; el = -0.3f * k; lean = 0.35f - 0.3f * k; }
        armR = sh; elbR = el;
        armL = 0.5f; elbL = -0.6f; armRollL = 0.35f;
        legL = 0.45f; legR = -0.35f; kneeL = -0.3f; kneeR = -0.25f;
        bob = w_sin(w_clampf(tt * 2.0f, 0.0f, 1.0f) * W_PI) * 0.09f * s;
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
    if (u_class[i] == UC_PEASANT) {
        /* straw hat, team sash, basket on the way home */
        u_color(0.86f, 0.76f, 0.42f);
        u_box(head, 0.0f, 0.28f * s, 0.0f, 0.24f * s, 0.018f * s, 0.24f * s);
        u_box(head, 0.0f, 0.33f * s, 0.0f, 0.12f * s, 0.05f * s, 0.12f * s);
        if (u_team[i] == TEAM_PLAYER) u_color(0.16f, 0.36f, 0.86f); else u_color(0.80f, 0.16f, 0.14f);
        u_box(torso, 0.0f, 0.06f * s, 0.0f, 0.22f * g, 0.04f * s, 0.13f * g);
        if (u_pstate[i] == PS_RETURN && lod) {
            u_color(0.55f, 0.40f, 0.20f);
            u_box(torso, 0.0f, 0.35f * s, 0.19f * g, 0.15f, 0.14f * s, 0.08f);
        }
        if (u_carry[i] > 0) {
            if (u_carry_t[i] == RES_WOOD) {       /* a bundle of logs across the back */
                u_color(0.45f, 0.30f, 0.16f);
                u_box(torso, 0.0f, 0.50f * s, 0.18f * g, 0.32f, 0.055f, 0.055f);
                u_box(torso, 0.02f, 0.40f * s, 0.18f * g, 0.30f, 0.055f, 0.055f);
                u_color(0.80f, 0.66f, 0.42f);
                u_box(torso, 0.33f, 0.50f * s, 0.18f * g, 0.012f, 0.045f, 0.045f);
            } else {                              /* a dressed stone block */
                u_color(0.60f, 0.60f, 0.62f);
                u_box(torso, 0.0f, 0.42f * s, 0.21f * g, 0.15f, 0.13f * s, 0.10f);
            }
        }
    } else if (u_helmet[i]) {
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
        if (u_class[i] == UC_PEASANT) {
            /* axe or pick while on a gathering job, else unarmed */
            int job = u_job[i];
            if (side > 0 && (u_pstate[i] == PS_B_GO || u_pstate[i] == PS_B_WORK)) {
                /* builder's mallet */
                u_color(0.50f, 0.36f, 0.20f);
                u_box(a2, 0.0f, -0.31f * s, -0.18f, 0.022f, 0.022f, 0.24f);
                u_color(0.42f, 0.30f, 0.17f);
                u_box(a2, 0.0f, -0.31f * s, -0.42f, 0.07f, 0.13f, 0.07f);
            } else if (side > 0 && u_pstate[i] >= PS_G_GO && u_pstate[i] <= PS_G_WORK && job >= 0 && job < GJ_MAX) {
                u_color(0.50f, 0.36f, 0.20f);
                u_box(a2, 0.0f, -0.31f * s, -0.24f, 0.024f, 0.024f, 0.33f);
                u_color(0.55f, 0.56f, 0.60f);
                if (g_gj_type[job] == RES_WOOD) u_box(a2, 0.0f, -0.31f * s + 0.07f, -0.52f, 0.018f, 0.10f, 0.08f);
                else u_box(a2, 0.0f, -0.31f * s, -0.54f, 0.026f, 0.22f, 0.026f);
            }
        } else if (u_class[i] == UC_MACE) {
            if (side > 0) {
                /* mace: wooden haft, iron head with spikes */
                u_color(0.40f, 0.27f, 0.14f);
                u_box(a2, 0.0f, -0.31f * s, -0.22f, 0.026f, 0.026f, 0.30f);
                u_color(0.30f, 0.30f, 0.33f);
                u_box(a2, 0.0f, -0.31f * s, -0.56f, 0.085f, 0.085f, 0.085f);
                u_color(0.52f, 0.52f, 0.56f);
                u_box(a2, 0.0f, -0.31f * s, -0.56f, 0.15f, 0.025f, 0.025f);
                u_box(a2, 0.0f, -0.31f * s, -0.56f, 0.025f, 0.15f, 0.025f);
                u_box(a2, 0.0f, -0.31f * s, -0.56f, 0.025f, 0.025f, 0.15f);
            }
        } else if (u_archer[i]) {
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

/* Fish shoals near the camera: spreading ripples and jumping fish. */
static void shoals_emit(void) {
    int sl, k;
    float I[12];
    u_frame(I, 0.0f, 0.0f, 0.0f, 0.0f);
    for (sl = 0; sl < SLOTS; sl++) {
        if (!g_slot_used[sl] || g_slot_nres[sl] == 0) continue;
        float ox = ((float)g_slot_cx[sl] + 0.5f) * W_CHUNK_SIZE - g_cam[0], oz = ((float)g_slot_cz[sl] + 0.5f) * W_CHUNK_SIZE - g_cam[2];
        if (ox * ox + oz * oz > 140.0f * 140.0f) continue;
        int n = g_slot_nres[sl];
        for (k = 0; k < n; k++) {
            int q = sl * RES_MAX + k;
            if (g_slot_res_t[q] != RES_FOOD) continue;
            uint32_t id = g_slot_res_id[q];
            if (res_remaining(id) <= 0) continue;
            float x = g_slot_res_x[q], z = g_slot_res_z[q];
            float ph = (float)(id & 1023u) * 0.01f;
            int r;
            for (r = 0; r < 2; r++) {
                float tt = g_time * 0.45f + ph + 0.5f * (float)r;
                float f = tt - (float)w_ifloor(tt);
                u_color(0.55f + 0.4f * (1.0f - f), 0.70f + 0.28f * (1.0f - f), 0.85f + 0.15f * (1.0f - f));
                u_ring(x, W_SEA_LEVEL + 0.03f, z, 0.4f + 2.0f * f, 0.07f);
            }
            int m;
            for (m = 0; m < 3; m++) {
                float tt = g_time * 0.8f + ph * 3.0f + 0.37f * (float)m;
                float f = tt - (float)w_ifloor(tt);
                if (f > 0.45f) continue;                    /* in the air 45% of the time */
                float u = f / 0.45f;
                float a = (float)m * 2.1f + ph * 7.0f + (float)w_ifloor(tt) * 1.3f;
                float px = x + w_cos(a) * (0.6f + 1.2f * u), pz = z + w_sin(a) * (0.6f + 1.2f * u);
                float py = W_SEA_LEVEL + 1.1f * u * (1.0f - u) * 4.0f * 0.35f;
                float pa[3], pb[3];
                u_set3(pa, px - w_cos(a) * 0.18f, py - 0.05f + 0.1f * u, pz - w_sin(a) * 0.18f);
                u_set3(pb, px + w_cos(a) * 0.18f, py + 0.05f - 0.1f * u, pz + w_sin(a) * 0.18f);
                u_color(0.80f, 0.85f, 0.90f);
                u_seg(I, pa, pb, 0.06f);
            }
        }
    }
}

/* One structure drawn under construction (progress p in [0,1]) or as a
 * placement preview (ghost: tinted footprint, valid green / invalid red). */
static float g_se_x, g_se_z, g_se_x2, g_se_z2, g_se_y, g_se_lo, g_se_yaw;   /* site_emit_one's geometry */
static void site_emit_one(int kind, float p, int ghost, int ok) {
    float x = g_se_x, z = g_se_z, x2 = g_se_x2, z2 = g_se_z2, y = g_se_y, lo = g_se_lo, yaw = g_se_yaw;
    float M[12], a, r[4];
    float wood[3], stone[3];
    wood[0] = 0.55f; wood[1] = 0.40f; wood[2] = 0.24f;
    stone[0] = 0.62f; stone[1] = 0.60f; stone[2] = 0.56f;
    if (ghost) {
        if (ok) { wood[0] = 0.30f; wood[1] = 0.95f; wood[2] = 0.40f; } else { wood[0] = 1.0f; wood[1] = 0.25f; wood[2] = 0.2f; }
        stone[0] = wood[0]; stone[1] = wood[1]; stone[2] = wood[2];
    }
    float H = kind == ST_HOUSE ? 3.4f : (kind == ST_BARRACKS ? 3.7f : (kind == ST_SMITH ? 3.0f : (kind == ST_WORKSHOP ? 3.6f : (kind == ST_STOCKPILE ? 1.6f : (kind == ST_DOCK ? 2.0f : PS_WALL_H + 0.8f)))));
    float hb = y + 0.05f;
    if (kind == ST_WALL) {
        float ex = x2 - x, ez = z2 - z;
        float len = w_sqrt(ex * ex + ez * ez);
        if (len < 0.1f) return;
        u_frame(M, (x + x2) * 0.5f, 0.0f, (z + z2) * 0.5f, approx_atan2_w(ez, ex));
        u_color3(stone);
        if (ghost) { u_box(M, 0.0f, y + 0.1f, 0.0f, len * 0.5f, 0.12f, PS_WALL_T * 0.5f); }
        else {
            float hh = (y - lo + 1.0f + PS_WALL_H) * p;
            u_box(M, 0.0f, lo - 1.0f + hh * 0.5f, 0.0f, len * 0.5f, hh * 0.5f + 0.01f, PS_WALL_T * 0.5f);
        }
        float u;
        u_color(0.48f, 0.34f, 0.20f);
        for (u = -len * 0.5f; u <= len * 0.5f + 0.01f; u = u + 3.0f) {
            u_box(M, u, y + (ghost ? 0.6f : H * 0.5f), PS_WALL_T * 0.5f + 0.25f, 0.06f, ghost ? 0.6f : H * 0.5f, 0.06f);
            u_box(M, u, y + (ghost ? 0.6f : H * 0.5f), -PS_WALL_T * 0.5f - 0.25f, 0.06f, ghost ? 0.6f : H * 0.5f, 0.06f);
        }
        if (!ghost) {
            u_box(M, 0.0f, y + H * 0.45f, PS_WALL_T * 0.5f + 0.25f, len * 0.5f, 0.04f, 0.05f);
            u_box(M, 0.0f, y + H * 0.45f, -PS_WALL_T * 0.5f - 0.25f, len * 0.5f, 0.04f, 0.05f);
        }
        return;
    }
    if (kind == ST_TOWER) {
        float k2;
        u_color3(stone);
        for (k2 = 0.0f; k2 < 0.7f; k2 = k2 + 0.785f) {
            u_frame(M, x, 0.0f, z, k2);
            if (ghost) u_box(M, 0.0f, y + 0.1f, 0.0f, PS_TOWER_R * 0.9f, 0.12f, PS_TOWER_R * 0.9f);
            else { float hh = (y - lo + 1.0f + H) * p; u_box(M, 0.0f, lo - 1.0f + hh * 0.5f, 0.0f, PS_TOWER_R * 0.9f, hh * 0.5f + 0.01f, PS_TOWER_R * 0.9f); }
        }
        u_frame(M, x, 0.0f, z, 0.0f);
        u_color(0.48f, 0.34f, 0.20f);
        int q;
        for (q = 0; q < 4; q++) {
            float px = (q & 1) ? PS_TOWER_R + 0.3f : -PS_TOWER_R - 0.3f, pz = (q & 2) ? PS_TOWER_R + 0.3f : -PS_TOWER_R - 0.3f;
            u_box(M, px, y + (ghost ? 0.6f : H * 0.5f), pz, 0.06f, ghost ? 0.6f : H * 0.5f, 0.06f);
        }
        return;
    }
    struct_rect(kind, yaw, 1, &a, r);
    u_frame(M, x, 0.0f, z, a);
    float cx = (r[0] + r[2]) * 0.5f, cz = (r[1] + r[3]) * 0.5f, hx = (r[2] - r[0]) * 0.5f, hz = (r[3] - r[1]) * 0.5f;
    if (ghost) {
        u_color3(wood);
        u_box(M, cx, y + 0.1f, cz, hx, 0.1f, hz);
        if (kind == ST_DOCK) { u_color3(wood); u_box(M, 0.0f, W_SEA_LEVEL + 0.5f, DOCK_PIER * 0.5f, 0.85f, 0.08f, DOCK_PIER * 0.5f); }
    } else {
        /* foundations, then walls rising with progress */
        u_color(0.50f, 0.48f, 0.44f);
        float fx0 = r[0], fx1 = r[2], fz0 = r[1], fz1 = kind == ST_BARRACKS ? PS_BARR_HZ : (kind == ST_SMITH ? PS_SMITH_HZ : (kind == ST_WORKSHOP ? PS_WS_HZ : (kind == ST_STOCKPILE ? PS_SP_HZ : r[3])));
        if (kind == ST_DOCK) { fx0 = 0.9f; fx1 = 2.9f; fz0 = -2.4f; fz1 = -0.4f; }
        if (kind == ST_GATE) { fx0 = -(PS_GATE_HALF + 2.0f * PS_GATE_TR); fx1 = -PS_GATE_HALF; }
        float fh = (hb - (lo - 0.6f)) * 0.5f;
        u_box(M, (fx0 + fx1) * 0.5f, lo - 0.6f + fh, (fz0 + fz1) * 0.5f, (fx1 - fx0) * 0.5f + 0.15f, fh, (fz1 - fz0) * 0.5f + 0.15f);
        if (kind == ST_GATE) u_box(M, (PS_GATE_HALF + PS_GATE_HALF + 2.0f * PS_GATE_TR) * 0.5f, lo - 0.6f + fh, 0.0f, PS_GATE_TR + 0.15f, fh, (fz1 - fz0) * 0.5f + 0.15f);
        float wh = H * p;
        if (kind == ST_HOUSE) u_color(0.86f, 0.80f, 0.66f); else if (kind == ST_DOCK) u_color3(wood);
        else if (kind == ST_SMITH) u_color(0.46f, 0.43f, 0.40f);
        else if (kind == ST_WORKSHOP) u_color3(wood); else u_color3(stone);
        if (wh > 0.02f) {
            if (kind == ST_GATE) {
                u_box(M, fx0 + PS_GATE_TR, hb + wh * 0.5f, 0.0f, PS_GATE_TR * 0.9f, wh * 0.5f, PS_GATE_TR * 0.9f);
                u_box(M, -fx0 - PS_GATE_TR, hb + wh * 0.5f, 0.0f, PS_GATE_TR * 0.9f, wh * 0.5f, PS_GATE_TR * 0.9f);
            } else u_box(M, (fx0 + fx1) * 0.5f, hb + wh * 0.5f, (fz0 + fz1) * 0.5f, (fx1 - fx0) * 0.5f, wh * 0.5f, (fz1 - fz0) * 0.5f);
            if (kind == ST_DOCK) {          /* pier planks laid out from the shore */
                u_color3(wood);
                u_box(M, 0.0f, W_SEA_LEVEL + 0.45f, DOCK_PIER * p * 0.5f, 0.85f, 0.08f, DOCK_PIER * p * 0.5f + 0.01f);
            }
        }
    }
    /* scaffold poles at the corners (+ the yard / pier ends) */
    u_color(0.48f, 0.34f, 0.20f);
    float ph = ghost ? 0.6f : (H + 0.6f) * 0.5f;
    int q;
    for (q = 0; q < 4; q++) {
        float px = (q & 1) ? r[2] + 0.25f : r[0] - 0.25f, pz = (q & 2) ? r[3] + 0.25f : r[1] - 0.25f;
        u_box(M, px, y + ph, pz, 0.07f, ph, 0.07f);
    }
    if (!ghost) {
        u_box(M, cx, y + H * 0.55f, r[1] - 0.25f, hx + 0.25f, 0.04f, 0.05f);
        u_box(M, cx, y + H * 0.55f, r[3] + 0.25f, hx + 0.25f, 0.04f, 0.05f);
    }
    (void)hz;
}

/* Placement preview (set by the UI while placing). */
static int g_ghost_on, g_ghost_kind;

static void sites_emit(void) {
    int k;
    for (k = 0; k < g_bs_n; k++) {
        if (g_bs_done[k] && g_bs_fade[k] <= 0.0f) continue;
        float dx = g_bs_x[k] - g_cam[0], dz = g_bs_z[k] - g_cam[2];
        if (dx * dx + dz * dz > 170.0f * 170.0f) continue;
        float p = g_bs_done[k] ? 1.0f : w_clampf(g_bs_work[k] / g_bs_need[k], 0.0f, 1.0f);
        g_se_x = g_bs_x[k]; g_se_z = g_bs_z[k]; g_se_x2 = g_bs_x2[k]; g_se_z2 = g_bs_z2[k];
        g_se_y = g_bs_y[k]; g_se_lo = g_bs_lo[k]; g_se_yaw = g_bs_yaw[k];
        site_emit_one(g_bs_kind[k], p, 0, 1);
        if (!g_bs_done[k]) {
            float gx, gz;
            struct_goal(k, &gx, &gz);
            float top = g_bs_y[k] + PS_WALL_H + 2.0f;
            u_color(0.12f, 0.12f, 0.12f);
            u_bar(gx, top, gz, 1.6f, 0.25f);
            u_color(0.35f, 0.85f, 1.0f);
            float rx = w_cos(g_yaw), rz = w_sin(g_yaw), w2 = 1.55f * p;
            u_bar(gx - rx * (1.55f - w2), top + 0.03f, gz - rz * (1.55f - w2), w2, 0.19f);
        }
    }
    if (g_ghost_on) {
        float gy = nav_terrain_height(g_pl_x, g_pl_z);
        g_se_x = g_pl_x; g_se_z = g_pl_z; g_se_x2 = g_pl_x2; g_se_z2 = g_pl_z2;
        g_se_y = g_pl_valid ? g_pl_y : gy; g_se_lo = g_pl_valid ? g_pl_lo : gy; g_se_yaw = g_pl_yaw;
        site_emit_one(g_ghost_kind, 0.0f, 1, g_pl_valid);
    }
}

/* Builds this frame's unit geometry; returns the vertex count. */
static int units_build_mesh(void) {
    int i;
    g_un_verts = 0;
    float fx = w_sin(g_yaw), fz = -w_cos(g_yaw);
    for (i = 0; i < g_un; i++) {
        if (!u_active[i] || (!unit_loaded(i) && u_class[i] != UC_DRAGON)) continue;
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
    /* selected home building + barracks training progress */
    if (g_sel_building >= 0 && g_sel_building < g_hb_n) {
        int b = g_sel_building;
        float r = (g_hb_hx[b] > g_hb_hz[b] ? g_hb_hx[b] : g_hb_hz[b]) + 1.5f;
        u_color(0.25f, 1.0f, 0.35f);
        u_ring(g_hb_x[b], g_hb_y[b] + 0.12f, g_hb_z[b], r, 0.18f);
    }
    for (i = 0; i < g_hb_n; i++) {
        if (g_bq_n[i] == 0) continue;
        float frac = w_clampf(g_bq_t[i] / bq_time(i), 0.0f, 1.0f);
        float top = g_hb_y[i] + (g_hb_t[i] == BT_TOWNCENTER ? 10.0f : 6.0f);
        u_color(0.12f, 0.12f, 0.12f);
        u_bar(g_hb_x[i], top, g_hb_z[i], 1.6f, 0.25f);
        u_color(1.0f, 0.82f, 0.25f);
        float rx = w_cos(g_yaw), rz = w_sin(g_yaw);
        float w2 = 1.55f * frac;
        u_bar(g_hb_x[i] - rx * (1.55f - w2), top + 0.03f, g_hb_z[i] - rz * (1.55f - w2), w2, 0.19f);
    }
    for (i = 0; i < g_hb_n; i++) {
        if (!g_rs_tech[i]) continue;
        float dxr = g_hb_x[i] - g_cam[0], dzr = g_hb_z[i] - g_cam[2];
        if (dxr * dxr + dzr * dzr > 170.0f * 170.0f) continue;
        float frac = w_clampf(g_rs_t[i] / tech_time(g_rs_tech[i] - 1), 0.0f, 1.0f);
        float top = g_hb_y[i] + (g_hb_t[i] == BT_TOWNCENTER ? 11.0f : 7.0f);
        u_color(0.12f, 0.12f, 0.12f);
        u_bar(g_hb_x[i], top, g_hb_z[i], 1.6f, 0.25f);
        u_color(0.75f, 0.45f, 1.0f);
        float rx = w_cos(g_yaw), rz = w_sin(g_yaw);
        float w2 = 1.55f * frac;
        u_bar(g_hb_x[i] - rx * (1.55f - w2), top + 0.03f, g_hb_z[i] - rz * (1.55f - w2), w2, 0.19f);
    }
    shoals_emit();
    sites_emit();
    structure_bars_emit();
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
        if (!unit_alive(i) || (!unit_loaded(i) && u_class[i] != UC_DRAGON)) continue;
        float px, py;
        int dg = u_class[i] == UC_DRAGON;
        if (!project_point(u_x[i], u_y[i] + (dg ? 0.0f : 1.0f), u_z[i], &px, &py)) continue;
        float dx = px - sx, dy = py - sy;
        float d2 = dx * dx + dy * dy;
        if (dg) { float nd = d2 * 0.3f; d2 = nd; }             /* big: easier to tap */
        if (d2 > 60.0f * 60.0f) continue;
        if (d2 < bestScore) { bestScore = d2; best = i; }
    }
    return best;
}

/* Loaded tree / rock under screen point (sx,sy) (projected trunk or
 * boulder centre within 70 px, nearest to the camera), or -1. */
static int pick_resource(float sx, float sy, int mask) {
    int sl, k, best = -1;
    float bestScore = 1e30f;
    for (sl = 0; sl < SLOTS; sl++) {
        if (!g_slot_used[sl] || g_slot_nres[sl] == 0) continue;
        float ox = (float)g_slot_cx[sl] * W_CHUNK_SIZE + W_CHUNK_SIZE * 0.5f, oz = (float)g_slot_cz[sl] * W_CHUNK_SIZE + W_CHUNK_SIZE * 0.5f;
        float cdx = ox - g_cam[0], cdz = oz - g_cam[2];
        if (cdx * cdx + cdz * cdz > 190.0f * 190.0f) continue;
        int n = g_slot_nres[sl];
        for (k = 0; k < n; k++) {
            int q = sl * RES_MAX + k;
            if (!(mask & (1 << g_slot_res_t[q]))) continue;
            float hy = g_slot_res_t[q] == RES_WOOD ? 1.4f : (g_slot_res_t[q] == RES_FOOD ? 0.1f : 0.5f);
            float px, py;
            if (!project_point(g_slot_res_x[q], g_slot_res_y[q] + hy, g_slot_res_z[q], &px, &py)) continue;
            float dx = px - sx, dy = py - sy;
            float d2 = dx * dx + dy * dy;
            if (d2 > 70.0f * 70.0f) continue;
            float ex = g_slot_res_x[q] - g_cam[0], ez = g_slot_res_z[q] - g_cam[2];
            float score = d2 + (ex * ex + ez * ez) * 0.5f;
            if (score >= bestScore) continue;
            if (res_remaining(g_slot_res_id[q]) <= 0) continue;
            bestScore = score; best = q;
        }
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

/* Home building under screen point (sx,sy), or -1. The tap ray is cut
 * with horizontal slices through each building's height and tested
 * against its rotated footprint, so a building can be tapped anywhere on
 * it (its centre may be off screen); the nearest hit along the ray wins.
 * With nothing hit, falls back to the nearest centre within a screen
 * radius (smaller when `strict`, i.e. units are selected and a near-miss
 * should rather be a move order). */
static int pick_building(float sx, float sy, int strict) {
    int b, k, best = -1;
    float bestD = 1e30f;
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_t[b] == BT_STALL || !g_hb_done[b] || g_hb_own[b] != 0) continue;
        float c = w_cos(g_hb_yaw[b] - 0.5f * W_PI), sn = w_sin(g_hb_yaw[b] - 0.5f * W_PI);   /* mesh frame */
        int nsl = g_hb_t[b] == BT_TOWNCENTER ? 6 : 4;
        for (k = 0; k < nsl; k++) {
            float y = g_hb_y[b] + 0.3f + 1.4f * (float)k, hx, hz;
            if (!screen_to_plane(sx, sy, y, &hx, &hz)) continue;
            float bx = hx - g_hb_x[b], bz = hz - g_hb_z[b];
            float lx = bx * c + bz * sn, lz = -bx * sn + bz * c;
            if (w_absf(lx) > g_hb_hx[b] + 0.4f || w_absf(lz) > g_hb_hz[b] + 0.4f) continue;
            float ex = hx - g_cam[0], ey = y - g_cam[1], ez = hz - g_cam[2];
            float d2 = ex * ex + ey * ey + ez * ez;
            if (d2 < bestD) { bestD = d2; best = b; }
        }
    }
    if (best >= 0) return best;
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_t[b] == BT_STALL || !g_hb_done[b] || g_hb_own[b] != 0) continue;
        float px, py;
        float hgt = g_hb_t[b] == BT_TOWNCENTER ? 5.0f : 2.0f;
        if (!project_point(g_hb_x[b], g_hb_y[b] + hgt, g_hb_z[b], &px, &py)) continue;
        float dx = px - sx, dy = py - sy;
        float d2 = dx * dx + dy * dy;
        float reach = g_hb_t[b] == BT_TOWNCENTER ? 160.0f : (g_hb_t[b] == BT_BARRACKS ? 120.0f : 70.0f);
        if (strict) reach = reach * 0.4f;
        if (d2 > reach * reach) continue;
        if (d2 < bestD) { bestD = d2; best = b; }
    }
    return best;
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
