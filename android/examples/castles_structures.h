/* ---- castles_structures.h: fishing docks + player-built structures ----
 *
 * Fishing docks: a wooden pier running out over the water from a shore
 * cell, a net shed beside it and a path back to the road. World docks
 * are WFC tiles (K_DOCK, castles_world.h) that the road planner only
 * offers on shore cells next to a planned road; the player can also have
 * peasants build them. Fish shoals (RES_FOOD resources, no mesh -- the
 * game layer draws them animated) are scattered over coastal water.
 *
 * Player structures: houses, barracks, docks, castle walls, towers and
 * gatehouses that peasants have finished building. The render thread
 * appends each finished one to g_ps_* (fields first, then g_nps) and has
 * the chunks under it regenerated; the generator reads the list here and
 * emits meshes + navigation exactly like the castle sites': each mesh
 * piece belongs to the chunk holding its centre, navigation is
 * rasterised per cell in every chunk the structure touches. Walls carry
 * a wall-walk (layer 1) joined to towers and gatehouses, and a ladder on
 * the side facing the home castle.
 *
 * Included by castles_world.h after castles_sites_build.h. */

#ifndef CASTLES_STRUCTURES_H
#define CASTLES_STRUCTURES_H

#define ST_HOUSE     1
#define ST_BARRACKS  2
#define ST_DOCK      3
#define ST_WALL      4
#define ST_TOWER     5
#define ST_GATE      6
#define ST_KINDS     6

#define PS_WALL_T    1.8f     /* player wall thickness                    */
#define PS_WALL_H    4.5f     /* wall-walk height above the wall's base   */
#define PS_TOWER_R   2.1f
#define PS_GATE_HALF 1.6f     /* gate passage half width                  */
#define PS_GATE_TR   1.7f     /* gatehouse flank tower radius             */
#define PS_HOUSE_HX  2.4f
#define PS_HOUSE_HZ  2.0f
#define PS_BARR_HX   4.6f
#define PS_BARR_HZ   2.8f
#define DOCK_PIER    5.0f     /* pier length from the shore anchor        */

#define MAXPS 256
static int g_nps;
static int g_ps_kind[MAXPS];
static float g_ps_x[MAXPS], g_ps_z[MAXPS], g_ps_x2[MAXPS], g_ps_z2[MAXPS];
static float g_ps_y[MAXPS], g_ps_lo[MAXPS], g_ps_yaw[MAXPS];

/* World docks found in the chunk being generated (for the game layer). */
#define DOCK_OUT_MAX 8
static int g_out_ndock;
static float g_out_dock_x[DOCK_OUT_MAX], g_out_dock_z[DOCK_OUT_MAX], g_out_dock_y[DOCK_OUT_MAX], g_out_dock_yaw[DOCK_OUT_MAX];

/* Axis-aligned bounds of a structure (anchor, end point, kind). */
static void st_bounds(int kind, float x, float z, float x2, float z2, float *b) {
    float r = 3.6f;
    if (kind == ST_BARRACKS) r = 8.5f;
    else if (kind == ST_DOCK) r = DOCK_PIER + 3.5f;
    else if (kind == ST_TOWER) r = PS_TOWER_R + 0.6f;
    else if (kind == ST_GATE) r = PS_GATE_HALF + 2.0f * PS_GATE_TR + 0.8f;
    b[0] = x - r; b[1] = z - r; b[2] = x + r; b[3] = z + r;
    if (kind == ST_WALL) {
        float m = PS_WALL_T + 0.6f;
        b[0] = (x < x2 ? x : x2) - m; b[1] = (z < z2 ? z : z2) - m;
        b[2] = (x > x2 ? x : x2) + m; b[3] = (z > z2 ? z : z2) + m;
    }
}

/* ------------------------------------------------------------------ */
/* Oriented nav rasterisation                                          */
/* ------------------------------------------------------------------ */
/* Every chunk cell whose centre lies in the rectangle rc = {lx0, lz0,
 * lx1, lz1} of the frame at (x,z) turned by `a` (m_xform convention:
 * local x = (cos a, sin a), local z = (-sin a, cos a)): mode 0 blocks
 * the ground; mode 1 makes it a walkable deck rising from h0 at lz0 to
 * h1 at lz1 (set g_or_h0/h1 first -- six float parameters at most). */
static float g_or_h0, g_or_h1;
static void w_nav_orect(float x, float z, float a, const float *rc, int mode) {
    float lx0 = rc[0], lz0 = rc[1], lx1 = rc[2], lz1 = rc[3];
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    float c = w_cos(a), sn = w_sin(a);
    float r = w_sqrt((w_absf(lx0) > w_absf(lx1) ? lx0 * lx0 : lx1 * lx1) + (w_absf(lz0) > w_absf(lz1) ? lz0 * lz0 : lz1 * lz1)) + 1.0f;
    int i0 = w_nav_lo(x - r, ox), i1 = w_nav_hi(x + r, ox), j0 = w_nav_lo(z - r, oz), j1 = w_nav_hi(z + r, oz);
    int i, j;
    for (j = j0; j <= j1; j++) {
        for (i = i0; i <= i1; i++) {
            float px = ox + (float)i + 0.5f - x, pz = oz + (float)j + 0.5f - z;
            float lx = px * c + pz * sn, lz = -px * sn + pz * c;
            if (lx < lx0 || lx > lx1 || lz < lz0 || lz > lz1) continue;
            int n = j * NAV_C + i;
            if (mode == 0) { g_nav_f[n] = 0; continue; }
            float t = (lz - lz0) / (lz1 - lz0);
            g_nav_h[n] = g_or_h0 + (g_or_h1 - g_or_h0) * t;
            g_nav_f[n] = NAV_WALK;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Fishing dock                                                        */
/* ------------------------------------------------------------------ */
/* Anchor (x,z) on the shore at ground height y; the pier runs from it
 * toward yaw (the water) for DOCK_PIER, the shed stands on the land
 * beside the pier's root. Mesh only from the chunk holding the anchor. */
static void w_dock(float x, float z, float y, float yaw, int mesh) {
    float a = yaw - 0.5f * W_PI;                   /* local +z = toward the water */
    float deck1 = W_SEA_LEVEL + 0.45f;
    float deck0 = y > deck1 ? y : deck1;
    /* navigation */
    float rc[4];
    g_or_h0 = deck0; g_or_h1 = deck1;
    rc[0] = -0.85f; rc[1] = -0.2f; rc[2] = 0.85f; rc[3] = DOCK_PIER + 0.2f;
    w_nav_orect(x, z, a, rc, 1);
    rc[0] = 0.9f; rc[1] = -2.4f; rc[2] = 2.9f; rc[3] = -0.4f;
    w_nav_orect(x, z, a, rc, 0);                                     /* shed */
    if (!mesh || !w_in_chunk(x, z)) return;
    m_xform(x, z, a);
    /* path from the road to the pier */
    m_color(0.62f, 0.52f, 0.36f);
    m_xbox(-0.8f, y - 0.3f, -3.8f, 0.8f, y + 0.06f, -0.1f);
    /* pier: deck planks in sloped steps, posts down into the water */
    int k;
    for (k = 0; k < 5; k++) {
        float z0 = DOCK_PIER * (float)k / 5.0f, z1 = DOCK_PIER * (float)(k + 1) / 5.0f;
        float hm = deck0 + (deck1 - deck0) * ((float)k + 0.5f) / 5.0f;
        m_color(0.55f + 0.04f * (float)(k & 1), 0.40f, 0.24f);
        m_xbox(-0.85f, hm - 0.14f, z0, 0.85f, hm + 0.04f, z1 + 0.02f);
    }
    m_color(0.36f, 0.25f, 0.15f);
    for (k = 1; k <= 4; k++) {
        float pz = DOCK_PIER * (float)k / 4.0f - 0.1f;
        float hp = deck0 + (deck1 - deck0) * pz / DOCK_PIER;
        m_xbox(-0.9f, W_SEA_LEVEL - 2.0f, pz - 0.12f, -0.66f, hp + 0.45f, pz + 0.12f);
        m_xbox(0.66f, W_SEA_LEVEL - 2.0f, pz - 0.12f, 0.9f, hp + 0.45f, pz + 0.12f);
    }
    /* mooring bollard + lantern post at the end, barrels, a coil of rope */
    m_color(0.25f, 0.20f, 0.16f);
    m_xbox(-0.6f, deck1, DOCK_PIER - 0.5f, -0.3f, deck1 + 0.5f, DOCK_PIER - 0.2f);
    m_xbox(0.55f, deck1, DOCK_PIER - 0.35f, 0.7f, deck1 + 2.2f, DOCK_PIER - 0.2f);
    m_color(1.0f, 0.85f, 0.45f);
    m_xbox(0.45f, deck1 + 2.2f, DOCK_PIER - 0.45f, 0.8f, deck1 + 2.55f, DOCK_PIER - 0.1f);
    m_color(0.50f, 0.33f, 0.18f);
    m_xbox(-0.7f, deck0 + 0.04f, 0.6f, -0.2f, deck0 + 0.75f, 1.1f);
    m_xbox(-0.7f, deck0 + 0.04f, 1.25f, -0.2f, deck0 + 0.75f, 1.75f);
    m_color(0.78f, 0.70f, 0.52f);
    m_xbox(0.2f, deck1 + 0.04f, DOCK_PIER - 1.6f, 0.65f, deck1 + 0.2f, DOCK_PIER - 1.15f);
    /* net shed: plank walls, slate gable, a net hung out to dry */
    m_xform(x + w_cos(a) * 1.9f - w_sin(a) * -1.4f, z + w_sin(a) * 1.9f + w_cos(a) * -1.4f, a);
    m_color(0.50f, 0.36f, 0.22f);
    m_xbox(-1.0f, y - 0.4f, -1.0f, 1.0f, y + 1.9f, 1.0f);
    m_color(0.30f, 0.33f, 0.38f);
    m_xgable(1.2f, 1.2f, y + 1.9f, y + 2.9f);
    m_color(0.22f, 0.15f, 0.09f);
    m_xbox(-1.06f, y, -0.4f, -1.0f, y + 1.5f, 0.4f);
    m_color(0.36f, 0.25f, 0.15f);
    m_xbox(1.3f, y - 0.2f, -0.9f, 1.42f, y + 1.8f, -0.78f);
    m_xbox(1.3f, y - 0.2f, 0.78f, 1.42f, y + 1.8f, 0.9f);
    m_xbox(1.3f, y + 1.7f, -0.9f, 1.42f, y + 1.82f, 0.9f);
    m_color(0.82f, 0.80f, 0.70f);
    m_xbox(1.34f, y + 0.6f, -0.78f, 1.38f, y + 1.7f, 0.78f);
}

/* ------------------------------------------------------------------ */
/* Player structures                                                   */
/* ------------------------------------------------------------------ */
#define CB_SCRATCH (CS_MAX * SITE_MAXB)   /* spare g_cb_* entry for the building meshes */

/* Distance from (px,pz) to segment (ax,az)-(bx,bz). */
static float st_seg_dist(float px, float pz, float ax, float az, float bx, float bz) {
    float ex = bx - ax, ez = bz - az;
    float l2 = ex * ex + ez * ez;
    float u = l2 > 1e-6f ? ((px - ax) * ex + (pz - az) * ez) / l2 : 0.0f;
    u = w_clampf(u, 0.0f, 1.0f);
    float dx = px - (ax + ex * u), dz = pz - (az + ez * u);
    return w_sqrt(dx * dx + dz * dz);
}

/* A tower: ground blocked, walkable top (joins wall-walks at the same
 * height). */
static void w_ps_tower_nav(float x, float z, float r, float top) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int i0 = w_nav_lo(x - r, ox), i1 = w_nav_hi(x + r, ox), j0 = w_nav_lo(z - r, oz), j1 = w_nav_hi(z + r, oz);
    int i, j;
    for (j = j0; j <= j1; j++) {
        for (i = i0; i <= i1; i++) {
            float dx = ox + (float)i + 0.5f - x, dz = oz + (float)j + 0.5f - z;
            float d = w_sqrt(dx * dx + dz * dz);
            if (d <= r) w_nav_wall(j * NAV_C + i, top, d < r - 0.45f);
        }
    }
}

/* Ladder from the ground up onto a player wall at its midpoint, on the
 * side facing (fx,fz). Same 4-adjacent top/foot scan as the castle
 * sites' ladders (castles_sites_build.h). */
static void w_ps_wall_ladder(float ax, float az, float bx, float bz, float fx, float fz) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    float mx = (ax + bx) * 0.5f, mz = (az + bz) * 0.5f;
    if (!w_in_chunk(mx, mz) || g_nlad >= MAX_LADDERS) return;
    float ex = bx - ax, ez = bz - az;
    float len = w_sqrt(ex * ex + ez * ez);
    if (len < 4.0f) return;
    float nx = -ez / len, nz = ex / len;
    if ((fx - mx) * nx + (fz - mz) * nz < 0.0f) { nx = -nx; nz = -nz; }   /* toward (fx,fz) */
    int d;
    if (w_absf(nx) > w_absf(nz)) d = nx > 0.0f ? D_E : D_W; else d = nz > 0.0f ? D_S : D_N;
    int ci = w_ifloor(mx - ox), cj = w_ifloor(mz - oz), step, top = -1, foot = -1;
    for (step = 0; step < 6; step++) {
        if (ci < 0 || cj < 0 || ci >= NAV_C || cj >= NAV_C) break;
        int n = cj * NAV_C + ci;
        if ((g_nav_f[n] & NAV_WALK) && !(g_nav_f[NAV_N + n] & NAV_WALK)) { foot = n; break; }
        if (g_nav_f[NAV_N + n] & NAV_WALK) top = n;
        if (d == D_E) ci++; else if (d == D_W) ci--; else if (d == D_S) cj++; else cj--;
    }
    if (top < 0 || foot < 0) return;
    int tdx = (foot % NAV_C) - (top % NAV_C), tdz = (foot / NAV_C) - (top / NAV_C);
    if (tdx * tdx + tdz * tdz != 1) return;
    /* the ladder points from the foot back toward the wall: dir to top */
    int dTop = (d + 2) & 3;
    float topH = g_nav_h[NAV_N + top], footH = g_nav_h[foot];
    g_nav_f[foot] = (unsigned char)(NAV_WALK | NAV_TERRAIN | NAV_LADDER | (dTop << 2));
    g_nav_f[NAV_N + top] = (unsigned char)(NAV_WALK | NAV_LADDER | (d << 2));
    float fxw = ox + (float)(foot % NAV_C) + 0.5f, fzw = oz + (float)(foot / NAV_C) + 0.5f;
    float txw = ox + (float)(top % NAV_C) + 0.5f, tzw = oz + (float)(top / NAV_C) + 0.5f;
    float faceX = fxw, faceZ = fzw;
    if (d == D_E || d == D_W) faceX = txw + (fxw - txw) * 0.45f; else faceZ = tzw + (fzw - tzw) * 0.45f;
    g_lad_fx[g_nlad] = fxw + (fxw - txw) * 0.05f; g_lad_fz[g_nlad] = fzw + (fzw - tzw) * 0.05f; g_lad_fy[g_nlad] = footH - 0.1f;
    g_lad_tx[g_nlad] = faceX; g_lad_tz[g_nlad] = faceZ; g_lad_ty[g_nlad] = topH + 0.7f;
    g_lad_alongx[g_nlad] = (d == D_N || d == D_S);
    g_nlad = g_nlad + 1;
}

static void w_ps_wall(int p) {
    float ax = g_ps_x[p], az = g_ps_z[p], bx = g_ps_x2[p], bz = g_ps_z2[p];
    float top = g_ps_y[p] + PS_WALL_H, t = PS_WALL_T;
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    float bb[4];
    st_bounds(ST_WALL, ax, az, bx, bz, bb);
    int i0 = w_nav_lo(bb[0], ox), i1 = w_nav_hi(bb[2], ox), j0 = w_nav_lo(bb[1], oz), j1 = w_nav_hi(bb[3], oz);
    int i, j;
    for (j = j0; j <= j1; j++) {
        for (i = i0; i <= i1; i++) {
            float d = st_seg_dist(ox + (float)i + 0.5f, oz + (float)j + 0.5f, ax, az, bx, bz);
            if (d <= t * 0.5f + 0.25f) w_nav_wall(j * NAV_C + i, top, d < t * 0.5f - 0.05f);
        }
    }
    float ex = bx - ax, ez = bz - az;
    float len = w_sqrt(ex * ex + ez * ez);
    int pieces = (int)(len / 4.0f) + 1, k;
    for (k = 0; k < pieces; k++) {
        float u0 = (float)k / (float)pieces, u1 = (float)(k + 1) / (float)pieces;
        float mx = ax + ex * (u0 + u1) * 0.5f, mz = az + ez * (u0 + u1) * 0.5f;
        if (!w_in_chunk(mx, mz)) continue;
        float pl = len * (u1 - u0);
        m_xform(mx, mz, approx_atan2_w(ez, ex));
        m_color(0.66f, 0.64f, 0.59f);
        m_xbox(-pl * 0.5f - 0.05f, g_ps_lo[p] - 1.0f, -t * 0.5f, pl * 0.5f + 0.05f, top, t * 0.5f);
        w_merlon_row(pl, t, 1.0f, top);
        w_merlon_row(pl, t, -1.0f, top);
    }
    /* round end caps so walls meet each other / towers cleanly */
    if (w_in_chunk(ax, az)) { m_color(0.66f, 0.64f, 0.59f); m_prism(ax, az, t * 0.5f, g_ps_lo[p] - 1.0f, top, 8); }
    if (w_in_chunk(bx, bz)) { m_color(0.66f, 0.64f, 0.59f); m_prism(bx, bz, t * 0.5f, g_ps_lo[p] - 1.0f, top, 8); }
}

static void w_ps_gate(int p) {
    float x = g_ps_x[p], z = g_ps_z[p], base = g_ps_y[p], yaw = g_ps_yaw[p];
    float top = base + PS_WALL_H;
    float ax = -w_sin(yaw), az = w_cos(yaw);           /* across the passage */
    float off = PS_GATE_HALF + PS_GATE_TR;
    int sgn;
    for (sgn = -1; sgn <= 1; sgn = sgn + 2) {
        float tx = x + ax * off * (float)sgn, tz = z + az * off * (float)sgn;
        w_ps_tower_nav(tx, tz, PS_GATE_TR, top);
        w_tower_any(tx, tz, g_ps_lo[p], PS_GATE_TR, top + 0.6f);
    }
    /* walkway over the arch joins the two tower tops */
    g_or_h0 = top; g_or_h1 = top;
    {
        float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
        int i0 = w_nav_lo(x - 3.0f, ox), i1 = w_nav_hi(x + 3.0f, ox), j0 = w_nav_lo(z - 3.0f, oz), j1 = w_nav_hi(z + 3.0f, oz);
        int i, j;
        for (j = j0; j <= j1; j++) {
            for (i = i0; i <= i1; i++) {
                float px = ox + (float)i + 0.5f - x, pz = oz + (float)j + 0.5f - z;
                float la = px * ax + pz * az, lp = px * w_cos(yaw) + pz * w_sin(yaw);
                if (w_absf(la) <= PS_GATE_HALF + 0.6f && w_absf(lp) <= 0.8f) {
                    int n = NAV_N + j * NAV_C + i;
                    g_nav_h[n] = top; g_nav_f[n] = NAV_WALK;
                }
            }
        }
    }
    if (!w_in_chunk(x, z)) return;
    m_xform(x, z, yaw + 0.5f * W_PI);                  /* local x across the passage */
    float t = PS_WALL_T + 0.4f;
    m_color(0.60f, 0.58f, 0.53f);
    m_xbox(-PS_GATE_HALF - 0.2f, base + 3.3f, -t * 0.5f, PS_GATE_HALF + 0.2f, top, t * 0.5f);
    w_merlon_row(2.0f * PS_GATE_HALF + 0.4f, t, 1.0f, top);
    w_merlon_row(2.0f * PS_GATE_HALF + 0.4f, t, -1.0f, top);
    m_color(0.18f, 0.14f, 0.10f);
    m_xbox(-PS_GATE_HALF, base + 3.1f, -t * 0.5f - 0.05f, PS_GATE_HALF, base + 3.3f, t * 0.5f + 0.05f);
    m_color(0.42f, 0.27f, 0.13f);           /* doors swung open against the towers */
    m_xbox(-PS_GATE_HALF - 0.05f, base - 0.1f, -t * 0.5f, -PS_GATE_HALF + 0.3f, base + 3.1f, t * 0.5f);
    m_xbox(PS_GATE_HALF - 0.3f, base - 0.1f, -t * 0.5f, PS_GATE_HALF + 0.05f, base + 3.1f, t * 0.5f);
    m_color(0.16f, 0.36f, 0.86f);
    m_xbox(-0.5f, base + 3.4f, t * 0.5f + 0.02f, 0.5f, top - 0.4f, t * 0.5f + 0.09f);
    m_xbox(-0.5f, base + 3.4f, -t * 0.5f - 0.09f, 0.5f, top - 0.4f, -t * 0.5f - 0.02f);
}

/* Building (house / barracks) through the castle-site mesh code, using
 * the spare g_cb_* entry; its ground is blocked like a site building's. */
static void w_ps_building(int p) {
    int q = CB_SCRATCH, kind = g_ps_kind[p];
    float hx = kind == ST_HOUSE ? PS_HOUSE_HX : PS_BARR_HX, hz = kind == ST_HOUSE ? PS_HOUSE_HZ : PS_BARR_HZ;
    float rc[4];
    rc[0] = -hx - 0.43f; rc[1] = -hz - 0.43f; rc[2] = hx + 0.43f; rc[3] = hz + 0.43f;
    w_nav_orect(g_ps_x[p], g_ps_z[p], g_ps_yaw[p] - 0.5f * W_PI, rc, 0);
    if (!w_in_chunk(g_ps_x[p], g_ps_z[p])) return;
    g_cb_t[q] = kind == ST_HOUSE ? BT_HOUSE : BT_BARRACKS;
    g_cb_x[q] = g_ps_x[p]; g_cb_z[q] = g_ps_z[p]; g_cb_yaw[q] = g_ps_yaw[p];
    g_cb_hx[q] = hx; g_cb_hz[q] = hz;
    g_team_r = 0.16f; g_team_g = 0.36f; g_team_b = 0.86f;
    /* a stone footing so the building sits on sloping ground */
    m_xform(g_ps_x[p], g_ps_z[p], g_ps_yaw[p] - 0.5f * W_PI);
    m_color(0.50f, 0.48f, 0.44f);
    m_xbox(-hx - 0.15f, g_ps_lo[p] - 0.6f, -hz - 0.15f, hx + 0.15f, g_ps_y[p] + 0.05f, hz + 0.15f);
    if (kind == ST_HOUSE) w_house_any(q, g_ps_y[p]);
    else w_barracks_any(q, g_ps_y[p]);
}

/* Emits every finished player structure touching this chunk. */
static void w_player_structures(void) {
    float ox = w_chunk_origin_x(), oz = w_chunk_origin_z();
    int n = g_nps, p;
    for (p = 0; p < n; p++) {
        float bb[4];
        int kind = g_ps_kind[p];
        st_bounds(kind, g_ps_x[p], g_ps_z[p], g_ps_x2[p], g_ps_z2[p], bb);
        if (bb[2] < ox - 1.0f || bb[0] > ox + W_CHUNK_SIZE + 1.0f || bb[3] < oz - 1.0f || bb[1] > oz + W_CHUNK_SIZE + 1.0f) continue;
        if (kind == ST_WALL) w_ps_wall(p);
        else if (kind == ST_TOWER) {
            w_ps_tower_nav(g_ps_x[p], g_ps_z[p], PS_TOWER_R, g_ps_y[p] + PS_WALL_H);
            w_tower_any(g_ps_x[p], g_ps_z[p], g_ps_lo[p], PS_TOWER_R, g_ps_y[p] + PS_WALL_H);
        }
        else if (kind == ST_GATE) w_ps_gate(p);
        else if (kind == ST_DOCK) w_dock(g_ps_x[p], g_ps_z[p], g_ps_y[p], g_ps_yaw[p], 1);
        else w_ps_building(p);
    }
    /* wall ladders last: they need every wall's nav in place */
    for (p = 0; p < n; p++) {
        if (g_ps_kind[p] != ST_WALL) continue;
        float fx = g_ps_x[p], fz = g_ps_z[p] + 1.0f;
        if (g_home_known) { WSite *hs = w_site_cached(g_home_tx, g_home_tz); fx = hs->cx; fz = hs->cz; }
        w_ps_wall_ladder(g_ps_x[p], g_ps_z[p], g_ps_x2[p], g_ps_z2[p], fx, fz);
    }
}

#endif /* CASTLES_STRUCTURES_H */
