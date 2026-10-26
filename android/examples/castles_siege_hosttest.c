/* Host check for siege (castles_siege.h through the real units code):
 * loads the chunks around an AI castle into slots the way the streamer
 * does, parks a catapult, a trebuchet and a ram outside its walls and
 * runs the game: walls should come down, the generator should leave
 * rubble and open ground there after the chunk rebuild.
 *   build: see castles_ai_hosttest.c (same android-side paste-ins). */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
static int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap;
    (void)prio; (void)tag;
    va_start(ap, fmt);
    printf("  ");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    return 0;
}
#include "android/examples/castles_world.h"
#include "android/examples/castles_ballistics.h"
#define SLOTS 64
#define TAG "castles"
#include AI_SLOTS_INC
static float g_view_w = 1440.0f, g_view_h = 2700.0f;
static float g_cam[3];
static float g_yaw, g_pitch;
static float g_time;
#include AI_HELPERS_INC
#include "android/examples/castles_units.h"

static float stage[30000 * W_VSTRIDE];
static void host_load(int slot, int cx, int cz) {
    world_gen_chunk(cx, cz, stage, 30000);
    g_slot_used[slot] = 1; g_slot_cx[slot] = cx; g_slot_cz[slot] = cz;
    memcpy(g_slot_navh + slot * (NAV_L * NAV_N), g_nav_h, sizeof(float) * NAV_L * NAV_N);
    memcpy(g_slot_navf + slot * (NAV_L * NAV_N), g_nav_f, NAV_L * NAV_N);
    memcpy(g_slot_hgt + slot * (W_GRIDV * W_GRIDV), g_hgt, sizeof(float) * W_GRIDV * W_GRIDV);
    g_slot_nres[slot] = g_out_nres;
    g_nav_cache_slot = -1;
}

int main(int argc, char **argv) {
    int i, j, f = argc > 1 ? atoi(argv[1]) : 3;
    world_init();
    units_init();
    units_home_init();
    ai_init();
    WSite *s = &f_site[f];
    printf("target: AI castle %d at %.0f,%.0f R %.0f rings %d, %d castle pieces registered in all\n", f, s->cx, s->cz, s->R, s->rings, g_el_n);
    int c0x = w_ifloor(s->cx / W_CHUNK_SIZE) - 2, c0z = w_ifloor(s->cz / W_CHUNK_SIZE) - 2, n = 0;
    for (j = 0; j < 5; j++) for (i = 0; i < 5; i++) host_load(n++, c0x + i, c0z + j);
    for (i = 0; i < g_un; i++) if (u_own[i] == f) u_active[i] = 0;        /* no defenders: just the siege */
    g_cam[0] = s->cx; g_cam[2] = s->cz;
    /* engines outside the walls, on the side away from the gates */
    float a = s->gate_a[0] + 3.14159f, dx = cosf(a), dz = sinf(a);
    float rOut = s->R + 12.0f;
    int cat = unit_spawn(TEAM_PLAYER, s->cx + dx * (rOut + 14.0f), s->cz + dz * (rOut + 14.0f), 0, 11u, UC_CATAPULT, 0);
    int tre = unit_spawn(TEAM_PLAYER, s->cx + dx * (rOut + 45.0f) + dz * 10.0f, s->cz + dz * (rOut + 45.0f) - dx * 10.0f, 0, 12u, UC_TREB, 0);
    /* the ram 3 units outside the wall section nearest that side */
    float px0 = s->cx + dx * (s->R + 6.0f) - dz * 12.0f, pz0 = s->cz + dz * (s->R + 6.0f) + dx * 12.0f, best = 1e9f;
    float rx = px0, rz = pz0;
    for (i = 0; i < g_el_n; i++) {
        if (el_own[i] != f || el_kind[i] != EK_WALL) continue;
        float mx = (el_x[i] + el_x2[i]) * 0.5f, mz = (el_z[i] + el_z2[i]) * 0.5f;
        float d = (mx - px0) * (mx - px0) + (mz - pz0) * (mz - pz0);
        if (d < best) { best = d; float ox = mx - s->cx, oz = mz - s->cz, ol = sqrtf(ox * ox + oz * oz); rx = mx + ox / ol * 3.0f; rz = mz + oz / ol * 3.0f; }
    }
    int ram = unit_spawn(TEAM_PLAYER, rx, rz, 0, 13u, UC_RAM, 0);
    printf("catapult %d, trebuchet %d, ram %d spawned\n", cat, tre, ram);
    float dt = 1.0f / 30.0f;
    int step, dead0 = 0, e;
    for (step = 0; step < 30 * 150; step++) {
        g_time = g_time + dt;
        units_update(dt);
        /* stream: rebuild what was knocked down */
        while (g_rebuild_n > 0) {
            int cx = g_rebuild_cx[0], cz = g_rebuild_cz[0], k, sl = find_slot(cx, cz);
            for (k = 1; k < g_rebuild_n; k++) { g_rebuild_cx[k - 1] = g_rebuild_cx[k]; g_rebuild_cz[k - 1] = g_rebuild_cz[k]; }
            g_rebuild_n--;
            if (sl >= 0) host_load(sl, cx, cz);
        }
        if (step % (30 * 15) == 0) {
            int d = 0;
            for (e = 0; e < g_el_n; e++) if (el_own[e] == f && el_dead[e]) d++;
            printf("t=%3ds: castle %d pieces down %d; catapult st %d tgt %d | treb state %d st %d | ram st %d state %d swing %.2f | stones in flight %d\n",
                   (int)g_time, f, d, u_sstate[cat], u_st[cat], u_sstate[tre], u_st[tre], u_st[ram], u_state[ram], u_swing[ram],
                   ({ int c = 0, k; for (k = 0; k < MAXP; k++) if (p_active[k] && a_kind[k] && a_flying[k]) c++; c; }));
        }
    }
    /* breaches: the nav of destroyed wall pieces is open ground now */
    int open = 0, closed = 0;
    for (e = 0; e < g_el_n; e++) {
        if (el_own[e] != f || !el_dead[e] || el_kind[e] != EK_WALL) continue;
        float mx = (el_x[e] + el_x2[e]) * 0.5f, mz = (el_z[e] + el_z2[e]) * 0.5f, h;
        int fl = nav_node(0, w_ifloor(mx), w_ifloor(mz), &h);
        if (fl & NAV_WALK) open++; else closed++;
        dead0++;
    }
    printf("wall sections down %d: ground walkable at %d, still blocked at %d; destroyed list %d\n", dead0, open, closed, g_ndestroyed);
    return 0;
}
