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

#define W_MAXT 48
static int g_nt;                               /* tile count              */
static unsigned char g_tsock[W_MAXT * 4];      /* sockets N,E,S,W         */
static unsigned char g_tkind[W_MAXT];
static unsigned char g_tparam[W_MAXT];
static int g_tile_keep;                        /* index of the KEEP tile  */
static int g_tile_water;
static int g_tile_road_base;                   /* road tile for mask m is g_tile_road_base + m - 1 */
static int g_tile_tower0, g_tile_wall0, g_tile_ramp0, g_tile_gate0, g_tile_court;

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

static float w_height(float x, float z) {
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

/* Castle placement for a chunk, a pure function of the chunk coords.
 * Returns 1 and fills cell-space center + half-size if this chunk wants a
 * castle. Castles stay inside cells [2,9] so terrain flattening never
 * touches a chunk-border vertex (keeps terrain seamless). */
/* Checks a castle footprint centered on cell (x,z) with half-size hs:
 * dry, not too high, not too steep (it gets flattened). */
static int w_castle_site_ok(int cx, int cz, int x, int z, int hs) {
    float wx = (float)cx * W_CHUNK_SIZE + ((float)x + 0.5f) * W_CELL_SIZE;
    float wz = (float)cz * W_CHUNK_SIZE + ((float)z + 0.5f) * W_CELL_SIZE;
    float span = (float)hs * W_CELL_SIZE + 2.0f;
    float lo = 1000.0f, hi = -1000.0f;
    int a, b;
    for (a = -1; a <= 1; a++) {
        for (b = -1; b <= 1; b++) {
            float hh = w_height(wx + (float)a * span, wz + (float)b * span);
            if (hh < lo) lo = hh;
            if (hh > hi) hi = hh;
        }
    }
    if (lo < 0.4f || hi > 18.0f || hi - lo > 9.0f) return 0;
    return 1;
}

static int w_chunk_castle(int cx, int cz, int *ccx, int *ccz, int *half) {
    float k = w_kingdom(cx, cz);
    float p = w_smooth(0.25f, 0.70f, k) * 0.9f;
    float roll = w_hash01(cx, cz, W_SEED_C);
    if (roll >= p) return 0;
    uint32_t h = w_hash2(cx, cz, W_SEED_C + 77u);
    /* Candidate sites: big castles near the middle first, then smaller
     * ones further out. Footprints always stay inside cells [2,9]. */
    int i;
    for (i = 0; i < 12; i++) {
        uint32_t hi = w_hash(h + (uint32_t)i * 7919u);
        int hs, x, z;
        if (i < 4) { hs = 3; x = 5 + (int)(hi & 1u); z = 5 + (int)((hi >> 1) & 1u); }
        else { hs = 2; x = 4 + (int)(hi & 3u); z = 4 + (int)((hi >> 2) & 3u); }
        if (i == 0 && ((h >> 9) & 1u)) continue;  /* some chunks prefer small castles */
        if (w_castle_site_ok(cx, cz, x, z, hs)) {
            *ccx = x; *ccz = z; *half = hs;
            return 1;
        }
    }
    return 0;
}

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
    int dx, dz, hs;
    int c1 = w_chunk_castle(cx, cz, &dx, &dz, &hs);
    int c2 = w_chunk_castle(cx + 1, cz, &dx, &dz, &hs);
    if (c1 || c2) { float p2 = p + 0.35f; p = p2; }
    float roll = (float)((h >> 8) & 0xFFFFu) * (1.0f / 65536.0f);
    if (roll >= p) return -1;
    float ex = (float)(cx + 1) * W_CHUNK_SIZE;
    float ez = (float)cz * W_CHUNK_SIZE + ((float)k + 0.5f) * W_CELL_SIZE;
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
    int dx, dz, hs;
    int c1 = w_chunk_castle(cx, cz, &dx, &dz, &hs);
    int c2 = w_chunk_castle(cx, cz + 1, &dx, &dz, &hs);
    if (c1 || c2) { float p2 = p + 0.35f; p = p2; }
    float roll = (float)((h >> 8) & 0xFFFFu) * (1.0f / 65536.0f);
    if (roll >= p) return -1;
    float ex = (float)cx * W_CHUNK_SIZE + ((float)k + 0.5f) * W_CELL_SIZE;
    float ez = (float)(cz + 1) * W_CHUNK_SIZE;
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

static void w_plan_chunk(void) {
    int i, x, z;
    int crossings[16];
    int ncross = 0;
    for (i = 0; i < W_NCELLS; i++) { g_plan[i] = 0; g_cplan[i] = 0; g_czone[i] = 0; }
    g_hub = -1;
    g_castle_gate_side = D_S;

    /* Border crossings: border cell + the outward direction bit. */
    for (i = 0; i < W_CELLS; i++) {
        if (g_border[0 * W_CELLS + i] == SK_R) { int c = i; g_plan[c] = (unsigned char)(g_plan[c] | 1); crossings[ncross] = c; ncross++; }
        if (g_border[1 * W_CELLS + i] == SK_R) { int c = i * W_CELLS + W_CELLS - 1; g_plan[c] = (unsigned char)(g_plan[c] | 2); crossings[ncross] = c; ncross++; }
        if (g_border[2 * W_CELLS + i] == SK_R) { int c = (W_CELLS - 1) * W_CELLS + i; g_plan[c] = (unsigned char)(g_plan[c] | 4); crossings[ncross] = c; ncross++; }
        if (g_border[3 * W_CELLS + i] == SK_R) { int c = i * W_CELLS; g_plan[c] = (unsigned char)(g_plan[c] | 8); crossings[ncross] = c; ncross++; }
    }

    if (g_has_castle) {
        int x0 = g_castle_cx - g_castle_half, x1 = g_castle_cx + g_castle_half;
        int z0 = g_castle_cz - g_castle_half, z1 = g_castle_cz + g_castle_half;
        /* Gate faces the average direction of the road crossings. */
        float ax = 0.0f, az = 0.0f;
        for (i = 0; i < ncross; i++) {
            float dx = (float)(crossings[i] % W_CELLS - g_castle_cx);
            float dz = (float)(crossings[i] / W_CELLS - g_castle_cz);
            float nax = ax + dx, naz = az + dz;
            ax = nax; az = naz;
        }
        if (ncross == 0) {
            g_castle_gate_side = (int)(w_hash2(g_ch_cx, g_ch_cz, 991u) & 3u);
        } else if (w_absf(ax) > w_absf(az)) {
            g_castle_gate_side = ax > 0.0f ? D_E : D_W;
        } else {
            g_castle_gate_side = az > 0.0f ? D_S : D_N;
        }
        uint32_t rh = w_hash2(g_ch_cx, g_ch_cz, 4242u);
        for (z = z0; z <= z1; z++) {
            for (x = x0; x <= x1; x++) {
                int c = z * W_CELLS + x;
                int tile;
                g_czone[c] = 1;
                if (x == x0 && z == z0) tile = g_tile_tower0 + 0;
                else if (x == x1 && z == z0) tile = g_tile_tower0 + 1;
                else if (x == x1 && z == z1) tile = g_tile_tower0 + 2;
                else if (x == x0 && z == z1) tile = g_tile_tower0 + 3;
                else {
                    int side = -1;
                    if (z == z0) side = D_N;
                    else if (x == x1) side = D_E;
                    else if (z == z1) side = D_S;
                    else if (x == x0) side = D_W;
                    if (side < 0) {
                        tile = (x == g_castle_cx && z == g_castle_cz) ? g_tile_keep : g_tile_court;
                    } else {
                        int mid = (side == D_N || side == D_S) ? (x == g_castle_cx) : (z == g_castle_cz);
                        if (mid && side == g_castle_gate_side) tile = g_tile_gate0 + side;
                        else if (((rh >> ((x * 3 + z * 5) & 15)) & 3u) == 0u) tile = g_tile_ramp0 + side;
                        else tile = g_tile_wall0 + side;
                    }
                }
                g_cplan[c] = (unsigned char)(tile + 1);
            }
        }
        /* Hub just outside the gate. */
        int gx = g_castle_cx, gz = g_castle_cz;
        if (g_castle_gate_side == D_N) gz = z0 - 1;
        if (g_castle_gate_side == D_S) gz = z1 + 1;
        if (g_castle_gate_side == D_E) gx = x1 + 1;
        if (g_castle_gate_side == D_W) gx = x0 - 1;
        g_hub = gz * W_CELLS + gx;
        /* Hub connects into the gate. */
        g_plan[g_hub] = (unsigned char)(g_plan[g_hub] | (1 << ((g_castle_gate_side + 2) & 3)));
    } else if (ncross > 0) {
        uint32_t h = w_hash2(g_ch_cx, g_ch_cz, 31337u);
        int hx = 4 + (int)(h % 4u), hz = 4 + (int)((h >> 4) % 4u);
        g_hub = hz * W_CELLS + hx;
        /* Prefer a dry hub: move to the nearest land cell if needed. */
        if (g_cclass[g_hub] != CC_LAND) {
            int bestc = -1, bestd = 1000;
            for (i = 0; i < W_NCELLS; i++) {
                if (g_cclass[i] != CC_LAND) continue;
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
            if (w_is_castle_kind(k)) {
                if (zone) {
                    if (g_cplan[c] == t + 1) w = 30.0f;
                    else if (k == K_KEEP) w = 0.0f;
                    else w = 0.25f;
                }
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
                    int forced = 0, dd;
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
            int court = 0;
            if (rt >= 0) {
                int k = g_tkind[rt];
                if (k == K_COURT || k == K_KEEP || k == K_GATE || k == K_TOWER || k == K_WALL || k == K_RAMP) court = 1;
            }
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
/* Props                                                               */
/* ------------------------------------------------------------------ */
static void w_tree(float x, float z, float y, float size, int pine) {
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
    m_color(0.47f, 0.46f, 0.44f);
    m_cone(x, z, 1.1f * size, y - 0.4f, y + 1.1f * size, 5);
}

static void w_house(float x, float z, float y, int alongX, float tone) {
    float hx = alongX ? 1.5f : 1.1f, hz = alongX ? 1.1f : 1.5f;
    m_color(0.86f * tone, 0.80f * tone, 0.66f * tone);
    m_boxc(x, z, hx, hz, y - 0.5f, y + 2.0f);
    m_color(0.66f, 0.24f, 0.18f);
    m_gable(x, z, hx + 0.25f, hz + 0.25f, y + 2.0f, y + 3.4f, alongX);
}

static void w_well(float x, float z, float y) {
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

static void w_merlons_x(float x0, float x1, float z, float y) {
    float x;
    m_color(0.70f, 0.68f, 0.63f);
    for (x = x0 + 0.35f; x < x1 - 0.2f; x = x + 1.0f) {
        m_box(x, y, z - WALL_T, x + 0.5f, y + 0.6f, z + WALL_T);
    }
}

static void w_merlons_z(float z0, float z1, float x, float y) {
    float z;
    m_color(0.70f, 0.68f, 0.63f);
    for (z = z0 + 0.35f; z < z1 - 0.2f; z = z + 1.0f) {
        m_box(x - WALL_T, y, z, x + WALL_T, y + 0.6f, z + 0.5f);
    }
}

/* Wall segment across a cell on side s, centered on the cell's center
 * line (cells are 4 units; walls run edge to edge). */
static void w_wall_piece(float cx, float cz, float y, int s, int kind) {
    float hc = W_CELL_SIZE * 0.5f;
    int alongX = (s == D_N || s == D_S);
    m_color(0.66f, 0.64f, 0.59f);
    if (kind == K_GATE) {
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
        w_merlons_x(cx - hc, cx + hc, cz, y + WALL_H);
    } else {
        m_box(cx - WALL_T, y - 1.0f, cz - hc, cx + WALL_T, y + WALL_H, cz + hc);
        w_merlons_z(cz - hc, cz + hc, cx, y + WALL_H);
    }
    if (kind == K_RAMP) {
        /* Ramp-wall: a stone ramp on the courtyard side climbing to the
         * wall walk. Inward direction is opposite of s. */
        float a[3], b[3], c[3], d[3], e[3], f[3];
        float top = y + WALL_H - 0.2f;
        m_color(0.60f, 0.57f, 0.52f);
        if (alongX) {
            float zi = (s == D_N) ? cz + WALL_T : cz - WALL_T;
            float zo = (s == D_N) ? zi + 1.4f : zi - 1.4f;
            m_ref(cx + 0.6f, y + 1.0f, (zi + zo) * 0.5f);
            m_set3(a, cx - hc + 0.2f, y, zi); m_set3(b, cx + hc - 0.2f, y, zi);
            m_set3(c, cx + hc - 0.2f, top, zi);
            m_set3(d, cx - hc + 0.2f, y, zo); m_set3(e, cx + hc - 0.2f, y, zo);
            m_set3(f, cx + hc - 0.2f, top, zo);
            m_tri(a, b, c); m_tri(d, e, f);
            m_quad(a, c, f, d);          /* sloped walking surface */
            m_quad(b, e, f, c);          /* tall end               */
        } else {
            float xi = (s == D_W) ? cx + WALL_T : cx - WALL_T;
            float xo = (s == D_W) ? xi + 1.4f : xi - 1.4f;
            m_ref((xi + xo) * 0.5f, y + 1.0f, cz + 0.6f);
            m_set3(a, xi, y, cz - hc + 0.2f); m_set3(b, xi, y, cz + hc - 0.2f);
            m_set3(c, xi, top, cz + hc - 0.2f);
            m_set3(d, xo, y, cz - hc + 0.2f); m_set3(e, xo, y, cz + hc - 0.2f);
            m_set3(f, xo, top, cz + hc - 0.2f);
            m_tri(a, b, c); m_tri(d, e, f);
            m_quad(a, c, f, d);
            m_quad(b, e, f, c);
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
    m_color(0.69f, 0.67f, 0.62f);
    m_prism(cx, cz, 1.7f, y - 1.0f, y + 7.2f, 8);
    m_color(0.72f, 0.70f, 0.65f);
    m_prism(cx, cz, 2.0f, y + 7.2f, y + 7.9f, 8);
    m_color(0.62f, 0.22f, 0.20f);
    m_cone(cx, cz, 2.1f, y + 7.9f, y + 11.0f, 8);
}

static void w_keep_piece(float cx, float cz, float y, uint32_t h) {
    float s = 1.6f;
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

/* ------------------------------------------------------------------ */
/* Chunk generation entry point                                        */
/* ------------------------------------------------------------------ */
/* Per-chunk outputs for the game layer. */
static int g_out_has_castle;
static float g_out_keep_x, g_out_keep_z, g_out_keep_y;
static unsigned char g_out_cells[W_NCELLS];   /* minimap: 0 water 1 land 2 mountain 3 road 4 castle 5 forest */

static void world_init(void) {
    world_init_tiles();
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
    /* A crossing cell must be land on this side too (edge checks already
     * looked +-6 units, i.e. into both border cells). */
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
                w_tree(wx + jx, wz + jz, ty, sz, pine);
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
                w_rock(wx + jx, wz + jz, ry, sz);
            }
        }
        if (k == K_ROAD) {
            int d;
            for (d = 0; d < 4; d++) if (p & (1 << d)) w_road_segment(lcx, lcz, d);
            if (w_popcount4(p) == 1) w_well(wx, wz, gy);
        }
        if (k == K_GRASS && (h & 15u) != 0u) {
            /* Village houses along roads, denser in kingdom regions. */
            int d, roadSide = -1;
            for (d = 0; d < 4; d++) {
                int n = w_nb(c, d);
                if (n >= 0 && w_is_road_tile(g_result[n]) && g_tkind[g_result[n]] == K_ROAD) { roadSide = d; break; }
            }
            float roll = (float)((h >> 4) & 255u) / 255.0f;
            if (roadSide >= 0 && roll < 0.15f + 0.55f * kingdom && g_cclass[c] == CC_LAND) {
                float tone = 0.9f + (float)((h >> 12) & 15u) / 15.0f * 0.15f;
                w_house(wx, wz, gy, (roadSide == D_N || roadSide == D_S), tone);
            }
        }
        if (k == K_BRIDGE) w_bridge(wx, wz, p == 5);
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
        if (k == K_ROAD || k == K_BRIDGE) mc = 3;
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
    w_compute_terrain();
    w_compute_borders();
    int ccx = 0, ccz = 0, half = 0;
    g_has_castle = w_chunk_castle(cx, cz, &ccx, &ccz, &half);
    g_castle_cx = ccx; g_castle_cz = ccz; g_castle_half = half;
    g_castle_base = 0.0f;
    w_plan_chunk();
    w_wfc_solve();
    w_flatten_castle();
    w_emit_terrain();
    w_emit_water();
    w_emit_props();
    return g_mn;
}

/* Height of the final (possibly castle-flattened) ground at a world
 * point -- uses the raw noise height; good enough for camera clamping. */
static float world_ground_height(float x, float z) {
    float h = w_height(x, z);
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
        int cx = (k == 0) ? 0 : (k == 1) ? 3 : (k == 2) ? -2 : 5;
        int cz = (k == 0) ? 0 : (k == 1) ? -1 : (k == 2) ? 4 : 5;
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
    }
    return n;
}

#endif /* CASTLES_WORLD_H */
