#include "include/vulkan_android.h"
#include "triangle_spirv.h"
#include "android/jni_min.h"
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

/* Copies a just-rendered offscreen frame into a real, acquired swapchain
 * image -- see offscreenImage's own comment in vulkan_thread_main() for
 * why this exists at all (a real Mali driver bug makes vkCreateImageView
 * crash on this device's swapchain's 5th image; rendering into one
 * persistent offscreen image and copying it into whichever swapchain
 * image was acquired avoids ever calling vkCreateImageView on a
 * swapchain image in the first place). The render pass's own
 * colorAttach.finalLayout (PRESENT_SRC_KHR) already leaves offscreenImage
 * in that layout at the end of every render pass (its initialLayout is
 * UNDEFINED, so the next frame's render pass re-transitions it
 * regardless of what this function leaves it in).
 *
 * A separate function, not inlined into vulkan_thread_main()'s own
 * per-frame loop -- purely for readability, NOT working around any
 * squash bug. This exact code (ported from forms_android.c, where it was
 * developed and fully verified first -- see that file's own history)
 * originally failed to even PARSE ("expected ';' but got 'IDENT'" right
 * after a `VkImageCopy` declaration) wherever it was placed, which looked
 * exactly like one of this project's own deep ARM64 codegen/parser-limit
 * bugs until isolated down to a 4-line standalone repro: `VkImageCopy`
 * (needed for vkCmdCopyImage, never used anywhere in this project
 * before) was simply missing from include/vulkan_core.h's own
 * hand-trimmed subset of the real Vulkan header -- squash's parser has no
 * "unknown type name" diagnostic, so referencing an undefined type
 * produced a generic, misleading syntax error instead. Fixed by adding
 * the real `VkImageCopy` struct + the `vkCmdCopyImage` declaration to
 * include/vulkan_core.h (see that file's own comment at the new
 * declaration for the full story). */
static void sqw_copy_offscreen_to_swapchain(VkCommandBuffer cmd, VkImage offscreenImage, VkImage swapImage, uint32_t extentWidth, uint32_t extentHeight) {
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
static DomNode *g_dom_root = 0;

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

/* ---- real soft-keyboard text entry state (see the approved UI-component-
 * library plan): a real, persistent, off-screen android.widget.EditText
 * drives the REAL Android IME; this demo's own Vulkan glyph pipeline still
 * renders all VISIBLE text. Declared here (forward of nativeTouchEvent,
 * which needs to react to taps on a text box) even though they're only
 * ASSIGNED later in nativeEditTextReady() -- C requires the declaration to
 * come before first use, and nativeTouchEvent is defined above that
 * point in the file. All zero-initialized, matching every other global in
 * this file (see g_hover_box_index's own comment on why a non-zero
 * initializer isn't reliable here). */
static jobject g_edit_text = 0;
static jmethodID g_m_edittext_settext = 0;
static jmethodID g_m_edittext_gettext = 0;
static jmethodID g_m_edittext_requestfocus = 0;
static jmethodID g_m_edittext_clearfocus = 0;
static jmethodID g_m_charsequence_tostring = 0;
static jobject g_imm = 0;
static jmethodID g_m_imm_showsoftinput = 0;
static jmethodID g_m_imm_hidesoftinputfromwindow = 0;
static jmethodID g_m_view_getwindowtoken = 0;
/* Which DomNode (by layout-box index, same "int not pointer" convention as
 * g_hover_box_index -- see its own comment) currently owns the keyboard,
 * so a later keystroke callback (phase 4) knows which DomNode::form_value
 * to update. Zero-initialized with a separate "is anything focused" flag,
 * NOT a "-1 means none" sentinel -- same confirmed squash bug as
 * g_hover_box_index: a non-zero global initializer isn't reliable here,
 * full stop, regardless of how the value is later used. */
static int g_focused_box_index = 0;
static int g_has_focus = 0;

/* Phase 3 of the plan: tapping a text box / textarea requests real focus
 * and raises the real system keyboard. Runs directly inside
 * nativeTouchEvent (NOT the render thread's own touch-handling loop)
 * specifically because `env` here is a genuinely valid JNIEnv for
 * nativeTouchEvent's OWN calling thread (Android's real UI thread) --
 * JNIEnv values are thread-specific and the render thread is a plain
 * pthread with no JNIEnv of its own at all, so any JNI call needed in
 * response to a tap has to happen here, synchronously, not be deferred
 * through the usual g_touch_pending flag handoff the Vulkan redraw path
 * uses. */
static void sqw_dispatch_text_focus(void *env, float x, float y, int action) {
    if (action != ACTION_DOWN) return;
    if (!g_edit_text || g_layout.count == 0) return;
    float contentX = x + g_scroll_x;
    float contentY = y + g_scroll_y;
    int hit = layout_hit_test(&g_layout, contentX, contentY);
    if (hit < 0) return;
    LayoutBox *lb = &g_layout.boxes[hit];
    if (lb->kind != SQW_BOX_INPUT_TEXT && lb->kind != SQW_BOX_TEXTAREA) return;

    DomNode *node = lb->node;
    g_focused_box_index = hit;
    g_has_focus = 1;

    jstring currentVal = sqw_jni_NewStringUTF(env, node->form_value);
    jvalue setArgs[1];
    setArgs[0].l = currentVal;
    sqw_jni_CallVoidMethodA(env, g_edit_text, g_m_edittext_settext, setArgs);

    sqw_jni_CallBooleanMethodA(env, g_edit_text, g_m_edittext_requestfocus, 0);

    if (g_imm && g_m_imm_showsoftinput) {
        jvalue showArgs[2];
        showArgs[0].l = g_edit_text;
        showArgs[1].i = 0;
        sqw_jni_CallBooleanMethodA(env, g_imm, g_m_imm_showsoftinput, showArgs);
    }
    __android_log_print(4, "squashbtn", "sqw_dispatch_text_focus: focused box=%d", hit);
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
/* Not a real Android MotionEvent action -- reuses the exact same
 * g_touch_pending/g_touch_action single-slot queue nativeTouchEvent
 * already uses to wake the render thread's touch-handling loop, just with
 * a value that loop recognizes as "a DomNode::form_value changed
 * elsewhere (nativeTextChanged, real keystroke delivery), redraw, skip
 * all the hit-test/hover/drag logic that assumes real screen coordinates." */
#define ACTION_TEXT_CHANGED 3

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
    sqw_dispatch_text_focus(env, x, y, action);
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
        "<label for=\"name\">Name:</label>"
        "<input type=\"text\" id=\"name\" name=\"name\" class=\"textfield\">"
        "<label for=\"bio\">Bio:</label>"
        "<textarea id=\"bio\" name=\"bio\"></textarea>"
        "<input type=\"checkbox\" id=\"subscribe\" name=\"subscribe\" class=\"checkbox\">"
        "<label for=\"subscribe\">Subscribe</label>"
        "<input type=\"radio\" id=\"opt_a\" name=\"choice\" class=\"checkbox\" checked>"
        "<label for=\"opt_a\">Option A</label>"
        "<input type=\"radio\" id=\"opt_b\" name=\"choice\" class=\"checkbox\">"
        "<label for=\"opt_b\">Option B</label>"
        "<input type=\"checkbox\" id=\"toggle1\" name=\"toggle1\" class=\"toggle\">"
        "<label for=\"toggle1\">Toggle</label>"
        "<button type=\"submit\" id=\"submitbtn\">Submit</button>"
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
        "pre{color:#ffffff;background-color:#242424;font-size:28px;}"
        "label{color:#ffffff;font-size:28px;}"
        "input.textfield{width:400px;height:60px;}"
        "input.checkbox{width:40px;height:40px;}"
        "input.toggle{width:70px;height:36px;}"
        "textarea{width:600px;height:150px;}"
        /* No explicit height here: SQW's block-layout path only honors an
         * explicit css_height for a CHILDLESS element (see layout.c's own
         * "content_h_eff" comment) -- this button has a text child, so an
         * explicit height would be silently dropped in favor of the
         * content-driven (text line height) one anyway. font-size drives
         * a comfortably-sized touch target instead. */
        "#submitbtn{display:inline-block;width:300px;font-size:32px;color:#ffffff;background-color:#555555;}"
        "#submitbtn:hover{background-color:#777777;}";
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
    g_dom_root = root;
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

    /* g_form_nodes populated HERE, called directly inline (not through a
     * separate wrapper function, and never again later from the touch-
     * handling loop below) -- two real, confirmed ARM64 codegen bugs
     * found this session, both involving dom_walk():
     *   1. A dom_walk() call moved into its own one-line wrapper function
     *      silently visits ZERO nodes (no crash, callback just never
     *      invoked) even though the identical call inline in THIS
     *      function's own body visits every node correctly.
     *   2. Independently, dom_walk() also silently visits zero nodes when
     *      called from the touch-handling for(;;) loop later in this
     *      same function/thread, after all of vulkan_thread_main's own
     *      large Vulkan setup locals (VkImageView swapViews[8], command
     *      buffers, etc.) are already on the stack -- a stack-depth-
     *      dependent failure, confirmed via a trivial counting-only
     *      callback showing 29/29 nodes visited from this early call site
     *      and 0/29 from the late one in the identical build.
     * Neither isolated further -- worked around by calling dom_walk()
     * exactly once, inline, right here, and never again. */
    g_form_node_count = 0;
    dom_walk(root, sqw_collect_form_nodes_visit, 0);
}

/* ---- widget interaction helpers: checkbox/radio toggle, label "for="
 * forwarding, submit-field collection -- all pure C/DOM logic, no new
 * JNI/DEX surface. Radio-group exclusivity and submit-field collection
 * are ported from SQW/sqw_main.c's own radio_group_clear_visit()/
 * form_collect_visit() (desktop walks only the enclosing <form>; this
 * demo's radios/inputs aren't inside an explicit <form> wrapper, so these
 * walk the whole document instead -- same logic, wider root). ---- */

/* Flat list of every <input>/<textarea> in the document, built ONCE via a
 * dom_walk() call made directly inline inside build_page_dom_layout() (see
 * its own comment for why it must be inline, not a wrapper function, and
 * must never run again later from the touch-handling loop) and reused by
 * every runtime interaction helper below (label "for=" lookup, radio-group
 * clearing, submit collection) instead of calling dom_walk() again at tap
 * time. */
#define SQW_MAX_FORM_NODES 32
static DomNode *g_form_nodes[SQW_MAX_FORM_NODES];
static int g_form_node_count = 0;
static void sqw_collect_form_nodes_visit(DomNode *node, int depth, void *ctx) {
    (void)depth; (void)ctx;
    if (g_form_node_count >= SQW_MAX_FORM_NODES) return;
    if (strcmp(node->tag, "input") == 0 || strcmp(node->tag, "textarea") == 0) {
        g_form_nodes[g_form_node_count++] = node;
    }
}

static DomNode *sqw_find_by_id(const char *id) {
    int i;
    if (!id) return 0;
    for (i = 0; i < g_form_node_count; i++) {
        const char *nid = dom_get_attr(g_form_nodes[i], "id");
        if (nid && strcmp(nid, id) == 0) return g_form_nodes[i];
    }
    return 0;
}

/* kind_for_tag() alone can't distinguish a checkbox/radio <input> from a
 * plain text <input> -- it only ever sees the tag name "input" (real,
 * confirmed bug: tapping a checkbox's own real-width <label> re-derived
 * its kind via plain kind_for_tag(), which unconditionally returns
 * SQW_BOX_INPUT_TEXT for any "input" tag regardless of its real type=
 * attribute -- the type-aware distinction only exists in
 * layout_compute()'s own inline-child branch. The checkbox's
 * SQW_BOX_INPUT_CHECK dispatch branch was silently never reached this
 * way, so tapping the label visibly did nothing). Re-derives the real
 * kind by also checking type=, for any node reached via re-targeting
 * (label for=, button-caption text) rather than directly from
 * layout_hit_test() (which already gets it right the first time). */
static SqwBoxKind sqw_kind_for_node(DomNode *node) {
    if (strcmp(node->tag, "input") == 0) {
        const char *type = dom_get_attr(node, "type");
        if (type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0)) return SQW_BOX_INPUT_CHECK;
        return SQW_BOX_INPUT_TEXT;
    }
    if (strcmp(node->tag, "textarea") == 0) return SQW_BOX_TEXTAREA;
    return kind_for_tag(node->tag);
}

static void sqw_radio_group_clear(const char *name, DomNode *skip) {
    int i;
    for (i = 0; i < g_form_node_count; i++) {
        DomNode *node = g_form_nodes[i];
        if (node == skip) continue;
        const char *type = dom_get_attr(node, "type");
        if (!type || strcmp(type, "radio") != 0) continue;
        const char *nname = dom_get_attr(node, "name");
        if (nname && strcmp(nname, name) == 0) node->form_checked = 0;
    }
}

/* Returns 1 if `node` is a toggle-styled checkbox (class="toggle"), not a
 * plain checkbox -- SQW itself has no native <input type="toggle">, so a
 * toggle switch here is just a checkbox with a different VISUAL (pill
 * track + sliding thumb instead of a bordered box + inset fill), sharing
 * the exact same form_checked boolean and SQW_BOX_INPUT_CHECK kind. */
static int sqw_node_is_toggle(DomNode *node) {
    const char *cls = dom_get_attr(node, "class");
    if (!cls) return 0;
    const char *p = cls;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *tok = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        int tlen = (int)(p - tok);
        if (tlen == 6 && strncmp(tok, "toggle", 6) == 0) return 1;
    }
    return 0;
}

/* Click handling for a SQW_BOX_INPUT_CHECK box (checkbox, radio, or a
 * toggle -- all share this one box kind, see layout.c's own comment) --
 * ported 1:1 from SQW/sqw_main.c's own click handler: a radio sets itself
 * checked and clears every other same-"name" radio in the document; a
 * plain checkbox (or toggle) just flips its own form_checked. Re-resolves
 * CSS afterward in case a ":checked"-driven rule exists, matching the
 * same live re-style approach this file's own ":hover" handling already
 * uses. */
static void sqw_toggle_check_or_radio(DomNode *node) {
    const char *type = dom_get_attr(node, "type");
    if (type && strcmp(type, "radio") == 0) {
        const char *name = dom_get_attr(node, "name");
        if (name) sqw_radio_group_clear(name, node);
        node->form_checked = 1;
    } else {
        node->form_checked = !node->form_checked;
    }
    css_apply_one(node, &g_css_sheet, g_viewport_w);
}

/* Submit: ported from SQW/sqw_main.c's own form_collect_visit(), but
 * local-only (no network) -- walks the WHOLE document (this demo has no
 * explicit <form> wrapper) collecting every named input/textarea's
 * current value, skipping unchecked checkboxes/radios and the submit
 * control itself, and hands the result to a simple app-facing callback
 * instead of building a URL-encoded request. This is the shape other
 * applications using this as a component library would want: get the
 * field values back, decide what to do with them themselves. */
#define SQW_MAX_FORM_FIELDS 32
typedef struct { const char *name; const char *value; } SqwFormField;
/* First direct text-node child's content -- used to draw a <button>'s own
 * label or a <label>'s own caption, neither of which get a separate
 * SQW_BOX_TEXT run of their own (both are "atomic inline" boxes sized via
 * direct_text_width() but never split into word-level sub-boxes, see
 * layout.c's own is_atomic_inline_tag branch) -- so the draw pass reads
 * the text straight from the DOM, exactly the same way SQW's own desktop
 * renderer draws a <button>'s label from its child text / form_value
 * (see sqw_main.c's own SQW_BOX_BUTTON draw-time comment). */
static const char *node_direct_text(DomNode *node, int *outLen) {
    int i;
    for (i = 0; i < node->child_count; i++) {
        DomNode *c = node->children[i];
        if (dom_is_text(c)) {
            *outLen = (int)strlen(c->text);
            return c->text;
        }
    }
    *outLen = 0;
    return "";
}

static void sqw_submit_form(void) {
    SqwFormField fields[SQW_MAX_FORM_FIELDS];
    int count = 0;
    int i;
    for (i = 0; i < g_form_node_count && count < SQW_MAX_FORM_FIELDS; i++) {
        DomNode *node = g_form_nodes[i];
        const char *name = dom_get_attr(node, "name");
        if (!name) continue;
        const char *type = dom_get_attr(node, "type");
        if (type && (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0 || strcmp(type, "reset") == 0)) continue;
        if (type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0) && !node->form_checked) continue;
        fields[count].name = name;
        fields[count].value = node->form_value;
        count++;
    }
    __android_log_print(4, "squashbtn", "FORM SUBMITTED: %d field(s)", count);
    for (i = 0; i < count; i++) {
        __android_log_print(4, "squashbtn", "  %s = %s", fields[i].name, fields[i].value);
    }
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

#define MAX_RECTS 64
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

    g_glyphVCount = 0;
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
        /* Only draw the caption manually when this button is a true
         * ATOMIC-INLINE box (css_display==CSS_DISPLAY_INLINE exactly) --
         * see layout.c's own is_atomic_inline_tag branch: that's the ONLY
         * case where the button's text child gets no separate box of its
         * own. A real, confirmed bug found via forms_android.c's
         * block-layout signup form: #submitbtn here uses
         * display:inline-block, which routes through the ordinary
         * block-child-layout path instead, where its text DOES get real
         * SQW_BOX_TEXT child box(es) -- drawing it manually here too
         * double-drew the caption (the garbled-looking "Suhmitt"/
         * "S6bbrítít" text originally misdiagnosed as a screenshot
         * downscaling artifact), since the second loop below
         * (kind==SQW_BOX_TEXT) draws those same boxes again. */
        if (btn_node && btn_node->css_display == CSS_DISPLAY_INLINE) {
            int tlen = 0;
            const char *txt = node_direct_text(btn_node, &tlen);
            if (tlen > 0) {
                float scale = node_text_scale(btn_node);
                text_draw_string(b->x - g_scroll_x + 8.0f, b->y - g_scroll_y + (b->h - SQW_FONT_CELL_H * scale) * 0.5f,
                                  txt, tlen, scale, 1.0f, 1.0f, 1.0f, 1.0f, ew, eh);
            }
        }
    }
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
        } else if (lb->kind == SQW_BOX_SPAN && strcmp(lb->node->tag, "label") == 0) {
            /* <label>: same "atomic inline, no separate text run" shape as
             * <button> above -- read its own direct text child. */
            DomNode *label_node = lb->node;
            int tlen = 0;
            const char *txt = node_direct_text(label_node, &tlen);
            if (tlen > 0) {
                float tr = 1.0f, tg = 1.0f, tb = 1.0f;
                if (label_node->css_has_color) { tr = label_node->css_color[0]; tg = label_node->css_color[1]; tb = label_node->css_color[2]; }
                text_draw_string(bx, by, txt, tlen, lb->text_scale, tr, tg, tb, 1.0f, ew, eh);
            }
        } else if (lb->kind == SQW_BOX_INPUT_TEXT || lb->kind == SQW_BOX_TEXTAREA) {
            DomNode *field_node = lb->node;
            /* Blue-tinted border when focused (real keyboard is up on
             * this field), gray otherwise -- same signal SQW's own
             * desktop renderer uses, just one filled rect here instead of
             * 4 hollow border strips. */
            int isFocused = g_has_focus && g_focused_box_index == i;
            float br = isFocused ? 0.20f : 0.55f, bg = isFocused ? 0.45f : 0.55f, bb = isFocused ? 0.85f : 0.55f;
            write_rect_quad(&g_rectStaging[rectCount * 6], lb->x - 2, lb->y - 2, lb->w + 4, lb->h + 4, br, bg, bb,
                             g_scroll_x, g_scroll_y, ew, eh);
            rectCount++;
            write_rect_quad(&g_rectStaging[rectCount * 6], lb->x, lb->y, lb->w, lb->h, 1.0f, 1.0f, 1.0f,
                             g_scroll_x, g_scroll_y, ew, eh);
            rectCount++;
            int vlen = (int)strlen(field_node->form_value);
            if (vlen > 0) {
                float scale = node_text_scale(field_node);
                text_draw_string(bx + 6.0f, by + 4.0f, field_node->form_value, vlen, scale, 0.05f, 0.05f, 0.05f, 1.0f, ew, eh);
            }
        } else if (lb->kind == SQW_BOX_INPUT_CHECK) {
            DomNode *check_node = lb->node;
            int isToggle = sqw_node_is_toggle(check_node);
            if (isToggle) {
                /* Pill track + sliding thumb -- 2 rects, no SQW desktop
                 * precedent to port (it has no native toggle), styled as
                 * its own checkbox variant sharing the same form_checked
                 * boolean. */
                float trackR = check_node->form_checked ? 0.20f : 0.35f;
                float trackG = check_node->form_checked ? 0.60f : 0.35f;
                float trackB = check_node->form_checked ? 0.30f : 0.35f;
                write_rect_quad(&g_rectStaging[rectCount * 6], lb->x, lb->y, lb->w, lb->h, trackR, trackG, trackB,
                                 g_scroll_x, g_scroll_y, ew, eh);
                rectCount++;
                float thumbSize = lb->h - 6.0f;
                float thumbX = check_node->form_checked ? (lb->x + lb->w - thumbSize - 3.0f) : (lb->x + 3.0f);
                write_rect_quad(&g_rectStaging[rectCount * 6], thumbX, lb->y + 3.0f, thumbSize, thumbSize, 0.95f, 0.95f, 0.95f,
                                 g_scroll_x, g_scroll_y, ew, eh);
                rectCount++;
            } else {
                /* Plain checkbox/radio: bordered face, then (matching
                 * SQW desktop's own simplification -- no real checkmark/
                 * circle primitive exists anywhere in that renderer
                 * either) one inset filled rect when checked. Radio vs
                 * checkbox differs only in that inset amount, same as
                 * desktop. */
                int isRadio = 0;
                const char *type = dom_get_attr(check_node, "type");
                if (type && strcmp(type, "radio") == 0) isRadio = 1;
                write_rect_quad(&g_rectStaging[rectCount * 6], lb->x, lb->y, lb->w, lb->h, 0.55f, 0.55f, 0.55f,
                                 g_scroll_x, g_scroll_y, ew, eh);
                rectCount++;
                write_rect_quad(&g_rectStaging[rectCount * 6], lb->x + 1, lb->y + 1, lb->w - 2, lb->h - 2, 1.0f, 1.0f, 1.0f,
                                 g_scroll_x, g_scroll_y, ew, eh);
                rectCount++;
                if (check_node->form_checked) {
                    float inset = isRadio ? 4.0f : 3.0f;
                    write_rect_quad(&g_rectStaging[rectCount * 6], lb->x + inset, lb->y + inset, lb->w - inset * 2, lb->h - inset * 2,
                                     0.15f, 0.45f, 0.85f, g_scroll_x, g_scroll_y, ew, eh);
                    rectCount++;
                }
            }
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
    /* [16], not [8] -- a real, confirmed squash ARM64 bug found this
     * session (in forms_android.c first, then confirmed to affect this
     * file identically): a LOCAL array of VkImage (a `typedef struct
     * VkImage_T *VkImage` opaque-pointer handle --
     * VK_DEFINE_NON_DISPATCHABLE_HANDLE in vulkan_core.h) read back with
     * its LAST populated element's upper 32 bits silently lost/
     * overwritten (real stack corruption, not just a stale/uninitialized
     * read) when sized exactly [8] with 5 real elements written via
     * vkGetSwapchainImagesKHR(). The SAME shape hit `VkCommandBuffer
     * cmdBufs[8]` too (see its own declaration below) -- both are arrays
     * of a non-dispatchable-handle pointer-typedef, both indexed at
     * runtime up to index 4. Root cause not isolated further (plausibly
     * a local-array element-size miscalculation for this exact typedef
     * shape, the same general bug CLASS as this project's very first
     * documented ARM64 codegen bug, `a64_codegen_global`'s wrong
     * `typeinfo_size_a64()` call, but for a LOCAL array this time) --
     * worked around by over-allocating well past anything actually
     * indexed. Verified via an extended real-device stress test in
     * forms_android.c (150+ rapid redraws cycling through every
     * swapchain index many times, zero crashes after this fix,
     * deterministic crash before it) before being ported here. */
    VkImage swapImages[16];
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

    /* Real Mali driver bug workaround (confirmed via an earlier session's
     * actual driver crash-backtrace inspection -- NOT a squash bug, NOT a
     * live-view-count or eviction-order issue, both tried and ruled out
     * in this project's own history, see forms_android.c's identical
     * copy of this comment for the full story): vkCreateImageView()
     * crashes deterministically inside the vendor driver's own
     * image_view::init when called against this device's swapchain's 5th
     * real image, no matter when or how it's created. Fixed properly (not
     * just differently triggered) by never calling vkCreateImageView on a
     * swapchain image AT ALL: render every frame into ONE persistent
     * OFFSCREEN image instead (a real, ordinary, non-swapchain VkImage --
     * vkCreateImageView against it is unaffected, exactly like the
     * already-proven-safe font atlas image above), then vkCmdCopyImage()
     * the finished frame into whichever swapchain image was acquired
     * that frame. vkCmdCopyImage only ever needs the raw VkImage handle,
     * never a VkImageView, so the swapchain's own images are never
     * touched by the buggy call at all, regardless of how many distinct
     * indices a long interactive session eventually cycles through.
     * Developed and fully verified first in forms_android.c (including a
     * 150+-rapid-redraw real-device stress test cycling through every
     * swapchain index many times with zero crashes) before being ported
     * here unchanged, to eliminate this same pre-existing crash from both
     * demos without introducing new divergence between them. */
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
    __android_log_print(4, "squashbtn", "offscreen vkCreateImage vr=%d", (int)vr);
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
    __android_log_print(4, "squashbtn", "offscreen vkCreateImageView vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    VkFramebufferCreateInfo offFbInfo;
    memset(&offFbInfo, 0, sizeof(offFbInfo));
    offFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    offFbInfo.renderPass = renderPass;
    offFbInfo.attachmentCount = 1;
    offFbInfo.pAttachments = &offscreenView;
    offFbInfo.width = extent.width;
    offFbInfo.height = extent.height;
    offFbInfo.layers = 1;
    VkFramebuffer offscreenFramebuffer = 0;
    vr = vkCreateFramebuffer(device, &offFbInfo, 0, &offscreenFramebuffer);
    __android_log_print(4, "squashbtn", "offscreen vkCreateFramebuffer vr=%d", (int)vr);
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
    /* [16], not [8] -- a real, confirmed squash ARM64 local-array bug
     * (see swapImages' own declaration above for the full story, ported
     * from forms_android.c where this was found and fixed first):
     * VkCommandBuffer (an opaque-pointer handle typedef, same shape as
     * VkImage) indexed at runtime up to index 4 had its last populated
     * element's upper 32 bits silently lost when this array was sized
     * exactly [8] -- worked around by over-allocating well past anything
     * actually indexed. */
    VkCommandBuffer cmdBufs[16];
    vr = vkAllocateCommandBuffers(device, &cbAllocInfo, cmdBufs);
    __android_log_print(4, "squashbtn", "vkAllocateCommandBuffers vr=%d", (int)vr);
    if (vr != VK_SUCCESS) return 0;

    for (;;) {
        uint32_t imageIndex = 0;

        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, ~0ull);
        vkResetFences(device, 1, &inFlightFence);

        vr = vkAcquireNextImageKHR(device, swapchain, ~0ull, imageAvailable, 0, &imageIndex);
        if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR) {
            __android_log_print(4, "squashbtn", "vkAcquireNextImageKHR failed vr=%d, stopping render loop", (int)vr);
            return 0;
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
        rpBegin.framebuffer = offscreenFramebuffer;
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

        /* Copy the just-rendered offscreen frame into the real, acquired
         * swapchain image -- see sqw_copy_offscreen_to_swapchain()'s own
         * comment (near find_memory_type(), above) for why this exists
         * at all. */
        sqw_copy_offscreen_to_swapchain(cmd, offscreenImage, swapImages[imageIndex], extent.width, extent.height);

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

            if (action == ACTION_TEXT_CHANGED) {
                /* form_value was already updated directly by
                 * nativeTextChanged() before this flag was raised -- just
                 * redraw so the new characters appear via the existing
                 * glyph pipeline. None of the coordinate-based hit-test/
                 * hover/drag logic below applies to this synthetic action,
                 * so skip straight past all of it via this one guard
                 * (avoids a goto across the block-scoped locals below). */
                changed = 1;
            } else if (action == ACTION_DOWN) {
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
                    if (upHit >= 0) {
                        SqwBoxKind k = g_layout.boxes[upHit].kind;
                        DomNode *tappedNode = g_layout.boxes[upHit].node;
                        /* A tap landing on a button's own CAPTION rather
                         * than its background hit-tests into that
                         * caption's own SQW_BOX_TEXT word-box, not the
                         * button's box -- real for any button whose
                         * css_display isn't exactly CSS_DISPLAY_INLINE
                         * (e.g. #submitbtn's display:inline-block here).
                         * Re-target to the owning element first, before
                         * the label check below, so a tap anywhere on a
                         * button's own text still counts as tapping the
                         * button -- see this file's own node_direct_text()
                         * draw-site comment for the matching double-draw
                         * fix this pairs with. */
                        if (k == SQW_BOX_TEXT && tappedNode->parent) {
                            tappedNode = tappedNode->parent;
                            k = sqw_kind_for_node(tappedNode);
                        }
                        /* <label for="x">: hit-test the label's own box,
                         * but act as though the tap landed on the real
                         * target control instead -- cheap, self-contained,
                         * shares this same dispatch (no separate phase). */
                        if (k == SQW_BOX_SPAN && strcmp(tappedNode->tag, "label") == 0) {
                            const char *forId = dom_get_attr(tappedNode, "for");
                            DomNode *target = forId ? sqw_find_by_id(forId) : 0;
                            if (target) { tappedNode = target; k = sqw_kind_for_node(target); }
                        }
                        if (k == SQW_BOX_INPUT_CHECK) {
                            sqw_toggle_check_or_radio(tappedNode);
                            changed = 1;
                        } else if (k == SQW_BOX_BUTTON) {
                            const char *btype = dom_get_attr(tappedNode, "type");
                            int isSubmit = (strcmp(tappedNode->tag, "button") == 0) &&
                                           (!btype || strcmp(btype, "submit") == 0);
                            if (isSubmit) {
                                sqw_submit_form();
                            } else {
                                __android_log_print(4, "squashbtn", "button CLICKED (box#%d)", upHit);
                            }
                        }
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

/* The EditText is constructed once in onCreate() (see android_dex.c's
 * extended onCreate bytecode) and handed to native exactly once here. We
 * keep it alive past this call via NewGlobalRef (a local ref would be
 * freed once this native method returns) and resolve every jmethodID
 * we'll ever need against it up front -- GetMethodID/GetObjectClass are
 * real reflection-style lookups, not free, so doing this once here
 * instead of on every keystroke/tap is both correct (jmethodIDs are
 * stable for a class's lifetime) and the obviously right place for it.
 * (g_edit_text and friends are declared earlier in this file, forward of
 * nativeTouchEvent/sqw_dispatch_text_focus, which also need them.) */
void nativeEditTextReady(void *env, void *clazz, void *editText) {
    g_edit_text = sqw_jni_NewGlobalRef(env, (jobject)editText);
    jclass editTextClass = sqw_jni_GetObjectClass(env, g_edit_text);

    g_m_edittext_settext = sqw_jni_GetMethodID(env, editTextClass, "setText", "(Ljava/lang/CharSequence;)V");
    g_m_edittext_gettext = sqw_jni_GetMethodID(env, editTextClass, "getText", "()Ljava/lang/CharSequence;");
    g_m_edittext_requestfocus = sqw_jni_GetMethodID(env, editTextClass, "requestFocus", "()Z");
    g_m_edittext_clearfocus = sqw_jni_GetMethodID(env, editTextClass, "clearFocus", "()V");
    g_m_view_getwindowtoken = sqw_jni_GetMethodID(env, editTextClass, "getWindowToken", "()Landroid/os/IBinder;");

    /* toString() is declared on java.lang.Object, not EditText/TextView/
     * CharSequence -- resolved separately against that class, then called
     * later (via CallObjectMethodA) on whatever concrete object getText()
     * returns, exactly as real JNI code always does for an inherited
     * method. */
    jclass objectClass = sqw_jni_FindClass(env, "java/lang/Object");
    g_m_charsequence_tostring = sqw_jni_GetMethodID(env, objectClass, "toString", "()Ljava/lang/String;");

    /* InputMethodManager: obtained via the EditText's own getContext()
     * (the Activity it was constructed with), not a separate "this"
     * parameter -- nativeEditTextReady is a STATIC native method (see
     * android_dex.c's access-flags comment), so its own "clazz" parameter
     * is the SquashActivity jclass, not an instance; a View's Context IS
     * the real Activity instance that created it, which is exactly what
     * getSystemService() needs to be called on. */
    jmethodID m_getcontext = sqw_jni_GetMethodID(env, editTextClass, "getContext", "()Landroid/content/Context;");
    jobject contextObj = sqw_jni_CallObjectMethodA(env, g_edit_text, m_getcontext, 0);
    jclass contextClass = sqw_jni_GetObjectClass(env, contextObj);
    jmethodID m_getsystemservice = sqw_jni_GetMethodID(env, contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = sqw_jni_NewStringUTF(env, "input_method");
    jvalue svcArgs[1];
    svcArgs[0].l = serviceName;
    jobject imm = sqw_jni_CallObjectMethodA(env, contextObj, m_getsystemservice, svcArgs);
    g_imm = sqw_jni_NewGlobalRef(env, imm);
    jclass immClass = sqw_jni_GetObjectClass(env, g_imm);
    g_m_imm_showsoftinput = sqw_jni_GetMethodID(env, immClass, "showSoftInput", "(Landroid/view/View;I)Z");
    g_m_imm_hidesoftinputfromwindow = sqw_jni_GetMethodID(env, immClass, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");

    __android_log_print(4, "squashbtn", "nativeEditTextReady: edittext=%p imm=%p showSoftInput=%p hideSoftInput=%p",
                         (void *)g_edit_text, (void *)g_imm,
                         (void *)g_m_imm_showsoftinput, (void *)g_m_imm_hidesoftinputfromwindow);

    /* One-shot verification (see the approved plan's phase 2): prove a
     * real CallVoidMethodA call with a real jstring argument reaches the
     * real EditText by setting then immediately reading back a known
     * value, entirely independent of any touch/UI wiring. Cleared back to
     * empty immediately after so the (invisible, 1x1) EditText starts
     * genuinely blank rather than holding leftover test content. */
    jstring testStr = sqw_jni_NewStringUTF(env, "phase2 jni verification");
    jvalue args[1];
    args[0].l = testStr;
    sqw_jni_CallVoidMethodA(env, g_edit_text, g_m_edittext_settext, args);

    jobject gotten = sqw_jni_CallObjectMethodA(env, g_edit_text, g_m_edittext_gettext, 0);
    jstring gottenStr = (jstring)sqw_jni_CallObjectMethodA(env, gotten, g_m_charsequence_tostring, 0);
    const char *cstr = sqw_jni_GetStringUTFChars(env, gottenStr, 0);
    __android_log_print(4, "squashbtn", "nativeEditTextReady: setText/getText round-trip = %s", cstr ? cstr : "(null)");
    if (cstr) sqw_jni_ReleaseStringUTFChars(env, gottenStr, cstr);

    jstring emptyStr = sqw_jni_NewStringUTF(env, "");
    args[0].l = emptyStr;
    sqw_jni_CallVoidMethodA(env, g_edit_text, g_m_edittext_settext, args);
}

/* Phase 4/5 of the plan: real keystroke delivery. Called by the hand-
 * written SquashTextWatcher.onTextChanged (see android_dex.c) on the
 * Android UI thread every time the real EditText's text changes --
 * exactly once per edit, already converted to a real Java String on the
 * Java side via toString(). Copies it straight into the currently-
 * focused DomNode's form_value (bounded to that field's real 512-byte
 * size) and wakes the render thread via the existing touch-event queue
 * with the synthetic ACTION_TEXT_CHANGED action, so the new text actually
 * appears through the existing glyph pipeline on the very next frame. */
void nativeTextChanged(void *env, void *clazz, void *text) {
    (void)clazz;
    if (!g_has_focus || g_focused_box_index < 0 || g_focused_box_index >= g_layout.count) return;
    DomNode *node = g_layout.boxes[g_focused_box_index].node;
    const char *cstr = sqw_jni_GetStringUTFChars(env, (jstring)text, 0);
    if (cstr) {
        strncpy(node->form_value, cstr, sizeof(node->form_value) - 1);
        node->form_value[sizeof(node->form_value) - 1] = 0;
        sqw_jni_ReleaseStringUTFChars(env, (jstring)text, cstr);
    }
    g_touch_action = ACTION_TEXT_CHANGED;
    g_touch_pending = 1;
}
