/* ---- castles_ai.h: an AI for each enemy castle ----
 *
 * The enemy castles nearest the player's (up to F_MAX - 1) each run a
 * faction AI with the same economy the player has: peasants gathering
 * wood and stone, fishing boats bringing in food, peasants building
 * houses / barracks / a blacksmith / a dock, units trained at the town
 * centre, barracks and dock, advancements researched -- and, once its
 * army is big enough, waves of soldiers sent to attack the player.
 *
 *   Decision: every so often the AI plans each candidate advancement
 *   path (rush, archers, men-at-arms, economic boom, drilled army) with
 *   the A* planner (castles_planner.h) from its current state, scores
 *   each by value (personality, game phase, what it already has) over
 *   planned time, and adopts the best plan.
 *   Execution: it walks the plan, doing each train / build / research
 *   step as soon as it can afford it; the gather steps in the plan tell
 *   it what its upcoming spending needs, so peasants are split between
 *   wood, stone and building in proportion to the shortfall. Boats
 *   always fish.
 *   Off screen: enemy castles are usually far outside the loaded
 *   chunks, so their economy runs on rates (per gathering peasant, per
 *   boat, plus the market) whether or not anyone watches, and
 *   construction advances by the number of builders. Units still exist
 *   (and can be killed); an attack wave walks straight across unloaded
 *   land (steering round water) and switches to GPU pathfinding as it
 *   comes into the loaded area. When an AI castle is on screen its
 *   peasants and boats really walk to trees / rocks / shoals and
 *   building sites with the same jobs and flow fields the player's use.
 *
 * Included by castles_units.h just before units_update(). */

#ifndef CASTLES_AI_H
#define CASTLES_AI_H

#define AI_LOTS      12
#define PLAN_MAX     48
#define AI_GOALS     6
#define AI_G_RUSH    0
#define AI_G_ARCH    1
#define AI_G_MAA     2
#define AI_G_BOOM    3
#define AI_G_DRILL   4
#define AI_G_SIEGE   5

#define AI_RATE_WOOD   0.33f     /* per gathering peasant per second */
#define AI_RATE_STONE  0.26f
#define AI_RATE_FISH   0.55f     /* per boat per second              */

static int f_pers[F_MAX];                 /* 0 aggressive, 1 tactician, 2 builder */
static int f_nlot[F_MAX];
static float f_lot_x[F_MAX * AI_LOTS], f_lot_z[F_MAX * AI_LOTS], f_lot_yaw[F_MAX * AI_LOTS];
static int f_lot_used[F_MAX * AI_LOTS];
static int f_dock_ok[F_MAX], f_dock_used[F_MAX];
static float f_dock_x[F_MAX], f_dock_z[F_MAX], f_dock_y[F_MAX], f_dock_yaw[F_MAX];
static int f_plan[F_MAX * PLAN_MAX];
static int f_plan_n[F_MAX], f_plan_i[F_MAX], f_goal[F_MAX], f_plan_ok[F_MAX];
static float f_plan_time[F_MAX], f_replan_t[F_MAX], f_wait_t[F_MAX];
static int f_dec_stage[F_MAX], f_dec_best[F_MAX], f_decisions[F_MAX];
static float f_dec_bestU[F_MAX], f_dec_util[F_MAX * AI_GOALS];
static int f_dec_start[F_MAX * PV_N];
static int f_mixn[F_MAX];
static float f_tick_t[F_MAX], f_role_t[F_MAX], f_sync_t[F_MAX], f_log_t[F_MAX], f_wave_t[F_MAX];
static int f_waves[F_MAX];
/* role counts from the last tick */
static int f_nw[F_MAX], f_ns[F_MAX], f_nbld[F_MAX], f_nboat[F_MAX], f_npeas[F_MAX], f_narmy[F_MAX], f_ngar[F_MAX], f_nsiege[F_MAX], f_ndragon[F_MAX];
/* taming: the dragon its peasants have gone to (-1 none) and the time left */
static int f_tame_d[F_MAX];
static float f_tame_t[F_MAX];

static const char *ai_goal_name(int g) {
    if (g == AI_G_RUSH) return "rush";
    if (g == AI_G_ARCH) return "archers";
    if (g == AI_G_MAA) return "men-at-arms";
    if (g == AI_G_BOOM) return "economy";
    if (g == AI_G_SIEGE) return "siege";
    return "drilled army";
}

static int faction_at_site(int tx, int tz) {
    int f;
    for (f = 1; f < F_MAX; f++) if (f_on[f] && f_site[f].tx == tx && f_site[f].tz == tz) return f;
    return -1;
}

/* Market income every castle gets (food per second). */
static float ai_market_food(int f) { return 0.25f + 0.02f * (float)f_houses[f]; }

/* ---- lots outside the walls ---- */
/* Barracks-sized lots (with the yard) on the castle's flat clearing,
 * outside the outer curtain wall and its bastions, clear of the gate
 * roads, front facing the castle. */
static int ai_lot_fits(int f, float x, float z, float yaw, const float *bx, const float *bz, int nb) {
    WSite *s = &f_site[f];
    float a = yaw - 0.5f * W_PI, c = w_cos(a), sn = w_sin(a);
    float t0 = site_ring_t(s, 0);
    int i, j, k, g;
    for (j = 0; j <= 4; j++) {
        for (i = 0; i <= 4; i++) {
            float lx = -PS_BARR_HX - 0.8f + (2.0f * PS_BARR_HX + 1.6f) * (float)i / 4.0f;
            float lz = -PS_BARR_HZ - 0.8f + (2.0f * PS_BARR_HZ + 4.6f) * (float)j / 4.0f;
            float px = x + lx * c - lz * sn, pz = z + lx * sn + lz * c;
            float dx = px - s->cx, dz = pz - s->cz;
            if (dx * dx + dz * dz > (s->Rc - 1.0f) * (s->Rc - 1.0f)) return 0;
            int inside;
            float d = site_ring_dist(s, 0, px, pz, &inside);
            if (inside || d < t0 * 0.5f + 1.5f) return 0;
            for (k = 0; k < nb; k++) if (site_poly_inside(bx + k * 6, bz + k * 6, 6, 6, px, pz) > -1.2f) return 0;
            for (g = 0; g < s->ngates; g++) {
                float gx = w_cos(s->gate_a[g]), gz = w_sin(s->gate_a[g]);
                float along = dx * gx + dz * gz, perp = dx * gz - dz * gx;
                if (along > 0.0f && perp < 3.8f && perp > -3.8f) return 0;
            }
        }
    }
    for (k = 0; k < f_nlot[f]; k++) {
        float ex = f_lot_x[f * AI_LOTS + k] - x, ez = f_lot_z[f * AI_LOTS + k] - z;
        if (ex * ex + ez * ez < 11.5f * 11.5f) return 0;
    }
    return 1;
}

static void ai_lots(int f) {
    WSite *s = &f_site[f];
    float vx[SITE_MAXV], vz[SITE_MAXV], bx[SITE_MAXV * 6], bz[SITE_MAXV * 6];
    int nv = site_ring_verts(s, 0, vx, vz), nb = 0, i;
    if (s->bastion) for (i = 0; i < nv; i++) if (site_is_bastion(s, 0, i)) { site_bastion_poly(s, vx, vz, nv, i, bx + nb * 6, bz + nb * 6); nb++; }
    f_nlot[f] = 0;
    float a;
    for (a = 0.0f; a < 2.0f * W_PI && f_nlot[f] < AI_LOTS; a = a + 0.06f) {
        float r;
        for (r = s->Rc - 5.0f; r > s->R * 0.5f; r = r - 1.0f) {
            float x = s->cx + w_cos(a) * r, z = s->cz + w_sin(a) * r;
            float yaw = approx_atan2_w(s->cz - z, s->cx - x);         /* front toward the castle */
            if (!ai_lot_fits(f, x, z, yaw, bx, bz, nb)) continue;
            int k = f * AI_LOTS + f_nlot[f];
            f_lot_x[k] = x; f_lot_z[k] = z; f_lot_yaw[k] = yaw; f_lot_used[k] = 0;
            f_nlot[f] = f_nlot[f] + 1;
            break;
        }
    }
}

/* A shore spot for a dock within ~100 of the clearing (raw heights,
 * so it works without the land being loaded). */
static void ai_find_shore(int f) {
    WSite *s = &f_site[f];
    float d, a;
    f_dock_ok[f] = 0; f_dock_used[f] = 0;
    for (d = s->Rc + s->blend + 4.0f; d < s->Rc + s->blend + 100.0f; d = d + 3.0f) {
        for (a = 0.0f; a < 2.0f * W_PI; a = a + 0.12f) {
            float px = s->cx + w_cos(a) * d, pz = s->cz + w_sin(a) * d;
            float h = w_height_raw(px, pz);
            if (h < 0.3f || h > 3.0f) continue;
            int k;
            for (k = 0; k < 16; k++) {
                float b = (float)k * (W_PI / 8.0f), dx = w_cos(b), dz = w_sin(b);
                if (w_height_raw(px - dx * 1.8f, pz - dz * 1.8f) < 0.2f) continue;
                if (w_height_raw(px + dx * 2.5f, pz + dz * 2.5f) > -0.9f) continue;
                if (w_height_raw(px + dx * 4.0f, pz + dz * 4.0f) > -0.9f) continue;
                if (w_height_raw(px + dx * 5.5f, pz + dz * 5.5f) > -0.9f) continue;
                if (w_height_raw(px + dx * 7.0f, pz + dz * 7.0f) > -1.0f) continue;
                f_dock_ok[f] = 1;
                f_dock_x[f] = px; f_dock_z[f] = pz; f_dock_y[f] = h; f_dock_yaw[f] = b;
                return;
            }
        }
    }
}

/* ---- setup ---- */
static void ai_spawn_at(int f, int b, int cls, int k) {
    float x, z;
    building_door(b, 1.4f + 0.6f * (float)(k % 4), &x, &z);
    float ox = (float)((k * 7) % 5 - 2) * 0.7f;
    int u = unit_spawn(TEAM_ENEMY, x + ox, z, 0, w_hash2(f * 97 + k, b, 31u), cls, f);
    if (u < 0) return;
    if (cls == UC_PEASANT || cls == UC_BOAT) peasant_to_manual(u);
    ai_unit_trained(f, u);
}

/* Picks the enemy castles nearest home and sets each one up as an AI
 * faction (its buildings from the castle layout, a few peasants and
 * soldiers, building lots, a shore spot). */
static void ai_init(void) {
    int tx, tz, k, f;
    int ctx[24], ctz[24], cn = 0;
    float cd[24];
    if (!g_home_set) return;
    for (tz = g_hsite.tz - 3; tz <= g_hsite.tz + 3; tz++) {
        for (tx = g_hsite.tx - 3; tx <= g_hsite.tx + 3; tx++) {
            if (tx == g_hsite.tx && tz == g_hsite.tz) continue;
            WSite s;
            w_site_compute(tx, tz, &s);
            if (!s.valid || cn >= 24) continue;
            float dx = s.cx - g_hsite.cx, dz = s.cz - g_hsite.cz;
            ctx[cn] = tx; ctz[cn] = tz; cd[cn] = dx * dx + dz * dz;
            cn++;
        }
    }
    for (f = 1; f < F_MAX; f++) {
        int best = -1;
        for (k = 0; k < cn; k++) if (cd[k] >= 0.0f && (best < 0 || cd[k] < cd[best])) best = k;
        if (best < 0) break;
        cd[best] = -1.0f;
        w_site_compute(ctx[best], ctz[best], &f_site[f]);
        WSite *s = &f_site[f];
        /* buildings from the castle's layout (stalls are just scenery) */
        int bt[SITE_MAXB];
        float bx[SITE_MAXB], bz[SITE_MAXB], byaw[SITE_MAXB], bhx[SITE_MAXB], bhz[SITE_MAXB];
        int nb = site_layout(s, bt, bx, bz, byaw, bhx, bhz), b;
        f_tc[f] = -1; f_houses[f] = 0;
        g_add_own = f;
        for (b = 0; b < nb; b++) {
            if (bt[b] == BT_STALL) continue;
            int hb = home_add_building(bt[b], bx[b], bz[b], s->base, byaw[b], bhx[b], bhz[b], 1);
            if (hb < 0) break;
            if (bt[b] == BT_TOWNCENTER) f_tc[f] = hb;
            if (bt[b] == BT_HOUSE) f_houses[f]++;
        }
        g_add_own = 0;
        if (f_tc[f] < 0) continue;
        f_on[f] = 1;
        f_wood[f] = 150; f_stone[f] = 120; f_food[f] = 150;
        f_tech[f] = 0;
        f_pers[f] = (int)(w_hash2(s->tx, s->tz, 0xA1u) % 3u);
        f_plan_n[f] = 0; f_plan_i[f] = 0; f_dec_stage[f] = 0; f_replan_t[f] = 0.0f; f_tame_d[f] = -1;
        f_wave_t[f] = 0.0f; f_waves[f] = 0; f_mixn[f] = 0;
        f_log_t[f] = 25.0f - 5.0f * (float)f;
        ai_lots(f);
        ai_find_shore(f);
        el_register_site(f, s);
        /* starting folk: peasants at the town centre, a few soldiers */
        for (k = 0; k < 5; k++) ai_spawn_at(f, f_tc[f], UC_PEASANT, k);
        for (k = 0; k < 3; k++) ai_spawn_at(f, f_tc[f], k == 2 ? UC_BOW : UC_SWORD, k + 5);
        __android_log_print(4, "castles", "AI castle %d: tile %d,%d at %d,%d radius %d, %d buildings (%d houses), %d lots, %s, personality %s",
                            f, s->tx, s->tz, (int)s->cx, (int)s->cz, (int)s->R, nb, f_houses[f], f_nlot[f],
                            f_dock_ok[f] ? "shore for a dock" : "no shore", f_pers[f] == 0 ? "aggressive" : (f_pers[f] == 1 ? "tactician" : "builder"));
    }
}

/* A unit the faction has just trained or spawned: give it its role. */
static void ai_unit_trained(int f, int u) {
    int c = u_class[u];
    if (c == UC_BOAT) u_role[u] = R_FISH;
    else if (c == UC_PEASANT) u_role[u] = R_NONE;
    else u_role[u] = R_ARMY;
    (void)f;
}

/* ---- planning ---- */
static int ai_siege_class(int k) { return k % 3 == 0 ? UC_CATAPULT : (k % 3 == 1 ? UC_RAM : UC_TREB); }
static int ai_goal_class(int g, int k) {
    if (g == AI_G_RUSH) return (k & 1) ? UC_SWORD : UC_MACE;
    if (g == AI_G_ARCH) return (k % 4 == 3) ? UC_SWORD : UC_BOW;
    if (g == AI_G_MAA) return (k & 1) ? UC_MACE : UC_SWORD;
    if (g == AI_G_DRILL) return k % 3 == 0 ? UC_SWORD : (k % 3 == 1 ? UC_BOW : UC_MACE);
    return UC_SWORD;
}

static int ai_count_own_bt(int f, int type, int doneOnly) {
    int b, n = 0;
    for (b = 0; b < g_hb_n; b++) if (g_hb_own[b] == f && g_hb_t[b] == type && (g_hb_done[b] || !doneOnly)) n++;
    return n;
}

/* The faction's current state for the planner (+ context pc_*). */
static void ai_plan_state(int f, int *s) {
    int k, i;
    for (k = 0; k < PV_N; k++) s[k] = 0;
    s[PV_W] = f_wood[f] / 10 > PV_RCAP ? PV_RCAP : f_wood[f] / 10;
    s[PV_S] = f_stone[f] / 10 > PV_RCAP ? PV_RCAP : f_stone[f] / 10;
    s[PV_F] = f_food[f] / 10 > PV_RCAP ? PV_RCAP : f_food[f] / 10;
    int garrison = 0;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_own[i] != f) continue;
        if (u_class[i] == UC_DRAGON) { garrison++; continue; }    /* takes pop room, not planned */
        if (u_class[i] == UC_PEASANT) s[PV_NP]++;
        else if (u_class[i] == UC_BOAT) s[PV_NB]++;
        else if (is_siege(i)) s[PV_NG]++;
        else if (u_role[i] == R_ARMY || u_role[i] == R_WAVE) s[PV_NS]++;     /* the wave counts against the army cap */
        else garrison++;
    }
    /* queued units count as trained */
    int b;
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_own[b] != f) continue;
        for (k = 0; k < g_bq_n[b]; k++) {
            int c = g_bq_cls[b * BQ_MAX + k];
            if (c == UC_PEASANT) s[PV_NP]++; else if (c == UC_BOAT) s[PV_NB]++;
            else if (c == UC_CATAPULT || c == UC_RAM || c == UC_TREB) s[PV_NG]++; else s[PV_NS]++;
        }
    }
    if (s[PV_NP] > PC_NPMAX) s[PV_NP] = PC_NPMAX;
    if (s[PV_NB] > PC_NBMAX) s[PV_NB] = PC_NBMAX;
    if (s[PV_NS] > PC_NSMAX) s[PV_NS] = PC_NSMAX;
    if (s[PV_NG] > PC_NGMAX) s[PV_NG] = PC_NGMAX;
    s[PV_WS] = ai_count_own_bt(f, BT_WORKSHOP, 0);
    /* buildings under construction count as built (builders are on it) */
    s[PV_BR] = ai_count_own_bt(f, BT_BARRACKS, 0);
    s[PV_DK] = ai_count_own_bt(f, BT_DOCK, 0);
    s[PV_SM] = ai_count_own_bt(f, BT_SMITH, 0);
    s[PV_T] = f_tech[f];
    for (b = 0; b < g_hb_n; b++) if (g_hb_own[b] == f && g_rs_tech[b]) s[PV_T] = s[PV_T] | (1 << (g_rs_tech[b] - 1));
    float tm = has_tech(f, T_TOOLS) ? 1.3f : 1.0f;
    pc_rw = AI_RATE_WOOD * tm; pc_rs = AI_RATE_STONE * tm;
    pc_rf = AI_RATE_FISH * (has_tech(f, T_NETS) ? 1.5f : 1.0f);
    pc_pf = ai_market_food(f);
    pc_pop0 = pop_limit_of(f) - garrison;
    int freeLots = 0;
    for (k = 0; k < f_nlot[f]; k++) if (!f_lot_used[f * AI_LOTS + k]) freeLots++;
    pc_lots = freeLots;
    pc_br0 = s[PV_BR]; pc_sm0 = s[PV_SM];
    pc_can_dock = f_dock_ok[f] && !f_dock_used[f] && s[PV_DK] == 0;
}

/* Sets the planner goal (pg_*) and soldier costs for goal g from state s. */
static float ai_goal_setup(int f, int g, const int *s) {
    int c[3];
    pg_tech = 0; pg_np = 0; pg_nb = 0; pg_ns = 0; pg_ng = 0;
    {
        int sc[3], k2;
        float tsum = 0.0f;
        pc_sg[0] = 0; pc_sg[1] = 0; pc_sg[2] = 0;
        for (k2 = 0; k2 < 3; k2++) {           /* the mix's average engine */
            unit_cost(ai_siege_class(k2), sc);
            pc_sg[0] = pc_sg[0] + sc[0]; pc_sg[1] = pc_sg[1] + sc[1]; pc_sg[2] = pc_sg[2] + sc[2];
            float nt2 = tsum + train_time_base(ai_siege_class(k2));
            tsum = nt2;
        }
        pc_sg[0] = pl_ceil10(pc_sg[0] / 3); pc_sg[1] = pl_ceil10(pc_sg[1] / 3); pc_sg[2] = pl_ceil10(pc_sg[2] / 3);
        pc_sg_time = tsum / 3.0f;
    }
    float value = 1.0f;
    int cls = ai_goal_class(g, 0);
    if (g == AI_G_RUSH) { pg_ns = s[PV_NS] + 6; value = 1.0f; }
    else if (g == AI_G_ARCH) { pg_tech = 1 << T_FLETCHING; pg_ns = s[PV_NS] + 6; value = 1.2f; }
    else if (g == AI_G_MAA) { pg_tech = (1 << T_FORGING) | (1 << T_ARMOR); pg_ns = s[PV_NS] + 8; value = 1.5f; }
    else if (g == AI_G_DRILL) { pg_tech = 1 << T_DRILL; pg_ns = s[PV_NS] + 8; value = 1.1f; }
    else if (g == AI_G_SIEGE) {
        pg_ng = s[PV_NG] + 2; pg_ns = s[PV_NS] + 4;
        if (pg_ng > PC_NGMAX) pg_ng = PC_NGMAX;
        value = g_time > 360.0f ? 1.6f : 0.3f;          /* walls matter once armies clash */
    }
    else {
        pg_tech = 1 << T_TOOLS;
        pg_np = s[PV_NP] + 4 > 10 ? s[PV_NP] + 4 : 10;
        if (pg_np > PC_NPMAX) pg_np = PC_NPMAX;
        if (s[PV_DK] > 0 || pc_can_dock) { pg_tech = pg_tech | (1 << T_NETS); pg_nb = 2; }
        value = s[PV_NP] < 8 ? 2.0f : (s[PV_NP] < 12 ? 1.0f : 0.3f);
    }
    if (pg_ns > PC_NSMAX) pg_ns = PC_NSMAX;
    unit_cost(cls, c);
    pc_sol[0] = pl_ceil10(c[0]); pc_sol[1] = pl_ceil10(c[1]); pc_sol[2] = pl_ceil10(c[2]);
    pc_sol_time = train_time_base(cls);
    /* personality + game phase */
    if (f_pers[f] == 0) { if (g == AI_G_RUSH) value = value * 1.5f; if (g == AI_G_DRILL || g == AI_G_SIEGE) value = value * 1.3f; }
    if (f_pers[f] == 1) { if (g == AI_G_MAA) value = value * 1.4f; if (g == AI_G_ARCH) value = value * 1.3f; if (g == AI_G_SIEGE) value = value * 1.2f; }
    if (f_pers[f] == 2) { if (g == AI_G_BOOM) value = value * 1.6f; }
    if (g == AI_G_RUSH && g_time < 150.0f && f_pers[f] != 0) value = value * 0.7f;
    /* a little variety between decisions */
    float jit = 0.9f + 0.2f * (float)(w_hash2(f, f_decisions[f] * 7 + g, 0xA17u) & 255u) / 255.0f;
    return value * jit;
}

/* One step of a decision: plans goal f_dec_stage-1 (one A* per call, so
 * a decision is spread over a few frames), then adopts the best. */
static void ai_decide_step(int f) {
    int st = f_dec_stage[f];
    int *s0 = f_dec_start + f * PV_N;
    if (st == 1) { ai_plan_state(f, s0); f_dec_best[f] = -1; f_dec_bestU[f] = -1.0f; }
    int g = st - 1;
    if (g < AI_GOALS) {
        int acts[PLAN_MAX], reached;
        float t;
        ai_plan_state(f, s0);
        float value = ai_goal_setup(f, g, s0);
        int n = plan_astar(s0, acts, PLAN_MAX, &t, &reached);
        float u = value / (t + 40.0f + (reached ? 0.0f : 300.0f));
        if (n == 0) u = -1.0f;
        f_dec_util[f * AI_GOALS + g] = u;
        if (u > f_dec_bestU[f]) {
            f_dec_bestU[f] = u; f_dec_best[f] = g;
            int k;
            for (k = 0; k < n; k++) f_plan[f * PLAN_MAX + k] = acts[k];
            f_plan_n[f] = n; f_plan_time[f] = t; f_plan_ok[f] = reached;
        }
        f_dec_stage[f] = st + 1;
        return;
    }
    /* adopt */
    f_dec_stage[f] = 0;
    f_decisions[f] = f_decisions[f] + 1;
    f_goal[f] = f_dec_best[f];
    f_plan_i[f] = 0; f_replan_t[f] = 0.0f; f_wait_t[f] = 0.0f;
    if (f_goal[f] < 0) { f_plan_n[f] = 0; return; }
    char buf[400];
    int k, p = 0;
    for (k = 0; k < f_plan_n[f] && p < 360; k++) {
        const char *nm = plan_action_name(f_plan[f * PLAN_MAX + k]);
        int q = 0;
        if (k > 0) { buf[p] = ' '; p++; }
        while (nm[q] && p < 390) { buf[p] = nm[q]; p++; q++; }
    }
    buf[p] = 0;
    __android_log_print(4, "castles", "AI castle %d chose %s (%s, %ds; scores rush %d archers %d maa %d economy %d drill %d siege %d)",
                        f, ai_goal_name(f_goal[f]), f_plan_ok[f] ? "full plan" : "partial plan", (int)f_plan_time[f],
                        (int)(f_dec_util[f * AI_GOALS + 0] * 10000.0f), (int)(f_dec_util[f * AI_GOALS + 1] * 10000.0f),
                        (int)(f_dec_util[f * AI_GOALS + 2] * 10000.0f), (int)(f_dec_util[f * AI_GOALS + 3] * 10000.0f),
                        (int)(f_dec_util[f * AI_GOALS + 4] * 10000.0f), (int)(f_dec_util[f * AI_GOALS + 5] * 10000.0f));
    __android_log_print(4, "castles", "AI castle %d plan: %s", f, buf);
}

/* ---- execution ---- */
static int ai_find_building(int f, int type) {
    int b, best = -1, bq = 1 << 20;
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_own[b] != f || g_hb_t[b] != type || !g_hb_done[b]) continue;
        int q = g_bq_n[b] + (g_rs_tech[b] ? 3 : 0);
        if (q < bq) { bq = q; best = b; }
    }
    return best;
}

/* Starts building kind on a free lot (dock: the shore spot). Returns
 * the site, -2 if it can't afford it yet, -1 if impossible. */
static int ai_build_at(int f, int kind);
/* (the placement globals g_pl_* are the player's too -- a placement may be
 * on screen -- so they are put back afterwards) */
static int ai_build(int f, int kind) {
    float sx = g_pl_x, sz = g_pl_z, sx2 = g_pl_x2, sz2 = g_pl_z2, sy = g_pl_y, slo = g_pl_lo, syaw = g_pl_yaw;
    int sv = g_pl_valid;
    int r = ai_build_at(f, kind);
    g_pl_x = sx; g_pl_z = sz; g_pl_x2 = sx2; g_pl_z2 = sz2; g_pl_y = sy; g_pl_lo = slo; g_pl_yaw = syaw;
    g_pl_valid = sv;
    return r;
}
static int ai_build_at(int f, int kind) {
    int k, lot = -1;
    if (kind == ST_DOCK) {
        if (!f_dock_ok[f] || f_dock_used[f]) return -1;
        g_pl_x = f_dock_x[f]; g_pl_z = f_dock_z[f]; g_pl_yaw = f_dock_yaw[f];
        g_pl_y = f_dock_y[f]; g_pl_lo = f_dock_y[f];
    } else {
        for (k = 0; k < f_nlot[f] && lot < 0; k++) if (!f_lot_used[f * AI_LOTS + k]) lot = k;
        if (lot < 0) return -1;
        int q = f * AI_LOTS + lot;
        g_pl_x = f_lot_x[q]; g_pl_z = f_lot_z[q]; g_pl_yaw = f_lot_yaw[q];
        g_pl_y = f_site[f].base; g_pl_lo = f_site[f].base;
        /* the lot is barracks-sized: smaller buildings sit at its back */
        if (kind != ST_BARRACKS) {
            float bkz = kind == ST_HOUSE ? PS_BARR_HZ - PS_HOUSE_HZ : (kind == ST_WORKSHOP ? PS_BARR_HZ - PS_WS_HZ : PS_BARR_HZ - PS_SMITH_HZ);
            g_pl_x = f_lot_x[q] - w_cos(g_pl_yaw) * bkz; g_pl_z = f_lot_z[q] - w_sin(g_pl_yaw) * bkz;
        }
    }
    g_pl_x2 = g_pl_x; g_pl_z2 = g_pl_z;
    int s2 = build_site_create(f, kind);
    if (s2 < 0) return s2;
    if (kind == ST_DOCK) f_dock_used[f] = 1; else f_lot_used[f * AI_LOTS + lot] = 1;
    return s2;
}

/* Tries the plan's next non-gathering action. 1 done, 0 waiting
 * (resources, a queue), -1 impossible now (replan). */
static int ai_try_action(int f, int a) {
    int r, b;
    if (a == PA_PEASANT) {
        r = barracks_train(f_tc[f], UC_PEASANT);
        return r > 0 ? 1 : (r < 0 ? 0 : (pop_count_of(f) + pop_queued_of(f) >= pop_limit_of(f) ? -1 : 0));
    }
    if (a == PA_BOAT) {
        b = ai_find_building(f, BT_DOCK);
        if (b < 0) return ai_count_own_bt(f, BT_DOCK, 0) > 0 ? 0 : -1;
        r = barracks_train(b, UC_BOAT);
        return r > 0 ? 1 : 0;
    }
    if (a == PA_SOLDIER) {
        b = ai_find_building(f, BT_BARRACKS);
        if (b < 0) return ai_count_own_bt(f, BT_BARRACKS, 0) > 0 ? 0 : -1;
        r = barracks_train(b, ai_goal_class(f_goal[f], f_mixn[f]));
        if (r > 0) { f_mixn[f] = f_mixn[f] + 1; return 1; }
        return pop_count_of(f) + pop_queued_of(f) >= pop_limit_of(f) ? -1 : 0;
    }
    if (a == PA_SIEGE) {
        b = ai_find_building(f, BT_WORKSHOP);
        if (b < 0) return ai_count_own_bt(f, BT_WORKSHOP, 0) > 0 ? 0 : -1;
        r = barracks_train(b, ai_siege_class(f_mixn[f]));
        if (r > 0) { f_mixn[f] = f_mixn[f] + 1; return 1; }
        return pop_count_of(f) + pop_queued_of(f) >= pop_limit_of(f) ? -1 : 0;
    }
    if (a == PA_HOUSE || a == PA_BARRACKS || a == PA_SMITH || a == PA_DOCK || a == PA_WORKSHOP) {
        int kind = a == PA_HOUSE ? ST_HOUSE : (a == PA_BARRACKS ? ST_BARRACKS : (a == PA_SMITH ? ST_SMITH : (a == PA_WORKSHOP ? ST_WORKSHOP : ST_DOCK)));
        r = ai_build(f, kind);
        return r >= 0 ? 1 : (r == -2 ? 0 : -1);
    }
    if (a == PA_TRADEW || a == PA_TRADES) {
        if (a == PA_TRADEW) { if (f_wood[f] < 120) return 0; f_wood[f] = f_wood[f] - 120; }
        else { if (f_stone[f] < 120) return 0; f_stone[f] = f_stone[f] - 120; }
        f_food[f] = f_food[f] + 40;
        return 1;
    }
    if (a >= PA_RES0 && a < PA_RES0 + T_N) {
        int t = a - PA_RES0;
        if (has_tech(f, t)) return 1;
        for (b = 0; b < g_hb_n; b++) if (g_hb_own[b] == f && g_rs_tech[b] == t + 1) return 1;   /* under way */
        int bt = tech_building(t), any = 0;
        for (b = 0; b < g_hb_n; b++) {
            if (g_hb_own[b] != f || g_hb_t[b] != bt) continue;
            any = 1;
            r = research_start(b, t);
            if (r > 0) return 1;
            if (r < 0) return 0;
        }
        return any ? 0 : -1;
    }
    return 1;
}

/* Sums the cost of the next few spending actions of the plan. */
static void ai_upcoming_cost(int f, int *need) {
    int k, seen = 0, c[3];
    need[0] = 0; need[1] = 0; need[2] = 0;
    for (k = f_plan_i[f]; k < f_plan_n[f] && seen < 4; k++) {
        int a = f_plan[f * PLAN_MAX + k];
        if (PA_GATHER(a)) continue;
        seen++;
        c[0] = 0; c[1] = 0; c[2] = 0;
        if (a == PA_PEASANT) unit_cost(UC_PEASANT, c);
        else if (a == PA_BOAT) unit_cost(UC_BOAT, c);
        else if (a == PA_SOLDIER) unit_cost(ai_goal_class(f_goal[f], f_mixn[f] + seen), c);
        else if (a == PA_HOUSE) struct_cost(ST_HOUSE, 0.0f, c);
        else if (a == PA_BARRACKS) struct_cost(ST_BARRACKS, 0.0f, c);
        else if (a == PA_SMITH) struct_cost(ST_SMITH, 0.0f, c);
        else if (a == PA_DOCK) struct_cost(ST_DOCK, 0.0f, c);
        else if (a == PA_WORKSHOP) struct_cost(ST_WORKSHOP, 0.0f, c);
        else if (a == PA_SIEGE) unit_cost(ai_siege_class(f_mixn[f] + seen), c);
        else if (a == PA_TRADEW) c[0] = 120;
        else if (a == PA_TRADES) c[1] = 120;
        else if (a >= PA_RES0) tech_cost(a - PA_RES0, c);
        need[0] = need[0] + c[0]; need[1] = need[1] + c[1]; need[2] = need[2] + c[2];
    }
}

static int ai_open_site(int f) {
    int k;
    for (k = 0; k < g_bs_n; k++) if (g_bs_own[k] == f && !g_bs_done[k]) return k;
    return -1;
}

/* Splits the peasants between wood, stone and building by what the
 * plan's next purchases are short of. */
static void ai_assign_roles(int f) {
    int need[3], i;
    ai_upcoming_cost(f, need);
    int dw = need[0] - f_wood[f], ds = need[1] - f_stone[f];
    if (dw < 0) dw = 0;
    if (ds < 0) ds = 0;
    int np = 0;
    for (i = 0; i < g_un; i++) if (unit_alive(i) && u_own[i] == f && u_class[i] == UC_PEASANT) np++;
    int site = ai_open_site(f);
    int wantB = site >= 0 ? (np >= 6 ? 3 : (np >= 3 ? 2 : 1)) : 0;
    int rest = np - wantB, wantW, wantS;
    if (dw + ds == 0) { wantW = (rest * 6 + 5) / 10; }
    else { wantW = (rest * dw + (dw + ds) / 2) / (dw + ds); if (dw > 0 && wantW == 0 && rest > 0) wantW = 1; if (ds > 0 && wantW == rest && rest > 1) wantW = rest - 1; }
    wantS = rest - wantW;
    /* keep peasants in their roles where possible */
    int haveW = 0, haveS = 0, haveB = 0;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_own[i] != f || u_class[i] != UC_PEASANT) continue;
        int r = u_role[i];
        if (r == R_WOOD && haveW < wantW) { haveW++; continue; }
        if (r == R_STONE && haveS < wantS) { haveS++; continue; }
        if (r == R_BUILD && haveB < wantB && u_bsite[i] >= 0 && u_bsite[i] < g_bs_n && !g_bs_done[u_bsite[i]]) { haveB++; continue; }
        u_role[i] = R_NONE;
    }
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_own[i] != f || u_class[i] != UC_PEASANT || u_role[i] != R_NONE) continue;
        if (haveB < wantB) { u_role[i] = R_BUILD; u_bsite[i] = site; haveB++; }
        else if (haveW < wantW) { u_role[i] = R_WOOD; haveW++; }
        else { u_role[i] = R_STONE; haveS++; }
    }
}

/* Counts the faction's units by role (and its builders per site). */
static void ai_count(int f) {
    int i;
    f_nw[f] = 0; f_ns[f] = 0; f_nbld[f] = 0; f_nboat[f] = 0; f_npeas[f] = 0; f_narmy[f] = 0; f_ngar[f] = 0; f_nsiege[f] = 0; f_ndragon[f] = 0;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_own[i] != f) continue;
        int r = u_role[i];
        if (u_class[i] == UC_PEASANT) {
            f_npeas[f]++;
            if (r == R_WOOD) f_nw[f]++; else if (r == R_STONE) f_ns[f]++; else if (r == R_BUILD) f_nbld[f]++;
        } else if (u_class[i] == UC_BOAT) f_nboat[f]++;
        else if (u_class[i] == UC_DRAGON) f_ndragon[f]++;
        else if (r == R_ARMY && is_siege(i)) f_nsiege[f]++;
        else if (r == R_ARMY) f_narmy[f]++;
        else if (r == R_GARRISON) f_ngar[f]++;
    }
}

/* The off-screen economy: income by role, construction by builders. */
static void ai_economy(int f, float dt) {
    float tm = has_tech(f, T_TOOLS) ? 1.3f : 1.0f;
    float nw = f_wood_acc[f] + (float)f_nw[f] * AI_RATE_WOOD * tm * dt;
    float ns = f_stone_acc[f] + (float)f_ns[f] * AI_RATE_STONE * tm * dt;
    float nf = f_food_acc[f] + (float)f_nboat[f] * AI_RATE_FISH * (has_tech(f, T_NETS) ? 1.5f : 1.0f) * dt;
    int iw = (int)nw, is = (int)ns, iff = (int)nf;
    f_wood_acc[f] = nw - (float)iw; f_stone_acc[f] = ns - (float)is; f_food_acc[f] = nf - (float)iff;
    f_wood[f] = f_wood[f] + iw; f_stone[f] = f_stone[f] + is; f_food[f] = f_food[f] + iff;
    /* construction */
    int k, i;
    for (k = 0; k < g_bs_n; k++) {
        if (g_bs_own[k] != f || g_bs_done[k]) continue;
        int nb = 0;
        for (i = 0; i < g_un; i++) if (unit_alive(i) && u_own[i] == f && u_role[i] == R_BUILD && u_bsite[i] == k) nb++;
        float nwk = g_bs_work[k] + (float)nb * 0.85f * dt;
        g_bs_work[k] = nwk;
        if (nwk >= g_bs_need[k]) build_complete(k);
    }
}

/* Market food for every faction (the player's too). */
static void factions_market(float dt) {
    int f;
    for (f = 0; f < F_MAX; f++) {
        if (!f_on[f]) continue;
        float m = f_food_acc[f] + ai_market_food(f) * dt;
        int im = (int)m;
        f_food_acc[f] = m - (float)im;
        f_food[f] = f_food[f] + im;
    }
}

/* ---- on screen: make the work visible ---- */
static void ai_sync_physical(int f) {
    int i, j;
    float tcx, tcz;
    building_door(f_tc[f], 1.8f, &tcx, &tcz);
    /* jobs on land that has scrolled out of view are dropped */
    for (j = 0; j < GJ_MAX; j++) {
        if (!g_gj_on[j] || g_gj_own[j] != f) continue;
        if (nav_slot_for(w_ifloor(g_gj_x[j] / W_CHUNK_SIZE), w_ifloor(g_gj_z[j] / W_CHUNK_SIZE)) >= 0) continue;
        for (i = 0; i < g_un; i++) if (u_job[i] == j && u_own[i] == f) peasant_to_manual(i);
        gather_job_end(j);
    }
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_own[i] != f || !u_ld[i]) continue;
        int ps = u_pstate[i], r = u_role[i];
        int gathering = ps >= PS_G_GO && ps <= PS_G_DROP && u_job[i] >= 0 && u_job[i] < GJ_MAX && g_gj_on[u_job[i]];
        if (u_class[i] == UC_PEASANT) {
            if (r == R_WOOD || r == R_STONE) {
                int type = r == R_WOOD ? RES_WOOD : RES_STONE;
                if (gathering && g_gj_type[u_job[i]] == type) continue;
                int q = res_find_near(tcx, tcz, 80.0f, type);
                int job = q >= 0 ? gather_job_for(f, q) : -1;
                if (job >= 0) gather_assign_unit(i, job, q);
                else if (ps != PS_MANUAL) { unit_release_field(i); u_order[i] = ORD_NONE; peasant_to_manual(i); }
            } else if (r == R_BUILD) {
                int s2 = u_bsite[i];
                if (s2 < 0 || s2 >= g_bs_n || g_bs_done[s2]) continue;
                if ((ps == PS_B_GO || ps == PS_B_WORK) && u_bsite[i] == s2) continue;
                builder_assign(i, s2);
            } else if (ps != PS_MANUAL) { unit_release_field(i); u_order[i] = ORD_NONE; peasant_to_manual(i); }
        } else if (u_class[i] == UC_BOAT) {
            if (gathering) continue;
            float dx, dz;
            if (!nearest_dock_drop(f, u_x[i], u_z[i], &dx, &dz)) continue;
            int q = res_find_near(dx, dz, 120.0f, RES_FOOD);
            int job = q >= 0 ? gather_job_for(f, q) : -1;
            if (job >= 0) gather_assign_unit(i, job, q);
        }
    }
    dispatch_repaths();
}

/* ---- attack waves ---- */
/* Sends unit i (attack-moving) to (gx,gz): it fights whatever it meets
 * and carries on. Off screen it walks there directly (ai_travel). */
static void ai_send(int i, float gx, float gz) {
    u_amove[i] = 1;
    u_gx[i] = gx; u_gy[i] = g_hb_y[f_tc[0]]; u_gz[i] = gz; u_gl[i] = 0;
    u_order[i] = ORD_MOVE; u_target[i] = -1;
    unit_release_field(i);
    if (u_ld[i]) { u_repath[i] = 1; u_state[i] = US_WAIT; }
    else u_state[i] = US_MOVE;
}

/* Off-screen march: straight for the goal, turning aside from water. */
static void ai_travel(int i, float dt) {
    float dx = u_gx[i] - u_x[i], dz = u_gz[i] - u_z[i];
    float d = w_sqrt(dx * dx + dz * dz);
    if (d < 3.0f || u_state[i] == US_DEAD) return;
    float base = approx_atan2_w(dz, dx);
    int k;
    for (k = 0; k < 7; k++) {
        float off = (float)((k + 1) / 2) * 0.5f * ((k & 1) ? 1.0f : -1.0f);
        float a = base + off;
        float px = u_x[i] + w_cos(a) * 4.0f, pz = u_z[i] + w_sin(a) * 4.0f;
        if (w_height_raw(px, pz) < W_SEA_LEVEL + 0.15f) continue;
        float step = u_speed[i] * 0.9f * dt;
        u_x[i] = u_x[i] + w_cos(a) * step;
        u_z[i] = u_z[i] + w_sin(a) * step;
        u_yaw[i] = approx_atan2(w_cos(a), -w_sin(a));
        u_layer[i] = 0;
        u_y[i] = world_ground_height(u_x[i], u_z[i]);
        u_state[i] = US_MOVE;
        return;
    }
}

static void ai_waves(int f, float dt) {
    float nt = f_wave_t[f] + dt;
    f_wave_t[f] = nt;
    if (g_time < 300.0f + 60.0f * (float)(f - 1) || f_wave_t[f] < 150.0f || f_tc[0] < 0) return;
    int size = 4 + 2 * f_waves[f] + (f_pers[f] == 0 ? 2 : 0);
    if (size > 12) size = 12;
    if (f_narmy[f] < size) return;
    float gx, gz;
    building_door(f_tc[0], 7.0f, &gx, &gz);
    int i, n = 0;
    int ng = 0, nd = 0;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_own[i] != f || u_class[i] != UC_DRAGON || u_dstate[i] == DG_ATTACK) continue;
        u_role[i] = R_WAVE; u_amove[i] = 1;                 /* the dragons fly with every wave */
        dragon_order_fly(i, gx, gz);
        nd++;
    }
    for (i = 0; i < g_un && (n < size || ng < 2); i++) {
        if (!unit_alive(i) || u_own[i] != f || u_role[i] != R_ARMY || u_class[i] == UC_DRAGON) continue;
        if (is_siege(i)) { if (ng >= 2) continue; ng++; }
        else { if (n >= size) continue; n++; }
        u_role[i] = R_WAVE;
        ai_send(i, gx + (float)((n + ng) % 5 - 2) * 1.2f, gz + (float)((n + ng) / 5) * 1.2f);
    }
    n = n + ng;
    f_waves[f] = f_waves[f] + 1;
    f_wave_t[f] = 0.0f;
    __android_log_print(4, "castles", "AI castle %d sends wave %d: %d units (%d siege engines, %d dragons) march on the player's town centre", f, f_waves[f], n, ng, nd);
}

/* Taming: a castle with peasants to spare and no dragon sends two to a
 * wild dragon roosting within ~230 (off screen this is counted: the
 * walk there plus the calming work, paused while the dragon is away). */
static void ai_tame(int f, float dt) {
    int d = f_tame_d[f];
    if (d >= 0) {
        if (!unit_alive(d) || u_class[d] != UC_DRAGON || u_own[d] >= 0) { f_tame_d[f] = -1; return; }
        if (u_dstate[d] != DG_PERCH) return;
        u_dtame_t[d] = g_time;                                   /* keeps it calm */
        float nt = f_tame_t[f] - dt;
        f_tame_t[f] = nt;
        if (nt <= 0.0f) { f_tame_d[f] = -1; dragon_tamed(d, f); }
        return;
    }
    if (g_time < 240.0f || f_npeas[f] < 6 || f_ndragon[f] > 0) return;
    int i, best = -1;
    float bd = 230.0f * 230.0f;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_class[i] != UC_DRAGON || u_own[i] >= 0 || u_dstate[i] != DG_PERCH) continue;
        int j, taken = 0;
        for (j = 1; j < F_MAX; j++) if (j != f && f_tame_d[j] == i) taken = 1;
        if (taken) continue;
        float dx = u_x[i] - f_site[f].cx, dz = u_z[i] - f_site[f].cz;
        if (dx * dx + dz * dz < bd) { bd = dx * dx + dz * dz; best = i; }
    }
    if (best < 0) return;
    f_tame_d[f] = best;
    f_tame_t[f] = w_sqrt(bd) / 2.6f + DG_TAME_T / 2.0f;
    __android_log_print(4, "castles", "AI castle %d sends peasants to tame a dragon %d away", f, (int)w_sqrt(bd));
}

/* Enemy soldiers marching on / fighting near the player (for the HUD). */
static int ai_incoming(void) {
    int i, n = 0;
    for (i = 0; i < g_un; i++) if (unit_alive(i) && u_own[i] > 0 && u_role[i] == R_WAVE) n++;
    return n;
}

static void ai_log(int f) {
    int a = f_plan_i[f] < f_plan_n[f] ? f_plan[f * PLAN_MAX + f_plan_i[f]] : -1;
    /* two lines: squash's ARM64 variadic calls garble arguments beyond
     * the first dozen or so */
    __android_log_print(4, "castles", "AI castle %d [%s] step %d/%d (%s) wood %d stone %d food %d",
                        f, f_goal[f] >= 0 ? ai_goal_name(f_goal[f]) : "-", f_plan_i[f], f_plan_n[f], a >= 0 ? plan_action_name(a) : "replanning",
                        f_wood[f], f_stone[f], f_food[f]);
    __android_log_print(4, "castles", "AI castle %d   peasants %d (wood %d stone %d build %d) boats %d army %d garrison %d advancements %d waves %d",
                        f, f_npeas[f], f_nw[f], f_ns[f], f_nbld[f], f_nboat[f], f_narmy[f], f_ngar[f], f_tech[f], f_waves[f]);
    int i, vis = 0, g = 0, bw = 0, fish = 0;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_own[i] != f || !u_ld[i]) continue;
        vis++;
        if (u_class[i] == UC_PEASANT && u_pstate[i] >= PS_G_GO && u_pstate[i] <= PS_G_DROP) g++;
        if (u_class[i] == UC_PEASANT && (u_pstate[i] == PS_B_GO || u_pstate[i] == PS_B_WORK)) bw++;
        if (u_class[i] == UC_BOAT && u_pstate[i] >= PS_G_GO && u_pstate[i] <= PS_G_DROP) fish++;
    }
    if (vis > 0) __android_log_print(4, "castles", "AI castle %d   on screen: %d units, %d peasants gathering, %d building, %d boats fishing", f, vis, g, bw, fish);
}

/* Per frame. */
static void ai_update(float dt) {
    int f;
    factions_market(dt);
    for (f = 1; f < F_MAX; f++) {
        if (!f_on[f]) continue;
        ai_economy(f, dt);
        if (f_dec_stage[f] > 0) { ai_decide_step(f); continue; }
        float nt = f_tick_t[f] + dt;
        f_tick_t[f] = nt;
        if (nt < 0.5f) continue;
        f_tick_t[f] = 0.0f;
        ai_count(f);
        float nr = f_replan_t[f] + nt;
        f_replan_t[f] = nr;
        /* skip the plan's gathering steps: gathering never stops */
        while (f_plan_i[f] < f_plan_n[f] && PA_GATHER(f_plan[f * PLAN_MAX + f_plan_i[f]])) f_plan_i[f] = f_plan_i[f] + 1;
        /* replan when the plan is done (after a pause if nothing was
         * worth doing last time), every 45 s, or when stuck */
        if ((f_plan_i[f] >= f_plan_n[f] && (f_goal[f] >= 0 || nr > 20.0f)) || nr > 45.0f || f_wait_t[f] > 50.0f) { f_dec_stage[f] = 1; continue; }
        if (f_plan_i[f] >= f_plan_n[f]) { ai_waves(f, nt); continue; }
        int r = ai_try_action(f, f_plan[f * PLAN_MAX + f_plan_i[f]]);
        if (r > 0) { f_plan_i[f] = f_plan_i[f] + 1; f_wait_t[f] = 0.0f; }
        else if (r < 0) f_dec_stage[f] = 1;
        else { float nw2 = f_wait_t[f] + nt; f_wait_t[f] = nw2; }
        float nrt = f_role_t[f] + nt;
        f_role_t[f] = nrt;
        if (nrt > 2.0f) { f_role_t[f] = 0.0f; ai_assign_roles(f); }
        float nst = f_sync_t[f] + nt;
        f_sync_t[f] = nst;
        if (nst > 1.5f) { f_sync_t[f] = 0.0f; ai_sync_physical(f); }
        ai_waves(f, nt);
        ai_tame(f, nt);
        float nl = f_log_t[f] + nt;
        f_log_t[f] = nl;
        if (nl > 30.0f) { f_log_t[f] = 0.0f; ai_log(f); }
    }
}

#endif /* CASTLES_AI_H */
