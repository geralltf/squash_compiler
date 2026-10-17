#include "include/vulkan_android.h"
#include "android/examples/castles_spirv.h"
#include "android/examples/castles_world.h"

/* Castles: an explorable, infinite, procedurally generated 3D world.
 *
 * World generation lives in castles_world.h (simplex/fBm terrain with
 * water, sand, grass and mountain regions; a "kingdom" noise field that
 * controls how often castles appear; Wave Function Collapse laying out
 * castles -- corner towers, walls, ramp-walls, gates, courtyards, keeps
 * -- plus roads, bridges, forests and villages; all of it seamless
 * across chunk borders). This file streams those chunks in and out
 * around the camera on a worker thread and renders them with Vulkan.
 *
 * Controls:
 *   left half of the screen  -- virtual joystick: fly forward/back,
 *                               strafe left/right
 *   right half of the screen -- drag to look around
 *   no input for a few seconds -- autopilot flies to the nearest
 *                               castle you haven't discovered yet
 * Fly close to a castle's keep to discover it: the screen flashes gold
 * and a gold marker is added to the counter in the top-left corner. The
 * minimap (top right) shows terrain, roads and castles around you (red
 * = undiscovered, gold = discovered).
 *
 * Same `-android -android-activity` architecture and the same real-
 * device workarounds as buttons_android.c (see its top comment):
 *   - vkCreateInstance on its own disposable thread;
 *   - every frame renders into ONE persistent offscreen image that is
 *     then copied into the acquired swapchain image (never creating a
 *     VkImageView on a swapchain image -- Mali driver crash);
 *   - vertex data is uploaded with map/memcpy/unmap each time, never
 *     through a persistently mapped pointer;
 *   - allocation sizes use max(requested, memReq.size);
 *   - VkImage/VkCommandBuffer local arrays sized [16]. */

extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
extern void *ANativeWindow_fromSurface(void *env, void *surface);
extern int pthread_create(unsigned long *thread, void *attr, void *(*start_routine)(void *), void *arg);
extern int pthread_mutex_lock(void *m);
extern int pthread_mutex_unlock(void *m);
extern int usleep(int usec);
extern int clock_gettime(int clk, void *ts);

#define TAG "castles"

typedef struct { float mvp[16]; float cam[4]; float params[4]; } PushConstants;
typedef struct { long tv_sec; long tv_nsec; } TimeSpec;

#define VK_COMPARE_OP_LESS_OR_EQUAL_ 3
#define VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT_ 0x200

/* ------------------------------------------------------------------ */
/* Chunk slots: one big host-visible vertex buffer, fixed-size slots.  */
/* ------------------------------------------------------------------ */
#define VIEW_R       3                     /* chunks loaded around camera */
#define SLOTS        64
#define SLOT_VERTS   30000
#define VERT_BYTES   (W_VSTRIDE * 4)
#define SLOT_BYTES   (SLOT_VERTS * VERT_BYTES)

static int g_slot_used[SLOTS];
static int g_slot_cx[SLOTS], g_slot_cz[SLOTS];
static int g_slot_vn[SLOTS];
static int g_slot_castle[SLOTS];
static float g_slot_keep_x[SLOTS], g_slot_keep_z[SLOTS], g_slot_keep_y[SLOTS];
static unsigned char g_slot_cells[SLOTS * W_NCELLS];

/* ------------------------------------------------------------------ */
/* Worker-thread chunk generation handshake (guarded by g_gen_mutex).  */
/* state: 0 idle, 1 requested, 2 done (result waiting in g_gen_stage). */
/* ------------------------------------------------------------------ */
static long g_gen_mutex[8];                /* bionic: all-zero = PTHREAD_MUTEX_INITIALIZER */
static int g_gen_state;
static int g_gen_cx, g_gen_cz;
static int g_gen_vn;
static int g_gen_castle;
static float g_gen_keep_x, g_gen_keep_z, g_gen_keep_y;
static unsigned char g_gen_cells[W_NCELLS];
static float g_gen_stage[SLOT_VERTS * W_VSTRIDE];
static int g_gen_ready;                    /* world_init + selftest done */

static int gen_get_state(void) {
    pthread_mutex_lock(g_gen_mutex);
    int s = g_gen_state;
    pthread_mutex_unlock(g_gen_mutex);
    return s;
}

static void gen_set_state(int s) {
    pthread_mutex_lock(g_gen_mutex);
    g_gen_state = s;
    pthread_mutex_unlock(g_gen_mutex);
}


static void *gen_thread_main(void *arg) {
    (void)arg;
    world_init();
    /* Self-test: integer checksums that must match a host gcc build of
     * castles_world.h -- catches ARM64 codegen differences early. */
    int st[64];
    int n = world_selftest(st, g_gen_stage, SLOT_VERTS);
    int i;
    for (i = 0; i < n; i++) __android_log_print(4, TAG, "selftest[%d]=%d", i, st[i]);
    pthread_mutex_lock(g_gen_mutex);
    g_gen_ready = 1;
    pthread_mutex_unlock(g_gen_mutex);
    for (;;) {
        if (gen_get_state() != 1) { usleep(2000); continue; }
        pthread_mutex_lock(g_gen_mutex);
        int cx = g_gen_cx, cz = g_gen_cz;
        pthread_mutex_unlock(g_gen_mutex);
        int vn = world_gen_chunk(cx, cz, g_gen_stage, SLOT_VERTS);
        pthread_mutex_lock(g_gen_mutex);
        g_gen_vn = vn;
        g_gen_castle = g_out_has_castle;
        g_gen_keep_x = g_out_keep_x; g_gen_keep_z = g_out_keep_z; g_gen_keep_y = g_out_keep_y;
        memcpy(g_gen_cells, g_out_cells, W_NCELLS);
        g_gen_state = 2;
        pthread_mutex_unlock(g_gen_mutex);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Touch input (written on the UI thread, read on the render thread).  */
/* ------------------------------------------------------------------ */
static float g_view_w, g_view_h;
static int g_touch_count;                  /* fingers currently down       */
static float g_touch_x0, g_touch_y0, g_touch_x1, g_touch_y1;
static int g_touch_seq;                    /* bumps on every event         */
static int g_touch_reanchor;               /* finger set changed           */

/* Raw IEEE-754 bits, not floats -- see buttons_android.c's
 * nativeTouchEvent comment (float JNI arguments arrive as 0.0). The
 * single-pointer callback is still exported so the shim has a target;
 * all state comes from nativeMultiTouchEvent, which sees both fingers. */
void nativeTouchEvent(void *env, void *clazz, int x_bits, int y_bits, int action) {
    (void)env; (void)clazz; (void)x_bits; (void)y_bits; (void)action;
}

/* packed = pointerCount << 4 | actionMasked. With one finger, x1/y1
 * repeat x0/y0. actionMasked: 0 DOWN, 1 UP, 2 MOVE, 3 CANCEL,
 * 5 POINTER_DOWN, 6 POINTER_UP. */
void nativeMultiTouchEvent(void *env, void *clazz, int packed, int x0_bits, int y0_bits, int x1_bits, int y1_bits) {
    (void)env; (void)clazz;
    int count = packed >> 4;
    int action = packed & 15;
    float x0 = *(float *)&x0_bits;
    float y0 = *(float *)&y0_bits;
    float x1 = *(float *)&x1_bits;
    float y1 = *(float *)&y1_bits;
    if (action == 1 || action == 3) count = 0;
    if (action == 6) {
        /* Which finger lifted isn't reported; drop to "re-anchor" and let
         * the next MOVE (count already reduced) re-establish roles. */
        count = count - 1;
        g_touch_reanchor = 1;
    }
    if (action == 5) g_touch_reanchor = 1;
    if (count > 2) count = 2;
    if (count < 0) count = 0;
    g_touch_x0 = x0; g_touch_y0 = y0;
    g_touch_x1 = x1; g_touch_y1 = y1;
    g_touch_count = count;
    g_touch_seq = g_touch_seq + 1;
}

/* ------------------------------------------------------------------ */
/* Matrices (column-major, Vulkan clip space: y down, z in [0,1])      */
/* ------------------------------------------------------------------ */
static void mat_mul(float *out, const float *a, const float *b) {
    int c, r, k;
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) {
                float t = a[k * 4 + r] * b[c * 4 + k];
                float s2 = s + t;
                s = s2;
            }
            out[c * 4 + r] = s;
        }
    }
}

static void mat_perspective(float *m, float fovy, float aspect, float zn, float zf) {
    int i;
    for (i = 0; i < 16; i++) m[i] = 0.0f;
    float half = fovy * 0.5f;
    float f = w_cos(half) / w_sin(half);
    m[0] = f / aspect;
    m[5] = -f;
    m[10] = zf / (zn - zf);
    m[11] = -1.0f;
    m[14] = (zn * zf) / (zn - zf);
}

/* View matrix for eye position e (3 floats) and yaw/pitch.
 * yaw 0 looks toward -z (north); positive yaw turns toward +x (east). */
static void mat_view(float *m, const float *e, float yaw, float pitch) {
    float cp = w_cos(pitch), sp = w_sin(pitch), cy = w_cos(yaw), sy = w_sin(yaw);
    float fx = cp * sy, fy = sp, fz = -cp * cy;
    float rx = cy, ry = 0.0f, rz = sy;
    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;
    m[0] = rx; m[4] = ry; m[8] = rz;
    m[1] = ux; m[5] = uy; m[9] = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f;
    m[12] = -(rx * e[0] + ry * e[1] + rz * e[2]);
    m[13] = -(ux * e[0] + uy * e[1] + uz * e[2]);
    m[14] = fx * e[0] + fy * e[1] + fz * e[2];
    m[15] = 1.0f;
}

static void mat_ortho_pixels(float *m, float w, float h) {
    int i;
    for (i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = 2.0f / w;
    m[5] = 2.0f / h;
    m[10] = 1.0f;
    m[12] = -1.0f;
    m[13] = -1.0f;
    m[15] = 1.0f;
}

/* ------------------------------------------------------------------ */
/* Camera / game state (render thread only)                            */
/* ------------------------------------------------------------------ */
static float g_cam[3];
static float g_yaw, g_pitch;
static float g_time;
static float g_idle;                        /* seconds since last touch    */
static int g_last_seq;
static int g_joy_active, g_look_active;
static float g_joy_ax, g_joy_ay, g_joy_x, g_joy_y;
static float g_look_lx, g_look_ly;
static float g_flash;                       /* discovery flash timer       */

#define MAX_DISC 256
static int g_ndisc;
static float g_disc_x[MAX_DISC], g_disc_z[MAX_DISC];

static int is_discovered(float x, float z) {
    int i;
    for (i = 0; i < g_ndisc; i++) {
        float dx = g_disc_x[i] - x, dz = g_disc_z[i] - z;
        if (dx * dx + dz * dz < 1.0f) return 1;
    }
    return 0;
}

static float wrap_angle(float a) {
    float r = a;
    while (r > W_PI) { float t = r - 2.0f * W_PI; r = t; }
    while (r < -W_PI) { float t = r + 2.0f * W_PI; r = t; }
    return r;
}

/* Fast inverse-free atan2 approximation (good to ~0.01 rad). */
static float approx_atan2(float y, float x) {
    float ax = w_absf(x), ay = w_absf(y);
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

/* Spawn near the closest castle to the origin (castle placement is a
 * pure function of chunk coords, so this needs no generated chunks). */
static void choose_spawn(void) {
    int r, cx, cz, a, b, c;
    for (r = 0; r < 30; r++) {
        for (cz = -r; cz <= r; cz++) {
            for (cx = -r; cx <= r; cx++) {
                if (cx != -r && cx != r && cz != -r && cz != r) continue;
                if (w_chunk_castle(cx, cz, &a, &b, &c)) {
                    float kx = (float)cx * W_CHUNK_SIZE + ((float)a + 0.5f) * W_CELL_SIZE;
                    float kz = (float)cz * W_CHUNK_SIZE + ((float)b + 0.5f) * W_CELL_SIZE;
                    g_cam[0] = kx;
                    g_cam[2] = kz + 70.0f;
                    float gh = world_ground_height(g_cam[0], g_cam[2]);
                    g_cam[1] = gh + 24.0f;
                    g_yaw = 0.0f;
                    g_pitch = -0.32f;
                    __android_log_print(4, TAG, "spawn near castle chunk %d,%d", cx, cz);
                    return;
                }
            }
        }
    }
    g_cam[0] = 0.0f; g_cam[2] = 0.0f;
    g_cam[1] = world_ground_height(0.0f, 0.0f) + 24.0f;
    g_pitch = -0.3f;
}

static void update_input(float dt) {
    int count = g_touch_count;
    float tx0 = g_touch_x0, ty0 = g_touch_y0, tx1 = g_touch_x1, ty1 = g_touch_y1;
    int seq = g_touch_seq;
    if (seq != g_last_seq) { g_last_seq = seq; g_idle = 0.0f; }
    else if (count == 0) { float ni = g_idle + dt; g_idle = ni; }
    if (g_touch_reanchor) {
        g_touch_reanchor = 0;
        g_joy_active = 0;
        g_look_active = 0;
    }
    int haveJoy = 0, haveLook = 0;
    float jx = 0.0f, jy = 0.0f, lx = 0.0f, ly = 0.0f;
    float half = g_view_w * 0.5f;
    if (count >= 1) {
        if (tx0 < half) { haveJoy = 1; jx = tx0; jy = ty0; } else { haveLook = 1; lx = tx0; ly = ty0; }
    }
    if (count >= 2) {
        if (tx1 < half && !haveJoy) { haveJoy = 1; jx = tx1; jy = ty1; }
        else if (tx1 >= half && !haveLook) { haveLook = 1; lx = tx1; ly = ty1; }
    }
    if (haveJoy) {
        if (!g_joy_active) { g_joy_active = 1; g_joy_ax = jx; g_joy_ay = jy; }
        g_joy_x = jx; g_joy_y = jy;
    } else {
        g_joy_active = 0;
    }
    if (haveLook) {
        if (!g_look_active) { g_look_active = 1; g_look_lx = lx; g_look_ly = ly; }
        float dx = lx - g_look_lx, dy = ly - g_look_ly;
        g_look_lx = lx; g_look_ly = ly;
        float ny = g_yaw + dx * 0.0042f;
        float np = g_pitch - dy * 0.0036f;
        g_yaw = wrap_angle(ny);
        g_pitch = w_clampf(np, -1.35f, 0.9f);
    } else {
        g_look_active = 0;
    }
}

static void update_camera(float dt) {
    float fwd = 0.0f, strafe = 0.0f;
    float radius = g_view_w * 0.13f;
    if (g_joy_active) {
        float dx = (g_joy_x - g_joy_ax) / radius, dy = (g_joy_y - g_joy_ay) / radius;
        float len = w_sqrt(dx * dx + dy * dy);
        if (len > 1.0f) { float ndx = dx / len, ndy = dy / len; dx = ndx; dy = ndy; }
        fwd = -dy;
        strafe = dx;
    }
    float speed = 26.0f;
    float cp = w_cos(g_pitch), sp = w_sin(g_pitch), cy = w_cos(g_yaw), sy = w_sin(g_yaw);
    float ground = world_ground_height(g_cam[0], g_cam[2]);

    if (g_idle > 5.0f) {
        /* Autopilot: head for the nearest undiscovered castle. */
        int i, best = -1;
        float bestD = 1e30f;
        for (i = 0; i < SLOTS; i++) {
            if (!g_slot_used[i] || !g_slot_castle[i]) continue;
            if (is_discovered(g_slot_keep_x[i], g_slot_keep_z[i])) continue;
            float dx = g_slot_keep_x[i] - g_cam[0], dz = g_slot_keep_z[i] - g_cam[2];
            float d = dx * dx + dz * dz;
            if (d < bestD) { bestD = d; best = i; }
        }
        float targetYaw = g_yaw;
        if (best >= 0) {
            float dx = g_slot_keep_x[best] - g_cam[0], dz = g_slot_keep_z[best] - g_cam[2];
            targetYaw = approx_atan2(dx, -dz);
        } else {
            float wob = w_sin(g_time * 0.11f) * 0.25f;
            float ty = g_yaw + wob * dt;
            targetYaw = ty;
        }
        float dyaw = wrap_angle(targetYaw - g_yaw);
        float maxTurn = 0.55f * dt;
        if (dyaw > maxTurn) dyaw = maxTurn;
        if (dyaw < -maxTurn) dyaw = -maxTurn;
        float ny = g_yaw + dyaw;
        g_yaw = wrap_angle(ny);
        float np = g_pitch + (-0.30f - g_pitch) * w_clampf(dt * 0.8f, 0.0f, 1.0f);
        g_pitch = np;
        /* Clear the terrain ahead, not just below: take the highest ground
         * along the next ~60 units of the flight path. */
        float clear = ground < 0.0f ? 0.0f : ground;
        int k;
        for (k = 1; k <= 4; k++) {
            float ax = g_cam[0] + sy * 15.0f * (float)k;
            float az = g_cam[2] - cy * 15.0f * (float)k;
            float ah = world_ground_height(ax, az);
            if (ah > clear) clear = ah;
        }
        float altTarget = clear + 20.0f;
        float gain = altTarget > g_cam[1] ? 1.6f : 0.5f;
        float vy = (altTarget - g_cam[1]) * gain;
        float nx = g_cam[0] + sy * speed * 0.6f * dt;
        float nz = g_cam[2] - cy * speed * 0.6f * dt;
        float nyy = g_cam[1] + vy * dt;
        g_cam[0] = nx; g_cam[2] = nz; g_cam[1] = nyy;
    } else {
        float fx = cp * sy, fy = sp, fz = -cp * cy;
        float rx = cy, rz = sy;
        float nx = g_cam[0] + (fx * fwd + rx * strafe) * speed * dt;
        float nyy = g_cam[1] + fy * fwd * speed * dt;
        float nz = g_cam[2] + (fz * fwd + rz * strafe) * speed * dt;
        g_cam[0] = nx; g_cam[1] = nyy; g_cam[2] = nz;
    }
    float g2 = world_ground_height(g_cam[0], g_cam[2]);
    float minY = g2 + 3.0f;
    if (g_cam[1] < minY) g_cam[1] = minY;
    if (g_cam[1] > 160.0f) g_cam[1] = 160.0f;

    /* Discovery: within reach of a castle keep. */
    int i;
    for (i = 0; i < SLOTS; i++) {
        if (!g_slot_used[i] || !g_slot_castle[i]) continue;
        float dx = g_slot_keep_x[i] - g_cam[0], dz = g_slot_keep_z[i] - g_cam[2];
        if (dx * dx + dz * dz < 38.0f * 38.0f && !is_discovered(g_slot_keep_x[i], g_slot_keep_z[i]) && g_ndisc < MAX_DISC) {
            g_disc_x[g_ndisc] = g_slot_keep_x[i];
            g_disc_z[g_ndisc] = g_slot_keep_z[i];
            g_ndisc = g_ndisc + 1;
            g_flash = 1.0f;
            __android_log_print(4, TAG, "castle discovered! total=%d", g_ndisc);
        }
    }
}

/* ------------------------------------------------------------------ */
/* HUD geometry (render thread only; separate from castles_world.h's   */
/* mesh globals, which belong to the generator thread).                */
/* ------------------------------------------------------------------ */
#define HUD_MAX_VERTS 16000
static float g_hud[HUD_MAX_VERTS * W_VSTRIDE];
static int g_hud_n;
static float g_hr, g_hg, g_hb;

static void hud_color(float r, float g, float b) { g_hr = r; g_hg = g; g_hb = b; }

static void hud_vert(float x, float y) {
    if (g_hud_n >= HUD_MAX_VERTS) return;
    float *o = g_hud + g_hud_n * W_VSTRIDE;
    o[0] = x; o[1] = y; o[2] = 0.0f;
    o[3] = 0.0f; o[4] = 0.0f; o[5] = 1.0f;
    o[6] = g_hr; o[7] = g_hg; o[8] = g_hb;
    g_hud_n = g_hud_n + 1;
}

static void hud_rect(float x, float y, float w, float h) {
    hud_vert(x, y); hud_vert(x + w, y); hud_vert(x + w, y + h);
    hud_vert(x, y); hud_vert(x + w, y + h); hud_vert(x, y + h);
}

static void hud_ring(float cx, float cy, float r, float thick) {
    int i;
    for (i = 0; i < 24; i++) {
        float a0 = (float)i * (2.0f * W_PI / 24.0f), a1 = (float)(i + 1) * (2.0f * W_PI / 24.0f);
        float c0 = w_cos(a0), s0 = w_sin(a0), c1 = w_cos(a1), s1 = w_sin(a1);
        float ri = r - thick;
        hud_vert(cx + c0 * ri, cy + s0 * ri); hud_vert(cx + c0 * r, cy + s0 * r); hud_vert(cx + c1 * r, cy + s1 * r);
        hud_vert(cx + c0 * ri, cy + s0 * ri); hud_vert(cx + c1 * r, cy + s1 * r); hud_vert(cx + c1 * ri, cy + s1 * ri);
    }
}

static void hud_disc(float cx, float cy, float r) {
    int i;
    for (i = 0; i < 20; i++) {
        float a0 = (float)i * (2.0f * W_PI / 20.0f), a1 = (float)(i + 1) * (2.0f * W_PI / 20.0f);
        hud_vert(cx, cy);
        hud_vert(cx + w_cos(a0) * r, cy + w_sin(a0) * r);
        hud_vert(cx + w_cos(a1) * r, cy + w_sin(a1) * r);
    }
}

static int find_slot(int cx, int cz) {
    int i;
    for (i = 0; i < SLOTS; i++) {
        if (g_slot_used[i] && g_slot_cx[i] == cx && g_slot_cz[i] == cz) return i;
    }
    return -1;
}

#define MAP_CELLS 44

static void build_hud(void) {
    float W = g_view_w, H = g_view_h;
    g_hud_n = 0;

    /* Discovery flash: gold frame around the screen edges. */
    if (g_flash > 0.0f) {
        float t = g_flash;
        hud_color(1.0f, 0.82f, 0.25f);
        float b = 26.0f * t;
        hud_rect(0.0f, 0.0f, W, b);
        hud_rect(0.0f, H - b, W, b);
        hud_rect(0.0f, 0.0f, b, H);
        hud_rect(W - b, 0.0f, b, H);
    }

    /* Minimap (north up), centered on the camera's cell. */
    float mapPx = W * 0.27f;
    float cellPx = mapPx / (float)MAP_CELLS;
    float mx0 = W - mapPx - 30.0f, my0 = 30.0f;
    hud_color(0.08f, 0.08f, 0.10f);
    hud_rect(mx0 - 6.0f, my0 - 6.0f, mapPx + 12.0f, mapPx + 12.0f);
    int pgx = w_ifloor(g_cam[0] / W_CELL_SIZE), pgz = w_ifloor(g_cam[2] / W_CELL_SIZE);
    int i, j;
    int lastSlot = -1, lastCx = 1 << 30, lastCz = 1 << 30;
    for (j = 0; j < MAP_CELLS; j++) {
        for (i = 0; i < MAP_CELLS; i++) {
            int gx = pgx - MAP_CELLS / 2 + i, gz = pgz - MAP_CELLS / 2 + j;
            int ccx = w_ifloor((float)gx / (float)W_CELLS), ccz = w_ifloor((float)gz / (float)W_CELLS);
            if (ccx != lastCx || ccz != lastCz) { lastSlot = find_slot(ccx, ccz); lastCx = ccx; lastCz = ccz; }
            int cls = 255;
            if (lastSlot >= 0) {
                int lx = gx - ccx * W_CELLS, lz = gz - ccz * W_CELLS;
                cls = g_slot_cells[lastSlot * W_NCELLS + lz * W_CELLS + lx];
            }
            if (cls == 0) hud_color(0.18f, 0.40f, 0.65f);
            else if (cls == 1) hud_color(0.36f, 0.58f, 0.28f);
            else if (cls == 2) hud_color(0.55f, 0.53f, 0.50f);
            else if (cls == 3) hud_color(0.80f, 0.68f, 0.45f);
            else if (cls == 4) hud_color(0.30f, 0.29f, 0.30f);
            else if (cls == 5) hud_color(0.18f, 0.40f, 0.18f);
            else hud_color(0.05f, 0.05f, 0.06f);
            hud_rect(mx0 + (float)i * cellPx, my0 + (float)j * cellPx, cellPx + 0.5f, cellPx + 0.5f);
        }
    }
    /* Castle markers. */
    for (i = 0; i < SLOTS; i++) {
        if (!g_slot_used[i] || !g_slot_castle[i]) continue;
        float rx = (g_slot_keep_x[i] / W_CELL_SIZE - (float)(pgx - MAP_CELLS / 2)) * cellPx;
        float rz = (g_slot_keep_z[i] / W_CELL_SIZE - (float)(pgz - MAP_CELLS / 2)) * cellPx;
        if (rx < 0.0f || rz < 0.0f || rx > mapPx || rz > mapPx) continue;
        if (is_discovered(g_slot_keep_x[i], g_slot_keep_z[i])) hud_color(1.0f, 0.82f, 0.2f);
        else hud_color(0.9f, 0.15f, 0.12f);
        hud_rect(mx0 + rx - cellPx * 1.2f, my0 + rz - cellPx * 1.2f, cellPx * 2.4f, cellPx * 2.4f);
    }
    /* Player arrow. */
    {
        float pcx = mx0 + (g_cam[0] / W_CELL_SIZE - (float)(pgx - MAP_CELLS / 2)) * cellPx;
        float pcz = my0 + (g_cam[2] / W_CELL_SIZE - (float)(pgz - MAP_CELLS / 2)) * cellPx;
        float sy = w_sin(g_yaw), cy = w_cos(g_yaw);
        float L = cellPx * 2.6f, Wd = cellPx * 1.3f;
        hud_color(1.0f, 1.0f, 1.0f);
        hud_vert(pcx + sy * L, pcz - cy * L);
        hud_vert(pcx - sy * L * 0.5f + cy * Wd, pcz + cy * L * 0.5f + sy * Wd);
        hud_vert(pcx - sy * L * 0.5f - cy * Wd, pcz + cy * L * 0.5f - sy * Wd);
    }

    /* Discovered-castle counter: one gold tower icon per castle. */
    float ix = 30.0f, iy = 30.0f, isz = W * 0.03f;
    for (i = 0; i < g_ndisc && i < 60; i++) {
        float x = ix + (float)(i % 12) * (isz * 1.35f);
        float y = iy + (float)(i / 12) * (isz * 1.6f);
        hud_color(0.25f, 0.2f, 0.05f);
        hud_rect(x - 3.0f, y - 3.0f, isz + 6.0f, isz * 1.2f + 6.0f);
        hud_color(1.0f, 0.82f, 0.25f);
        hud_rect(x, y + isz * 0.3f, isz, isz * 0.9f);
        hud_rect(x, y, isz * 0.28f, isz * 0.35f);
        hud_rect(x + isz * 0.36f, y, isz * 0.28f, isz * 0.35f);
        hud_rect(x + isz * 0.72f, y, isz * 0.28f, isz * 0.35f);
    }
    if (g_ndisc == 0) {
        hud_color(0.25f, 0.2f, 0.05f);
        hud_rect(ix - 3.0f, iy - 3.0f, isz + 6.0f, isz * 1.2f + 6.0f);
        hud_color(0.45f, 0.40f, 0.30f);
        hud_rect(ix, iy + isz * 0.3f, isz, isz * 0.9f);
    }

    /* Virtual joystick. */
    float jr = W * 0.13f;
    if (g_joy_active) {
        hud_color(0.95f, 0.95f, 0.95f);
        hud_ring(g_joy_ax, g_joy_ay, jr, 6.0f);
        float dx = g_joy_x - g_joy_ax, dy = g_joy_y - g_joy_ay;
        float len = w_sqrt(dx * dx + dy * dy);
        if (len > jr) { float s = jr / len; float ndx = dx * s, ndy = dy * s; dx = ndx; dy = ndy; }
        hud_disc(g_joy_ax + dx, g_joy_ay + dy, jr * 0.38f);
    } else {
        hud_color(0.85f, 0.88f, 0.92f);
        hud_ring(W * 0.22f, H * 0.80f, jr, 4.0f);
    }
}

/* ------------------------------------------------------------------ */
/* Vulkan helpers                                                      */
/* ------------------------------------------------------------------ */
static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *memProps, uint32_t typeBits, VkFlags props) {
    uint32_t i;
    for (i = 0; i < memProps->memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (memProps->memoryTypes[i].propertyFlags & props) == props) return i;
    }
    __android_log_print(4, TAG, "find_memory_type: no suitable memory type found!");
    return 0xFFFFFFFFu;
}

/* See buttons_android.c's sqw_copy_offscreen_to_swapchain(). */
static void copy_offscreen_to_swapchain(VkCommandBuffer cmd, VkImage offscreenImage, VkImage swapImage, uint32_t extentWidth, uint32_t extentHeight) {
    VkImageMemoryBarrier offToSrc;
    memset(&offToSrc, 0, sizeof(offToSrc));
    offToSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    offToSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    offToSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    offToSrc.srcQueueFamilyIndex = 0xFFFFFFFFu;
    offToSrc.dstQueueFamilyIndex = 0xFFFFFFFFu;
    offToSrc.image = offscreenImage;
    offToSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    offToSrc.subresourceRange.levelCount = 1;
    offToSrc.subresourceRange.layerCount = 1;
    offToSrc.srcAccessMask = 0;
    offToSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, 0, 0, 0, 1, &offToSrc);

    VkImageMemoryBarrier swapToDst;
    memset(&swapToDst, 0, sizeof(swapToDst));
    swapToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapToDst.srcQueueFamilyIndex = 0xFFFFFFFFu;
    swapToDst.dstQueueFamilyIndex = 0xFFFFFFFFu;
    swapToDst.image = swapImage;
    swapToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapToDst.subresourceRange.levelCount = 1;
    swapToDst.subresourceRange.layerCount = 1;
    swapToDst.srcAccessMask = 0;
    swapToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, 0, 0, 0, 1, &swapToDst);

    VkImageCopy copyRegion;
    memset(&copyRegion, 0, sizeof(copyRegion));
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent.width = extentWidth;
    copyRegion.extent.height = extentHeight;
    copyRegion.extent.depth = 1;
    vkCmdCopyImage(cmd, offscreenImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    VkImageMemoryBarrier swapToPresent;
    memset(&swapToPresent, 0, sizeof(swapToPresent));
    swapToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    swapToPresent.srcQueueFamilyIndex = 0xFFFFFFFFu;
    swapToPresent.dstQueueFamilyIndex = 0xFFFFFFFFu;
    swapToPresent.image = swapImage;
    swapToPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapToPresent.subresourceRange.levelCount = 1;
    swapToPresent.subresourceRange.layerCount = 1;
    swapToPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapToPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, 0, 0, 0, 1, &swapToPresent);
}

/* Creates a buffer + host-visible coherent memory of `size` bytes. */
static int create_host_buffer(VkDevice device, VkPhysicalDeviceMemoryProperties *memProps, VkDeviceSize size,
                              VkBuffer *outBuf, VkDeviceMemory *outMem) {
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buf = 0;
    VkResult vr = vkCreateBuffer(device, &bufInfo, 0, &buf);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "vkCreateBuffer failed vr=%d", (int)vr); return 0; }
    VkMemoryRequirements memReq;
    memset(&memReq, 0, sizeof(memReq));
    vkGetBufferMemoryRequirements(device, buf, &memReq);
    uint32_t mt = find_memory_type(memProps, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xFFFFFFFFu) return 0;
    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = (size > memReq.size) ? size : memReq.size;
    allocInfo.memoryTypeIndex = mt;
    VkDeviceMemory mem = 0;
    vr = vkAllocateMemory(device, &allocInfo, 0, &mem);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "vkAllocateMemory failed vr=%d", (int)vr); return 0; }
    vkBindBufferMemory(device, buf, mem, 0);
    *outBuf = buf;
    *outMem = mem;
    return 1;
}

static void upload(VkDevice device, VkDeviceMemory mem, VkDeviceSize offset, const void *src, VkDeviceSize bytes) {
    if (bytes == 0) return;
    void *mapped = 0;
    VkResult vr = vkMapMemory(device, mem, offset, bytes, 0, &mapped);
    if (vr != VK_SUCCESS || !mapped) { __android_log_print(4, TAG, "vkMapMemory failed vr=%d", (int)vr); return; }
    memcpy(mapped, src, (unsigned long)bytes);
    vkUnmapMemory(device, mem);
}

/* ------------------------------------------------------------------ */
/* Chunk streaming (render thread)                                     */
/* ------------------------------------------------------------------ */
static int chunk_dist(int cx, int cz, int pcx, int pcz) {
    int dx = cx > pcx ? cx - pcx : pcx - cx;
    int dz = cz > pcz ? cz - pcz : pcz - cz;
    return dx > dz ? dx : dz;
}

/* Collects a finished chunk from the generator (if any) into a slot and
 * requests the next missing chunk nearest the camera. */
static void stream_chunks(VkDevice device, VkDeviceMemory worldMem) {
    int pcx = w_ifloor(g_cam[0] / W_CHUNK_SIZE), pcz = w_ifloor(g_cam[2] / W_CHUNK_SIZE);
    int st = gen_get_state();
    if (st == 2) {
        pthread_mutex_lock(g_gen_mutex);
        int cx = g_gen_cx, cz = g_gen_cz, vn = g_gen_vn;
        pthread_mutex_unlock(g_gen_mutex);
        /* Pick a slot: a free one, else the farthest loaded one. */
        int i, slot = -1, far = -1, farD = -1;
        for (i = 0; i < SLOTS; i++) {
            if (!g_slot_used[i]) { slot = i; break; }
            int d = chunk_dist(g_slot_cx[i], g_slot_cz[i], pcx, pcz);
            if (d > farD) { farD = d; far = i; }
        }
        if (slot < 0) slot = far;
        if (find_slot(cx, cz) < 0 && slot >= 0) {
            upload(device, worldMem, (VkDeviceSize)slot * (VkDeviceSize)SLOT_BYTES, g_gen_stage,
                   (VkDeviceSize)vn * (VkDeviceSize)VERT_BYTES);
            g_slot_used[slot] = 1;
            g_slot_cx[slot] = cx; g_slot_cz[slot] = cz;
            g_slot_vn[slot] = vn;
            pthread_mutex_lock(g_gen_mutex);
            g_slot_castle[slot] = g_gen_castle;
            g_slot_keep_x[slot] = g_gen_keep_x; g_slot_keep_z[slot] = g_gen_keep_z; g_slot_keep_y[slot] = g_gen_keep_y;
            memcpy(g_slot_cells + slot * W_NCELLS, g_gen_cells, W_NCELLS);
            pthread_mutex_unlock(g_gen_mutex);
        }
        gen_set_state(0);
        st = 0;
    }
    if (st != 0) return;
    /* Nearest missing chunk within the view radius. */
    int bestCx = 0, bestCz = 0, bestD = 1 << 30;
    int dx, dz;
    for (dz = -VIEW_R; dz <= VIEW_R; dz++) {
        for (dx = -VIEW_R; dx <= VIEW_R; dx++) {
            int cx = pcx + dx, cz = pcz + dz;
            if (find_slot(cx, cz) >= 0) continue;
            int d = dx * dx + dz * dz;
            if (d < bestD) { bestD = d; bestCx = cx; bestCz = cz; }
        }
    }
    if (bestD == (1 << 30)) return;
    pthread_mutex_lock(g_gen_mutex);
    g_gen_cx = bestCx; g_gen_cz = bestCz;
    g_gen_state = 1;
    pthread_mutex_unlock(g_gen_mutex);
}

/* ------------------------------------------------------------------ */
/* Threads                                                             */
/* ------------------------------------------------------------------ */
VkInstance g_instance = 0;
int g_instance_ready = 0;
/* Surface lifecycle: a render session runs from nativeSurfaceCreated to
 * nativeSurfaceDestroyed (e.g. leaving the app or rotating the device).
 * The VkInstance, the generator thread and the game state (camera,
 * discovered castles) outlive sessions; everything device-level is torn
 * down and rebuilt per session. */
static int g_quit;
static int g_render_running;
static int g_gen_started;

void *create_instance_thread(void *window) {
    __android_log_print(4, TAG, "create_instance_thread: starting, window=%p", window);
    init_castles_spirv();

    VkApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "squash castles";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "squash";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = (1u << 22);

    const char *instExts[2];
    instExts[0] = "VK_KHR_surface";
    instExts[1] = "VK_KHR_android_surface";
    VkInstanceCreateInfo instInfo;
    memset(&instInfo, 0, sizeof(instInfo));
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledExtensionCount = 2;
    instInfo.ppEnabledExtensionNames = instExts;

    VkResult vr = vkCreateInstance(&instInfo, 0, &g_instance);
    __android_log_print(4, TAG, "vkCreateInstance vr=%d instance=%p", (int)vr, (void*)g_instance);
    if (vr == VK_SUCCESS) g_instance_ready = 1; else g_instance_ready = -1;
    return 0;
}

/* Monotonic time in microseconds (integer math only). */
static long now_us(void) {
    TimeSpec ts;
    ts.tv_sec = 0; ts.tv_nsec = 0;
    clock_gettime(1, &ts);
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void *render_session(void *window) {
    while (g_instance_ready == 0) usleep(10000);
    if (g_instance_ready < 0) { __android_log_print(4, TAG, "vkCreateInstance failed, aborting"); return 0; }
    VkInstance instance = g_instance;
    VkResult vr;

    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(instance, &physCount, 0);
    if (physCount == 0) return 0;
    VkPhysicalDevice physDevices[8];
    if (physCount > 8) physCount = 8;
    vkEnumeratePhysicalDevices(instance, &physCount, physDevices);
    VkPhysicalDevice phys = physDevices[0];

    VkAndroidSurfaceCreateInfoKHR surfInfo;
    memset(&surfInfo, 0, sizeof(surfInfo));
    surfInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfInfo.window = window;
    VkSurfaceKHR surface = 0;
    vr = vkCreateAndroidSurfaceKHR(instance, &surfInfo, 0, &surface);
    __android_log_print(4, TAG, "vkCreateAndroidSurfaceKHR vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, 0);
    VkQueueFamilyProperties qfProps[16];
    if (qfCount > 16) qfCount = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qfCount, qfProps);
    uint32_t queueFamily = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0; i < qfCount; i++) {
        if (!(qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 presentOk = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &presentOk);
        if (presentOk) { queueFamily = i; break; }
    }
    if (queueFamily == 0xFFFFFFFFu) return 0;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo dqInfo;
    memset(&dqInfo, 0, sizeof(dqInfo));
    dqInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dqInfo.queueFamilyIndex = queueFamily;
    dqInfo.queueCount = 1;
    dqInfo.pQueuePriorities = &queuePriority;

    const char *devExts[1];
    devExts[0] = "VK_KHR_swapchain";
    VkPhysicalDeviceFeatures feats;
    memset(&feats, 0, sizeof(feats));
    VkDeviceCreateInfo devInfo;
    memset(&devInfo, 0, sizeof(devInfo));
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &dqInfo;
    devInfo.enabledExtensionCount = 1;
    devInfo.ppEnabledExtensionNames = devExts;
    devInfo.pEnabledFeatures = &feats;

    VkDevice device = 0;
    vr = vkCreateDevice(phys, &devInfo, 0, &device);
    __android_log_print(4, TAG, "vkCreateDevice vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkQueue queue = 0;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkPhysicalDeviceMemoryProperties memProps;
    memset(&memProps, 0, sizeof(memProps));
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

    VkCommandPoolCreateInfo cpInfo;
    memset(&cpInfo, 0, sizeof(cpInfo));
    cpInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpInfo.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = 0;
    vr = vkCreateCommandPool(device, &cpInfo, 0, &commandPool);
    if (vr != VK_SUCCESS) return 0;

    VkSurfaceCapabilitiesKHR caps;
    memset(&caps, 0, sizeof(caps));
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, 0);
    VkSurfaceFormatKHR fmts[32];
    if (fmtCount > 32) fmtCount = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts);
    VkFormat chosenFormat = (fmtCount > 0) ? fmts[0].format : VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR chosenColorSpace = (fmtCount > 0) ? fmts[0].colorSpace : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (i = 0; i < fmtCount; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { chosenFormat = fmts[i].format; chosenColorSpace = fmts[i].colorSpace; break; }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) { extent.width = 1080; extent.height = 2400; }
    __android_log_print(4, TAG, "surface extent=%ux%u minImageCount=%u", extent.width, extent.height, caps.minImageCount);
    g_view_w = (float)extent.width;
    g_view_h = (float)extent.height;

    uint32_t imageCount = caps.minImageCount;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR scInfo;
    memset(&scInfo, 0, sizeof(scInfo));
    scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scInfo.surface = surface;
    scInfo.minImageCount = imageCount;
    scInfo.imageFormat = chosenFormat;
    scInfo.imageColorSpace = chosenColorSpace;
    scInfo.imageExtent = extent;
    scInfo.imageArrayLayers = 1;
    scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.preTransform = caps.currentTransform;
    scInfo.compositeAlpha = 0x1;
    scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scInfo.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = 0;
    vr = vkCreateSwapchainKHR(device, &scInfo, 0, &swapchain);
    __android_log_print(4, TAG, "vkCreateSwapchainKHR vr=%d format=%d", (int)vr, (int)chosenFormat);
    if (vr != VK_SUCCESS) return 0;

    uint32_t swapImageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, 0);
    VkImage swapImages[16];
    if (swapImageCount > 8) swapImageCount = 8;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);

    VkSemaphoreCreateInfo semInfo;
    memset(&semInfo, 0, sizeof(semInfo));
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore imageAvailable = 0, renderFinished = 0;
    vkCreateSemaphore(device, &semInfo, 0, &imageAvailable);
    vkCreateSemaphore(device, &semInfo, 0, &renderFinished);

    VkFenceCreateInfo fenceInfo;
    memset(&fenceInfo, 0, sizeof(fenceInfo));
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence inFlightFence = 0;
    vkCreateFence(device, &fenceInfo, 0, &inFlightFence);

    /* ---- render pass: offscreen color + depth ---- */
    VkAttachmentDescription attachments[2];
    memset(attachments, 0, sizeof(attachments));
    attachments[0].format = chosenFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef;
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep;
    memset(&dep, 0, sizeof(dep));
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT_;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT_;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo;
    memset(&rpInfo, 0, sizeof(rpInfo));
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    VkRenderPass renderPass = 0;
    vr = vkCreateRenderPass(device, &rpInfo, 0, &renderPass);
    __android_log_print(4, TAG, "vkCreateRenderPass vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    /* ---- offscreen color image (copied to the swapchain each frame) ---- */
    VkImageCreateInfo offImgInfo;
    memset(&offImgInfo, 0, sizeof(offImgInfo));
    offImgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    offImgInfo.imageType = VK_IMAGE_TYPE_2D;
    offImgInfo.format = chosenFormat;
    offImgInfo.extent.width = extent.width;
    offImgInfo.extent.height = extent.height;
    offImgInfo.extent.depth = 1;
    offImgInfo.mipLevels = 1;
    offImgInfo.arrayLayers = 1;
    offImgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    offImgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    offImgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    offImgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    offImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage offscreenImage = 0;
    vr = vkCreateImage(device, &offImgInfo, 0, &offscreenImage);
    if (vr != VK_SUCCESS) return 0;
    VkMemoryRequirements offMemReq;
    vkGetImageMemoryRequirements(device, offscreenImage, &offMemReq);
    VkMemoryAllocateInfo offAllocInfo;
    memset(&offAllocInfo, 0, sizeof(offAllocInfo));
    offAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    offAllocInfo.allocationSize = offMemReq.size;
    offAllocInfo.memoryTypeIndex = find_memory_type(&memProps, offMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory offscreenMemory = 0;
    vr = vkAllocateMemory(device, &offAllocInfo, 0, &offscreenMemory);
    if (vr != VK_SUCCESS) return 0;
    vkBindImageMemory(device, offscreenImage, offscreenMemory, 0);

    VkImageViewCreateInfo offViewInfo;
    memset(&offViewInfo, 0, sizeof(offViewInfo));
    offViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    offViewInfo.image = offscreenImage;
    offViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    offViewInfo.format = chosenFormat;
    offViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    offViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    offViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    offViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    offViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    offViewInfo.subresourceRange.levelCount = 1;
    offViewInfo.subresourceRange.layerCount = 1;
    VkImageView offscreenView = 0;
    vr = vkCreateImageView(device, &offViewInfo, 0, &offscreenView);
    if (vr != VK_SUCCESS) return 0;

    /* ---- depth image ---- */
    VkImageCreateInfo depthImgInfo;
    memset(&depthImgInfo, 0, sizeof(depthImgInfo));
    depthImgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImgInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImgInfo.format = VK_FORMAT_D32_SFLOAT;
    depthImgInfo.extent.width = extent.width;
    depthImgInfo.extent.height = extent.height;
    depthImgInfo.extent.depth = 1;
    depthImgInfo.mipLevels = 1;
    depthImgInfo.arrayLayers = 1;
    depthImgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depthImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage depthImage = 0;
    vr = vkCreateImage(device, &depthImgInfo, 0, &depthImage);
    __android_log_print(4, TAG, "depth vkCreateImage vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;
    VkMemoryRequirements depthMemReq;
    vkGetImageMemoryRequirements(device, depthImage, &depthMemReq);
    VkMemoryAllocateInfo depthAllocInfo;
    memset(&depthAllocInfo, 0, sizeof(depthAllocInfo));
    depthAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAllocInfo.allocationSize = depthMemReq.size;
    depthAllocInfo.memoryTypeIndex = find_memory_type(&memProps, depthMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory depthMemory = 0;
    vr = vkAllocateMemory(device, &depthAllocInfo, 0, &depthMemory);
    if (vr != VK_SUCCESS) return 0;
    vkBindImageMemory(device, depthImage, depthMemory, 0);

    VkImageViewCreateInfo depthViewInfo;
    memset(&depthViewInfo, 0, sizeof(depthViewInfo));
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
    depthViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    depthViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    depthViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    depthViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.layerCount = 1;
    VkImageView depthView = 0;
    vr = vkCreateImageView(device, &depthViewInfo, 0, &depthView);
    __android_log_print(4, TAG, "depth vkCreateImageView vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkImageView fbViews[2];
    fbViews[0] = offscreenView;
    fbViews[1] = depthView;
    VkFramebufferCreateInfo fbInfo;
    memset(&fbInfo, 0, sizeof(fbInfo));
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = fbViews;
    fbInfo.width = extent.width;
    fbInfo.height = extent.height;
    fbInfo.layers = 1;
    VkFramebuffer framebuffer = 0;
    vr = vkCreateFramebuffer(device, &fbInfo, 0, &framebuffer);
    __android_log_print(4, TAG, "vkCreateFramebuffer vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    /* ---- pipeline ---- */
    VkShaderModuleCreateInfo vsInfo;
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(g_castles_vert_spv);
    vsInfo.pCode = g_castles_vert_spv;
    VkShaderModule vsModule = 0;
    vr = vkCreateShaderModule(device, &vsInfo, 0, &vsModule);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "vs module failed vr=%d", (int)vr); return 0; }

    VkShaderModuleCreateInfo fsInfo;
    memset(&fsInfo, 0, sizeof(fsInfo));
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(g_castles_frag_spv);
    fsInfo.pCode = g_castles_frag_spv;
    VkShaderModule fsModule = 0;
    vr = vkCreateShaderModule(device, &fsInfo, 0, &fsModule);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "fs module failed vr=%d", (int)vr); return 0; }

    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding;
    binding.binding = 0;
    binding.stride = VERT_BYTES;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3];
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 12;
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[2].offset = 24;

    VkPipelineVertexInputStateCreateInfo vinState;
    memset(&vinState, 0, sizeof(vinState));
    vinState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vinState.vertexBindingDescriptionCount = 1;
    vinState.pVertexBindingDescriptions = &binding;
    vinState.vertexAttributeDescriptionCount = 3;
    vinState.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo iaState;
    memset(&iaState, 0, sizeof(iaState));
    iaState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport;
    viewport.x = 0; viewport.y = 0;
    viewport.width = (float)extent.width; viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
    VkRect2D scissor;
    scissor.offset.x = 0; scissor.offset.y = 0;
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo vpState;
    memset(&vpState, 0, sizeof(vpState));
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = &viewport;
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rsState;
    memset(&rsState, 0, sizeof(rsState));
    rsState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsState.polygonMode = VK_POLYGON_MODE_FILL;
    rsState.cullMode = 0;
    rsState.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rsState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msState;
    memset(&msState, 0, sizeof(msState));
    msState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dsState;
    memset(&dsState, 0, sizeof(dsState));
    dsState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsState.depthTestEnable = VK_TRUE;
    dsState.depthWriteEnable = VK_TRUE;
    dsState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL_;
    dsState.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttach;
    memset(&blendAttach, 0, sizeof(blendAttach));
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cbState;
    memset(&cbState, 0, sizeof(cbState));
    cbState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbState.attachmentCount = 1;
    cbState.pAttachments = &blendAttach;

    VkPushConstantRange pcRange;
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plInfo;
    memset(&plInfo, 0, sizeof(plInfo));
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;

    VkPipelineLayout pipelineLayout = 0;
    vr = vkCreatePipelineLayout(device, &plInfo, 0, &pipelineLayout);
    if (vr != VK_SUCCESS) return 0;

    VkGraphicsPipelineCreateInfo gpInfo;
    memset(&gpInfo, 0, sizeof(gpInfo));
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &vinState;
    gpInfo.pInputAssemblyState = &iaState;
    gpInfo.pViewportState = &vpState;
    gpInfo.pRasterizationState = &rsState;
    gpInfo.pMultisampleState = &msState;
    gpInfo.pDepthStencilState = &dsState;
    gpInfo.pColorBlendState = &cbState;
    gpInfo.layout = pipelineLayout;
    gpInfo.renderPass = renderPass;
    gpInfo.subpass = 0;
    gpInfo.basePipelineIndex = -1;

    VkPipeline pipeline = 0;
    vr = vkCreateGraphicsPipelines(device, 0, 1, &gpInfo, 0, &pipeline);
    __android_log_print(4, TAG, "vkCreateGraphicsPipelines vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    /* ---- vertex buffers ---- */
    VkBuffer worldBuffer = 0;
    VkDeviceMemory worldMemory = 0;
    if (!create_host_buffer(device, &memProps, (VkDeviceSize)SLOTS * (VkDeviceSize)SLOT_BYTES, &worldBuffer, &worldMemory)) return 0;
    VkBuffer hudBuffer = 0;
    VkDeviceMemory hudMemory = 0;
    if (!create_host_buffer(device, &memProps, (VkDeviceSize)HUD_MAX_VERTS * (VkDeviceSize)VERT_BYTES, &hudBuffer, &hudMemory)) return 0;
    __android_log_print(4, TAG, "vertex buffers ready (%d slots x %d verts)", SLOTS, SLOT_VERTS);

    VkCommandBufferAllocateInfo cbAllocInfo;
    memset(&cbAllocInfo, 0, sizeof(cbAllocInfo));
    cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.commandPool = commandPool;
    cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandBufferCount = swapImageCount;
    VkCommandBuffer cmdBufs[16];
    vr = vkAllocateCommandBuffers(device, &cbAllocInfo, cmdBufs);
    if (vr != VK_SUCCESS) return 0;

    /* ---- world ---- */
    for (i = 0; i < SLOTS; i++) g_slot_used[i] = 0;   /* new buffers: nothing uploaded yet */
    if (!g_gen_started) {
        unsigned long genThread;
        g_gen_started = 1;
        pthread_create(&genThread, 0, gen_thread_main, 0);
    }
    for (;;) {
        pthread_mutex_lock(g_gen_mutex);
        int ready = g_gen_ready;
        pthread_mutex_unlock(g_gen_mutex);
        if (ready) break;
        usleep(5000);
    }
    if (g_cam[0] == 0.0f && g_cam[1] == 0.0f && g_cam[2] == 0.0f) choose_spawn();

    long tPrev = now_us();
    long tLog = tPrev;
    int frames = 0;
    for (;;) {
        if (g_quit) break;
        long tNow = now_us();
        long dus = tNow - tPrev;
        float dt = (float)dus * 0.000001f;
        tPrev = tNow;
        if (dt > 0.1f) dt = 0.1f;
        if (dt < 0.0f) dt = 0.0f;
        float nt = g_time + dt;
        g_time = nt;
        if (g_flash > 0.0f) { float nf = g_flash - dt * 1.2f; g_flash = nf; }

        uint32_t imageIndex = 0;
        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, ~0ull);
        vkResetFences(device, 1, &inFlightFence);

        /* GPU is idle here: safe to overwrite any slot / the HUD buffer. */
        update_input(dt);
        update_camera(dt);
        stream_chunks(device, worldMemory);
        build_hud();
        upload(device, hudMemory, 0, g_hud, (VkDeviceSize)g_hud_n * (VkDeviceSize)VERT_BYTES);

        vr = vkAcquireNextImageKHR(device, swapchain, ~0ull, imageAvailable, 0, &imageIndex);
        if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR) {
            __android_log_print(4, TAG, "vkAcquireNextImageKHR failed vr=%d, stopping render loop", (int)vr);
            return 0;
        }

        VkCommandBuffer cmd = cmdBufs[imageIndex];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo;
        memset(&beginInfo, 0, sizeof(beginInfo));
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkClearValue clears[2];
        memset(clears, 0, sizeof(clears));
        clears[0].color.float32[0] = 0.62f;
        clears[0].color.float32[1] = 0.78f;
        clears[0].color.float32[2] = 0.93f;
        clears[0].color.float32[3] = 1.0f;
        clears[1].depthStencil.depth = 1.0f;
        clears[1].depthStencil.stencil = 0;

        VkRenderPassBeginInfo rpBegin;
        memset(&rpBegin, 0, sizeof(rpBegin));
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffer;
        rpBegin.renderArea.offset.x = 0; rpBegin.renderArea.offset.y = 0;
        rpBegin.renderArea.extent = extent;
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        /* World pass. */
        float proj[16], view[16];
        PushConstants pc;
        mat_perspective(proj, 1.05f, g_view_w / g_view_h, 0.5f, 420.0f);
        mat_view(view, g_cam, g_yaw, g_pitch);
        mat_mul(pc.mvp, proj, view);
        pc.cam[0] = g_cam[0]; pc.cam[1] = g_cam[1]; pc.cam[2] = g_cam[2]; pc.cam[3] = 0.0f;
        pc.params[0] = g_time;
        pc.params[1] = (float)VIEW_R * W_CHUNK_SIZE * 0.45f;
        pc.params[2] = (float)VIEW_R * W_CHUNK_SIZE + 6.0f;
        pc.params[3] = 0.0f;
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize offset0 = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &worldBuffer, &offset0);
        float fx = w_sin(g_yaw), fz = -w_cos(g_yaw);
        int s, drawn = 0;
        for (s = 0; s < SLOTS; s++) {
            if (!g_slot_used[s] || g_slot_vn[s] <= 0) continue;
            float ccx = ((float)g_slot_cx[s] + 0.5f) * W_CHUNK_SIZE - g_cam[0];
            float ccz = ((float)g_slot_cz[s] + 0.5f) * W_CHUNK_SIZE - g_cam[2];
            float ahead = ccx * fx + ccz * fz;
            if (ahead < -40.0f) continue;
            float dist2 = ccx * ccx + ccz * ccz;
            float maxD = ((float)VIEW_R + 0.8f) * W_CHUNK_SIZE;
            if (dist2 > maxD * maxD) continue;
            vkCmdDraw(cmd, (uint32_t)g_slot_vn[s], 1, (uint32_t)(s * SLOT_VERTS), 0);
            drawn = drawn + 1;
        }

        /* HUD pass (same pipeline, unlit, pixel-space ortho). */
        if (g_hud_n > 0) {
            PushConstants hpc;
            mat_ortho_pixels(hpc.mvp, g_view_w, g_view_h);
            hpc.cam[0] = 0.0f; hpc.cam[1] = 0.0f; hpc.cam[2] = 0.0f; hpc.cam[3] = 0.0f;
            hpc.params[0] = g_time; hpc.params[1] = 1.0f; hpc.params[2] = 2.0f; hpc.params[3] = 1.0f;
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(hpc), &hpc);
            VkDeviceSize hoff = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &hudBuffer, &hoff);
            vkCmdDraw(cmd, (uint32_t)g_hud_n, 1, 0, 0);
        }
        vkCmdEndRenderPass(cmd);

        copy_offscreen_to_swapchain(cmd, offscreenImage, swapImages[imageIndex], extent.width, extent.height);
        vkEndCommandBuffer(cmd);

        VkFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo;
        memset(&submitInfo, 0, sizeof(submitInfo));
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished;
        vr = vkQueueSubmit(queue, 1, &submitInfo, inFlightFence);

        VkPresentInfoKHR presentInfo;
        memset(&presentInfo, 0, sizeof(presentInfo));
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        vr = vkQueuePresentKHR(queue, &presentInfo);

        frames = frames + 1;
        if (tNow - tLog > 3000000) {
            int loaded = 0;
            for (s = 0; s < SLOTS; s++) if (g_slot_used[s]) loaded = loaded + 1;
            __android_log_print(4, TAG, "fps=%d chunks loaded=%d drawn=%d cam=%d,%d,%d castles=%d",
                                (int)((long)frames * 1000000 / (tNow - tLog)), loaded, drawn,
                                (int)g_cam[0], (int)g_cam[1], (int)g_cam[2], g_ndisc);
            frames = 0;
            tLog = tNow;
        }
    }

    /* Session teardown (surface going away). */
    vkDeviceWaitIdle(device);
    vkDestroyBuffer(device, hudBuffer, 0);
    vkFreeMemory(device, hudMemory, 0);
    vkDestroyBuffer(device, worldBuffer, 0);
    vkFreeMemory(device, worldMemory, 0);
    vkDestroyPipeline(device, pipeline, 0);
    vkDestroyPipelineLayout(device, pipelineLayout, 0);
    vkDestroyShaderModule(device, vsModule, 0);
    vkDestroyShaderModule(device, fsModule, 0);
    vkDestroyFramebuffer(device, framebuffer, 0);
    vkDestroyImageView(device, depthView, 0);
    vkDestroyImage(device, depthImage, 0);
    vkFreeMemory(device, depthMemory, 0);
    vkDestroyImageView(device, offscreenView, 0);
    vkDestroyImage(device, offscreenImage, 0);
    vkFreeMemory(device, offscreenMemory, 0);
    vkDestroyRenderPass(device, renderPass, 0);
    vkDestroyFence(device, inFlightFence, 0);
    vkDestroySemaphore(device, imageAvailable, 0);
    vkDestroySemaphore(device, renderFinished, 0);
    vkDestroyCommandPool(device, commandPool, 0);
    vkDestroySwapchainKHR(device, swapchain, 0);
    vkDestroyDevice(device, 0);
    vkDestroySurfaceKHR(instance, surface, 0);
    __android_log_print(4, TAG, "render session ended");
    return 0;
}

void *vulkan_thread_main(void *window) {
    render_session(window);
    g_render_running = 0;
    return 0;
}

void nativeSurfaceCreated(void *env, void *clazz, void *surface) {
    unsigned long t1, t2;
    (void)clazz;
    void *window = ANativeWindow_fromSurface(env, surface);
    __android_log_print(4, TAG, "nativeSurfaceCreated: window=%p", window);
    g_quit = 0;
    g_render_running = 1;
    /* One VkInstance for the app's lifetime, created on its own thread
     * (see buttons_android.c's create_instance_thread comment). */
    if (g_instance_ready == 0) pthread_create(&t1, 0, create_instance_thread, window);
    pthread_create(&t2, 0, vulkan_thread_main, window);
}

void nativeSurfaceChanged(void *env, void *clazz, int width, int height) {
    (void)env; (void)clazz;
    __android_log_print(4, TAG, "nativeSurfaceChanged: %dx%d", width, height);
}

void nativeSurfaceDestroyed(void *env, void *clazz) {
    (void)env; (void)clazz;
    __android_log_print(4, TAG, "nativeSurfaceDestroyed");
    /* The surface must not be rendered to after this returns: stop the
     * render loop and wait (bounded) for its teardown. */
    g_quit = 1;
    int waited = 0;
    while (g_render_running && waited < 400) { usleep(5000); waited = waited + 1; }
}

/* The DEX shim's onCreate() always hands native an EditText (used for
 * soft-keyboard text entry by the forms/buttons demos) and its
 * TextWatcher forwards edits; JNI aborts the app if these symbols are
 * missing, so they exist here as no-ops -- this game has no text entry. */
void nativeEditTextReady(void *env, void *clazz, void *editText) {
    (void)env; (void)clazz; (void)editText;
}

void nativeTextChanged(void *env, void *clazz, void *text) {
    (void)env; (void)clazz; (void)text;
}
