/* ---- castles_siege.h: structure health, destruction, siege engines ----
 *
 * Structures: every building (g_hb_*) has hit points, and so does every
 * piece of the castles that matter -- the player's and the AI castles':
 * curtain-wall sections (~6 units of each wall edge, the same cut the
 * generator uses), corner towers, bastions, gatehouses -- plus walls,
 * towers and gatehouses the factions build. Knocked down, a piece's id
 * goes on the generator's destroyed list (player-built ones get
 * g_ps_dead) and the chunks under it are regenerated: rubble instead,
 * and the ground opens up -- a breach units can walk through.
 *
 * Siege engines (trained at a siege workshop):
 *   catapult  -- lobs stones (castles_ballistics.h, the same integrator
 *                and solver as arrows, with a heavy projectile) at
 *                structures up to 34 away, or at units when no structure
 *                is in reach; a stone smashes whatever it lands on.
 *   trebuchet -- outranges everything (75) but must be set up before it
 *                can shoot (and packed up again before it can move):
 *                ~6 s each way.
 *   ram       -- rolls up to a wall, tower, gate or building and batters
 *                it; useless against units.
 * Engines move on the ground only (fields without the wall-walk layer
 * or ladders) and never fight hand to hand.
 *
 * Included by castles_units.h before castles_ai.h. */

#ifndef CASTLES_SIEGE_H
#define CASTLES_SIEGE_H

/* ---- destructible castle pieces ---- */
#define EL_MAX 2600
static int g_el_n;
static int el_kind[EL_MAX], el_own[EL_MAX], el_dead[EL_MAX], el_ps[EL_MAX];
static float el_x[EL_MAX], el_z[EL_MAX], el_x2[EL_MAX], el_z2[EL_MAX], el_y[EL_MAX], el_top[EL_MAX], el_r[EL_MAX];
static int el_hp[EL_MAX], el_maxhp[EL_MAX];
static uint32_t el_id_[EL_MAX];

static int el_add(int kind, int own, float x, float z, float r, int hp) {
    int e = g_el_n;
    if (e >= EL_MAX) return -1;
    el_kind[e] = kind; el_own[e] = own; el_dead[e] = 0; el_ps[e] = -1;
    el_x[e] = x; el_z[e] = z; el_x2[e] = x; el_z2[e] = z; el_r[e] = r;
    el_hp[e] = hp; el_maxhp[e] = hp; el_id_[e] = 0u;
    g_el_n = e + 1;
    return e;
}

/* Registers castle s's walls, towers, bastions and gatehouses for
 * faction own, with the generator's piece cut and ids. */
static void el_register_site(int own, WSite *s) {
    float vx[SITE_MAXV], vz[SITE_MAXV], e[4], gg0[2], gg1[2];
    int k, v, g;
    for (k = 0; k < s->rings; k++) {
        float t = site_ring_t(s, k), top = site_ring_top(s, k);
        int nv = site_ring_verts(s, k, vx, vz);
        for (v = 0; v < nv; v++) {
            int w2 = (v + 1) % nv;
            e[0] = vx[v]; e[1] = vz[v]; e[2] = vx[w2]; e[3] = vz[w2];
            float ex = e[2] - e[0], ez = e[3] - e[1];
            float len = w_sqrt(ex * ex + ez * ez);
            int ng = site_edge_gaps(s, e, gg0, gg1), npc = site_wall_pieces(len), pc;
            for (pc = 0; pc < npc; pc++) {
                float s0 = (float)pc / (float)npc, s1 = (float)(pc + 1) / (float)npc;
                float solid = s1 - s0;
                for (g = 0; g < ng; g++) {
                    float a0 = gg0[g] > s0 ? gg0[g] : s0, a1 = gg1[g] < s1 ? gg1[g] : s1;
                    if (a1 > a0) { float ns = solid - (a1 - a0); solid = ns; }
                }
                if (solid * len < 0.5f) continue;                  /* all gate passage */
                int el = el_add(EK_WALL, own, e[0] + ex * s0, e[1] + ez * s0, t * 0.5f, 300);
                if (el < 0) return;
                el_x2[el] = e[0] + ex * s1; el_z2[el] = e[1] + ez * s1;
                el_y[el] = s->base; el_top[el] = top;
                el_id_[el] = el_id(s->tx, s->tz, EK_WALL, k, v, pc);
            }
            if (site_is_bastion(s, k, v)) {
                float b = site_bastion_b(s);
                float ox = vx[v] - s->cx, oz = vz[v] - s->cz, ol = w_sqrt(ox * ox + oz * oz);
                int el = el_add(EK_BASTION, own, vx[v] + ox / ol * b, vz[v] + oz / ol * b, b, 600);
                if (el < 0) return;
                el_y[el] = s->base; el_top[el] = site_ring_top(s, 0);
                el_id_[el] = el_id(s->tx, s->tz, EK_BASTION, 0, v, 0);
            } else if (site_corner(s, v, 0)) {
                int el = el_add(EK_TOWER, own, vx[v], vz[v], site_tower_r(t), 450);
                if (el < 0) return;
                el_y[el] = s->base; el_top[el] = top + 2.6f;
                el_id_[el] = el_id(s->tx, s->tz, EK_TOWER, k, v, 0);
            }
        }
        for (g = 0; g < s->ngates; g++) {
            float gx, gz;
            site_ring_point(s, k, s->gate_a[g], &gx, &gz);
            int el = el_add(EK_GATE, own, gx, gz, GATE_HALF + 2.0f * site_tower_r(t) * 0.85f, 600);
            if (el < 0) return;
            el_y[el] = s->base; el_top[el] = top + 0.6f;
            el_id_[el] = el_id(s->tx, s->tz, EK_GATE, k, g, 0);
        }
    }
}

/* A finished player/AI-built wall, tower or gatehouse (structure p). */
static void el_register_ps(int own, int p) {
    int kind = g_ps_kind[p], el;
    if (kind == ST_WALL) {
        float ex = g_ps_x2[p] - g_ps_x[p], ez = g_ps_z2[p] - g_ps_z[p];
        int hp = (int)(50.0f * w_sqrt(ex * ex + ez * ez)) + 100;
        el = el_add(EK_WALL, own, g_ps_x[p], g_ps_z[p], PS_WALL_T * 0.5f, hp);
        if (el < 0) return;
        el_x2[el] = g_ps_x2[p]; el_z2[el] = g_ps_z2[p];
    } else if (kind == ST_TOWER) el = el_add(EK_TOWER, own, g_ps_x[p], g_ps_z[p], PS_TOWER_R, 450);
    else if (kind == ST_GATE) el = el_add(EK_GATE, own, g_ps_x[p], g_ps_z[p], PS_GATE_HALF + 2.0f * PS_GATE_TR, 600);
    else return;
    if (el < 0) return;
    el_y[el] = g_ps_lo[p]; el_top[el] = g_ps_y[p] + PS_WALL_H;
    el_ps[el] = p;
}

static int team_of(int own) { return own == 0 ? TEAM_PLAYER : TEAM_ENEMY; }

/* Distance from (x,z) to element e's footprint. */
static float el_dist(int e, float x, float z) {
    if (el_kind[e] == EK_WALL) return st_seg_dist(x, z, el_x[e], el_z[e], el_x2[e], el_z2[e]) - el_r[e];
    float dx = x - el_x[e], dz = z - el_z[e];
    return w_sqrt(dx * dx + dz * dz) - el_r[e];
}

/* Distance from (x,z) to building b's footprint. */
static float hb_dist(int b, float x, float z) {
    float a = g_hb_yaw[b] - 0.5f * W_PI, c = w_cos(a), sn = w_sin(a);
    float dx = x - g_hb_x[b], dz = z - g_hb_z[b];
    float lx = dx * c + dz * sn, lz = -dx * sn + dz * c;
    float ox = w_absf(lx) - g_hb_hx[b], oz = w_absf(lz) - g_hb_hz[b];
    if (ox < 0.0f) ox = 0.0f;
    if (oz < 0.0f) oz = 0.0f;
    return w_sqrt(ox * ox + oz * oz);
}

/* Queues every loaded chunk overlapping [x0,x1] x [z0,z1] for regeneration. */
static void rebuild_box(float x0, float z0, float x1, float z1) {
    int cx0 = w_ifloor(x0 / W_CHUNK_SIZE), cx1 = w_ifloor(x1 / W_CHUNK_SIZE);
    int cz0 = w_ifloor(z0 / W_CHUNK_SIZE), cz1 = w_ifloor(z1 / W_CHUNK_SIZE), cx, cz;
    for (cz = cz0; cz <= cz1; cz++)
        for (cx = cx0; cx <= cx1; cx++) request_rebuild_at(((float)cx + 0.5f) * W_CHUNK_SIZE, ((float)cz + 0.5f) * W_CHUNK_SIZE);
}

static void destroyed_add(uint32_t id) {
    if (g_ndestroyed < DESTROY_MAX) { g_destroyed[g_ndestroyed] = id; g_ndestroyed = g_ndestroyed + 1; }
}

static void el_damage(int e, int dmg) {
    if (el_dead[e]) return;
    el_hp[e] = el_hp[e] - dmg;
    if (el_hp[e] > 0) return;
    el_hp[e] = 0; el_dead[e] = 1;
    if (el_ps[e] >= 0) g_ps_dead[el_ps[e]] = 1; else destroyed_add(el_id_[e]);
    float x0 = el_x[e] < el_x2[e] ? el_x[e] : el_x2[e], x1 = el_x[e] < el_x2[e] ? el_x2[e] : el_x[e];
    float z0 = el_z[e] < el_z2[e] ? el_z[e] : el_z2[e], z1 = el_z[e] < el_z2[e] ? el_z2[e] : el_z[e];
    rebuild_box(x0 - el_r[e] - 4.0f, z0 - el_r[e] - 4.0f, x1 + el_r[e] + 4.0f, z1 + el_r[e] + 4.0f);
    __android_log_print(4, "castles", "%s of faction %d knocked down at %d,%d",
                        el_kind[e] == EK_WALL ? "wall section" : (el_kind[e] == EK_TOWER ? "tower" : (el_kind[e] == EK_GATE ? "gatehouse" : "bastion")),
                        el_own[e], (int)el_x[e], (int)el_z[e]);
}

static int building_maxhp(int type) {
    if (type == BT_TOWNCENTER) return 800;
    if (type == BT_BARRACKS) return 400;
    if (type == BT_HOUSE) return 180;
    if (type == BT_DOCK) return 250;
    if (type == BT_SMITH || type == BT_WORKSHOP) return 300;
    if (type == BT_STOCKPILE) return 150;
    return 0;                                   /* stalls: scenery */
}

static void hb_damage(int b, int dmg) {
    if (g_hb_dead[b] || g_hb_maxhp[b] <= 0) return;
    g_hb_hp[b] = g_hb_hp[b] - dmg;
    if (g_hb_hp[b] > 0) return;
    g_hb_hp[b] = 0; g_hb_dead[b] = 1;
    int own = g_hb_own[b];
    if (g_hb_done[b] && g_hb_t[b] == BT_HOUSE && own >= 0) f_houses[own] = f_houses[own] - 1;
    g_hb_done[b] = 0; g_bq_n[b] = 0; g_rs_tech[b] = 0;
    if (g_hb_ps[b] >= 0) g_ps_dead[g_hb_ps[b]] = 1; else destroyed_add(g_hb_id[b]);
    float r = g_hb_hx[b] + g_hb_hz[b] + 4.0f;
    rebuild_box(g_hb_x[b] - r, g_hb_z[b] - r, g_hb_x[b] + r, g_hb_z[b] + r);
    if (g_sel_building == b) g_sel_building = -1;
    __android_log_print(4, "castles", "%s of faction %d destroyed at %d,%d",
                        g_hb_t[b] == BT_TOWNCENTER ? "town centre" : (g_hb_t[b] == BT_BARRACKS ? "barracks" : (g_hb_t[b] == BT_HOUSE ? "house" : "building")),
                        own, (int)g_hb_x[b], (int)g_hb_z[b]);
}

/* Smashes every structure of the other team within `radius` of (x,z)
 * (and below its top). */
static void structure_hit(float x, float z, float y, float radius, int dmg, int team) {
    int e, b;
    for (e = 0; e < g_el_n; e++) {
        if (el_dead[e] || team_of(el_own[e]) == team) continue;
        if (y > el_top[e] + 2.0f) continue;
        if (el_dist(e, x, z) <= radius) el_damage(e, dmg);
    }
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_dead[b] || g_hb_maxhp[b] <= 0 || g_hb_own[b] < 0 || team_of(g_hb_own[b]) == team) continue;
        if (hb_dist(b, x, z) <= radius) hb_damage(b, dmg);
    }
}

/* ---- targets: > 0 element + 1, < 0 -(building + 1) ---- */
static int tgt_alive(int t) {
    if (t > 0) return !el_dead[t - 1];
    if (t < 0) return !g_hb_dead[-t - 1] && g_hb_maxhp[-t - 1] > 0;
    return 0;
}
static float tgt_dist(int t, float x, float z) { return t > 0 ? el_dist(t - 1, x, z) : hb_dist(-t - 1, x, z); }
static void tgt_aim(int t, float fx, float fz, float *p) {
    if (t > 0) {
        int e = t - 1;
        float cx = el_x[e], cz = el_z[e];
        if (el_kind[e] == EK_WALL) {
            /* the point of the wall nearest the shooter */
            float ex = el_x2[e] - el_x[e], ez = el_z2[e] - el_z[e];
            float l2 = ex * ex + ez * ez;
            float u = l2 > 1e-6f ? ((fx - el_x[e]) * ex + (fz - el_z[e]) * ez) / l2 : 0.5f;
            u = w_clampf(u, 0.1f, 0.9f);
            cx = el_x[e] + ex * u; cz = el_z[e] + ez * u;
        }
        p[0] = cx; p[1] = el_y[e] + (el_top[e] - el_y[e]) * 0.55f; p[2] = cz;
        return;
    }
    int b = -t - 1;
    p[0] = g_hb_x[b]; p[1] = g_hb_y[b] + 1.6f; p[2] = g_hb_z[b];
}
static int tgt_own(int t) { return t > 0 ? el_own[t - 1] : g_hb_own[-t - 1]; }

/* Nearest enemy structure of unit i within range r (footprint distance). */
static int siege_find_target(int i, float r) {
    int e, b, best = 0;
    float bd = r;
    for (e = 0; e < g_el_n; e++) {
        if (el_dead[e] || team_of(el_own[e]) == u_team[i]) continue;
        float dx = el_x[e] - u_x[i], dz = el_z[e] - u_z[i];
        if (dx * dx + dz * dz > (r + 40.0f) * (r + 40.0f)) continue;
        float d = el_dist(e, u_x[i], u_z[i]);
        if (d < bd) { bd = d; best = e + 1; }
    }
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_dead[b] || g_hb_maxhp[b] <= 0 || g_hb_own[b] < 0 || team_of(g_hb_own[b]) == u_team[i]) continue;
        float d = hb_dist(b, u_x[i], u_z[i]);
        if (d < bd) { bd = d; best = -(b + 1); }
    }
    return best;
}

/* The enemy structure at ground point (x,z) (within 2.5), or 0. */
static int structure_at(float x, float z, int team) {
    int e, b, best = 0;
    float bd = 2.5f;
    for (e = 0; e < g_el_n; e++) {
        if (el_dead[e] || team_of(el_own[e]) == team) continue;
        float d = el_dist(e, x, z);
        if (d < bd) { bd = d; best = e + 1; }
    }
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_dead[b] || g_hb_maxhp[b] <= 0 || g_hb_own[b] < 0 || team_of(g_hb_own[b]) == team) continue;
        float d = hb_dist(b, x, z);
        if (d < bd) { bd = d; best = -(b + 1); }
    }
    return best;
}

/* ---- siege engines ---- */
#define SG_PACKED    0
#define SG_DEPLOYING 1
#define SG_DEPLOYED  2
#define SG_PACKING   3
#define SG_SETUP_T   6.0f

static int is_siege(int i) { return u_class[i] == UC_CATAPULT || u_class[i] == UC_RAM || u_class[i] == UC_TREB; }
static float siege_range(int c) { return c == UC_TREB ? 75.0f : (c == UC_CATAPULT ? 34.0f : 1.6f); }
static float siege_minrange(int c) { return c == UC_TREB ? 16.0f : (c == UC_CATAPULT ? 5.0f : 0.0f); }

/* Moves engine i toward (x,z) on a ground field. */
static void siege_goto(int i, float x, float z) {
    u_gx[i] = x; u_gz[i] = z; u_gy[i] = nav_surface(0, x, z); u_gl[i] = 0;
    u_order[i] = ORD_MOVE; u_target[i] = -1;
    unit_release_field(i);
    u_repath[i] = 1; u_state[i] = US_WAIT;
}

/* Throws a stone from engine i at p (solved with the arrow integrator,
 * lofted arcs first so it clears walls). */
static void siege_fire(int i, const float *p) {
    int c = u_class[i], k, a = -1, ang;
    float from[3], f[3];
    float mass = c == UC_TREB ? 12.0f : 6.0f;
    float fmax = c == UC_TREB ? 80000.0f : 30000.0f;
    from[0] = u_x[i] + w_sin(u_yaw[i]) * 0.8f; from[1] = u_y[i] + (c == UC_TREB ? 7.5f : 2.6f); from[2] = u_z[i] - w_cos(u_yaw[i]) * 0.8f;
    bal_projectile(mass, fmax);
    int ok = 0;
    for (ang = BAL_ANGLES - 1; ang >= 0 && !ok; ang--) ok = bal_solve_at(from, p, ang, f);
    bal_projectile(0.0f, 0.0f);
    if (!ok) { u_st[i] = 0; return; }
    for (k = 0; k < MAXP; k++) if (!p_active[k]) { a = k; break; }
    if (a < 0) return;
    bal_reset(a, from[0], from[1], from[2], mass);
    bal_add_force(a, f[0], f[1], f[2]);
    p_active[a] = 1; a_flying[a] = 1; a_life[a] = 0.0f;
    a_team[a] = u_team[i];
    a_kind[a] = c == UC_TREB ? 2 : 1;
    a_dmg[a] = c == UC_TREB ? 4 : 3;           /* to units it lands on */
    a_sdmg[a] = c == UC_TREB ? 90 : 45;        /* to structures       */
    float fm = w_sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    a_dx[a] = f[0] / fm; a_dy[a] = f[1] / fm; a_dz[a] = f[2] / fm;
}

/* A stone landing (or hitting something) at p: smashes structures and
 * hurts units around it. */
static void siege_impact(int a, float x, float y, float z) {
    int j, team = a_team[a];
    float r = a_kind[a] == 2 ? 3.0f : 2.4f;
    structure_hit(x, z, y, r, a_sdmg[a], team);
    for (j = 0; j < g_un; j++) {
        if (!unit_alive(j) || u_team[j] == team) continue;
        float dx = u_x[j] - x, dz = u_z[j] - z, dy = u_y[j] - y;
        if (dx * dx + dz * dz < r * r && dy > -3.0f && dy < 3.0f) unit_damage(j, a_dmg[a]);
    }
}

/* Per frame for a loaded siege engine. */
static void siege_update(int i, float dt) {
    int c = u_class[i];
    /* trebuchet setting up / packing up */
    if (c == UC_TREB && (u_sstate[i] == SG_DEPLOYING || u_sstate[i] == SG_PACKING)) {
        float nt = u_st_t[i] + dt;
        u_st_t[i] = nt;
        u_state[i] = US_IDLE;
        if (nt >= SG_SETUP_T) {
            u_sstate[i] = u_sstate[i] == SG_DEPLOYING ? SG_DEPLOYED : SG_PACKED;
            if (u_sstate[i] == SG_PACKED && u_spend[i]) { u_spend[i] = 0; u_order[i] = ORD_MOVE; u_repath[i] = 1; u_state[i] = US_WAIT; }
        }
        return;
    }
    /* a set-up trebuchet can't move: pack first, then go */
    if (c == UC_TREB && u_sstate[i] == SG_DEPLOYED && (u_repath[i] || u_state[i] == US_MOVE || u_state[i] == US_WAIT)) {
        unit_release_field(i);
        u_repath[i] = 0; u_spend[i] = 1;
        u_sstate[i] = SG_PACKING; u_st_t[i] = 0.0f; u_state[i] = US_IDLE;
        return;
    }
    if (u_state[i] == US_WAIT && u_field[i] >= 0 && g_field_state[u_field[i]] == FS_READY) u_state[i] = US_MOVE;
    if (u_amove[i] && u_state[i] == US_MOVE && u_field[i] < 0 && !u_repath[i]) { u_repath[i] = 1; u_state[i] = US_WAIT; }
    if (u_state[i] == US_MOVE) {
        unit_follow_field(i, dt);
        /* attack-move (a wave): stop for the first structure in reach */
        if (u_amove[i] && !u_st[i]) {
            int t = siege_find_target(i, c == UC_RAM ? 10.0f : siege_range(c) * 0.9f);
            if (t) { u_st[i] = t; unit_release_field(i); u_state[i] = US_IDLE; }
        }
        if (!u_st[i] || u_order[i] == ORD_MOVE) return;
    }
    if (u_state[i] == US_WAIT) return;
    if (u_st[i] && !tgt_alive(u_st[i])) u_st[i] = 0;
    int ut = u_target[i];
    if (ut >= 0 && !unit_alive(ut)) { u_target[i] = -1; ut = -1; }
    /* find something to hit */
    if (!u_st[i] && ut < 0) {
        int t = siege_find_target(i, c == UC_RAM ? 8.0f : siege_range(c));
        if (t) u_st[i] = t;
        else if (c == UC_CATAPULT) {
            int e = unit_find_enemy(i, 28.0f, 10.0f);
            if (e >= 0 && u_class[e] != UC_BOAT) { u_target[i] = e; ut = e; }
        }
    }
    if (!u_st[i] && ut < 0) {
        if (u_state[i] == US_ATTACK) { u_state[i] = US_IDLE; u_swing[i] = 0.0f; }
        /* nothing in reach: an attack wave marches on */
        if (u_amove[i] && u_state[i] == US_IDLE) {
            float gdx = u_gx[i] - u_x[i], gdz = u_gz[i] - u_z[i];
            if (gdx * gdx + gdz * gdz > 8.0f * 8.0f) {
                if (c == UC_TREB && u_sstate[i] == SG_DEPLOYED) { u_sstate[i] = SG_PACKING; u_st_t[i] = 0.0f; u_spend[i] = 1; }
                else { u_order[i] = ORD_MOVE; u_repath[i] = 1; u_state[i] = US_WAIT; }
            }
        }
        if (c == UC_TREB && u_sstate[i] == SG_DEPLOYED) { float nw = u_swing[i] * 0.9f; u_swing[i] = nw; }
        return;
    }
    float p[3];
    float d;
    if (u_st[i]) { tgt_aim(u_st[i], u_x[i], u_z[i], p); d = tgt_dist(u_st[i], u_x[i], u_z[i]); }
    else { p[0] = u_x[ut]; p[1] = u_y[ut] + 0.8f; p[2] = u_z[ut]; float dx = p[0] - u_x[i], dz = p[2] - u_z[i]; d = w_sqrt(dx * dx + dz * dz); }
    float ax = p[0] - u_x[i], az = p[2] - u_z[i];
    if (c == UC_RAM) {
        if (d > 1.6f) {
            /* roll up to it: straight when close, on a field otherwise */
            if (d < 12.0f) {
                float l = w_sqrt(ax * ax + az * az);
                unit_face(i, ax, az, dt);
                float ox = u_x[i], oz = u_z[i];
                unit_walk(i, ax / l * u_speed[i] * dt, az / l * u_speed[i] * dt, 0);
                if ((u_x[i] - ox) * (u_x[i] - ox) + (u_z[i] - oz) * (u_z[i] - oz) < 1e-6f) { float ns = u_stuck[i] + dt; u_stuck[i] = ns; }
                else u_stuck[i] = 0.0f;
                u_state[i] = US_MOVE;
                if (u_stuck[i] > 2.0f) { u_stuck[i] = 0.0f; if (d < 3.0f) { u_state[i] = US_ATTACK; } else siege_goto(i, p[0], p[2]); }
                if (u_state[i] != US_ATTACK) return;
            } else { siege_goto(i, p[0], p[2]); return; }
        }
        u_state[i] = US_ATTACK;
        unit_face(i, ax, az, dt);
        float prev = u_swing[i], ns = prev + dt / 2.2f;
        u_swing[i] = ns;
        if (prev < 0.55f && ns >= 0.55f && u_st[i]) {
            if (u_st[i] > 0) el_damage(u_st[i] - 1, 40); else hb_damage(-u_st[i] - 1, 40);
        }
        if (ns >= 1.0f) u_swing[i] = 0.0f;
        return;
    }
    /* catapult / trebuchet */
    float rng = siege_range(c), mr = siege_minrange(c);
    if (d > rng || d < mr) {
        if (c == UC_TREB && u_sstate[i] == SG_DEPLOYED) {
            if (d < mr) { u_st[i] = 0; return; }
            u_sstate[i] = SG_PACKING; u_st_t[i] = 0.0f; u_spend[i] = 0; return;
        }
        if (d < mr) { u_st[i] = 0; u_target[i] = -1; return; }
        /* close in to ~80% of range */
        float l = w_sqrt(ax * ax + az * az);
        float want = l - rng * 0.8f;
        siege_goto(i, u_x[i] + ax / l * want, u_z[i] + az / l * want);
        return;
    }
    u_state[i] = US_IDLE;
    unit_face(i, ax, az, dt);
    if (c == UC_TREB && u_sstate[i] != SG_DEPLOYED) { u_sstate[i] = SG_DEPLOYING; u_st_t[i] = 0.0f; return; }
    float period = c == UC_TREB ? 9.0f : 4.5f;
    float prev = u_swing[i], ns = prev + dt / period;
    u_swing[i] = ns;
    if (prev < 0.35f && ns >= 0.35f) siege_fire(i, p);
    if (ns >= 1.0f) u_swing[i] = 0.0f;
}

#endif /* CASTLES_SIEGE_H */
