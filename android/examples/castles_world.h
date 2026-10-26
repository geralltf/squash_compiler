/* ---- castles_world.h: procedural world for castles_android.c ----
 *
 * Seamless, infinite, chunked terrain + Wave Function Collapse (WFC)
 * structure generation, emitted as flat-shaded low-poly triangle meshes.
 * Pure C with no Vulkan/Android dependency, so the exact same file also
 * builds with a host gcc for logic testing (see world_selftest()).
 *
 * Layers, in generation order for one chunk:
 *   1. Terrain height  -- 2D simplex fBm "continentalness" decides sea vs
 *      land, a ridged simplex layer raises mountain ranges where land is
 *      high, plus small detail bumps. Height is a pure function of WORLD
 *      coordinates, so every chunk border matches its neighbor exactly.
 *   2. Kingdom density -- a second, very low-frequency simplex field
 *      ("kingdom") decides how often castles appear: dense castle
 *      regions, empty wilderness in between.
 *   3. Chunk-border road crossings -- whether a road crosses the edge
 *      shared by two chunks, and at which cell, is a hash of that EDGE's
 *      own global coordinates (plus terrain/kingdom checks on both
 *      sides), so the two chunks always agree without either having to
 *      exist first. This is what makes WFC output seamless.
 *   4. A road/castle plan (Dijkstra paths from each crossing to a hub --
 *      the castle gate, or a village well) that only BIASES the WFC
 *      weights; it never forces a tile.
 *   5. WFC over a 12x12 cell grid with edge sockets (ground, road,
 *      courtyard, and one directional wall-run socket per castle side).
 *      The socket rules alone are what force castles to be closed
 *      rectangles with corner towers, walls/ramp-walls and road-facing
 *      gates; roads to be continuous; and bridges to span water.
 *   6. Mesh emission -- terrain (flattened under castles), water,
 *      roads, bridges, trees, rocks, houses, and castle pieces.
 *
 * squash ARM64 codegen caveats this file deliberately works around (see
 * project notes / buttons_android.c for each investigation):
 *   - no non-zero global initializers anywhere (they read back as 0):
 *     every table is filled at runtime by world_init();
 *   - no "x += call()" -- always hoist the call into a local first;
 *   - no float variable updated from an expression reading itself
 *     ("r = 0.5f*(r + x/r)") -- write to a fresh local, then copy;
 *   - at most 6 float parameters per function, no struct-by-value
 *     parameters/returns, no 2D arrays, no function-local statics. */

#ifndef CASTLES_WORLD_H
#define CASTLES_WORLD_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* World dimensions                                                    */
/* ------------------------------------------------------------------ */
#define W_CELLS        12                   /* WFC cells per chunk side      */
#define W_NCELLS       (W_CELLS * W_CELLS)
#define W_CELL_SIZE    4.0f                 /* world units per cell          */
#define W_SUB          2                    /* terrain quads per cell side   */
#define W_GRID         (W_CELLS * W_SUB)    /* terrain quads per chunk side  */
#define W_GRIDV        (W_GRID + 1)         /* terrain verts per chunk side  */
#define W_STEP         (W_CELL_SIZE / W_SUB)
#define W_CHUNK_SIZE   (W_CELL_SIZE * W_CELLS)
#define W_VSTRIDE      9                    /* floats per vertex: pos nrm col */

#define W_SEED_H   0x1234567u
#define W_SEED_R   0x7654321u
#define W_SEED_D   0x0badf00u
#define W_SEED_K   0x5eed001u
#define W_SEED_C   0xca57e00u
#define W_SEED_E   0x00ed9e5u
#define W_SEED_T   0x7ee5000u

/* Sockets on a cell edge. Two neighboring cells are compatible iff the
 * sockets on their shared edge are equal. One wall-run socket per castle
 * side keeps walls from joining up in impossible ways. */
#define SK_G   0   /* ground: anything that is not road/castle            */
#define SK_R   1   /* road                                                */
#define SK_C   2   /* castle courtyard interior                           */
#define SK_WN  3   /* along a north wall run (also: tower NW/NE side)     */
#define SK_WE  4
#define SK_WS  5
#define SK_WW  6

/* Directions: N = -z, E = +x, S = +z, W = -x. Road masks use bit (1<<d). */
#define D_N 0
#define D_E 1
#define D_S 2
#define D_W 3

/* Tile kinds */
#define K_GRASS   0
#define K_FOREST  1
#define K_WATER   2
#define K_ROCKS   3
#define K_ROAD    4   /* param = road mask                       */
#define K_BRIDGE  5   /* param = road mask (5 = N-S, 10 = E-W)    */
#define K_TOWER   6   /* param = corner 0 NW, 1 NE, 2 SE, 3 SW    */
#define K_WALL    7   /* param = side (outward direction)        */
#define K_RAMP    8   /* param = side; wall with an inner ramp   */
#define K_GATE    9   /* param = side                            */
#define K_COURT  10
#define K_KEEP   11
#define K_DOCK   12   /* param = side facing the water; road on the opposite side */

#define W_MAXT 48
static int g_nt;                               /* tile count              */
static unsigned char g_tsock[W_MAXT * 4];      /* sockets N,E,S,W         */
static unsigned char g_tkind[W_MAXT];
static unsigned char g_tparam[W_MAXT];
static int g_tile_keep;                        /* index of the KEEP tile  */
static int g_tile_water;
static int g_tile_road_base;                   /* road tile for mask m is g_tile_road_base + m - 1 */
static int g_tile_tower0, g_tile_wall0, g_tile_ramp0, g_tile_gate0, g_tile_court, g_tile_dock0;

/* Cell classes */
#define CC_LAND     0
#define CC_WATER    1
#define CC_MOUNTAIN 2

/* ------------------------------------------------------------------ */
/* Small math (own implementations: no libm dependency)                */
/* ------------------------------------------------------------------ */
#define W_PI 3.14159265f

static int w_ifloor(float x) {
    int i = (int)x;
    if ((float)i > x) i = i - 1;
    return i;
}

static float w_absf(float x) { return x < 0.0f ? -x : x; }

static float w_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float w_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float r = 1.0f;
    if (x > 1.0f) r = x * 0.5f;
    if (x > 400.0f) r = x * 0.03f;
    int i;
    for (i = 0; i < 18; i++) {
        float nr = 0.5f * (r + x / r);
        r = nr;
    }
    return r;
}

static float w_sin(float x) {
    float k = x * (1.0f / (2.0f * W_PI));
    int ki = w_ifloor(k + 0.5f);
    float r = x - (float)ki * (2.0f * W_PI);
    if (r > 0.5f * W_PI) { float t = W_PI - r; r = t; }
    if (r < -0.5f * W_PI) { float t = -W_PI - r; r = t; }
    float r2 = r * r;
    float p = 1.0f - r2 / 72.0f;
    float p2 = 1.0f - r2 / 42.0f * p;
    float p3 = 1.0f - r2 / 20.0f * p2;
    float p4 = 1.0f - r2 / 6.0f * p3;
    return r * p4;
}

static float w_cos(float x) { return w_sin(x + 0.5f * W_PI); }

/* atan2(y, x) approximation (~0.01 rad). */
static float approx_atan2_w(float y, float x) {
    float ax = x < 0.0f ? -x : x, ay = y < 0.0f ? -y : y;
    float mx = ax > ay ? ax : ay;
    float mn = ax > ay ? ay : ax;
    if (mx < 1e-9f) return 0.0f;
    float a = mn / mx;
    float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) { float t = 1.5707963f - r; r = t; }
    if (x < 0.0f) { float t = W_PI - r; r = t; }
    if (y < 0.0f) { float t = -r; r = t; }
    return r;
}

static float w_smooth(float e0, float e1, float x) {
    float t = w_clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* ------------------------------------------------------------------ */
/* Hashing (explicitly masked to 32 bits)                              */
/* ------------------------------------------------------------------ */
static uint32_t w_hash(uint32_t x) {
    x = x & 0xFFFFFFFFu;
    x = x ^ (x >> 16);
    x = (x * 0x7feb352du) & 0xFFFFFFFFu;
    x = x ^ (x >> 15);
    x = (x * 0x846ca68bu) & 0xFFFFFFFFu;
    x = x ^ (x >> 16);
    return x;
}

static uint32_t w_hash2(int x, int z, uint32_t seed) {
    uint32_t hz = w_hash(((uint32_t)z + seed) & 0xFFFFFFFFu);
    uint32_t hx = (((uint32_t)x * 0x9E3779B1u) & 0xFFFFFFFFu) ^ hz;
    return w_hash(hx);
}

static float w_hash01(int x, int z, uint32_t seed) {
    uint32_t h = w_hash2(x, z, seed);
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

/* ------------------------------------------------------------------ */
/* 2D simplex noise (gradients from a hash, no permutation table)      */
/* ------------------------------------------------------------------ */
#define W_F2 0.36602540378f
#define W_G2 0.21132486540f

static float w_grad(int ix, int iz, float x, float z, uint32_t seed) {
    uint32_t h = w_hash2(ix, iz, seed) & 7u;
    if (h == 0u) return x + z;
    if (h == 1u) return -x + z;
    if (h == 2u) return x - z;
    if (h == 3u) return -x - z;
    if (h == 4u) return x;
    if (h == 5u) return -x;
    if (h == 6u) return z;
    return -z;
}

static float w_simplex(float x, float z, uint32_t seed) {
    float s = (x + z) * W_F2;
    int i = w_ifloor(x + s);
    int j = w_ifloor(z + s);
    float t = (float)(i + j) * W_G2;
    float x0 = x - ((float)i - t);
    float z0 = z - ((float)j - t);
    int i1 = 0, j1 = 1;
    if (x0 > z0) { i1 = 1; j1 = 0; }
    float x1 = x0 - (float)i1 + W_G2;
    float z1 = z0 - (float)j1 + W_G2;
    float x2 = x0 - 1.0f + 2.0f * W_G2;
    float z2 = z0 - 1.0f + 2.0f * W_G2;
    float n = 0.0f;
    float t0 = 0.5f - x0 * x0 - z0 * z0;
    if (t0 > 0.0f) {
        float g = w_grad(i, j, x0, z0, seed);
        float tt = t0 * t0;
        n = n + tt * tt * g;
    }
    float t1 = 0.5f - x1 * x1 - z1 * z1;
    if (t1 > 0.0f) {
        float g = w_grad(i + i1, j + j1, x1, z1, seed);
        float tt = t1 * t1;
        n = n + tt * tt * g;
    }
    float t2 = 0.5f - x2 * x2 - z2 * z2;
    if (t2 > 0.0f) {
        float g = w_grad(i + 1, j + 1, x2, z2, seed);
        float tt = t2 * t2;
        n = n + tt * tt * g;
    }
    return 70.0f * n;
}

static float w_fbm(float x, float z, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    int o;
    for (o = 0; o < octaves; o++) {
        float n = w_simplex(x * freq, z * freq, seed + (uint32_t)o * 1013u);
        float s2 = sum + amp * n;
        float n2 = norm + amp;
        float a2 = amp * 0.5f;
        float f2 = freq * 2.03f;
        sum = s2; norm = n2; amp = a2; freq = f2;
    }
    return sum / norm;
}

/* ------------------------------------------------------------------ */
/* Terrain height + regional fields                                    */
/* ------------------------------------------------------------------ */
#define W_SEA_LEVEL 0.0f

/* Continentalness in [0,1]: sea below ~0.42. */
static float w_continent(float x, float z) {
    float e = w_fbm(x * 0.0042f, z * 0.0042f, 5, W_SEED_H);
    return 0.5f + 0.75f * e;
}

/* Natural terrain height (no castle plateaus) -- see w_height in
 * castles_sites.h for the height everything actually uses. */
static float w_height_raw(float x, float z) {
    float c = w_continent(x, z);
    /* Gentle lowlands: land rises slowly away from the coast. */
    float base = (c - 0.37f) * 26.0f;
    float rn = w_simplex(x * 0.0105f, z * 0.0105f, W_SEED_R);
    float ridge = 1.0f - w_absf(rn);
    float ridge2 = ridge * ridge;
    /* Mountain ranges only in the continental cores. */
    float mmask = w_smooth(0.60f, 0.85f, c);
    float hills = w_simplex(x * 0.02f, z * 0.02f, W_SEED_R + 9u);
    float detail = w_simplex(x * 0.045f, z * 0.045f, W_SEED_D);
    float h = base + mmask * ridge2 * 42.0f + hills * 1.6f * (0.3f + mmask) + detail * 0.6f;
    if (h < 0.0f) { float hs = h * 0.7f; h = hs; }
    return h;
}

/* Kingdom density in [0,1]: how castle-friendly a region is. */
static float w_kingdom(int cx, int cz) {
    float k = w_simplex((float)cx * 0.17f, (float)cz * 0.17f, W_SEED_K);
    return w_clampf(0.5f + 0.6f * k, 0.0f, 1.0f);
}

/* Forest density in [0,1] at a world point. */
static float w_forest(float x, float z) {
    float f = w_fbm(x * 0.012f, z * 0.012f, 3, W_SEED_T);
    return w_clampf(0.45f + 0.9f * f, 0.0f, 1.0f);
}

#include "android/examples/castles_sites.h"

static int w_cell_class_at(float wx, float wz) {
    float h = w_height(wx, wz);
    if (h < -0.15f) return CC_WATER;
    if (h > 18.0f) return CC_MOUNTAIN;
    return CC_LAND;
}

/* Road crossing on the EAST edge of chunk (cx,cz) (shared with the WEST
 * edge of (cx+1,cz)). Returns the crossing cell row 0..11, or -1. */
static int w_edge_road_east(int cx, int cz) {
    uint32_t h = w_hash2(cx * 2 + 1, cz * 2, W_SEED_E);
    int k = 2 + (int)(h % 8u);
    float p = 0.18f + 0.55f * 0.5f * (w_kingdom(cx, cz) + w_kingdom(cx + 1, cz));
    float roll = (float)((h >> 8) & 0xFFFFu) * (1.0f / 65536.0f);
    if (roll >= p) return -1;
    float ex = (float)(cx + 1) * W_CHUNK_SIZE;
    float ez = (float)cz * W_CHUNK_SIZE + ((float)k + 0.5f) * W_CELL_SIZE;
    /* never into a castle clearing (castles get their own gate roads) */
    if (w_in_clearing(ex - 2.0f, ez) || w_in_clearing(ex + 2.0f, ez)) return -1;
    if (w_cell_class_at(ex - 2.0f, ez) != CC_LAND) return -1;
    if (w_cell_class_at(ex + 2.0f, ez) != CC_LAND) return -1;
    if (w_cell_class_at(ex - 6.0f, ez) != CC_LAND) return -1;
    if (w_cell_class_at(ex + 6.0f, ez) != CC_LAND) return -1;
    return k;
}

/* Road crossing on the SOUTH edge of chunk (cx,cz). Returns column or -1. */
static int w_edge_road_south(int cx, int cz) {
    uint32_t h = w_hash2(cx * 2, cz * 2 + 1, W_SEED_E);
    int k = 2 + (int)(h % 8u);
    float p = 0.18f + 0.55f * 0.5f * (w_kingdom(cx, cz) + w_kingdom(cx, cz + 1));
    float roll = (float)((h >> 8) & 0xFFFFu) * (1.0f / 65536.0f);
    if (roll >= p) return -1;
    float ex = (float)cx * W_CHUNK_SIZE + ((float)k + 0.5f) * W_CELL_SIZE;
    float ez = (float)(cz + 1) * W_CHUNK_SIZE;
    if (w_in_clearing(ex, ez - 2.0f) || w_in_clearing(ex, ez + 2.0f)) return -1;
    if (w_cell_class_at(ex, ez - 2.0f) != CC_LAND) return -1;
    if (w_cell_class_at(ex, ez + 2.0f) != CC_LAND) return -1;
    if (w_cell_class_at(ex, ez - 6.0f) != CC_LAND) return -1;
    if (w_cell_class_at(ex, ez + 6.0f) != CC_LAND) return -1;
    return k;
}

/* ------------------------------------------------------------------ */
/* Tile set                                                            */
/* ------------------------------------------------------------------ */
static int w_add_tile(int kind, int param, int sn, int se, int ss, int sw) {
    int t = g_nt;
    g_tkind[t] = (unsigned char)kind;
    g_tparam[t] = (unsigned char)param;
    g_tsock[t * 4 + 0] = (unsigned char)sn;
    g_tsock[t * 4 + 1] = (unsigned char)se;
    g_tsock[t * 4 + 2] = (unsigned char)ss;
    g_tsock[t * 4 + 3] = (unsigned char)sw;
    g_nt = t + 1;
    return t;
}

/* Wall-ish tile for castle side s: outward socket `out`, inward = C, the
 * two along-wall directions get that side's own run socket. */
static void w_add_wall_tile(int kind, int s, int out) {
    int sk[4];
    int run = SK_WN + s;
    sk[s] = out;
    sk[(s + 2) & 3] = SK_C;
    sk[(s + 1) & 3] = run;
    sk[(s + 3) & 3] = run;
    w_add_tile(kind, s, sk[0], sk[1], sk[2], sk[3]);
}

static void world_init_tiles(void) {
    int m, s;
    g_nt = 0;
    w_add_tile(K_GRASS, 0, SK_G, SK_G, SK_G, SK_G);
    w_add_tile(K_FOREST, 0, SK_G, SK_G, SK_G, SK_G);
    g_tile_water = w_add_tile(K_WATER, 0, SK_G, SK_G, SK_G, SK_G);
    w_add_tile(K_ROCKS, 0, SK_G, SK_G, SK_G, SK_G);
    g_tile_road_base = g_nt;
    for (m = 1; m < 16; m++) {
        w_add_tile(K_ROAD, m,
                   (m & 1) ? SK_R : SK_G, (m & 2) ? SK_R : SK_G,
                   (m & 4) ? SK_R : SK_G, (m & 8) ? SK_R : SK_G);
    }
    w_add_tile(K_BRIDGE, 5, SK_R, SK_G, SK_R, SK_G);
    w_add_tile(K_BRIDGE, 10, SK_G, SK_R, SK_G, SK_R);
    /* Corner towers: the two outward sides are ground, the other two
     * continue the adjoining walls' run sockets. */
    g_tile_tower0 = g_nt;
    w_add_tile(K_TOWER, 0, SK_G, SK_WN, SK_WW, SK_G);   /* NW */
    w_add_tile(K_TOWER, 1, SK_G, SK_G, SK_WE, SK_WN);   /* NE */
    w_add_tile(K_TOWER, 2, SK_WE, SK_G, SK_G, SK_WS);   /* SE */
    w_add_tile(K_TOWER, 3, SK_WW, SK_WS, SK_G, SK_G);   /* SW */
    g_tile_wall0 = g_nt;
    for (s = 0; s < 4; s++) w_add_wall_tile(K_WALL, s, SK_G);
    g_tile_ramp0 = g_nt;
    for (s = 0; s < 4; s++) w_add_wall_tile(K_RAMP, s, SK_G);
    g_tile_gate0 = g_nt;
    for (s = 0; s < 4; s++) w_add_wall_tile(K_GATE, s, SK_R);
    g_tile_court = w_add_tile(K_COURT, 0, SK_C, SK_C, SK_C, SK_C);
    g_tile_keep = w_add_tile(K_KEEP, 0, SK_C, SK_C, SK_C, SK_C);
    /* Fishing docks: water on side s, a road arriving from the opposite
     * side -- so WFC can only put one where the road network reaches the
     * shore (the planner offers them, see w_plan_dock). */
    g_tile_dock0 = g_nt;
    for (s = 0; s < 4; s++) {
        int sk[4];
        sk[0] = SK_G; sk[1] = SK_G; sk[2] = SK_G; sk[3] = SK_G;
        sk[(s + 2) & 3] = SK_R;
        w_add_tile(K_DOCK, s, sk[0], sk[1], sk[2], sk[3]);
    }
}

static int w_is_castle_kind(int k) { return k >= K_TOWER && k <= K_KEEP; }

static int w_popcount4(int m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1); }

/* ------------------------------------------------------------------ */
/* Per-chunk generation state (one chunk is generated at a time)       */
/* ------------------------------------------------------------------ */
static int g_ch_cx, g_ch_cz;
static float g_hgt[W_GRIDV * W_GRIDV];        /* terrain heights            */
static unsigned char g_cclass[W_NCELLS];      /* CC_* per cell              */
static float g_cforest[W_NCELLS];
static float g_cheight[W_NCELLS];             /* height at cell center      */
static unsigned char g_plan[W_NCELLS];        /* planned road mask          */
static unsigned char g_cplan[W_NCELLS];       /* planned castle tile + 1    */
static unsigned char g_czone[W_NCELLS];       /* castle tiles allowed here  */
static int g_border[4 * W_CELLS];             /* border socket per edge cell, side-major */
static int g_has_castle, g_castle_cx, g_castle_cz, g_castle_half, g_castle_gate_side;
static float g_castle_base;                   /* flattened ground height    */
static int g_hub;                             /* plan hub cell or -1        */
/* Castle clearings (castles_sites.h): fixed cells are pre-collapsed to
 * grass (or, for a gate road's last cell, a road end pointing out of the
 * clearing); anchors are the first cells outside, where WFC must pick
 * the gate road up. */
static unsigned char g_cfixed[W_NCELLS];
static unsigned char g_cterm[W_NCELLS];       /* dir+1: fixed road end toward anchor */
static unsigned char g_canchor[W_NCELLS];     /* dir+1: anchor, road toward the castle */
static unsigned char g_dplan[W_NCELLS];       /* water dir+1: planned fishing dock  */

/* WFC state */
static unsigned char g_allow[W_NCELLS * W_MAXT];
static int g_count[W_NCELLS];
static int g_result[W_NCELLS];
static float g_weight[W_NCELLS * W_MAXT];
static int g_queue[W_NCELLS * 8];
static unsigned char g_inq[W_NCELLS];
static uint32_t g_rng;
static int g_wfc_attempts;                    /* stats for logging          */

static uint32_t w_rand(void) {
    uint32_t x = g_rng;
    x = x ^ ((x << 13) & 0xFFFFFFFFu);
    x = x ^ (x >> 17);
    x = x ^ ((x << 5) & 0xFFFFFFFFu);
    g_rng = x;
    return x;
}

static float w_randf(void) {
    uint32_t r = w_rand();
    return (float)(r >> 8) * (1.0f / 16777216.0f);
}

static int g_ddx_w(int d) { return d == D_E ? 1 : (d == D_W ? -1 : 0); }
static int g_ddz_w(int d) { return d == D_S ? 1 : (d == D_N ? -1 : 0); }

static int w_nb(int c, int d) {
    int x = c % W_CELLS, z = c / W_CELLS;
    if (d == D_N) { if (z == 0) return -1; return c - W_CELLS; }
    if (d == D_S) { if (z == W_CELLS - 1) return -1; return c + W_CELLS; }
    if (d == D_E) { if (x == W_CELLS - 1) return -1; return c + 1; }
    if (x == 0) return -1;
    return c - 1;
}

/* Border socket required on the outward edge of border cell c in
 * direction d, or -1 if (c,d) is not a chunk-border edge. */
static int w_border_socket(int c, int d) {
    int x = c % W_CELLS, z = c / W_CELLS;
    if (d == D_N && z == 0) return g_border[0 * W_CELLS + x];
    if (d == D_E && x == W_CELLS - 1) return g_border[1 * W_CELLS + z];
    if (d == D_S && z == W_CELLS - 1) return g_border[2 * W_CELLS + x];
    if (d == D_W && x == 0) return g_border[3 * W_CELLS + z];
    return -1;
}

/* ------------------------------------------------------------------ */
/* Planning: road paths (Dijkstra) and castle layout. Plans only bias  */
/* WFC weights.                                                        */
/* ------------------------------------------------------------------ */
static int g_dist[W_NCELLS];
static int g_prev[W_NCELLS];
static unsigned char g_done[W_NCELLS];

static int w_cell_cost(int c) {
    if (g_czone[c]) return 1000000;
    if (g_cclass[c] == CC_WATER) return 7;
    if (g_cclass[c] == CC_MOUNTAIN) return 60;
    return 2;
}

static int w_dir_between(int a, int b) {
    if (b == a - W_CELLS) return D_N;
    if (b == a + W_CELLS) return D_S;
    if (b == a + 1) return D_E;
    return D_W;
}

/* Shortest path from `from` to `to`; marks plan bits along it. */
static void w_plan_path(int from, int to) {
    int i, d;
    for (i = 0; i < W_NCELLS; i++) { g_dist[i] = 0x3fffffff; g_prev[i] = -1; g_done[i] = 0; }
    g_dist[from] = 0;
    for (;;) {
        int best = -1, bd = 0x3fffffff;
        for (i = 0; i < W_NCELLS; i++) {
            if (!g_done[i] && g_dist[i] < bd) { bd = g_dist[i]; best = i; }
        }
        if (best < 0 || best == to) break;
        g_done[best] = 1;
        for (d = 0; d < 4; d++) {
            int n = w_nb(best, d);
            if (n < 0 || g_done[n]) continue;
            int cost = w_cell_cost(n);
            if (n == to) cost = 1;
            int nd = bd + cost;
            if (nd < g_dist[n]) { g_dist[n] = nd; g_prev[n] = best; }
        }
    }
    if (g_dist[to] >= 1000000) return;
    int c = to;
    while (c != from && c >= 0) {
        int p = g_prev[c];
        if (p < 0) break;
        int dpc = w_dir_between(p, c);
        int dcp = (dpc + 2) & 3;
        g_plan[p] = (unsigned char)(g_plan[p] | (1 << dpc));
        g_plan[c] = (unsigned char)(g_plan[c] | (1 << dcp));
        c = p;
    }
}

/* Fishing dock: on about half the chunks with a planned road reaching
 * the shore, one dry shore cell (water for two cells on one side, the
 * planned road right behind it) is offered a dock. The road cell gets a
 * branch toward it in the plan; WFC then decides with the K_DOCK
 * tiles' sockets. */
static void w_plan_dock(void) {
    int c, d, best = -1, bestd = 0, bestSpur = 0;
    uint32_t bh = 0xFFFFFFFFu;
    for (c = 0; c < W_NCELLS; c++) g_dplan[c] = 0;
    if ((w_hash2(g_ch_cx, g_ch_cz, 0xD0C5u) % 4u) == 0u) return;
    for (c = 0; c < W_NCELLS; c++) {
        if (g_cclass[c] != CC_LAND || g_cfixed[c] || g_plan[c] || g_canchor[c] || g_cheight[c] < 0.35f) continue;
        for (d = 0; d < 4; d++) {
            int n1 = w_nb(c, d), back = w_nb(c, (d + 2) & 3);
            if (n1 < 0 || back < 0 || g_cclass[n1] != CC_WATER || g_cheight[n1] > -0.8f) continue;
            if (g_cfixed[back] || g_cclass[back] != CC_LAND) continue;
            int n2 = w_nb(n1, d);
            if (n2 >= 0 ? g_cclass[n2] != CC_WATER : 0) continue;
            /* the road right behind, or one more cell back (a spur) */
            int spur = 0;
            if (!g_plan[back]) {
                int b2 = w_nb(back, (d + 2) & 3);
                if (b2 < 0 || !g_plan[b2] || g_cfixed[b2] || g_canchor[back]) continue;
                spur = 1;
            }
            uint32_t h = w_hash2(g_ch_cx * W_CELLS + c % W_CELLS, g_ch_cz * W_CELLS + c / W_CELLS, 0xD0C6u + (uint32_t)d);
            if (spur) h = h | 0x80000000u;               /* direct road access first */
            if (h < bh) { bh = h; best = c; bestd = d; bestSpur = spur; }
        }
    }
    if (best < 0) return;
    g_dplan[best] = (unsigned char)(bestd + 1);
    int od = (bestd + 2) & 3;
    int back = w_nb(best, od);
    g_plan[back] = (unsigned char)(g_plan[back] | (1 << bestd));
    if (bestSpur) {
        int b2 = w_nb(back, od);
        g_plan[back] = (unsigned char)(g_plan[back] | (1 << od));
        g_plan[b2] = (unsigned char)(g_plan[b2] | (1 << bestd));
    }
}

static void w_plan_chunk(void) {
    int i, x, z;
    int crossings[16];
    int ncross = 0;
    for (i = 0; i < W_NCELLS; i++) { g_plan[i] = 0; g_cplan[i] = 0; g_czone[i] = g_cfixed[i]; }
    g_hub = -1;
    g_castle_gate_side = D_S;

    /* Border crossings: border cell + the outward direction bit. */
    for (i = 0; i < W_CELLS; i++) {
        if (g_border[0 * W_CELLS + i] == SK_R) { int c = i; g_plan[c] = (unsigned char)(g_plan[c] | 1); crossings[ncross] = c; ncross++; }
        if (g_border[1 * W_CELLS + i] == SK_R) { int c = i * W_CELLS + W_CELLS - 1; g_plan[c] = (unsigned char)(g_plan[c] | 2); crossings[ncross] = c; ncross++; }
        if (g_border[2 * W_CELLS + i] == SK_R) { int c = (W_CELLS - 1) * W_CELLS + i; g_plan[c] = (unsigned char)(g_plan[c] | 4); crossings[ncross] = c; ncross++; }
        if (g_border[3 * W_CELLS + i] == SK_R) { int c = i * W_CELLS; g_plan[c] = (unsigned char)(g_plan[c] | 8); crossings[ncross] = c; ncross++; }
    }
    /* Castle gate-road anchors behave like crossings into the network;
     * fixed cells (castle clearings) are not part of the road plan. */
    for (i = 0; i < W_NCELLS && ncross < 16; i++) {
        if (g_canchor[i]) { g_plan[i] = (unsigned char)(g_plan[i] | (1 << (g_canchor[i] - 1))); crossings[ncross] = i; ncross++; }
    }
    {
        int m = 0;
        for (i = 0; i < ncross; i++) if (!g_cfixed[crossings[i]]) { crossings[m] = crossings[i]; m++; }
        ncross = m;
    }

    if (ncross > 0) {
        uint32_t h = w_hash2(g_ch_cx, g_ch_cz, 31337u);
        int hx = 4 + (int)(h % 4u), hz = 4 + (int)((h >> 4) % 4u);
        g_hub = hz * W_CELLS + hx;
        /* Prefer a dry hub outside castle clearings. */
        if (g_cclass[g_hub] != CC_LAND || g_cfixed[g_hub]) {
            int bestc = -1, bestd = 1000;
            for (i = 0; i < W_NCELLS; i++) {
                if (g_cclass[i] != CC_LAND || g_cfixed[i]) continue;
                int ix = i % W_CELLS, iz = i / W_CELLS;
                if (ix < 1 || iz < 1 || ix > W_CELLS - 2 || iz > W_CELLS - 2) continue;
                int dd = (ix > hx ? ix - hx : hx - ix) + (iz > hz ? iz - hz : hz - iz);
                if (dd < bestd) { bestd = dd; bestc = i; }
            }
            if (bestc >= 0) g_hub = bestc;
        }
    }
    if (g_hub >= 0) {
        for (i = 0; i < ncross; i++) {
            if (crossings[i] != g_hub) w_plan_path(crossings[i], g_hub);
        }
    }
    w_plan_dock();
}

/* ------------------------------------------------------------------ */
/* WFC                                                                 */
/* ------------------------------------------------------------------ */
static float w_road_base_weight(int m) {
    int pc = w_popcount4(m);
    if (pc == 1) return 0.04f;
    if (pc == 2) { if (m == 5 || m == 10) return 1.0f; return 0.7f; }
    if (pc == 3) return 0.25f;
    return 0.12f;
}

/* Fills the per-cell domain + weights. allow_castle=0 removes every
 * castle tile (fallback when a castle layout can't be solved). */
static void w_wfc_init(int allow_castle) {
    int c, t;
    for (c = 0; c < W_NCELLS; c++) {
        int cls = g_cclass[c];
        int plan = g_plan[c];
        float fo = g_cforest[c];
        int zone = allow_castle && g_czone[c];
        int cnt = 0;
        for (t = 0; t < g_nt; t++) {
            int k = g_tkind[t];
            float w = 0.0f;
            if (k == K_DOCK) {
                w = (!g_cfixed[c] && g_dplan[c] == g_tparam[t] + 1) ? 50.0f : 0.0f;
            } else if (w_is_castle_kind(k)) {
                if (zone) {
                    if (g_cplan[c] == t + 1) w = 30.0f;
                    else if (k == K_KEEP) w = 0.0f;
                    else w = 0.25f;
                }
            } else if (g_cfixed[c]) {
                /* castle clearing: grass, or the gate road's last cell */
                if (g_cterm[c]) w = (k == K_ROAD && g_tparam[t] == (1 << (g_cterm[c] - 1))) ? 1.0f : 0.0f;
                else w = (k == K_GRASS) ? 1.0f : 0.0f;
            } else if (cls == CC_WATER) {
                if (k == K_WATER) w = 1.0f;
                else if (k == K_BRIDGE) w = (plan == g_tparam[t]) ? 40.0f : 0.03f;
            } else if (k == K_WATER || k == K_BRIDGE) {
                w = 0.0f;
            } else if (k == K_ROAD) {
                int m = g_tparam[t];
                float bw = w_road_base_weight(m);
                if (plan != 0) w = (m == plan) ? 60.0f : bw * 0.5f;
                else w = bw * 0.015f;
                /* No roads up cliffs (they render as vertical stripes),
                 * except where a border crossing forces one in. */
                if (cls == CC_MOUNTAIN) {
                    int forced = g_canchor[c] != 0, dd;
                    for (dd = 0; dd < 4; dd++) if (w_border_socket(c, dd) == SK_R) forced = 1;
                    if (!forced) w = 0.0f;
                }
            } else {
                float gl = (plan != 0) ? 0.03f : 1.0f;
                if (zone) { float g2 = gl * 0.05f; gl = g2; }
                if (k == K_GRASS) w = gl * (cls == CC_MOUNTAIN ? 0.4f : (1.05f - fo));
                if (k == K_FOREST) w = gl * (cls == CC_MOUNTAIN ? 0.5f * fo : fo + 0.02f);
                if (k == K_ROCKS) w = gl * (cls == CC_MOUNTAIN ? 2.0f : 0.02f);
            }
            /* Border cells: outward edge must match the border socket. */
            int d;
            for (d = 0; d < 4 && w > 0.0f; d++) {
                int bs = w_border_socket(c, d);
                if (bs >= 0 && g_tsock[t * 4 + d] != bs) w = 0.0f;
            }
            g_weight[c * W_MAXT + t] = w;
            g_allow[c * W_MAXT + t] = (unsigned char)(w > 0.0f ? 1 : 0);
            if (w > 0.0f) cnt++;
        }
        g_count[c] = cnt;
        g_result[c] = -1;
        g_inq[c] = 0;
    }
    /* Pre-collapse the keep at the planned castle center. */
    if (allow_castle && g_has_castle) {
        int kc = g_castle_cz * W_CELLS + g_castle_cx;
        for (t = 0; t < g_nt; t++) g_allow[kc * W_MAXT + t] = (unsigned char)(t == g_tile_keep ? 1 : 0);
        g_count[kc] = 1;
    }
}

/* Arc-consistency propagation from every queued cell. 0 on contradiction. */
static int w_propagate(int qn) {
    int qh = 0;
    while (qh < qn) {
        int c = g_queue[qh];
        qh++;
        g_inq[c] = 0;
        int d;
        for (d = 0; d < 4; d++) {
            int n = w_nb(c, d);
            if (n < 0) continue;
            int mask = 0, t;
            for (t = 0; t < g_nt; t++) {
                if (g_allow[c * W_MAXT + t]) mask = mask | (1 << g_tsock[t * 4 + d]);
            }
            int od = (d + 2) & 3;
            int changed = 0;
            for (t = 0; t < g_nt; t++) {
                if (!g_allow[n * W_MAXT + t]) continue;
                if (!(mask & (1 << g_tsock[t * 4 + od]))) {
                    g_allow[n * W_MAXT + t] = 0;
                    g_count[n] = g_count[n] - 1;
                    changed = 1;
                }
            }
            if (g_count[n] <= 0) return 0;
            if (changed && !g_inq[n]) {
                if (qn >= W_NCELLS * 8) {
                    /* Compact the queue (entries before qh are consumed). */
                    int k, m = 0;
                    for (k = qh; k < qn; k++) { g_queue[m] = g_queue[k]; m++; }
                    qn = m; qh = 0;
                }
                g_queue[qn] = n; qn++;
                g_inq[n] = 1;
            }
        }
    }
    return 1;
}

static int w_wfc_run(uint32_t seed, int allow_castle) {
    int c, t;
    g_rng = seed | 1u;
    w_wfc_init(allow_castle);
    for (c = 0; c < W_NCELLS; c++) {
        if (g_count[c] <= 0) return 0;
    }
    /* Initial propagation from every cell. */
    int qn = 0;
    for (c = 0; c < W_NCELLS; c++) { g_queue[qn] = c; qn++; g_inq[c] = 1; }
    if (!w_propagate(qn)) return 0;
    for (;;) {
        /* Observe: lowest-entropy undecided cell (count, then weight
         * spread), ties broken randomly. */
        int best = -1;
        float bestE = 1e30f;
        for (c = 0; c < W_NCELLS; c++) {
            if (g_result[c] >= 0) continue;
            float sw = 0.0f, mx = 0.0f;
            for (t = 0; t < g_nt; t++) {
                if (g_allow[c * W_MAXT + t]) {
                    float w = g_weight[c * W_MAXT + t];
                    float s2 = sw + w;
                    sw = s2;
                    if (w > mx) mx = w;
                }
            }
            /* Lower = more decided: (1 - dominant tile share) plus count. */
            float share = sw > 0.0f ? mx / sw : 1.0f;
            float rj = w_randf();
            float e = (float)g_count[c] * 0.02f + (1.0f - share) + rj * 0.001f;
            if (g_count[c] == 1) e = -1.0f + rj * 0.001f;
            if (e < bestE) { bestE = e; best = c; }
        }
        if (best < 0) break;
        /* Collapse by weight. */
        float sw = 0.0f;
        for (t = 0; t < g_nt; t++) {
            if (g_allow[best * W_MAXT + t]) { float s2 = sw + g_weight[best * W_MAXT + t]; sw = s2; }
        }
        float rr = w_randf();
        float pick = rr * sw;
        int chosen = -1;
        for (t = 0; t < g_nt; t++) {
            if (!g_allow[best * W_MAXT + t]) continue;
            chosen = t;
            float p2 = pick - g_weight[best * W_MAXT + t];
            pick = p2;
            if (pick <= 0.0f) break;
        }
        if (chosen < 0) return 0;
        for (t = 0; t < g_nt; t++) g_allow[best * W_MAXT + t] = (unsigned char)(t == chosen ? 1 : 0);
        g_count[best] = 1;
        g_result[best] = chosen;
        g_queue[0] = best;
        g_inq[best] = 1;
        if (!w_propagate(1)) return 0;
    }
    return 1;
}

/* Solve the chunk's WFC with retries + graceful fallbacks. */
static void w_wfc_solve(void) {
    int a;
    g_wfc_attempts = 0;
    if (g_has_castle) {
        for (a = 0; a < 6; a++) {
            g_wfc_attempts++;
            if (w_wfc_run(w_hash2(g_ch_cx, g_ch_cz, 1000u + (uint32_t)a), 1)) return;
        }
        g_has_castle = 0;
    }
    for (a = 0; a < 6; a++) {
        g_wfc_attempts++;
        if (w_wfc_run(w_hash2(g_ch_cx, g_ch_cz, 2000u + (uint32_t)a), 0)) return;
    }
    /* Last resort (never observed): plain ground, roads only where
     * border crossings need them as dead-ends. */
    int c;
    for (c = 0; c < W_NCELLS; c++) {
        int m = 0, d;
        for (d = 0; d < 4; d++) if (w_border_socket(c, d) == SK_R) m = m | (1 << d);
        if (g_cclass[c] == CC_WATER) g_result[c] = g_tile_water;
        else if (m) g_result[c] = g_tile_road_base + m - 1;
        else g_result[c] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Mesh building                                                       */
/* ------------------------------------------------------------------ */
static float *g_mv;          /* output vertex floats (W_VSTRIDE each) */
static int g_mn;             /* vertices written                      */
static int g_mcap;           /* capacity in vertices                  */
static float g_cr, g_cg, g_cb;
static float g_refx, g_refy, g_refz;
static int g_ref_up;         /* 1: orient normals upward instead      */

static void m_color(float r, float g, float b) { g_cr = r; g_cg = g; g_cb = b; }
static void m_ref(float x, float y, float z) { g_refx = x; g_refy = y; g_refz = z; g_ref_up = 0; }

static void m_vert(const float *p, const float *n) {
    if (g_mn >= g_mcap) return;
    float *o = g_mv + g_mn * W_VSTRIDE;
    o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
    o[3] = n[0]; o[4] = n[1]; o[5] = n[2];
    o[6] = g_cr; o[7] = g_cg; o[8] = g_cb;
    g_mn = g_mn + 1;
}

static void m_tri(const float *a, const float *b, const float *c) {
    float u[3], v[3], n[3];
    u[0] = b[0] - a[0]; u[1] = b[1] - a[1]; u[2] = b[2] - a[2];
    v[0] = c[0] - a[0]; v[1] = c[1] - a[1]; v[2] = c[2] - a[2];
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    float len = w_sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len < 1e-8f) return;
    float inv = 1.0f / len;
    n[0] = n[0] * inv; n[1] = n[1] * inv; n[2] = n[2] * inv;
    int flip = 0;
    if (g_ref_up) {
        if (n[1] < 0.0f) flip = 1;
    } else {
        float cx = (a[0] + b[0] + c[0]) * (1.0f / 3.0f) - g_refx;
        float cy = (a[1] + b[1] + c[1]) * (1.0f / 3.0f) - g_refy;
        float cz = (a[2] + b[2] + c[2]) * (1.0f / 3.0f) - g_refz;
        if (cx * n[0] + cy * n[1] + cz * n[2] < 0.0f) flip = 1;
    }
    if (flip) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
    m_vert(a, n); m_vert(b, n); m_vert(c, n);
}

static void m_quad(const float *a, const float *b, const float *c, const float *d) {
    m_tri(a, b, c);
    m_tri(a, c, d);
}

static void m_set3(float *p, float x, float y, float z) { p[0] = x; p[1] = y; p[2] = z; }

/* Axis-aligned box without its bottom face. */
static void m_box(float x0, float y0, float z0, float x1, float y1, float z1) {
    float p[24];
    m_ref((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f);
    m_set3(p + 0, x0, y0, z0); m_set3(p + 3, x1, y0, z0);
    m_set3(p + 6, x1, y0, z1); m_set3(p + 9, x0, y0, z1);
    m_set3(p + 12, x0, y1, z0); m_set3(p + 15, x1, y1, z0);
    m_set3(p + 18, x1, y1, z1); m_set3(p + 21, x0, y1, z1);
    m_quad(p + 12, p + 15, p + 18, p + 21);           /* top   */
    m_quad(p + 0, p + 3, p + 15, p + 12);             /* -z    */
    m_quad(p + 3, p + 6, p + 18, p + 15);             /* +x    */
    m_quad(p + 6, p + 9, p + 21, p + 18);             /* +z    */
    m_quad(p + 9, p + 0, p + 12, p + 21);             /* -x    */
}

/* Box given center x/z, half extents, and y range. */
static void m_boxc(float cx, float cz, float hx, float hz, float y0, float y1) {
    m_box(cx - hx, y0, cz - hz, cx + hx, y1, cz + hz);
}

/* Vertical n-gon prism (no bottom). */
static void m_prism(float cx, float cz, float r, float y0, float y1, int sides) {
    float a[3], b[3], c[3], d[3], top[3];
    int i;
    m_ref(cx, (y0 + y1) * 0.5f, cz);
    m_set3(top, cx, y1, cz);
    for (i = 0; i < sides; i++) {
        float t0 = (float)i * (2.0f * W_PI) / (float)sides;
        float t1 = (float)(i + 1) * (2.0f * W_PI) / (float)sides;
        float s0 = w_sin(t0), c0 = w_cos(t0), s1 = w_sin(t1), c1 = w_cos(t1);
        m_set3(a, cx + c0 * r, y0, cz + s0 * r);
        m_set3(b, cx + c1 * r, y0, cz + s1 * r);
        m_set3(c, cx + c1 * r, y1, cz + s1 * r);
        m_set3(d, cx + c0 * r, y1, cz + s0 * r);
        m_quad(a, b, c, d);
        m_tri(d, c, top);
    }
}

/* Cone / pyramid (sides=4 gives a square pyramid rotated 45 degrees). */
static void m_cone(float cx, float cz, float r, float y0, float y1, int sides) {
    float a[3], b[3], tip[3];
    int i;
    m_ref(cx, y0, cz);
    m_set3(tip, cx, y1, cz);
    for (i = 0; i < sides; i++) {
        float t0 = (float)i * (2.0f * W_PI) / (float)sides;
        float t1 = (float)(i + 1) * (2.0f * W_PI) / (float)sides;
        m_set3(a, cx + w_cos(t0) * r, y0, cz + w_sin(t0) * r);
        m_set3(b, cx + w_cos(t1) * r, y0, cz + w_sin(t1) * r);
        m_tri(a, b, tip);
    }
}

/* Square pyramid roof aligned with the axes. */
static void m_pyramid(float cx, float cz, float h, float y0, float y1) {
    float a[3], b[3], c[3], d[3], tip[3];
    m_ref(cx, y0, cz);
    m_set3(a, cx - h, y0, cz - h); m_set3(b, cx + h, y0, cz - h);
    m_set3(c, cx + h, y0, cz + h); m_set3(d, cx - h, y0, cz + h);
    m_set3(tip, cx, y1, cz);
    m_tri(a, b, tip); m_tri(b, c, tip); m_tri(c, d, tip); m_tri(d, a, tip);
}

/* Gable roof: ridge along x (alongX=1) or z. */
static void m_gable(float cx, float cz, float hx, float hz, float y0, float y1, int alongX) {
    float a[3], b[3], c[3], d[3], r0[3], r1[3];
    m_ref(cx, y0, cz);
    m_set3(a, cx - hx, y0, cz - hz); m_set3(b, cx + hx, y0, cz - hz);
    m_set3(c, cx + hx, y0, cz + hz); m_set3(d, cx - hx, y0, cz + hz);
    if (alongX) {
        m_set3(r0, cx - hx, y1, cz); m_set3(r1, cx + hx, y1, cz);
        m_quad(a, b, r1, r0); m_quad(d, c, r1, r0);
        m_tri(a, d, r0); m_tri(b, c, r1);
    } else {
        m_set3(r0, cx, y1, cz - hz); m_set3(r1, cx, y1, cz + hz);
        m_quad(a, d, r1, r0); m_quad(b, c, r1, r0);
        m_tri(a, b, r0); m_tri(d, c, r1);
    }
}

/* ------------------------------------------------------------------ */
/* Terrain                                                             */
/* ------------------------------------------------------------------ */
static float w_chunk_origin_x(void) { return (float)g_ch_cx * W_CHUNK_SIZE; }
static float w_chunk_origin_z(void) { return (float)g_ch_cz * W_CHUNK_SIZE; }

/* Height at a chunk-local point, matching the emitted triangle mesh
 * (each quad is split along its (i,j)-(i+1,j+1) diagonal). */
static float w_mesh_height(float lx, float lz) {
    float gx = lx / W_STEP, gz = lz / W_STEP;
    int i = w_ifloor(gx), j = w_ifloor(gz);
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    if (i > W_GRID - 1) i = W_GRID - 1;
    if (j > W_GRID - 1) j = W_GRID - 1;
    float fx = gx - (float)i, fz = gz - (float)j;
    float h00 = g_hgt[j * W_GRIDV + i];
    float h10 = g_hgt[j * W_GRIDV + i + 1];
    float h01 = g_hgt[(j + 1) * W_GRIDV + i];
    float h11 = g_hgt[(j + 1) * W_GRIDV + i + 1];
    if (fx >= fz) return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
    return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
}

static void w_terrain_color(float h, float ny, float jitter, int courtyard) {
    float r, g, b;
    if (courtyard) { r = 0.58f; g = 0.55f; b = 0.50f; }
    else if (h < -1.2f) { r = 0.52f; g = 0.50f; b = 0.38f; }
    else if (h < 1.1f) { r = 0.87f; g = 0.80f; b = 0.56f; }
    else if (ny < 0.70f) { r = 0.50f; g = 0.47f; b = 0.43f; }
    else if (h < 11.0f) {
        float t = w_clampf(h / 11.0f, 0.0f, 1.0f);
        r = 0.30f + 0.08f * t; g = 0.58f - 0.10f * t; b = 0.24f;
    }
    else if (h < 19.0f) { r = 0.52f; g = 0.50f; b = 0.47f; }
    else { r = 0.94f; g = 0.95f; b = 0.97f; }
    float s = 0.94f + 0.12f * jitter;
    m_color(r * s, g * s, b * s);
}

/* |normal.y| of a triangle (1 = flat, 0 = vertical). */
static float w_tri_ny(const float *a, const float *b, const float *c) {
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float len = w_sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-8f) return 1.0f;
    return w_absf(ny) / len;
}

static int w_site_court(float x, float z);

static void w_emit_terrain(void) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int i, j;
    float a[3], b[3], c[3], d[3];
    g_ref_up = 1;
    for (j = 0; j < W_GRID; j++) {
        for (i = 0; i < W_GRID; i++) {
            float x0 = ox + (float)i * W_STEP, x1 = ox + (float)(i + 1) * W_STEP;
            float z0 = oz + (float)j * W_STEP, z1 = oz + (float)(j + 1) * W_STEP;
            m_set3(a, x0, g_hgt[j * W_GRIDV + i], z0);
            m_set3(b, x1, g_hgt[j * W_GRIDV + i + 1], z0);
            m_set3(c, x1, g_hgt[(j + 1) * W_GRIDV + i + 1], z1);
            m_set3(d, x0, g_hgt[(j + 1) * W_GRIDV + i], z1);
            int cell = (j / W_SUB) * W_CELLS + (i / W_SUB);
            int rt = g_result[cell];
            int court = w_site_court(ox + ((float)i + 0.5f) * W_STEP, oz + ((float)j + 0.5f) * W_STEP);
            (void)rt;
            int gi = g_ch_cx * W_GRID + i, gj = g_ch_cz * W_GRID + j;
            /* Triangle 1: a b c */
            float ny1 = w_tri_ny(a, b, c);
            float j1 = w_hash01(gi, gj, 17u);
            w_terrain_color((a[1] + b[1] + c[1]) * (1.0f / 3.0f), ny1, j1, court);
            g_ref_up = 1;
            m_tri(a, b, c);
            /* Triangle 2: a c d */
            float ny2 = w_tri_ny(a, c, d);
            float j2 = w_hash01(gi, gj, 23u);
            w_terrain_color((a[1] + c[1] + d[1]) * (1.0f / 3.0f), ny2, j2, court);
            g_ref_up = 1;
            m_tri(a, c, d);
        }
    }
}

/* Water surface: a coarse grid at sea level (the vertex shader animates
 * it; normal.y = 2 marks a vertex as water). Emitted only where needed. */
static void w_emit_water(void) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int i, j;
    int any = 0;
    for (i = 0; i < W_GRIDV * W_GRIDV; i++) if (g_hgt[i] < W_SEA_LEVEL + 0.4f) { any = 1; break; }
    if (!any) return;
    float n[3];
    m_set3(n, 0.0f, 2.0f, 0.0f);
    float p[12];
    int N = 6;
    float st = W_CHUNK_SIZE / (float)N;
    for (j = 0; j < N; j++) {
        for (i = 0; i < N; i++) {
            /* Skip quads whose terrain is entirely well above water. */
            int k, l, need = 0;
            for (l = j * W_GRID / N; l <= (j + 1) * W_GRID / N && !need; l++)
                for (k = i * W_GRID / N; k <= (i + 1) * W_GRID / N; k++)
                    if (g_hgt[l * W_GRIDV + k] < W_SEA_LEVEL + 0.4f) { need = 1; break; }
            if (!need) continue;
            float x0 = ox + (float)i * st, x1 = ox + (float)(i + 1) * st;
            float z0 = oz + (float)j * st, z1 = oz + (float)(j + 1) * st;
            m_set3(p + 0, x0, W_SEA_LEVEL, z0); m_set3(p + 3, x1, W_SEA_LEVEL, z0);
            m_set3(p + 6, x1, W_SEA_LEVEL, z1); m_set3(p + 9, x0, W_SEA_LEVEL, z1);
            m_color(0.16f, 0.42f, 0.66f);
            m_vert(p + 0, n); m_vert(p + 3, n); m_vert(p + 6, n);
            m_vert(p + 0, n); m_vert(p + 6, n); m_vert(p + 9, n);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Navigation grid (for units / GPU pathfinding)                       */
/* ------------------------------------------------------------------ */
/* Per chunk: a 48x48 grid of 1-unit cells in NAV_L layers -- layer 0 is
 * the ground (terrain, courtyards, ramps, bridge decks), layer 1 the
 * castle wall-walks (running through towers and over gates). Each node
 * has a height and flags. Obstacles are stamped by the same prop
 * emitters that build the meshes, so navigation always matches what is
 * drawn. Movement rules (shared by the GPU flow-field shader and the CPU
 * unit stepping in castles_android.c): a node can step to any of its 8
 * neighbors in ANY layer whose height differs by at most NAV_MAXSTEP
 * (ramps link the layers this way), and a NAV_LADDER node links to the
 * ladder node one cell away in direction (flags >> 2) & 3 regardless of
 * height (climbing). */
#define NAV_C        48
#define NAV_L        2
#define NAV_N        (NAV_C * NAV_C)
#define NAV_WALK     1
#define NAV_LADDER   2       /* bits 2-3: direction (D_N..D_W) to the other end */
#define NAV_TERRAIN  16      /* height is the terrain mesh surface */
#define NAV_WATER    32      /* deep enough for boats (layer 0 only) */
#define NAV_MAXSTEP  1.3f

static float g_nav_h[NAV_L * NAV_N];
static unsigned char g_nav_f[NAV_L * NAV_N];

static void w_nav_init(void) {
    int i, j;
    for (j = 0; j < NAV_C; j++) {
        for (i = 0; i < NAV_C; i++) {
            float h = w_mesh_height((float)i + 0.5f, (float)j + 0.5f);
            g_nav_h[j * NAV_C + i] = h;
            g_nav_f[j * NAV_C + i] = (unsigned char)(h > -0.45f ? (NAV_WALK | NAV_TERRAIN) : (h < -0.7f ? NAV_WATER : 0));
            g_nav_h[NAV_N + j * NAV_C + i] = 0.0f;
            g_nav_f[NAV_N + j * NAV_C + i] = 0;
        }
    }
}

/* Cell index range [i0,i1] whose centers lie inside world span [a0,a1]
 * along one axis (o = chunk origin on that axis). */
static int w_nav_lo(float a0, float o) {
    int i = w_ifloor(a0 - o - 0.5f) + 1;
    return i < 0 ? 0 : i;
}
static int w_nav_hi(float a1, float o) {
    int i = w_ifloor(a1 - o - 0.5f);
    return i > NAV_C - 1 ? NAV_C - 1 : i;
}

/* Sets height+flags of every layer-`layer` cell centered in the rect;
 * flags == 0 makes it an obstacle (height untouched). */
static void w_nav_rect(int layer, float x0, float z0, float x1, float z1, float h, int flags) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int i0 = w_nav_lo(x0, ox), i1 = w_nav_hi(x1, ox);
    int j0 = w_nav_lo(z0, oz), j1 = w_nav_hi(z1, oz);
    int i, j;
    for (j = j0; j <= j1; j++) {
        for (i = i0; i <= i1; i++) {
            int n = layer * NAV_N + j * NAV_C + i;
            if (flags) g_nav_h[n] = h;
            g_nav_f[n] = (unsigned char)flags;
        }
    }
}

static void w_nav_block(float x0, float z0, float x1, float z1) {
    w_nav_rect(0, x0, z0, x1, z1, 0.0f, 0);
}

static void w_nav_block_circle(float x, float z, float r) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int i0 = w_nav_lo(x - r, ox), i1 = w_nav_hi(x + r, ox);
    int j0 = w_nav_lo(z - r, oz), j1 = w_nav_hi(z + r, oz);
    int i, j;
    for (j = j0; j <= j1; j++) {
        for (i = i0; i <= i1; i++) {
            float dx = ox + (float)i + 0.5f - x, dz = oz + (float)j + 0.5f - z;
            if (dx * dx + dz * dz <= r * r) g_nav_f[j * NAV_C + i] = 0;
        }
    }
}

/* Marks one end of a ladder: cell centered at (x,z) in `layer`, height
 * h, linking toward direction dir. Returns 1 if the cell is in-chunk. */
static int w_nav_ladder_end(int layer, float x, float z, float h, int dir) {
    int i = w_ifloor(x - w_chunk_origin_x()), j = w_ifloor(z - w_chunk_origin_z());
    if (i < 0 || j < 0 || i >= NAV_C || j >= NAV_C) return 0;
    int n = layer * NAV_N + j * NAV_C + i;
    g_nav_h[n] = h;
    g_nav_f[n] = (unsigned char)(NAV_WALK | NAV_LADDER | (dir << 2));
    return 1;
}

/* ------------------------------------------------------------------ */
/* Harvestable resources                                               */
/* ------------------------------------------------------------------ */
/* Every tree (wood) and rock (stone) a chunk emits is listed in g_out_res_*
 * with a stable id hashed from its cell and index, so the game layer can
 * send peasants to it. Fully harvested ids go into g_depleted (written by
 * the render thread, read here): the generator then leaves them out --
 * no mesh, no nav obstacle -- and the game rebuilds the chunk. */
#define RES_WOOD   0
#define RES_STONE  1
#define RES_FOOD   2              /* fish shoals (ids tagged 0x40000000) */
#define RES_MAX    320            /* listed per chunk (the rest stay scenery) */
#define DEPL_MAX   4096
static int g_out_nres;
static float g_out_res_x[RES_MAX], g_out_res_z[RES_MAX], g_out_res_y[RES_MAX], g_out_res_r[RES_MAX];
static int g_out_res_t[RES_MAX];
static uint32_t g_out_res_id[RES_MAX];
static uint32_t g_depleted[DEPL_MAX];
static int g_ndepleted;

/* ---- destroyed structures ---- */
/* Castle wall sections, towers, bastions, gatehouses and buildings that
 * siege engines have knocked down, by stable id (the render thread adds
 * them, the generator reads them: it draws rubble instead and leaves the
 * ground walkable). Walls are cut into sections of about 6 units per
 * edge so a breach is a gap, not the whole side. */
#define DESTROY_MAX 2048
#define EK_WALL     1
#define EK_TOWER    2
#define EK_GATE     3
#define EK_BASTION  4
#define EK_BUILDING 5
static uint32_t g_destroyed[DESTROY_MAX];
static int g_ndestroyed;

static uint32_t el_id(int tx, int tz, int kind, int a, int b, int c) {
    uint32_t h = w_hash2(tx * 7 + kind * 1013, tz * 13 + a * 389, 0xE1E1u + (uint32_t)(b * 131 + c * 7919));
    return (h & 0x0FFFFFFFu) | ((uint32_t)kind << 28);
}
/* A building's id from its position (as computed by the castle layout /
 * the dock generator -- the same floats on both threads). */
static uint32_t el_id_xz(float x, float z) {
    return el_id(w_ifloor(x * 4.0f), w_ifloor(z * 4.0f), EK_BUILDING, 0, 0, 0);
}
static int w_is_destroyed(uint32_t id) {
    int i, n = g_ndestroyed;
    for (i = 0; i < n; i++) if (g_destroyed[i] == id) return 1;
    return 0;
}
static int site_wall_pieces(float len) {
    int n = (int)(len / 6.0f + 0.5f);
    return n < 1 ? 1 : (n > 60 ? 60 : n);
}

static int w_res_depleted(uint32_t id) {
    int i, n = g_ndepleted;
    for (i = 0; i < n; i++) if (g_depleted[i] == id) return 1;
    return 0;
}

static void w_res_add(int type, uint32_t id, float x, float z, float y, float r) {
    int k = g_out_nres;
    if (k >= RES_MAX) return;
    g_out_res_t[k] = type; g_out_res_id[k] = id;
    g_out_res_x[k] = x; g_out_res_z[k] = z; g_out_res_y[k] = y; g_out_res_r[k] = r;
    g_out_nres = k + 1;
}

/* ------------------------------------------------------------------ */
/* Props                                                               */
/* ------------------------------------------------------------------ */
static void w_tree(float x, float z, float y, float size, int pine) {
    w_nav_block_circle(x, z, 0.5f);
    m_color(0.42f, 0.29f, 0.17f);
    m_boxc(x, z, 0.18f * size, 0.18f * size, y - 0.3f, y + 1.2f * size);
    if (pine) {
        m_color(0.13f, 0.36f, 0.20f);
        m_cone(x, z, 1.25f * size, y + 0.9f * size, y + 3.1f * size, 6);
        m_color(0.16f, 0.42f, 0.22f);
        m_cone(x, z, 0.9f * size, y + 2.2f * size, y + 4.3f * size, 6);
    } else {
        m_color(0.24f, 0.52f, 0.20f);
        m_prism(x, z, 1.15f * size, y + 1.1f * size, y + 2.6f * size, 6);
        m_color(0.28f, 0.58f, 0.22f);
        m_cone(x, z, 1.15f * size, y + 2.6f * size, y + 3.5f * size, 6);
    }
}

static void w_rock(float x, float z, float y, float size) {
    w_nav_block_circle(x, z, 0.9f * size);
    m_color(0.47f, 0.46f, 0.44f);
    m_cone(x, z, 1.1f * size, y - 0.4f, y + 1.1f * size, 5);
}

static void w_house(float x, float z, float y, int alongX, float tone) {
    float hx = alongX ? 1.5f : 1.1f, hz = alongX ? 1.1f : 1.5f;
    w_nav_block(x - hx - 0.2f, z - hz - 0.2f, x + hx + 0.2f, z + hz + 0.2f);
    m_color(0.86f * tone, 0.80f * tone, 0.66f * tone);
    m_boxc(x, z, hx, hz, y - 0.5f, y + 2.0f);
    m_color(0.66f, 0.24f, 0.18f);
    m_gable(x, z, hx + 0.25f, hz + 0.25f, y + 2.0f, y + 3.4f, alongX);
}

static void w_well(float x, float z, float y) {
    w_nav_block_circle(x, z, 1.0f);
    m_color(0.60f, 0.58f, 0.55f);
    m_prism(x, z, 0.8f, y - 0.3f, y + 0.8f, 8);
    m_color(0.42f, 0.29f, 0.17f);
    m_boxc(x - 0.7f, z, 0.08f, 0.08f, y + 0.8f, y + 2.2f);
    m_boxc(x + 0.7f, z, 0.08f, 0.08f, y + 0.8f, y + 2.2f);
    m_color(0.62f, 0.25f, 0.18f);
    m_gable(x, z, 1.0f, 0.6f, y + 2.1f, y + 2.8f, 1);
}

/* Road: a slightly raised strip from cell center to each road edge,
 * following the terrain mesh. */
static void w_road_segment(float lcx, float lcz, int d) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    float dx = 0.0f, dz = 0.0f;
    if (d == D_N) dz = -1.0f;
    if (d == D_S) dz = 1.0f;
    if (d == D_E) dx = 1.0f;
    if (d == D_W) dx = -1.0f;
    float px = -dz * 0.75f, pz = dx * 0.75f;
    int s;
    float a[3], b[3], c[3], e[3];
    g_ref_up = 1;
    m_color(0.62f, 0.52f, 0.36f);
    for (s = 0; s < 2; s++) {
        float t0 = (float)s * 1.0f, t1 = (float)(s + 1) * 1.0f;
        float ax = lcx + dx * t0, az = lcz + dz * t0;
        float bx = lcx + dx * t1, bz = lcz + dz * t1;
        /* Extend the first piece back over the center to cover corners. */
        if (s == 0) { float nax = ax - dx * 0.75f, naz = az - dz * 0.75f; ax = nax; az = naz; }
        float ha = w_mesh_height(ax, az), hb = w_mesh_height(bx, bz);
        float ha1 = w_mesh_height(ax + px, az + pz), ha2 = w_mesh_height(ax - px, az - pz);
        float hb1 = w_mesh_height(bx + px, bz + pz), hb2 = w_mesh_height(bx - px, bz - pz);
        float ya = ha; if (ha1 > ya) ya = ha1; if (ha2 > ya) ya = ha2;
        float yb = hb; if (hb1 > yb) yb = hb1; if (hb2 > yb) yb = hb2;
        float yal = ya + 0.12f, ybl = yb + 0.12f;
        m_set3(a, ox + ax + px, yal, oz + az + pz);
        m_set3(b, ox + bx + px, ybl, oz + bz + pz);
        m_set3(c, ox + bx - px, ybl, oz + bz - pz);
        m_set3(e, ox + ax - px, yal, oz + az - pz);
        g_ref_up = 1;
        m_quad(a, b, c, e);
    }
}

static void w_bridge(float wx, float wz, int alongZ) {
    float deck = 1.2f;
    float hx = alongZ ? 1.1f : 2.05f, hz = alongZ ? 2.05f : 1.1f;
    /* Walkable deck over the water (between the rails). */
    if (alongZ) w_nav_rect(0, wx - 0.9f, wz - 2.05f, wx + 0.9f, wz + 2.05f, deck, NAV_WALK);
    else w_nav_rect(0, wx - 2.05f, wz - 0.9f, wx + 2.05f, wz + 0.9f, deck, NAV_WALK);
    m_color(0.55f, 0.38f, 0.22f);
    m_boxc(wx, wz, hx, hz, deck - 0.25f, deck);
    m_color(0.45f, 0.30f, 0.17f);
    if (alongZ) {
        m_boxc(wx - 1.0f, wz, 0.08f, 2.0f, deck, deck + 0.6f);
        m_boxc(wx + 1.0f, wz, 0.08f, 2.0f, deck, deck + 0.6f);
    } else {
        m_boxc(wx, wz - 1.0f, 2.0f, 0.08f, deck, deck + 0.6f);
        m_boxc(wx, wz + 1.0f, 2.0f, 0.08f, deck, deck + 0.6f);
    }
    m_color(0.50f, 0.48f, 0.45f);
    m_boxc(wx, wz, 0.4f, 0.4f, -4.0f, deck - 0.25f);
}

/* ---- castle pieces ---- */
#define WALL_H 4.6f
#define WALL_T 0.65f

/* Parapet merlons along the OUTER edge of the wall top (side `out` is
 * -1 or +1 along the wall's thickness axis), leaving the wall-walk on the
 * inner side clear for units. */
static void w_merlons_x(float x0, float x1, float z, float y, float out) {
    float x;
    float za = z + out * WALL_T, zb = z + out * (WALL_T - 0.32f);
    float zlo = za < zb ? za : zb, zhi = za < zb ? zb : za;
    m_color(0.70f, 0.68f, 0.63f);
    for (x = x0 + 0.35f; x < x1 - 0.2f; x = x + 1.0f) {
        m_box(x, y, zlo, x + 0.5f, y + 0.8f, zhi);
    }
}

static void w_merlons_z(float z0, float z1, float x, float y, float out) {
    float z;
    float xa = x + out * WALL_T, xb = x + out * (WALL_T - 0.32f);
    float xlo = xa < xb ? xa : xb, xhi = xa < xb ? xb : xa;
    m_color(0.70f, 0.68f, 0.63f);
    for (z = z0 + 0.35f; z < z1 - 0.2f; z = z + 1.0f) {
        m_box(xlo, y, z, xhi, y + 0.8f, z + 0.5f);
    }
}

/* Wooden ladder leaning against the inner face of a wall: foot one cell
 * in from the wall's inner face, top at the wall-walk. (fx,fz) = foot,
 * (tx,tz) = top, both at the ladder's center line; across = unit vector
 * along the wall (1,0) or (0,1). */
static void w_ladder(float fx, float fz, float fy, float tx, float tz, float ty, int alongX) {
    float ax = alongX ? 0.32f : 0.0f, az = alongX ? 0.0f : 0.32f;
    float a[3], b[3], c[3], d[3];
    int k;
    m_color(0.48f, 0.33f, 0.19f);
    for (k = -1; k <= 1; k = k + 2) {
        float sx = ax * (float)k, sz = az * (float)k;
        float wx = alongX ? 0.05f : 0.0f, wz = alongX ? 0.0f : 0.05f;
        m_ref(fx + sx, (fy + ty) * 0.5f, fz + sz);
        g_ref_up = 0;
        m_set3(a, fx + sx - wx, fy, fz + sz - wz); m_set3(b, fx + sx + wx, fy, fz + sz + wz);
        m_set3(c, tx + sx + wx, ty, tz + sz + wz); m_set3(d, tx + sx - wx, ty, tz + sz - wz);
        m_quad(a, b, c, d);
    }
    m_color(0.55f, 0.39f, 0.23f);
    for (k = 1; k < 10; k++) {
        float t = (float)k / 10.0f;
        float rx = fx + (tx - fx) * t, ry = fy + (ty - fy) * t, rz = fz + (tz - fz) * t;
        if (alongX) m_box(rx - 0.32f, ry - 0.04f, rz - 0.04f, rx + 0.32f, ry + 0.04f, rz + 0.04f);
        else m_box(rx - 0.04f, ry - 0.04f, rz - 0.32f, rx + 0.04f, ry + 0.04f, rz + 0.32f);
    }
}

/* Ramp surface in the ground layer: rises linearly from y0 at a0 to y1
 * at a1 along the wall (x when alongX, else z); b0..b1 is its extent
 * across the wall's thickness axis. */
static void w_nav_ramp(float a0, float a1, float b0, float b1, float y0, float y1, int alongX) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int i0 = w_nav_lo(alongX ? a0 : b0, ox), i1 = w_nav_hi(alongX ? a1 : b1, ox);
    int j0 = w_nav_lo(alongX ? b0 : a0, oz), j1 = w_nav_hi(alongX ? b1 : a1, oz);
    int i, j;
    for (j = j0; j <= j1; j++) {
        for (i = i0; i <= i1; i++) {
            float a = alongX ? ox + (float)i + 0.5f : oz + (float)j + 0.5f;
            float t = (a - a0) / (a1 - a0);
            g_nav_h[j * NAV_C + i] = y0 + (y1 - y0) * t;
            g_nav_f[j * NAV_C + i] = NAV_WALK;
        }
    }
}

/* Wall segment across a cell on side s, centered on the cell's center
 * line (cells are 4 units; walls run edge to edge). */
static void w_wall_piece(float cx, float cz, float y, int s, int kind) {
    float hc = W_CELL_SIZE * 0.5f;
    int alongX = (s == D_N || s == D_S);
    float top = y + WALL_H;
    /* Outward sign along the wall's thickness axis. */
    float out = (s == D_N || s == D_W) ? -1.0f : 1.0f;
    /* Nav: the wall body blocks the ground; its top is the wall-walk.
     * (A gate's opening is re-opened below.) */
    if (alongX) {
        w_nav_block(cx - hc, cz - WALL_T, cx + hc, cz + WALL_T);
        w_nav_rect(1, cx - hc, cz - WALL_T, cx + hc, cz + WALL_T, top, NAV_WALK);
    } else {
        w_nav_block(cx - WALL_T, cz - hc, cx + WALL_T, cz + hc);
        w_nav_rect(1, cx - WALL_T, cz - hc, cx + WALL_T, cz + hc, top, NAV_WALK);
    }
    m_color(0.66f, 0.64f, 0.59f);
    if (kind == K_GATE) {
        /* Ground passes through the 2-unit opening. */
        if (alongX) w_nav_rect(0, cx - 0.95f, cz - WALL_T - 0.1f, cx + 0.95f, cz + WALL_T + 0.1f, y, NAV_WALK | NAV_TERRAIN);
        else w_nav_rect(0, cx - WALL_T - 0.1f, cz - 0.95f, cx + WALL_T + 0.1f, cz + 0.95f, y, NAV_WALK | NAV_TERRAIN);
        /* Two pillars + lintel, leaving a 2-unit opening, plus doors. */
        if (alongX) {
            m_box(cx - hc, y - 1.0f, cz - WALL_T, cx - 1.0f, y + WALL_H, cz + WALL_T);
            m_box(cx + 1.0f, y - 1.0f, cz - WALL_T, cx + hc, y + WALL_H, cz + WALL_T);
            m_box(cx - 1.0f, y + 3.0f, cz - WALL_T, cx + 1.0f, y + WALL_H, cz + WALL_T);
            m_color(0.70f, 0.68f, 0.63f);
            m_boxc(cx - 1.6f, cz, 0.6f, 0.9f, y + WALL_H, y + WALL_H + 1.2f);
            m_boxc(cx + 1.6f, cz, 0.6f, 0.9f, y + WALL_H, y + WALL_H + 1.2f);
            m_color(0.40f, 0.26f, 0.14f);
            m_box(cx - 1.0f, y - 0.2f, cz - 0.12f, cx + 1.0f, y + 2.9f, cz + 0.12f);
        } else {
            m_box(cx - WALL_T, y - 1.0f, cz - hc, cx + WALL_T, y + WALL_H, cz - 1.0f);
            m_box(cx - WALL_T, y - 1.0f, cz + 1.0f, cx + WALL_T, y + WALL_H, cz + hc);
            m_box(cx - WALL_T, y + 3.0f, cz - 1.0f, cx + WALL_T, y + WALL_H, cz + 1.0f);
            m_color(0.70f, 0.68f, 0.63f);
            m_boxc(cx, cz - 1.6f, 0.9f, 0.6f, y + WALL_H, y + WALL_H + 1.2f);
            m_boxc(cx, cz + 1.6f, 0.9f, 0.6f, y + WALL_H, y + WALL_H + 1.2f);
            m_color(0.40f, 0.26f, 0.14f);
            m_box(cx - 0.12f, y - 0.2f, cz - 1.0f, cx + 0.12f, y + 2.9f, cz + 1.0f);
        }
        return;
    }
    if (alongX) {
        m_box(cx - hc, y - 1.0f, cz - WALL_T, cx + hc, y + WALL_H, cz + WALL_T);
        w_merlons_x(cx - hc, cx + hc, cz, y + WALL_H, out);
    } else {
        m_box(cx - WALL_T, y - 1.0f, cz - hc, cx + WALL_T, y + WALL_H, cz + hc);
        w_merlons_z(cz - hc, cz + hc, cx, y + WALL_H, out);
    }
    if (kind == K_WALL && ((w_hash2(w_ifloor(cx), w_ifloor(cz), 4711u) % 3u) == 0u)) {
        /* Ladder up the inner face. Foot one cell inward of the wall-walk's
         * inner row; the climb link joins foot (ground) and top (wall). */
        float in = -out;
        int toTop = s, toFoot = (s + 2) & 3;
        if (alongX) {
            float lx = cx + 0.5f, footZ = cz + in * 1.5f, topZ = cz + in * 0.5f;
            if (w_nav_ladder_end(0, lx, footZ, y, toTop) && w_nav_ladder_end(1, lx, topZ, top, toFoot))
                w_ladder(lx, cz + in * 1.45f, y - 0.1f, lx, cz + in * (WALL_T + 0.06f), top + 0.7f, 1);
        } else {
            float lz = cz + 0.5f, footX = cx + in * 1.5f, topX = cx + in * 0.5f;
            if (w_nav_ladder_end(0, footX, lz, y, toTop) && w_nav_ladder_end(1, topX, lz, top, toFoot))
                w_ladder(cx + in * 1.45f, lz, y - 0.1f, cx + in * (WALL_T + 0.06f), lz, top + 0.7f, 0);
        }
    }
    if (kind == K_RAMP) {
        /* Ramp-wall: a stone ramp on the courtyard side climbing to the
         * wall walk. Inward direction is opposite of s. */
        float a[3], b[3], c[3], d[3], e[3], f[3];
        float rtop = y + WALL_H - 0.2f;
        m_color(0.60f, 0.57f, 0.52f);
        if (alongX) {
            float zi = (s == D_N) ? cz + WALL_T : cz - WALL_T;
            float zo = (s == D_N) ? zi + 1.4f : zi - 1.4f;
            m_ref(cx + 0.6f, y + 1.0f, (zi + zo) * 0.5f);
            m_set3(a, cx - hc + 0.2f, y, zi); m_set3(b, cx + hc - 0.2f, y, zi);
            m_set3(c, cx + hc - 0.2f, rtop, zi);
            m_set3(d, cx - hc + 0.2f, y, zo); m_set3(e, cx + hc - 0.2f, y, zo);
            m_set3(f, cx + hc - 0.2f, rtop, zo);
            m_tri(a, b, c); m_tri(d, e, f);
            m_quad(a, c, f, d);          /* sloped walking surface */
            m_quad(b, e, f, c);          /* tall end               */
            w_nav_ramp(cx - hc + 0.2f, cx + hc - 0.2f, zi < zo ? zi : zo, zi < zo ? zo : zi, y, rtop, 1);
        } else {
            float xi = (s == D_W) ? cx + WALL_T : cx - WALL_T;
            float xo = (s == D_W) ? xi + 1.4f : xi - 1.4f;
            m_ref((xi + xo) * 0.5f, y + 1.0f, cz + 0.6f);
            m_set3(a, xi, y, cz - hc + 0.2f); m_set3(b, xi, y, cz + hc - 0.2f);
            m_set3(c, xi, rtop, cz + hc - 0.2f);
            m_set3(d, xo, y, cz - hc + 0.2f); m_set3(e, xo, y, cz + hc - 0.2f);
            m_set3(f, xo, rtop, cz + hc - 0.2f);
            m_tri(a, b, c); m_tri(d, e, f);
            m_quad(a, c, f, d);
            m_quad(b, e, f, c);
            w_nav_ramp(cz - hc + 0.2f, cz + hc - 0.2f, xi < xo ? xi : xo, xi < xo ? xo : xi, y, rtop, 0);
        }
    }
}

static void w_tower_piece(float cx, float cz, float y, int corner) {
    float hc = W_CELL_SIZE * 0.5f;
    /* Short wall stubs joining the tower to its two neighboring walls. */
    m_color(0.66f, 0.64f, 0.59f);
    if (corner == 0) { m_box(cx, y - 1.0f, cz - WALL_T, cx + hc, y + WALL_H, cz + WALL_T); m_box(cx - WALL_T, y - 1.0f, cz, cx + WALL_T, y + WALL_H, cz + hc); }
    if (corner == 1) { m_box(cx - hc, y - 1.0f, cz - WALL_T, cx, y + WALL_H, cz + WALL_T); m_box(cx - WALL_T, y - 1.0f, cz, cx + WALL_T, y + WALL_H, cz + hc); }
    if (corner == 2) { m_box(cx - hc, y - 1.0f, cz - WALL_T, cx, y + WALL_H, cz + WALL_T); m_box(cx - WALL_T, y - 1.0f, cz - hc, cx + WALL_T, y + WALL_H, cz); }
    if (corner == 3) { m_box(cx, y - 1.0f, cz - WALL_T, cx + hc, y + WALL_H, cz + WALL_T); m_box(cx - WALL_T, y - 1.0f, cz - hc, cx + WALL_T, y + WALL_H, cz); }
    /* Nav: tower + stubs block the ground; the wall-walk runs along the
     * stubs and through the tower's middle (doorways drawn below). */
    float top = y + WALL_H;
    w_nav_block_circle(cx, cz, 2.0f);
    if (corner == 0 || corner == 3) { w_nav_block(cx, cz - WALL_T, cx + hc, cz + WALL_T); w_nav_rect(1, cx - 1.0f, cz - WALL_T, cx + hc, cz + WALL_T, top, NAV_WALK); }
    if (corner == 1 || corner == 2) { w_nav_block(cx - hc, cz - WALL_T, cx, cz + WALL_T); w_nav_rect(1, cx - hc, cz - WALL_T, cx + 1.0f, cz + WALL_T, top, NAV_WALK); }
    if (corner == 0 || corner == 1) { w_nav_block(cx - WALL_T, cz, cx + WALL_T, cz + hc); w_nav_rect(1, cx - WALL_T, cz - 1.0f, cx + WALL_T, cz + hc, top, NAV_WALK); }
    if (corner == 2 || corner == 3) { w_nav_block(cx - WALL_T, cz - hc, cx + WALL_T, cz); w_nav_rect(1, cx - WALL_T, cz - hc, cx + WALL_T, cz + 1.0f, top, NAV_WALK); }
    m_color(0.69f, 0.67f, 0.62f);
    m_prism(cx, cz, 1.7f, y - 1.0f, y + 7.2f, 8);
    /* Dark doorways where the wall-walks enter the tower. */
    m_color(0.16f, 0.13f, 0.11f);
    if (corner == 0 || corner == 3) m_box(cx + 1.40f, top, cz - 0.45f, cx + 1.75f, top + 1.9f, cz + 0.45f);
    if (corner == 1 || corner == 2) m_box(cx - 1.75f, top, cz - 0.45f, cx - 1.40f, top + 1.9f, cz + 0.45f);
    if (corner == 0 || corner == 1) m_box(cx - 0.45f, top, cz + 1.40f, cx + 0.45f, top + 1.9f, cz + 1.75f);
    if (corner == 2 || corner == 3) m_box(cx - 0.45f, top, cz - 1.75f, cx + 0.45f, top + 1.9f, cz - 1.40f);
    m_color(0.69f, 0.67f, 0.62f);
    m_color(0.72f, 0.70f, 0.65f);
    m_prism(cx, cz, 2.0f, y + 7.2f, y + 7.9f, 8);
    m_color(0.62f, 0.22f, 0.20f);
    m_cone(cx, cz, 2.1f, y + 7.9f, y + 11.0f, 8);
}

static void w_keep_piece(float cx, float cz, float y, uint32_t h) {
    float s = 1.6f;
    w_nav_block(cx - 1.9f, cz - 1.9f, cx + 1.9f, cz + 1.9f);
    m_color(0.72f, 0.70f, 0.66f);
    m_boxc(cx, cz, s, s, y - 1.0f, y + 9.0f);
    m_color(0.74f, 0.72f, 0.67f);
    m_boxc(cx, cz, s + 0.3f, s + 0.3f, y + 9.0f, y + 9.6f);
    m_color(0.30f, 0.33f, 0.55f);
    m_pyramid(cx, cz, s + 0.4f, y + 9.6f, y + 13.5f);
    /* Banner pole + flag. */
    m_color(0.35f, 0.25f, 0.15f);
    m_boxc(cx, cz, 0.06f, 0.06f, y + 13.0f, y + 15.5f);
    if (h & 1u) m_color(0.85f, 0.15f, 0.15f); else m_color(0.95f, 0.80f, 0.15f);
    m_box(cx + 0.06f, y + 14.4f, cz - 0.04f, cx + 1.4f, y + 15.3f, cz + 0.04f);
}

#include "android/examples/castles_sites_build.h"
#include "android/examples/castles_structures.h"

/* ------------------------------------------------------------------ */
/* Chunk generation entry point                                        */
/* ------------------------------------------------------------------ */
/* Per-chunk outputs for the game layer. */
static int g_out_has_castle;
static float g_out_keep_x, g_out_keep_z, g_out_keep_y;
static unsigned char g_out_cells[W_NCELLS];   /* minimap: 0 water 1 land 2 mountain 3 road 4 castle 5 forest */
static float g_out_castle_base;               /* ground height inside the castle */

static void world_init(void) {
    WSite home;
    world_init_tiles();
    g_home_known = site_find_home(&g_home_tx, &g_home_tz, &home);
}

static void w_compute_terrain(void) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int i, j;
    for (j = 0; j < W_GRIDV; j++) {
        for (i = 0; i < W_GRIDV; i++) {
            float hh = w_height(ox + (float)i * W_STEP, oz + (float)j * W_STEP);
            g_hgt[j * W_GRIDV + i] = hh;
        }
    }
    for (j = 0; j < W_CELLS; j++) {
        for (i = 0; i < W_CELLS; i++) {
            int c = j * W_CELLS + i;
            /* Center vertex of the cell's 2x2 quads. */
            float hc = g_hgt[(j * W_SUB + 1) * W_GRIDV + i * W_SUB + 1];
            g_cheight[c] = hc;
            int cls = CC_LAND;
            if (hc < -0.15f) cls = CC_WATER;
            else {
                float lo = hc, hi = hc;
                int a, b;
                for (b = 0; b <= 2; b++) {
                    for (a = 0; a <= 2; a++) {
                        float v = g_hgt[(j * W_SUB + b) * W_GRIDV + i * W_SUB + a];
                        if (v < lo) lo = v;
                        if (v > hi) hi = v;
                    }
                }
                if (hc > 18.0f || hi - lo > 5.0f) cls = CC_MOUNTAIN;
            }
            g_cclass[c] = (unsigned char)cls;
            float fo = w_forest(ox + ((float)i + 0.5f) * W_CELL_SIZE, oz + ((float)j + 0.5f) * W_CELL_SIZE);
            g_cforest[c] = fo;
        }
    }
}

static void w_compute_borders(void) {
    int i;
    int kn = w_edge_road_south(g_ch_cx, g_ch_cz - 1);
    int ke = w_edge_road_east(g_ch_cx, g_ch_cz);
    int ks = w_edge_road_south(g_ch_cx, g_ch_cz);
    int kw = w_edge_road_east(g_ch_cx - 1, g_ch_cz);
    for (i = 0; i < W_CELLS; i++) {
        g_border[0 * W_CELLS + i] = (i == kn) ? SK_R : SK_G;
        g_border[1 * W_CELLS + i] = (i == ke) ? SK_R : SK_G;
        g_border[2 * W_CELLS + i] = (i == ks) ? SK_R : SK_G;
        g_border[3 * W_CELLS + i] = (i == kw) ? SK_R : SK_G;
    }
    /* Castle gate roads that leave a clearing across this chunk's edge:
     * the terminal cell is on one side, the anchor on the other; both
     * chunks derive the same edge from the site, so they agree. */
    int si, g;
    int gx0 = g_ch_cx * W_CELLS, gz0 = g_ch_cz * W_CELLS;
    for (si = 0; si < g_ncs; si++) {
        WSite *st = g_cs[si];
        for (g = 0; g < st->ngates; g++) {
            int tgx, tgz, dir;
            if (!site_gate_anchor(st, g, &tgx, &tgz, &dir)) continue;
            int agx = tgx + g_ddx_w(dir), agz = tgz + g_ddz_w(dir);
            int tin = tgx >= gx0 && tgz >= gz0 && tgx < gx0 + W_CELLS && tgz < gz0 + W_CELLS;
            int ain = agx >= gx0 && agz >= gz0 && agx < gx0 + W_CELLS && agz < gz0 + W_CELLS;
            if (tin == ain) continue;
            int lx = (tin ? tgx : agx) - gx0, lz = (tin ? tgz : agz) - gz0;
            int side = tin ? dir : ((dir + 2) & 3);           /* outward side of our cell */
            if (side == D_N) g_border[0 * W_CELLS + lx] = SK_R;
            if (side == D_E) g_border[1 * W_CELLS + lz] = SK_R;
            if (side == D_S) g_border[2 * W_CELLS + lx] = SK_R;
            if (side == D_W) g_border[3 * W_CELLS + lz] = SK_R;
        }
    }
}

/* Clearing cells, gate-road terminals and anchors for this chunk. */
static void w_compute_fixed(void) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int c, si, g;
    for (c = 0; c < W_NCELLS; c++) {
        float x = ox + ((float)(c % W_CELLS) + 0.5f) * W_CELL_SIZE, z = oz + ((float)(c / W_CELLS) + 0.5f) * W_CELL_SIZE;
        g_cfixed[c] = (unsigned char)w_in_clearing(x, z);
        g_cterm[c] = 0;
        g_canchor[c] = 0;
    }
    int gx0 = g_ch_cx * W_CELLS, gz0 = g_ch_cz * W_CELLS;
    for (si = 0; si < g_ncs; si++) {
        WSite *st = g_cs[si];
        for (g = 0; g < st->ngates; g++) {
            int tgx, tgz, dir;
            if (!site_gate_anchor(st, g, &tgx, &tgz, &dir)) continue;
            int agx = tgx + g_ddx_w(dir), agz = tgz + g_ddz_w(dir);
            if (tgx >= gx0 && tgz >= gz0 && tgx < gx0 + W_CELLS && tgz < gz0 + W_CELLS)
                g_cterm[(tgz - gz0) * W_CELLS + (tgx - gx0)] = (unsigned char)(dir + 1);
            if (agx >= gx0 && agz >= gz0 && agx < gx0 + W_CELLS && agz < gz0 + W_CELLS)
                g_canchor[(agz - gz0) * W_CELLS + (agx - gx0)] = (unsigned char)(((dir + 2) & 3) + 1);
        }
    }
}

/* Flatten the castle footprint (and blend one vertex ring around it). */
static void w_flatten_castle(void) {
    int i, j;
    int x0 = 99, x1 = -1, z0 = 99, z1 = -1;
    int c;
    for (c = 0; c < W_NCELLS; c++) {
        int rt = g_result[c];
        if (rt >= 0 && w_is_castle_kind(g_tkind[rt])) {
            int x = c % W_CELLS, z = c / W_CELLS;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (z < z0) z0 = z;
            if (z > z1) z1 = z;
        }
    }
    if (x1 < 0) return;
    int kc = g_castle_cz * W_CELLS + g_castle_cx;
    float base = g_cheight[kc];
    if (base < 1.2f) base = 1.2f;
    g_castle_base = base;
    int vi0 = x0 * W_SUB, vi1 = (x1 + 1) * W_SUB, vj0 = z0 * W_SUB, vj1 = (z1 + 1) * W_SUB;
    for (j = vj0 - 1; j <= vj1 + 1; j++) {
        for (i = vi0 - 1; i <= vi1 + 1; i++) {
            if (i <= 0 || j <= 0 || i >= W_GRID || j >= W_GRID) continue;
            int inside = (i >= vi0 && i <= vi1 && j >= vj0 && j <= vj1);
            float hv = g_hgt[j * W_GRIDV + i];
            float nv = inside ? base : (hv + base) * 0.5f;
            g_hgt[j * W_GRIDV + i] = nv;
        }
    }
}

static int w_is_road_tile(int t) {
    if (t < 0) return 0;
    return g_tkind[t] == K_ROAD || g_tkind[t] == K_BRIDGE || g_tkind[t] == K_GATE;
}

static void w_emit_props(void) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int c;
    float kingdom = w_kingdom(g_ch_cx, g_ch_cz);
    for (c = 0; c < W_NCELLS; c++) {
        int x = c % W_CELLS, z = c / W_CELLS;
        int t = g_result[c];
        if (t < 0) continue;
        if (g_cfixed[c]) {            /* castle clearing: drawn by castles_sites_build.h */
            g_out_cells[c] = (unsigned char)(w_site_court(ox + ((float)x + 0.5f) * W_CELL_SIZE, oz + ((float)z + 0.5f) * W_CELL_SIZE) ? 4 : 1);
            continue;
        }
        int k = g_tkind[t];
        int p = g_tparam[t];
        float lcx = ((float)x + 0.5f) * W_CELL_SIZE, lcz = ((float)z + 0.5f) * W_CELL_SIZE;
        float wx = ox + lcx, wz = oz + lcz;
        float gy = w_mesh_height(lcx, lcz);
        int gx = g_ch_cx * W_CELLS + x, gz = g_ch_cz * W_CELLS + z;
        uint32_t h = w_hash2(gx, gz, 555u);
        if (k == K_FOREST || (k == K_GRASS && (h & 15u) == 0u) || (k == K_ROCKS && (h & 3u) == 0u)) {
            int n = (k == K_FOREST) ? 2 + (int)(h % 3u) : 1;
            int q;
            for (q = 0; q < n; q++) {
                uint32_t hq = w_hash2(gx * 7 + q, gz * 13 - q, 909u);
                float jx = ((float)(hq & 255u) / 255.0f - 0.5f) * 3.0f;
                float jz = ((float)((hq >> 8) & 255u) / 255.0f - 0.5f) * 3.0f;
                float sz = 0.75f + (float)((hq >> 16) & 255u) / 255.0f * 0.6f;
                float ty = w_mesh_height(lcx + jx, lcz + jz);
                if (ty < 0.4f) continue;
                int pine = (ty > 7.0f) || ((hq >> 24) & 1u);
                if (g_ps_near_n > 0 && w_ps_covers(wx + jx, wz + jz, 1.0f)) continue;
                uint32_t rid = w_hash2(gx * 8 + q, gz, 0x7EE5u) & 0x3FFFFFFFu;
                if (g_ndepleted > 0 && w_res_depleted(rid)) continue;
                w_tree(wx + jx, wz + jz, ty, sz, pine);
                w_res_add(RES_WOOD, rid, wx + jx, wz + jz, ty, 0.5f);
            }
        }
        if (k == K_ROCKS) {
            int q;
            for (q = 0; q < 2; q++) {
                uint32_t hq = w_hash2(gx * 5 + q, gz * 11 + q, 313u);
                float jx = ((float)(hq & 255u) / 255.0f - 0.5f) * 2.6f;
                float jz = ((float)((hq >> 8) & 255u) / 255.0f - 0.5f) * 2.6f;
                float sz = 0.7f + (float)((hq >> 16) & 255u) / 255.0f * 0.9f;
                float ry = w_mesh_height(lcx + jx, lcz + jz);
                uint32_t rid = (w_hash2(gx * 8 + q, gz, 0x5701u) & 0x3FFFFFFFu) | 0x80000000u;
                if (g_ps_near_n > 0 && w_ps_covers(wx + jx, wz + jz, 1.5f)) continue;
                if (g_ndepleted > 0 && w_res_depleted(rid)) continue;
                w_rock(wx + jx, wz + jz, ry, sz);
                w_res_add(RES_STONE, rid, wx + jx, wz + jz, ry, 0.9f * sz);
            }
        }
        if (k == K_ROAD) {
            int d;
            for (d = 0; d < 4; d++) if (p & (1 << d)) w_road_segment(lcx, lcz, d);
            if (w_popcount4(p) == 1 && !(g_ps_near_n > 0 && w_ps_covers(wx, wz, 1.5f))) w_well(wx, wz, gy);
        }
        if (k == K_GRASS && (h & 15u) != 0u) {
            /* Village houses along roads, denser in kingdom regions. */
            int d, roadSide = -1;
            for (d = 0; d < 4; d++) {
                int n = w_nb(c, d);
                if (n >= 0 && w_is_road_tile(g_result[n]) && g_tkind[g_result[n]] == K_ROAD) { roadSide = d; break; }
            }
            float roll = (float)((h >> 4) & 255u) / 255.0f;
            if (roadSide >= 0 && roll < 0.15f + 0.55f * kingdom && g_cclass[c] == CC_LAND && !(g_ps_near_n > 0 && w_ps_covers(wx, wz, 2.5f))) {
                float tone = 0.9f + (float)((h >> 12) & 15u) / 15.0f * 0.15f;
                w_house(wx, wz, gy, (roadSide == D_N || roadSide == D_S), tone);
            }
        }
        if (k == K_BRIDGE) w_bridge(wx, wz, p == 5);
        if (k == K_DOCK) {
            float ddx = (float)g_ddx_w(p), ddz = (float)g_ddz_w(p);
            float px = wx + ddx * 1.0f, pz = wz + ddz * 1.0f;
            float py = w_mesh_height(lcx + ddx * 1.0f, lcz + ddz * 1.0f);
            float dyaw = approx_atan2_w(ddz, ddx);
            if (g_ndestroyed > 0 && w_is_destroyed(el_id_xz(px, pz))) { w_rubble_disc(px, pz, py, 1.6f); }
            else w_dock(px, pz, py, dyaw, 1);
            if (g_out_ndock < DOCK_OUT_MAX) {
                int q = g_out_ndock;
                g_out_dock_x[q] = px; g_out_dock_z[q] = pz; g_out_dock_y[q] = py; g_out_dock_yaw[q] = dyaw;
                g_out_ndock = q + 1;
            }
        }
        if (k == K_WATER && g_cheight[c] < -0.9f && (h % 7u) == 0u) {
            /* fish shoal: only in coastal water (land within two cells) */
            int coastal = 0, ddx2, ddz2;
            for (ddz2 = -2; ddz2 <= 2 && !coastal; ddz2++)
                for (ddx2 = -2; ddx2 <= 2; ddx2++)
                    if (w_cell_class_at(wx + (float)ddx2 * W_CELL_SIZE, wz + (float)ddz2 * W_CELL_SIZE) != CC_WATER) { coastal = 1; break; }
            if (coastal) {
                uint32_t hf = w_hash2(gx, gz, 0xF15Bu);
                uint32_t rid = (hf & 0x3FFFFFFFu) | 0x40000000u;
                float jx = ((float)(hf & 255u) / 255.0f - 0.5f) * 1.6f, jz = ((float)((hf >> 8) & 255u) / 255.0f - 0.5f) * 1.6f;
                if (!(g_ndepleted > 0 && w_res_depleted(rid))) w_res_add(RES_FOOD, rid, wx + jx, wz + jz, W_SEA_LEVEL, 2.5f);
            }
        }
        if (k == K_WALL || k == K_RAMP || k == K_GATE) w_wall_piece(wx, wz, g_castle_base, p, k);
        if (k == K_TOWER) w_tower_piece(wx, wz, g_castle_base, p);
        if (k == K_KEEP) {
            w_keep_piece(wx, wz, g_castle_base, h);
            g_out_has_castle = 1;
            g_out_keep_x = wx; g_out_keep_z = wz; g_out_keep_y = g_castle_base;
        }
        if (k == K_COURT) {
            uint32_t r = h % 6u;
            if (r == 0u) w_well(wx, wz, g_castle_base);
            else if (r == 1u || r == 2u) w_house(wx, wz, g_castle_base, (int)(h >> 9) & 1, 1.0f);
        }
        /* Minimap class. */
        int mc = 1;
        if (g_cclass[c] == CC_WATER) mc = 0;
        if (g_cclass[c] == CC_MOUNTAIN) mc = 2;
        if (k == K_FOREST) mc = 5;
        if (k == K_ROAD || k == K_BRIDGE || k == K_DOCK) mc = 3;
        if (w_is_castle_kind(k)) mc = 4;
        g_out_cells[c] = (unsigned char)mc;
    }
}

/* Generates chunk (cx,cz) into `out` (capacity `cap` vertices).
 * Returns the number of vertices written. */
static int world_gen_chunk(int cx, int cz, float *out, int cap) {
    g_ch_cx = cx; g_ch_cz = cz;
    g_mv = out; g_mn = 0; g_mcap = cap;
    g_out_has_castle = 0;
    g_out_nres = 0;
    g_out_ndock = 0;
    w_ps_collect_near();
    w_collect_sites();
    w_compute_terrain();
    w_compute_fixed();
    w_compute_borders();
    g_has_castle = 0;               /* castles are sites now (castles_sites.h) */
    g_castle_base = 0.0f;
    w_plan_chunk();
    w_wfc_solve();
    w_nav_init();
    int si, k;
    g_nlad = 0;
    for (si = 0; si < g_ncs; si++) {
        WSite *st = g_cs[si];
        g_cb_n[si] = site_layout(st, g_cb_t + si * SITE_MAXB, g_cb_x + si * SITE_MAXB, g_cb_z + si * SITE_MAXB,
                                 g_cb_yaw + si * SITE_MAXB, g_cb_hx + si * SITE_MAXB, g_cb_hz + si * SITE_MAXB);
        w_site_nav(si);
        if (w_in_chunk(st->cx, st->cz)) {
            g_out_has_castle = 1;
            g_out_keep_x = st->cx; g_out_keep_z = st->cz; g_out_keep_y = st->base;
        }
    }
    g_out_castle_base = g_ncs > 0 ? g_cs[0]->base : 0.0f;
    w_emit_terrain();
    w_emit_water();
    w_emit_props();
    for (si = 0; si < g_ncs; si++) w_site_emit(si);
    w_player_structures();
    for (k = 0; k < g_nlad; k++)
        w_ladder(g_lad_fx[k], g_lad_fz[k], g_lad_fy[k], g_lad_tx[k], g_lad_tz[k], g_lad_ty[k], g_lad_alongx[k]);
    return g_mn;
}

/* Natural ground height without castle plateaus (uncached, so safe on
 * any thread) -- for camera clamping where no chunk is loaded. */
static float world_ground_height(float x, float z) {
    float h = w_height_raw(x, z);
    return h < W_SEA_LEVEL ? W_SEA_LEVEL : h;
}

/* Deterministic integer checksums for comparing a squash ARM64 build
 * against a host gcc build. Writes `n` ints into out. */
static int world_selftest(int *out, float *scratch, int cap) {
    int n = 0;
    out[n] = (int)(w_hash(12345u) & 0x7fffffff); n++;
    out[n] = (int)(w_hash2(-7, 19, 3u) & 0x7fffffff); n++;
    out[n] = (int)(w_simplex(1.3f, -2.7f, 5u) * 10000.0f); n++;
    out[n] = (int)(w_fbm(10.1f, 3.3f, 5, W_SEED_H) * 10000.0f); n++;
    out[n] = (int)(w_height(123.0f, -456.0f) * 1000.0f); n++;
    out[n] = (int)(w_sqrt(2.0f) * 100000.0f); n++;
    out[n] = (int)(w_sin(1.0f) * 100000.0f); n++;
    out[n] = (int)(w_cos(-2.5f) * 100000.0f); n++;
    out[n] = g_nt; n++;
    int k;
    for (k = 0; k < 4; k++) {
        /* chunk 3: the home castle's centre (walls, gates, ladders, buildings) */
        int cx = (k == 0) ? 0 : (k == 1) ? 3 : (k == 2) ? -2 : 0;
        int cz = (k == 0) ? 0 : (k == 1) ? -1 : (k == 2) ? 4 : 0;
        if (k == 3 && g_home_known) {
            WSite *hs = w_site_cached(g_home_tx, g_home_tz);
            cx = w_ifloor(hs->cx / W_CHUNK_SIZE); cz = w_ifloor(hs->cz / W_CHUNK_SIZE);
        }
        int vn = world_gen_chunk(cx, cz, scratch, cap);
        uint32_t th = 0u;
        int c;
        for (c = 0; c < W_NCELLS; c++) {
            uint32_t th2 = ((th * 31u) & 0xFFFFFFFFu) + (uint32_t)(g_result[c] + 1);
            th = th2 & 0xFFFFFFFFu;
        }
        out[n] = (int)(th & 0x7fffffff); n++;
        out[n] = vn; n++;
        out[n] = g_wfc_attempts * 10 + g_out_has_castle; n++;
        /* Nav grid: walkable count per layer, ladder count, exact flag
         * hash, and the height total. (Heights are compared as a sum, not
         * hashed per cell: squash evaluates float locals in double
         * precision, so a per-cell truncation like (int)(h*4) can land
         * on the other side of a boundary than a gcc float build --
         * confirmed by building this file with -Dfloat=double.) */
        int walk0 = 0, walk1 = 0, ladders = 0, q;
        uint32_t fh = 0u;
        float hsum = 0.0f;
        for (q = 0; q < NAV_L * NAV_N; q++) {
            int f = g_nav_f[q];
            if (f & NAV_WALK) {
                if (q < NAV_N) walk0++; else walk1++;
                float hs2 = hsum + g_nav_h[q];
                hsum = hs2;
            }
            if (f & NAV_LADDER) ladders++;
            uint32_t fh2 = ((fh * 131u) & 0xFFFFFFFFu) + (uint32_t)f;
            fh = fh2 & 0xFFFFFFFFu;
        }
        out[n] = walk0 * 10000 + walk1; n++;
        out[n] = ladders * 1000000 + (int)hsum; n++;
        out[n] = (int)(fh & 0x7fffffff); n++;
    }
    return n;
}

#endif /* CASTLES_WORLD_H */
