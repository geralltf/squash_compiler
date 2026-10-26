/* ---- castles_planner.h: advancement planner for the AI castles ----
 *
 * Goal-oriented action planning with A*. A castle's economy is
 * abstracted into a small integer state -- stockpile (in tens), peasant /
 * boat / soldier counts, buildings and researched advancements -- and
 * the things it can do into actions (gather, train, build, research)
 * with preconditions, effects and a duration estimated from its income
 * rates. A* finds the quickest sequence of actions from the current
 * state to a goal ("men-at-arms: forging + armour + 8 more soldiers"),
 * so gathering is planned exactly as far as the spending needs it. The
 * AI (castles_ai.h) plans several candidate advancement paths, weighs
 * each one's value against its planned time, follows the best plan and
 * replans as things change.
 *
 * Pure C over its own globals (no game state), so the host test
 * android/examples/castles_planner_hosttest.c builds it too. */

#ifndef CASTLES_PLANNER_H
#define CASTLES_PLANNER_H

/* ---- advancements ---- */
#define T_TOOLS     0   /* town centre: peasants gather 30% faster            */
#define T_NETS      1   /* dock: boats fish faster and hold half as much again */
#define T_FORGING   2   /* blacksmith: +1 melee damage                         */
#define T_FLETCHING 3   /* blacksmith: +1 arrow damage, +4 bow range            */
#define T_ARMOR     4   /* blacksmith (after forging): soldiers +2 hp           */
#define T_DRILL     5   /* barracks: soldiers train 35% faster, move 10% faster */
#define T_N         6

/* Cost of advancement t: c[0] wood, c[1] stone, c[2] food. */
static void tech_cost(int t, int *c) {
    c[0] = 0; c[1] = 0; c[2] = 0;
    if (t == T_TOOLS) { c[0] = 60; c[2] = 40; }
    else if (t == T_NETS) { c[0] = 50; c[2] = 30; }
    else if (t == T_FORGING) { c[1] = 60; c[2] = 60; }
    else if (t == T_FLETCHING) { c[0] = 70; c[2] = 50; }
    else if (t == T_ARMOR) { c[1] = 80; c[2] = 60; }
    else { c[0] = 40; c[2] = 50; }
}
static float tech_time(int t) { return t == T_ARMOR ? 35.0f : (t == T_NETS ? 20.0f : 25.0f); }
static int tech_prereq(int t) { return t == T_ARMOR ? T_FORGING : -1; }
static const char *tech_name(int t) {
    if (t == T_TOOLS) return "tools";
    if (t == T_NETS) return "fishing nets";
    if (t == T_FORGING) return "forging";
    if (t == T_FLETCHING) return "fletching";
    if (t == T_ARMOR) return "armour";
    return "drill";
}

/* ---- planning state (ints; resources in tens, capped) ---- */
#define PV_W   0    /* wood / 10              */
#define PV_S   1    /* stone / 10             */
#define PV_F   2    /* food / 10              */
#define PV_NP  3    /* peasants               */
#define PV_NB  4    /* fishing boats          */
#define PV_NS  5    /* soldiers (army)        */
#define PV_H   6    /* houses built by the AI */
#define PV_BR  7    /* barracks               */
#define PV_DK  8    /* docks                  */
#define PV_SM  9    /* blacksmiths            */
#define PV_T   10   /* advancements (bits)    */
#define PV_WS  11   /* siege workshops        */
#define PV_NG  12   /* siege engines          */
#define PV_N   13
#define PV_RCAP 40  /* resource cap: 400      */

/* ---- actions ---- */
#define PA_WOOD     0    /* gather 60 wood with every peasant     */
#define PA_STONE    1
#define PA_FISH     2    /* boats fish 60 food (+ market income)   */
#define PA_FOOD     3    /* wait for 30 food from the market alone */
#define PA_PEASANT  4
#define PA_BOAT     5
#define PA_SOLDIER  6
#define PA_HOUSE    7
#define PA_BARRACKS 8
#define PA_DOCK     9
#define PA_SMITH    10
#define PA_RES0     11   /* + advancement                          */
#define PA_TRADEW   (PA_RES0 + T_N)       /* market: 120 wood -> 40 food  */
#define PA_TRADES   (PA_RES0 + T_N + 1)   /* market: 120 stone -> 40 food */
#define PA_WORKSHOP (PA_RES0 + T_N + 2)
#define PA_SIEGE    (PA_RES0 + T_N + 3)   /* a catapult / ram / trebuchet */
#define PA_N        (PA_RES0 + T_N + 4)
#define PA_GATHER(a) ((a) <= PA_FOOD)

/* ---- context: the castle's rates and limits (set before planning) ---- */
static float pc_rw, pc_rs, pc_rf, pc_pf;   /* wood / stone per peasant-second, food per boat-second, market food per second */
static int pc_pop0;                        /* population limit without AI-built houses */
static int pc_lots;                        /* free building lots                       */
static int pc_br0, pc_sm0;                 /* barracks / blacksmiths at the start      */
static int pc_can_dock;                    /* a shore spot for a dock exists           */
static int pc_sol[3];                      /* a soldier's cost in tens (wood, stone, food) */
static float pc_sol_time;                  /* a soldier's training time                */
static int pc_sg[3];                       /* a siege engine's cost in tens            */
static float pc_sg_time;
#define PC_NPMAX 14
#define PC_NBMAX 3
#define PC_NSMAX 16
#define PC_NGMAX 4

/* ---- goal ---- */
static int pg_tech, pg_ns, pg_np, pg_nb, pg_ng;

static int pl_ceil10(int v) { return (v + 9) / 10; }

static float pl_builders(int np) { return (float)(np < 1 ? 1 : (np > 3 ? 3 : np)); }

/* Applies action a to state s into o; returns its duration in seconds,
 * or -1 if it can't be done there. */
static float plan_step(const int *s, int a, int *o) {
    int k;
    for (k = 0; k < PV_N; k++) o[k] = s[k];
    int np = s[PV_NP], nb = s[PV_NB], ns = s[PV_NS];
    int pop = np + nb + ns + s[PV_NG], popmax = pc_pop0 + 4 * s[PV_H];
    int used = s[PV_H] + (s[PV_BR] > pc_br0 ? s[PV_BR] - pc_br0 : 0) + (s[PV_SM] > pc_sm0 ? s[PV_SM] - pc_sm0 : 0) + s[PV_WS];
    if (a == PA_WOOD) {
        if (np < 1 || s[PV_W] > PV_RCAP - 6) return -1.0f;
        o[PV_W] = s[PV_W] + 6;
        return 60.0f / (pc_rw * (float)np);
    }
    if (a == PA_STONE) {
        if (np < 1 || s[PV_S] > PV_RCAP - 6) return -1.0f;
        o[PV_S] = s[PV_S] + 6;
        return 60.0f / (pc_rs * (float)np);
    }
    if (a == PA_FISH) {
        if (nb < 1 || s[PV_F] > PV_RCAP - 6) return -1.0f;
        o[PV_F] = s[PV_F] + 6;
        return 60.0f / (pc_rf * (float)nb + pc_pf);
    }
    if (a == PA_FOOD) {
        if (pc_pf <= 0.0f || s[PV_F] > PV_RCAP - 3) return -1.0f;
        o[PV_F] = s[PV_F] + 3;
        return 30.0f / pc_pf;
    }
    if (a == PA_PEASANT) {
        if (np >= PC_NPMAX || pop >= popmax || s[PV_F] < 2) return -1.0f;
        o[PV_F] = s[PV_F] - 2; o[PV_NP] = np + 1;
        return 5.0f;
    }
    if (a == PA_BOAT) {
        if (s[PV_DK] < 1 || nb >= PC_NBMAX || pop >= popmax || s[PV_W] < 3) return -1.0f;
        o[PV_W] = s[PV_W] - 3; o[PV_NB] = nb + 1;
        return 10.0f;
    }
    if (a == PA_SOLDIER) {
        if (s[PV_BR] < 1 || ns >= PC_NSMAX || pop >= popmax) return -1.0f;
        if (s[PV_W] < pc_sol[0] || s[PV_S] < pc_sol[1] || s[PV_F] < pc_sol[2]) return -1.0f;
        o[PV_W] = s[PV_W] - pc_sol[0]; o[PV_S] = s[PV_S] - pc_sol[1]; o[PV_F] = s[PV_F] - pc_sol[2];
        o[PV_NS] = ns + 1;
        float t = pc_sol_time;
        if (s[PV_T] & (1 << T_DRILL)) { float t2 = t * 0.65f; t = t2; }
        return t / (float)s[PV_BR];
    }
    if (a == PA_HOUSE) {
        if (np < 1 || used >= pc_lots || s[PV_H] >= 6 || s[PV_W] < 3) return -1.0f;
        o[PV_W] = s[PV_W] - 3; o[PV_H] = s[PV_H] + 1;
        return 20.0f / pl_builders(np);
    }
    if (a == PA_BARRACKS) {
        if (np < 1 || used >= pc_lots || s[PV_BR] >= pc_br0 + 1 || s[PV_BR] >= 2 || s[PV_W] < 6 || s[PV_S] < 4) return -1.0f;
        o[PV_W] = s[PV_W] - 6; o[PV_S] = s[PV_S] - 4; o[PV_BR] = s[PV_BR] + 1;
        return 45.0f / pl_builders(np);
    }
    if (a == PA_DOCK) {
        if (np < 1 || !pc_can_dock || s[PV_DK] >= 1 || s[PV_W] < 5) return -1.0f;
        o[PV_W] = s[PV_W] - 5; o[PV_DK] = 1;
        return 30.0f / pl_builders(np);
    }
    if (a == PA_SMITH) {
        if (np < 1 || used >= pc_lots || s[PV_SM] >= 1 || s[PV_W] < 5 || s[PV_S] < 4) return -1.0f;
        o[PV_W] = s[PV_W] - 5; o[PV_S] = s[PV_S] - 4; o[PV_SM] = 1;
        return 30.0f / pl_builders(np);
    }
    if (a == PA_WORKSHOP) {
        if (np < 1 || used >= pc_lots || s[PV_WS] >= 1 || s[PV_W] < 10 || s[PV_S] < 6) return -1.0f;
        o[PV_W] = s[PV_W] - 10; o[PV_S] = s[PV_S] - 6; o[PV_WS] = 1;
        return 40.0f / pl_builders(np);
    }
    if (a == PA_SIEGE) {
        if (s[PV_WS] < 1 || s[PV_NG] >= PC_NGMAX || pop >= popmax) return -1.0f;
        if (s[PV_W] < pc_sg[0] || s[PV_S] < pc_sg[1] || s[PV_F] < pc_sg[2]) return -1.0f;
        o[PV_W] = s[PV_W] - pc_sg[0]; o[PV_S] = s[PV_S] - pc_sg[1]; o[PV_F] = s[PV_F] - pc_sg[2];
        o[PV_NG] = s[PV_NG] + 1;
        return pc_sg_time;
    }
    if (a == PA_TRADEW || a == PA_TRADES) {
        int r = a == PA_TRADEW ? PV_W : PV_S;
        if (s[r] < 12 || s[PV_F] > PV_RCAP - 4) return -1.0f;
        o[r] = s[r] - 12; o[PV_F] = s[PV_F] + 4;
        return 2.0f;
    }
    {
        int t = a - PA_RES0, c[3];
        if (t < 0 || t >= T_N || (s[PV_T] & (1 << t))) return -1.0f;
        if (tech_prereq(t) >= 0 && !(s[PV_T] & (1 << tech_prereq(t)))) return -1.0f;
        if (t == T_NETS && s[PV_DK] < 1) return -1.0f;
        if (t == T_DRILL && s[PV_BR] < 1) return -1.0f;
        if ((t == T_FORGING || t == T_FLETCHING || t == T_ARMOR) && s[PV_SM] < 1) return -1.0f;
        tech_cost(t, c);
        int cw = pl_ceil10(c[0]), cs = pl_ceil10(c[1]), cf = pl_ceil10(c[2]);
        if (s[PV_W] < cw || s[PV_S] < cs || s[PV_F] < cf) return -1.0f;
        o[PV_W] = s[PV_W] - cw; o[PV_S] = s[PV_S] - cs; o[PV_F] = s[PV_F] - cf;
        o[PV_T] = s[PV_T] | (1 << t);
        return tech_time(t);
    }
}

static int plan_goal_met(const int *s) {
    return (s[PV_T] & pg_tech) == pg_tech && s[PV_NS] >= pg_ns && s[PV_NP] >= pg_np && s[PV_NB] >= pg_nb && s[PV_NG] >= pg_ng;
}

/* Optimistic time still needed (each missing advancement's research,
 * the blacksmith it needs, the units still to train). */
static float plan_h(const int *s) {
    float h = 0.0f;
    int t;
    for (t = 0; t < T_N; t++) {
        if (!(pg_tech & (1 << t)) || (s[PV_T] & (1 << t))) continue;
        float nh = h + tech_time(t);
        h = nh;
    }
    if ((pg_tech & ((1 << T_FORGING) | (1 << T_FLETCHING) | (1 << T_ARMOR))) && s[PV_SM] < 1) {
        float nh = h + 30.0f / pl_builders(s[PV_NP]);
        h = nh;
    }
    if (s[PV_NS] < pg_ns) {
        float st = pc_sol_time;
        if (s[PV_T] & (1 << T_DRILL)) { float t2 = st * 0.65f; st = t2; }
        float nh = h + (float)(pg_ns - s[PV_NS]) * st / 2.0f;
        h = nh;
    }
    if (s[PV_NP] < pg_np) { float nh = h + (float)(pg_np - s[PV_NP]) * 5.0f; h = nh; }
    if (s[PV_NB] < pg_nb) { float nh = h + (float)(pg_nb - s[PV_NB]) * 10.0f; h = nh; }
    /* + gathering for whatever the rest of the goal still costs */
    int nw = 0, ns2 = 0, nf = 0, c[3];
    for (t = 0; t < T_N; t++) {
        if (!(pg_tech & (1 << t)) || (s[PV_T] & (1 << t))) continue;
        tech_cost(t, c);
        nw = nw + pl_ceil10(c[0]); ns2 = ns2 + pl_ceil10(c[1]); nf = nf + pl_ceil10(c[2]);
    }
    if ((pg_tech & ((1 << T_FORGING) | (1 << T_FLETCHING) | (1 << T_ARMOR))) && s[PV_SM] < 1) { nw = nw + 5; ns2 = ns2 + 4; }
    if (s[PV_NS] < pg_ns) { int d = pg_ns - s[PV_NS]; nw = nw + d * pc_sol[0]; ns2 = ns2 + d * pc_sol[1]; nf = nf + d * pc_sol[2]; }
    if (s[PV_NP] < pg_np) nf = nf + 2 * (pg_np - s[PV_NP]);
    if (s[PV_NB] < pg_nb) { nw = nw + 3 * (pg_nb - s[PV_NB]); if (s[PV_DK] < 1) nw = nw + 5; }
    if (s[PV_NG] < pg_ng) {
        int d = pg_ng - s[PV_NG];
        nw = nw + d * pc_sg[0]; ns2 = ns2 + d * pc_sg[1]; nf = nf + d * pc_sg[2];
        if (s[PV_WS] < 1) { nw = nw + 10; ns2 = ns2 + 6; float nh = h + 40.0f / pl_builders(s[PV_NP]); h = nh; }
        float nh2 = h + (float)d * pc_sg_time;
        h = nh2;
    }
    int dw = nw - s[PV_W], ds = ns2 - s[PV_S], df = nf - s[PV_F];
    float np = (float)(s[PV_NP] > 0 ? s[PV_NP] : 1);
    if (dw > 0) { float nh = h + (float)dw * 10.0f / (pc_rw * np); h = nh; }
    if (ds > 0) { float nh = h + (float)ds * 10.0f / (pc_rs * np); h = nh; }
    if (df > 0) {
        /* food comes from boats + market, or from wood gathered to trade (3:1) */
        float viaFood = (float)df * 10.0f / (pc_rf * (float)s[PV_NB] + pc_pf + 0.001f);
        float viaTrade = (float)df * 30.0f / (pc_rw * np);
        float nh = h + (viaFood < viaTrade ? viaFood : viaTrade);
        h = nh;
    }
    return h;
}

/* ---- A* ---- */
#define PN_MAX   8000          /* nodes                 */
#define PH_MAX   40000         /* heap entries (lazy)   */
#define PT_SIZE  16384         /* state hash table      */
#define PL_EXPAND 3500         /* expansion budget      */
#define PL_HW    1.25f         /* heuristic weight: slightly greedy, far fewer nodes */
static int nd_s[PN_MAX * PV_N];
static float nd_g[PN_MAX], nd_f[PN_MAX];
static int nd_par[PN_MAX], nd_act[PN_MAX], nd_closed[PN_MAX];
static int nd_n;
static int ph_i[PH_MAX];
static float ph_f[PH_MAX];
static int ph_n;
static int pt_tab[PT_SIZE];          /* node + 1, 0 = empty */
static int g_plan_expanded;          /* stats */

static uint32_t plan_hash(const int *s) {
    uint32_t h = 2166136261u;
    int k;
    for (k = 0; k < PV_N; k++) {
        uint32_t x = h ^ (uint32_t)(s[k] & 0xFFFF);
        h = x * 16777619u;
    }
    return h;
}

static void ph_push(int i, float f) {
    if (ph_n >= PH_MAX) return;
    int k = ph_n;
    ph_n = k + 1;
    while (k > 0) {
        int p = (k - 1) / 2;
        if (ph_f[p] <= f) break;
        ph_i[k] = ph_i[p]; ph_f[k] = ph_f[p];
        k = p;
    }
    ph_i[k] = i; ph_f[k] = f;
}

static int ph_pop(float *f) {
    int top = ph_i[0];
    *f = ph_f[0];
    ph_n = ph_n - 1;
    int li = ph_i[ph_n];
    float lf = ph_f[ph_n];
    int k = 0;
    for (;;) {
        int c = 2 * k + 1;
        if (c >= ph_n) break;
        if (c + 1 < ph_n && ph_f[c + 1] < ph_f[c]) c = c + 1;
        if (ph_f[c] >= lf) break;
        ph_i[k] = ph_i[c]; ph_f[k] = ph_f[c];
        k = c;
    }
    ph_i[k] = li; ph_f[k] = lf;
    return top;
}

/* Finds (or adds) the node for state s; *isNew tells which. -1 if full. */
static int plan_node(const int *s, int *isNew) {
    uint32_t h = plan_hash(s);
    int slot = (int)(h & (PT_SIZE - 1)), probes, k;
    *isNew = 0;
    for (probes = 0; probes < PT_SIZE; probes++) {
        int e = pt_tab[slot];
        if (e == 0) break;
        int n = e - 1, same = 1;
        for (k = 0; k < PV_N; k++) if (nd_s[n * PV_N + k] != s[k]) { same = 0; break; }
        if (same) return n;
        slot = (slot + 1) & (PT_SIZE - 1);
    }
    if (nd_n >= PN_MAX || probes >= PT_SIZE) return -1;
    int n = nd_n;
    nd_n = n + 1;
    for (k = 0; k < PV_N; k++) nd_s[n * PV_N + k] = s[k];
    nd_closed[n] = 0;
    pt_tab[slot] = n + 1;
    *isNew = 1;
    return n;
}

/* Plans from `start` to the goal (pg_*). Writes up to `max` actions to
 * acts and returns their count (the plan to the closest state reached
 * if the goal wasn't found within budget, flagged by *reached = 0);
 * *time gets the plan's duration. */
static int plan_astar(const int *start, int *acts, int max, float *time, int *reached) {
    int k, isNew, child[PV_N];
    nd_n = 0; ph_n = 0;
    for (k = 0; k < PT_SIZE; k++) pt_tab[k] = 0;
    int root = plan_node(start, &isNew);
    nd_g[root] = 0.0f; nd_par[root] = -1; nd_act[root] = -1;
    float h0 = plan_h(start) * PL_HW;
    nd_f[root] = h0;
    ph_push(root, h0);
    int best = root, goal = -1;
    float bestH = h0;
    g_plan_expanded = 0;
    while (ph_n > 0 && g_plan_expanded < PL_EXPAND) {
        float f;
        int n = ph_pop(&f);
        if (nd_closed[n] || f > nd_f[n] + 0.001f) continue;      /* stale heap entry */
        nd_closed[n] = 1;
        g_plan_expanded = g_plan_expanded + 1;
        if (plan_goal_met(nd_s + n * PV_N)) { goal = n; break; }
        int a;
        for (a = 0; a < PA_N; a++) {
            float dt = plan_step(nd_s + n * PV_N, a, child);
            if (dt < 0.0f) continue;
            int c = plan_node(child, &isNew);
            if (c < 0) continue;
            float g = nd_g[n] + dt;
            if (!isNew && (nd_closed[c] || g >= nd_g[c] - 0.001f)) continue;
            float hc = plan_h(child) * PL_HW;
            nd_g[c] = g; nd_par[c] = n; nd_act[c] = a;
            nd_f[c] = g + hc;
            nd_closed[c] = 0;
            ph_push(c, g + hc);
            if (hc < bestH) { bestH = hc; best = c; }
        }
    }
    int end = goal >= 0 ? goal : best;
    *reached = goal >= 0;
    *time = nd_g[end];
    /* walk back; keep the first `max` actions of a longer plan */
    int total = 0, n2 = end;
    while (n2 >= 0 && nd_act[n2] >= 0) { total++; n2 = nd_par[n2]; }
    int i2 = total - 1;
    n2 = end;
    while (n2 >= 0 && nd_act[n2] >= 0) {
        if (i2 < max) acts[i2] = nd_act[n2];
        i2--;
        n2 = nd_par[n2];
    }
    return total < max ? total : max;
}

static const char *plan_action_name(int a) {
    if (a == PA_WOOD) return "gather-wood";
    if (a == PA_STONE) return "gather-stone";
    if (a == PA_FISH) return "fish";
    if (a == PA_FOOD) return "market-food";
    if (a == PA_PEASANT) return "peasant";
    if (a == PA_BOAT) return "boat";
    if (a == PA_SOLDIER) return "soldier";
    if (a == PA_HOUSE) return "house";
    if (a == PA_BARRACKS) return "barracks";
    if (a == PA_DOCK) return "dock";
    if (a == PA_SMITH) return "blacksmith";
    if (a == PA_TRADEW) return "sell-wood";
    if (a == PA_TRADES) return "sell-stone";
    if (a == PA_WORKSHOP) return "siege-workshop";
    if (a == PA_SIEGE) return "siege-engine";
    return tech_name(a - PA_RES0);
}

#endif /* CASTLES_PLANNER_H */
