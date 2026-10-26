/* Host check for the enemy-castle AI (castles_ai.h + castles_planner.h,
 * through the real castles_units.h): sets the factions up and runs the
 * game for 15 minutes with no land loaded -- exactly the situation of
 * enemy castles far from the camera -- printing the AI's decisions,
 * construction, research and attack waves as they happen.
 *
 *   gcc -O2 -I. -o /tmp/aitest android/examples/castles_ai_hosttest.c -lm
 *
 * The android-side declarations the units code needs (chunk slots,
 * camera, angle helpers) are pasted in from castles_android.c by the
 * build line in its comment (see castles_ai_hosttest.sh). */
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

int main(void) {
    int f, i, step;
    world_init();
    units_init();
    units_home_init();
    ai_init();
    dragons_init();
    float dt = 1.0f / 20.0f;
    for (step = 0; step < 20 * 60 * 15; step++) {
        g_time = g_time + dt;
        units_update(dt);
        if (step % (20 * 60) == 20 * 60 - 1) {
            printf("--- minute %d\n", (step + 1) / (20 * 60));
            for (f = 1; f < F_MAX; f++) if (f_on[f]) ai_log(f);
            int nwave = 0;
            float dmin = 1e9f;
            for (i = 0; i < g_un; i++) {
                if (!unit_alive(i) || u_role[i] != R_WAVE) continue;
                nwave++;
                float dx = u_x[i] - g_hsite.cx, dz = u_z[i] - g_hsite.cz;
                float d = sqrtf(dx * dx + dz * dz);
                if (d < dmin) dmin = d;
            }
            if (nwave) printf("  waves: %d soldiers marching, nearest %.0f from the player's castle\n", nwave, dmin);
        }
    }
    return 0;
}
