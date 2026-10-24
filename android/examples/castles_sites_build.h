/* ---- castles_sites_build.h: per-chunk castle geometry + navigation ----
 *
 * Builds whatever parts of nearby castle sites (castles_sites.h) fall in
 * the chunk being generated: nav rasterisation (wall-walks, parapets,
 * towers, bastions, gatehouses, ladders, buildings) and meshes (walls at
 * any angle, merlons, towers, bastions, gatehouses, ladders, streets,
 * gate roads, town centre, houses, barracks, market stalls).
 *
 * Ownership rule for meshes: each piece is emitted by the chunk containing
 * its centre/midpoint (pieces may overhang into neighbouring chunks).
 * Navigation is rasterised per cell against every element, so it is exact
 * and seamless regardless of which chunk draws what.
 *
 * Included by castles_world.h after the mesh and nav helpers. */

#ifndef CASTLES_SITES_BUILD_H
#define CASTLES_SITES_BUILD_H

/* Nav parapet on curtain walls: the merlon strip (drawn 0.32 deep). */
#define WALK_PARAPET 0.3f

/* Sites touching the chunk being generated. */
#define CS_MAX 4
static WSite *g_cs[CS_MAX];
static int g_ncs;
static int g_home_tx, g_home_tz, g_home_known;

static void w_collect_sites(void) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int tx0 = w_ifloor((ox - 120.0f) / SITE_TILE), tx1 = w_ifloor((ox + W_CHUNK_SIZE + 120.0f) / SITE_TILE);
    int tz0 = w_ifloor((oz - 120.0f) / SITE_TILE), tz1 = w_ifloor((oz + W_CHUNK_SIZE + 120.0f) / SITE_TILE);
    int tx, tz;
    g_ncs = 0;
    for (tz = tz0; tz <= tz1; tz++) {
        for (tx = tx0; tx <= tx1; tx++) {
            WSite *s = w_site_cached(tx, tz);
            if (!s->valid || g_ncs >= CS_MAX) continue;
            /* clearing + blend band vs chunk AABB */
            float nx = w_clampf(s->cx, ox, ox + W_CHUNK_SIZE), nz = w_clampf(s->cz, oz, oz + W_CHUNK_SIZE);
            float ex = nx - s->cx, ez = nz - s->cz, r = s->Rc + s->blend;
            if (ex * ex + ez * ez > r * r) continue;
            s->home = g_home_known && s->tx == g_home_tx && s->tz == g_home_tz;
            g_cs[g_ncs] = s;
            g_ncs = g_ncs + 1;
        }
    }
}

static int w_in_chunk(float x, float z) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    return x >= ox && z >= oz && x < ox + W_CHUNK_SIZE && z < oz + W_CHUNK_SIZE;
}

/* ------------------------------------------------------------------ */
/* Oriented mesh helpers: a "current transform" (origin + yaw) so local */
/* boxes can sit at any angle without exceeding six float parameters.  */
/* ------------------------------------------------------------------ */
static float g_xf_x, g_xf_z, g_xf_c, g_xf_s;

/* Local +x maps to (cos yaw, sin yaw) in world x/z; local +z to (-sin, cos). */
static void m_xform(float x, float z, float yaw) {
    g_xf_x = x; g_xf_z = z; g_xf_c = w_cos(yaw); g_xf_s = w_sin(yaw);
}

static void m_xpt(float *p, float lx, float y, float lz) {
    p[0] = g_xf_x + lx * g_xf_c - lz * g_xf_s;
    p[1] = y;
    p[2] = g_xf_z + lx * g_xf_s + lz * g_xf_c;
}

/* Box in local coordinates (no bottom face). */
static void m_xbox(float lx0, float y0, float lz0, float lx1, float y1, float lz1) {
    float p[24], c[3];
    m_xpt(p + 0, lx0, y0, lz0); m_xpt(p + 3, lx1, y0, lz0);
    m_xpt(p + 6, lx1, y0, lz1); m_xpt(p + 9, lx0, y0, lz1);
    m_xpt(p + 12, lx0, y1, lz0); m_xpt(p + 15, lx1, y1, lz0);
    m_xpt(p + 18, lx1, y1, lz1); m_xpt(p + 21, lx0, y1, lz1);
    m_xpt(c, (lx0 + lx1) * 0.5f, (y0 + y1) * 0.5f, (lz0 + lz1) * 0.5f);
    m_ref(c[0], c[1], c[2]);
    m_quad(p + 12, p + 15, p + 18, p + 21);
    m_quad(p + 0, p + 3, p + 15, p + 12);
    m_quad(p + 3, p + 6, p + 18, p + 15);
    m_quad(p + 6, p + 9, p + 21, p + 18);
    m_quad(p + 9, p + 0, p + 12, p + 21);
}

/* Gable roof over local rect +-hx, +-hz, ridge along local x. */
static void m_xgable(float hx, float hz, float y0, float y1) {
    float a[3], b[3], c[3], d[3], r0[3], r1[3], ctr[3];
    m_xpt(a, -hx, y0, -hz); m_xpt(b, hx, y0, -hz);
    m_xpt(c, hx, y0, hz); m_xpt(d, -hx, y0, hz);
    m_xpt(r0, -hx, y1, 0.0f); m_xpt(r1, hx, y1, 0.0f);
    m_xpt(ctr, 0.0f, y0, 0.0f);
    m_ref(ctr[0], ctr[1], ctr[2]);
    m_quad(a, b, r1, r0); m_quad(d, c, r1, r0);
    m_tri(a, d, r0); m_tri(b, c, r1);
}

/* Hip / pyramid roof over local rect. */
static void m_xhip(float hx, float hz, float y0, float y1) {
    float a[3], b[3], c[3], d[3], t[3], ctr[3];
    m_xpt(a, -hx, y0, -hz); m_xpt(b, hx, y0, -hz);
    m_xpt(c, hx, y0, hz); m_xpt(d, -hx, y0, hz);
    m_xpt(t, 0.0f, y1, 0.0f);
    m_xpt(ctr, 0.0f, y0, 0.0f);
    m_ref(ctr[0], ctr[1], ctr[2]);
    m_tri(a, b, t); m_tri(b, c, t); m_tri(c, d, t); m_tri(d, a, t);
}

/* Prism over a convex world-space polygon (top + sides). */
static void m_poly_prism(const float *xs, const float *zs, int n, float y0, float y1) {
    float cx = 0.0f, cz = 0.0f, a[3], b[3], c[3], d[3], t[3];
    int i;
    for (i = 0; i < n; i++) { float ncx = cx + xs[i], ncz = cz + zs[i]; cx = ncx; cz = ncz; }
    cx = cx / (float)n; cz = cz / (float)n;
    m_ref(cx, (y0 + y1) * 0.5f, cz);
    m_set3(t, cx, y1, cz);
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        m_set3(a, xs[i], y0, zs[i]); m_set3(b, xs[j], y0, zs[j]);
        m_set3(c, xs[j], y1, zs[j]); m_set3(d, xs[i], y1, zs[i]);
        m_quad(a, b, c, d);
        m_tri(d, c, t);
    }
}

/* Raised road strip between two world points (heights from the terrain). */
static void w_road_strip(float ax, float az, float bx, float bz, float w) {
    float dx = bx - ax, dz = bz - az;
    float len = w_sqrt(dx * dx + dz * dz);
    if (len < 0.01f) return;
    float px = -dz / len * w, pz = dx / len * w;
    float ha = w_height(ax, az) + 0.13f, hb = w_height(bx, bz) + 0.13f;
    float a[3], b[3], c[3], d[3];
    m_set3(a, ax + px, ha, az + pz); m_set3(b, bx + px, hb, bz + pz);
    m_set3(c, bx - px, hb, bz - pz); m_set3(d, ax - px, ha, az - pz);
    g_ref_up = 1;
    m_quad(a, b, c, d);
}

/* Road along a polyline segment, cut into 2-unit pieces each owned by
 * the chunk containing its midpoint. */
static void w_road_line(float ax, float az, float bx, float bz, float w) {
    float dx = bx - ax, dz = bz - az;
    float len = w_sqrt(dx * dx + dz * dz);
    int n = (int)(len / 2.0f) + 1, i;
    for (i = 0; i < n; i++) {
        float t0 = (float)i / (float)n, t1 = (float)(i + 1) / (float)n;
        float x0 = ax + dx * t0, z0 = az + dz * t0, x1 = ax + dx * t1, z1 = az + dz * t1;
        if (!w_in_chunk((x0 + x1) * 0.5f, (z0 + z1) * 0.5f)) continue;
        w_road_strip(x0, z0, x1, z1, w);
    }
}

/* ------------------------------------------------------------------ */
/* Edge helpers                                                        */
/* ------------------------------------------------------------------ */
/* Outward unit normal of edge A->B of a ring around (cx,cz). */
static void site_edge_normal(const WSite *s, const float *e, float *nx, float *nz) {
    float ex = e[2] - e[0], ez = e[3] - e[1];
    float len = w_sqrt(ex * ex + ez * ez);
    float x = ez / len, z = -ex / len;
    float mx = (e[0] + e[2]) * 0.5f - s->cx, mz = (e[1] + e[3]) * 0.5f - s->cz;
    if (x * mx + z * mz < 0.0f) { x = -x; z = -z; }
    *nx = x; *nz = z;
}

/* Gate gaps on edge e (ax,az,bx,bz) as parameter intervals; returns count. */
static int site_edge_gaps(const WSite *s, const float *e, float *g0, float *g1) {
    float ex = e[2] - e[0], ez = e[3] - e[1];
    float len = w_sqrt(ex * ex + ez * ez);
    int g, n = 0;
    for (g = 0; g < s->ngates && n < 2; g++) {
        float dx = w_cos(s->gate_a[g]), dz = w_sin(s->gate_a[g]);
        float ax = e[0] - s->cx, az = e[1] - s->cz;
        float den = dx * ez - dz * ex;
        if (den < 1e-6f && den > -1e-6f) continue;
        float t = (ax * ez - az * ex) / den;
        float u = (ax * dz - az * dx) / den;
        if (t <= 0.0f || u < 0.0f || u > 1.0f) continue;
        g0[n] = u - GATE_HALF / len; g1[n] = u + GATE_HALF / len;
        n++;
    }
    return n;
}

static float site_tower_r(float t) { return site_tower_r_s(t); }
static float site_bastion_b(const WSite *s) { return 3.5f + s->wt; }

/* Bastion polygon at outer-ring vertex i (6 points into xs/zs). */
static void site_bastion_poly(const WSite *s, const float *vx, const float *vz, int n, int i, float *xs, float *zs) {
    int ip = (i + n - 1) % n, in = (i + 1) % n;
    float V0 = vx[i], V1 = vz[i];
    float ox = V0 - s->cx, oz = V1 - s->cz;
    float ol = w_sqrt(ox * ox + oz * oz);
    ox = ox / ol; oz = oz / ol;
    float a1x = vx[ip] - V0, a1z = vz[ip] - V1, a2x = vx[in] - V0, a2z = vz[in] - V1;
    float l1 = w_sqrt(a1x * a1x + a1z * a1z), l2 = w_sqrt(a2x * a2x + a2z * a2z);
    a1x = a1x / l1; a1z = a1z / l1; a2x = a2x / l2; a2z = a2z / l2;
    float b = site_bastion_b(s);
    xs[0] = V0 + a1x * b;              zs[0] = V1 + a1z * b;
    xs[1] = xs[0] + ox * b * 0.9f;     zs[1] = zs[0] + oz * b * 0.9f;
    xs[2] = V0 + ox * b * 2.0f;        zs[2] = V1 + oz * b * 2.0f;
    xs[4] = V0 + a2x * b;              zs[4] = V1 + a2z * b;
    xs[3] = xs[4] + ox * b * 0.9f;     zs[3] = zs[4] + oz * b * 0.9f;
    xs[5] = V0 - ox * 0.6f;            zs[5] = V1 - oz * 0.6f;
}

/* Signed distance of (px,pz) inside a convex polygon (positive inside,
 * = distance to the nearest of edges 0..nedge-1, edge i running from
 * vertex i to i+1); used for bastion parapets. */
static float site_poly_inside(const float *xs, const float *zs, int n, int nedge, float px, float pz) {
    float cx = 0.0f, cz = 0.0f, best = 1e9f;
    int i;
    for (i = 0; i < n; i++) { float ncx = cx + xs[i], ncz = cz + zs[i]; cx = ncx; cz = ncz; }
    cx = cx / (float)n; cz = cz / (float)n;
    for (i = 0; i < nedge; i++) {
        int j = (i + 1) % n;
        float ex = xs[j] - xs[i], ez = zs[j] - zs[i];
        float len = w_sqrt(ex * ex + ez * ez);
        if (len < 1e-5f) continue;
        float nx = ez / len, nz = -ex / len;
        /* orient the normal toward the inside */
        if ((cx - xs[i]) * nx + (cz - zs[i]) * nz < 0.0f) { nx = -nx; nz = -nz; }
        float d = (px - xs[i]) * nx + (pz - zs[i]) * nz;
        if (d < best) best = d;
    }
    return best;
}

/* Is vertex i of ring k drawn as a bastion (outer ring of a bastion fort)? */
static int site_is_bastion(const WSite *s, int k, int i) {
    return k == 0 && s->bastion && site_corner(s, i, 1);
}

/* ------------------------------------------------------------------ */
/* Navigation                                                          */
/* ------------------------------------------------------------------ */
#define MAX_LADDERS 32
static int g_nlad;
static float g_lad_fx[MAX_LADDERS], g_lad_fz[MAX_LADDERS], g_lad_fy[MAX_LADDERS];
static float g_lad_tx[MAX_LADDERS], g_lad_tz[MAX_LADDERS], g_lad_ty[MAX_LADDERS];
static int g_lad_alongx[MAX_LADDERS];

/* Building lists for the sites of the current chunk (generator thread). */
static int g_cb_n[CS_MAX];
static int g_cb_t[CS_MAX * SITE_MAXB];
static float g_cb_x[CS_MAX * SITE_MAXB], g_cb_z[CS_MAX * SITE_MAXB], g_cb_yaw[CS_MAX * SITE_MAXB];
static float g_cb_hx[CS_MAX * SITE_MAXB], g_cb_hz[CS_MAX * SITE_MAXB];

static void w_nav_wall(int n, float h, int walkTop) {
    g_nav_f[n] = (unsigned char)(g_nav_f[n] & ~NAV_WALK);         /* ground blocked */
    if (walkTop) { g_nav_h[NAV_N + n] = h; g_nav_f[NAV_N + n] = NAV_WALK; }
}

/* Per-site, per-chunk precomputed geometry for the nav rasteriser. */
#define NAVP_MAXE (SITE_MAXV * 4)
static float g_np_e[NAVP_MAXE * 4], g_np_n[NAVP_MAXE * 2], g_np_len2[NAVP_MAXE];
static float g_np_g0[NAVP_MAXE * 2], g_np_g1[NAVP_MAXE * 2];
static int g_np_ng[NAVP_MAXE], g_np_ring[NAVP_MAXE], g_np_ne;
static float g_np_tx[NAVP_MAXE + 32], g_np_tz[NAVP_MAXE + 32], g_np_tr[NAVP_MAXE + 32], g_np_ttop[NAVP_MAXE + 32];
static int g_np_nt;
static float g_np_bx[SITE_MAXV * 6], g_np_bz[SITE_MAXV * 6];
static int g_np_nb;

static void w_site_nav_prepare(WSite *s) {
    float vx[SITE_MAXV], vz[SITE_MAXV], e[4], gg0[2], gg1[2];
    int k, v, g;
    g_np_ne = 0; g_np_nt = 0; g_np_nb = 0;
    for (k = 0; k < s->rings; k++) {
        float t = site_ring_t(s, k), top = site_ring_top(s, k);
        int nv = site_ring_verts(s, k, vx, vz);
        for (v = 0; v < nv; v++) {
            int w2 = (v + 1) % nv, q = g_np_ne;
            e[0] = vx[v]; e[1] = vz[v]; e[2] = vx[w2]; e[3] = vz[w2];
            g_np_e[q * 4 + 0] = e[0]; g_np_e[q * 4 + 1] = e[1]; g_np_e[q * 4 + 2] = e[2]; g_np_e[q * 4 + 3] = e[3];
            float nx, nz;
            site_edge_normal(s, e, &nx, &nz);
            g_np_n[q * 2 + 0] = nx; g_np_n[q * 2 + 1] = nz;
            float ex = e[2] - e[0], ez = e[3] - e[1];
            g_np_len2[q] = ex * ex + ez * ez;
            int ng = site_edge_gaps(s, e, gg0, gg1);
            g_np_ng[q] = ng;
            for (g = 0; g < ng; g++) { g_np_g0[q * 2 + g] = gg0[g]; g_np_g1[q * 2 + g] = gg1[g]; }
            g_np_ring[q] = k;
            g_np_ne = q + 1;
            if (site_is_bastion(s, k, v)) {
                site_bastion_poly(s, vx, vz, nv, v, g_np_bx + g_np_nb * 6, g_np_bz + g_np_nb * 6);
                g_np_nb = g_np_nb + 1;
            } else if (site_corner(s, v, 0)) {
                int tq = g_np_nt;
                g_np_tx[tq] = vx[v]; g_np_tz[tq] = vz[v]; g_np_tr[tq] = site_tower_r(t); g_np_ttop[tq] = top;
                g_np_nt = tq + 1;
            }
        }
        /* gatehouse towers flanking each passage */
        for (g = 0; g < s->ngates; g++) {
            float gx, gz;
            site_ring_point(s, k, s->gate_a[g], &gx, &gz);
            float ax2 = -w_sin(s->gate_a[g]), az2 = w_cos(s->gate_a[g]);
            float rt = site_tower_r(t) * 0.85f;
            int sgn;
            for (sgn = -1; sgn <= 1; sgn = sgn + 2) {
                int tq = g_np_nt;
                g_np_tx[tq] = gx + ax2 * (GATE_HALF + rt) * (float)sgn;
                g_np_tz[tq] = gz + az2 * (GATE_HALF + rt) * (float)sgn;
                g_np_tr[tq] = rt; g_np_ttop[tq] = top;
                g_np_nt = tq + 1;
            }
        }
    }
}

/* Rasterises one site into the chunk's nav grid. */
static void w_site_nav(int si) {
    WSite *s = g_cs[si];
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    float vx[SITE_MAXV], vz[SITE_MAXV], e[4], gg0[2], gg1[2];
    int i, j, k, v;
    float reach = s->R + site_bastion_b(s) * 2.2f + 2.0f;
    /* skip chunks the castle doesn't touch at all */
    {
        float nx = w_clampf(s->cx, ox, ox + W_CHUNK_SIZE), nz = w_clampf(s->cz, oz, oz + W_CHUNK_SIZE);
        float dx = nx - s->cx, dz = nz - s->cz;
        if (dx * dx + dz * dz > reach * reach) return;
    }
    w_site_nav_prepare(s);
    for (j = 0; j < NAV_C; j++) {
        for (i = 0; i < NAV_C; i++) {
            float px = ox + (float)i + 0.5f, pz = oz + (float)j + 0.5f;
            float dx = px - s->cx, dz = pz - s->cz;
            float dc2 = dx * dx + dz * dz;
            if (dc2 > reach * reach) continue;
            float dc = w_sqrt(dc2);
            int n = j * NAV_C + i, q;
            for (q = 0; q < g_np_ne; q++) {
                k = g_np_ring[q];
                float t = site_ring_t(s, k), top = site_ring_top(s, k);
                float rIn = site_ring_inner_r(s, k) - t - 1.0f;
                float rOut = site_ring_r(s, k) + t + 1.0f;
                if (dc < rIn || dc > rOut) continue;
                float e0 = g_np_e[q * 4 + 0], e1 = g_np_e[q * 4 + 1];
                float ex = g_np_e[q * 4 + 2] - e0, ez = g_np_e[q * 4 + 3] - e1;
                float u = ((px - e0) * ex + (pz - e1) * ez) / g_np_len2[q];
                if (u < 0.0f || u > 1.0f) continue;
                float sd = (px - e0) * g_np_n[q * 2] + (pz - e1) * g_np_n[q * 2 + 1];     /* + = outward */
                /* The walkable band must be at least a cell diagonal
                 * (1.41) wide or a thin wall at an angle rasterises into
                 * cells that only touch at corners, which cuts the
                 * wall-walk apart -- so it reaches 0.65 past the inner
                 * face (t + 0.35 wide in all); the outer 0.3 under the
                 * merlons stays unwalkable. */
                if (sd > t * 0.5f + 0.3f || sd < -t * 0.5f - 0.65f) continue;
                int g, inGap = 0;
                for (g = 0; g < g_np_ng[q]; g++) if (u >= g_np_g0[q * 2 + g] && u <= g_np_g1[q * 2 + g]) inGap = 1;
                int walk = sd < t * 0.5f - WALK_PARAPET;
                int body = sd <= t * 0.5f + 0.3f && sd >= -t * 0.5f - 0.15f;
                if (inGap) { if (walk) { g_nav_h[NAV_N + n] = top; g_nav_f[NAV_N + n] = NAV_WALK; } }
                else if (body) w_nav_wall(n, top, walk);
                else if (walk) { g_nav_h[NAV_N + n] = top; g_nav_f[NAV_N + n] = NAV_WALK; }
            }
            /* towers (corners + gatehouses) */
            for (q = 0; q < g_np_nt; q++) {
                float tx = px - g_np_tx[q], tz = pz - g_np_tz[q], rt = g_np_tr[q];
                if (tx > rt || tx < -rt || tz > rt || tz < -rt) continue;
                float td = w_sqrt(tx * tx + tz * tz);
                if (td <= rt) w_nav_wall(n, g_np_ttop[q], td < rt - PARAPET);
            }
            /* bastions (outer ring) */
            if (s->bastion && dc > site_ring_inner_r(s, 0) - 2.0f) {
                float top0 = site_ring_top(s, 0);
                for (q = 0; q < g_np_nb; q++) {
                    float inside = site_poly_inside(g_np_bx + q * 6, g_np_bz + q * 6, 6, 6, px, pz);
                    if (inside < 0.0f) continue;
                    /* parapet only along the four outer faces: the back
                     * edges (4-5-0) lie inside the curtain wall, and
                     * eroding them pinches a sharp star's bastion off
                     * from the wall-walk at its waist */
                    float outer = site_poly_inside(g_np_bx + q * 6, g_np_bz + q * 6, 6, 4, px, pz);
                    w_nav_wall(n, top0, outer > PARAPET);
                }
            }
            /* buildings block the ground */
            if (dc < site_ring_r(s, 0)) {
                int b, nb = g_cb_n[si];
                for (b = 0; b < nb; b++) {
                    int qb = si * SITE_MAXB + b;
                    float bxx = px - g_cb_x[qb], bzz = pz - g_cb_z[qb];
                    float lim = g_cb_hx[qb] + g_cb_hz[qb] + 1.0f;
                    if (bxx > lim || bxx < -lim || bzz > lim || bzz < -lim) continue;
                    float c = w_cos(g_cb_yaw[qb]), sn = w_sin(g_cb_yaw[qb]);
                    float lx = bxx * c + bzz * sn, lz = -bxx * sn + bzz * c;
                    /* 0.43, not a round number: lot geometry can put a
                     * footprint edge exactly on a cell centre, and the
                     * comparison then depends on the last rounding bit
                     * (squash evaluates it in double precision) */
                    if (w_absf(lx) <= g_cb_hx[qb] + 0.43f && w_absf(lz) <= g_cb_hz[qb] + 0.43f)
                        g_nav_f[n] = (unsigned char)(g_nav_f[n] & ~NAV_WALK);
                }
            }
        }
    }
    /* Ladders on the inner face, spaced by distance along each ring
     * (every ~22 units of wall, at least two per ring) so short-edged
     * rings like a circle's still get them. */
    for (k = 0; k < s->rings; k++) {
        float t = site_ring_t(s, k);
        int nv = site_ring_verts(s, k, vx, vz);
        float perim = 0.0f;
        for (v = 0; v < nv; v++) {
            int w3 = (v + 1) % nv;
            float px2 = vx[w3] - vx[v], pz2 = vz[w3] - vz[v];
            float pl = perim + w_sqrt(px2 * px2 + pz2 * pz2);
            perim = pl;
        }
        float spacing = perim / 2.0f < 22.0f ? perim / 2.0f : 22.0f;
        float acc = spacing * 0.5f;
        for (v = 0; v < nv; v++) {
            int w2 = (v + 1) % nv;
            e[0] = vx[v]; e[1] = vz[v]; e[2] = vx[w2]; e[3] = vz[w2];
            float ex = e[2] - e[0], ez = e[3] - e[1];
            float len = w_sqrt(ex * ex + ez * ez);
            float na = acc + len;
            acc = na;
            if (acc < spacing || len < 3.0f) continue;
            /* gated edge: put the ladder at the quarter point furthest
             * from the gate passage (a triangle with three gates has a
             * gate on every edge and would otherwise get no ladders) */
            int ngl = site_edge_gaps(s, e, gg0, gg1);
            float lu = len > 7.0f ? 0.33f : 0.5f;
            if (ngl > 0) {
                float gm = (gg0[0] + gg1[0]) * 0.5f;
                lu = gm > 0.5f ? 0.2f : 0.8f;
                if (ngl > 1 || len * (gm > 0.5f ? gg0[0] - 0.2f : 0.8f - gg1[0]) < 2.5f) continue;
            }
            acc = 0.0f;
            float nx, nz;
            site_edge_normal(s, e, &nx, &nz);
            float lpx = e[0] + ex * lu, lpz = e[1] + ez * lu;
            int d;
            if (w_absf(nx) > w_absf(nz)) d = nx > 0.0f ? D_W : D_E; else d = nz > 0.0f ? D_N : D_S;   /* inward */
            int ci = w_ifloor(lpx - ox), cj = w_ifloor(lpz - oz), step;
            int top = -1, foot = -1;
            for (step = 0; step < 8; step++) {
                if (ci < 0 || cj < 0 || ci >= NAV_C || cj >= NAV_C) break;
                int n = cj * NAV_C + ci;
                if ((g_nav_f[n] & NAV_WALK) && !(g_nav_f[NAV_N + n] & NAV_WALK)) { foot = n; break; }
                if (g_nav_f[NAV_N + n] & NAV_WALK) top = n;
                if (d == D_E) ci++; else if (d == D_W) ci--; else if (d == D_S) cj++; else cj--;
            }
            if (top < 0 || foot < 0 || g_nlad >= MAX_LADDERS) continue;
            /* top and foot must be 4-adjacent */
            int tdx = (foot % NAV_C) - (top % NAV_C), tdz = (foot / NAV_C) - (top / NAV_C);
            if (tdx * tdx + tdz * tdz != 1) continue;
            float topH = g_nav_h[NAV_N + top], footH = g_nav_h[foot];
            g_nav_f[foot] = (unsigned char)(NAV_WALK | NAV_TERRAIN | NAV_LADDER | (((d + 2) & 3) << 2));
            g_nav_f[NAV_N + top] = (unsigned char)(NAV_WALK | NAV_LADDER | (d << 2));
            float fxw = ox + (float)(foot % NAV_C) + 0.5f, fzw = oz + (float)(foot / NAV_C) + 0.5f;
            float txw = ox + (float)(top % NAV_C) + 0.5f, tzw = oz + (float)(top / NAV_C) + 0.5f;
            /* draw from the foot to the inner wall face */
            float faceX = fxw, faceZ = fzw;
            if (d == D_E || d == D_W) faceX = txw + (fxw - txw) * 0.45f; else faceZ = tzw + (fzw - tzw) * 0.45f;
            g_lad_fx[g_nlad] = fxw + (fxw - txw) * 0.05f; g_lad_fz[g_nlad] = fzw + (fzw - tzw) * 0.05f; g_lad_fy[g_nlad] = footH - 0.1f;
            g_lad_tx[g_nlad] = faceX; g_lad_tz[g_nlad] = faceZ; g_lad_ty[g_nlad] = topH + 0.7f;
            g_lad_alongx[g_nlad] = (d == D_N || d == D_S);
            g_nlad = g_nlad + 1;
            (void)t;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Meshes                                                              */
/* ------------------------------------------------------------------ */
static float g_team_r, g_team_g, g_team_b;

static void w_merlon_row(float len, float t, float out, float top) {
    float x;
    float zo = out * t * 0.5f, zi = out * (t * 0.5f - 0.32f);
    float z0 = zo < zi ? zo : zi, z1 = zo < zi ? zi : zo;
    m_color(0.70f, 0.68f, 0.63f);
    for (x = -len * 0.5f + 0.3f; x < len * 0.5f - 0.3f; x = x + 1.25f) {
        m_xbox(x, top, z0, x + 0.6f, top + 0.8f, z1);
    }
}

/* Curtain wall piece from (ax,az) to (bx,bz). */
static void w_wall_piece_any(const WSite *s, int k, const float *e, float nx, float nz) {
    float ex = e[2] - e[0], ez = e[3] - e[1];
    float len = w_sqrt(ex * ex + ez * ez);
    float mx = (e[0] + e[2]) * 0.5f, mz = (e[1] + e[3]) * 0.5f;
    if (!w_in_chunk(mx, mz)) return;
    float t = site_ring_t(s, k), top = site_ring_top(s, k);
    m_xform(mx, mz, approx_atan2_w(ez, ex));
    /* local +z = (-sin, cos); outer side sign */
    float out = (-g_xf_s * nx + g_xf_c * nz) > 0.0f ? 1.0f : -1.0f;
    float tone = 0.62f + 0.06f * (float)k;
    m_color(tone, tone * 0.97f, tone * 0.9f);
    m_xbox(-len * 0.5f - 0.05f, s->base - 1.5f, -t * 0.5f, len * 0.5f + 0.05f, top, t * 0.5f);
    w_merlon_row(len, t, out, top);
}

static void w_tower_any(float x, float z, float base, float r, float top) {
    if (!w_in_chunk(x, z)) return;
    m_color(0.68f, 0.66f, 0.61f);
    m_prism(x, z, r, base - 1.5f, top + 2.6f, 8);
    m_color(0.72f, 0.70f, 0.65f);
    m_prism(x, z, r + 0.3f, top + 2.6f, top + 3.2f, 8);
    m_color(0.60f, 0.22f, 0.20f);
    m_cone(x, z, r + 0.4f, top + 3.2f, top + 3.2f + r * 1.8f, 8);
    /* doorway where the wall-walk passes through */
    m_color(0.16f, 0.13f, 0.11f);
    m_prism(x, z, r + 0.02f, top, top + 1.9f, 4);
}

static void w_bastion_mesh(const WSite *s, const float *xs, const float *zs) {
    float cx = 0.0f, cz = 0.0f;
    int i;
    for (i = 0; i < 6; i++) { float ncx = cx + xs[i], ncz = cz + zs[i]; cx = ncx; cz = ncz; }
    cx = cx / 6.0f; cz = cz / 6.0f;
    if (!w_in_chunk(cx, cz)) return;
    float top = site_ring_top(s, 0);
    m_color(0.64f, 0.62f, 0.57f);
    m_poly_prism(xs, zs, 6, s->base - 1.5f, top);
    /* parapet merlons on the four outer faces */
    for (i = 0; i < 4; i++) {
        float ex = xs[i + 1] - xs[i], ez = zs[i + 1] - zs[i];
        float len = w_sqrt(ex * ex + ez * ez);
        float mx = (xs[i] + xs[i + 1]) * 0.5f, mz = (zs[i] + zs[i + 1]) * 0.5f;
        m_xform(mx, mz, approx_atan2_w(ez, ex));
        float out = (-g_xf_s * (mx - cx) + g_xf_c * (mz - cz)) > 0.0f ? 1.0f : -1.0f;
        float x;
        m_color(0.70f, 0.68f, 0.63f);
        for (x = -len * 0.5f + 0.3f; x < len * 0.5f - 0.3f; x = x + 1.25f) {
            float zo = out * 0.0f, zi = -out * 0.32f;
            m_xbox(x, top, zo < zi ? zo : zi, x + 0.6f, top + 0.8f, zo < zi ? zi : zo);
        }
    }
}

static void w_gatehouse(const WSite *s, int k, int g) {
    float gx, gz;
    site_ring_point(s, k, s->gate_a[g], &gx, &gz);
    float t = site_ring_t(s, k), top = site_ring_top(s, k);
    float ax = -w_sin(s->gate_a[g]), az = w_cos(s->gate_a[g]);     /* along the wall */
    float rt = site_tower_r(t) * 0.85f;
    int sgn;
    for (sgn = -1; sgn <= 1; sgn = sgn + 2)
        w_tower_any(gx + ax * (GATE_HALF + rt) * (float)sgn, gz + az * (GATE_HALF + rt) * (float)sgn, s->base, rt, top + 0.6f);
    if (!w_in_chunk(gx, gz)) return;
    m_xform(gx, gz, s->gate_a[g] + 0.5f * W_PI);       /* local x along the wall */
    m_color(0.60f, 0.58f, 0.53f);
    m_xbox(-GATE_HALF - 0.2f, s->base + 3.3f, -t * 0.5f - 0.3f, GATE_HALF + 0.2f, top, t * 0.5f + 0.3f);
    /* portcullis + doors */
    m_color(0.18f, 0.14f, 0.10f);
    m_xbox(-GATE_HALF, s->base + 3.1f, -t * 0.5f - 0.35f, GATE_HALF, s->base + 3.3f, t * 0.5f + 0.35f);
    m_color(0.42f, 0.27f, 0.13f);
    m_xbox(-GATE_HALF - 0.05f, s->base - 0.1f, -t * 0.5f - 0.05f, -GATE_HALF + 0.35f, s->base + 3.1f, t * 0.5f + 0.05f);
    m_xbox(GATE_HALF - 0.35f, s->base - 0.1f, -t * 0.5f - 0.05f, GATE_HALF + 0.05f, s->base + 3.1f, t * 0.5f + 0.05f);
    /* banner over the gate */
    m_color(g_team_r, g_team_g, g_team_b);
    m_xbox(-0.5f, s->base + 3.4f, t * 0.5f + 0.31f, 0.5f, top - 0.4f, t * 0.5f + 0.38f);
    m_xbox(-0.5f, s->base + 3.4f, -t * 0.5f - 0.38f, 0.5f, top - 0.4f, -t * 0.5f - 0.31f);
}

/* ---- buildings ---- */
static void w_house_any(int q, float y) {
    uint32_t h = w_hash2(w_ifloor(g_cb_x[q] * 3.0f), w_ifloor(g_cb_z[q] * 3.0f), 616u);
    float hx = g_cb_hx[q], hz = g_cb_hz[q];
    int stories = 1 + (int)(h & 1u);
    float wallH = 2.2f * (float)stories;
    m_xform(g_cb_x[q], g_cb_z[q], g_cb_yaw[q] - 0.5f * W_PI);  /* local +z faces the street (centre) */
    float tone = 0.85f + 0.12f * (float)((h >> 3) & 3u) / 3.0f;
    uint32_t pal = (h >> 6) % 4u;
    if (pal == 0u) m_color(0.92f * tone, 0.88f * tone, 0.78f * tone);
    else if (pal == 1u) m_color(0.90f * tone, 0.80f * tone, 0.62f * tone);
    else if (pal == 2u) m_color(0.86f * tone, 0.86f * tone, 0.84f * tone);
    else m_color(0.80f * tone, 0.70f * tone, 0.55f * tone);
    m_xbox(-hx, y - 0.4f, -hz, hx, y + wallH, hz);
    /* half-timbering: dark beams on the corners and at the floor line */
    m_color(0.30f, 0.20f, 0.12f);
    m_xbox(-hx - 0.06f, y, -hz - 0.06f, -hx + 0.14f, y + wallH, -hz + 0.14f);
    m_xbox(hx - 0.14f, y, -hz - 0.06f, hx + 0.06f, y + wallH, -hz + 0.14f);
    m_xbox(-hx - 0.06f, y, hz - 0.14f, -hx + 0.14f, y + wallH, hz + 0.06f);
    m_xbox(hx - 0.14f, y, hz - 0.14f, hx + 0.06f, y + wallH, hz + 0.06f);
    if (stories == 2) m_xbox(-hx - 0.06f, y + 2.1f, hz - 0.06f, hx + 0.06f, y + 2.3f, hz + 0.06f);
    /* door + windows on the street side */
    m_color(0.24f, 0.16f, 0.10f);
    m_xbox(-0.45f, y, hz - 0.02f, 0.45f, y + 1.7f, hz + 0.08f);
    m_color(0.20f, 0.25f, 0.32f);
    m_xbox(-hx + 0.5f, y + 1.0f, hz - 0.02f, -hx + 1.1f, y + 1.6f, hz + 0.08f);
    m_xbox(hx - 1.1f, y + 1.0f, hz - 0.02f, hx - 0.5f, y + 1.6f, hz + 0.08f);
    /* roof */
    uint32_t rc = (h >> 10) % 4u;
    if (rc == 0u) m_color(0.66f, 0.24f, 0.18f);
    else if (rc == 1u) m_color(0.45f, 0.28f, 0.18f);
    else if (rc == 2u) m_color(0.34f, 0.38f, 0.46f);
    else m_color(0.70f, 0.55f, 0.30f);
    m_xgable(hx + 0.3f, hz + 0.35f, y + wallH, y + wallH + 1.6f);
    /* chimney */
    m_color(0.55f, 0.40f, 0.33f);
    m_xbox(hx * 0.45f, y + wallH, -0.35f, hx * 0.45f + 0.5f, y + wallH + 2.0f, 0.15f);
}

static void w_barracks_any(int q, float y) {
    float hx = g_cb_hx[q], hz = g_cb_hz[q];
    m_xform(g_cb_x[q], g_cb_z[q], g_cb_yaw[q] - 0.5f * W_PI);
    /* long stone hall with a crenellated flat roof */
    m_color(0.52f, 0.50f, 0.47f);
    m_xbox(-hx, y - 0.4f, -hz, hx, y + 3.4f, hz);
    m_color(0.46f, 0.44f, 0.41f);
    m_xbox(-hx - 0.1f, y + 3.4f, -hz - 0.1f, hx + 0.1f, y + 3.7f, hz + 0.1f);
    float x;
    m_color(0.56f, 0.54f, 0.50f);
    for (x = -hx; x < hx - 0.2f; x = x + 1.2f) {
        m_xbox(x, y + 3.7f, hz - 0.3f, x + 0.6f, y + 4.4f, hz + 0.1f);
        m_xbox(x, y + 3.7f, -hz - 0.1f, x + 0.6f, y + 4.4f, -hz + 0.3f);
    }
    /* big door, arrow-slit windows */
    m_color(0.30f, 0.20f, 0.10f);
    m_xbox(-0.9f, y, hz - 0.02f, 0.9f, y + 2.4f, hz + 0.1f);
    m_color(0.12f, 0.12f, 0.14f);
    m_xbox(-hx + 1.0f, y + 1.2f, hz - 0.02f, -hx + 1.2f, y + 2.6f, hz + 0.1f);
    m_xbox(hx - 1.2f, y + 1.2f, hz - 0.02f, hx - 1.0f, y + 2.6f, hz + 0.1f);
    /* team banners on poles either side of the door */
    int sgn;
    for (sgn = -1; sgn <= 1; sgn = sgn + 2) {
        float bx = (float)sgn * 2.0f;
        m_color(0.35f, 0.25f, 0.15f);
        m_xbox(bx - 0.06f, y, hz + 0.6f, bx + 0.06f, y + 5.0f, hz + 0.72f);
        m_color(g_team_r, g_team_g, g_team_b);
        m_xbox(bx + 0.06f, y + 3.2f, hz + 0.62f, bx + 1.0f, y + 4.8f, hz + 0.70f);
    }
    /* training yard: fence, dummies, weapon rack */
    m_color(0.48f, 0.34f, 0.20f);
    float zf = hz + 3.6f;
    for (x = -hx; x <= hx + 0.01f; x = x + 1.5f) m_xbox(x - 0.07f, y, zf - 0.07f, x + 0.07f, y + 1.0f, zf + 0.07f);
    m_xbox(-hx, y + 0.75f, zf - 0.05f, hx, y + 0.85f, zf + 0.05f);
    for (sgn = -1; sgn <= 1; sgn = sgn + 2) {
        float dx = (float)sgn * 2.6f;
        m_color(0.42f, 0.30f, 0.18f);
        m_xbox(dx - 0.08f, y, hz + 2.2f - 0.08f, dx + 0.08f, y + 1.6f, hz + 2.2f + 0.08f);
        m_xbox(dx - 0.6f, y + 1.2f, hz + 2.2f - 0.06f, dx + 0.6f, y + 1.32f, hz + 2.2f + 0.06f);
        m_color(0.86f, 0.76f, 0.45f);
        m_xbox(dx - 0.2f, y + 1.6f, hz + 2.2f - 0.2f, dx + 0.2f, y + 2.0f, hz + 2.2f + 0.2f);
    }
    m_color(0.40f, 0.28f, 0.16f);
    m_xbox(-0.8f, y, hz + 2.9f, 0.8f, y + 1.3f, hz + 3.0f);
    m_color(0.78f, 0.80f, 0.84f);
    for (x = -0.6f; x < 0.7f; x = x + 0.3f) m_xbox(x - 0.03f, y + 0.2f, hz + 2.85f, x + 0.03f, y + 1.5f, hz + 2.9f);
}

static void w_towncenter_any(int q, float y, int home) {
    float hx = g_cb_hx[q], hz = g_cb_hz[q];
    m_xform(g_cb_x[q], g_cb_z[q], g_cb_yaw[q] - 0.5f * W_PI);
    /* plaza */
    m_color(0.62f, 0.58f, 0.52f);
    m_xbox(-hx - 4.0f, y - 0.3f, -hz - 4.0f, hx + 4.0f, y + 0.08f, hz + 5.0f);
    /* stone ground floor, timber upper floor, steep slate roof */
    m_color(0.70f, 0.67f, 0.60f);
    m_xbox(-hx, y, -hz, hx, y + 3.2f, hz);
    m_color(0.90f, 0.84f, 0.70f);
    m_xbox(-hx - 0.3f, y + 3.2f, -hz - 0.3f, hx + 0.3f, y + 6.0f, hz + 0.3f);
    m_color(0.32f, 0.22f, 0.13f);
    float x;
    for (x = -hx - 0.3f; x <= hx + 0.31f; x = x + 1.5f) {
        m_xbox(x - 0.08f, y + 3.2f, hz + 0.25f, x + 0.08f, y + 6.0f, hz + 0.4f);
        m_xbox(x - 0.08f, y + 3.2f, -hz - 0.4f, x + 0.08f, y + 6.0f, -hz - 0.25f);
    }
    m_color(0.28f, 0.30f, 0.38f);
    m_xgable(hx + 0.6f, hz + 0.7f, y + 6.0f, y + 9.5f);
    /* grand doors */
    m_color(0.36f, 0.22f, 0.10f);
    m_xbox(-1.3f, y, hz - 0.02f, 1.3f, y + 3.0f, hz + 0.12f);
    /* bell tower at the back with a spire and a gold weathervane */
    m_color(0.72f, 0.69f, 0.62f);
    m_xbox(-1.4f, y, -hz - 2.8f, 1.4f, y + 11.0f, -hz);
    m_color(0.20f, 0.18f, 0.16f);
    m_xbox(-0.7f, y + 8.4f, -hz - 2.9f, 0.7f, y + 10.0f, -hz + 0.1f);
    m_color(0.28f, 0.30f, 0.38f);
    m_xhip(1.7f, 1.7f, y + 11.0f, y + 15.5f);
    m_xform(g_cb_x[q], g_cb_z[q], g_cb_yaw[q] - 0.5f * W_PI);
    m_color(0.95f, 0.80f, 0.20f);
    {
        float p[3];
        m_xpt(p, 0.0f, 0.0f, -hz - 1.4f);
        m_box(p[0] - 0.06f, y + 15.4f, p[2] - 0.06f, p[0] + 0.06f, y + 16.8f, p[2] + 0.06f);
    }
    /* big team banner over the doors */
    if (home) m_color(0.16f, 0.36f, 0.86f); else m_color(0.80f, 0.16f, 0.14f);
    m_xbox(-0.8f, y + 3.6f, hz + 0.42f, 0.8f, y + 5.8f, hz + 0.5f);
    /* well on the plaza */
    float wp[3];
    m_xpt(wp, hx + 2.6f, 0.0f, hz + 2.8f);
    w_well(wp[0], wp[2], y);
}

static void w_stall_any(int q, float y) {
    uint32_t h = w_hash2(w_ifloor(g_cb_x[q] * 3.0f), w_ifloor(g_cb_z[q] * 3.0f), 717u);
    m_xform(g_cb_x[q], g_cb_z[q], g_cb_yaw[q]);
    m_color(0.45f, 0.32f, 0.18f);
    m_xbox(-1.2f, y, -0.9f, 1.2f, y + 0.9f, 0.9f);
    m_xbox(-1.25f, y, -1.05f, -1.1f, y + 2.2f, -0.9f);
    m_xbox(1.1f, y, -1.05f, 1.25f, y + 2.2f, -0.9f);
    uint32_t c = h % 4u;
    if (c == 0u) m_color(0.85f, 0.25f, 0.20f); else if (c == 1u) m_color(0.20f, 0.55f, 0.85f);
    else if (c == 2u) m_color(0.90f, 0.75f, 0.20f); else m_color(0.30f, 0.65f, 0.30f);
    m_xgable(1.45f, 1.25f, y + 2.0f, y + 2.6f);
    m_color(0.85f, 0.60f, 0.25f);
    m_xbox(-0.9f, y + 0.9f, -0.5f, 0.9f, y + 1.15f, 0.5f);
}

/* Emits every castle piece of site si owned by this chunk. */
static void w_site_emit(int si) {
    WSite *s = g_cs[si];
    float vx[SITE_MAXV], vz[SITE_MAXV], e[4], gg0[2], gg1[2], bxs[6], bzs[6];
    int k, v, g;
    if (s->home) { g_team_r = 0.16f; g_team_g = 0.36f; g_team_b = 0.86f; }
    else { g_team_r = 0.80f; g_team_g = 0.16f; g_team_b = 0.14f; }
    for (k = 0; k < s->rings; k++) {
        int nv = site_ring_verts(s, k, vx, vz);
        float t = site_ring_t(s, k), top = site_ring_top(s, k);
        for (v = 0; v < nv; v++) {
            int w2 = (v + 1) % nv;
            e[0] = vx[v]; e[1] = vz[v]; e[2] = vx[w2]; e[3] = vz[w2];
            float nx, nz;
            site_edge_normal(s, e, &nx, &nz);
            float ex = e[2] - e[0], ez = e[3] - e[1];
            float len = w_sqrt(ex * ex + ez * ez);
            int ng = site_edge_gaps(s, e, gg0, gg1);
            /* solid intervals between gate gaps, cut into <= 4-unit pieces */
            float iv0[3], iv1[3];
            int ni = 0;
            float cur = 0.0f;
            if (ng == 2 && gg0[1] < gg0[0]) { float tmp = gg0[0]; gg0[0] = gg0[1]; gg0[1] = tmp; tmp = gg1[0]; gg1[0] = gg1[1]; gg1[1] = tmp; }
            for (g = 0; g < ng; g++) { iv0[ni] = cur; iv1[ni] = gg0[g]; ni++; cur = gg1[g]; }
            iv0[ni] = cur; iv1[ni] = 1.0f; ni++;
            int q;
            for (q = 0; q < ni; q++) {
                float span = (iv1[q] - iv0[q]) * len;
                if (span <= 0.05f) continue;
                int pieces = (int)(span / 4.0f) + 1, p;
                for (p = 0; p < pieces; p++) {
                    float u0 = iv0[q] + (iv1[q] - iv0[q]) * (float)p / (float)pieces;
                    float u1 = iv0[q] + (iv1[q] - iv0[q]) * (float)(p + 1) / (float)pieces;
                    float pe[4];
                    pe[0] = e[0] + ex * u0; pe[1] = e[1] + ez * u0;
                    pe[2] = e[0] + ex * u1; pe[3] = e[1] + ez * u1;
                    w_wall_piece_any(s, k, pe, nx, nz);
                }
            }
            /* corner: bastion or tower */
            if (site_is_bastion(s, k, v)) {
                site_bastion_poly(s, vx, vz, nv, v, bxs, bzs);
                w_bastion_mesh(s, bxs, bzs);
            } else if (site_corner(s, v, 0)) {
                w_tower_any(vx[v], vz[v], s->base, site_tower_r(t), top);
            }
        }
        for (g = 0; g < s->ngates; g++) w_gatehouse(s, k, g);
    }
    /* Streets: each gate road runs from the clearing edge through every
     * ring's gate to the town-centre plaza. */
    for (g = 0; g < s->ngates; g++) {
        float a = s->gate_a[g], dx = w_cos(a), dz = w_sin(a);
        int tgx, tgz, dir;
        float ox2, oz2;
        site_ring_point(s, 0, a, &ox2, &oz2);
        m_color(0.62f, 0.52f, 0.36f);
        if (site_gate_anchor(s, g, &tgx, &tgz, &dir)) {
            /* to the middle of the anchor edge (WFC's road starts there) */
            float ecx = ((float)tgx + 0.5f) * W_CELL_SIZE, ecz = ((float)tgz + 0.5f) * W_CELL_SIZE;
            float ax2 = ecx, az2 = ecz;
            if (dir == D_E) ax2 = ecx + 2.0f; else if (dir == D_W) ax2 = ecx - 2.0f;
            else if (dir == D_S) az2 = ecz + 2.0f; else az2 = ecz - 2.0f;
            float qx = s->cx + dx * (s->Rc - 6.0f), qz = s->cz + dz * (s->Rc - 6.0f);
            w_road_line(ox2 + dx * 1.0f, oz2 + dz * 1.0f, qx, qz, 1.4f);
            w_road_line(qx, qz, ecx, ecz, 1.4f);
            w_road_line(ecx, ecz, ax2, az2, 0.75f);
        }
        m_color(0.58f, 0.55f, 0.50f);
        w_road_line(ox2, oz2, s->cx + dx * 7.0f, s->cz + dz * 7.0f, 1.6f);
    }
    /* Buildings */
    int b, nb = g_cb_n[si];
    for (b = 0; b < nb; b++) {
        int q = si * SITE_MAXB + b;
        if (!w_in_chunk(g_cb_x[q], g_cb_z[q])) continue;
        float y = s->base;
        if (g_cb_t[q] == BT_HOUSE) w_house_any(q, y);
        else if (g_cb_t[q] == BT_BARRACKS) w_barracks_any(q, y);
        else if (g_cb_t[q] == BT_TOWNCENTER) w_towncenter_any(q, y, s->home);
        else w_stall_any(q, y);
    }
}

/* Is a chunk-local point inside the outer ring's courtyard (for terrain
 * colouring)? */
static int w_site_court(float x, float z) {
    int i;
    for (i = 0; i < g_ncs; i++) {
        WSite *s = g_cs[i];
        float dx = x - s->cx, dz = z - s->cz;
        float r = site_ring_inner_r(s, 0) - site_ring_t(s, 0) * 0.5f;
        if (dx * dx + dz * dz < r * r) return 1;
    }
    return 0;
}

#endif /* CASTLES_SITES_BUILD_H */
