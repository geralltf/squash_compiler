/* SQW: minimal HTML5 browser skeleton. Opens an SDL3 window, bridges to a
 * real Vulkan swapchain (see vk_context.c and SQW_GetWindowHWND /
 * SQW_GetWindowX11Display+SQW_GetWindowX11Window in SDL3_Build/sdl_core.inc),
 * parses a small embedded HTML document into a DOM (dom.c), computes a
 * trivial block layout (layout.c), and renders the resulting boxes as
 * flat-colored quads (renderer_vk.c) every frame. Links against the cached
 * SDL3_Build/sdl_common.sqo instead of recompiling SDL3 from source (see
 * Makefile.SQW / Makefile.SQW.linux). SDL3 itself is never modified by this
 * project. Platform split (Win32 HWND vs Xlib Display/Window) follows the
 * same #ifdef __linux__ convention as SDL3_Build/scratch/platform_shim.h. */
#ifdef __linux__
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
/* Direct single-TU include of the shared SDL3 subsystem body, NOT a link
 * against the cached SDL3_Build/sdl_common.sqo (the path Makefile.SQW uses
 * on Windows and every other file in this project defaults to) -- this
 * build hits a real, separate squash objfile_merge cross-object symbol
 * resolution bug where a symbol the .sqo's own export table genuinely
 * contains (SDL_fabsf, pulled in transitively via SDL3/SDL_rect.h's inline
 * SDL_RectsEqualEpsilon) still resolves as an unresolved dynamic import at
 * final link. sdl_core.inc already handles its own SDL3/SDL.h /
 * SDL3/SDL_main.h includes and "#undef main" (see its own comment there),
 * so neither is needed here. See SDL3_Build/clear_linux_unity.c's
 * identical comment for the first reproduction of this exact bug and
 * Makefile.SDL3.linux.clear for the established single-TU workaround this
 * follows -- left as a standing follow-up for whoever fixes the underlying
 * objfile_merge bug so the shared .sqo cache path can be used here too. */
#include "sdl_core.inc"
#else
#include <windows.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#undef main
#endif

/* #include, not separate `squash -c`/.sqo compilation+link: every one of
 * these crashed when compiled to its own .sqo and linked in (heap
 * corruption / access violation, reproduced with minimal repros that
 * differ ONLY in going through the .sqo object-merge path vs. a direct
 * single-TU compile) -- a real, currently-unresolved squash compiler bug
 * in cross-object linking for larger functions, not anything specific to
 * this code. #include sidesteps it entirely; see each file's own Makefile
 * comment. dom_walk.c pulls in dom.c and html_lexer.c the same way. */
#include "vk_context.c"
#include "dom_walk.c"
#include "layout.c"
#include "renderer_vk.c"
#include "text_renderer_vk.c"
#include "net_client.c"

#ifdef __linux__
/* squash_init_private_bootstrap()/SQW_GetWindowX11Display()/
 * SQW_GetWindowX11Window() are real, already-defined functions in this
 * same translation unit by this point (sdl_core.inc was #included above,
 * not linked in as a separate .sqo -- see that #include's own comment), so
 * no forward declaration is needed for them here, unlike the Windows
 * .sqo-linked branch below. Deliberately NOT declaring them "extern": per
 * unistd.h's own comment, "extern" on a bodyless prototype routes squash's
 * codegen through its cross-object SYM_IMPORT path, which is wrong for a
 * symbol that already has a real local definition earlier in this TU.
 * _exit() is the one genuine exception -- a real libc.so.6 export with no
 * body anywhere squash compiles, so it does need "extern" for the same
 * reason unistd.h's own syscall wrappers do. */
extern void _exit(int code);
#else
/* Defined in SDL3_Build/sdl_core.inc (baked into sdl_common.sqo); see
 * example_shim.inc's identical forward declaration for why this is needed
 * as a cross-object call. */
void squash_init_private_bootstrap(void);
extern void *SQW_GetWindowHWND(SDL_Window *window);
#endif

#define SQW_VIEWPORT_W 1024.0f
#define SQW_VIEWPORT_H 768.0f
#define SQW_PATH_MAX 512

static const char *SQW_INITIAL_PAGE = "SQW/testpages/index.html";

/* Reads a whole local file into a heap buffer (NUL-terminated), or returns
 * NULL on failure -- NOT fatal (unlike compiler.c's own read_file(), which
 * exit(1)s: a bad local href here is a normal, recoverable browsing event,
 * not a compiler input error). */
static char *sqw_read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, fp);
    buf[got] = '\0';
    fclose(fp);
    return buf;
}

/* Copies the directory part of `path` (including a trailing '/', or empty
 * if `path` has no '/') into `out` (size SQW_PATH_MAX) -- used to resolve
 * a page's own relative hrefs against wherever THAT page was actually
 * loaded from, not a fixed base directory, so a chain of relative links
 * (page in dir A links to a page in dir B, which links to a sibling of
 * itself) keeps resolving correctly. */
static void sqw_dirname(const char *path, char *out) {
    int len = (int)strlen(path);
    int cut = 0;
    int i;
    for (i = 0; i < len; i++) if (path[i] == '/') cut = i + 1;
    if (cut >= SQW_PATH_MAX) cut = SQW_PATH_MAX - 1;
    memcpy(out, path, (size_t)cut);
    out[cut] = '\0';
}

/* Loads and parses `path` as the new current page, replacing *root_ptr
 * and recomputing layout in place. On failure (file not found/unreadable)
 * leaves the current page entirely untouched and just logs a warning --
 * a broken local link should never crash or blank the browser. */
static void sqw_navigate_to(const char *path, DomNode **root_ptr, LayoutList *boxes_ptr,
                             char *current_dir, float viewport_w, float viewport_h) {
    char *html = sqw_read_file(path);
    if (!html) {
        fprintf(stderr, "SQW: navigate: cannot open %s\n", path); fflush(stdout);
        return;
    }
    DomNode *new_root = dom_parse(html);
    free(html);
    dom_free(*root_ptr);
    *root_ptr = new_root;
    layout_list_free(boxes_ptr);
    layout_compute(*root_ptr, viewport_w, viewport_h, boxes_ptr);
    sqw_dirname(path, current_dir);
    fprintf(stderr, "SQW: navigated to %s (%d boxes)\n", path, boxes_ptr->count); fflush(stdout);
}

/* Same document-swap as sqw_navigate_to(), but from an in-memory HTML
 * buffer (a fetched network response body) instead of a local file --
 * used by the SQW_TEST_CLICK_X/Y-independent real http(s):// anchor path.
 * current_dir is reset to empty: a page fetched over the network has no
 * local directory of its own for further relative-hrefs to resolve
 * against (out of scope here -- see net_client.h's own comment on URL
 * parsing being deliberately minimal). */
static void sqw_navigate_to_html(const char *html, DomNode **root_ptr, LayoutList *boxes_ptr,
                                  char *current_dir, float viewport_w, float viewport_h) {
    DomNode *new_root = dom_parse(html);
    dom_free(*root_ptr);
    *root_ptr = new_root;
    layout_list_free(boxes_ptr);
    layout_compute(*root_ptr, viewport_w, viewport_h, boxes_ptr);
    current_dir[0] = '\0';
    fprintf(stderr, "SQW: navigated to fetched page (%d boxes)\n", boxes_ptr->count); fflush(stdout);
}

/* Concatenates `node`'s DIRECT text-node children (no descent -- matches
 * layout.c's direct_text_width(), which sized the box this labels) into
 * `buf`. Used at draw time for <a>/<span>/<button>, none of which get
 * their own SQW_BOX_TEXT run from layout_compute() (they're laid out as
 * one opaque inline box, see layout.c's kind_for_tag comment). Returns
 * the number of characters written (not counting the NUL). */
static int concat_direct_text(const DomNode *node, char *buf, int bufcap) {
    int i, n = 0;
    for (i = 0; i < node->child_count && n < bufcap - 1; i++) {
        const DomNode *c = node->children[i];
        if (!dom_is_text(c)) continue;
        int len = (int)strlen(c->text);
        int room = bufcap - 1 - n;
        if (len > room) len = room;
        memcpy(buf + n, c->text, len);
        n += len;
    }
    buf[n] = 0;
    return n;
}

/* Classic web anchor palette: unvisited blue, visited purple (see
 * DomNode.visited in dom.h, set on click by the input-handling loop). */
static void anchor_color(const DomNode *a, float *r, float *g, float *b) {
    if (a->visited) { *r = 0.33f; *g = 0.10f; *b = 0.54f; }
    else            { *r = 0.00f; *g = 0.00f; *b = 0.93f; }
}

/* Draws every SQW_BOX_TEXT run plus the label text of every A/SPAN/BUTTON
 * box (see concat_direct_text's comment for why those need separate
 * handling), including the anchor underline. All queued glyph/rect draws
 * for the frame; caller must still sqw_text_renderer_flush() and this
 * function's own sqw_renderer_draw_rect() calls are already real draw
 * calls (not batched, see that function's own comment). */
static void draw_layout_text(SqwTextRenderer *tr, SqwVkContext *vk, SqwRenderer *renderer, VkCommandBuffer cmd,
                              LayoutList *boxes, float viewport_w, float viewport_h,
                              float scroll_x, float scroll_y) {
    int i;
    char label[256];
    for (i = 0; i < boxes->count; i++) {
        LayoutBox *b = &boxes->boxes[i];
        float bx = b->x - scroll_x;
        float by = b->y - scroll_y;
        if (b->kind == SQW_BOX_TEXT) {
            sqw_text_draw_string(tr, bx, by, b->node->text + b->text_start, b->text_len,
                SQW_TEXT_SCALE, 0.0f, 0.0f, 0.0f, 1.0f, viewport_w, viewport_h);
        } else if (b->kind == SQW_BOX_A) {
            float r, g, bl;
            anchor_color(b->node, &r, &g, &bl);
            /* Subtle hover feedback: a translucent-looking lighter tint by
             * blending toward white, cheap and doesn't need real alpha
             * blending on the (opaque) box pipeline. */
            if (b->node->hover) { r = r + (1.0f - r) * 0.35f; g = g + (1.0f - g) * 0.35f; bl = bl + (1.0f - bl) * 0.35f; }
            sqw_renderer_draw_rect(vk, renderer, cmd, bx, by + b->h - 2.0f, b->w, 2.0f, r, g, bl, viewport_w, viewport_h);
            concat_direct_text(b->node, label, sizeof label);
            sqw_text_draw_string(tr, bx, by, label, (int)strlen(label),
                SQW_TEXT_SCALE, r, g, bl, 1.0f, viewport_w, viewport_h);
        } else if (b->kind == SQW_BOX_SPAN) {
            concat_direct_text(b->node, label, sizeof label);
            sqw_text_draw_string(tr, bx, by, label, (int)strlen(label),
                SQW_TEXT_SCALE, 0.0f, 0.0f, 0.0f, 1.0f, viewport_w, viewport_h);
        } else if (b->kind == SQW_BOX_BUTTON) {
            concat_direct_text(b->node, label, sizeof label);
            float tr_col = b->node->active ? 0.9f : 0.1f;
            sqw_text_draw_string(tr, bx + 4.0f, by + 2.0f, label, (int)strlen(label),
                SQW_TEXT_SCALE, tr_col, tr_col, tr_col, 1.0f, viewport_w, viewport_h);
        }
    }
}

#define SQW_SCROLLBAR_THICKNESS 12.0f
#define SQW_SCROLLBAR_MIN_THUMB 24.0f

typedef struct {
    const char *target_id;
    DomNode *found;
} FindByIdCtx;

static void find_by_id_visit(DomNode *node, int depth, void *ctx) {
    (void)depth;
    FindByIdCtx *c = (FindByIdCtx *)ctx;
    if (c->found) return;
    const char *id = dom_get_attr(node, "id");
    if (id && strcmp(id, c->target_id) == 0) c->found = node;
}

/* Finds the element with the given id anywhere under root (dom_walk(), not
 * hand-rolled recursion -- matches this project's established iterative-
 * traversal convention, see dom_walk.c's own comment on why). */
static DomNode *find_by_id(DomNode *root, const char *id) {
    FindByIdCtx ctx;
    ctx.target_id = id; ctx.found = NULL;
    dom_walk(root, find_by_id_visit, &ctx);
    return ctx.found;
}

/* Finds the first LayoutBox whose ->node is exactly `node` -- used to
 * resolve an in-page "#fragment" anchor target (found via find_by_id) to
 * an actual on-screen position to scroll to. */
static LayoutBox *find_box_for_node(LayoutList *list, DomNode *node) {
    int i;
    for (i = 0; i < list->count; i++) if (list->boxes[i].node == node) return &list->boxes[i];
    return NULL;
}

/* layout_hit_test() returns the LAST (innermost, in paint order) box whose
 * rect contains the point -- for an <a>/<button>'s own label text, that is
 * the individual word's SQW_BOX_TEXT run pushed by place_text_node()
 * (child, painted after its parent), NOT the enclosing SQW_BOX_A/
 * SQW_BOX_BUTTON box, and a SQW_BOX_TEXT box's ->node is the TEXT node
 * itself, which has no "href" attribute and isn't a recognized interactive
 * kind. Since real anchor/button labels are made almost entirely of their
 * own word-boxes, most real clicks landed squarely on a word (silently
 * ignored) and only the narrow gaps between words / around the label
 * (still inside the <a>/<button>'s own box, but outside any word's box)
 * actually hit the interactive box directly -- confirmed as the root cause
 * of anchors/buttons feeling almost entirely unresponsive to real clicks.
 * Walks the DOM parent chain (DomNode::parent) to find the actual
 * clickable element a clicked word/run belongs to. */
static DomNode *interactive_ancestor(DomNode *node) {
    while (node) {
        if (strcmp(node->tag, "a") == 0 || strcmp(node->tag, "button") == 0) return node;
        node = node->parent;
    }
    return NULL;
}

/* Clamps a scroll offset to [0, max(0, content_extent - viewport_extent)]. */
static float clamp_scroll(float value, float content_extent, float viewport_extent) {
    float max_scroll = content_extent - viewport_extent;
    if (max_scroll < 0) max_scroll = 0;
    if (value < 0) return 0;
    if (value > max_scroll) return max_scroll;
    return value;
}

/* Draws the vertical/horizontal scrollbar track+thumb (only when content
 * overflows that axis) and returns their thumb rects via out params so
 * the caller can hit-test drag start against them -- 0-size (w==0/h==0)
 * when that axis doesn't need a scrollbar. Uses sqw_renderer_draw_rect
 * (see its own comment: real per-call draws, not part of the batched
 * LayoutList pass) since these aren't part of the page's own layout. */
static void draw_scrollbars(SqwVkContext *vk, SqwRenderer *renderer, VkCommandBuffer cmd,
                             float content_w, float content_h, float viewport_w, float viewport_h,
                             float scroll_x, float scroll_y, LayoutBox *out_vthumb, LayoutBox *out_hthumb) {
    out_vthumb->w = 0; out_hthumb->h = 0;
    if (content_h > viewport_h) {
        float track_x = viewport_w - SQW_SCROLLBAR_THICKNESS;
        sqw_renderer_draw_rect(vk, renderer, cmd, track_x, 0.0f, SQW_SCROLLBAR_THICKNESS, viewport_h,
            0.85f, 0.85f, 0.85f, viewport_w, viewport_h);
        float thumb_h = viewport_h * (viewport_h / content_h);
        if (thumb_h < SQW_SCROLLBAR_MIN_THUMB) thumb_h = SQW_SCROLLBAR_MIN_THUMB;
        float track_free = viewport_h - thumb_h;
        float max_scroll = content_h - viewport_h;
        float thumb_y = (max_scroll > 0) ? (scroll_y / max_scroll) * track_free : 0.0f;
        sqw_renderer_draw_rect(vk, renderer, cmd, track_x, thumb_y, SQW_SCROLLBAR_THICKNESS, thumb_h,
            0.55f, 0.55f, 0.55f, viewport_w, viewport_h);
        out_vthumb->x = track_x; out_vthumb->y = thumb_y; out_vthumb->w = SQW_SCROLLBAR_THICKNESS; out_vthumb->h = thumb_h;
    }
    if (content_w > viewport_w) {
        float track_y = viewport_h - SQW_SCROLLBAR_THICKNESS;
        sqw_renderer_draw_rect(vk, renderer, cmd, 0.0f, track_y, viewport_w, SQW_SCROLLBAR_THICKNESS,
            0.85f, 0.85f, 0.85f, viewport_w, viewport_h);
        float thumb_w = viewport_w * (viewport_w / content_w);
        if (thumb_w < SQW_SCROLLBAR_MIN_THUMB) thumb_w = SQW_SCROLLBAR_MIN_THUMB;
        float track_free = viewport_w - thumb_w;
        float max_scroll = content_w - viewport_w;
        float thumb_x = (max_scroll > 0) ? (scroll_x / max_scroll) * track_free : 0.0f;
        sqw_renderer_draw_rect(vk, renderer, cmd, thumb_x, track_y, thumb_w, SQW_SCROLLBAR_THICKNESS,
            0.55f, 0.55f, 0.55f, viewport_w, viewport_h);
        out_hthumb->x = thumb_x; out_hthumb->y = track_y; out_hthumb->w = thumb_w; out_hthumb->h = SQW_SCROLLBAR_THICKNESS;
    }
}

static int point_in_rect(float px, float py, const LayoutBox *r) {
    return px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h;
}

int main(void) {
    squash_init_private_bootstrap();

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SQW: SDL_Init failed: %s\n", SDL_GetError()); fflush(stdout);
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("SQW", (int)SQW_VIEWPORT_W, (int)SQW_VIEWPORT_H, 0);
    if (!window) {
        fprintf(stderr, "SQW: SDL_CreateWindow failed: %s\n", SDL_GetError()); fflush(stdout);
        return 1;
    }
    SDL_ShowWindow(window);

#ifdef __linux__
    Display *dpy = (Display *)SQW_GetWindowX11Display(window);
    Window win = (Window)SQW_GetWindowX11Window(window);
    if (!dpy || !win) {
        fprintf(stderr, "SQW: SQW_GetWindowX11Display/Window returned NULL\n"); fflush(stdout);
        return 1;
    }
#else
    HWND hwnd = (HWND)SQW_GetWindowHWND(window);
    if (!hwnd) {
        fprintf(stderr, "SQW: SQW_GetWindowHWND returned NULL\n"); fflush(stdout);
        return 1;
    }
    HINSTANCE hinstance = GetModuleHandleA(NULL);
#endif

    /* Heap-allocated, not a stack local: keeps these ~KB-sized structs off
     * the stack (see project memory on squash's past issues with large
     * stack-resident locals). */
    SqwVkContext *vk = (SqwVkContext *)malloc(sizeof(SqwVkContext));
#ifdef __linux__
    if (!sqw_vk_context_init(vk, dpy, win, ((uint32_t)SQW_VIEWPORT_W << 16) | (uint32_t)SQW_VIEWPORT_H)) {
#else
    if (!sqw_vk_context_init(vk, hinstance, hwnd, ((uint32_t)SQW_VIEWPORT_W << 16) | (uint32_t)SQW_VIEWPORT_H)) {
#endif
        fprintf(stderr, "SQW: Vulkan init failed\n"); fflush(stdout);
        return 1;
    }

    SqwRenderer *renderer = (SqwRenderer *)malloc(sizeof(SqwRenderer));
    if (!sqw_renderer_init(vk, renderer)) {
        fprintf(stderr, "SQW: renderer init failed\n"); fflush(stdout);
        return 1;
    }

    SqwTextRenderer *text_renderer = (SqwTextRenderer *)malloc(sizeof(SqwTextRenderer));
    if (!sqw_text_renderer_init(vk, text_renderer)) {
        fprintf(stderr, "SQW: text renderer init failed\n"); fflush(stdout);
        return 1;
    }

    char current_dir[SQW_PATH_MAX];
    sqw_dirname(SQW_INITIAL_PAGE, current_dir);
    char *initial_html = sqw_read_file(SQW_INITIAL_PAGE);
    if (!initial_html) {
        fprintf(stderr, "SQW: cannot open initial page %s\n", SQW_INITIAL_PAGE); fflush(stdout);
        return 1;
    }
    DomNode *root = dom_parse(initial_html);
    free(initial_html);
    LayoutList boxes;
    float viewport_w = SQW_VIEWPORT_W, viewport_h = SQW_VIEWPORT_H;
    layout_compute(root, viewport_w, viewport_h, &boxes);
    fprintf(stderr, "SQW: DOM parsed, layout computed (%d boxes, content %.0fx%.0f)\n",
        boxes.count, boxes.content_w, boxes.content_h); fflush(stdout);

    float scroll_x = 0.0f, scroll_y = 0.0f;
    DomNode *hover_node = NULL;
    DomNode *active_node = NULL;
    int dragging_v = 0, dragging_h = 0;
    float drag_anchor_mouse = 0.0f, drag_anchor_scroll = 0.0f;
    float mouse_x = 0.0f, mouse_y = 0.0f;
    SqwNetResult *pending_fetch = NULL; /* non-NULL while an http(s):// anchor click's background fetch is outstanding */

    /* Test-only synthetic input hook (SQW_TEST_CLICK_X/Y env vars): pushes
     * real SDL events through SDL_PushEvent() -- not a shortcut that
     * bypasses the event loop, the exact same SDL_PollEvent() path a real
     * XTest-injected or physical click takes -- at a fixed frame so local
     * automated testing doesn't depend on XTest actually reaching this
     * window (confirmed unreliable under this environment's Xwayland
     * setup: XTestFakeMotionEvent/ButtonEvent calls succeeded but SQW
     * never received a single resulting SDL_EVENT_MOUSE_MOTION). Off by
     * default; only active with the env var set. */
    int test_click_x = -1, test_click_y = -1;
    {
        const char *ex = getenv("SQW_TEST_CLICK_X");
        const char *ey = getenv("SQW_TEST_CLICK_Y");
        if (ex && ey) { test_click_x = atoi(ex); test_click_y = atoi(ey); }
    }
    /* SQW_TEST_KEY: same rationale as SQW_TEST_CLICK_X/Y above -- names one
     * of "pagedown"/"pageup"/"home"/"end"/"up"/"down", pushed as a real
     * SDL_EVENT_KEY_DOWN so it exercises the exact same code path a real
     * keypress does. */
    SDL_Scancode test_key = SDL_SCANCODE_UNKNOWN;
    {
        const char *tk = getenv("SQW_TEST_KEY");
        if (tk) {
            if (!strcmp(tk,"pagedown")) test_key = SDL_SCANCODE_PAGEDOWN;
            else if (!strcmp(tk,"pageup")) test_key = SDL_SCANCODE_PAGEUP;
            else if (!strcmp(tk,"home")) test_key = SDL_SCANCODE_HOME;
            else if (!strcmp(tk,"end")) test_key = SDL_SCANCODE_END;
            else if (!strcmp(tk,"up")) test_key = SDL_SCANCODE_UP;
            else if (!strcmp(tk,"down")) test_key = SDL_SCANCODE_DOWN;
        }
    }
    if (getenv("SQW_DUMP_BOXES")) {
        int bi;
        for (bi = 0; bi < boxes.count; bi++) {
            LayoutBox *db = &boxes.boxes[bi];
            fprintf(stderr, "[box] i=%d kind=%d x=%d y=%d w=%d h=%d\n",
                bi, db->kind, (int)db->x, (int)db->y, (int)db->w, (int)db->h);
        }
        fflush(stderr);
    }

    fprintf(stderr, "SQW: entering render loop\n"); fflush(stdout);
    int running = 1;
    int frame_count = 0;
    while (running) {
        if (test_click_x >= 0 && frame_count == 30) {
            SDL_Event mv; memset(&mv, 0, sizeof mv);
            mv.type = SDL_EVENT_MOUSE_MOTION;
            mv.motion.x = (float)test_click_x; mv.motion.y = (float)test_click_y;
            SDL_PushEvent(&mv);
        }
        if (test_click_x >= 0 && frame_count == 60) {
            SDL_Event bd; memset(&bd, 0, sizeof bd);
            bd.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            bd.button.button = 1; bd.button.down = 1;
            bd.button.x = (float)test_click_x; bd.button.y = (float)test_click_y;
            SDL_PushEvent(&bd);
            SDL_Event bu; memset(&bu, 0, sizeof bu);
            bu.type = SDL_EVENT_MOUSE_BUTTON_UP;
            bu.button.button = 1; bu.button.down = 0;
            bu.button.x = (float)test_click_x; bu.button.y = (float)test_click_y;
            SDL_PushEvent(&bu);
        }
        if (test_key != SDL_SCANCODE_UNKNOWN && frame_count == 30) {
            SDL_Event kd; memset(&kd, 0, sizeof kd);
            kd.type = SDL_EVENT_KEY_DOWN;
            kd.key.scancode = test_key; kd.key.down = true;
            SDL_PushEvent(&kd);
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = 0;
            } else if (ev.type == SDL_EVENT_WINDOW_RESIZED) {
                int new_w = ev.window.data1, new_h = ev.window.data2;
                if (new_w > 0 && new_h > 0) {
                    uint32_t packed = ((uint32_t)new_w << 16) | (uint32_t)new_h;
                    sqw_vk_recreate_swapchain(vk, packed);
                    viewport_w = (float)new_w; viewport_h = (float)new_h;
                    layout_list_free(&boxes);
                    layout_compute(root, viewport_w, viewport_h, &boxes);
                    scroll_x = clamp_scroll(scroll_x, boxes.content_w, viewport_w);
                    scroll_y = clamp_scroll(scroll_y, boxes.content_h, viewport_h);
                }
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                mouse_x = ev.motion.x; mouse_y = ev.motion.y;
                if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[motion] x=%d y=%d\n", (int)mouse_x, (int)mouse_y); fflush(stderr); }
                if (dragging_v) {
                    float track_free = viewport_h - SQW_SCROLLBAR_MIN_THUMB;
                    float thumb_h = viewport_h * (viewport_h / boxes.content_h);
                    if (thumb_h < SQW_SCROLLBAR_MIN_THUMB) thumb_h = SQW_SCROLLBAR_MIN_THUMB;
                    track_free = viewport_h - thumb_h;
                    float delta_mouse = mouse_y - drag_anchor_mouse;
                    float scale = (track_free > 0) ? (boxes.content_h - viewport_h) / track_free : 0.0f;
                    scroll_y = clamp_scroll(drag_anchor_scroll + delta_mouse * scale, boxes.content_h, viewport_h);
                } else if (dragging_h) {
                    float thumb_w = viewport_w * (viewport_w / boxes.content_w);
                    if (thumb_w < SQW_SCROLLBAR_MIN_THUMB) thumb_w = SQW_SCROLLBAR_MIN_THUMB;
                    float track_free = viewport_w - thumb_w;
                    float delta_mouse = mouse_x - drag_anchor_mouse;
                    float scale = (track_free > 0) ? (boxes.content_w - viewport_w) / track_free : 0.0f;
                    scroll_x = clamp_scroll(drag_anchor_scroll + delta_mouse * scale, boxes.content_w, viewport_w);
                } else {
                    float cx = mouse_x + scroll_x, cy = mouse_y + scroll_y;
                    int hit = layout_hit_test(&boxes, cx, cy);
                    DomNode *new_hover = NULL;
                    if (hit >= 0) {
                        SqwBoxKind k = boxes.boxes[hit].kind;
                        if (k == SQW_BOX_A || k == SQW_BOX_BUTTON) new_hover = boxes.boxes[hit].node;
                        else if (k == SQW_BOX_TEXT) new_hover = interactive_ancestor(boxes.boxes[hit].node);
                    }
                    if (new_hover != hover_node) {
                        if (hover_node) hover_node->hover = 0;
                        if (new_hover) new_hover->hover = 1;
                        hover_node = new_hover;
                    }
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == 1) {
                if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[button-down] x=%d y=%d\n", (int)ev.button.x, (int)ev.button.y); fflush(stderr); }
                LayoutBox vthumb, hthumb;
                vthumb.w = 0; hthumb.h = 0;
                if (boxes.content_h > viewport_h) {
                    float thumb_h = viewport_h * (viewport_h / boxes.content_h);
                    if (thumb_h < SQW_SCROLLBAR_MIN_THUMB) thumb_h = SQW_SCROLLBAR_MIN_THUMB;
                    float track_free = viewport_h - thumb_h;
                    float max_scroll = boxes.content_h - viewport_h;
                    float thumb_y = (max_scroll > 0) ? (scroll_y / max_scroll) * track_free : 0.0f;
                    vthumb.x = viewport_w - SQW_SCROLLBAR_THICKNESS; vthumb.y = thumb_y;
                    vthumb.w = SQW_SCROLLBAR_THICKNESS; vthumb.h = thumb_h;
                }
                if (boxes.content_w > viewport_w) {
                    float thumb_w = viewport_w * (viewport_w / boxes.content_w);
                    if (thumb_w < SQW_SCROLLBAR_MIN_THUMB) thumb_w = SQW_SCROLLBAR_MIN_THUMB;
                    float track_free = viewport_w - thumb_w;
                    float max_scroll = boxes.content_w - viewport_w;
                    float thumb_x = (max_scroll > 0) ? (scroll_x / max_scroll) * track_free : 0.0f;
                    hthumb.x = thumb_x; hthumb.y = viewport_h - SQW_SCROLLBAR_THICKNESS;
                    hthumb.w = thumb_w; hthumb.h = SQW_SCROLLBAR_THICKNESS;
                }
                LayoutBox vtrack, htrack;
                vtrack.x = viewport_w - SQW_SCROLLBAR_THICKNESS; vtrack.y = 0.0f;
                vtrack.w = SQW_SCROLLBAR_THICKNESS; vtrack.h = viewport_h;
                htrack.x = 0.0f; htrack.y = viewport_h - SQW_SCROLLBAR_THICKNESS;
                htrack.w = viewport_w; htrack.h = SQW_SCROLLBAR_THICKNESS;
                if (vthumb.w > 0 && point_in_rect(ev.button.x, ev.button.y, &vthumb)) {
                    dragging_v = 1; drag_anchor_mouse = ev.button.y; drag_anchor_scroll = scroll_y;
                } else if (hthumb.h > 0 && point_in_rect(ev.button.x, ev.button.y, &hthumb)) {
                    dragging_h = 1; drag_anchor_mouse = ev.button.x; drag_anchor_scroll = scroll_x;
                } else if (vthumb.w > 0 && point_in_rect(ev.button.x, ev.button.y, &vtrack)) {
                    /* Clicked the empty ("white") vertical track above/below
                     * the thumb -- classic scrollbar UX (matches every
                     * desktop toolkit's own track-click behavior) is to page
                     * up/down by one viewport-height toward the click, NOT
                     * to jump the thumb straight to the click position (that
                     * jump-to-click behavior is scrollbar THUMB-drag/click
                     * behavior, a different, more abrupt interaction some
                     * platforms use only as an opt-in setting) and NOT to
                     * start a drag -- a single track click is one discrete
                     * page step, not a drag gesture. */
                    if (ev.button.y < vthumb.y) scroll_y = clamp_scroll(scroll_y - viewport_h, boxes.content_h, viewport_h);
                    else scroll_y = clamp_scroll(scroll_y + viewport_h, boxes.content_h, viewport_h);
                } else if (hthumb.h > 0 && point_in_rect(ev.button.x, ev.button.y, &htrack)) {
                    if (ev.button.x < hthumb.x) scroll_x = clamp_scroll(scroll_x - viewport_w, boxes.content_w, viewport_w);
                    else scroll_x = clamp_scroll(scroll_x + viewport_w, boxes.content_w, viewport_w);
                } else {
                    float cx = ev.button.x + scroll_x, cy = ev.button.y + scroll_y;
                    int hit = layout_hit_test(&boxes, cx, cy);
                    if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[hit-test] cx=%d cy=%d hit=%d kind=%d\n", (int)cx, (int)cy, hit, hit>=0?(int)boxes.boxes[hit].kind:-1); fflush(stderr); }
                    if (hit >= 0) {
                        LayoutBox *hb = &boxes.boxes[hit];
                        /* See interactive_ancestor()'s own comment: a hit on
                         * an <a>/<button>'s own label text lands on the word's
                         * SQW_BOX_TEXT run, not the enclosing interactive box,
                         * so resolve to the real clickable element first. */
                        DomNode *target_node = hb->node;
                        SqwBoxKind eff_kind = hb->kind;
                        if (eff_kind == SQW_BOX_TEXT) {
                            DomNode *anc = interactive_ancestor(hb->node);
                            if (anc) { target_node = anc; eff_kind = (strcmp(anc->tag, "a") == 0) ? SQW_BOX_A : SQW_BOX_BUTTON; }
                        }
                        if (eff_kind == SQW_BOX_A) {
                            target_node->visited = 1;
                            target_node->active = 1;
                            active_node = target_node;
                            /* dom_get_attr's returned pointer lives inside the
                             * CURRENT DomNode -- copy it before any possible
                             * sqw_navigate_to() below, which frees the whole
                             * current DOM tree (including target_node itself,
                             * making it/active_node dangling). */
                            const char *href_raw = dom_get_attr(target_node, "href");
                            char href[SQW_PATH_MAX];
                            href[0] = 0;
                            if (href_raw) { strncpy(href, href_raw, sizeof href - 1); href[sizeof href - 1] = 0; }
                            if (href[0] == '#') {
                                DomNode *target = find_by_id(root, href + 1);
                                LayoutBox *tb = target ? find_box_for_node(&boxes, target) : NULL;
                                if (tb) scroll_y = clamp_scroll(tb->y, boxes.content_h, viewport_h);
                            } else if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
                                if (pending_fetch) {
                                    /* a previous fetch is still outstanding -- drop it rather than
                                     * leak it or race two responses against one DOM swap */
                                    sqw_net_result_free(pending_fetch);
                                }
                                fprintf(stderr, "SQW: fetching %s ...\n", href); fflush(stdout);
                                pending_fetch = sqw_net_fetch_async(href);
                            } else if (href[0]) {
                                /* Local relative path: resolve against the
                                 * CURRENT page's own directory, not a fixed
                                 * base -- see sqw_dirname()'s comment. */
                                char full_path[SQW_PATH_MAX];
                                snprintf(full_path, sizeof full_path, "%s%s", current_dir, href);
                                sqw_navigate_to(full_path, &root, &boxes, current_dir, viewport_w, viewport_h);
                                scroll_x = 0.0f; scroll_y = 0.0f;
                                hover_node = NULL; active_node = NULL; /* old DOM (and target_node) is gone */
                            }
                        } else if (eff_kind == SQW_BOX_BUTTON) {
                            target_node->active = 1;
                            active_node = target_node;
                        }
                    }
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == 1) {
                if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[button-up] x=%d y=%d\n", (int)ev.button.x, (int)ev.button.y); fflush(stderr); }
                dragging_v = 0; dragging_h = 0;
                if (active_node) { active_node->active = 0; active_node = NULL; }
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                scroll_y = clamp_scroll(scroll_y - ev.wheel.y * ((SQW_FONT_CELL_H * SQW_TEXT_SCALE * 3.0f)), boxes.content_h, viewport_h);
                if (ev.wheel.x != 0.0f) scroll_x = clamp_scroll(scroll_x - ev.wheel.x * ((SQW_FONT_CELL_H * SQW_TEXT_SCALE * 3.0f)), boxes.content_w, viewport_w);
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[key-down] scancode=%d\n", (int)ev.key.scancode); fflush(stderr); }
                /* Page Up/Down page by one viewport (matches the scrollbar
                 * track-click behavior -- see its own comment), Home/End
                 * jump to the very top/bottom, Up/Down nudge by one text
                 * line -- the standard keyboard scrolling set every desktop
                 * browser/reader supports, translated from real X11 key
                 * events by SDL3_Build/sdl_core.inc's own PRIVATE_PumpEvents
                 * (only this small fixed set of navigation keys, not a full
                 * keymap -- see that function's own comment). */
                if (ev.key.scancode == SDL_SCANCODE_PAGEDOWN) {
                    scroll_y = clamp_scroll(scroll_y + viewport_h, boxes.content_h, viewport_h);
                } else if (ev.key.scancode == SDL_SCANCODE_PAGEUP) {
                    scroll_y = clamp_scroll(scroll_y - viewport_h, boxes.content_h, viewport_h);
                } else if (ev.key.scancode == SDL_SCANCODE_HOME) {
                    scroll_y = 0.0f;
                } else if (ev.key.scancode == SDL_SCANCODE_END) {
                    scroll_y = clamp_scroll(boxes.content_h, boxes.content_h, viewport_h);
                } else if (ev.key.scancode == SDL_SCANCODE_DOWN) {
                    scroll_y = clamp_scroll(scroll_y + (SQW_FONT_CELL_H * SQW_TEXT_SCALE * 3.0f), boxes.content_h, viewport_h);
                } else if (ev.key.scancode == SDL_SCANCODE_UP) {
                    scroll_y = clamp_scroll(scroll_y - (SQW_FONT_CELL_H * SQW_TEXT_SCALE * 3.0f), boxes.content_h, viewport_h);
                }
            }
        }
        if (!running) break;

        if (pending_fetch) {
            pthread_mutex_lock(&pending_fetch->mutex);
            int fetch_ready = pending_fetch->ready;
            int fetch_success = pending_fetch->success;
            char *fetch_body = pending_fetch->body;
            pthread_mutex_unlock(&pending_fetch->mutex);
            if (fetch_ready) {
                if (fetch_success) {
                    sqw_navigate_to_html(fetch_body, &root, &boxes, current_dir, viewport_w, viewport_h);
                    scroll_x = 0.0f; scroll_y = 0.0f;
                    hover_node = NULL; active_node = NULL; /* old DOM is gone */
                } else {
                    fprintf(stderr, "SQW: fetch failed\n"); fflush(stdout);
                }
                sqw_net_result_free(pending_fetch);
                pending_fetch = NULL;
            }
        }

        uint32_t imageIndex = 0;
        VkCommandBuffer cmd = sqw_vk_begin_frame(vk, 0.95f, 0.95f, 0.95f, 1.0f, &imageIndex);
        if (!cmd) continue; /* e.g. minimized (0x0 extent) -- just skip this frame, not fatal */
        sqw_renderer_draw(vk, renderer, cmd, &boxes, viewport_w, viewport_h, scroll_x, scroll_y);
        draw_layout_text(text_renderer, vk, renderer, cmd, &boxes, viewport_w, viewport_h, scroll_x, scroll_y);
        LayoutBox vthumb_dummy, hthumb_dummy;
        draw_scrollbars(vk, renderer, cmd, boxes.content_w, boxes.content_h, viewport_w, viewport_h, scroll_x, scroll_y, &vthumb_dummy, &hthumb_dummy);
        sqw_text_renderer_flush(vk, text_renderer, cmd, viewport_w, viewport_h);
        sqw_vk_end_frame(vk, cmd, imageIndex);

        frame_count++;
        if (frame_count % 300 == 0) { fprintf(stderr, "SQW: frame=%d\n", frame_count); fflush(stdout); }
    }

    fprintf(stderr, "SQW: render loop finished, frame_count=%d\n", frame_count); fflush(stdout);

    /* Skip Vulkan/SDL teardown and exit directly (_exit on Linux,
     * TerminateProcess on Windows) -- see triangle_vulkan.c's own comment
     * / platform_shim.h's platform_exit() for why: on Windows, the Vulkan
     * ICD's DLL unload path hangs on this machine after real device/
     * swapchain work has been done, an environment/driver quirk unrelated
     * to squash or SQW; _exit() on Linux mirrors that by skipping atexit
     * handlers and any shared-library destructors (the Vulkan ICD's own
     * included) for the same reason. All real work above (window, device,
     * swapchain, N rendered frames) already completed successfully by this
     * point. */
#ifdef __linux__
    _exit(0);
#else
    TerminateProcess(GetCurrentProcess(), 0);
#endif
    return 0;
}
