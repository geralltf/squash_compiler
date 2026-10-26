/* ---- castles_dragons_draw.h: dragon meshes ----
 *
 * A jointed box model animated from the dragon's state: neck and head
 * chain, a tail that sways side to side, two-part wings that beat in
 * flight (the outer part lagging) and fold at rest, legs that stand on a
 * perch and tuck up in the air, a jaw that opens to breathe a cone of
 * flame, banking into turns, and a shadow on the ground beneath.
 * Included by castles_units.h with the other unit meshes. */

#ifndef CASTLES_DRAGONS_DRAW_H
#define CASTLES_DRAGONS_DRAW_H

/* o = M * Ry(a) (a sideways bend). */
static void dg_yaw(float *o, const float *M, float a) {
    float r[12];
    float c = w_cos(a), sn = w_sin(a);
    r[0] = c; r[1] = 0.0f; r[2] = -sn;
    r[3] = 0.0f; r[4] = 1.0f; r[5] = 0.0f;
    r[6] = sn; r[7] = 0.0f; r[8] = c;
    r[9] = 0.0f; r[10] = 0.0f; r[11] = 0.0f;
    m34_mul(o, M, r);
}

static void dg_point(const float *M, float x, float y, float z, float *p) {
    p[0] = M[0] * x + M[3] * y + M[6] * z + M[9];
    p[1] = M[1] * x + M[4] * y + M[7] * z + M[10];
    p[2] = M[2] * x + M[5] * y + M[8] * z + M[11];
}

static void dragon_emit(int i, int lod) {
    float s = u_scale[i];
    int perched = u_dstate[i] == DG_PERCH && u_state[i] != US_DEAD;
    int dead = u_state[i] == US_DEAD;
    float ph = u_phase[i];
    float body[3], belly[3], wing[3];
    body[0] = u_tunic[i * 3 + 0]; body[1] = u_tunic[i * 3 + 1]; body[2] = u_tunic[i * 3 + 2];
    belly[0] = u_hair[i * 3 + 0]; belly[1] = u_hair[i * 3 + 1]; belly[2] = u_hair[i * 3 + 2];
    if (u_hit[i] > 0.0f) { body[0] = 1.0f; body[1] = 0.85f; body[2] = 0.8f; }
    /* membranes: a redder, paler shade of the body, so they read apart from it */
    wing[0] = body[0] * 0.9f + 0.16f; wing[1] = body[1] * 0.75f + 0.05f; wing[2] = body[2] * 0.8f + 0.06f;
    float gy = dragon_ground(u_x[i], u_z[i]);

    /* shadow */
    if (u_y[i] - gy > 0.6f && !dead) {
        float r = 2.6f * s * w_clampf(1.2f - (u_y[i] - gy) / 40.0f, 0.5f, 1.0f);
        u_color(0.07f, 0.07f, 0.08f);
        u_ring(u_x[i], gy + 0.06f, u_z[i], r, r);
    }
    float root[12], B[12];
    m34_root(root, i, 0.0f);
    float lift = perched ? 1.45f * s : (dead ? 0.6f * s : 0.0f);
    float pitch = u_dbreath[i] > 0.0f ? 0.18f : 0.0f;
    m34_local(B, root, 0.0f, lift, 0.0f, pitch, dead ? 0.9f : u_dbank[i]);

    /* torso, belly, spines */
    u_color3(body);
    u_box(B, 0.0f, 0.0f, 0.0f, 0.75f * s, 0.62f * s, 1.5f * s);
    u_color3(belly);
    u_box(B, 0.0f, -0.42f * s, -0.1f, 0.58f * s, 0.26f * s, 1.25f * s);
    if (lod) {
        int k;
        u_color(body[0] * 0.6f, body[1] * 0.6f, body[2] * 0.6f);
        for (k = 0; k < 5; k++) u_box(B, 0.0f, 0.68f * s, (-1.1f + 0.55f * (float)k) * s, 0.06f * s, 0.16f * s, 0.12f * s);
    }

    /* neck (S-curve up at rest, stretched in flight, low when breathing) and head */
    float N[12], P[12], H[12], J[12];
    int k;
    m34_local(N, B, 0.0f, 0.25f * s, -1.35f * s, 0.0f, 0.0f);
    for (k = 0; k < 5; k++) {
        float np;
        if (perched) np = k < 2 ? -0.45f : 0.12f;
        else if (u_dbreath[i] > 0.0f) np = 0.12f;
        else np = -0.06f + 0.07f * w_sin(ph * 0.5f + (float)k * 0.7f);
        float sway = 0.05f * w_sin(ph * 0.35f + (float)k);
        m34_local(P, N, 0.0f, 0.0f, -0.5f * s, np, 0.0f);
        dg_yaw(N, P, sway);
        u_color3(body);
        u_box(N, 0.0f, 0.0f, 0.25f * s, (0.32f - 0.025f * (float)k) * s, (0.3f - 0.025f * (float)k) * s, 0.3f * s);
        if (lod) { u_color3(belly); u_box(N, 0.0f, -0.2f * s, 0.25f * s, (0.22f - 0.02f * (float)k) * s, 0.08f * s, 0.26f * s); }
    }
    float hp2 = perched ? 0.35f : (u_dbreath[i] > 0.0f ? 0.25f : 0.1f);
    m34_local(H, N, 0.0f, 0.0f, -0.35f * s, hp2, 0.0f);
    u_color3(body);
    u_box(H, 0.0f, 0.05f * s, -0.2f * s, 0.32f * s, 0.27f * s, 0.42f * s);
    u_box(H, 0.0f, 0.0f, -0.75f * s, 0.2f * s, 0.16f * s, 0.32f * s);           /* snout */
    float jaw = u_dbreath[i] > 0.0f ? 0.6f : (u_dstate[i] == DG_ATTACK ? 0.25f : 0.06f + 0.04f * w_sin(ph));
    m34_local(J, H, 0.0f, -0.17f * s, -0.4f * s, jaw, 0.0f);
    u_color3(belly);
    u_box(J, 0.0f, 0.0f, -0.32f * s, 0.18f * s, 0.07f * s, 0.36f * s);
    if (lod) {
        float R[12];
        u_color(1.0f, 0.95f, 0.85f);                                             /* teeth */
        u_box(J, 0.0f, 0.08f * s, -0.45f * s, 0.15f * s, 0.03f * s, 0.2f * s);
        u_color3(u_face + i * 3);                                                /* glowing eyes */
        u_box(H, 0.2f * s, 0.15f * s, -0.38f * s, 0.06f * s, 0.05f * s, 0.07f * s);
        u_box(H, -0.2f * s, 0.15f * s, -0.38f * s, 0.06f * s, 0.05f * s, 0.07f * s);
        u_color(0.82f, 0.76f, 0.62f);                                            /* horns swept back */
        m34_local(R, H, 0.17f * s, 0.25f * s, 0.05f * s, 0.75f, 0.25f);
        u_box(R, 0.0f, 0.0f, 0.32f * s, 0.05f * s, 0.05f * s, 0.34f * s);
        m34_local(R, H, -0.17f * s, 0.25f * s, 0.05f * s, 0.75f, -0.25f);
        u_box(R, 0.0f, 0.0f, 0.32f * s, 0.05f * s, 0.05f * s, 0.34f * s);
    }

    /* tail: swaying side to side, tapering to a spade */
    float T[12], Q[12];
    m34_local(T, B, 0.0f, 0.1f * s, 1.4f * s, 0.0f, 0.0f);
    for (k = 0; k < 7; k++) {
        float sw = (dead ? 0.0f : 0.22f) * w_sin(ph * (perched ? 0.6f : 1.0f) - (float)k * 0.75f);
        float tp = perched ? (k < 3 ? 0.18f : -0.05f) : 0.04f * w_sin(ph * 0.5f + (float)k);
        m34_local(Q, T, 0.0f, 0.0f, 0.62f * s, tp, 0.0f);
        dg_yaw(T, Q, sw);
        float w = (0.34f - 0.042f * (float)k) * s;
        u_color3(body);
        u_box(T, 0.0f, 0.0f, 0.0f, w, w * 0.9f, 0.34f * s);
    }
    u_color(body[0] * 0.7f, body[1] * 0.7f, body[2] * 0.7f);
    u_box(T, 0.0f, 0.0f, 0.5f * s, 0.32f * s, 0.03f * s, 0.25f * s);

    /* wings: beat in flight (outer half lagging), fold at rest */
    int side;
    for (side = -1; side <= 1; side = side + 2) {
        float sx = (float)side, flap, fold;
        if (dead) { flap = -0.5f; fold = 0.3f; }
        else if (perched) { flap = -1.05f; fold = 2.35f; }
        else {
            float amp = u_dstate[i] == DG_HOVER || u_dstate[i] == DG_LAND ? 0.45f : 0.75f;
            flap = 0.12f + amp * w_sin(ph);
            fold = 0.3f * w_sin(ph - 0.7f) - 0.05f;
        }
        float W1[12], W2[12];
        m34_local(W1, B, sx * 0.62f * s, 0.42f * s, -0.45f * s, 0.0f, sx * flap);
        u_color3(body);
        u_box(W1, sx * 1.55f * s, 0.0f, 0.0f, 1.55f * s, 0.09f * s, 0.09f * s);          /* arm bone */
        u_color3(wing);
        u_box(W1, sx * 1.55f * s, -0.02f, 0.8f * s, 1.5f * s, 0.025f * s, 0.8f * s);     /* inner membrane */
        m34_local(W2, W1, sx * 3.1f * s, 0.0f, 0.0f, 0.0f, sx * fold);
        u_color3(body);
        u_box(W2, sx * 1.35f * s, 0.0f, 0.0f, 1.35f * s, 0.07f * s, 0.07f * s);
        if (lod) u_box(W2, sx * 0.9f * s, 0.0f, 0.55f * s, 0.05f * s, 0.05f * s, 0.6f * s);  /* finger spar */
        u_color3(wing);
        u_box(W2, sx * 1.3f * s, -0.02f, 0.62f * s, 1.3f * s, 0.022f * s, 0.62f * s);   /* outer membrane */
        u_color(0.82f, 0.76f, 0.62f);
        u_box(W2, sx * 2.75f * s, 0.0f, 0.0f, 0.1f * s, 0.06f * s, 0.06f * s);           /* claw at the tip */
    }

    /* legs: standing on a perch, tucked in flight */
    {
        int fl;
        for (fl = 0; fl < 4; fl++) {
            float lx = (fl & 1) ? 0.52f * s : -0.52f * s, lz = (fl & 2) ? 0.95f * s : -0.85f * s;
            float L1[12], L2[12];
            float lp = perched ? 0.0f : 1.25f;
            m34_local(L1, B, lx, -0.45f * s, lz, lp, 0.0f);
            u_color3(body);
            u_box(L1, 0.0f, -0.42f * s, 0.0f, 0.15f * s, 0.45f * s, 0.17f * s);
            m34_local(L2, L1, 0.0f, -0.85f * s, 0.0f, perched ? 0.0f : -0.8f, 0.0f);
            u_box(L2, 0.0f, -0.25f * s, -0.05f * s, 0.12f * s, 0.28f * s, 0.13f * s);
            u_color(0.82f, 0.76f, 0.62f);
            u_box(L2, 0.0f, -0.52f * s, -0.18f * s, 0.16f * s, 0.05f * s, 0.2f * s);
        }
    }

    /* fire */
    if (u_dbreath[i] > 0.0f && !dead) {
        float m[3], F[12];
        dg_point(H, 0.0f, -0.05f * s, -1.05f * s, m);
        float fx = w_sin(u_yaw[i]), fz = -w_cos(u_yaw[i]);
        float dy = -0.55f;
        float dl = w_sqrt(fx * fx + fz * fz + dy * dy);
        fx = fx / dl; fz = fz / dl; dy = dy / dl;
        int q;
        for (q = 0; q < 16; q++) {
            float t = ((float)q + w_sin(g_time * 37.0f + (float)q * 2.1f) * 0.4f + 0.5f) / 16.0f;
            float d = t * 10.0f;
            float jx = 0.45f * d * 0.18f * w_sin(g_time * 23.0f + (float)q * 1.7f);
            float jz = 0.45f * d * 0.18f * w_cos(g_time * 19.0f + (float)q * 2.3f);
            float sz = 0.18f + 0.75f * t;
            u_frame(F, m[0] + fx * d + jx, m[1] + dy * d + 0.1f * d * t, m[2] + fz * d + jz, g_time * 9.0f + (float)q);
            if (t < 0.3f) u_color(1.0f, 0.95f, 0.45f);
            else if (t < 0.7f) u_color(1.0f, 0.55f + 0.2f * w_sin(g_time * 30.0f + (float)q), 0.1f);
            else u_color(0.85f, 0.22f, 0.06f);
            u_box(F, 0.0f, 0.0f, 0.0f, sz, sz * 0.8f, sz);
        }
    }

    /* selection ring on the ground, health bar */
    if (dead) return;
    if (u_selected[i]) { u_color(0.25f, 1.0f, 0.35f); u_ring(u_x[i], gy + 0.1f, u_z[i], 3.4f * s, 0.14f); }
    if (u_selected[i] || u_hp[i] < u_maxhp[i]) {
        float frac = (float)u_hp[i] / (float)u_maxhp[i];
        float top = u_y[i] + lift + 1.8f * s;
        u_color(0.12f, 0.12f, 0.12f);
        u_bar(u_x[i], top, u_z[i], 1.1f, 0.16f);
        if (u_team[i] == TEAM_PLAYER) u_color(0.2f, 0.95f, 0.3f); else if (u_team[i] == TEAM_WILD) u_color(0.95f, 0.55f, 0.15f); else u_color(1.0f, 0.25f, 0.2f);
        float rx = w_cos(g_yaw), rz = w_sin(g_yaw), w2 = 1.08f * frac;
        u_bar(u_x[i] - rx * (1.08f - w2), top + 0.02f, u_z[i] - rz * (1.08f - w2), w2, 0.12f);
    }
}

#endif /* CASTLES_DRAGONS_DRAW_H */
