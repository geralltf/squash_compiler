/* Host-side checker for castles_world.h (dev tool, not part of the APK).
 *
 *   gcc -O2 -I . android/examples/castles_world_hosttest.c -o /tmp/castles_test
 *   /tmp/castles_test [center_cx center_cz]
 *
 * Prints:
 *   - the same world_selftest() checksums castles_android.c logs on the
 *     device at startup ("selftest[i]=..." under logcat tag "castles");
 *     any mismatch means squash's ARM64 build computes something
 *     differently from this gcc build;
 *   - generation stats for a 9x9 block of chunks (castles, WFC retries,
 *     vertex counts vs. the 30000-vertex GPU slot, ms per chunk);
 *   - a seam check: every WFC edge socket on every shared chunk border
 *     must agree from both sides;
 *   - an ASCII map of the block, one char per WFC cell:
 *       ~ water  . land  ^ mountain  T forest  | - + * road (* = well)
 *       H bridge  O tower  # wall/ramp-wall  G gate  : courtyard  K keep */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "android/examples/castles_world.h"

#define CAP 60000
#define R 4
#define NCH (2 * R + 1)

static float g_buf[CAP * W_VSTRIDE];
static int g_res[NCH * NCH][W_NCELLS];
static unsigned char g_cls[NCH * NCH][W_NCELLS];

static char cell_char(int t, int cls) {
    const char *base = "~.^=#T";
    if (t < 0) return base[cls];
    switch (g_tkind[t]) {
    case K_KEEP:   return 'K';
    case K_TOWER:  return 'O';
    case K_GATE:   return 'G';
    case K_WALL:
    case K_RAMP:   return '#';
    case K_COURT:  return ':';
    case K_BRIDGE: return 'H';
    case K_ROAD: {
        int m = g_tparam[t];
        if (m == 5) return '|';
        if (m == 10) return '-';
        if (w_popcount4(m) == 1) return '*';
        return '+';
    }
    default:       return base[cls];
    }
}

int main(int argc, char **argv) {
    int st[64], n, i, j, x, z;
    world_init();
    n = world_selftest(st, g_buf, CAP);
    printf("selftest:");
    for (i = 0; i < n; i++) printf(" %d", st[i]);
    printf("\n");

    int ox = argc > 1 ? atoi(argv[1]) : 0, oz = argc > 2 ? atoi(argv[2]) : 0;
    int maxv = 0, castles = 0, retried = 0;
    long total = 0;
    clock_t t0 = clock();
    for (j = 0; j < NCH; j++) {
        for (i = 0; i < NCH; i++) {
            int vn = world_gen_chunk(ox + i - R, oz + j - R, g_buf, CAP);
            if (vn > maxv) maxv = vn;
            total += vn;
            castles += g_out_has_castle;
            if (g_wfc_attempts > 1) retried++;
            memcpy(g_res[j * NCH + i], g_result, sizeof g_result);
            memcpy(g_cls[j * NCH + i], g_out_cells, W_NCELLS);
        }
    }
    double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / (NCH * NCH);
    printf("chunks=%d castles=%d wfc_retried=%d maxverts=%d (slot 30000) avgverts=%ld ms/chunk=%.2f\n",
           NCH * NCH, castles, retried, maxv, total / (NCH * NCH), ms);

    int bad = 0;
    for (j = 0; j < NCH; j++) {
        for (i = 0; i < NCH; i++) {
            int *a = g_res[j * NCH + i];
            if (i + 1 < NCH) {
                int *b = g_res[j * NCH + i + 1];
                for (z = 0; z < W_CELLS; z++)
                    if (g_tsock[a[z * W_CELLS + W_CELLS - 1] * 4 + D_E] != g_tsock[b[z * W_CELLS] * 4 + D_W]) bad++;
            }
            if (j + 1 < NCH) {
                int *b = g_res[(j + 1) * NCH + i];
                for (x = 0; x < W_CELLS; x++)
                    if (g_tsock[a[(W_CELLS - 1) * W_CELLS + x] * 4 + D_S] != g_tsock[b[x] * 4 + D_N]) bad++;
            }
        }
    }
    printf("seam mismatches=%d\n", bad);

    for (z = 0; z < NCH * W_CELLS; z++) {
        for (x = 0; x < NCH * W_CELLS; x++) {
            int ci = (z / W_CELLS) * NCH + x / W_CELLS, c = (z % W_CELLS) * W_CELLS + x % W_CELLS;
            int cls = g_cls[ci][c];
            if (cls > 5) cls = 1;
            putchar(cell_char(g_res[ci][c], cls == 3 || cls == 4 ? 1 : cls));
        }
        putchar('\n');
    }
    return bad != 0;
}
