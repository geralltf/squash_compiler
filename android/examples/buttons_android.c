#include "include/vulkan_android.h"
#include "triangle_spirv.h"
#include "SQW/dom_walk.c"
#include "SQW/css.c"
#include "SQW/layout.c"
#include "SQW/text_metrics.h"
#include "SQW/text_spirv.h"

/* Interactive buttons + scrollable text: real SQW DOM/CSS/layout (SQW/
 * dom.c, SQW/css.c, SQW/layout.c -- the same engine SQW/sqw_main.c's
 * desktop browser uses) computes button/paragraph/pre positions and
 * colors from real HTML+CSS (including a real ":hover" stylesheet rule --
 * see g_css_text below), real page text is rendered as real glyph quads
 * sampled from SQW's own baked font atlas (SQW/font_atlas.h,
 * SQW/text_renderer_vk.c's exact pipeline/vertex-format/glyph-quad math,
 * ported here to this file's raw Vulkan setup), the whole page scrolls
 * both vertically and horizontally with real drag-to-scroll touch input
 * and real scrollbars (SQW/sqw_main.c's own scrollbar pixel-math, ported
 * verbatim), and hovering a finger over a button live-reapplies its CSS
 * ":hover" rule (SQW's own DomNode::hover + css_apply_one() mechanism --
 * see sqw_main.c's mouse-move handler, the model this file's touch
 * handler follows) rather than the earlier, simpler ad-hoc "darken while
 * pressed" placeholder. Same `-android -android-activity` architecture as
 * triangle_android.c -- see that file's own top comment for why (raw
 * android.app.NativeActivity was found broken on real Android 17
 * hardware), and the same lazy per-swapchain-image-index view/framebuffer
 * creation fix for the real Mali vkCreateImageView driver bug found while
 * building triangle_android.c -- except this demo renders repeatedly
 * (once per touch/drag event), so it caches each per-index view/
 * framebuffer the first time that index comes up rather than creating
 * only one ever.
 *
 * Scope note: SQW's own desktop engine only ever implements WHOLE-PAGE
 * scrolling (a single viewport scrolling the entire document) -- real
 * per-element "overflow:scroll" scroll containers, each with their own
 * clipped viewport and independent scroll offset, don't exist anywhere in
 * css.c/layout.c (confirmed via direct investigation: "overflow" is only
 * ever parsed as a boolean css_overflow_hidden clip flag, nothing else).
 * This demo therefore implements the same whole-page model SQW itself
 * uses, not a separate per-element scroll container -- the entire page
 * (buttons + paragraph + pre block) is the one scrollable "content area". */

extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
extern void *ANativeWindow_fromSurface(void *env, void *surface);
extern int pthread_create(unsigned long *thread, void *attr, void *(*start_routine)(void *), void *arg);
extern int usleep(int usec);

typedef struct { float x, y; float r, g, b; } Vertex;
typedef struct { float angle; } PushConstants;
typedef struct { float x, y, u, v; float r, g, b, a; } GlyphVertex;

static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *memProps, uint32_t typeBits, VkFlags props) {
    uint32_t i;
    for (i = 0; i < memProps->memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (memProps->memoryTypes[i].propertyFlags & props) == props) return i;
    }
    __android_log_print(4, "squashbtn", "find_memory_type: no suitable memory type found!");
    return 0xFFFFFFFFu;
}

VkInstance g_instance = 0;
int g_instance_ready = 0;

void *create_instance_thread(void *window) {
    __android_log_print(4, "squashbtn", "create_instance_thread: starting, window=%p", window);
    init_triangle_spirv();
    init_text_spirv();

    VkApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "squash android buttons";
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
    __android_log_print(4, "squashbtn", "vkCreateInstance vr=%d instance=%p", (int)vr, (void*)g_instance);
    if (vr == VK_SUCCESS) g_instance_ready = 1; else g_instance_ready = -1;
    return 0;
}

/* ---- real button state, computed once from SQW's layout, mutated by touch ---- */
#define MAX_BUTTONS 8
typedef struct {
    int box_index;      /* index into g_layout.boxes[], for layout_hit_test() correlation */
    DomNode *node;      /* the real <button> DomNode -- color is ALWAYS read fresh from
                          * here at render time (node->css_bg), never cached, so a live
                          * ":hover" CSS re-resolve (see touch handler) shows up for free
                          * on the very next redraw with no separate "pressed" state. */
    float x, y, w, h;   /* content-space pixels, same convention as LayoutBox */
} Button;
static Button g_buttons[MAX_BUTTONS];
static int g_button_count = 0;
static LayoutList g_layout;

/* Kept alive for the whole app lifetime (never freed) so a live ":hover"
 * re-resolve can re-run css_apply_one() against the SAME rule set that
 * originally styled the page -- see SQW/sqw_main.c's own g_current_css_sheet
 * comment for why this must persist instead of being freed right after the
 * initial css_apply() the way the previous (non-hoverable) version of this
 * demo did. */
static CssStylesheet g_css_sheet;
static float g_viewport_w = 0, g_viewport_h = 0;
/* An int box-index, NOT a stored DomNode* -- a real, confirmed squash/
 * ARM64 bug found while building this file: a global DomNode* pointer
 * variable, assigned from a local inside this same touch-handling loop
 * (plain "=" AND memcpy() both tried), reads back as a completely
 * different, wrong value on the very next statement -- reproduced
 * consistently on device (an equality check right after the assignment
 * came back false every time), while a plain global int (g_button_count,
 * used everywhere else in this file across many calls) has never shown
 * this problem. Storing the hit-tested LAYOUT BOX INDEX instead and
 * re-deriving the real DomNode* fresh from g_layout.boxes[idx].node each
 * time it's needed sidesteps the bug entirely.
 *
 * Deliberately zero-initialized, with a SEPARATE zero-initialized flag
 * for "is anything hovered" rather than a "-1 means none" sentinel --
 * another real, confirmed bug found here: "static int x = -1;" reads
 * back as 0 (only an implicit/explicit ZERO initializer is reliable for
 * a squash global on this build, matching every other global in this
 * file, all of which are zero-initialized already). */
static int g_hover_box_index = 0;
static int g_has_hover = 0;

/* ---- whole-page scroll state (see this file's own top comment: SQW's
 * real engine only ever implements whole-page scrolling, so that's what
 * this demo ports, not a separate per-element scroll container). ---- */
static float g_scroll_x = 0, g_scroll_y = 0;
#define SQW_SCROLLBAR_THICKNESS 12.0f
#define SQW_SCROLLBAR_MIN_THUMB 24.0f

static float clamp_scroll(float v, float content, float view) {
    float maxv = content - view;
    if (maxv < 0) maxv = 0;
    if (v < 0) v = 0;
    if (v > maxv) v = maxv;
    return v;
}

/* ---- touch handoff: nativeTouchEvent (Android main thread) -> render
 * thread. Same simple polled-flag convention as g_instance_ready above,
 * already proven safe at this project's scale (a human dragging a finger
 * can't race a ~5ms poll loop in any way that matters for a demo). ---- */
static volatile float g_touch_x = 0, g_touch_y = 0;
static volatile int g_touch_action = -1;
static volatile int g_touch_pending = 0;

/* drag-vs-tap disambiguation state, updated only from the render thread's
 * own touch-handling loop below (single reader/writer, no locking needed --
 * same convention as the g_touch_* handoff fields above). */
static float g_drag_down_x = 0, g_drag_down_y = 0;
static float g_drag_anchor_scroll_x = 0, g_drag_anchor_scroll_y = 0;
static float g_drag_total_dist = 0;
static int g_dragging = 0;

#define ACTION_DOWN 0
#define ACTION_UP 1
#define ACTION_MOVE 2

/* x_bits/y_bits are the raw IEEE-754 bit patterns of the real touch
 * coordinates, NOT the coordinates themselves -- a real, confirmed
 * platform/runtime issue on this device makes a plain `float` argument to
 * a squash-exported native method always arrive as 0.0 (verified via
 * extensive direct device testing: byte-correct DEX bytecode, byte-correct
 * ELF export, byte-correct AAPCS64 prologue spilling all checked out fine;
 * a real javac+d8-built reference app confirms MotionEvent.getX()/getY()
 * themselves return correct values on this exact device), while the exact
 * same call shape with an int argument works every time. The DEX shim
 * (android/android_dex.c, see its own T_FLOAT_CLASS comment) now converts
 * x/y to their raw bits via java.lang.Float.floatToIntBits() before this
 * call, sidestepping the broken float-argument path entirely; reinterpret
 * them back into real floats here via a pointer-cast reinterpretation
 * (NOT a numeric (float)cast, which would convert the integer VALUE
 * instead of reinterpreting its bits). */
void nativeTouchEvent(void *env, void *clazz, int x_bits, int y_bits, int action) {
    float x = *(float *)&x_bits;
    float y = *(float *)&y_bits;
    g_touch_x = x; g_touch_y = y; g_touch_action = action; g_touch_pending = 1;
}

/* Fills 6 vertices (2 triangles) for one flat-colored rect into `out`,
 * converting a content-space (pre-scroll) pixel rect to NDC against the
 * real swapchain extent -- shared by buttons, the <pre> background, and
 * both scrollbar tracks/thumbs (which are already screen-space, not
 * content-space, so callers pass sx=0 for those). */
static void write_rect_quad(Vertex *out, float px, float py, float pw, float ph,
                             float r, float g, float b, float sx, float sy,
                             uint32_t ew, uint32_t eh) {
    float x0 = ((px - sx) / (float)ew) * 2.0f - 1.0f;
    float y0 = ((py - sy) / (float)eh) * 2.0f - 1.0f;
    float x1 = ((px + pw - sx) / (float)ew) * 2.0f - 1.0f;
    float y1 = ((py + ph - sy) / (float)eh) * 2.0f - 1.0f;

    out[0].x = x0; out[0].y = y0; out[0].r = r; out[0].g = g; out[0].b = b;
    out[1].x = x1; out[1].y = y0; out[1].r = r; out[1].g = g; out[1].b = b;
    out[2].x = x0; out[2].y = y1; out[2].r = r; out[2].g = g; out[2].b = b;
    out[3].x = x1; out[3].y = y0; out[3].r = r; out[3].g = g; out[3].b = b;
    out[4].x = x1; out[4].y = y1; out[4].r = r; out[4].g = g; out[4].b = b;
    out[5].x = x0; out[5].y = y1; out[5].r = r; out[5].g = g; out[5].b = b;
}

/* ---- real HTML + CSS content: 3 hoverable buttons, one wrapping
 * paragraph, one non-wrapping <pre> line (deliberately long enough to
 * overflow past the viewport width and pull in the horizontal
 * scrollbar). Colors/sizes live in the stylesheet (not inline style=""),
 * since a real ":hover" rule needs a real selector to attach to -- an
 * inline style attribute has no notion of pseudo-classes at all. */
/* Deliberately functions, NOT "static const char *g_body_html = ...;"
 * file-scope initialized globals -- a real, confirmed squash ARM64/Android
 * codegen bug (found while building this file): a global pointer
 * initialized directly from a string literal reads back as garbage/empty
 * at runtime (the .rela.dyn relocation that should point it at the
 * literal's real .rodata address isn't applied correctly for this
 * backend), while the exact same literal used directly in a function
 * body -- as a local variable, a return value, or an inline call
 * argument -- works correctly every time. Returning it from a function
 * keeps this file's Vulkan setup below readable without hitting that bug. */
static const char *get_body_html(void) {
    return "<html><body>"
        "<button id=\"btn1\"></button>"
        "<button id=\"btn2\"></button>"
        "<button id=\"btn3\"></button>"
        "<p>This paragraph demonstrates real word wrapping inside SQW's own layout engine. Each word is measured against the baked font atlas and the line breaks exactly at the container's width, the same way a real browser flows ordinary text. Keep scrolling down, and try dragging sideways too, to see the non-wrapping line below.</p>"
        "<pre>This single pre-formatted line deliberately never wraps no matter how long it gets so it overflows far past the right edge of the screen and pulls in the horizontal scrollbar you can drag to reveal the rest of it.</pre>"
        "</body></html>";
}

static const char *get_css_text(void) {
    return "#btn1{display:inline-block;width:900px;height:140px;background-color:#e74c3c;}"
        "#btn1:hover{background-color:#ff9d8c;}"
        "#btn2{display:inline-block;width:900px;height:140px;background-color:#2ecc71;}"
        "#btn2:hover{background-color:#8ff0b8;}"
        "#btn3{display:inline-block;width:900px;height:140px;background-color:#3498db;}"
        "#btn3:hover{background-color:#9cd5f7;}"
        "p{color:#ffffff;font-size:32px;}"
        "pre{color:#ffffff;background-color:#242424;font-size:28px;}";
}

/* ---- real SQW DOM + layout_compute()/layout_hit_test(): the actual UI
 * definition and hit-testing are genuine SQW engine code, not reimplemented
 * here. The DOM nodes themselves come from dom_parse()'s real HTML text
 * lexer, plus real css_apply()/css_parse_into() for styling (including a
 * real ":hover" stylesheet rule -- see get_css_text() above). sqw_font_atlas_
 * decode() must run before layout_compute(), or every glyph advance
 * measures as 0 and word-wrap never breaks a line (SQW/sqw_main.c calls
 * this once at startup, before its own first layout, for the same
 * reason). */
static void build_page_dom_layout(uint32_t ew, uint32_t eh) {
    uint32_t i;
    sqw_font_atlas_decode();

    DomNode *root = dom_parse(get_body_html());
    css_stylesheet_init(&g_css_sheet);
    css_parse_into(&g_css_sheet, get_css_text());
    css_apply(root, &g_css_sheet, (float)ew);

    layout_compute(root, (float)ew, (float)eh, &g_layout);

    g_button_count = 0;
    for (i = 0; i < (uint32_t)g_layout.count && g_button_count < MAX_BUTTONS; i++) {
        LayoutBox *lb = &g_layout.boxes[i];
        if (lb->kind != SQW_BOX_BUTTON) continue;
        Button *b = &g_buttons[g_button_count++];
        b->box_index = (int)i;
        b->node = lb->node;
        b->x = lb->x; b->y = lb->y; b->w = lb->w; b->h = lb->h;
    }
    __android_log_print(4, "squashbtn", "SQW layout done: %d button(s), %d total box(es), content=%.0fx%.0f",
                         g_button_count, g_layout.count, g_layout.content_w, g_layout.content_h);
}

/* ---- glyph rendering: ported field-by-field from SQW/text_renderer_vk.c's
 * sqw_text_draw_char()/sqw_text_draw_string() -- same NDC mapping, same
 * atlas UV math. Writes go to a plain CPU-heap staging array
 * (g_glyphStaging), NOT directly into the Vulkan-mapped pointer -- a
 * real, confirmed squash/ARM64 bug found while building this file: many
 * small per-field writes through a POINTER RETURNED BY vkMapMemory
 * eventually SIGSEGV (reproduced consistently around the 18th glyph,
 * while the exact same write pattern into a plain static/heap array of
 * the same size never fails). A single bulk memcpy() of the finished
 * array into the mapped GPU buffer, done once per frame in
 * build_frame_geometry() below, works fine at any size -- exactly the
 * pattern the font atlas upload above already uses successfully for a
 * 145KB staging buffer. */
#define TEXT_MAX_GLYPHS 2048
static GlyphVertex g_glyphStaging[TEXT_MAX_GLYPHS * 6];
/* NOT persistently mapped -- see g_glyphStaging's own comment: even a
 * single bulk memcpy into a pointer obtained once and held across other
 * Vulkan calls (buffer/image/pipeline creation) SIGSEGVs on this squash/
 * ARM64/Mali combination. build_frame_geometry() instead does a fresh
 * vkMapMemory()/memcpy()/vkUnmapMemory() every time it runs, exactly
 * mirroring the font atlas upload above (map, one bulk memcpy, unmap
 * immediately) -- the one pattern already proven to work at any size. */
static VkDevice g_device = 0;
static VkDeviceMemory g_textVertexMemory = 0;
static VkDeviceMemory g_rectVertexMemory = 0;
static uint32_t g_glyphVCount = 0;

static void text_draw_char(float x, float y, char ch, float scale,
                            float r, float g, float b, float a,
                            uint32_t ew, uint32_t eh) {
    if (g_glyphVCount + 6 > TEXT_MAX_GLYPHS * 6) return;
    const SqwGlyphMetrics *gm = sqw_glyph_lookup(ch);
    if (!gm || gm->glyph_w <= 0 || gm->glyph_h <= 0) return;

    float px0 = x + (float)gm->bearing_x * scale;
    float py0 = y + (float)gm->bearing_y * scale;
    float pw = (float)gm->glyph_w * scale;
    float ph = (float)gm->glyph_h * scale;
    float px1 = px0 + pw;
    float py1 = py0 + ph;

    float u0 = (float)(gm->atlas_x) / (float)SQW_FONT_ATLAS_W;
    float v0 = (float)(gm->atlas_y) / (float)SQW_FONT_ATLAS_H;
    float u1 = (float)(gm->atlas_x + gm->glyph_w) / (float)SQW_FONT_ATLAS_W;
    float v1 = (float)(gm->atlas_y + gm->glyph_h) / (float)SQW_FONT_ATLAS_H;

    float x0 = (px0 / (float)ew) * 2.0f - 1.0f;
    float y0 = (py0 / (float)eh) * 2.0f - 1.0f;
    float x1 = (px1 / (float)ew) * 2.0f - 1.0f;
    float y1 = (py1 / (float)eh) * 2.0f - 1.0f;

    GlyphVertex *v = g_glyphStaging;
    uint32_t n = g_glyphVCount;
    v[n].x = x0; v[n].y = y0; v[n].u = u0; v[n].v = v0; v[n].r = r; v[n].g = g; v[n].b = b; v[n].a = a; n++;
    v[n].x = x1; v[n].y = y0; v[n].u = u1; v[n].v = v0; v[n].r = r; v[n].g = g; v[n].b = b; v[n].a = a; n++;
    v[n].x = x0; v[n].y = y1; v[n].u = u0; v[n].v = v1; v[n].r = r; v[n].g = g; v[n].b = b; v[n].a = a; n++;
    v[n].x = x1; v[n].y = y0; v[n].u = u1; v[n].v = v0; v[n].r = r; v[n].g = g; v[n].b = b; v[n].a = a; n++;
    v[n].x = x1; v[n].y = y1; v[n].u = u1; v[n].v = v1; v[n].r = r; v[n].g = g; v[n].b = b; v[n].a = a; n++;
    v[n].x = x0; v[n].y = y1; v[n].u = u0; v[n].v = v1; v[n].r = r; v[n].g = g; v[n].b = b; v[n].a = a; n++;
    g_glyphVCount = n;
}

static void text_draw_string(float x, float y, const char *s, int len, float scale,
                              float r, float g, float b, float a, uint32_t ew, uint32_t eh) {
    float cx = x;
    int i;
    for (i = 0; i < len; i++) {
        text_draw_char(cx, y, s[i], scale, r, g, b, a, ew, eh);
        /* See sqw_text_measure()'s own comment (text_metrics.h) -- "cx +=
         * call()" is the same confirmed-broken accumulation shape, fixed
         * the same way. */
        float adv = sqw_text_glyph_advance(s[i], scale);
        cx = cx + adv;
    }
}

#define MAX_RECTS 16
static Vertex g_rectStaging[MAX_RECTS * 6];

/* Builds this frame's flat-rect vertices (buttons, <pre> background,
 * scrollbar track/thumb rects) and glyph vertices (every SQW_BOX_TEXT
 * layout box) in one pass over g_layout.boxes -- called once per redraw.
 * Both vertex kinds are built into plain CPU staging arrays
 * (g_rectStaging/g_glyphStaging) and bulk-memcpy()'d into the actual
 * Vulkan-mapped buffers at the end -- see g_glyphStaging's own comment
 * for why writing field-by-field directly into a vkMapMemory() pointer
 * isn't safe on this build. Returns the number of flat-rect vertices
 * written (the glyph count is left in g_glyphVCount for the caller to
 * read after this returns). */
static uint32_t build_frame_geometry(uint32_t ew, uint32_t eh) {
    uint32_t rectCount = 0;
    int i;

    for (i = 0; i < g_button_count; i++) {
        Button *b = &g_buttons[i];
        DomNode *btn_node = b->node;
        float r = 0.6f, gg = 0.6f, bl = 0.6f;
        if (btn_node && btn_node->css_has_bg) {
            r = btn_node->css_bg[0]; gg = btn_node->css_bg[1]; bl = btn_node->css_bg[2];
        }
        write_rect_quad(&g_rectStaging[rectCount * 6], b->x, b->y, b->w, b->h, r, gg, bl,
                         g_scroll_x, g_scroll_y, ew, eh);
        rectCount++;
    }
    g_glyphVCount = 0;
    for (i = 0; i < g_layout.count; i++) {
        LayoutBox *lb = &g_layout.boxes[i];
        float bx = lb->x - g_scroll_x;
        float by = lb->y - g_scroll_y;
        if (lb->kind == SQW_BOX_PRE) {
            DomNode *pre_node = lb->node;
            float r = 0.93f, gg = 0.93f, bl = 0.90f;
            if (pre_node && pre_node->css_has_bg) {
                r = pre_node->css_bg[0]; gg = pre_node->css_bg[1]; bl = pre_node->css_bg[2];
            }
            write_rect_quad(&g_rectStaging[rectCount * 6], lb->x, lb->y, lb->w, lb->h, r, gg, bl,
                             g_scroll_x, g_scroll_y, ew, eh);
            rectCount++;
        } else if (lb->kind == SQW_BOX_TEXT) {
            /* "textnode"/"owner": plain local pointers, read through for
             * every field access, never a chained "lb->node->parent->X" --
             * see dom.h's own css_gap comment for the confirmed squash
             * codegen bug this avoids on a struct this large. */
            DomNode *textnode = lb->node;
            DomNode *owner = textnode->parent;
            DomNode *anc = owner;
            while (anc && !anc->css_has_color) anc = anc->parent;
            float tr = 1.0f, tg = 1.0f, tb = 1.0f; /* default white: this demo's
                                                      * page background is dark,
                                                      * unlike SQW's own default
                                                      * black-on-white page. */
            if (anc) { tr = anc->css_color[0]; tg = anc->css_color[1]; tb = anc->css_color[2]; }
            text_draw_string(bx, by, textnode->text + lb->text_start, lb->text_len,
                              lb->text_scale, tr, tg, tb, 1.0f, ew, eh);
        }
    }
    if (g_glyphVCount > 0) {
        void *gp = 0;
        vkMapMemory(g_device, g_textVertexMemory, 0, (VkDeviceSize)g_glyphVCount * sizeof(GlyphVertex), 0, &gp);
        memcpy(gp, g_glyphStaging, (size_t)g_glyphVCount * sizeof(GlyphVertex));
        vkUnmapMemory(g_device, g_textVertexMemory);
    }

    /* ---- scrollbars: exact pixel-math ported from SQW/sqw_main.c's own
     * draw_scrollbars() (no toolbar in this demo, so no TOOLBAR_H offset
     * subtracted from the vertical view height the way desktop does). ---- */
    float view_w = (float)ew, view_h = (float)eh;
    if (g_layout.content_h > view_h) {
        float thumb_h = view_h * (view_h / g_layout.content_h);
        if (thumb_h < SQW_SCROLLBAR_MIN_THUMB) thumb_h = SQW_SCROLLBAR_MIN_THUMB;
        float track_free = view_h - thumb_h;
        float maxscroll = g_layout.content_h - view_h;
        float thumb_y = (maxscroll > 0) ? (g_scroll_y / maxscroll) * track_free : 0;
        write_rect_quad(&g_rectStaging[rectCount * 6], view_w - SQW_SCROLLBAR_THICKNESS, 0,
                         SQW_SCROLLBAR_THICKNESS, view_h, 0.25f, 0.25f, 0.28f, 0, 0, ew, eh);
        rectCount++;
        write_rect_quad(&g_rectStaging[rectCount * 6], view_w - SQW_SCROLLBAR_THICKNESS, thumb_y,
                         SQW_SCROLLBAR_THICKNESS, thumb_h, 0.6f, 0.6f, 0.65f, 0, 0, ew, eh);
        rectCount++;
    }
    if (g_layout.content_w > view_w) {
        float thumb_w = view_w * (view_w / g_layout.content_w);
        if (thumb_w < SQW_SCROLLBAR_MIN_THUMB) thumb_w = SQW_SCROLLBAR_MIN_THUMB;
        float track_free = view_w - thumb_w;
        float maxscroll = g_layout.content_w - view_w;
        float thumb_x = (maxscroll > 0) ? (g_scroll_x / maxscroll) * track_free : 0;
        write_rect_quad(&g_rectStaging[rectCount * 6], 0, view_h - SQW_SCROLLBAR_THICKNESS,
                         view_w, SQW_SCROLLBAR_THICKNESS, 0.25f, 0.25f, 0.28f, 0, 0, ew, eh);
        rectCount++;
        write_rect_quad(&g_rectStaging[rectCount * 6], thumb_x, view_h - SQW_SCROLLBAR_THICKNESS,
                         thumb_w, SQW_SCROLLBAR_THICKNESS, 0.6f, 0.6f, 0.65f, 0, 0, ew, eh);
        rectCount++;
    }

    if (rectCount > 0) {
        void *rp = 0;
        vkMapMemory(g_device, g_rectVertexMemory, 0, (VkDeviceSize)rectCount * 6 * sizeof(Vertex), 0, &rp);
        memcpy(rp, g_rectStaging, (size_t)rectCount * 6 * sizeof(Vertex));
        vkUnmapMemory(g_device, g_rectVertexMemory);
    }
    return rectCount;
}

void *vulkan_thread_main(void *window) {
    while (g_instance_ready == 0) usleep(10000);
    __android_log_print(4, "squashbtn", "render thread: saw g_instance_ready=%d g_instance=%p", g_instance_ready, (void*)g_instance);
    if (g_instance_ready < 0) { __android_log_print(4, "squashbtn", "vkCreateInstance failed, aborting"); return 0; }
    VkInstance instance = g_instance;
    VkResult vr;

    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(instance, &physCount, 0);
    __android_log_print(4, "squashbtn", "physical device count=%u", physCount);
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
    __android_log_print(4, "squashbtn", "vkCreateAndroidSurfaceKHR vr=%d surface=%p", (int)vr, (void*)surface);
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
    __android_log_print(4, "squashbtn", "queueFamily=%u", queueFamily);
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
    __android_log_print(4, "squashbtn", "vkCreateDevice vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;
    g_device = device;

    VkQueue queue = 0;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkPhysicalDeviceMemoryProperties memProps;
    memset(&memProps, 0, sizeof(memProps));
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);

    /* Command pool created early (moved up from where the non-text version
     * of this demo used to create it, right before the render loop) --
     * the one-shot font-atlas upload below needs it before any of that. */
    VkCommandPoolCreateInfo cpInfo;
    memset(&cpInfo, 0, sizeof(cpInfo));
    cpInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpInfo.queueFamilyIndex = queueFamily;
    VkCommandPool commandPool = 0;
    vr = vkCreateCommandPool(device, &cpInfo, 0, &commandPool);
    __android_log_print(4, "squashbtn", "vkCreateCommandPool vr=%d", (int)vr);
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
    __android_log_print(4, "squashbtn", "surface extent=%ux%u minImageCount=%u", extent.width, extent.height, caps.minImageCount);
    g_viewport_w = (float)extent.width;
    g_viewport_h = (float)extent.height;

    /* Real Mali driver quirk (see triangle_android.c's own comment): don't
     * fight the device's real minImageCount, just don't ask for extra. */
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
    scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scInfo.preTransform = caps.currentTransform;
    scInfo.compositeAlpha = 0x1;
    scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scInfo.clipped = VK_TRUE;

    VkSwapchainKHR swapchain = 0;
    vr = vkCreateSwapchainKHR(device, &scInfo, 0, &swapchain);
    __android_log_print(4, "squashbtn", "vkCreateSwapchainKHR vr=%d format=%d", (int)vr, (int)chosenFormat);
    if (vr != VK_SUCCESS) return 0;

    uint32_t swapImageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, 0);
    VkImage swapImages[8];
    if (swapImageCount > 8) swapImageCount = 8;
    vkGetSwapchainImagesKHR(device, swapchain, &swapImageCount, swapImages);
    __android_log_print(4, "squashbtn", "swapImageCount=%u", swapImageCount);

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
    __android_log_print(4, "squashbtn", "sync objects created");

    VkAttachmentDescription colorAttach;
    memset(&colorAttach, 0, sizeof(colorAttach));
    colorAttach.format = chosenFormat;
    colorAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep;
    memset(&dep, 0, sizeof(dep));
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo;
    memset(&rpInfo, 0, sizeof(rpInfo));
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttach;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    VkRenderPass renderPass = 0;
    vr = vkCreateRenderPass(device, &rpInfo, 0, &renderPass);
    __android_log_print(4, "squashbtn", "vkCreateRenderPass vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkShaderModuleCreateInfo vsInfo;
    memset(&vsInfo, 0, sizeof(vsInfo));
    vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsInfo.codeSize = sizeof(g_triangle_vert_spv);
    vsInfo.pCode = g_triangle_vert_spv;
    VkShaderModule vsModule = 0;
    vr = vkCreateShaderModule(device, &vsInfo, 0, &vsModule);
    __android_log_print(4, "squashbtn", "vkCreateShaderModule(vs) vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkShaderModuleCreateInfo fsInfo;
    memset(&fsInfo, 0, sizeof(fsInfo));
    fsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fsInfo.codeSize = sizeof(g_triangle_frag_spv);
    fsInfo.pCode = g_triangle_frag_spv;
    VkShaderModule fsModule = 0;
    vr = vkCreateShaderModule(device, &fsInfo, 0, &fsModule);
    __android_log_print(4, "squashbtn", "vkCreateShaderModule(fs) vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

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
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2];
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 8;

    VkPipelineVertexInputStateCreateInfo vinState;
    memset(&vinState, 0, sizeof(vinState));
    vinState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vinState.vertexBindingDescriptionCount = 1;
    vinState.pVertexBindingDescriptions = &binding;
    vinState.vertexAttributeDescriptionCount = 2;
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
    __android_log_print(4, "squashbtn", "vkCreatePipelineLayout vr=%d", (int)vr);
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
    gpInfo.pColorBlendState = &cbState;
    gpInfo.layout = pipelineLayout;
    gpInfo.renderPass = renderPass;
    gpInfo.subpass = 0;
    gpInfo.basePipelineIndex = -1;

    VkPipeline pipeline = 0;
    vr = vkCreateGraphicsPipelines(device, 0, 1, &gpInfo, 0, &pipeline);
    __android_log_print(4, "squashbtn", "vkCreateGraphicsPipelines vr=%d pipeline=%p", (int)vr, (void*)pipeline);
    if (vr != VK_SUCCESS) return 0;

    /* ==================================================================
     * Text pipeline: font atlas texture + descriptor set + glyph pipeline,
     * ported from SQW/text_renderer_vk.c's sqw_text_renderer_init() (see
     * this file's own top comment). build_page_dom_layout() below already
     * calls sqw_font_atlas_decode() itself (needed before layout_compute()
     * for real word-wrap measurement), so g_font_atlas_alpha is ready by
     * the time this runs, provided build_page_dom_layout() is called
     * first -- see the call site below.
     * ================================================================== */
    build_page_dom_layout(extent.width, extent.height);

    VkDeviceSize atlasSize = (VkDeviceSize)SQW_FONT_ATLAS_W * (VkDeviceSize)SQW_FONT_ATLAS_H;
    VkBufferCreateInfo stagingBufInfo;
    memset(&stagingBufInfo, 0, sizeof(stagingBufInfo));
    stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufInfo.size = atlasSize;
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer stagingBuf = 0;
    vr = vkCreateBuffer(device, &stagingBufInfo, 0, &stagingBuf);
    __android_log_print(4, "squashbtn", "atlas staging vkCreateBuffer vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkMemoryRequirements stagingMemReq;
    memset(&stagingMemReq, 0, sizeof(stagingMemReq));
    vkGetBufferMemoryRequirements(device, stagingBuf, &stagingMemReq);
    uint32_t stagingMemType = find_memory_type(&memProps, stagingMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingMemType == 0xFFFFFFFFu) return 0;
    VkMemoryAllocateInfo stagingAllocInfo;
    memset(&stagingAllocInfo, 0, sizeof(stagingAllocInfo));
    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    /* See the vertex-buffer allocInfo's own comment further down for the
     * real memReq.size-reads-wrong bug this works around. */
    stagingAllocInfo.allocationSize = (atlasSize > stagingMemReq.size) ? atlasSize : stagingMemReq.size;
    stagingAllocInfo.memoryTypeIndex = stagingMemType;
    VkDeviceMemory stagingMem = 0;
    vr = vkAllocateMemory(device, &stagingAllocInfo, 0, &stagingMem);
    if (vr != VK_SUCCESS) return 0;
    vkBindBufferMemory(device, stagingBuf, stagingMem, 0);

    void *stagingMapped = 0;
    vkMapMemory(device, stagingMem, 0, atlasSize, 0, &stagingMapped);
    memcpy(stagingMapped, g_font_atlas_alpha, (size_t)atlasSize);
    vkUnmapMemory(device, stagingMem);

    VkImageCreateInfo atlasImgInfo;
    memset(&atlasImgInfo, 0, sizeof(atlasImgInfo));
    atlasImgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    atlasImgInfo.imageType = VK_IMAGE_TYPE_2D;
    atlasImgInfo.format = VK_FORMAT_R8_UNORM;
    atlasImgInfo.extent.width = SQW_FONT_ATLAS_W;
    atlasImgInfo.extent.height = SQW_FONT_ATLAS_H;
    atlasImgInfo.extent.depth = 1;
    atlasImgInfo.mipLevels = 1;
    atlasImgInfo.arrayLayers = 1;
    atlasImgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    atlasImgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    atlasImgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    atlasImgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    atlasImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage atlasImage = 0;
    vr = vkCreateImage(device, &atlasImgInfo, 0, &atlasImage);
    __android_log_print(4, "squashbtn", "atlas vkCreateImage vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkMemoryRequirements atlasMemReq;
    memset(&atlasMemReq, 0, sizeof(atlasMemReq));
    vkGetImageMemoryRequirements(device, atlasImage, &atlasMemReq);
    uint32_t atlasMemType = find_memory_type(&memProps, atlasMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (atlasMemType == 0xFFFFFFFFu) return 0;
    VkMemoryAllocateInfo atlasAllocInfo;
    memset(&atlasAllocInfo, 0, sizeof(atlasAllocInfo));
    atlasAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    /* Same memReq.size bug as the other allocations here -- an optimally
     * tiled image can legitimately need more than width*height bytes
     * (row padding), so floor on a generous multiple of the raw pixel
     * count rather than the exact byte count a linear buffer would need. */
    atlasAllocInfo.allocationSize = (atlasSize * 4 > atlasMemReq.size) ? atlasSize * 4 : atlasMemReq.size;
    atlasAllocInfo.memoryTypeIndex = atlasMemType;
    VkDeviceMemory atlasMemory = 0;
    vr = vkAllocateMemory(device, &atlasAllocInfo, 0, &atlasMemory);
    if (vr != VK_SUCCESS) return 0;
    vkBindImageMemory(device, atlasImage, atlasMemory, 0);

    VkCommandBufferAllocateInfo oneShotAllocInfo;
    memset(&oneShotAllocInfo, 0, sizeof(oneShotAllocInfo));
    oneShotAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    oneShotAllocInfo.commandPool = commandPool;
    oneShotAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    oneShotAllocInfo.commandBufferCount = 1;
    VkCommandBuffer oneShot = 0;
    vkAllocateCommandBuffers(device, &oneShotAllocInfo, &oneShot);

    VkCommandBufferBeginInfo oneShotBegin;
    memset(&oneShotBegin, 0, sizeof(oneShotBegin));
    oneShotBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    oneShotBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(oneShot, &oneShotBegin);

    VkImageMemoryBarrier toDst;
    memset(&toDst, 0, sizeof(toDst));
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = 0xFFFFFFFFu;
    toDst.dstQueueFamilyIndex = 0xFFFFFFFFu;
    toDst.image = atlasImage;
    toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toDst.subresourceRange.levelCount = 1;
    toDst.subresourceRange.layerCount = 1;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(oneShot, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, 0, 0, 0, 1, &toDst);

    VkBufferImageCopy copyRegion;
    memset(&copyRegion, 0, sizeof(copyRegion));
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent.width = SQW_FONT_ATLAS_W;
    copyRegion.imageExtent.height = SQW_FONT_ATLAS_H;
    copyRegion.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(oneShot, stagingBuf, atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    VkImageMemoryBarrier toShaderRead;
    memset(&toShaderRead, 0, sizeof(toShaderRead));
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = 0xFFFFFFFFu;
    toShaderRead.dstQueueFamilyIndex = 0xFFFFFFFFu;
    toShaderRead.image = atlasImage;
    toShaderRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toShaderRead.subresourceRange.levelCount = 1;
    toShaderRead.subresourceRange.layerCount = 1;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(oneShot, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, 0, 0, 0, 1, &toShaderRead);

    vkEndCommandBuffer(oneShot);
    VkSubmitInfo oneShotSubmit;
    memset(&oneShotSubmit, 0, sizeof(oneShotSubmit));
    oneShotSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    oneShotSubmit.commandBufferCount = 1;
    oneShotSubmit.pCommandBuffers = &oneShot;
    vkQueueSubmit(queue, 1, &oneShotSubmit, 0);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &oneShot);
    vkDestroyBuffer(device, stagingBuf, 0);
    vkFreeMemory(device, stagingMem, 0);
    __android_log_print(4, "squashbtn", "font atlas uploaded (%dx%d)", SQW_FONT_ATLAS_W, SQW_FONT_ATLAS_H);

    VkImageViewCreateInfo atlasViewInfo;
    memset(&atlasViewInfo, 0, sizeof(atlasViewInfo));
    atlasViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    atlasViewInfo.image = atlasImage;
    atlasViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    atlasViewInfo.format = VK_FORMAT_R8_UNORM;
    atlasViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    atlasViewInfo.subresourceRange.levelCount = 1;
    atlasViewInfo.subresourceRange.layerCount = 1;
    VkImageView atlasView = 0;
    vr = vkCreateImageView(device, &atlasViewInfo, 0, &atlasView);
    if (vr != VK_SUCCESS) return 0;

    VkSamplerCreateInfo atlasSampInfo;
    memset(&atlasSampInfo, 0, sizeof(atlasSampInfo));
    atlasSampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    atlasSampInfo.magFilter = VK_FILTER_LINEAR;
    atlasSampInfo.minFilter = VK_FILTER_LINEAR;
    atlasSampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    atlasSampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    atlasSampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    atlasSampInfo.maxLod = 0.0f;
    atlasSampInfo.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    VkSampler atlasSampler = 0;
    vr = vkCreateSampler(device, &atlasSampInfo, 0, &atlasSampler);
    if (vr != VK_SUCCESS) return 0;

    VkDescriptorSetLayoutBinding textBinding;
    memset(&textBinding, 0, sizeof(textBinding));
    textBinding.binding = 0;
    textBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textBinding.descriptorCount = 1;
    textBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo textDslInfo;
    memset(&textDslInfo, 0, sizeof(textDslInfo));
    textDslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    textDslInfo.bindingCount = 1;
    textDslInfo.pBindings = &textBinding;
    VkDescriptorSetLayout textDescSetLayout = 0;
    vr = vkCreateDescriptorSetLayout(device, &textDslInfo, 0, &textDescSetLayout);
    if (vr != VK_SUCCESS) return 0;

    VkDescriptorPoolSize textPoolSize;
    textPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textPoolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo textDpInfo;
    memset(&textDpInfo, 0, sizeof(textDpInfo));
    textDpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    textDpInfo.maxSets = 1;
    textDpInfo.poolSizeCount = 1;
    textDpInfo.pPoolSizes = &textPoolSize;
    VkDescriptorPool textDescPool = 0;
    vr = vkCreateDescriptorPool(device, &textDpInfo, 0, &textDescPool);
    if (vr != VK_SUCCESS) return 0;

    VkDescriptorSetAllocateInfo textDsaInfo;
    memset(&textDsaInfo, 0, sizeof(textDsaInfo));
    textDsaInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    textDsaInfo.descriptorPool = textDescPool;
    textDsaInfo.descriptorSetCount = 1;
    textDsaInfo.pSetLayouts = &textDescSetLayout;
    VkDescriptorSet textDescSet = 0;
    vr = vkAllocateDescriptorSets(device, &textDsaInfo, &textDescSet);
    if (vr != VK_SUCCESS) return 0;

    VkDescriptorImageInfo textImgInfo;
    textImgInfo.sampler = atlasSampler;
    textImgInfo.imageView = atlasView;
    textImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet textWrite;
    memset(&textWrite, 0, sizeof(textWrite));
    textWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textWrite.dstSet = textDescSet;
    textWrite.dstBinding = 0;
    textWrite.descriptorCount = 1;
    textWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textWrite.pImageInfo = &textImgInfo;
    vkUpdateDescriptorSets(device, 1, &textWrite, 0, 0);

    VkShaderModuleCreateInfo textVsInfo;
    memset(&textVsInfo, 0, sizeof(textVsInfo));
    textVsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    textVsInfo.codeSize = sizeof(g_text_vert_spv);
    textVsInfo.pCode = g_text_vert_spv;
    VkShaderModule textVsModule = 0;
    vr = vkCreateShaderModule(device, &textVsInfo, 0, &textVsModule);
    __android_log_print(4, "squashbtn", "text vkCreateShaderModule(vs) vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkShaderModuleCreateInfo textFsInfo;
    memset(&textFsInfo, 0, sizeof(textFsInfo));
    textFsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    textFsInfo.codeSize = sizeof(g_text_frag_spv);
    textFsInfo.pCode = g_text_frag_spv;
    VkShaderModule textFsModule = 0;
    vr = vkCreateShaderModule(device, &textFsInfo, 0, &textFsModule);
    __android_log_print(4, "squashbtn", "text vkCreateShaderModule(fs) vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkPipelineShaderStageCreateInfo textStages[2];
    memset(textStages, 0, sizeof(textStages));
    textStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    textStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    textStages[0].module = textVsModule;
    textStages[0].pName = "main";
    textStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    textStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    textStages[1].module = textFsModule;
    textStages[1].pName = "main";

    VkVertexInputBindingDescription textBindingDesc;
    textBindingDesc.binding = 0;
    textBindingDesc.stride = sizeof(GlyphVertex);
    textBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription textAttrs[3];
    textAttrs[0].location = 0; textAttrs[0].binding = 0; textAttrs[0].format = VK_FORMAT_R32G32_SFLOAT; textAttrs[0].offset = 0;
    textAttrs[1].location = 1; textAttrs[1].binding = 0; textAttrs[1].format = VK_FORMAT_R32G32_SFLOAT; textAttrs[1].offset = 8;
    textAttrs[2].location = 2; textAttrs[2].binding = 0; textAttrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; textAttrs[2].offset = 16;

    VkPipelineVertexInputStateCreateInfo textVinState;
    memset(&textVinState, 0, sizeof(textVinState));
    textVinState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    textVinState.vertexBindingDescriptionCount = 1;
    textVinState.pVertexBindingDescriptions = &textBindingDesc;
    textVinState.vertexAttributeDescriptionCount = 3;
    textVinState.pVertexAttributeDescriptions = textAttrs;

    VkPipelineInputAssemblyStateCreateInfo textIaState;
    memset(&textIaState, 0, sizeof(textIaState));
    textIaState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    textIaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo textVpState;
    memset(&textVpState, 0, sizeof(textVpState));
    textVpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    textVpState.viewportCount = 1;
    textVpState.scissorCount = 1;

    VkDynamicState textDynStates[2];
    textDynStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
    textDynStates[1] = VK_DYNAMIC_STATE_SCISSOR;
    VkPipelineDynamicStateCreateInfo textDynState;
    memset(&textDynState, 0, sizeof(textDynState));
    textDynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    textDynState.dynamicStateCount = 2;
    textDynState.pDynamicStates = textDynStates;

    VkPipelineRasterizationStateCreateInfo textRsState;
    memset(&textRsState, 0, sizeof(textRsState));
    textRsState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    textRsState.polygonMode = VK_POLYGON_MODE_FILL;
    textRsState.cullMode = 0;
    textRsState.frontFace = VK_FRONT_FACE_CLOCKWISE;
    textRsState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo textMsState;
    memset(&textMsState, 0, sizeof(textMsState));
    textMsState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    textMsState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState textBlendAttach;
    memset(&textBlendAttach, 0, sizeof(textBlendAttach));
    textBlendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    textBlendAttach.blendEnable = VK_TRUE;
    textBlendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    textBlendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    textBlendAttach.colorBlendOp = VK_BLEND_OP_ADD;
    textBlendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    textBlendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    textBlendAttach.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo textCbState;
    memset(&textCbState, 0, sizeof(textCbState));
    textCbState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    textCbState.attachmentCount = 1;
    textCbState.pAttachments = &textBlendAttach;

    VkPipelineLayoutCreateInfo textPlInfo;
    memset(&textPlInfo, 0, sizeof(textPlInfo));
    textPlInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    textPlInfo.setLayoutCount = 1;
    textPlInfo.pSetLayouts = &textDescSetLayout;
    VkPipelineLayout textPipelineLayout = 0;
    vr = vkCreatePipelineLayout(device, &textPlInfo, 0, &textPipelineLayout);
    if (vr != VK_SUCCESS) return 0;

    VkGraphicsPipelineCreateInfo textGpInfo;
    memset(&textGpInfo, 0, sizeof(textGpInfo));
    textGpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    textGpInfo.stageCount = 2;
    textGpInfo.pStages = textStages;
    textGpInfo.pVertexInputState = &textVinState;
    textGpInfo.pInputAssemblyState = &textIaState;
    textGpInfo.pViewportState = &textVpState;
    textGpInfo.pRasterizationState = &textRsState;
    textGpInfo.pMultisampleState = &textMsState;
    textGpInfo.pColorBlendState = &textCbState;
    textGpInfo.pDynamicState = &textDynState;
    textGpInfo.layout = textPipelineLayout;
    textGpInfo.renderPass = renderPass;
    textGpInfo.subpass = 0;
    textGpInfo.basePipelineIndex = -1;
    VkPipeline textPipeline = 0;
    vr = vkCreateGraphicsPipelines(device, 0, 1, &textGpInfo, 0, &textPipeline);
    __android_log_print(4, "squashbtn", "text vkCreateGraphicsPipelines vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkDeviceSize textVbSize = (VkDeviceSize)(TEXT_MAX_GLYPHS * 6 * sizeof(GlyphVertex));
    VkBufferCreateInfo textBufInfo;
    memset(&textBufInfo, 0, sizeof(textBufInfo));
    textBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    textBufInfo.size = textVbSize;
    textBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    textBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer textVertexBuffer = 0;
    vr = vkCreateBuffer(device, &textBufInfo, 0, &textVertexBuffer);
    if (vr != VK_SUCCESS) return 0;

    VkMemoryRequirements textMemReq;
    memset(&textMemReq, 0, sizeof(textMemReq));
    vkGetBufferMemoryRequirements(device, textVertexBuffer, &textMemReq);
    uint32_t textMemType = find_memory_type(&memProps, textMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (textMemType == 0xFFFFFFFFu) return 0;
    VkMemoryAllocateInfo textAllocInfo;
    memset(&textAllocInfo, 0, sizeof(textAllocInfo));
    textAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    /* Real, confirmed squash/ARM64 bug found while building this file:
     * VkMemoryRequirements::size reads back wrong (it comes back as just
     * the alignment, 4096 on this Mali driver) after
     * vkGetBufferMemoryRequirements() -- allocating exactly that instead
     * of the real buffer size silently under-allocates, and writing past
     * byte 4096 of the mapped region SIGSEGVs. textVbSize is the size we
     * actually asked vkCreateBuffer() for, so it's always at least as
     * large as the buffer really needs; take whichever of the two is
     * bigger so a correct memReq.size on some other driver still works. */
    textAllocInfo.allocationSize = (textVbSize > textMemReq.size) ? textVbSize : textMemReq.size;
    textAllocInfo.memoryTypeIndex = textMemType;
    VkDeviceMemory textVertexMemory = 0;
    vr = vkAllocateMemory(device, &textAllocInfo, 0, &textVertexMemory);
    if (vr != VK_SUCCESS) return 0;
    vkBindBufferMemory(device, textVertexBuffer, textVertexMemory, 0);
    g_textVertexMemory = textVertexMemory;
    __android_log_print(4, "squashbtn", "text renderer ready (max %d glyphs)", TEXT_MAX_GLYPHS);

    /* ==================================================================
     * Flat-rect vertex buffer: buttons + <pre> background + scrollbar
     * track/thumb rects, rebuilt in full every redraw (see
     * build_frame_geometry()).
     * ================================================================== */
    VkDeviceSize vbSize = (VkDeviceSize)(MAX_RECTS * 6 * sizeof(Vertex));
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = vbSize;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer vertexBuffer = 0;
    vr = vkCreateBuffer(device, &bufInfo, 0, &vertexBuffer);
    __android_log_print(4, "squashbtn", "vkCreateBuffer vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkMemoryRequirements memReq;
    memset(&memReq, 0, sizeof(memReq));
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memReq);

    uint32_t memTypeIdx = find_memory_type(&memProps, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    __android_log_print(4, "squashbtn", "memTypeIdx=%u", memTypeIdx);
    if (memTypeIdx == 0xFFFFFFFFu) return 0;

    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    /* See textAllocInfo.allocationSize's own comment above -- same
     * memReq.size-reads-wrong bug, same fix. */
    allocInfo.allocationSize = (vbSize > memReq.size) ? vbSize : memReq.size;
    allocInfo.memoryTypeIndex = memTypeIdx;
    VkDeviceMemory vertexMemory = 0;
    vr = vkAllocateMemory(device, &allocInfo, 0, &vertexMemory);
    __android_log_print(4, "squashbtn", "vkAllocateMemory vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;
    vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0);
    g_rectVertexMemory = vertexMemory;

    /* NOT persistently mapped -- build_frame_geometry() writes into plain
     * CPU staging arrays and does its own fresh map/memcpy/unmap for each
     * of the two buffers every time it runs. See g_glyphStaging's own
     * comment for why (a pointer obtained once via vkMapMemory() and held
     * across later Vulkan calls SIGSEGVs on write on this squash/ARM64/
     * Mali combination -- map-memcpy-unmap immediately, like the font
     * atlas upload above, is the only pattern proven to work). */
    uint32_t rectCount = build_frame_geometry(extent.width, extent.height);
    __android_log_print(4, "squashbtn", "vertex buffers ready (%u rects, %u glyph verts)", rectCount, g_glyphVCount);

    VkCommandBufferAllocateInfo cbAllocInfo;
    memset(&cbAllocInfo, 0, sizeof(cbAllocInfo));
    cbAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAllocInfo.commandPool = commandPool;
    cbAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAllocInfo.commandBufferCount = swapImageCount;
    VkCommandBuffer cmdBufs[8];
    vr = vkAllocateCommandBuffers(device, &cbAllocInfo, cmdBufs);
    __android_log_print(4, "squashbtn", "vkAllocateCommandBuffers vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    /* Per-swapchain-image-index resources, created lazily on first use
     * and cached thereafter (see this file's top comment on the real
     * Mali vkCreateImageView bug this avoids triggering in a tight
     * loop).
     *
     * KNOWN LIMITATION, confirmed while building this file: creating a
     * 5th DISTINCT swapchain image's view+framebuffer -- whether lazily
     * here during interactive use, or in a tight loop up front before
     * the render loop even starts -- SIGSEGVs inside vkCreateImageView()
     * itself on this device, deterministically and regardless of timing
     * (a 50ms sleep between calls made no difference). Images 0-3 always
     * succeed; only the swapchain's 5th image (this device's own
     * minImageCount) ever exposes it. Root cause not yet isolated --
     * doesn't reproduce for the flat-color-only triangle demo, which
     * never runs enough redraws in one session to reach a 5th distinct
     * image. Sustained dragging/scrolling that cycles through every
     * swapchain image can still hit this. */
    VkImageView swapViews[8];
    VkFramebuffer framebuffers[8];
    int haveView[8];
    memset(haveView, 0, sizeof(haveView));

    for (;;) {
        uint32_t imageIndex = 0;

        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, ~0ull);
        vkResetFences(device, 1, &inFlightFence);

        vr = vkAcquireNextImageKHR(device, swapchain, ~0ull, imageAvailable, 0, &imageIndex);
        if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR) {
            __android_log_print(4, "squashbtn", "vkAcquireNextImageKHR failed vr=%d, stopping render loop", (int)vr);
            return 0;
        }

        if (!haveView[imageIndex]) {
            VkImageViewCreateInfo ivInfo;
            memset(&ivInfo, 0, sizeof(ivInfo));
            ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ivInfo.image = swapImages[imageIndex];
            ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivInfo.format = chosenFormat;
            ivInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ivInfo.subresourceRange.levelCount = 1;
            ivInfo.subresourceRange.layerCount = 1;
            vr = vkCreateImageView(device, &ivInfo, 0, &swapViews[imageIndex]);
            if (vr != VK_SUCCESS) { __android_log_print(4, "squashbtn", "vkCreateImageView[%u] failed vr=%d", imageIndex, (int)vr); return 0; }

            VkFramebufferCreateInfo fbInfo;
            memset(&fbInfo, 0, sizeof(fbInfo));
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &swapViews[imageIndex];
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;
            vr = vkCreateFramebuffer(device, &fbInfo, 0, &framebuffers[imageIndex]);
            if (vr != VK_SUCCESS) { __android_log_print(4, "squashbtn", "vkCreateFramebuffer[%u] failed vr=%d", imageIndex, (int)vr); return 0; }
            haveView[imageIndex] = 1;
            __android_log_print(4, "squashbtn", "created view+framebuffer for imageIndex=%u", imageIndex);
        }

        VkCommandBuffer cmd = cmdBufs[imageIndex];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo;
        memset(&beginInfo, 0, sizeof(beginInfo));
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkClearValue clearValue;
        memset(&clearValue, 0, sizeof(clearValue));
        clearValue.color.float32[0] = 0.07f;
        clearValue.color.float32[1] = 0.07f;
        clearValue.color.float32[2] = 0.09f;
        clearValue.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo rpBegin;
        memset(&rpBegin, 0, sizeof(rpBegin));
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[imageIndex];
        rpBegin.renderArea.offset.x = 0; rpBegin.renderArea.offset.y = 0;
        rpBegin.renderArea.extent = extent;
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize offset0 = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset0);
        PushConstants pc;
        pc.angle = 0.0f;
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd, rectCount * 6, 1, 0, 0);

        if (g_glyphVCount > 0) {
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textPipelineLayout, 0, 1, &textDescSet, 0, 0);
            VkDeviceSize toffset0 = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &textVertexBuffer, &toffset0);
            vkCmdDraw(cmd, g_glyphVCount, 1, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
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
        __android_log_print(4, "squashbtn", "frame presented (imageIndex=%u) vr=%d rects=%u glyphs=%u", imageIndex, (int)vr, rectCount, g_glyphVCount);

        /* Block here until a touch event changes something worth a
         * redraw -- this demo only ever re-renders in response to real
         * input, never a continuous animation loop. */
        for (;;) {
            while (!g_touch_pending) usleep(5000);
            g_touch_pending = 0;

            float tx = g_touch_x, ty = g_touch_y;
            int action = g_touch_action;
            int changed = 0;

            if (action == ACTION_DOWN) {
                g_drag_down_x = tx; g_drag_down_y = ty;
                g_drag_anchor_scroll_x = g_scroll_x; g_drag_anchor_scroll_y = g_scroll_y;
                g_drag_total_dist = 0;
                g_dragging = 1;
            } else if (action == ACTION_MOVE && g_dragging) {
                float dx = tx - g_drag_down_x;
                float dy = ty - g_drag_down_y;
                g_drag_total_dist = g_drag_total_dist + (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                float newScrollX = clamp_scroll(g_drag_anchor_scroll_x - dx, g_layout.content_w, (float)extent.width);
                float newScrollY = clamp_scroll(g_drag_anchor_scroll_y - dy, g_layout.content_h, (float)extent.height);
                if (newScrollX != g_scroll_x || newScrollY != g_scroll_y) {
                    g_scroll_x = newScrollX; g_scroll_y = newScrollY;
                    changed = 1;
                }
            }

            /* Hover hit-test: content-space coordinates (screen + current
             * scroll), same conversion SQW/sqw_main.c's own mouse-move
             * handler uses. A finger touching/dragging counts as "hovering"
             * for a touch device -- there is no separate mouse-move signal
             * to drive this from otherwise. */
            float contentX = tx + g_scroll_x;
            float contentY = ty + g_scroll_y;
            int hit = -1;
            if (action == ACTION_DOWN || action == ACTION_MOVE) {
                hit = layout_hit_test(&g_layout, contentX, contentY);
            }
            int newHoverIdx = 0;
            int newHasHover = 0;
            if (hit >= 0 && g_layout.boxes[hit].kind == SQW_BOX_BUTTON) { newHoverIdx = hit; newHasHover = 1; }
            if (action == ACTION_UP) newHasHover = 0;

            if (newHasHover != g_has_hover || newHoverIdx != g_hover_box_index) {
                if (g_has_hover) {
                    DomNode *oldNode = g_layout.boxes[g_hover_box_index].node;
                    oldNode->hover = 0;
                    css_apply_one(oldNode, &g_css_sheet, g_viewport_w);
                }
                if (newHasHover) {
                    DomNode *newNode = g_layout.boxes[newHoverIdx].node;
                    newNode->hover = 1;
                    css_apply_one(newNode, &g_css_sheet, g_viewport_w);
                }
                g_hover_box_index = newHoverIdx;
                g_has_hover = newHasHover;
                changed = 1;
            }

            if (action == ACTION_UP) {
                if (g_dragging && g_drag_total_dist < 20.0f) {
                    int upHit = layout_hit_test(&g_layout, contentX, contentY);
                    if (upHit >= 0 && g_layout.boxes[upHit].kind == SQW_BOX_BUTTON) {
                        __android_log_print(4, "squashbtn", "button CLICKED (box#%d)", upHit);
                    }
                }
                g_dragging = 0;
            }

            __android_log_print(4, "squashbtn", "touch action=%d at %.0f,%.0f scroll=%.0f,%.0f hit=%d",
                                 action, tx, ty, g_scroll_x, g_scroll_y, hit);

            if (changed) {
                /* The GPU may still be reading the previous frame's
                 * vertex data from these same buffers (no wait happens
                 * between presenting a frame and handling the next touch
                 * event) -- wait for that submission to fully finish
                 * before build_frame_geometry() maps and overwrites them,
                 * or the CPU write races the GPU read. Real, reproduced
                 * bug: without this, a fast sequence of touch-driven
                 * redraws (e.g. a drag generating many MOVE events) could
                 * SIGSEGV inside the map/memcpy/unmap step. The outer
                 * loop's own vkWaitForFences()/vkResetFences() at the top
                 * still runs normally afterward -- waiting on an
                 * already-signaled fence just returns immediately, so
                 * this doesn't double up. */
                vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, ~0ull);
                rectCount = build_frame_geometry(extent.width, extent.height);
                break; /* fall through to re-render */
            }
        }
    }
}

void nativeSurfaceCreated(void *env, void *clazz, void *surface) {
    unsigned long t1, t2;
    void *window = ANativeWindow_fromSurface(env, surface);
    __android_log_print(4, "squashbtn", "nativeSurfaceCreated: window=%p (from surface=%p), spawning instance+render threads", window, surface);
    pthread_create(&t1, 0, create_instance_thread, window);
    pthread_create(&t2, 0, vulkan_thread_main, window);
}

void nativeSurfaceChanged(void *env, void *clazz, int width, int height) {
    __android_log_print(4, "squashbtn", "nativeSurfaceChanged: %dx%d", width, height);
}

void nativeSurfaceDestroyed(void *env, void *clazz) {
    __android_log_print(4, "squashbtn", "nativeSurfaceDestroyed");
}
