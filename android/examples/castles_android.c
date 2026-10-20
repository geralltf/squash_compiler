#include "include/vulkan_android.h"
#include "android/jni_min.h"
#include "android/examples/castles_spirv.h"
#include "android/examples/castles_world.h"
#include "android/examples/castles_ballistics.h"

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
 * Units (castles_units.h): a player squad spawns at the home castle and
 * guards hold every other castle's courtyard and wall-walks. They are
 * procedurally generated humanoids (random face colour, hair or helmet,
 * proportions, team tunic; sword + shield, or bow + quiver for archers)
 * with procedural idle / walk / sword-swing / bow-draw-and-release /
 * ladder-climb / death animation. Arrows are real projectiles
 * (castles_ballistics.h): forces accumulated through bal_add_force() and
 * integrated at a fixed 120 Hz step, with each shot's launch force solved
 * by running that same integration until the arc passes through the
 * target.
 *
 * Pathfinding runs on the GPU: an order builds a 160x160x2 layered nav
 * window (ground + castle wall-walks, joined by ramp-walls and ladders,
 * see castles_world.h's "Navigation grid"), and castles_nav.comp relaxes
 * a distance field over it until it converges (~80-150 ms); units then
 * walk downhill on that field, so a whole group shares one GPU search.
 * Goals beyond the window are reached in legs (castles_units.h).
 *
 * Controls (mode button, bottom right, toggles between them):
 *   Command mode (default):
 *     tap a squad member        -- select it
 *     drag a rectangle          -- select every squad member inside it
 *     tap ground or a wall-walk -- move the selection there
 *     tap an enemy              -- attack it
 *     select-all button         -- select the whole squad (double-tap:
 *                                  also move the camera to it)
 *     two fingers               -- pan (drag) or zoom (pinch): each
 *                                  gesture is one or the other
 *     navigator circle (bottom left) -- drag left/right to rotate the
 *                                  view, up/down to tilt it; double-tap
 *                                  to reset (north up, default tilt)
 *   Fly mode:
 *     left half of the screen   -- virtual joystick: pan forward/back
 *                                  and left/right at the current height
 *     right half of the screen  -- drag to look around
 *     no input for a few seconds -- autopilot flies to the nearest
 *                                   castle you haven't discovered yet
 * Flying close to a castle's keep discovers it: the screen flashes gold
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
static long now_us(void);

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
/* Per-slot navigation grid (castles_world.h "Navigation grid") and the
 * terrain vertex heights, for units walking the loaded world. */
static float g_slot_navh[SLOTS * NAV_L * NAV_N];
static unsigned char g_slot_navf[SLOTS * NAV_L * NAV_N];
static float g_slot_hgt[SLOTS * W_GRIDV * W_GRIDV];

static int find_slot(int cx, int cz) {
    int i;
    for (i = 0; i < SLOTS; i++) {
        if (g_slot_used[i] && g_slot_cx[i] == cx && g_slot_cz[i] == cz) return i;
    }
    return -1;
}

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
/* Touch input: the UI thread queues every multi-touch event; the      */
/* render thread drains the queue each frame (a polled "latest state"  */
/* alone loses taps whose DOWN and UP both land between two frames).   */
/* ------------------------------------------------------------------ */
static float g_view_w, g_view_h;

#define EVQ 128
static long g_ev_mutex[8];                 /* bionic: all-zero = PTHREAD_MUTEX_INITIALIZER */
static int g_evq_head, g_evq_tail;
static int g_evq_action[EVQ], g_evq_count[EVQ];
static float g_evq_x0[EVQ], g_evq_y0[EVQ], g_evq_x1[EVQ], g_evq_y1[EVQ];

/* Touch Y arrives in window space but the SurfaceView starts below the
 * title bar; corrected with View.getLocationOnScreen() on the shim's
 * EditText (same FrameLayout origin as the SurfaceView), queried lazily
 * on the first touch -- see buttons_android.c's sqw_ensure_surface_offset
 * for why it can't be queried in nativeEditTextReady (pre-layout). */
static jobject g_edit_text;
static jmethodID g_m_getlocationonscreen;
static int g_surface_offset_y;
static int g_surface_offset_queried;

static void ensure_surface_offset(void *env) {
    if (g_surface_offset_queried || !g_edit_text || !g_m_getlocationonscreen) return;
    g_surface_offset_queried = 1;
    jintArray locArr = sqw_jni_NewIntArray(env, 2);
    jvalue locArgs[1];
    locArgs[0].l = locArr;
    sqw_jni_CallVoidMethodA(env, g_edit_text, g_m_getlocationonscreen, locArgs);
    jint locOut[2];
    sqw_jni_GetIntArrayRegion(env, locArr, 0, 2, locOut);
    g_surface_offset_y = (int)locOut[1];
    __android_log_print(4, TAG, "surface screen offset x=%d y=%d", (int)locOut[0], g_surface_offset_y);
}

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
    (void)clazz;
    ensure_surface_offset(env);
    float x0 = *(float *)&x0_bits;
    float y0 = *(float *)&y0_bits;
    float x1 = *(float *)&x1_bits;
    float y1 = *(float *)&y1_bits;
    float oy = (float)g_surface_offset_y;
    pthread_mutex_lock(g_ev_mutex);
    int next = (g_evq_head + 1) % EVQ;
    if (next != g_evq_tail) {
        int k = g_evq_head;
        g_evq_action[k] = packed & 15;
        g_evq_count[k] = packed >> 4;
        g_evq_x0[k] = x0; g_evq_y0[k] = y0 - oy;
        g_evq_x1[k] = x1; g_evq_y1[k] = y1 - oy;
        g_evq_head = next;
    }
    pthread_mutex_unlock(g_ev_mutex);
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

#include "android/examples/castles_units.h"

#define RTS_PITCH     (-0.98f)        /* default: ~56 degrees down     */
#define RTS_PITCH_MIN (-1.50f)        /* nearly straight down          */
#define RTS_PITCH_MAX (-0.40f)        /* low, toward the horizon       */
#define RTS_DIST_MIN 12.0f
#define RTS_DIST_MAX 110.0f
static float g_rts_tx, g_rts_ty, g_rts_tz, g_rts_dist;
static float g_rts_pitch;            /* set only by the navigator circle */

/* Places g_cam for the current target/distance. */
static void rts_place_camera(void) {
    float cp = w_cos(g_pitch), sp = w_sin(g_pitch), cy = w_cos(g_yaw), sy = w_sin(g_yaw);
    g_cam[0] = g_rts_tx - cp * sy * g_rts_dist;
    g_cam[1] = g_rts_ty - sp * g_rts_dist;
    g_cam[2] = g_rts_tz + cp * cy * g_rts_dist;
}

/* Spawn at the castle closest to the origin -- the player's home castle,
 * where the squad appears once its chunk loads (castle placement is a
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
                    g_yaw = 0.0f;
                    g_rts_pitch = RTS_PITCH;
                    g_pitch = RTS_PITCH;
                    g_rts_tx = kx; g_rts_tz = kz + 4.0f;
                    g_rts_ty = world_ground_height(kx, kz);
                    g_rts_dist = 42.0f;
                    rts_place_camera();
                    g_home_set = 1; g_home_cx = cx; g_home_cz = cz;
                    __android_log_print(4, TAG, "home castle chunk %d,%d", cx, cz);
                    return;
                }
            }
        }
    }
    g_cam[0] = 0.0f; g_cam[2] = 0.0f;
    g_cam[1] = world_ground_height(0.0f, 0.0f) + 24.0f;
    g_pitch = -0.3f;
}

/* ------------------------------------------------------------------ */
/* Modes, gestures and HUD buttons                                     */
/* ------------------------------------------------------------------ */
#define MODE_COMMAND 0
#define MODE_FLY     1
static int g_mode;                          /* zero-init: command mode  */

#define GS_NONE    0
#define GS_PENDING 1   /* one finger down, not moved yet (maybe a tap) */
#define GS_DRAG    2   /* one finger dragging (selection rectangle)    */
#define GS_MULTI   3   /* two fingers (camera gesture)                 */
#define GS_BUTTON  4   /* pressed on a HUD button                      */
#define GS_ENDED   5   /* multi-finger gesture ending, ignore leftovers */
#define GS_NAV     6   /* dragging on the camera navigator circle      */
static int g_gs;
static int g_gs_button;
static float g_gs_x0, g_gs_y0, g_gs_cx, g_gs_cy;
static float g_mt_x0, g_mt_y0, g_mt_x1, g_mt_y1;
/* Two-finger gesture kind, decided once per gesture (see on_multi). */
#define MT_UNDECIDED 0
#define MT_PAN       1
#define MT_ZOOM      2
static int g_mt_kind;
static float g_mt_start_mx, g_mt_start_my, g_mt_start_spread;
/* Latest pointer state, for the fly-mode joystick/look. */
static int g_cur_count;
static float g_cur_x0, g_cur_y0, g_cur_x1, g_cur_y1;
static int g_touch_reanchor;

#define BTN_NONE   0
#define BTN_MODE   1
#define BTN_SELALL 2

static float btn_size(void) { return g_view_w * 0.14f; }

/* HUD button rects: x,y,w,h into r. */
static void btn_rect(int b, float *r) {
    float sz = btn_size();
    r[2] = sz; r[3] = sz;
    r[1] = g_view_h - sz - 40.0f;
    if (b == BTN_MODE) r[0] = g_view_w - sz - 30.0f;
    else r[0] = g_view_w - 2.0f * sz - 60.0f;
}

static int btn_hit(float x, float y) {
    float r[4];
    btn_rect(BTN_MODE, r);
    if (x >= r[0] && y >= r[1] && x <= r[0] + r[2] && y <= r[1] + r[3]) return BTN_MODE;
    if (g_mode == MODE_COMMAND) {
        btn_rect(BTN_SELALL, r);
        if (x >= r[0] && y >= r[1] && x <= r[0] + r[2] && y <= r[1] + r[3]) return BTN_SELALL;
    }
    return BTN_NONE;
}

/* Puts the camera over the selected units (or the whole squad). */
static void focus_on_squad(void) {
    int i, n = 0;
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    for (i = 0; i < g_un; i++) {
        if (!unit_alive(i) || u_team[i] != TEAM_PLAYER || !u_selected[i]) continue;
        float nx = cx + u_x[i], ny = cy + u_y[i], nz = cz + u_z[i];
        cx = nx; cy = ny; cz = nz;
        n++;
    }
    if (n == 0) return;
    cx = cx / (float)n; cy = cy / (float)n; cz = cz / (float)n;
    g_rts_tx = cx; g_rts_tz = cz; g_rts_ty = cy;
    if (g_rts_dist > 40.0f) g_rts_dist = 40.0f;
    rts_place_camera();
    __android_log_print(4, TAG, "camera -> squad at %d,%d", (int)cx, (int)cz);
}

static long g_selall_last_us;

static void on_button(int b) {
    if (b == BTN_MODE) {
        g_mode = (g_mode == MODE_COMMAND) ? MODE_FLY : MODE_COMMAND;
        if (g_mode == MODE_COMMAND) rts_begin();
        g_joy_active = 0; g_look_active = 0;
        __android_log_print(4, TAG, "mode -> %s", g_mode == MODE_COMMAND ? "command" : "fly");
    } else if (b == BTN_SELALL) {
        int i, n = 0;
        for (i = 0; i < g_un; i++) {
            u_selected[i] = unit_alive(i) && u_team[i] == TEAM_PLAYER && unit_loaded(i);
            if (u_selected[i]) n++;
        }
        __android_log_print(4, TAG, "select all: %d units", n);
        /* Double-tap: jump the camera to the squad. */
        long now = now_us();
        if (now - g_selall_last_us < 450000) focus_on_squad();
        g_selall_last_us = now;
    }
}

/* Command-mode tap: select a unit, attack an enemy, or move the
 * selection to the tapped ground / wall-walk. */
static void on_tap(float x, float y) {
    int u = pick_unit(x, y);
    int i;
    if (u >= 0 && u_team[u] == TEAM_PLAYER) {
        for (i = 0; i < g_un; i++) u_selected[i] = 0;
        u_selected[u] = 1;
        __android_log_print(4, TAG, "tap at %d,%d: selected unit %d", (int)x, (int)y, u);
        return;
    }
    if (units_selected_count() == 0) {
        __android_log_print(4, TAG, "tap at %d,%d: nothing selected", (int)x, (int)y);
        return;
    }
    if (u >= 0) {
        order_move(u_x[u], u_y[u], u_z[u], u_layer[u], u);
        return;
    }
    float hx, hy, hz;
    int hl;
    if (pick_ground(x, y, &hx, &hy, &hz, &hl)) order_move(hx, hy, hz, hl, -1);
}

/* Rectangle selection: every player unit whose body projects inside. */
static void on_drag_end(void) {
    float x0 = g_gs_x0 < g_gs_cx ? g_gs_x0 : g_gs_cx, x1 = g_gs_x0 < g_gs_cx ? g_gs_cx : g_gs_x0;
    float y0 = g_gs_y0 < g_gs_cy ? g_gs_y0 : g_gs_cy, y1 = g_gs_y0 < g_gs_cy ? g_gs_cy : g_gs_y0;
    int i, n = 0;
    for (i = 0; i < g_un; i++) {
        u_selected[i] = 0;
        if (!unit_alive(i) || u_team[i] != TEAM_PLAYER || !unit_loaded(i)) continue;
        float px, py;
        if (!project_point(u_x[i], u_y[i] + 0.9f, u_z[i], &px, &py)) continue;
        if (px >= x0 && px <= x1 && py >= y0 && py <= y1) { u_selected[i] = 1; n++; }
    }
    __android_log_print(4, TAG, "rect select (%d,%d)-(%d,%d): %d units", (int)x0, (int)y0, (int)x1, (int)y1, n);
}

/* ------------------------------------------------------------------ */
/* Command-mode camera: fixed-angle, isometric-style 3D view           */
/* ------------------------------------------------------------------ */
/* The camera orbits a target point on the ground at distance g_rts_dist.
 * Controls are strictly separated:
 *   - two fingers on the map: pan (drag) and zoom (pinch) only -- never
 *     any change of angle;
 *   - the navigator circle (bottom left): rotate around the target
 *     (left/right) and tilt (up/down) only; double-tap it to reset. */
static float rts_ground_at(float x, float z) {
    float h = nav_terrain_height(x, z);
    return h < 0.0f ? 0.0f : h;
}

/* Switches to the fixed view, looking at what's under the screen center
 * (or straight ahead of the camera if that's off the world). */
static void rts_begin(void) {
    float hx, hy, hz;
    int hl;
    if (pick_ground(g_view_w * 0.5f, g_view_h * 0.5f, &hx, &hy, &hz, &hl)) {
        g_rts_tx = hx; g_rts_tz = hz;
    } else {
        float sy = w_sin(g_yaw), cy = w_cos(g_yaw);
        g_rts_tx = g_cam[0] + sy * 30.0f; g_rts_tz = g_cam[2] - cy * 30.0f;
    }
    g_rts_ty = rts_ground_at(g_rts_tx, g_rts_tz);
    if (g_rts_dist < RTS_DIST_MIN) g_rts_dist = 40.0f;
    if (g_rts_pitch > RTS_PITCH_MAX || g_rts_pitch < RTS_PITCH_MIN) g_rts_pitch = RTS_PITCH;
    g_pitch = g_rts_pitch;
    rts_place_camera();
}

/* Per frame: follow the terrain under the target smoothly. */
static void rts_update(float dt) {
    float gy = rts_ground_at(g_rts_tx, g_rts_tz);
    float k = w_clampf(dt * 4.0f, 0.0f, 1.0f);
    float ny = g_rts_ty + (gy - g_rts_ty) * k;
    g_rts_ty = ny;
    g_pitch = g_rts_pitch;
    rts_place_camera();
}

/* Starts a two-finger gesture (called on POINTER_DOWN). */
static void multi_begin(float x0, float y0, float x1, float y1) {
    g_mt_x0 = x0; g_mt_y0 = y0; g_mt_x1 = x1; g_mt_y1 = y1;
    g_mt_kind = MT_UNDECIDED;
    g_mt_start_mx = (x0 + x1) * 0.5f; g_mt_start_my = (y0 + y1) * 0.5f;
    float dx = x1 - x0, dy = y1 - y0;
    g_mt_start_spread = w_sqrt(dx * dx + dy * dy);
}

/* Two fingers: EITHER pan OR zoom, never both. Fingers never keep exactly
 * the same spread while dragging, so applying spread changes as zoom on
 * every move made vertical (and any) pans zoom in and out. Instead, once
 * the fingers have moved ~40 px, the gesture is classified -- fingers
 * travelling together = pan, spreading/closing = pinch -- and stays that
 * for the rest of the gesture.
 * Panning moves the map by the same world distance per pixel everywhere
 * on screen (scaled for the view's centre), so up/down drags feel like
 * left/right ones. Pinch zooms toward the centre. */
static void on_multi(float x0, float y0, float x1, float y1) {
    float pmx = (g_mt_x0 + g_mt_x1) * 0.5f, pmy = (g_mt_y0 + g_mt_y1) * 0.5f;
    float nmx = (x0 + x1) * 0.5f, nmy = (y0 + y1) * 0.5f;
    float pdx = g_mt_x1 - g_mt_x0, pdy = g_mt_y1 - g_mt_y0;
    float ndx = x1 - x0, ndy = y1 - y0;
    float pd = w_sqrt(pdx * pdx + pdy * pdy), nd = w_sqrt(ndx * ndx + ndy * ndy);
    g_mt_x0 = x0; g_mt_y0 = y0; g_mt_x1 = x1; g_mt_y1 = y1;
    if (g_mode != MODE_COMMAND) return;
    if (g_mt_kind == MT_UNDECIDED) {
        float tx = nmx - g_mt_start_mx, ty = nmy - g_mt_start_my;
        float travel = w_sqrt(tx * tx + ty * ty);
        float spread = w_absf(nd - g_mt_start_spread);
        if (travel < 40.0f && spread < 40.0f) return;     /* not decided yet */
        g_mt_kind = spread > travel ? MT_ZOOM : MT_PAN;
        return;                                            /* start clean next move */
    }
    /* World units per pixel at the target's distance. */
    float half = 1.05f * 0.5f;
    float upp = 2.0f * g_rts_dist * (w_sin(half) / w_cos(half)) / g_view_h;
    float cy = w_cos(g_yaw), sy = w_sin(g_yaw);
    float foreshorten = -w_sin(g_rts_pitch);       /* ground depth is squashed on screen */
    if (foreshorten < 0.3f) foreshorten = 0.3f;
    float mdx = nmx - pmx, mdy = nmy - pmy;
    if (g_mt_kind == MT_ZOOM) { mdx = 0.0f; mdy = 0.0f; }
    /* The map follows the fingers: right -> camera moves left, down ->
     * camera moves forward (further "up" the map comes into view). */
    float side = -mdx * upp, fwd = mdy * upp / foreshorten;
    float ntx = g_rts_tx + cy * side + sy * fwd;
    float ntz = g_rts_tz + sy * side - cy * fwd;
    g_rts_tx = ntx; g_rts_tz = ntz;
    if (g_mt_kind == MT_ZOOM && pd > 30.0f && nd > 30.0f) {
        float nd2 = g_rts_dist * (pd / nd);
        g_rts_dist = w_clampf(nd2, RTS_DIST_MIN, RTS_DIST_MAX);
    }
    rts_place_camera();
}

/* ---- camera navigator circle (command mode, bottom left) ---- */
static float g_nav_lx, g_nav_ly;            /* last finger position     */
static float g_nav_kx, g_nav_ky;            /* knob offset (drawn)      */
static float g_nav_moved;                   /* total drag, tap detection */
static long g_nav_last_tap_us;

static void nav_circle(float *cx, float *cy, float *r) {
    *r = g_view_w * 0.11f;
    *cx = 40.0f + *r;
    *cy = g_view_h - 230.0f - *r;
}

static int nav_hit(float x, float y) {
    float cx, cy, r;
    if (g_mode != MODE_COMMAND) return 0;
    nav_circle(&cx, &cy, &r);
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= (r * 1.2f) * (r * 1.2f);
}

/* Left/right rotates around the target, up/down tilts (up = toward the
 * horizon, down = toward straight down). Direct and proportional, like a
 * trackball: the angle changes with the finger and stays when released. */
static void nav_drag(float x, float y) {
    float dx = x - g_nav_lx, dy = y - g_nav_ly;
    g_nav_lx = x; g_nav_ly = y;
    float nm = g_nav_moved + w_absf(dx) + w_absf(dy);
    g_nav_moved = nm;
    float ny = wrap_angle(g_yaw - dx * 0.0085f);
    g_yaw = ny;
    float np = w_clampf(g_rts_pitch - dy * 0.0045f, RTS_PITCH_MIN, RTS_PITCH_MAX);
    g_rts_pitch = np;
    g_pitch = np;
    float cx, cy, r;
    nav_circle(&cx, &cy, &r);
    float kx = x - cx, ky = y - cy;
    float kd = w_sqrt(kx * kx + ky * ky), lim = r * 0.62f;
    if (kd > lim) { float sc = lim / kd; float nkx = kx * sc, nky = ky * sc; kx = nkx; ky = nky; }
    g_nav_kx = kx; g_nav_ky = ky;
    rts_place_camera();
}

static void nav_release(void) {
    g_nav_kx = 0.0f; g_nav_ky = 0.0f;
    if (g_nav_moved < 20.0f) {
        long now = now_us();
        if (now - g_nav_last_tap_us < 450000) {
            /* Double-tap: default angle, north up. */
            g_yaw = 0.0f;
            g_rts_pitch = RTS_PITCH;
            g_pitch = RTS_PITCH;
            rts_place_camera();
            __android_log_print(4, TAG, "camera angle reset");
        }
        g_nav_last_tap_us = now;
    }
}

static void process_touch_events(void) {
    for (;;) {
        pthread_mutex_lock(g_ev_mutex);
        if (g_evq_tail == g_evq_head) { pthread_mutex_unlock(g_ev_mutex); break; }
        int k = g_evq_tail;
        int action = g_evq_action[k], count = g_evq_count[k];
        float x0 = g_evq_x0[k], y0 = g_evq_y0[k], x1 = g_evq_x1[k], y1 = g_evq_y1[k];
        g_evq_tail = (g_evq_tail + 1) % EVQ;
        pthread_mutex_unlock(g_ev_mutex);

        g_idle = 0.0f;
        int cur = count;
        if (action == 1 || action == 3) cur = 0;
        if (action == 6) { cur = count - 1; g_touch_reanchor = 1; }
        if (action == 5) g_touch_reanchor = 1;
        if (cur > 2) cur = 2;
        g_cur_count = cur;
        g_cur_x0 = x0; g_cur_y0 = y0; g_cur_x1 = x1; g_cur_y1 = y1;

        if (action == 0) {
            g_gs_x0 = x0; g_gs_y0 = y0; g_gs_cx = x0; g_gs_cy = y0;
            g_gs_button = btn_hit(x0, y0);
            if (g_gs_button != BTN_NONE) g_gs = GS_BUTTON;
            else if (nav_hit(x0, y0)) {
                g_gs = GS_NAV;
                g_nav_lx = x0; g_nav_ly = y0; g_nav_moved = 0.0f;
            } else g_gs = GS_PENDING;
        } else if (action == 5) {
            if (g_gs != GS_BUTTON && g_gs != GS_NAV) {
                g_gs = GS_MULTI;
                multi_begin(x0, y0, x1, y1);
            }
        } else if (action == 2) {
            if (g_gs == GS_NAV) nav_drag(x0, y0);
            else if (g_gs == GS_MULTI && count >= 2) on_multi(x0, y0, x1, y1);
            else if (g_gs == GS_PENDING || g_gs == GS_DRAG) {
                g_gs_cx = x0; g_gs_cy = y0;
                float dx = x0 - g_gs_x0, dy = y0 - g_gs_y0;
                if (g_gs == GS_PENDING && dx * dx + dy * dy > 32.0f * 32.0f) g_gs = GS_DRAG;
            }
        } else if (action == 6) {
            if (g_gs == GS_MULTI) g_gs = GS_ENDED;
        } else if (action == 1) {
            if (g_gs == GS_NAV) nav_release();
            else if (g_gs == GS_BUTTON && btn_hit(x0, y0) == g_gs_button) on_button(g_gs_button);
            else if (g_mode == MODE_COMMAND && g_gs == GS_PENDING) on_tap(x0, y0);
            else if (g_mode == MODE_COMMAND && g_gs == GS_DRAG) on_drag_end();
            g_gs = GS_NONE;
        } else if (action == 3) {
            if (g_gs == GS_NAV) nav_release();
            g_gs = GS_NONE;
        }
    }
}

static void update_input(float dt) {
    float ni = g_idle + dt;
    g_idle = ni;
    process_touch_events();
    if (g_touch_reanchor) {
        g_touch_reanchor = 0;
        g_joy_active = 0;
        g_look_active = 0;
    }
    /* Fly mode: left half joystick, right half look (not while a HUD
     * button is held). */
    int count = (g_mode == MODE_FLY && g_gs != GS_BUTTON) ? g_cur_count : 0;
    float tx0 = g_cur_x0, ty0 = g_cur_y0, tx1 = g_cur_x1, ty1 = g_cur_y1;
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

    if (g_mode == MODE_COMMAND) {
        rts_update(dt);
    } else if (g_idle > 5.0f) {
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
        /* Joystick pans over the ground: forward/back follow the heading
         * flattened onto the ground plane and the height is kept, so
         * pushing up never dives toward (or pulling down climbs away
         * from) the ground -- that read as zooming. */
        float fx = sy, fz = -cy;
        float rx = cy, rz = sy;
        float nx = g_cam[0] + (fx * fwd + rx * strafe) * speed * dt;
        float nz = g_cam[2] + (fz * fwd + rz * strafe) * speed * dt;
        g_cam[0] = nx; g_cam[2] = nz;
    }
    if (g_mode == MODE_FLY) {
        float g2 = world_ground_height(g_cam[0], g_cam[2]);
        float minY = g2 + 3.0f;
        if (g_cam[1] < minY) g_cam[1] = minY;
        if (g_cam[1] > 160.0f) g_cam[1] = 160.0f;
    }

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
#define HUD_MAX_VERTS 20000
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

    /* Virtual joystick (fly mode). */
    float jr = W * 0.13f;
    if (g_mode == MODE_FLY) {
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

    /* Selection rectangle while dragging (command mode). */
    if (g_mode == MODE_COMMAND && g_gs == GS_DRAG) {
        float x0 = g_gs_x0 < g_gs_cx ? g_gs_x0 : g_gs_cx, x1 = g_gs_x0 < g_gs_cx ? g_gs_cx : g_gs_x0;
        float y0 = g_gs_y0 < g_gs_cy ? g_gs_y0 : g_gs_cy, y1 = g_gs_y0 < g_gs_cy ? g_gs_cy : g_gs_y0;
        float t = 4.0f;
        hud_color(0.3f, 1.0f, 0.4f);
        hud_rect(x0, y0, x1 - x0, t);
        hud_rect(x0, y1 - t, x1 - x0, t);
        hud_rect(x0, y0, t, y1 - y0);
        hud_rect(x1 - t, y0, t, y1 - y0);
    }

    /* Buttons. */
    float r[4];
    btn_rect(BTN_MODE, r);
    float bx = r[0], by = r[1], bs = r[2];
    hud_color(0.08f, 0.08f, 0.10f);
    hud_rect(bx - 5.0f, by - 5.0f, bs + 10.0f, bs + 10.0f);
    if (g_mode == MODE_COMMAND) {
        /* Command mode: crossed swords on orange. */
        hud_color(0.85f, 0.50f, 0.15f);
        hud_rect(bx, by, bs, bs);
        hud_color(0.95f, 0.95f, 0.97f);
        int k;
        for (k = 0; k < 9; k++) {
            float t = (float)k / 8.0f;
            hud_rect(bx + bs * (0.2f + 0.6f * t) - 6.0f, by + bs * (0.2f + 0.6f * t) - 6.0f, 12.0f, 12.0f);
            hud_rect(bx + bs * (0.8f - 0.6f * t) - 6.0f, by + bs * (0.2f + 0.6f * t) - 6.0f, 12.0f, 12.0f);
        }
    } else {
        /* Fly mode: a bird/plane chevron on blue. */
        hud_color(0.18f, 0.42f, 0.80f);
        hud_rect(bx, by, bs, bs);
        hud_color(0.95f, 0.95f, 0.97f);
        int k;
        for (k = 0; k < 9; k++) {
            float t = (float)k / 8.0f;
            hud_rect(bx + bs * (0.15f + 0.35f * t) - 6.0f, by + bs * (0.35f + 0.3f * t) - 6.0f, 12.0f, 12.0f);
            hud_rect(bx + bs * (0.85f - 0.35f * t) - 6.0f, by + bs * (0.35f + 0.3f * t) - 6.0f, 12.0f, 12.0f);
        }
    }
    if (g_mode == MODE_COMMAND) {
        /* Select-all: three little figures. */
        btn_rect(BTN_SELALL, r);
        bx = r[0]; by = r[1];
        hud_color(0.08f, 0.08f, 0.10f);
        hud_rect(bx - 5.0f, by - 5.0f, bs + 10.0f, bs + 10.0f);
        hud_color(0.20f, 0.55f, 0.28f);
        hud_rect(bx, by, bs, bs);
        hud_color(0.95f, 0.95f, 0.97f);
        int k;
        for (k = 0; k < 3; k++) {
            float fx = bx + bs * (0.22f + 0.28f * (float)k);
            hud_disc(fx, by + bs * 0.34f, bs * 0.08f);
            hud_rect(fx - bs * 0.07f, by + bs * 0.45f, bs * 0.14f, bs * 0.30f);
        }
    }

    /* Camera navigator circle (command mode): ring + knob, tilt arrows
     * (up/down), rotate arrows (left/right), red north marker. */
    if (g_mode == MODE_COMMAND) {
        float ncx, ncy, nr;
        nav_circle(&ncx, &ncy, &nr);
        hud_color(0.08f, 0.08f, 0.10f);
        hud_ring(ncx, ncy, nr + 5.0f, 14.0f);
        hud_color(0.85f, 0.88f, 0.92f);
        hud_ring(ncx, ncy, nr, 6.0f);
        float a = nr * 0.78f, w = nr * 0.12f;
        hud_color(0.85f, 0.88f, 0.92f);
        /* up / down (tilt) */
        hud_vert(ncx, ncy - a - w); hud_vert(ncx - w, ncy - a + w * 0.4f); hud_vert(ncx + w, ncy - a + w * 0.4f);
        hud_vert(ncx, ncy + a + w); hud_vert(ncx + w, ncy + a - w * 0.4f); hud_vert(ncx - w, ncy + a - w * 0.4f);
        /* left / right (rotate) */
        hud_color(0.55f, 0.80f, 1.0f);
        hud_vert(ncx - a - w, ncy); hud_vert(ncx - a + w * 0.4f, ncy + w); hud_vert(ncx - a + w * 0.4f, ncy - w);
        hud_vert(ncx + a + w, ncy); hud_vert(ncx + a - w * 0.4f, ncy - w); hud_vert(ncx + a - w * 0.4f, ncy + w);
        /* north marker on the ring */
        float nx = w_sin(-g_yaw), ny = -w_cos(-g_yaw);
        float px = -ny, py = nx;
        hud_color(0.95f, 0.25f, 0.2f);
        hud_vert(ncx + nx * (nr + 22.0f), ncy + ny * (nr + 22.0f));
        hud_vert(ncx + nx * (nr - 4.0f) + px * 14.0f, ncy + ny * (nr - 4.0f) + py * 14.0f);
        hud_vert(ncx + nx * (nr - 4.0f) - px * 14.0f, ncy + ny * (nr - 4.0f) - py * 14.0f);
        /* knob */
        if (g_gs == GS_NAV) hud_color(0.55f, 0.80f, 1.0f); else hud_color(0.92f, 0.92f, 0.95f);
        hud_disc(ncx + g_nav_kx, ncy + g_nav_ky, nr * 0.30f);
    }

    /* Squad roster (bottom left): one figure per living squad member,
     * green when selected. */
    {
        int i, n = 0;
        float fs = W * 0.03f;
        for (i = 0; i < g_un; i++) {
            if (!unit_alive(i) || u_team[i] != TEAM_PLAYER) continue;
            float x = 30.0f + (float)(n % 12) * fs * 1.5f;
            float y = H - 60.0f - fs * 2.2f - (float)(n / 12) * fs * 2.6f;
            if (u_selected[i]) hud_color(0.3f, 1.0f, 0.4f);
            else if (u_archer[i]) hud_color(1.0f, 0.88f, 0.5f);
            else hud_color(0.92f, 0.92f, 0.95f);
            hud_disc(x + fs * 0.5f, y + fs * 0.4f, fs * 0.35f);
            hud_rect(x + fs * 0.15f, y + fs * 0.8f, fs * 0.7f, fs * 1.2f);
            n++;
        }
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

/* Creates a buffer + host-visible coherent memory of `size` bytes. (Mali
 * is unified memory, so host-visible storage buffers are fine for the
 * compute fields too.) */
static int create_host_buffer(VkDevice device, VkPhysicalDeviceMemoryProperties *memProps, VkDeviceSize size,
                              VkFlags usage, VkBuffer *outBuf, VkDeviceMemory *outMem) {
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
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

static void download(VkDevice device, VkDeviceMemory mem, VkDeviceSize offset, void *dst, VkDeviceSize bytes) {
    void *mapped = 0;
    VkResult vr = vkMapMemory(device, mem, offset, bytes, 0, &mapped);
    if (vr != VK_SUCCESS || !mapped) { __android_log_print(4, TAG, "vkMapMemory(read) failed vr=%d", (int)vr); return; }
    memcpy(dst, mapped, (unsigned long)bytes);
    vkUnmapMemory(device, mem);
}

/* ------------------------------------------------------------------ */
/* GPU flow fields: host side of castles_nav.comp                      */
/* ------------------------------------------------------------------ */
#define NAV_ITERS_PER_FRAME 48
#define VK_PIPELINE_STAGE_HOST_BIT_ 0x4000
#define VK_ACCESS_HOST_READ_BIT_    0x2000

typedef struct { int base; int field; int W; int L; } NavPush;

static int g_field_batches[NFIELD];
static long g_field_start_us[NFIELD];
static uint32_t g_changed[NFIELD * 4];

/* Before recording a frame (GPU idle): finish converged fields, upload
 * newly built ones, and clear the "changed" flags for this frame's
 * relaxation batch. Returns 1 if any field needs GPU work this frame. */
static int fields_prepare(VkDevice device, VkDeviceMemory hMem, VkDeviceMemory fMem,
                          VkDeviceMemory dMem, VkDeviceMemory cMem, long nowUs) {
    int f, any = 0;
    VkDeviceSize fieldF = (VkDeviceSize)NAVW_N * 4;
    download(device, cMem, 0, g_changed, sizeof(g_changed));
    for (f = 0; f < NFIELD; f++) {
        if (g_field_state[f] == FS_COMPUTING && g_field_batches[f] > 0 && g_changed[f] == 0u) {
            download(device, dMem, (VkDeviceSize)f * fieldF, g_fd + f * NAVW_N, fieldF);
            g_field_state[f] = FS_READY;
            __android_log_print(4, TAG, "GPU flow field %d converged: %d sweeps, %d ms",
                                f, g_field_batches[f] * NAV_ITERS_PER_FRAME, (int)((nowUs - g_field_start_us[f]) / 1000));
        }
        if (g_field_state[f] == FS_UPLOAD) {
            upload(device, hMem, (VkDeviceSize)f * fieldF, g_fh + f * NAVW_N, fieldF);
            upload(device, fMem, (VkDeviceSize)f * fieldF, g_ff + f * NAVW_N, fieldF);
            upload(device, dMem, (VkDeviceSize)f * fieldF, g_fd + f * NAVW_N, fieldF);
            g_field_state[f] = FS_COMPUTING;
            g_field_batches[f] = 0;
            g_field_start_us[f] = nowUs;
        }
        if (g_field_state[f] == FS_COMPUTING) any = 1;
    }
    for (f = 0; f < NFIELD * 4; f++) g_changed[f] = 0u;
    upload(device, cMem, 0, g_changed, sizeof(g_changed));
    return any;
}

/* Records this frame's relaxation sweeps for every computing field. */
static void fields_record(VkCommandBuffer cmd, VkPipeline pipe, VkPipelineLayout layout, VkDescriptorSet set,
                          VkBuffer dBuf, VkBuffer cBuf) {
    int f, k;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, 0);
    VkBufferMemoryBarrier bb;
    memset(&bb, 0, sizeof(bb));
    bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    bb.srcQueueFamilyIndex = 0xFFFFFFFFu;
    bb.dstQueueFamilyIndex = 0xFFFFFFFFu;
    bb.buffer = dBuf;
    bb.offset = 0;
    bb.size = ~0ull;
    uint32_t groups = (uint32_t)((NAVW_N + 63) / 64);
    for (f = 0; f < NFIELD; f++) {
        if (g_field_state[f] != FS_COMPUTING) continue;
        NavPush np;
        np.base = f * NAVW_N; np.field = f; np.W = NAVW; np.L = NAV_L;
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(np), &np);
        for (k = 0; k < NAV_ITERS_PER_FRAME; k++) {
            vkCmdDispatch(cmd, groups, 1, 1);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, 0, 1, &bb, 0, 0);
        }
        g_field_batches[f] = g_field_batches[f] + 1;
    }
    /* Make the distances + flags visible to the host after the fence. */
    VkBufferMemoryBarrier hb[2];
    memset(hb, 0, sizeof(hb));
    hb[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hb[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hb[0].dstAccessMask = VK_ACCESS_HOST_READ_BIT_;
    hb[0].srcQueueFamilyIndex = 0xFFFFFFFFu;
    hb[0].dstQueueFamilyIndex = 0xFFFFFFFFu;
    hb[0].buffer = dBuf;
    hb[0].size = ~0ull;
    hb[1] = hb[0];
    hb[1].buffer = cBuf;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT_,
        0, 0, 0, 2, hb, 0, 0);
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
            /* Nav grid + terrain heights straight from the generator's own
             * globals: it only writes them while state == 1, and the
             * mutex orders its last writes before these reads. */
            memcpy(g_slot_navh + slot * (NAV_L * NAV_N), g_nav_h, sizeof(float) * NAV_L * NAV_N);
            memcpy(g_slot_navf + slot * (NAV_L * NAV_N), g_nav_f, NAV_L * NAV_N);
            memcpy(g_slot_hgt + slot * (W_GRIDV * W_GRIDV), g_hgt, sizeof(float) * W_GRIDV * W_GRIDV);
            pthread_mutex_unlock(g_gen_mutex);
            g_nav_cache_slot = -1;
            if (g_slot_castle[slot]) units_on_castle_loaded(slot);
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
    if (!create_host_buffer(device, &memProps, (VkDeviceSize)SLOTS * (VkDeviceSize)SLOT_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &worldBuffer, &worldMemory)) return 0;
    VkBuffer hudBuffer = 0;
    VkDeviceMemory hudMemory = 0;
    if (!create_host_buffer(device, &memProps, (VkDeviceSize)HUD_MAX_VERTS * (VkDeviceSize)VERT_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &hudBuffer, &hudMemory)) return 0;
    VkBuffer unitBuffer = 0;
    VkDeviceMemory unitMemory = 0;
    if (!create_host_buffer(device, &memProps, (VkDeviceSize)U_MAX_VERTS * (VkDeviceSize)VERT_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &unitBuffer, &unitMemory)) return 0;
    __android_log_print(4, TAG, "vertex buffers ready (%d slots x %d verts)", SLOTS, SLOT_VERTS);

    /* ---- GPU pathfinding: storage buffers + compute pipeline ---- */
    VkDeviceSize navBytes = (VkDeviceSize)NFIELD * (VkDeviceSize)NAVW_N * 4;
    VkBuffer navHBuf = 0, navFBuf = 0, distBuf = 0, changedBuf = 0;
    VkDeviceMemory navHMem = 0, navFMem = 0, distMem = 0, changedMem = 0;
    if (!create_host_buffer(device, &memProps, navBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &navHBuf, &navHMem)) return 0;
    if (!create_host_buffer(device, &memProps, navBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &navFBuf, &navFMem)) return 0;
    if (!create_host_buffer(device, &memProps, navBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &distBuf, &distMem)) return 0;
    if (!create_host_buffer(device, &memProps, (VkDeviceSize)sizeof(g_changed), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &changedBuf, &changedMem)) return 0;

    VkDescriptorSetLayoutBinding navBindings[4];
    memset(navBindings, 0, sizeof(navBindings));
    for (i = 0; i < 4; i++) {
        navBindings[i].binding = i;
        navBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        navBindings[i].descriptorCount = 1;
        navBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dslInfo;
    memset(&dslInfo, 0, sizeof(dslInfo));
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 4;
    dslInfo.pBindings = navBindings;
    VkDescriptorSetLayout navDsl = 0;
    vr = vkCreateDescriptorSetLayout(device, &dslInfo, 0, &navDsl);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "nav dsl failed vr=%d", (int)vr); return 0; }

    VkDescriptorPoolSize poolSize;
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 4;
    VkDescriptorPoolCreateInfo dpInfo;
    memset(&dpInfo, 0, sizeof(dpInfo));
    dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.maxSets = 1;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = &poolSize;
    VkDescriptorPool navPool = 0;
    vr = vkCreateDescriptorPool(device, &dpInfo, 0, &navPool);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "nav pool failed vr=%d", (int)vr); return 0; }

    VkDescriptorSetAllocateInfo dsAlloc;
    memset(&dsAlloc, 0, sizeof(dsAlloc));
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = navPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &navDsl;
    VkDescriptorSet navSet = 0;
    vr = vkAllocateDescriptorSets(device, &dsAlloc, &navSet);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "nav set alloc failed vr=%d", (int)vr); return 0; }

    VkDescriptorBufferInfo navBufInfo[4];
    navBufInfo[0].buffer = navHBuf;    navBufInfo[0].offset = 0; navBufInfo[0].range = navBytes;
    navBufInfo[1].buffer = navFBuf;    navBufInfo[1].offset = 0; navBufInfo[1].range = navBytes;
    navBufInfo[2].buffer = distBuf;    navBufInfo[2].offset = 0; navBufInfo[2].range = navBytes;
    navBufInfo[3].buffer = changedBuf; navBufInfo[3].offset = 0; navBufInfo[3].range = sizeof(g_changed);
    VkWriteDescriptorSet navWrites[4];
    memset(navWrites, 0, sizeof(navWrites));
    for (i = 0; i < 4; i++) {
        navWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        navWrites[i].dstSet = navSet;
        navWrites[i].dstBinding = i;
        navWrites[i].descriptorCount = 1;
        navWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        navWrites[i].pBufferInfo = &navBufInfo[i];
    }
    vkUpdateDescriptorSets(device, 4, navWrites, 0, 0);

    VkPushConstantRange navPcRange;
    navPcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    navPcRange.offset = 0;
    navPcRange.size = sizeof(NavPush);
    VkPipelineLayoutCreateInfo navPlInfo;
    memset(&navPlInfo, 0, sizeof(navPlInfo));
    navPlInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    navPlInfo.setLayoutCount = 1;
    navPlInfo.pSetLayouts = &navDsl;
    navPlInfo.pushConstantRangeCount = 1;
    navPlInfo.pPushConstantRanges = &navPcRange;
    VkPipelineLayout navLayout = 0;
    vr = vkCreatePipelineLayout(device, &navPlInfo, 0, &navLayout);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "nav layout failed vr=%d", (int)vr); return 0; }

    VkShaderModuleCreateInfo csInfo;
    memset(&csInfo, 0, sizeof(csInfo));
    csInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    csInfo.codeSize = sizeof(g_castles_nav_spv);
    csInfo.pCode = g_castles_nav_spv;
    VkShaderModule csModule = 0;
    vr = vkCreateShaderModule(device, &csInfo, 0, &csModule);
    if (vr != VK_SUCCESS) { __android_log_print(4, TAG, "nav shader module failed vr=%d", (int)vr); return 0; }

    VkComputePipelineCreateInfo cpInfo2;
    memset(&cpInfo2, 0, sizeof(cpInfo2));
    cpInfo2.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo2.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpInfo2.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpInfo2.stage.module = csModule;
    cpInfo2.stage.pName = "main";
    cpInfo2.layout = navLayout;
    cpInfo2.basePipelineIndex = -1;
    VkPipeline navPipeline = 0;
    vr = vkCreateComputePipelines(device, 0, 1, &cpInfo2, 0, &navPipeline);
    __android_log_print(4, TAG, "nav compute pipeline vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;
    /* A new session has fresh GPU buffers: re-run unfinished fields. */
    for (i = 0; i < NFIELD; i++) if (g_field_state[i] == FS_COMPUTING) g_field_state[i] = FS_UPLOAD;

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
    units_init();
    if (g_cam[0] == 0.0f && g_cam[1] == 0.0f && g_cam[2] == 0.0f) {
        /* First session: ballistics self-test (compare with the host
         * checker's "ballistics:" line). Runs here, on the render thread,
         * because the solver's scratch body belongs to it. */
        int bt[16];
        int bn = bal_selftest(bt);
        int b;
        for (b = 0; b < bn; b++) __android_log_print(4, TAG, "ballistics[%d]=%d", b, bt[b]);
        choose_spawn();
    }

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
        int navWork = fields_prepare(device, navHMem, navFMem, distMem, changedMem, tNow);
        units_update(dt);
        int unitVerts = units_build_mesh();
        upload(device, unitMemory, 0, g_ustage, (VkDeviceSize)unitVerts * (VkDeviceSize)VERT_BYTES);
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

        if (navWork) fields_record(cmd, navPipeline, navLayout, navSet, distBuf, changedBuf);

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
        int q;
        for (q = 0; q < 16; q++) g_vp[q] = pc.mvp[q];
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

        /* Units (same lit pipeline + world push constants). */
        if (unitVerts > 0) {
            VkDeviceSize uoff = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &unitBuffer, &uoff);
            vkCmdDraw(cmd, (uint32_t)unitVerts, 1, 0, 0);
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
            int alive = 0, sqL0 = 0, sqL1 = 0, sqMove = 0, sqWait = 0, sqIdle = 0, sqFight = 0;
            float sqx = 0.0f, sqz = 0.0f;
            for (s = 0; s < g_un; s++) {
                if (!unit_alive(s)) continue;
                alive = alive + 1;
                if (u_team[s] != TEAM_PLAYER) continue;
                if (u_layer[s]) sqL1 = sqL1 + 1; else sqL0 = sqL0 + 1;
                if (u_state[s] == US_MOVE || u_state[s] == US_CLIMB) sqMove = sqMove + 1;
                else if (u_state[s] == US_WAIT) sqWait = sqWait + 1;
                else if (u_state[s] == US_IDLE) sqIdle = sqIdle + 1;
                else sqFight = sqFight + 1;
                float nsx = sqx + u_x[s], nsz = sqz + u_z[s];
                sqx = nsx; sqz = nsz;
            }
            int sqn = sqL0 + sqL1;
            if (sqn > 0) __android_log_print(4, TAG, "squad at %d,%d: moving %d waiting %d idle %d fighting %d; arrows fired %d hit %d",
                                             (int)(sqx / (float)sqn), (int)(sqz / (float)sqn), sqMove, sqWait, sqIdle, sqFight,
                                             g_arrows_fired, g_arrows_hit);
            __android_log_print(4, TAG, "fps=%d chunks=%d drawn=%d cam=%d,%d,%d castles=%d units=%d squad ground/wall=%d/%d verts=%d",
                                (int)((long)frames * 1000000 / (tNow - tLog)), loaded, drawn,
                                (int)g_cam[0], (int)g_cam[1], (int)g_cam[2], g_ndisc, alive, sqL0, sqL1, unitVerts);
            frames = 0;
            tLog = tNow;
        }
    }

    /* Session teardown (surface going away). */
    vkDeviceWaitIdle(device);
    vkDestroyPipeline(device, navPipeline, 0);
    vkDestroyShaderModule(device, csModule, 0);
    vkDestroyPipelineLayout(device, navLayout, 0);
    vkDestroyDescriptorPool(device, navPool, 0);
    vkDestroyDescriptorSetLayout(device, navDsl, 0);
    vkDestroyBuffer(device, navHBuf, 0);
    vkFreeMemory(device, navHMem, 0);
    vkDestroyBuffer(device, navFBuf, 0);
    vkFreeMemory(device, navFMem, 0);
    vkDestroyBuffer(device, distBuf, 0);
    vkFreeMemory(device, distMem, 0);
    vkDestroyBuffer(device, changedBuf, 0);
    vkFreeMemory(device, changedMem, 0);
    vkDestroyBuffer(device, unitBuffer, 0);
    vkFreeMemory(device, unitMemory, 0);
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
 * missing. This game has no text entry -- the EditText is only used to
 * find where the surface sits on screen. */
void nativeEditTextReady(void *env, void *clazz, void *editText) {
    (void)clazz;
    /* Kept only to locate the surface on screen (touch Y offset). */
    g_edit_text = sqw_jni_NewGlobalRef(env, (jobject)editText);
    jclass cls = sqw_jni_GetObjectClass(env, g_edit_text);
    g_m_getlocationonscreen = sqw_jni_GetMethodID(env, cls, "getLocationOnScreen", "([I)V");
}

void nativeTextChanged(void *env, void *clazz, void *text) {
    (void)env; (void)clazz; (void)text;
}
