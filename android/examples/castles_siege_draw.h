/* ---- castles_siege_draw.h: siege engine meshes, structure health bars ----
 *
 * The drawing half of castles_siege.h, included by castles_units.h next
 * to the other unit meshes (it needs the u_box / m34_* helpers). */

#ifndef CASTLES_SIEGE_DRAW_H
#define CASTLES_SIEGE_DRAW_H

/* ---- meshes ---- */
static void siege_wheel(const float *M, float x, float y, float z, float r) {
    float W[12];
    m34_local(W, M, x, y, z, 0.0f, 0.0f);
    u_color(0.30f, 0.21f, 0.12f);
    u_box(W, 0.0f, 0.0f, 0.0f, 0.08f, r, r * 0.42f);
    u_box(W, 0.0f, 0.0f, 0.0f, 0.08f, r * 0.42f, r);
    u_color(0.22f, 0.22f, 0.24f);
    u_box(W, 0.0f, 0.0f, 0.0f, 0.1f, r * 0.15f, r * 0.15f);
}

static void siege_emit(int i, int lod) {
    float root[12], A[12];
    int c = u_class[i];
    m34_root(root, i, 0.0f);
    float wood0 = 0.50f, wood1 = 0.36f, wood2 = 0.20f;
    if (c == UC_CATAPULT) {
        /* frame on four wheels, A-frame, throwing arm with a cup */
        u_color(wood0, wood1, wood2);
        u_box(root, -0.55f, 0.55f, 0.0f, 0.1f, 0.1f, 1.3f);
        u_box(root, 0.55f, 0.55f, 0.0f, 0.1f, 0.1f, 1.3f);
        u_box(root, 0.0f, 0.55f, -1.1f, 0.65f, 0.08f, 0.1f);
        u_box(root, 0.0f, 0.55f, 1.1f, 0.65f, 0.08f, 0.1f);
        u_color(wood0 * 0.9f, wood1 * 0.9f, wood2 * 0.9f);
        u_box(root, -0.5f, 1.1f, -0.25f, 0.08f, 0.55f, 0.08f);
        u_box(root, 0.5f, 1.1f, -0.25f, 0.08f, 0.55f, 0.08f);
        u_box(root, 0.0f, 1.62f, -0.25f, 0.6f, 0.07f, 0.07f);
        if (lod) { siege_wheel(root, -0.72f, 0.42f, -0.85f, 0.42f); siege_wheel(root, 0.72f, 0.42f, -0.85f, 0.42f);
                   siege_wheel(root, -0.72f, 0.42f, 0.85f, 0.42f); siege_wheel(root, 0.72f, 0.42f, 0.85f, 0.42f); }
        /* arm: rests back, swings up and forward at release (0.35) */
        float tt = u_swing[i], ang;
        if (tt < 0.35f) ang = 0.55f - 0.15f * tt / 0.35f;
        else if (tt < 0.45f) ang = 0.40f - 2.3f * (tt - 0.35f) / 0.10f;
        else ang = -1.9f + 2.45f * w_clampf((tt - 0.45f) / 0.5f, 0.0f, 1.0f);
        m34_local(A, root, 0.0f, 0.7f, 0.35f, ang, 0.0f);
        u_color(wood0 * 1.05f, wood1 * 1.05f, wood2);
        u_box(A, 0.0f, 0.0f, -0.9f, 0.07f, 0.07f, 1.05f);
        u_color(0.30f, 0.22f, 0.14f);
        u_box(A, 0.0f, 0.12f, -1.9f, 0.2f, 0.1f, 0.2f);
        if (tt < 0.35f || tt > 0.8f) { u_color(0.55f, 0.55f, 0.57f); u_box(A, 0.0f, 0.3f, -1.9f, 0.15f, 0.13f, 0.15f); }
        /* torsion skein */
        u_color(0.70f, 0.62f, 0.45f);
        u_box(root, 0.0f, 0.7f, 0.35f, 0.45f, 0.12f, 0.12f);
    } else if (c == UC_RAM) {
        /* covered wagon: hide roof over a frame, the ram swinging inside */
        u_color(wood0 * 0.9f, wood1 * 0.9f, wood2);
        u_box(root, -0.6f, 0.75f, 0.0f, 0.08f, 0.55f, 1.4f);
        u_box(root, 0.6f, 0.75f, 0.0f, 0.08f, 0.55f, 1.4f);
        float R1[12], R2[12];
        m34_local(R1, root, -0.36f, 1.55f, 0.0f, 0.0f, 0.62f);
        m34_local(R2, root, 0.36f, 1.55f, 0.0f, 0.0f, -0.62f);
        u_color(0.48f, 0.34f, 0.24f);
        u_box(R1, 0.0f, 0.0f, 0.0f, 0.5f, 0.05f, 1.55f);
        u_box(R2, 0.0f, 0.0f, 0.0f, 0.5f, 0.05f, 1.55f);
        u_color(0.38f, 0.26f, 0.18f);
        u_box(root, 0.0f, 1.95f, 0.0f, 0.06f, 0.06f, 1.55f);
        float sw = w_sin(w_clampf(u_swing[i] / 0.55f, 0.0f, 1.0f) * W_PI * 0.5f);
        float zoff = u_state[i] == US_ATTACK ? (u_swing[i] < 0.55f ? 0.6f * u_swing[i] / 0.55f - 0.55f * sw : 0.55f * (1.0f - u_swing[i])) : 0.0f;
        u_color(0.42f, 0.30f, 0.17f);
        u_box(root, 0.0f, 0.95f, -0.4f + zoff - 0.6f, 0.17f, 0.17f, 1.55f);
        u_color(0.30f, 0.30f, 0.33f);
        u_box(root, 0.0f, 0.95f, -2.15f + zoff - 0.6f, 0.24f, 0.24f, 0.2f);
        if (lod) { siege_wheel(root, -0.75f, 0.4f, -0.95f, 0.4f); siege_wheel(root, 0.75f, 0.4f, -0.95f, 0.4f);
                   siege_wheel(root, -0.75f, 0.4f, 0.95f, 0.4f); siege_wheel(root, 0.75f, 0.4f, 0.95f, 0.4f); }
    } else {
        /* trebuchet: long base on wheels; set up, a tall A-frame with the
         * arm, counterweight and sling; packed, all folded flat */
        float up = 1.0f;
        int ss = u_sstate[i];
        if (ss == SG_PACKED) up = 0.0f;
        else if (ss == SG_DEPLOYING) up = w_clampf(u_st_t[i] / SG_SETUP_T, 0.0f, 1.0f);
        else if (ss == SG_PACKING) up = 1.0f - w_clampf(u_st_t[i] / SG_SETUP_T, 0.0f, 1.0f);
        u_color(wood0, wood1, wood2);
        u_box(root, -0.8f, 0.5f, 0.0f, 0.12f, 0.12f, 2.3f);
        u_box(root, 0.8f, 0.5f, 0.0f, 0.12f, 0.12f, 2.3f);
        u_box(root, 0.0f, 0.5f, -2.0f, 0.9f, 0.1f, 0.12f);
        u_box(root, 0.0f, 0.5f, 2.0f, 0.9f, 0.1f, 0.12f);
        if (lod) { siege_wheel(root, -1.0f, 0.42f, -1.6f, 0.42f); siege_wheel(root, 1.0f, 0.42f, -1.6f, 0.42f);
                   siege_wheel(root, -1.0f, 0.42f, 1.6f, 0.42f); siege_wheel(root, 1.0f, 0.42f, 1.6f, 0.42f); }
        float H = 0.8f + 4.4f * up;                      /* pivot height */
        float L1[12], L2[12];
        u_color(wood0 * 0.92f, wood1 * 0.92f, wood2 * 0.92f);
        m34_local(L1, root, -0.75f, 0.55f, 0.0f, 0.0f, 0.0f);
        m34_local(L2, root, 0.75f, 0.55f, 0.0f, 0.0f, 0.0f);
        float lean = 0.38f;
        float P1[12], P2[12], P3[12], P4[12];
        m34_local(P1, L1, 0.0f, 0.0f, -0.0f, -lean * up - (1.0f - up) * 1.45f, 0.0f);
        m34_local(P2, L1, 0.0f, 0.0f, 0.0f, lean * up + (1.0f - up) * 1.45f, 0.0f);
        m34_local(P3, L2, 0.0f, 0.0f, 0.0f, -lean * up - (1.0f - up) * 1.45f, 0.0f);
        m34_local(P4, L2, 0.0f, 0.0f, 0.0f, lean * up + (1.0f - up) * 1.45f, 0.0f);
        float leg = (H - 0.55f) / w_cos(lean) * 0.5f + 0.05f;
        u_box(P1, 0.0f, leg, 0.0f, 0.09f, leg, 0.09f);
        u_box(P2, 0.0f, leg, 0.0f, 0.09f, leg, 0.09f);
        u_box(P3, 0.0f, leg, 0.0f, 0.09f, leg, 0.09f);
        u_box(P4, 0.0f, leg, 0.0f, 0.09f, leg, 0.09f);
        u_color(0.25f, 0.22f, 0.20f);
        u_box(root, 0.0f, H, 0.0f, 0.95f, 0.08f, 0.08f);
        /* arm: long end back and down at rest, swings over forward */
        float tt = ss == SG_DEPLOYED ? u_swing[i] : 0.0f, ang;
        if (tt < 0.35f) ang = 0.95f;
        else if (tt < 0.5f) ang = 0.95f - 3.2f * (tt - 0.35f) / 0.15f;
        else ang = -2.25f + 3.2f * w_clampf((tt - 0.5f) / 0.45f, 0.0f, 1.0f);
        if (up < 1.0f) { float a2 = ang * up + 1.57f * (1.0f - up); ang = a2; }
        m34_local(A, root, 0.0f, H, 0.0f, ang, 0.0f);
        u_color(wood0 * 1.08f, wood1 * 1.05f, wood2);
        u_box(A, 0.0f, 0.0f, 1.6f, 0.11f, 0.11f, 3.0f);          /* long end (+z = back at rest) */
        u_box(A, 0.0f, 0.0f, -0.9f, 0.13f, 0.13f, 0.9f);
        u_color(0.45f, 0.44f, 0.42f);
        u_box(A, 0.0f, -0.55f, -1.7f, 0.5f, 0.5f, 0.45f);          /* counterweight box */
        u_color(0.75f, 0.68f, 0.50f);
        u_box(A, 0.0f, -0.4f, 4.5f, 0.03f, 0.4f, 0.03f);
        if (tt < 0.4f && ss == SG_DEPLOYED) { u_color(0.55f, 0.55f, 0.57f); u_box(A, 0.0f, -0.85f, 4.5f, 0.22f, 0.2f, 0.22f); }
    }
    /* selection ring, health bar */
    if (u_selected[i]) { u_color(0.25f, 1.0f, 0.35f); u_ring(u_x[i], u_y[i] + 0.08f, u_z[i], c == UC_TREB ? 2.6f : 1.8f, 0.1f); }
    if (u_selected[i] || u_hp[i] < u_maxhp[i]) {
        float frac = (float)u_hp[i] / (float)u_maxhp[i];
        float top = u_y[i] + (c == UC_TREB ? 6.5f : 2.8f);
        u_color(0.12f, 0.12f, 0.12f);
        u_bar(u_x[i], top, u_z[i], 0.7f, 0.12f);
        if (u_team[i] == TEAM_PLAYER) u_color(0.2f, 0.95f, 0.3f); else u_color(1.0f, 0.25f, 0.2f);
        float rx = w_cos(g_yaw), rz = w_sin(g_yaw), w2 = 0.68f * frac;
        u_bar(u_x[i] - rx * (0.68f - w2), top + 0.015f, u_z[i] - rz * (0.68f - w2), w2, 0.08f);
    }
}

/* Health bars over buildings near the camera (always) and damaged castle
 * pieces. */
static void structure_bars_emit(void) {
    int b, e;
    float rx = w_cos(g_yaw), rz = w_sin(g_yaw);
    for (b = 0; b < g_hb_n; b++) {
        if (g_hb_dead[b] || g_hb_maxhp[b] <= 0 || !g_hb_done[b]) continue;
        float dx = g_hb_x[b] - g_cam[0], dz = g_hb_z[b] - g_cam[2];
        if (dx * dx + dz * dz > 95.0f * 95.0f) continue;
        float frac = (float)g_hb_hp[b] / (float)g_hb_maxhp[b];
        float w = g_hb_t[b] == BT_TOWNCENTER ? 2.6f : (g_hb_t[b] == BT_HOUSE ? 1.2f : 1.8f);
        float top = g_hb_y[b] + (g_hb_t[b] == BT_TOWNCENTER ? 9.0f : (g_hb_t[b] == BT_HOUSE ? 5.6f : 5.0f));
        u_color(0.10f, 0.10f, 0.10f);
        u_bar(g_hb_x[b], top, g_hb_z[b], w + 0.06f, 0.24f);
        if (g_hb_own[b] == 0) u_color(0.25f + 0.7f * (1.0f - frac), 0.9f * frac + 0.1f, 0.25f);
        else u_color(1.0f, 0.25f + 0.45f * frac, 0.18f);
        float w2 = w * frac;
        u_bar(g_hb_x[b] - rx * (w - w2), top + 0.03f, g_hb_z[b] - rz * (w - w2), w2, 0.18f);
    }
    for (e = 0; e < g_el_n; e++) {
        if (el_dead[e] || el_hp[e] >= el_maxhp[e]) continue;
        float cx = (el_x[e] + el_x2[e]) * 0.5f, cz = (el_z[e] + el_z2[e]) * 0.5f;
        float dx = cx - g_cam[0], dz = cz - g_cam[2];
        if (dx * dx + dz * dz > 95.0f * 95.0f) continue;
        float frac = (float)el_hp[e] / (float)el_maxhp[e];
        float top = el_top[e] + 1.6f;
        u_color(0.10f, 0.10f, 0.10f);
        u_bar(cx, top, cz, 1.26f, 0.2f);
        if (el_own[e] == 0) u_color(0.3f, 0.9f, 0.3f); else u_color(1.0f, 0.3f, 0.2f);
        float w2 = 1.2f * frac;
        u_bar(cx - rx * (1.2f - w2), top + 0.03f, cz - rz * (1.2f - w2), w2, 0.14f);
    }
}

#endif /* CASTLES_SIEGE_DRAW_H */
