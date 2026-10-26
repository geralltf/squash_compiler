/* Host check for castles_planner.h: plans each AI advancement path from
 * a typical starting castle and prints the plan, its time and the A*
 * effort.   gcc -O2 -I. -o /tmp/pt android/examples/castles_planner_hosttest.c */
#include <stdio.h>
#include <stdint.h>
#include "android/examples/castles_planner.h"

static void run(const char *name, int tech, int ns, int np, int nb, const int *start) {
    int acts[64], reached, k;
    float t;
    pg_tech = tech; pg_ns = ns; pg_np = np; pg_nb = nb; pg_ng = 0;
    if (tech < 0) { pg_tech = 0; pg_ng = 2; }
    int n = plan_astar(start, acts, 64, &t, &reached);
    printf("%-14s %s t=%6.1fs expanded=%4d nodes=%4d plan(%d):", name, reached ? "OK " : "PART", t, g_plan_expanded, nd_n, n);
    for (k = 0; k < n; k++) printf(" %s", plan_action_name(acts[k]));
    printf("\n");
}

int main(void) {
    int s[PV_N] = {0};
    pc_rw = 0.33f; pc_rs = 0.26f; pc_rf = 0.55f; pc_pf = 0.43f;
    pc_pop0 = 46; pc_lots = 6; pc_br0 = 1; pc_sm0 = 0; pc_can_dock = 1;
    pc_sol[0] = 1; pc_sol[1] = 0; pc_sol[2] = 2; pc_sol_time = 8.0f;
    pc_sg[0] = 11; pc_sg[1] = 4; pc_sg[2] = 0; pc_sg_time = 15.0f;
    s[PV_W] = 15; s[PV_S] = 10; s[PV_F] = 15; s[PV_NP] = 5; s[PV_NS] = 2; s[PV_BR] = 1;
    run("rush", 0, 8, 0, 0, s);
    run("archers", 1 << T_FLETCHING, 8, 0, 0, s);
    run("men-at-arms", (1 << T_FORGING) | (1 << T_ARMOR), 10, 0, 0, s);
    run("boom", (1 << T_TOOLS) | (1 << T_NETS), 0, 10, 2, s);
    run("drill", 1 << T_DRILL, 10, 0, 0, s);
    run("siege", -1, 6, 0, 0, s);
    pc_can_dock = 0;
    run("boom-nodock", 1 << T_TOOLS, 0, 10, 0, s);
    s[PV_W] = 2; s[PV_S] = 0; s[PV_F] = 1;
    run("poor-maa", (1 << T_FORGING) | (1 << T_ARMOR), 10, 0, 0, s);
    return 0;
}
