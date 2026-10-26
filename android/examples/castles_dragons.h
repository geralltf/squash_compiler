/* ---- castles_dragons.h: dragons ----
 *
 * Wild dragons roost on the mountain peaks around the castles, each a
 * random dark colour. Every minute or two one takes off and hunts: it
 * picks a unit near its roost (anybody's), flies at it, dives to breathe
 * fire in a cone in front of it, swoops low to bite -- snatching
 * peasants it passes over now and then -- and flies home after a while.
 * Archers can shoot dragons out of the sky; swords and maces only reach
 * one swooping low. A slain wild dragon's roost is taken by a new one
 * after a few minutes.
 *
 * Peasants can tame a roosting dragon by working at it (the player:
 * select peasants, tap the dragon; AI castles send peasants to roosts
 * near them). A tamed dragon belongs to that faction: the player can
 * select it and send it to fly somewhere, to attack a unit, or to a
 * castle where it attacks whatever is there; AI castles send theirs with
 * their attack waves.
 *
 * Dragons fly freely (no navigation grid), so they are simulated the same
 * on and off screen. Drawing: castles_dragons_draw.h. Included by
 * castles_units.h after castles_siege.h. */

#ifndef CASTLES_DRAGONS_H
#define CASTLES_DRAGONS_H

#define DG_PERCH  0
#define DG_FLY    1      /* to (u_gx, u_gz) at cruising height     */
#define DG_HOVER  2      /* circling over where it was sent        */
#define DG_ATTACK 3      /* after u_target                         */
#define DG_LAND   4      /* gliding down to (u_gx, u_gz)           */

#define DG_CRUISE   15.0f
#define DG_TAME_T   14.0f   /* peasant-seconds of work to tame one */

/* roosts of the wild dragons */
#define LAIR_MAX 4
static int g_nlair;
static float g_lair_x[LAIR_MAX], g_lair_z[LAIR_MAX], g_lair_t[LAIR_MAX];
static int g_lair_u[LAIR_MAX];

static int is_dragon(int i) { return u_class[i] == UC_DRAGON; }

static float dragon_ground(float x, float z) {
    float h = nav_terrain_height(x, z);
    return h < W_SEA_LEVEL ? W_SEA_LEVEL : h;
}

static float dg_rand(int i, int salt) {
    uint32_t h = w_hash2(i * 31 + salt, (int)(g_time * 7.0f), 0xD2A6u);
    return (float)(h & 65535u) / 65535.0f;
}

static int dragon_spawn(float x, float z, uint32_t seed, int own) {
    int team = own < 0 ? TEAM_WILD : team_of(own);
    int d = unit_spawn(team, x, z, 0, seed, UC_DRAGON, own);
    if (d < 0) return -1;
    /* a random dark body colour, a paler belly, glowing eyes */
    uint32_t h = w_hash(seed * 2654435761u + 77u);
    float hue = (float)(h & 1023u) / 1023.0f;
    float sat = 0.50f + 0.35f * (float)((h >> 10) & 255u) / 255.0f;
    float val = 0.24f + 0.18f * (float)((h >> 18) & 255u) / 255.0f;
    float c[3];
    u_hsv(c, hue, sat, val);
    u_tunic[d * 3 + 0] = c[0]; u_tunic[d * 3 + 1] = c[1]; u_tunic[d * 3 + 2] = c[2];
    u_hair[d * 3 + 0] = c[0] * 1.7f + 0.10f; u_hair[d * 3 + 1] = c[1] * 1.7f + 0.08f; u_hair[d * 3 + 2] = c[2] * 1.7f + 0.06f;
    float eh = (float)((h >> 26) & 63u) / 63.0f;
    u_face[d * 3 + 0] = 1.0f; u_face[d * 3 + 1] = 0.55f + 0.4f * eh; u_face[d * 3 + 2] = 0.1f;
    u_scale[d] = 1.0f + 0.25f * (float)((h >> 5) & 255u) / 255.0f;
    u_dstate[d] = DG_PERCH; u_dalt[d] = 0.0f; u_dhx[d] = x; u_dhz[d] = z;
    u_dt[d] = 40.0f + 80.0f * (float)((h >> 13) & 255u) / 255.0f;
    u_dbreath[d] = 0.0f; u_dcool[d] = 0.0f; u_dtame[d] = 0.0f; u_dtame_t[d] = -100.0f; u_dbank[d] = 0.0f;
    u_dvel[d] = 0.0f; u_deat[d] = 0.0f; u_dtick[d] = 0.0f;
    u_y[d] = dragon_ground(x, z);
    return d;
}

/* Roosts: the highest peaks within ~320 of the home castle, apart from
 * each other and from the castles; a wild dragon on each. */
static void dragons_init(void) {
    float px[64], pz[64], ph[64];
    int n = 0, k, i;
    g_nlair = 0;
    if (!g_home_set) return;
    float cx = g_hsite.cx, cz = g_hsite.cz, x, z;
    for (z = cz - 320.0f; z <= cz + 320.0f; z = z + 10.0f) {
        for (x = cx - 320.0f; x <= cx + 320.0f; x = x + 10.0f) {
            float h = w_height_raw(x, z);
            if (h < 18.0f) continue;
            if (w_height_raw(x + 10.0f, z) > h || w_height_raw(x - 10.0f, z) > h ||
                w_height_raw(x, z + 10.0f) > h || w_height_raw(x, z - 10.0f) > h) continue;
            if (n < 64) { px[n] = x; pz[n] = z; ph[n] = h; n++; }
        }
    }
    while (g_nlair < LAIR_MAX) {
        int best = -1;
        for (k = 0; k < n; k++) {
            if (ph[k] < 0.0f) continue;
            int ok = 1;
            for (i = 0; i < g_nlair && ok; i++) {
                float dx = px[k] - g_lair_x[i], dz = pz[k] - g_lair_z[i];
                if (dx * dx + dz * dz < 90.0f * 90.0f) ok = 0;
            }
            for (i = 0; i < F_MAX && ok; i++) {
                WSite *s = i == 0 ? &g_hsite : &f_site[i];
                if (i > 0 && !f_on[i]) continue;
                float dx = px[k] - s->cx, dz = pz[k] - s->cz;
                if (dx * dx + dz * dz < (s->Rc + 25.0f) * (s->Rc + 25.0f)) ok = 0;
            }
            if (!ok) { ph[k] = -1.0f; continue; }
            if (best < 0 || ph[k] > ph[best]) best = k;
        }
        if (best < 0) break;
        int l = g_nlair;
        g_lair_x[l] = px[best]; g_lair_z[l] = pz[best]; g_lair_t[l] = 0.0f;
        ph[best] = -1.0f;
        g_lair_u[l] = dragon_spawn(g_lair_x[l], g_lair_z[l], 4400u + (uint32_t)l * 977u, -1);
        g_nlair = l + 1;
        __android_log_print(4, "castles", "dragon roost %d on a peak at %d,%d (height %d)", l, (int)g_lair_x[l], (int)g_lair_z[l],
                            (int)w_height_raw(g_lair_x[l], g_lair_z[l]));
    }
}

/* A slain wild dragon's roost gets a new dragon after a few minutes. */
static void dragons_respawn(float dt) {
    int l;
    for (l = 0; l < g_nlair; l++) {
        int d = g_lair_u[l];
        if (d >= 0 && unit_alive(d) && u_class[d] == UC_DRAGON && u_own[d] < 0) continue;
        float nt = g_lair_t[l] + dt;
        g_lair_t[l] = nt;
        if (nt < 300.0f) continue;
        g_lair_t[l] = 0.0f;
        g_lair_u[l] = dragon_spawn(g_lair_x[l], g_lair_z[l], 5100u + (uint32_t)(g_time * 3.0f) + (uint32_t)l, -1);
        __android_log_print(4, "castles", "a new dragon has taken roost %d", l);
    }
}

/* Nearest unit dragon d may attack within r of (x,z) (wild ones: anybody). */
static int dragon_find_prey(int d, float x, float z, float r) {
    int j, best = -1;
    float bd = r * r;
    for (j = 0; j < g_un; j++) {
        if (j == d || !unit_alive(j) || u_team[j] == u_team[d] || u_class[j] == UC_BOAT) continue;
        if (u_class[j] == UC_DRAGON && u_own[j] < 0 && u_own[d] < 0) continue;     /* wild ones leave each other be */
        float dx = u_x[j] - x, dz = u_z[j] - z;
        float dd = dx * dx + dz * dz;
        if (dd < bd) { bd = dd; best = j; }
    }
    return best;
}

/* Steers toward (tx,tz) at speed v with a limited turn rate; banks into turns. */
static void dragon_steer(int d, float tx, float tz, float v, float dt) {
    float dx = tx - u_x[d], dz = tz - u_z[d];
    if (dx * dx + dz * dz > 0.01f) {
        float want = approx_atan2(dx, -dz);
        float diff = wrap_angle(want - u_yaw[d]);
        float mt = 1.7f * dt;
        float turn = diff > mt ? mt : (diff < -mt ? -mt : diff);
        u_yaw[d] = wrap_angle(u_yaw[d] + turn);
        float nb = u_dbank[d] + ((turn / (dt + 1e-4f)) * 0.45f - u_dbank[d]) * w_clampf(dt * 3.0f, 0.0f, 1.0f);
        u_dbank[d] = w_clampf(nb, -0.7f, 0.7f);
    }
    float nv = u_dvel[d] + (v - u_dvel[d]) * w_clampf(dt * 1.5f, 0.0f, 1.0f);
    u_dvel[d] = nv;
    float nx = u_x[d] + w_sin(u_yaw[d]) * nv * dt, nz = u_z[d] - w_cos(u_yaw[d]) * nv * dt;
    u_x[d] = nx; u_z[d] = nz;
}

static void dragon_altitude(int d, float want, float rate, float dt) {
    float a = u_dalt[d], step = rate * dt;
    float diff = want - a;
    if (diff > step) diff = step;
    if (diff < -step) diff = -step;
    float na = a + diff;
    u_dalt[d] = na;
    u_y[d] = dragon_ground(u_x[d], u_z[d]) + na;
}

/* Fire: everyone (but its own side) in a 35-degree cone up to 10 ahead. */
static void dragon_breathe(int d) {
    int j;
    float fx = w_sin(u_yaw[d]), fz = -w_cos(u_yaw[d]);
    float mx = u_x[d] + fx * 2.6f * u_scale[d], mz = u_z[d] + fz * 2.6f * u_scale[d];
    for (j = 0; j < g_un; j++) {
        if (j == d || !unit_alive(j) || u_team[j] == u_team[d]) continue;
        float dx = u_x[j] - mx, dz = u_z[j] - mz;
        float dd = w_sqrt(dx * dx + dz * dz);
        if (dd > 10.0f || u_y[j] > u_y[d] + 1.5f) continue;
        if (dd > 0.5f && (dx * fx + dz * fz) / dd < 0.82f) continue;
        unit_damage(j, 1);
    }
}

/* Lets the dragon go: from a perch it takes off. */
static void dragon_takeoff(int d) {
    if (u_dstate[d] == DG_PERCH) u_dvel[d] = 2.0f;
}

/* Orders for a tamed dragon (player or AI). */
static void dragon_order_fly(int d, float x, float z) {
    dragon_takeoff(d);
    u_gx[d] = x; u_gz[d] = z; u_target[d] = -1;
    u_dstate[d] = DG_FLY; u_dt[d] = 0.0f;
}
static void dragon_order_attack(int d, int t) {
    dragon_takeoff(d);
    u_target[d] = t; u_dstate[d] = DG_ATTACK; u_dt[d] = 0.0f;
}

static void dragon_tamed(int d, int own) {
    u_own[d] = own;
    u_team[d] = team_of(own);
    u_role[d] = own > 0 ? R_ARMY : 0;
    u_dtame[d] = 0.0f;
    u_amove[d] = 0;
    int l;
    for (l = 0; l < g_nlair; l++) if (g_lair_u[l] == d) { g_lair_u[l] = -1; g_lair_t[l] = 0.0f; }
    /* home: by the owner's town centre */
    if (own >= 0 && f_tc[own] >= 0) {
        float hx, hz;
        building_door(f_tc[own], 10.0f, &hx, &hz);
        u_dhx[d] = hx; u_dhz[d] = hz;
    }
    if (own > 0) dragon_order_fly(d, u_dhx[d], u_dhz[d]);
    __android_log_print(4, "castles", "a dragon has been tamed by faction %d", own);
}

/* Per frame, for every dragon (on screen or not). */
static void dragon_update(int d, float dt) {
    float np = u_phase[d] + dt * (u_dstate[d] == DG_PERCH ? 0.8f : 4.2f);
    u_phase[d] = np;
    if (u_hit[d] > 0.0f) { float nh = u_hit[d] - dt; u_hit[d] = nh; }
    if (u_state[d] == US_DEAD) {
        /* falls out of the sky, lies there a while */
        float g = dragon_ground(u_x[d], u_z[d]);
        if (u_y[d] > g) { float ny = u_y[d] - 9.0f * dt; u_y[d] = ny < g ? g : ny; }
        float nd = u_dead[d] + dt;
        u_dead[d] = nd;
        if (nd > 12.0f) u_active[d] = 0;
        return;
    }
    if (u_dbreath[d] > 0.0f) {
        float nb = u_dbreath[d] - dt;
        u_dbreath[d] = nb;
        float nt = u_dtick[d] + dt;
        u_dtick[d] = nt;
        if (nt >= 0.2f) { u_dtick[d] = 0.0f; dragon_breathe(d); }
    }
    if (u_dcool[d] > 0.0f) { float nc = u_dcool[d] - dt; u_dcool[d] = nc; }
    if (u_deat[d] > 0.0f) { float ne = u_deat[d] - dt; u_deat[d] = ne; }
    int st = u_dstate[d], wild = u_own[d] < 0;
    if (st == DG_PERCH) {
        u_dvel[d] = 0.0f; u_dbank[d] = 0.0f;
        dragon_altitude(d, 0.0f, 6.0f, dt);
        float nt = u_dt[d] - dt;
        u_dt[d] = nt;
        int beingTamed = g_time - u_dtame_t[d] < 1.5f;
        if (wild) {
            if (u_dtame[d] > 0.0f && !beingTamed && g_time - u_dtame_t[d] > 20.0f) u_dtame[d] = 0.0f;
            if (nt <= 0.0f && !beingTamed) {
                int t = dragon_find_prey(d, u_dhx[d], u_dhz[d], 150.0f);
                if (t >= 0) {
                    u_dhunt[d] = 0.0f;
                    dragon_order_attack(d, t);
                    __android_log_print(4, "castles", "a wild dragon leaves its roost to hunt (unit %d, faction %d)", t, u_own[t]);
                }
                u_dt[d] = 70.0f + 70.0f * dg_rand(d, 3);
            }
        } else if (nt <= 0.0f) {
            u_dt[d] = 1.0f;
            int t = dragon_find_prey(d, u_x[d], u_z[d], 22.0f);
            if (t >= 0) dragon_order_attack(d, t);
        }
        return;
    }
    if (st == DG_FLY) {
        float dx = u_gx[d] - u_x[d], dz = u_gz[d] - u_z[d];
        float dist = w_sqrt(dx * dx + dz * dz);
        dragon_steer(d, u_gx[d], u_gz[d], dist < 20.0f ? 6.0f : 10.0f, dt);
        dragon_altitude(d, DG_CRUISE, 5.0f, dt);
        /* a tamed dragon on the attack (a wave, or sent to a castle) hits what it meets */
        if (!wild && (u_amove[d] || dist < 30.0f)) {
            float nt = u_dt[d] - dt;
            u_dt[d] = nt;
            if (nt <= 0.0f) {
                u_dt[d] = 0.7f;
                int t = dragon_find_prey(d, u_x[d], u_z[d], 25.0f);
                if (t >= 0) { dragon_order_attack(d, t); return; }
            }
        }
        if (dist < 6.0f) {
            if (wild || (u_gx[d] == u_dhx[d] && u_gz[d] == u_dhz[d])) { u_dstate[d] = DG_LAND; u_gx[d] = u_dhx[d]; u_gz[d] = u_dhz[d]; }
            else { u_dstate[d] = DG_HOVER; u_dt[d] = 0.0f; u_dhunt[d] = 0.0f; }
        }
        return;
    }
    if (st == DG_HOVER) {
        float nh = u_dhunt[d] + dt;
        u_dhunt[d] = nh;
        float a = nh * 0.5f;
        dragon_steer(d, u_gx[d] + w_cos(a) * 11.0f, u_gz[d] + w_sin(a) * 11.0f, 6.5f, dt);
        dragon_altitude(d, DG_CRUISE - 2.0f, 4.0f, dt);
        float nt = u_dt[d] - dt;
        u_dt[d] = nt;
        if (nt <= 0.0f) {
            u_dt[d] = 0.7f;
            int t = dragon_find_prey(d, u_gx[d], u_gz[d], 26.0f);
            if (t >= 0) { dragon_order_attack(d, t); return; }
        }
        if (nh > 25.0f) { u_dstate[d] = DG_LAND; }          /* nothing here: settle down */
        return;
    }
    if (st == DG_LAND) {
        float dx = u_gx[d] - u_x[d], dz = u_gz[d] - u_z[d];
        float dist = w_sqrt(dx * dx + dz * dz);
        if (dist > 1.0f) dragon_steer(d, u_gx[d], u_gz[d], dist < 8.0f ? 2.5f : 6.0f, dt);
        dragon_altitude(d, dist < 10.0f ? 0.0f : 6.0f, 3.5f, dt);
        if (dist < 2.5f && u_dalt[d] < 0.3f) { u_dstate[d] = DG_PERCH; u_dt[d] = wild ? 60.0f + 60.0f * dg_rand(d, 5) : 1.0f; }
        return;
    }
    /* DG_ATTACK */
    int t = u_target[d];
    float nh2 = u_dhunt[d] + dt;
    u_dhunt[d] = nh2;
    int give = t < 0 || !unit_alive(t);
    if (!give && wild) {
        float lx = u_x[t] - u_dhx[d], lz = u_z[t] - u_dhz[d];
        if (nh2 > 35.0f || lx * lx + lz * lz > 230.0f * 230.0f) give = 1;
    }
    if (give) {
        u_target[d] = -1; u_dhunt[d] = 0.0f;
        int t2 = dragon_find_prey(d, u_x[d], u_z[d], wild ? 18.0f : 25.0f);
        if (t2 >= 0 && !(wild && nh2 > 35.0f)) { dragon_order_attack(d, t2); return; }
        if (wild) { u_gx[d] = u_dhx[d]; u_gz[d] = u_dhz[d]; u_dstate[d] = DG_FLY; }
        else if (u_amove[d]) { u_dstate[d] = DG_FLY; u_dt[d] = 0.5f; }        /* the wave flies on */
        else { u_gx[d] = u_x[d]; u_gz[d] = u_z[d]; u_dstate[d] = DG_HOVER; u_dt[d] = 0.0f; u_dhunt[d] = 0.0f; }
        return;
    }
    float tx = u_x[t], tz = u_z[t];
    float dx = tx - u_x[d], dz = tz - u_z[d];
    float dist = w_sqrt(dx * dx + dz * dz);
    /* make passes: dive in, breathe, swoop over, climb and come round */
    dragon_steer(d, tx, tz, 9.5f, dt);
    float want = dist > 20.0f ? DG_CRUISE : (dist > 7.0f ? 6.0f : 2.2f);
    if (u_dcool[d] > 2.5f) want = 9.0f;                    /* pulling up after a pass */
    dragon_altitude(d, want, 7.0f, dt);
    float ahead = (dx * w_sin(u_yaw[d]) - dz * w_cos(u_yaw[d])) / (dist + 1e-3f);
    if (dist < 12.0f && ahead > 0.7f && u_dalt[d] < 9.0f && u_dcool[d] <= 0.0f) {
        u_dbreath[d] = 1.6f; u_dcool[d] = 4.0f; u_dtick[d] = 0.0f;
    }
    if (dist < 2.8f && u_dalt[d] < 4.0f && u_deat[d] <= 0.0f) {
        u_deat[d] = 3.0f;
        if (u_class[t] == UC_PEASANT && dg_rand(d, 11) < 0.4f) {
            unit_damage(t, 100);
            __android_log_print(4, "castles", "a dragon snatched up a peasant of faction %d", u_own[t]);
        } else unit_damage(t, 3);                           /* a bite / a raking claw */
    }
}

/* ---- taming (peasants in PS_T_GO / PS_T_WORK, dragon in u_bsite) ---- */
static void peasant_tame_update(int i, float dt) {
    int d = u_bsite[i];
    if (d < 0 || !unit_alive(d) || u_class[d] != UC_DRAGON || u_own[d] >= 0) { unit_release_field(i); u_order[i] = ORD_NONE; peasant_to_manual(i); return; }
    float dx = u_x[d] - u_x[i], dz = u_z[d] - u_z[i];
    float dist = w_sqrt(dx * dx + dz * dz);
    if (u_pstate[i] == PS_T_GO) {
        if (dist < 4.0f && u_dstate[d] == DG_PERCH) {
            unit_release_field(i); u_order[i] = ORD_NONE; u_repath[i] = 0;
            u_pstate[i] = PS_T_WORK; u_state[i] = US_IDLE; u_work_t[i] = 0.0f;
            return;
        }
        if (u_state[i] == US_WAIT && u_field[i] >= 0 && g_field_state[u_field[i]] == FS_READY) u_state[i] = US_MOVE;
        if (u_state[i] == US_MOVE) unit_follow_field(i, dt);
        else if (u_state[i] == US_IDLE && dist > 4.0f && u_dstate[d] == DG_PERCH) {
            /* head for wherever it is roosting now */
            u_gx[i] = u_x[d]; u_gy[i] = u_y[d]; u_gz[i] = u_z[d]; u_gl[i] = 0;
            u_order[i] = ORD_MOVE; u_repath[i] = 1; u_state[i] = US_WAIT;
        }
        return;
    }
    /* PS_T_WORK: calming it (only while it stays put) */
    u_state[i] = US_IDLE;
    unit_face(i, dx, dz, dt);
    float nw = u_work_t[i] + dt;
    u_work_t[i] = nw;
    if (u_dstate[d] != DG_PERCH || dist > 5.0f) { u_pstate[i] = PS_T_GO; return; }
    float np = u_dtame[d] + dt;
    u_dtame[d] = np;
    u_dtame_t[d] = g_time;
    if (np >= DG_TAME_T) {
        dragon_tamed(d, u_own[i]);
        int j;
        for (j = 0; j < g_un; j++) if (u_class[j] == UC_PEASANT && (u_pstate[j] == PS_T_GO || u_pstate[j] == PS_T_WORK) && u_bsite[j] == d) {
            unit_release_field(j); u_order[j] = ORD_NONE; peasant_to_manual(j);
        }
    }
}

/* Sends the selected player peasants to tame wild dragon d. */
static int order_tame(int d) {
    int i, n = 0;
    for (i = 0; i < g_un; i++) {
        if (!u_selected[i] || !unit_alive(i) || u_own[i] != 0 || u_class[i] != UC_PEASANT) continue;
        unit_release_field(i);
        u_pstate[i] = PS_T_GO; u_bsite[i] = d; u_job[i] = -1;
        u_gx[i] = u_x[d]; u_gy[i] = u_y[d]; u_gz[i] = u_z[d]; u_gl[i] = 0;
        u_order[i] = ORD_MOVE; u_target[i] = -1; u_repath[i] = 1; u_state[i] = US_WAIT;
        n++;
    }
    if (n) {
        dispatch_repaths();
        __android_log_print(4, "castles", "%d peasants sent to tame a dragon", n);
    }
    return n;
}

#endif /* CASTLES_DRAGONS_H */
