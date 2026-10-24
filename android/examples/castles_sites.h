/* ---- castles_sites.h: castles as world-scale sites (castles_world.h) ----
 *
 * Castles are no longer single-chunk WFC structures. The world is divided
 * into "kingdom tiles" of SITE_TILE_CH x SITE_TILE_CH chunks; each tile may
 * hold one castle site, a pure function of the tile coordinates:
 *
 *   shape    polygon with 3..12 sides (3 = triangular, 4 = square, ...),
 *            star (5..8 points) or circular
 *   rings    the curtain wall plus 0..3 concentric inner rings
 *   bastion  any shape can be a bastion fort: arrowhead bastions replace
 *            the corner towers of the outer ring
 *   walls    1..3 "walls thick"; the top is a walkable wall-walk behind a
 *            non-walkable merlon parapet on the outer edge; inner rings
 *            stand taller (concentric castle)
 *   gates    gatehouses (flanking towers + gate block) where roads arrive;
 *            gates face neighbouring castle sites so roads link cities,
 *            and inner rings are gated on the same lines (radial streets)
 *   size     radius 24..70 -- up to three chunks across
 *
 * Every chunk generates only the parts of the castles that touch it, from
 * the site description alone, so castles are seamless across chunks just
 * like the terrain. The terrain itself is "precomputed" for them: within a
 * site's clearing radius the height is a flat plateau, blended smoothly
 * back to natural terrain around it (w_height), so big castles always have
 * room. WFC still lays out the countryside; clearing cells are fixed for
 * it, and each gatehouse road leaves the clearing at a globally defined
 * anchor cell that WFC must connect to the road network (castles_world.h,
 * w_compute_fixed / w_compute_borders).
 *
 * Interiors: a town centre (hall, bell tower, market stalls) at the
 * middle, radial streets from the gates, barracks and houses on the lots
 * between rings -- more rings and a bigger radius give more lots.
 *
 * Thread note: w_height() (blended) uses a site cache and must only run
 * on the generator thread; the render thread calls w_site_compute()
 * directly (uncached) and reads heights from the streamed chunks. */

#ifndef CASTLES_SITES_H
#define CASTLES_SITES_H

#define SITE_TILE_CH   5
#define SITE_TILE      (W_CHUNK_SIZE * (float)SITE_TILE_CH)      /* 240 units */
#define SITE_BLEND     24.0f      /* plateau -> natural terrain blend width */
#define SITE_BLEND_MAX 59.0f      /* widest blend (big height differences)  */
#define W_SEED_S       0x51735u

#define SHAPE_POLY   0
#define SHAPE_STAR   1
#define SHAPE_CIRCLE 2

#define SITE_MAXV     24          /* ring vertices                          */
#define SITE_MAXG     4           /* gates                                  */
#define GATE_HALF     1.7f        /* half width of a gate passage           */
#define PARAPET       0.45f       /* outer, non-walkable band of a wall top */

#define BT_HOUSE      1
#define BT_BARRACKS   2
#define BT_TOWNCENTER 3
#define BT_STALL      4
#define BT_DOCK       5           /* fishing dock (world or player-built; game layer only) */
#define SITE_MAXB     160

typedef struct {
    int valid, tx, tz;
    int shape, sides, rings, bastion, ngates, home;
    float cx, cz, base;           /* centre and plateau height             */
    float R, Rc;                  /* outer ring radius, clearing radius    */
    float blend;                  /* plateau -> terrain blend width        */
    float core, band;             /* innermost ring radius, ring spacing   */
    float rot, wt, wh;            /* rotation, outer wall thickness/height */
    float gate_a[SITE_MAXG];      /* gate angles (radians)                 */
} WSite;

/* ---- per-ring parameters ---- */
/* Rings are spaced by a fixed band (street + a row of buildings), so
 * every extra ring makes the castle bigger instead of squeezing it. */
static float site_ring_r(const WSite *s, int k) { return s->core + s->band * (float)(s->rings - 1 - k); }
static float site_ring_t(const WSite *s, int k) { float t = s->wt - 1.3f * (float)k; return t < 1.3f ? 1.3f : t; }
static float site_ring_h(const WSite *s, int k) { return s->wh + 1.1f * (float)k; }
static float site_ring_top(const WSite *s, int k) { return s->base + site_ring_h(s, k); }

/* Ring polygon vertices into xs/zs; returns the count. */
static int site_ring_verts(const WSite *s, int k, float *xs, float *zs) {
    float r = site_ring_r(s, k);
    int n, i;
    if (s->shape == SHAPE_POLY) n = s->sides;
    else if (s->shape == SHAPE_STAR) n = s->sides * 2;
    else n = 24;
    for (i = 0; i < n; i++) {
        float a = s->rot + (float)i * (2.0f * W_PI) / (float)n;
        float ri = r;
        if (s->shape == SHAPE_STAR && (i & 1)) ri = r * 0.72f;
        xs[i] = s->cx + w_cos(a) * ri;
        zs[i] = s->cz + w_sin(a) * ri;
    }
    return n;
}

/* Is vertex i of a ring a "corner" (tower / bastion position)? */
static int site_corner(const WSite *s, int i, int forBastion) {
    if (s->shape == SHAPE_POLY) return 1;
    if (s->shape == SHAPE_STAR) return (i & 1) == 0;
    return forBastion ? (i % 4) == 0 : (i % 3) == 0;
}

/* Inscribed radius of ring k (closest the wall comes to the centre). */
static float site_ring_inner_r(const WSite *s, int k) {
    float r = site_ring_r(s, k);
    if (s->shape == SHAPE_POLY) return r * w_cos(W_PI / (float)s->sides);
    if (s->shape == SHAPE_STAR) return r * 0.72f * w_cos(W_PI / (float)(s->sides * 2));
    return r * w_cos(W_PI / 24.0f);
}

/* ---- site computation (pure function of the tile) ---- */
/* Shape, rings, bastion and wall thickness of tile (tx,tz). */
static void site_style(int tx, int tz, WSite *s) {
    uint32_t h = w_hash2(tx, tz, W_SEED_S + 5u);
    uint32_t sh = h % 10u;
    if (sh < 6u) {
        s->shape = SHAPE_POLY;
        /* square and triangle most common, then 5..12 sides */
        uint32_t q = (h >> 4) % 12u;
        if (q < 3u) s->sides = 4; else if (q < 5u) s->sides = 3; else s->sides = 5 + (int)((h >> 8) % 8u);
    } else if (sh < 8u) {
        s->shape = SHAPE_STAR;
        s->sides = 5 + (int)((h >> 8) % 4u);
    } else {
        s->shape = SHAPE_CIRCLE;
        s->sides = 24;
    }
    s->rings = 1 + (int)((h >> 12) % 4u);                   /* curtain + 0..3 inner rings */
    s->bastion = ((h >> 16) % 10u) < 3u;
    int thick = 1 + (int)((h >> 20) % 3u);
    s->wt = 1.3f * (float)thick;
    s->wh = 4.6f + 0.8f * (float)(thick - 1);
    s->rot = (float)((h >> 24) & 255u) / 256.0f * 2.0f * W_PI;
}

/* How far the walls of a polygon/star/circle come in from the vertex
 * radius (inscribed / vertex ratio). */
static float site_inscribed_ratio(const WSite *s) {
    if (s->shape == SHAPE_POLY) return w_cos(W_PI / (float)s->sides);
    if (s->shape == SHAPE_STAR) return 0.72f * w_cos(W_PI / (float)(s->sides * 2));
    return w_cos(W_PI / 24.0f);
}

/* Candidate placement `cand` at size `scale`: centre, core radius, ring
 * band, outer radius and clearing radius. The core fits the town-centre
 * plaza plus a row of lots; each inner ring adds a band. */
static int site_roll_at(int tx, int tz, int cand, float scale, WSite *s, float *cx, float *cz, float *R, float *Rc) {
    uint32_t h = w_hash2(tx, tz, W_SEED_S);
    int ccx = tx * SITE_TILE_CH + SITE_TILE_CH / 2, ccz = tz * SITE_TILE_CH + SITE_TILE_CH / 2;
    float k = w_kingdom(ccx, ccz);
    float p = 0.15f + w_smooth(0.20f, 0.70f, k) * 0.75f;
    float roll = (float)((h >> 8) & 0xFFFFu) * (1.0f / 65536.0f);
    if (roll >= p) return 0;
    site_style(tx, tz, s);
    float u = (float)((h >> 3) & 255u) / 255.0f;
    float ins = site_inscribed_ratio(s);
    /* radii are vertex radii: divide by the inscribed ratio so the room
     * inside the walls is what we asked for */
    float core = (22.0f + (12.0f + 10.0f * k) * u) * scale / ins;
    float band = (14.0f + 1.3f * s->wt) / ins;
    float r = core + band * (float)(s->rings - 1);
    float rc = r + (s->bastion ? 3.5f + s->wt : 2.0f) * 2.0f + 8.0f;
    float margin = rc + SITE_BLEND * 0.5f;
    float span = SITE_TILE - 2.0f * margin;
    while (span < 0.0f && s->rings > 1) {
        s->rings = s->rings - 1;
        r = core + band * (float)(s->rings - 1);
        rc = r + (s->bastion ? 3.5f + s->wt : 2.0f) * 2.0f + 8.0f;
        margin = rc + SITE_BLEND * 0.5f;
        span = SITE_TILE - 2.0f * margin;
    }
    if (span < 0.0f) return 0;
    float jx = w_hash01(tx * 4 + cand, tz, W_SEED_S + 1u), jz = w_hash01(tx, tz * 4 + cand, W_SEED_S + 2u);
    *cx = (float)tx * SITE_TILE + margin + span * jx;
    *cz = (float)tz * SITE_TILE + margin + span * jz;
    *R = r; *Rc = rc;
    s->core = core; s->band = band;
    return 1;
}

/* Cheap "does this tile have a castle, and roughly where" (first
 * candidate, full size) -- used to aim gates at neighbouring castles. */
static int site_roll(int tx, int tz, float *cx, float *cz, float *R, float *Rc) {
    WSite tmp;
    return site_roll_at(tx, tz, 0, 1.0f, &tmp, cx, cz, R, Rc);
}

static void site_add_gate(WSite *s, float a) {
    int g;
    for (g = 0; g < s->ngates; g++) {
        float d = a - s->gate_a[g];
        while (d > W_PI) { float t = d - 2.0f * W_PI; d = t; }
        while (d < -W_PI) { float t = d + 2.0f * W_PI; d = t; }
        if (d < 0.9f && d > -0.9f) return;      /* too close to an existing gate */
    }
    if (s->ngates < SITE_MAXG) { s->gate_a[s->ngates] = a; s->ngates = s->ngates + 1; }
}

/* Snaps a desired gate angle onto the middle of a curtain edge (so the
 * gatehouse never lands on a corner tower or bastion). */
static float site_snap_gate(const WSite *s, float a) {
    if (s->shape == SHAPE_CIRCLE) {
        /* between corner towers: middle of a 3-vertex span */
        float step = 2.0f * W_PI / 24.0f;
        float rel = (a - s->rot) / step;
        int i = w_ifloor(rel / 3.0f) * 3;
        return s->rot + ((float)i + 1.5f) * step;
    }
    int n = s->shape == SHAPE_POLY ? s->sides : s->sides * 2;
    float step = 2.0f * W_PI / (float)n;
    float rel = (a - s->rot) / step;
    int i = w_ifloor(rel);
    return s->rot + ((float)i + 0.5f) * step;
}

static float site_raw_ground(float x, float z) {
    float h = w_height_raw(x, z);
    return h;
}

/* Full site description for tile (tx,tz). s->valid = 0 if no castle. */
static void site_clear(WSite *s) {
    int g;
    s->valid = 0; s->tx = 0; s->tz = 0; s->shape = 0; s->sides = 0; s->rings = 0;
    s->bastion = 0; s->ngates = 0; s->home = 0;
    s->cx = 0.0f; s->cz = 0.0f; s->base = 0.0f; s->R = 0.0f; s->Rc = 0.0f; s->blend = 0.0f;
    s->core = 0.0f; s->band = 0.0f;
    s->rot = 0.0f; s->wt = 0.0f; s->wh = 0.0f;
    for (g = 0; g < SITE_MAXG; g++) s->gate_a[g] = 0.0f;
}

static void site_copy(WSite *d, const WSite *s) {
    int g;
    d->valid = s->valid; d->tx = s->tx; d->tz = s->tz; d->shape = s->shape; d->sides = s->sides;
    d->rings = s->rings; d->bastion = s->bastion; d->ngates = s->ngates; d->home = s->home;
    d->cx = s->cx; d->cz = s->cz; d->base = s->base; d->R = s->R; d->Rc = s->Rc; d->blend = s->blend;
    d->core = s->core; d->band = s->band;
    d->rot = s->rot; d->wt = s->wt; d->wh = s->wh;
    for (g = 0; g < SITE_MAXG; g++) d->gate_a[g] = s->gate_a[g];
}

static void w_site_compute(int tx, int tz, WSite *s) {
    float cx, cz, R, Rc;
    site_clear(s);
    s->tx = tx; s->tz = tz;
    if (!site_roll(tx, tz, &cx, &cz, &R, &Rc)) return;
    /* Find room: 4 candidate positions x 3 sizes; score the ground each
     * would replace (water is expensive, height range less so) and keep
     * the best. The plateau then flattens whatever is there. */
    float bestScore = 1e30f, bcx = 0.0f, bcz = 0.0f, bR = 0.0f, bRc = 0.0f, bBase = 0.0f, bRange = 0.0f;
    float bCore = 0.0f, bBand = 0.0f;
    int bRings = 1;
    int cand, sc;
    for (sc = 0; sc < 3; sc++) {
        float scale = sc == 0 ? 1.0f : (sc == 1 ? 0.8f : 0.62f);
        for (cand = 0; cand < 4; cand++) {
            if (!site_roll_at(tx, tz, cand, scale, s, &cx, &cz, &R, &Rc)) continue;
            float sum = 0.0f, lo = 1e9f, hi = -1e9f;
            int i, n = 0, wet = 0;
            for (i = 0; i <= 16; i++) {
                float px = cx, pz = cz;
                if (i > 0) {
                    float a = (float)i * (2.0f * W_PI / 16.0f);
                    px = cx + w_cos(a) * Rc * 0.85f; pz = cz + w_sin(a) * Rc * 0.85f;
                }
                float h = site_raw_ground(px, pz);
                if (h < 0.4f) wet++;
                float ns = sum + h;
                sum = ns; n++;
                if (h < lo) lo = h;
                if (h > hi) hi = h;
            }
            float score = (float)wet * 12.0f + (hi - lo) + (1.0f - scale) * 20.0f;
            if (wet <= 4 && hi - lo <= 45.0f && score < bestScore) {
                bestScore = score; bcx = cx; bcz = cz; bR = R; bRc = Rc;
                bBase = sum / (float)n; bRange = hi - lo;
                bCore = s->core; bBand = s->band; bRings = s->rings;
            }
        }
    }
    if (bestScore >= 1e29f) { s->valid = 0; return; }
    s->valid = 1;
    s->cx = bcx; s->cz = bcz; s->R = bR; s->Rc = bRc;
    s->core = bCore; s->band = bBand; s->rings = bRings;
    s->base = w_clampf(bBase, 1.5f, 18.0f);
    s->blend = SITE_BLEND + (bRange > 10.0f ? bRange - 10.0f : 0.0f);
    cx = bcx; cz = bcz;
    uint32_t h = w_hash2(tx, tz, W_SEED_S + 5u);
    /* Gates toward neighbouring sites (roads to other cities). */
    int dx, dz;
    for (dz = -1; dz <= 1; dz++) {
        for (dx = -1; dx <= 1; dx++) {
            float ncx, ncz, nR, nRc;
            if (dx == 0 && dz == 0) continue;
            if (!site_roll(tx + dx, tz + dz, &ncx, &ncz, &nR, &nRc)) continue;
            site_add_gate(s, site_snap_gate(s, approx_atan2_w(ncz - cz, ncx - cx)));
        }
    }
    if (s->ngates == 0) {
        float a = (float)((h >> 2) & 255u) / 256.0f * 2.0f * W_PI;
        site_add_gate(s, site_snap_gate(s, a));
        site_add_gate(s, site_snap_gate(s, a + W_PI));
    }
}

/* ---- generator-thread cache + blended height ---- */
/* Indexed by the low 5 bits of each tile coordinate, so tiles within a
 * 32x32-tile region (7680 units) never evict each other -- a hashed
 * 64-entry cache let two of the 9 tiles w_height() checks collide and
 * recompute a whole site (hundreds of terrain samples) on every call. */
#define SITE_CACHE 1024
static WSite g_site_cache[SITE_CACHE];
static int g_site_cache_used[SITE_CACHE];

static WSite *w_site_cached(int tx, int tz) {
    int slot = (tx & 31) * 32 + (tz & 31);
    WSite *s = &g_site_cache[slot];
    if (g_site_cache_used[slot] && s->tx == tx && s->tz == tz) return s;
    w_site_compute(tx, tz, s);
    g_site_cache_used[slot] = 1;
    return s;
}

/* The (at most one) site whose clearing/blend reaches (x,z), or 0. */
static WSite *w_site_near(float x, float z, float extra) {
    int tx = w_ifloor(x / SITE_TILE), tz = w_ifloor(z / SITE_TILE);
    int dx, dz;
    for (dz = -1; dz <= 1; dz++) {
        for (dx = -1; dx <= 1; dx++) {
            WSite *s = w_site_cached(tx + dx, tz + dz);
            if (!s->valid) continue;
            float ex = x - s->cx, ez = z - s->cz, r = s->Rc + extra;
            if (ex * ex + ez * ez < r * r) return s;
        }
    }
    return 0;
}

/* Terrain height: natural terrain with each castle's plateau blended in. */
static float w_height(float x, float z) {
    float h = w_height_raw(x, z);
    WSite *s = w_site_near(x, z, SITE_BLEND_MAX);
    if (!s) return h;
    float ex = x - s->cx, ez = z - s->cz;
    float d = w_sqrt(ex * ex + ez * ez);
    if (d <= s->Rc) return s->base;
    if (d >= s->Rc + s->blend) return h;
    float t = w_smooth(s->Rc, s->Rc + s->blend, d);
    return s->base + (h - s->base) * t;
}

/* Is world point inside a castle clearing (fixed for WFC)? */
static int w_in_clearing(float x, float z) {
    return w_site_near(x, z, 0.0f) != 0;
}

/* ---- gates and roads ---- */
/* Point where the ray from the centre at angle a crosses ring k. */
static void site_ring_point(const WSite *s, int k, float a, float *px, float *pz) {
    float xs[SITE_MAXV], zs[SITE_MAXV];
    int n = site_ring_verts(s, k, xs, zs), i;
    float dx = w_cos(a), dz = w_sin(a);
    float best = 1e9f;
    *px = s->cx + dx * site_ring_r(s, k); *pz = s->cz + dz * site_ring_r(s, k);
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float ax = xs[i] - s->cx, az = zs[i] - s->cz, bx = xs[j] - s->cx, bz = zs[j] - s->cz;
        float ex = bx - ax, ez = bz - az;
        float den = dx * ez - dz * ex;
        if (den < 1e-6f && den > -1e-6f) continue;
        float t = (ax * ez - az * ex) / den;      /* along the ray */
        float u = (ax * dz - az * dx) / den;      /* along the edge */
        if (t > 0.0f && u >= 0.0f && u <= 1.0f && t < best) {
            best = t;
            *px = s->cx + dx * t; *pz = s->cz + dz * t;
        }
    }
}

/* Gate g's road leaves the clearing through the edge between global
 * WFC cells T (inside, fixed) and A = T + dir (outside). Pure function of
 * the site, so every chunk agrees. Returns 0 if it can't be placed. */
static int site_gate_anchor(const WSite *s, int g, int *tgx, int *tgz, int *dir) {
    float a = s->gate_a[g];
    float dx = w_cos(a), dz = w_sin(a);
    int d;
    if (w_absf(dx) > w_absf(dz)) d = dx > 0.0f ? D_E : D_W; else d = dz > 0.0f ? D_S : D_N;
    float px = s->cx + dx * (s->Rc - 6.0f), pz = s->cz + dz * (s->Rc - 6.0f);
    int gx = w_ifloor(px / W_CELL_SIZE), gz = w_ifloor(pz / W_CELL_SIZE);
    int sx = (d == D_E) ? 1 : (d == D_W) ? -1 : 0, sz = (d == D_S) ? 1 : (d == D_N) ? -1 : 0;
    int k;
    for (k = 0; k < 8; k++) {
        float ncx = ((float)(gx + sx) + 0.5f) * W_CELL_SIZE, ncz = ((float)(gz + sz) + 0.5f) * W_CELL_SIZE;
        float ex = ncx - s->cx, ez = ncz - s->cz;
        if (ex * ex + ez * ez >= s->Rc * s->Rc) { *tgx = gx; *tgz = gz; *dir = d; return 1; }
        gx = gx + sx; gz = gz + sz;
    }
    return 0;
}

static float site_tower_r_s(float t) { return 1.0f + t * 0.55f; }

/* ---- interior layout ---- */
/* Distance from (px,pz) to the boundary of ring k, and whether the point
 * is inside it (crossing-number test, so stars work too). */
static float site_ring_dist(const WSite *s, int k, float px, float pz, int *inside) {
    float xs[SITE_MAXV], zs[SITE_MAXV];
    int n = site_ring_verts(s, k, xs, zs), i, cross = 0;
    float best = 1e9f;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float ex = xs[j] - xs[i], ez = zs[j] - zs[i];
        float len2 = ex * ex + ez * ez;
        float u = ((px - xs[i]) * ex + (pz - zs[i]) * ez) / len2;
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
        float dx = px - (xs[i] + ex * u), dz = pz - (zs[i] + ez * u);
        float d = w_sqrt(dx * dx + dz * dz);
        if (d < best) best = d;
        if ((zs[i] > pz) != (zs[j] > pz)) {
            float xint = xs[i] + (pz - zs[i]) / (zs[j] - zs[i]) * ex;
            if (px < xint) cross = 1 - cross;
        }
    }
    *inside = cross;
    return best;
}

/* Buildings into caller arrays (generator and render threads each pass
 * their own). Rows of lots run parallel to each wall edge, inside a
 * street; a lot is kept if it clears its ring's walls, stays outside the
 * next ring (or the town-centre plaza), off the radial gate streets and
 * clear of other lots. Returns the count. */
static int site_layout(const WSite *s, int *bt, float *bx, float *bz, float *byaw, float *bhx, float *bhz) {
    int n = 0;
    float ga = s->gate_a[0];
    float xs[SITE_MAXV], zs[SITE_MAXV];
    /* Town centre at the middle, facing the main gate. */
    bt[n] = BT_TOWNCENTER; bx[n] = s->cx; bz[n] = s->cz; byaw[n] = ga; bhx[n] = 6.0f; bhz[n] = 4.5f; n++;
    int k, barracks = 0;
    int wantBarracks = 1 + (s->R > 45.0f) + (s->R > 65.0f);
    for (k = 0; k < s->rings; k++) {
        float t = site_ring_t(s, k);
        float wallClear = t * 0.5f + 3.0f + 2.6f;       /* wall + street + half a lot */
        int nv = site_ring_verts(s, k, xs, zs), v, row;
        for (row = 0; row < 4; row++) {
            float off = wallClear + (float)row * 7.0f;
            for (v = 0; v < nv && n < SITE_MAXB - 4; v++) {
                int w2 = (v + 1) % nv;
                float ex = xs[w2] - xs[v], ez = zs[w2] - zs[v];
                float len = w_sqrt(ex * ex + ez * ez);
                if (len < 8.0f) continue;
                float ux = ex / len, uz = ez / len;
                /* inward normal */
                float nx = -uz, nz = ux;
                float mx = (xs[v] + xs[w2]) * 0.5f - s->cx, mz = (zs[v] + zs[w2]) * 0.5f - s->cz;
                if (nx * mx + nz * mz > 0.0f) { nx = -nx; nz = -nz; }
                float u;
                for (u = 4.0f; u < len - 4.0f && n < SITE_MAXB - 4; u = u + 7.5f) {
                    float px = xs[v] + ux * u + nx * off, pz = zs[v] + uz * u + nz * off;
                    int inside;
                    float dw = site_ring_dist(s, k, px, pz, &inside);
                    if (!inside || dw < wallClear - 0.3f) continue;
                    if (k + 1 < s->rings) {
                        float dn = site_ring_dist(s, k + 1, px, pz, &inside);
                        if (inside || dn < site_ring_t(s, k + 1) * 0.5f + site_tower_r_s(site_ring_t(s, k + 1)) + 3.0f + 2.6f) continue;
                    } else {
                        float dx = px - s->cx, dz = pz - s->cz;
                        if (dx * dx + dz * dz < 13.5f * 13.5f) continue;
                    }
                    int g, bad = 0;
                    for (g = 0; g < s->ngates; g++) {
                        float sx = w_cos(s->gate_a[g]), sz = w_sin(s->gate_a[g]);
                        float along = (px - s->cx) * sx + (pz - s->cz) * sz;
                        float perp = (px - s->cx) * sz - (pz - s->cz) * sx;
                        if (along > 0.0f && perp < 5.5f && perp > -5.5f) bad = 1;
                    }
                    int q;
                    for (q = 1; q < n && !bad; q++) {
                        float dx = px - bx[q], dz = pz - bz[q];
                        float need = (bt[q] == BT_BARRACKS) ? 8.0f : 6.0f;
                        if (dx * dx + dz * dz < need * need) bad = 1;
                    }
                    if (bad) continue;
                    uint32_t h = w_hash2(w_ifloor(px), w_ifloor(pz), W_SEED_S + 9u);
                    float yaw = approx_atan2_w(nz, nx);          /* front faces inward */
                    if (barracks < wantBarracks && (row == 0 || barracks == 0) && ((h & 7u) == 0u || barracks == 0)) {
                        /* barracks need a longer lot: check the space beside */
                        bt[n] = BT_BARRACKS; bhx[n] = 4.6f; bhz[n] = 2.8f;
                        bx[n] = px; bz[n] = pz; byaw[n] = yaw; n++;
                        barracks++;
                        u = u + 7.5f;
                        continue;
                    }
                    if ((h >> 4) % 9u == 0u) { bt[n] = BT_STALL; bhx[n] = 1.4f; bhz[n] = 1.1f; }
                    else { bt[n] = BT_HOUSE; bhx[n] = 2.0f + 0.8f * (float)((h >> 8) & 3u) / 3.0f; bhz[n] = 1.7f + 0.6f * (float)((h >> 12) & 3u) / 3.0f; }
                    bx[n] = px; bz[n] = pz; byaw[n] = yaw; n++;
                }
            }
        }
    }
    /* Market stalls around the town-centre plaza. */
    int m;
    for (m = 0; m < 4 && n < SITE_MAXB; m++) {
        float a = ga + 0.6f + (float)m * (W_PI * 0.5f);
        bt[n] = BT_STALL; bx[n] = s->cx + w_cos(a) * 10.0f; bz[n] = s->cz + w_sin(a) * 10.0f; byaw[n] = a + W_PI;
        bhx[n] = 1.4f; bhz[n] = 1.1f; n++;
    }
    return n;
}

/* ---- the home castle: the valid site nearest the origin ---- */
static int site_find_home(int *htx, int *htz, WSite *out) {
    int r, tx, tz;
    for (r = 0; r <= 6; r++) {
        float bestD = 1e30f;
        int found = 0;
        for (tz = -r; tz <= r; tz++) {
            for (tx = -r; tx <= r; tx++) {
                if (tx != -r && tx != r && tz != -r && tz != r) continue;
                WSite s;
                w_site_compute(tx, tz, &s);
                if (!s.valid) continue;
                /* a home needs barracks and homes for its people */
                int bt[SITE_MAXB];
                float bx[SITE_MAXB], bz[SITE_MAXB], by[SITE_MAXB], bhx[SITE_MAXB], bhz[SITE_MAXB];
                int nb = site_layout(&s, bt, bx, bz, by, bhx, bhz), q, houses = 0, barracks = 0;
                for (q = 0; q < nb; q++) { if (bt[q] == BT_HOUSE) houses++; if (bt[q] == BT_BARRACKS) barracks++; }
                if (houses < 6 || barracks < 1) continue;
                float d = s.cx * s.cx + s.cz * s.cz;
                if (d < bestD) { bestD = d; *htx = tx; *htz = tz; site_copy(out, &s); found = 1; }
            }
        }
        if (found) { out->home = 1; return 1; }
    }
    return 0;
}

#endif /* CASTLES_SITES_H */
