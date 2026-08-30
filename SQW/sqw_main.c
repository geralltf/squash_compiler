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
#include <unistd.h>
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
#include "css.c"
#include "layout.c"
#include "js_engine.c"
#include "renderer_vk.c"
#include "text_renderer_vk.c"
#include "image_renderer_vk.c"
#include "net_client.c"
#include "img_decode_png.c"
#include "img_decode_gif.c"
#include "img_decode_jpeg.c"
#include "image_cache.c"
/* C# scripting (Phase 5 real slice -- see sqw_run_csharp_script()'s own
 * comment below): the in-process ".sqo" loader plus a host symbol table
 * of every CSR/csharp_rt.h function a compiled C# script can call.
 * CSR/csharp_rt.c is #included directly here (same single-TU convention
 * every other SQW dependency above already uses) so its functions live
 * in THIS process, at real, known addresses sqo_host_syms_csharp_rt()
 * can hand to the loader.
 *
 * sqo_loader.c's own ".sqo" reading is reimplemented locally as
 * sqo_read_objfile() (see sqo_loader.c's own comment right above it)
 * instead of #including the REAL objfile.c -- that file's objfile_read()
 * needs diag_emit() (diag.c) and my_strdup() (ast.c), which would drag
 * squash's entire compiler-internals AST module (ast_print, typeinfo_*,
 * dozens of functions with nothing to do with SQW) into the browser
 * binary for the sake of two small helper calls. objfile.h's own binary
 * format (the struct definition, pulled in via sqo_loader.h) is a
 * simple, stable, documented layout -- reimplementing just the read path
 * against it here is a small, low-risk amount of duplication next to
 * that alternative. */
#include "sqo_loader.c"
#include "sqo_host_syms.c"
#include "../CSR/csharp_rt.c"

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

/* Back-button history: a fixed-capacity LIFO of previously-requested
 * URLs/paths, heap-allocated (not a C-stack local -- see main()'s own
 * comment on keeping KB-sized structs off the stack) so it lives for the
 * whole process and is never subject to C-stack-overflow risk itself.
 * Every push/pop below is bounds-checked -- push never writes past
 * SQW_HISTORY_MAX (a long or adversarial browsing session cannot grow
 * this past a fixed, known size: once full, the OLDEST entry is dropped
 * to make room for the newest, same as any bounded ring/stack), and pop
 * never reads before index 0 (an empty stack just reports "nothing to go
 * back to" rather than under-reading). A popped slot is wiped immediately
 * so a stale URL never lingers in memory once it's no longer reachable
 * through `count`. */
#define SQW_HISTORY_MAX 64

typedef struct {
    char urls[SQW_HISTORY_MAX][SQW_PATH_MAX];
    int count; /* number of live entries, always 0..SQW_HISTORY_MAX */
} SqwHistoryStack;

static void sqw_history_push(SqwHistoryStack *h, const char *url) {
    if (!url || !url[0]) return;
    if (h->count >= SQW_HISTORY_MAX) {
        memmove(h->urls[0], h->urls[1], (size_t)(SQW_HISTORY_MAX - 1) * SQW_PATH_MAX);
        h->count = SQW_HISTORY_MAX - 1;
    }
    strncpy(h->urls[h->count], url, SQW_PATH_MAX - 1);
    h->urls[h->count][SQW_PATH_MAX - 1] = 0;
    h->count++;
}

/* Returns 1 and fills `out` (capacity outcap) with the most recently
 * pushed URL on success; returns 0 (out left untouched) if the stack is
 * empty -- the caller (the Back button's click handler) uses this to
 * decide whether there's anywhere to go back to at all. */
static int sqw_history_pop(SqwHistoryStack *h, char *out, int outcap) {
    if (h->count <= 0) return 0;
    h->count--;
    strncpy(out, h->urls[h->count], outcap - 1);
    out[outcap - 1] = 0;
    memset(h->urls[h->count], 0, sizeof h->urls[h->count]);
    return 1;
}

/* Finds every <style> element anywhere in the tree, feeds its text
 * content into a fresh CssStylesheet (in document order, across however
 * many separate <style> tags the page has -- real cascade order), then
 * resolves every element's final computed style from it plus that
 * element's own inline style="" (see css.h/css.c's own top comments for
 * exactly what CSS this covers). <link rel="stylesheet" href="..."> --
 * an EXTERNAL stylesheet -- is deliberately NOT fetched: that would need
 * its own network round trip layered into page navigation, out of scope
 * for this pass. A real site's externally-linked CSS (as opposed to its
 * own inline <style> blocks) currently has no effect here -- a real,
 * known limitation, not a silent gap. */
/* Kept alive for the CURRENT page (not freed at the end of sqw_apply_css()
 * the way its own local used to be) so sqw_handle_event()'s mouse-move
 * hover tracking can re-resolve just the old/new hover_node's style via
 * css_apply_one() when hover changes -- see that call site's own comment
 * for why ":hover" needs this to have any live visual effect at all. A
 * plain file-scope static rather than a new SqwAppState field + threading
 * a CssStylesheet* through every navigation function's signature (
 * sqw_navigate_to/sqw_navigate_to_html/sqw_go_navigate/submit_form and
 * every one of THEIR several call sites) -- same "avoid a wide signature
 * change" rationale layout.c's own g_image_size_lookup static already
 * uses. Freed and re-initialized on every navigation (see sqw_apply_css()
 * itself), so it's always exactly the sheet the CURRENTLY-loaded page's
 * own <style> tags produced. */
static CssStylesheet g_current_css_sheet;
static int g_current_css_sheet_valid = 0;

/* The JS interpreter for whichever page is CURRENTLY loaded -- same
 * "freed and replaced on every navigation, referenced by a file-scope
 * static rather than threaded through every navigation function's
 * signature" rationale as g_current_css_sheet just above (see its own
 * comment); also needed by sqw_handle_event()'s click handling, to call
 * js_dispatch_click() for whatever element was actually clicked. NULL
 * whenever no page has run js_run_script() yet (impossible in practice --
 * sqw_apply_css() below always calls it, even for a script-free page, so
 * onclick="" attributes still get wired -- but checked anyway everywhere
 * it's read, cheaply, since NULL is also this variable's own valid
 * "nothing to call" state). */
static JSInterp *g_current_js_interp = 0;

/* Pending fetch() calls issued by JS (see js_engine.h's own fetch-hook
 * comment) -- a small fixed-slot table, same convention/rationale as
 * g_current_js_interp above. Each slot pairs a real background
 * SqwNetResult* (net_client.c's own async fetch -- exactly what page
 * navigation itself already uses, just not swapped in as the whole
 * document) with the fetch_id js_engine.c is tracking on its own side --
 * sqw_js_fetch_start() (the actual hook, registered once in main()) fills
 * a slot when a script calls fetch(); sqw_poll_js_fetches() (called once
 * per frame, right next to js_run_timers()) polls every used slot and
 * delivers each ready one via js_deliver_fetch_result(). */
#define SQW_MAX_JS_FETCHES 16
typedef struct {
    SqwNetResult *result;
    long fetch_id;
    int used;
} SqwJsFetch;
static SqwJsFetch g_js_fetches[SQW_MAX_JS_FETCHES];

static void sqw_js_fetch_start(const char *url, long fetch_id, void *user_data) {
    (void)user_data;
    int i;
    for (i = 0; i < SQW_MAX_JS_FETCHES; i++) {
        if (g_js_fetches[i].used) continue;
        g_js_fetches[i].result = sqw_net_fetch_async(url);
        g_js_fetches[i].fetch_id = fetch_id;
        g_js_fetches[i].used = 1;
        return;
    }
    /* No free slot -- silently dropped, same generous-but-bounded-cap
       convention as js_engine.c's own JS_MAX_FETCHES on the other side
       of this hook. */
}

/* Called once per frame (see the main loop, right next to
 * js_run_timers()). Delivers every fetch() that has finished since the
 * last poll to whatever JS callback js_engine.c is still holding for
 * it -- see js_deliver_fetch_result()'s own comment on why it's always
 * safe to call even if the page navigated away (a fresh JSInterp with no
 * memory of that fetch_id) since this hook started it. */
static void sqw_poll_js_fetches(int *relayout_needed) {
    if (relayout_needed) *relayout_needed = 0;
    int i;
    for (i = 0; i < SQW_MAX_JS_FETCHES; i++) {
        if (!g_js_fetches[i].used) continue;
        SqwNetResult *r = g_js_fetches[i].result;
        pthread_mutex_lock(&r->mutex);
        int ready = r->ready;
        int success = r->success;
        char *body = r->body;
        pthread_mutex_unlock(&r->mutex);
        if (!ready) continue;
        long fetch_id = g_js_fetches[i].fetch_id;
        g_js_fetches[i].used = 0;
        if (g_current_js_interp) {
            int js_relayout = 0;
            js_deliver_fetch_result(g_current_js_interp, fetch_id, success ? body : "", success, &js_relayout);
            if (js_relayout && relayout_needed) *relayout_needed = 1;
        }
        sqw_net_result_free(r);
    }
}

#define SQW_SCRIPT_BUF_CAP 65536
/* <script type="module">'s own real (not concatenated) source text --
   see sqw_apply_css()'s own comment on why these are collected
   separately from ordinary script_buf text. `id` is that module's own
   resolved src URL (or, for an inline module script, an auto-generated
   "inline-module#N" id) -- js_run_module()'s own key for "import ...
   from" resolution against sibling modules. `text` is malloc'd, owned by
   whoever collected it (freed right after js_run_module() runs it). */
#define SQW_MAX_MODULE_SCRIPTS 16
typedef struct {
    char id[192];
    char *text;
} SqwModuleScript;

/* <script type="text/csharp"> -- collected separately from ordinary
   script_buf/modules[] text, same reasoning as SqwModuleScript above:
   each is a self-contained C# program, not something that can be
   concatenated with JS text or parsed by js_run_script()/js_run_module()
   at all. See sqw_run_csharp_script()'s own comment (right below) for
   how these actually get executed -- this first integration compiles
   each one via a real "squash" subprocess and runs the result once at
   page load, surfacing its Console.WriteLine/Write output the same way
   js_native_console_log() surfaces console.log (see that function's own
   comment): no in-process DOM-event dispatch yet (that needs a real
   runtime .sqo loader inside SQW, tracked as a documented follow-up in
   the plan at /home/squash/.claude/plans/nested-finding-walrus.md's
   Phase 5 notes -- this is a first, real, working slice of it, not the
   complete design). */
#define SQW_MAX_CSHARP_SCRIPTS 16
typedef struct {
    char id[192];
    char *text;
} SqwCsharpScript;

/* Phase 6f: <script type="text/c"> -- raw squash C, the most mechanical
 * remaining Phase 6 piece (see sqw_run_c_script()'s own comment for how
 * it differs from the C# path above -- barely at all). Same collect-
 * separately reasoning as SqwCsharpScript. */
#define SQW_MAX_C_SCRIPTS 16
typedef struct {
    char id[192];
    char *text;
} SqwCScript;

/* squash's own include/stdlib.h shim doesn't declare system() -- hand-
 * declared here rather than widening the shim (out of scope for this
 * integration), matching this project's own established pattern for a
 * missing libc declaration (e.g. setjmp/longjmp in CSR/csharp_rt.h,
 * strtoll in CS/cs_lexer.c). Verified this session that squash-compiled
 * code calling it works correctly. Still needed for the COMPILE step
 * below (see this function's own comment on why compiling stays a real
 * subprocess even though running is now in-process). */
extern int system(const char *command);

/* Compiles `cs_text` (one whole ".cs" program -- a real, current
 * limitation: unlike JS's script_buf concatenation, each <script
 * type="text/csharp"> block is its own independent compile, so two such
 * blocks on one page do NOT share classes/state the way real C# static
 * fields might imply -- documented, not silently wrong) via a real
 * "squash -c" subprocess (see compiler.c's own ".cs" handling --
 * lower_csharp_file()/is_csharp_source_path() -- this is the exact same
 * "squash foo.cs -c -o foo.sqo" path any user would invoke by hand, just
 * shelled out to rather than reusing in-process: recompiling THIS
 * process's own CodeGen/Assembler global state mid-render for a wholly
 * different translation unit would be a real, avoidable risk).
 *
 * RUNNING the result, unlike compiling it, IS now in-process: the
 * compiled ".sqo" is loaded directly into SQW's own memory via
 * sqo_loader_load() (SQW/sqo_loader.c) and its "main" export (cs_lower.c
 * always emits a real `int main(void) { <MainClass>__Main(); return 0;
 * }` wrapper, present in the export table even for a "-c" precompile --
 * see cs_lower.c's own comment on why this is simpler than tracking the
 * C# class name at the SQW level) is called as a genuine, real function
 * pointer in this process. This is the actual novel Phase 5 capability
 * (see /home/squash/.claude/plans/nested-finding-walrus.md): previous
 * sessions' version of this function ran a full subprocess and captured
 * its stdout with an "SQW/cs console:" prefix; now Console.WriteLine's
 * real csr_console_write_line() (CSR/csharp_rt.c, linked directly into
 * SQW -- see this file's own top-of-block #include comment) writes to
 * SQW's OWN stdout directly, the same as any other in-process code would
 * -- there is no longer a subprocess boundary to prefix output across.
 * Real DOM-event wiring (binding a loaded script's own exported methods
 * to click/input/etc, the way js_engine.c's dispatch works) is not yet
 * implemented -- this still only calls the script's top-level entry
 * point once, at page load, like a classic top-level script; that's the
 * next remaining piece, not this one. A compile, load, or symbol-lookup
 * failure is logged and otherwise swallowed (same "log and keep going,
 * never blank the page" convention sqw_load_script_src() documents for a
 * broken script src). `id` is used only for diagnostic messages (the
 * script's resolved src URL, or an auto-generated "inline-csharp#N" id
 * for an inline block, mirroring SqwModuleScript's own `id` convention). */
static void sqw_run_csharp_script(const char *cs_text, const char *id) {
    char src_path[256];
    char sqo_path[256];
    char cmd[1024];
    FILE *f;
    static int g_csharp_run_counter = 0;
    SqoLoaded loaded;
    SqoHostSymbol host_syms[SQO_HOST_SYMS_COUNT + SQO_HOST_SYMS_VULKAN_COUNT];
    int n_host_syms, n_rt_syms;
    int (*main_fn)(void);

    snprintf(src_path, sizeof src_path, "/tmp/sqw_cs_%d_%d.cs", (int)getpid(), g_csharp_run_counter);
    snprintf(sqo_path, sizeof sqo_path, "/tmp/sqw_cs_%d_%d.sqo", (int)getpid(), g_csharp_run_counter);
    g_csharp_run_counter++;

    f = fopen(src_path, "w");
    if (!f) { fprintf(stderr, "SQW: C# script '%s': could not create temp file %s\n", id, src_path); fflush(stderr); return; }
    fputs(cs_text, f);
    fclose(f);

    snprintf(cmd, sizeof cmd, "./squash -c -linux -64 %s -o %s >/tmp/sqw_cs_compile.log 2>&1", src_path, sqo_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "SQW: C# script '%s' failed to compile -- see /tmp/sqw_cs_compile.log\n", id);
        fflush(stderr);
        remove(src_path);
        return;
    }

    /* Phase 6c: a C# script's [DllImport("vulkan")]-declared calls need
     * the real Vulkan host symbols too (SQW/sqo_host_syms.c's
     * sqo_host_syms_vulkan()) -- not wiring this in here left every such
     * call unresolved at load time even though the compile step itself
     * succeeds (a script can DECLARE and CALL a real Vulkan function, but
     * without these entries sqo_loader_load() would fail with "unresolved
     * function symbol"). SQW already links real libvulkan.so.1 directly
     * (SQW/vk_context.c), so these are the same real functions, not a
     * separate/stub table. */
    n_rt_syms = sqo_host_syms_csharp_rt(host_syms);
    n_host_syms = n_rt_syms + sqo_host_syms_vulkan(host_syms + n_rt_syms);
    if (!sqo_loader_load(sqo_path, host_syms, n_host_syms, &loaded)) {
        fprintf(stderr, "SQW: C# script '%s' compiled but failed to load in-process\n", id);
        fflush(stderr);
        remove(src_path); remove(sqo_path);
        return;
    }

    main_fn = (int (*)(void))sqo_loader_get_symbol(&loaded, "main");
    if (!main_fn) {
        fprintf(stderr, "SQW: C# script '%s': loaded but has no 'main' export\n", id);
        fflush(stderr);
    } else {
        main_fn();
    }

    sqo_loader_free(&loaded);
    remove(src_path);
    remove(sqo_path);
}

/* Phase 6f: <script type="text/c"> -- the most mechanical Phase 6 piece.
 * `c_text` is already valid squash C (no lowering step at all, unlike
 * sqw_run_csharp_script() right above -- this is the one real difference
 * between the two functions, everything else mirrors it exactly): write
 * to a temp ".c" file, compile with a real "squash -c" subprocess (same
 * reasoning as the C# path for why this stays a subprocess even though
 * loading/running is in-process -- recompiling THIS process's own
 * CodeGen/Assembler global state mid-render for a wholly different
 * translation unit would be a real, avoidable risk), then load and run
 * the result in-process via sqo_loader_load(). Given the same host
 * symbol table as a C# script (csharp_rt + Vulkan) for parity -- a raw C
 * script is free to call csr_* directly (e.g. csr_console_write_line())
 * or a real Vulkan function exactly like a C#
 * [DllImport]-declared one, without needing its own separate native-
 * interop story. Same real, working DOM-event-dispatch gap as the C#
 * path -- runs its "main" once at page load, not wired to click/input/
 * etc. handlers yet (see sqw_run_csharp_script()'s own comment). */
static void sqw_run_c_script(const char *c_text, const char *id) {
    char src_path[256];
    char sqo_path[256];
    char cmd[1024];
    FILE *f;
    static int g_c_run_counter = 0;
    SqoLoaded loaded;
    SqoHostSymbol host_syms[SQO_HOST_SYMS_COUNT + SQO_HOST_SYMS_VULKAN_COUNT];
    int n_host_syms, n_rt_syms;
    int (*main_fn)(void);

    snprintf(src_path, sizeof src_path, "/tmp/sqw_c_%d_%d.c", (int)getpid(), g_c_run_counter);
    snprintf(sqo_path, sizeof sqo_path, "/tmp/sqw_c_%d_%d.sqo", (int)getpid(), g_c_run_counter);
    g_c_run_counter++;

    f = fopen(src_path, "w");
    if (!f) { fprintf(stderr, "SQW: C script '%s': could not create temp file %s\n", id, src_path); fflush(stderr); return; }
    fputs(c_text, f);
    fclose(f);

    snprintf(cmd, sizeof cmd, "./squash -c -linux -64 -ICSR %s -o %s >/tmp/sqw_c_compile.log 2>&1", src_path, sqo_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "SQW: C script '%s' failed to compile -- see /tmp/sqw_c_compile.log\n", id);
        fflush(stderr);
        remove(src_path);
        return;
    }

    n_rt_syms = sqo_host_syms_csharp_rt(host_syms);
    n_host_syms = n_rt_syms + sqo_host_syms_vulkan(host_syms + n_rt_syms);
    if (!sqo_loader_load(sqo_path, host_syms, n_host_syms, &loaded)) {
        fprintf(stderr, "SQW: C script '%s' compiled but failed to load in-process\n", id);
        fflush(stderr);
        remove(src_path); remove(sqo_path);
        return;
    }

    main_fn = (int (*)(void))sqo_loader_get_symbol(&loaded, "main");
    if (!main_fn) {
        fprintf(stderr, "SQW: C script '%s': loaded but has no 'main' function\n", id);
        fflush(stderr);
    } else {
        main_fn();
    }

    sqo_loader_free(&loaded);
    remove(src_path);
    remove(sqo_path);
}

/* Blocking network fetch for one <script src="..."> URL -- see
   sqw_apply_css()'s own comment on why a plain (non-async/non-defer)
   <script src> blocking the rest of page load until it arrives is
   actually REAL HTML5 default behavior, not a shortcut real browsers
   also parse-block on an ordinary <script src>. Reuses the existing
   ASYNC fetch primitive (net_client.h, a real background pthread) polled
   in a bounded loop here rather than adding a genuinely separate
   synchronous code path to net_client.c itself -- net_client.c's own
   worker thread doesn't know or care that its caller happens to be
   waiting synchronously this time. Returns a malloc'd, NUL-terminated
   body on success (caller frees), or NULL on failure/timeout --
   SQW_SCRIPT_FETCH_TIMEOUT_MS caps how long a single unreachable/slow
   external script can hold up the whole page load. */
#define SQW_SCRIPT_FETCH_TIMEOUT_MS 8000
static char *sqw_fetch_script_blocking(const char *url) {
    SqwNetResult *r = sqw_net_fetch_async(url);
    double start = (double)SDL_GetTicks();
    for (;;) {
        pthread_mutex_lock(&r->mutex);
        int ready = r->ready;
        pthread_mutex_unlock(&r->mutex);
        if (ready) break;
        if ((double)SDL_GetTicks() - start > SQW_SCRIPT_FETCH_TIMEOUT_MS) {
            fprintf(stderr, "SQW: script src fetch timed out: %s\n", url); fflush(stderr);
            sqw_net_result_abandon(r);
            return 0;
        }
        usleep(2000);
    }
    char *out = 0;
    pthread_mutex_lock(&r->mutex);
    if (r->success && r->body) {
        out = (char *)malloc((size_t)r->body_len + 1);
        memcpy(out, r->body, (size_t)r->body_len);
        out[r->body_len] = 0;
    }
    pthread_mutex_unlock(&r->mutex);
    if (!out) { fprintf(stderr, "SQW: script src fetch failed: %s\n", url); fflush(stderr); }
    sqw_net_result_free(r);
    return out;
}

/* Resolves a <script src="..."> attribute value to real script TEXT --
   absolute http(s):// is fetched over the network as-is; a plain
   relative src on a page itself reached over the network is fetched
   against base_url (the exact same rule sqw_resolve_image_urls() already
   uses for <img src>); a plain relative src on a LOCALLY loaded page
   (dir set, base_url empty) is read as a local file relative to dir --
   unlike images (net_client.c has no local-file read path), scripts
   already have a ready-made local reader in sqw_read_file(), and a local
   test page referencing a local sibling script file (e.g.
   "libs/util.js") is a genuinely common, worth-supporting case. Returns
   a malloc'd, NUL-terminated buffer (caller frees) or NULL if the src
   couldn't be resolved/loaded at all (a broken script src should never
   crash or blank the rest of the page -- same "log and keep going"
   convention as sqw_navigate_to()'s own file-not-found case). */
static char *sqw_load_script_src(const char *src, const char *dir, const char *base_url) {
    if (!src || !src[0]) return 0;
    if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
        return sqw_fetch_script_blocking(src);
    }
    if (base_url && base_url[0]) {
        char full_url[SQW_NET_URL_MAX];
        snprintf(full_url, sizeof full_url, "%s%s", base_url, src);
        return sqw_fetch_script_blocking(full_url);
    }
    if (dir) {
        char full_path[SQW_PATH_MAX];
        snprintf(full_path, sizeof full_path, "%s%s", dir, src);
        char *content = sqw_read_file(full_path);
        if (!content) { fprintf(stderr, "SQW: script src not found: %s\n", full_path); fflush(stderr); }
        return content;
    }
    return 0;
}

static void sqw_apply_css(DomNode *root, const char *dir, const char *base_url, float viewport_w) {
    if (g_current_css_sheet_valid) css_stylesheet_free(&g_current_css_sheet);
    css_stylesheet_init(&g_current_css_sheet);
    g_current_css_sheet_valid = 1;

    /* Heap, not a stack local -- 64KB is well past this project's own
     * "avoid large stack-resident locals" threshold (see dom_parse()'s
     * own comment on the identical rationale for its HtmlToken). Collects
     * every <script> tag's own text content, in document order,
     * concatenated with a blank-line separator -- real HTML5 executes
     * each <script> block as its own top-level program sharing one global
     * scope, which concatenation-then-one-parse reproduces correctly for
     * everything this engine's own scope covers (a syntax error confined
     * to one block, in real HTML5, only aborts THAT block, not later
     * ones -- concatenation instead aborts the whole page's script if
     * ANY block fails to parse; a real, accepted simplification, not
     * silent -- js_run_script() itself already logs a diagnostic either
     * way). */
    char *script_buf = (char *)malloc(SQW_SCRIPT_BUF_CAP);
    int script_len = 0;
    script_buf[0] = 0;

    /* <script type="module" ...> -- collected SEPARATELY from ordinary
       script_buf text (never concatenated into it): each one gets its
       own isolated top-level scope via js_run_module() (see that
       function's own comment for the full ES-module design), run once
       g_current_js_interp actually exists, right after the ordinary
       script_buf finishes below. A fixed-size table, same "generous but
       bounded" convention as everywhere else in this project -- a page
       using more than SQW_MAX_MODULE_SCRIPTS <script type="module">
       tags silently only runs the first that many. */
    SqwModuleScript modules[SQW_MAX_MODULE_SCRIPTS];
    int module_count = 0;

    /* <script type="text/csharp"> -- see SqwCsharpScript's own comment
       above for why these are collected separately, same reasoning as
       the `modules` array right above. */
    SqwCsharpScript cs_scripts[SQW_MAX_CSHARP_SCRIPTS];
    int cs_script_count = 0;

    /* <script type="text/c"> -- see SqwCScript's own comment above. */
    SqwCScript c_scripts[SQW_MAX_C_SCRIPTS];
    int c_script_count = 0;

    int cap = 64, top = 0;
    DomNode **stack = (DomNode **)malloc((size_t)cap * sizeof(DomNode *));
    int *next_child = (int *)malloc((size_t)cap * sizeof(int));
    stack[top] = root; next_child[top] = 0; top++;
    while (top > 0) {
        DomNode *n = stack[top - 1];
        if (next_child[top - 1] >= n->child_count) { top--; continue; }
        DomNode *child = n->children[next_child[top - 1]];
        next_child[top - 1]++;
        if (!dom_is_text(child)) {
            if (strcmp(child->tag, "style") == 0) {
                int i;
                for (i = 0; i < child->child_count; i++) {
                    DomNode *tc = child->children[i];
                    if (dom_is_text(tc)) css_parse_into(&g_current_css_sheet, tc->text);
                }
            } else if (strcmp(child->tag, "script") == 0) {
                /* "src=" -- a real external script, fetched (network) or
                   read (local file) via sqw_load_script_src() and
                   appended into the SAME concatenated script_buf an
                   inline block's own text would go into -- real HTML5
                   executes every <script> in one shared global scope
                   regardless of inline vs external, which concatenation-
                   then-one-parse already reproduces for inline blocks
                   (see this function's own top comment); an external
                   script with a "src" attribute has its own inline text
                   content (if any) IGNORED, matching real HTML5's own
                   "src wins, inline body is dead code" rule for that
                   case. */
                const char *src = dom_get_attr(child, "src");
                const char *type = dom_get_attr(child, "type");
                int is_module = type && !strcmp(type, "module");
                int is_csharp = type && !strcmp(type, "text/csharp");
                int is_c = type && !strcmp(type, "text/c");
                char *text = 0;
                if (src && src[0]) {
                    text = sqw_load_script_src(src, dir, base_url);
                } else {
                    /* Inline text -- collected into one buffer first
                       (module or not) since an inline <script> can have
                       several text-node children in principle. */
                    char inline_buf[SQW_SCRIPT_BUF_CAP];
                    int inline_len = 0;
                    inline_buf[0] = 0;
                    int i;
                    for (i = 0; i < child->child_count; i++) {
                        DomNode *tc = child->children[i];
                        if (dom_is_text(tc) && inline_len < SQW_SCRIPT_BUF_CAP - 2) {
                            int n = snprintf(inline_buf + inline_len, (size_t)(SQW_SCRIPT_BUF_CAP - inline_len), "%s\n", tc->text);
                            if (n > 0) inline_len += n;
                            if (inline_len > SQW_SCRIPT_BUF_CAP - 2) inline_len = SQW_SCRIPT_BUF_CAP - 2;
                        }
                    }
                    if (inline_len > 0) text = strdup(inline_buf);
                }
                if (is_csharp) {
                    if (text && cs_script_count < SQW_MAX_CSHARP_SCRIPTS) {
                        SqwCsharpScript *m = &cs_scripts[cs_script_count];
                        if (src && src[0]) { strncpy(m->id, src, sizeof m->id - 1); m->id[sizeof m->id - 1] = 0; }
                        else snprintf(m->id, sizeof m->id, "inline-csharp#%d", cs_script_count);
                        m->text = text;
                        cs_script_count++;
                        text = 0; /* ownership moved into cs_scripts[] */
                    }
                    free(text);
                } else if (is_c) {
                    if (text && c_script_count < SQW_MAX_C_SCRIPTS) {
                        SqwCScript *m = &c_scripts[c_script_count];
                        if (src && src[0]) { strncpy(m->id, src, sizeof m->id - 1); m->id[sizeof m->id - 1] = 0; }
                        else snprintf(m->id, sizeof m->id, "inline-c#%d", c_script_count);
                        m->text = text;
                        c_script_count++;
                        text = 0; /* ownership moved into c_scripts[] */
                    }
                    free(text);
                } else if (is_module) {
                    if (text && module_count < SQW_MAX_MODULE_SCRIPTS) {
                        SqwModuleScript *m = &modules[module_count];
                        if (src && src[0]) { strncpy(m->id, src, sizeof m->id - 1); m->id[sizeof m->id - 1] = 0; }
                        else snprintf(m->id, sizeof m->id, "inline-module#%d", module_count);
                        m->text = text;
                        module_count++;
                        text = 0; /* ownership moved into modules[] */
                    }
                    free(text);
                } else if (text && script_len < SQW_SCRIPT_BUF_CAP - 2) {
                    int n = snprintf(script_buf + script_len, (size_t)(SQW_SCRIPT_BUF_CAP - script_len), "%s\n", text);
                    if (n > 0) script_len += n;
                    if (script_len > SQW_SCRIPT_BUF_CAP - 2) script_len = SQW_SCRIPT_BUF_CAP - 2;
                    free(text);
                } else {
                    free(text);
                }
            } else if (strcmp(child->tag, "textarea") == 0) {
                /* <textarea>'s initial value is its own raw-text content
                 * (a real HTML5 rule -- see html_lexer.c's own RAW_TEXT_TAGS
                 * comment) -- <input>'s equivalent ("value" attribute) is
                 * already seeded at parse time (dom.c), this is the one
                 * remaining place a form control's initial value comes
                 * from, done here since it's the first point after
                 * dom_parse() where a tree walk like this one already
                 * exists to piggyback on. */
                int i;
                for (i = 0; i < child->child_count; i++) {
                    DomNode *tc = child->children[i];
                    if (dom_is_text(tc)) {
                        strncpy(child->form_value, tc->text, sizeof child->form_value - 1);
                        child->form_value[sizeof child->form_value - 1] = 0;
                        break;
                    }
                }
            }
            if (top >= cap) {
                cap *= 2;
                stack = (DomNode **)realloc(stack, (size_t)cap * sizeof(DomNode *));
                next_child = (int *)realloc(next_child, (size_t)cap * sizeof(int));
            }
            stack[top] = child; next_child[top] = 0; top++;
        }
    }
    free(stack); free(next_child);

    /* Run collected <script> text (plus wire up any "onclick" HTML
     * attribute, even on a page with no <script> tag at all -- see
     * js_wire_onclick_attrs()'s own comment) BEFORE css_apply(), not
     * after: a script can mutate the DOM (innerHTML, etc) at this point,
     * and css_apply() below needs to run AFTER that so every element --
     * including any a script just inserted -- gets a real computed style
     * (font_size/display/color/...), not the all-zero garbage
     * dom_node_new()'s plain calloc leaves a brand-new node with. Running
     * scripts any earlier (before <style>/<script> text is even fully
     * collected) or any later (after css_apply(), leaving inserted nodes
     * unstyled until some LATER event happens to trigger a restyle) would
     * both be wrong; this is the one correct ordering. */
    if (g_current_js_interp) js_interp_free(g_current_js_interp);
    g_current_js_interp = js_run_script(script_buf, root, 0);
    free(script_buf);

    /* Every <script type="module"> runs AFTER the page's ordinary script,
       in document order -- see js_run_module()'s own comment on why
       document order (not real dependency-graph resolution) is this
       engine's own honest scope limit for "import ... from" resolution. */
    {
        int i;
        for (i = 0; i < module_count; i++) {
            js_run_module(g_current_js_interp, modules[i].text, modules[i].id);
            free(modules[i].text);
        }
    }

    /* Every <script type="text/csharp"> also runs once, after JS --
       see sqw_run_csharp_script()'s own comment for exactly what "runs"
       means in this first integration (a real squash-compiled subprocess,
       output surfaced like console.log; no in-process DOM-event dispatch
       yet). */
    {
        int i;
        for (i = 0; i < cs_script_count; i++) {
            sqw_run_csharp_script(cs_scripts[i].text, cs_scripts[i].id);
            free(cs_scripts[i].text);
        }
    }

    /* Phase 6f: <script type="text/c"> -- see sqw_run_c_script()'s own
       comment. Runs after the C# scripts, same "runs once at page load"
       model as every other script type integrated so far. */
    {
        int i;
        for (i = 0; i < c_script_count; i++) {
            sqw_run_c_script(c_scripts[i].text, c_scripts[i].id);
            free(c_scripts[i].text);
        }
    }

    css_apply(root, &g_current_css_sheet, viewport_w);
}

/* Walks the whole tree once (same "one pass right after dom_parse()/
 * sqw_apply_css()" timing as css_apply() itself) resolving every <img
 * src="..."> to an absolute URL, cached directly on the node (DomNode::
 * img_url, see dom.h's own comment) and kicked off as an async fetch via
 * sqw_image_cache_request() (image_cache.h) -- layout.c and the draw pass
 * then only ever need node->img_url, never current_dir/current_base_url
 * themselves. Only absolute http(s):// src (as-is) or a plain relative src
 * on a page itself reached over the network (base_url-prefixed, the exact
 * same rule the anchor-click handler above uses for plain relative hrefs)
 * ever gets fetched -- a relative src on a LOCALLY loaded page (dir set,
 * base_url empty) is left unresolved (node->img_url stays empty, draws the
 * placeholder box forever), since this project's image pipeline only ever
 * fetches over HTTP(S) (net_client.c has no local-file read path), matching
 * the scope of the feature as requested. */
static void sqw_resolve_image_urls(DomNode *node, const char *dir, const char *base_url) {
    (void)dir;
    if (!dom_is_text(node) && strcmp(node->tag, "img") == 0) {
        const char *src = dom_get_attr(node, "src");
        if (src && src[0]) {
            char resolved[SQW_IMG_URL_MAX];
            resolved[0] = 0;
            if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
                strncpy(resolved, src, sizeof resolved - 1);
                resolved[sizeof resolved - 1] = 0;
            } else if (base_url && base_url[0]) {
                snprintf(resolved, sizeof resolved, "%s%s", base_url, src);
            }
            strncpy(node->img_url, resolved, sizeof node->img_url - 1);
            node->img_url[sizeof node->img_url - 1] = 0;
            if (node->img_url[0]) sqw_image_cache_request(node->img_url);
        }
    }
    int i;
    for (i = 0; i < node->child_count; i++) sqw_resolve_image_urls(node->children[i], dir, base_url);
}

/* Loads and parses `path` as the new current page, replacing *root_ptr
 * and recomputing layout in place. On failure (file not found/unreadable)
 * leaves the current page entirely untouched and just logs a warning --
 * a broken local link should never crash or blank the browser.
 * current_base_url is cleared: this page was reached via a local file
 * path, so it has no network origin for the click handler's href
 * resolution to fall back to (see that logic's own comment) -- without
 * this, navigating LOCAL -> NETWORK -> back to a LOCAL page over a plain
 * relative href would incorrectly keep treating further relative hrefs on
 * this local page as network-relative. */
static void sqw_navigate_to(const char *path, DomNode **root_ptr, LayoutList *boxes_ptr,
                             char *current_dir, char *current_base_url, float viewport_w, float viewport_h) {
    char *html = sqw_read_file(path);
    if (!html) {
        fprintf(stderr, "SQW: navigate: cannot open %s\n", path); fflush(stdout);
        return;
    }
    DomNode *new_root = dom_parse(html);
    free(html);
    dom_free(*root_ptr);
    *root_ptr = new_root;
    /* dir/base_url must be computed BEFORE sqw_apply_css() now (it needs
       them to resolve any <script src="...">), not after as this
       function's own code used to do when only image src resolution
       needed them (that still happens afterward, unchanged). */
    sqw_dirname(path, current_dir);
    current_base_url[0] = '\0';
    sqw_apply_css(*root_ptr, current_dir, current_base_url, viewport_w);
    /* Local-file page: <img src> only resolves (and only gets fetched) if
     * it's already an absolute http(s):// URL -- see
     * sqw_resolve_image_urls()'s own comment on why a bare local-relative
     * src is left unfetched. */
    sqw_image_cache_reset();
    sqw_resolve_image_urls(*root_ptr, current_dir, current_base_url);
    layout_list_free(boxes_ptr);
    layout_compute(*root_ptr, viewport_w, viewport_h, boxes_ptr);
    fprintf(stderr, "SQW: navigated to %s (%d boxes)\n", path, boxes_ptr->count); fflush(stdout);
}

/* Same document-swap as sqw_navigate_to(), but from an in-memory HTML
 * buffer (a fetched network response body) instead of a local file --
 * used by the SQW_TEST_CLICK_X/Y-independent real http(s):// anchor path.
 * current_dir is cleared (no local directory applies to a fetched page),
 * and current_base_url is set to `url`'s own directory (e.g.
 * "http://127.0.0.1:8080/") via the same sqw_dirname() logic local paths
 * already use -- it's equally valid on a '/'-delimited URL. Real browsers
 * resolve a plain relative href on a page fetched over the network against
 * THAT page's own URL, fetching the result over the network too, not as a
 * local file -- see the click handler's own href-resolution comment for
 * why this matters (this is the actual fix for "page 3, reached via the
 * network anchor from page 1, links back to page 1 by a plain
 * href=\"index.html\" -- clicking it silently failed to open a local file
 * literally named \"index.html\" instead of re-fetching
 * http://127.0.0.1:8080/index.html", confirmed as the real repro). */
static void sqw_navigate_to_html(const char *html, const char *url, DomNode **root_ptr, LayoutList *boxes_ptr,
                                  char *current_dir, char *current_base_url, float viewport_w, float viewport_h) {
    DomNode *new_root = dom_parse(html);
    dom_free(*root_ptr);
    *root_ptr = new_root;
    /* Same reordering as sqw_navigate_to()'s own identical comment. */
    current_dir[0] = '\0';
    sqw_dirname(url, current_base_url);
    sqw_apply_css(*root_ptr, current_dir, current_base_url, viewport_w);
    sqw_image_cache_reset();
    sqw_resolve_image_urls(*root_ptr, current_dir, current_base_url);
    layout_list_free(boxes_ptr);
    layout_compute(*root_ptr, viewport_w, viewport_h, boxes_ptr);
    fprintf(stderr, "SQW: navigated to fetched page %s (%d boxes)\n", url, boxes_ptr->count); fflush(stdout);
}

/* Shared by the Go button, pressing Enter in the URL bar, AND the Back
 * button: navigates to `url_text` the same way an anchor click would --
 * http(s):// URLs go through the async background-thread fetch (see
 * net_client.h's own comment on the async model; current_url is updated
 * once that fetch actually completes, at this file's own fetch-poll site
 * in main()), anything else is treated as a local path relative to
 * wherever SQW itself was launched from (typing a full path, not a
 * relative one, is the address-bar convention here). Does nothing on an
 * empty url_text.
 *
 * `push_history` controls whether the page being LEFT (current_url, as
 * of the moment this call starts) gets pushed onto `hist` first: 1 for
 * every FORWARD navigation (Go/Enter, anchor clicks), 0 for the Back
 * button itself -- Back only ever pops, never pushes, or clicking Back
 * repeatedly would just bounce between two pages forever instead of
 * actually retreating through history. */
static void sqw_go_navigate(const char *url_text, int push_history, SqwHistoryStack *hist, char *current_url,
                             SqwNetResult **pending_fetch, char *pending_fetch_url,
                             DomNode **root_ptr, LayoutList *boxes_ptr, char *current_dir, char *current_base_url,
                             float viewport_w, float viewport_h, float *scroll_x, float *scroll_y,
                             DomNode **hover_node, DomNode **active_node) {
    if (!url_text[0]) return;
    if (push_history) sqw_history_push(hist, current_url);
    if (strncmp(url_text, "http://", 7) == 0 || strncmp(url_text, "https://", 8) == 0) {
        if (*pending_fetch) sqw_net_result_abandon(*pending_fetch);
        fprintf(stderr, "SQW: fetching %s ...\n", url_text); fflush(stdout);
        strncpy(pending_fetch_url, url_text, SQW_PATH_MAX - 1);
        pending_fetch_url[SQW_PATH_MAX - 1] = 0;
        *pending_fetch = sqw_net_fetch_async(url_text);
    } else {
        sqw_navigate_to(url_text, root_ptr, boxes_ptr, current_dir, current_base_url, viewport_w, viewport_h);
        strncpy(current_url, url_text, SQW_PATH_MAX - 1); current_url[SQW_PATH_MAX - 1] = 0;
        *scroll_x = 0.0f; *scroll_y = 0.0f;
        *hover_node = NULL; *active_node = NULL;
    }
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
/* Draws every SQW_BOX_IMG box whose node->img_url has reached UPLOADED in
 * the image cache as a real textured quad, ON TOP of the flat placeholder
 * rect sqw_renderer_draw() (renderer_vk.c) already drew for every
 * SQW_BOX_IMG this frame (PENDING/FAILED images fall through to keep
 * showing that placeholder -- a deliberate "loading/broken" indicator, not
 * a bug: renderer_vk.c was left untouched rather than teaching it about
 * the image cache, see image_cache.h's own top comment on why this project
 * refers to images purely by URL string). Must run AFTER sqw_renderer_draw()
 * in the same frame so the textured quad actually ends up on top. */
static void draw_layout_images(SqwImageRenderer *ir, SqwVkContext *vk, VkCommandBuffer cmd,
                                LayoutList *boxes, float viewport_w, float viewport_h,
                                float scroll_x, float scroll_y) {
    int i;
    for (i = 0; i < boxes->count; i++) {
        LayoutBox *b = &boxes->boxes[i];
        if (b->kind != SQW_BOX_IMG) continue;
        SqwImageHandle handle;
        if (!sqw_image_cache_get_handle(b->node->img_url, &handle)) continue;
        float bx = b->x - scroll_x;
        float by = b->y - scroll_y;
        DomNode *n = b->node;
        sqw_image_draw_quad(vk, ir, cmd, handle.descriptorSet, bx, by, b->w, b->h,
            1.0f, 1.0f, 1.0f, n->css_opacity, viewport_w, viewport_h);
    }
}

/* Draws each box's own border, per side (css_border_width[4], already
 * factored into the box's own size by layout.c -- see dom.h's own
 * comment on css_border_width -- so the rect below is drawn flush against
 * b->x/b->y/b->w/b->h, no separate offset math needed). This was the one
 * deliberately-deferred half of "support border" (dom.h's own comment on
 * css_border_width explains why sizing landed first) -- four flat-colored
 * strips via the same sqw_renderer_draw_rect() primitive the toolbar/
 * scrollbars/focus rings already use, not a real line-style (solid vs.
 * dashed/dotted/double/... all render identically, solid) -- border-style
 * only ever zeroes width for none/hidden (see css.c's own comment), never
 * changes how a nonzero-width border actually draws. Border color falls
 * back to the element's own text color (real CSS's "currentColor"
 * default for border-color), then black, when no explicit border-color
 * was set -- matches how a real browser resolves an unset border-color. */
static void draw_layout_borders(SqwVkContext *vk, SqwRenderer *renderer, VkCommandBuffer cmd,
                                 LayoutList *boxes, float viewport_w, float viewport_h,
                                 float scroll_x, float scroll_y) {
    int i;
    for (i = 0; i < boxes->count; i++) {
        LayoutBox *b = &boxes->boxes[i];
        DomNode *n = b->node;
        if (!n || (n->css_border_width[0] <= 0.0f && n->css_border_width[1] <= 0.0f &&
                   n->css_border_width[2] <= 0.0f && n->css_border_width[3] <= 0.0f)) continue;
        float r, g, bl;
        if (n->css_has_border_color) { r = n->css_border_color[0]; g = n->css_border_color[1]; bl = n->css_border_color[2]; }
        else if (n->css_has_color) { r = n->css_color[0]; g = n->css_color[1]; bl = n->css_color[2]; }
        else { r = 0.0f; g = 0.0f; bl = 0.0f; }
        float bx = b->x - scroll_x, by = b->y - scroll_y;
        float tw = n->css_border_width[0], rw = n->css_border_width[1];
        float bw = n->css_border_width[2], lw = n->css_border_width[3];
        if (tw > 0.0f) sqw_renderer_draw_rect(vk, renderer, cmd, bx, by, b->w, tw, r, g, bl, viewport_w, viewport_h);
        if (bw > 0.0f) sqw_renderer_draw_rect(vk, renderer, cmd, bx, by + b->h - bw, b->w, bw, r, g, bl, viewport_w, viewport_h);
        if (lw > 0.0f) sqw_renderer_draw_rect(vk, renderer, cmd, bx, by, lw, b->h, r, g, bl, viewport_w, viewport_h);
        if (rw > 0.0f) sqw_renderer_draw_rect(vk, renderer, cmd, bx + b->w - rw, by, rw, b->h, r, g, bl, viewport_w, viewport_h);
    }
}

/* font-weight:bold, real per-element (css_apply() already resolved
 * inheritance -- see dom.h's own comment), rendered as a cheap "faux
 * bold": the SAME string drawn twice, offset 1px right, rather than a
 * genuinely bolder glyph -- this project's font atlas (font_atlas.h)
 * bakes exactly one weight of DejaVu Sans, no bold variant exists to
 * sample instead. A real second draw call, not a shader trick, so it
 * works through the exact same sqw_text_draw_string() path (and its own
 * SQW_TEXT_MAX_GLYPHS cap) as everything else -- doubles the glyph count
 * for bold text only, negligible at this project's page scale. */
static void draw_text_maybe_bold(SqwTextRenderer *tr, float x, float y, const char *s, int len, float scale,
                                  float r, float g, float b, float a, int bold, float viewport_w, float viewport_h) {
    if (bold) sqw_text_draw_string(tr, x + 1.0f, y, s, len, scale, r, g, b, a, viewport_w, viewport_h);
    sqw_text_draw_string(tr, x, y, s, len, scale, r, g, b, a, viewport_w, viewport_h);
}

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
            /* A plain text run's own color comes from its PARENT element's
             * computed style (see css.h/css.c) -- text nodes themselves
             * never carry CSS (only real elements can be a selector's
             * target), so this is exactly how a real browser resolves
             * "color" for a text run too (the property real CSS calls
             * inherited: an ancestor's declared value applies to its
             * descendant text with no rule of its own). Falls back to
             * black, this project's original fixed color, when no
             * ancestor declared one. */
            float tr_, tg_, tb_ = 0.0f;
            tr_ = tg_ = 0.0f;
            /* "textnode" / "owner": local plain pointers, read through
             * for every field access below -- never "b->node->parent->X"
             * or "b->node->X" chained inline. Real, confirmed squash
             * codegen bug (see css_set_default_style()'s own comment,
             * css.c): chaining two "->" hops (or more) to reach a field
             * reads back garbage on a struct this size, even though the
             * exact same field read through an ordinary local pointer
             * variable works correctly. */
            DomNode *textnode = b->node;
            DomNode *owner = textnode->parent;
            DomNode *anc = owner;
            while (anc && !anc->css_has_color) anc = anc->parent;
            if (anc) { tr_ = anc->css_color[0]; tg_ = anc->css_color[1]; tb_ = anc->css_color[2]; }
            /* opacity is NOT inherited (see dom.h's own comment), so this
             * is the text's DIRECT owning element's own opacity, not
             * walked up like color above. bold likewise comes straight
             * off the owner (IS inherited, already resolved by
             * css_apply(), no walk needed here either). */
            float op = owner ? owner->css_opacity : 1.0f;
            int bold = owner && owner->css_font_weight_bold;
            draw_text_maybe_bold(tr, bx, by, textnode->text + b->text_start, b->text_len,
                b->text_scale, tr_, tg_, tb_, op, bold, viewport_w, viewport_h);
        } else if (b->kind == SQW_BOX_A) {
            DomNode *n = b->node;
            float r, g, bl;
            anchor_color(n, &r, &g, &bl);
            if (n->css_has_color) { r = n->css_color[0]; g = n->css_color[1]; bl = n->css_color[2]; }
            /* Subtle hover feedback: a translucent-looking lighter tint by
             * blending toward white, cheap and doesn't need real alpha
             * blending on the (opaque) box pipeline. */
            if (n->hover) { r = r + (1.0f - r) * 0.35f; g = g + (1.0f - g) * 0.35f; bl = bl + (1.0f - bl) * 0.35f; }
            sqw_renderer_draw_rect(vk, renderer, cmd, bx, by + b->h - 2.0f, b->w, 2.0f, r, g, bl, viewport_w, viewport_h);
            concat_direct_text(n, label, sizeof label);
            float op = n->css_opacity; int bold = n->css_font_weight_bold;
            draw_text_maybe_bold(tr, bx, by, label, (int)strlen(label),
                b->text_scale, r, g, bl, op, bold, viewport_w, viewport_h);
        } else if (b->kind == SQW_BOX_SPAN) {
            DomNode *n = b->node;
            float tr_ = 0.0f, tg_ = 0.0f, tb_ = 0.0f;
            if (n->css_has_color) { tr_ = n->css_color[0]; tg_ = n->css_color[1]; tb_ = n->css_color[2]; }
            concat_direct_text(n, label, sizeof label);
            float op = n->css_opacity; int bold = n->css_font_weight_bold;
            draw_text_maybe_bold(tr, bx, by, label, (int)strlen(label),
                b->text_scale, tr_, tg_, tb_, op, bold, viewport_w, viewport_h);
        } else if (b->kind == SQW_BOX_BUTTON) {
            /* Covers both a real <button> AND <input type="submit"/"button">
             * (see layout.c's own dispatch) -- the latter's label comes
             * from its "value" attribute (already mirrored into
             * form_value at parse time, see dom.c), not a text child, so
             * concat_direct_text (which only reads DIRECT text-node
             * children) would find nothing for it. */
            const char *btn_label; int btn_label_len;
            if (strcmp(b->node->tag, "input") == 0) {
                btn_label = b->node->form_value; btn_label_len = (int)strlen(b->node->form_value);
            } else {
                btn_label_len = concat_direct_text(b->node, label, sizeof label);
                btn_label = label;
            }
            float tr_col = b->node->active ? 0.9f : 0.1f;
            sqw_text_draw_string(tr, bx + 4.0f, by + 2.0f, btn_label, btn_label_len,
                SQW_TEXT_SCALE, tr_col, tr_col, tr_col, 1.0f, viewport_w, viewport_h);
        } else if (b->kind == SQW_BOX_INPUT_TEXT || b->kind == SQW_BOX_TEXTAREA) {
            /* Thin border so the white field face (see renderer_vk.c's
             * box_color()) reads as an editable widget against the page
             * background -- four 1px-thick strips instead of pulling in a
             * hollow-rect primitive that doesn't otherwise exist here. A
             * brighter blue border marks the one field currently focused,
             * matching the classic browser focus-ring convention. */
            float br = b->node->form_focused ? 0.20f : 0.55f;
            float bg2 = b->node->form_focused ? 0.45f : 0.55f;
            float bb = b->node->form_focused ? 0.85f : 0.55f;
            sqw_renderer_draw_rect(vk, renderer, cmd, bx, by, b->w, 1.0f, br, bg2, bb, viewport_w, viewport_h);
            sqw_renderer_draw_rect(vk, renderer, cmd, bx, by + b->h - 1.0f, b->w, 1.0f, br, bg2, bb, viewport_w, viewport_h);
            sqw_renderer_draw_rect(vk, renderer, cmd, bx, by, 1.0f, b->h, br, bg2, bb, viewport_w, viewport_h);
            sqw_renderer_draw_rect(vk, renderer, cmd, bx + b->w - 1.0f, by, 1.0f, b->h, br, bg2, bb, viewport_w, viewport_h);
            if (b->kind == SQW_BOX_TEXTAREA) {
                /* Real multi-line rendering: form_value's own embedded
                 * newlines (typed Enter presses, see the keyboard-input
                 * handling in sqw_handle_event()) split it into lines,
                 * each drawn on its own row -- no word-wrap (matches
                 * <pre>'s own non-wrapping convention, layout.c), a
                 * textarea only breaks where the user actually pressed
                 * Enter. */
                const char *s = b->node->form_value;
                float ty = by + 3.0f;
                int start = 0, i2 = 0, slen = (int)strlen(s);
                while (start <= slen) {
                    i2 = start;
                    while (i2 < slen && s[i2] != '\n') i2++;
                    sqw_text_draw_string(tr, bx + 3.0f, ty, s + start, i2 - start,
                        SQW_TEXT_SCALE, 0.0f, 0.0f, 0.0f, 1.0f, viewport_w, viewport_h);
                    ty = ty + SQW_LINE_H;
                    start = i2 + 1;
                    if (i2 >= slen) break;
                }
            } else {
                sqw_text_draw_string(tr, bx + 3.0f, by + 3.0f, b->node->form_value, (int)strlen(b->node->form_value),
                    SQW_TEXT_SCALE, 0.0f, 0.0f, 0.0f, 1.0f, viewport_w, viewport_h);
            }
        } else if (b->kind == SQW_BOX_INPUT_CHECK) {
            const char *type = dom_get_attr(b->node, "type");
            int is_radio = type && strcmp(type, "radio") == 0;
            float br = b->node->form_focused ? 0.20f : 0.55f;
            sqw_renderer_draw_rect(vk, renderer, cmd, bx, by, b->w, 1.0f, br, br, br, viewport_w, viewport_h);
            sqw_renderer_draw_rect(vk, renderer, cmd, bx, by + b->h - 1.0f, b->w, 1.0f, br, br, br, viewport_w, viewport_h);
            sqw_renderer_draw_rect(vk, renderer, cmd, bx, by, 1.0f, b->h, br, br, br, viewport_w, viewport_h);
            sqw_renderer_draw_rect(vk, renderer, cmd, bx + b->w - 1.0f, by, 1.0f, b->h, br, br, br, viewport_w, viewport_h);
            if (b->node->form_checked) {
                /* A checkbox's check mark and a radio's filled dot are
                 * both approximated the same simple way here: an inset
                 * solid rect. Genuinely different SVG-style mark shapes
                 * aren't worth a dedicated glyph/path for two states. */
                float inset = is_radio ? 4.0f : 3.0f;
                sqw_renderer_draw_rect(vk, renderer, cmd, bx + inset, by + inset,
                    b->w - inset * 2.0f, b->h - inset * 2.0f, 0.15f, 0.45f, 0.85f, viewport_w, viewport_h);
            }
        }
    }
}

#define SQW_SCROLLBAR_THICKNESS 12.0f
#define SQW_SCROLLBAR_MIN_THUMB 24.0f
/* URL/search bar strip pinned to the top of the real window -- every
 * content-area computation (layout, scrollbar geometry/hit-test, mouse
 * wheel/keyboard paging, hover/click hit-test) subtracts this from the
 * window's real height so page content never renders under or gets
 * covered by it; every content DRAW call adds it back as a Y offset so
 * content actually paints below it instead of underneath. See
 * sqw_handle_event()/sqw_draw_frame() for both halves of that split. */
#define SQW_TOOLBAR_H 48.0f /* +8px over the original 40 (4 for top margin, 4 more for bottom -- URL bar text was clipping vertically both ways) */
#define SQW_URLBAR_PAD 8.0f
#define SQW_URLBAR_GO_W 56.0f
#define SQW_BACK_BTN_W 40.0f
/* Extra top/bottom margin for the toolbar's own text/caret (see
 * draw_toolbar's own comment) -- the taller bar above made room, but the
 * text itself still needs to actually use it on BOTH edges, or it just
 * clips the same as before with blank space left over on one side
 * instead. Symmetric (both 4px) so the glyph line sits centered in the
 * bar rather than hugging one edge. */
#define SQW_TOOLBAR_TEXT_MARGIN_TOP 4.0f
#define SQW_TOOLBAR_TEXT_MARGIN_BOTTOM 4.0f

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
        if (strcmp(node->tag, "a") == 0 || strcmp(node->tag, "button") == 0 ||
            strcmp(node->tag, "input") == 0 || strcmp(node->tag, "textarea") == 0) return node;
        node = node->parent;
    }
    return NULL;
}

/* Walks up node's own parent chain to the nearest enclosing <form> -- used
 * both to resolve a submit click's target form and (per the user's own
 * explicit, non-standard request) an anchor click's target form, and to
 * find a checkbox/radio's own form for radio-group scoping. NULL if node
 * isn't inside a <form> at all (a bare <input> with no enclosing form is
 * valid HTML5 too -- it just has nothing to submit). */
static DomNode *find_enclosing_form(DomNode *node) {
    while (node) {
        if (strcmp(node->tag, "form") == 0) return node;
        node = node->parent;
    }
    return NULL;
}

/* Percent-encodes src (application/x-www-form-urlencoded, the standard
 * real HTML5 form encoding: space -> '+', unreserved chars pass through,
 * everything else -> "%XX") and appends it to dst, respecting dst's
 * capacity. */
static void url_encode_append(char *dst, int dstcap, int *dstlen, const char *src) {
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = 0; src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        int is_unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (is_unreserved) {
            if (*dstlen < dstcap - 1) dst[(*dstlen)++] = (char)c;
        } else if (c == ' ') {
            if (*dstlen < dstcap - 1) dst[(*dstlen)++] = '+';
        } else {
            if (*dstlen < dstcap - 3) {
                dst[(*dstlen)++] = '%';
                dst[(*dstlen)++] = hex[(c >> 4) & 0xF];
                dst[(*dstlen)++] = hex[c & 0xF];
            }
        }
    }
    dst[*dstlen] = 0;
}

typedef struct {
    const char *name;
    DomNode *skip;
} RadioGroupCtx;

/* dom_walk() visitor: unchecks every OTHER <input type="radio"> in the
 * form sharing `name` -- see the radio-click handler's own comment for
 * why (real HTML5 radio-group mutual exclusivity). */
static void radio_group_clear_visit(DomNode *node, int depth, void *ctx) {
    (void)depth;
    RadioGroupCtx *c = (RadioGroupCtx *)ctx;
    if (node == c->skip) return;
    if (strcmp(node->tag, "input") != 0) return;
    const char *type = dom_get_attr(node, "type");
    if (!type || strcmp(type, "radio") != 0) return;
    const char *name = dom_get_attr(node, "name");
    if (name && strcmp(name, c->name) == 0) node->form_checked = 0;
}

typedef struct {
    char *buf;
    int cap;
    int len;
} FormCollectCtx;

/* dom_walk() visitor: appends one "name=value&" pair (URL-encoded) per
 * successfully-submittable descendant of the <form> being walked --
 * real HTML5 submittable-element rules, simplified to this project's own
 * supported control set: <input> (any type except "submit"/"button"/
 * "reset"/"hidden"-is-still-submittable-just-not-user-editable, and a
 * checkbox/radio only when checked) and <textarea>, each skipped
 * entirely if it has no "name" attribute (an unnamed control contributes
 * nothing to a real form submission either). */
static void form_collect_visit(DomNode *node, int depth, void *ctx) {
    (void)depth;
    FormCollectCtx *c = (FormCollectCtx *)ctx;
    int is_input = strcmp(node->tag, "input") == 0;
    int is_textarea = strcmp(node->tag, "textarea") == 0;
    if (!is_input && !is_textarea) return;
    const char *name = dom_get_attr(node, "name");
    if (!name || !name[0]) return;
    if (is_input) {
        const char *type = dom_get_attr(node, "type");
        if (type && (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0 || strcmp(type, "reset") == 0)) return;
        if (type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0) && !node->form_checked) return;
    }
    if (c->len > 0 && c->len < c->cap - 1) c->buf[c->len++] = '&';
    url_encode_append(c->buf, c->cap, &c->len, name);
    if (c->len < c->cap - 1) c->buf[c->len++] = '=';
    url_encode_append(c->buf, c->cap, &c->len, node->form_value);
}

/* Submits `form`: collects its own descendant inputs/textareas
 * (form_collect_visit) into a "name=value&..." encoded body, resolves
 * its "action" attribute the SAME way an <a href> is resolved elsewhere
 * in this file (http(s):// absolute / relative-against-current_base_url
 * for a networked page / relative-against-current_dir for a local page /
 * empty meaning "the current page itself"), and dispatches by "method"
 * ("GET", the real HTML5 default, or "POST"; anything else falls back to
 * GET). GET appends the encoded pairs as the URL's own query string and
 * reuses sqw_go_navigate() unchanged (it already knows how to route a
 * scheme'd vs. local URL). POST against an http(s) action uses
 * sqw_net_fetch_async_ex() directly with the pairs as the request body,
 * plugged into the SAME pending_fetch polling sqw_check_pending_fetch()
 * already uses for anchor-clicked network fetches, which doesn't care
 * which call started the fetch. A local (non-http) action has no server
 * behind it to POST to in this project, so it's treated the same as GET
 * there (a documented, deliberate limitation, not a bug). */
static void submit_form(DomNode *form, SqwHistoryStack *hist, char *current_url,
                         SqwNetResult **pending_fetch, char *pending_fetch_url,
                         DomNode **root_ptr, LayoutList *boxes_ptr, char *current_dir, char *current_base_url,
                         float viewport_w, float viewport_h, float *scroll_x, float *scroll_y,
                         DomNode **hover_node, DomNode **active_node) {
    char pairs[2048];
    FormCollectCtx ctx; ctx.buf = pairs; ctx.cap = (int)sizeof pairs; ctx.len = 0;
    pairs[0] = 0;
    dom_walk(form, form_collect_visit, &ctx);

    const char *action = dom_get_attr(form, "action");
    const char *method_attr = dom_get_attr(form, "method");
    /* Manual case-insensitive compare, not strcasecmp -- this project's
     * squash header shims (include/string.h) don't declare it (no
     * strings.h at all here), and real HTML markup is seen with
     * method="POST", "post", or mixed case just as often. */
    int is_post = 0;
    if (method_attr && strlen(method_attr) == 4) {
        char m0 = (char)tolower((unsigned char)method_attr[0]);
        char m1 = (char)tolower((unsigned char)method_attr[1]);
        char m2 = (char)tolower((unsigned char)method_attr[2]);
        char m3 = (char)tolower((unsigned char)method_attr[3]);
        is_post = (m0=='p' && m1=='o' && m2=='s' && m3=='t');
    }

    char full_url[SQW_PATH_MAX];
    full_url[0] = 0;
    if (action && (strncmp(action, "http://", 7) == 0 || strncmp(action, "https://", 8) == 0)) {
        strncpy(full_url, action, sizeof full_url - 1); full_url[sizeof full_url - 1] = 0;
    } else if (action && action[0] && current_base_url[0]) {
        snprintf(full_url, sizeof full_url, "%s%s", current_base_url, action);
    } else if (action && action[0]) {
        snprintf(full_url, sizeof full_url, "%s%s", current_dir, action);
    } else {
        strncpy(full_url, current_url, sizeof full_url - 1); full_url[sizeof full_url - 1] = 0;
    }

    int is_network = strncmp(full_url, "http://", 7) == 0 || strncmp(full_url, "https://", 8) == 0;

    if (is_post && is_network) {
        if (*pending_fetch) sqw_net_result_abandon(*pending_fetch);
        sqw_history_push(hist, current_url);
        fprintf(stderr, "SQW: submitting form POST %s ...\n", full_url); fflush(stdout);
        strncpy(pending_fetch_url, full_url, SQW_PATH_MAX - 1); pending_fetch_url[SQW_PATH_MAX - 1] = 0;
        *pending_fetch = sqw_net_fetch_async_ex(full_url, "POST", pairs, (long)ctx.len);
    } else {
        /* GET (or a local action, which has nowhere to POST to): append
         * the pairs as a real "?name=value&..." query string. */
        char get_url[SQW_PATH_MAX];
        if (ctx.len > 0) snprintf(get_url, sizeof get_url, "%s?%s", full_url, pairs);
        else { strncpy(get_url, full_url, sizeof get_url - 1); get_url[sizeof get_url - 1] = 0; }
        sqw_go_navigate(get_url, 1, hist, current_url, pending_fetch, pending_fetch_url,
                         root_ptr, boxes_ptr, current_dir, current_base_url, viewport_w, viewport_h,
                         scroll_x, scroll_y, hover_node, active_node);
    }
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
    float content_view_h = viewport_h - SQW_TOOLBAR_H; /* visible content height, below the URL bar */
    if (content_h > content_view_h) {
        float track_x = viewport_w - SQW_SCROLLBAR_THICKNESS;
        sqw_renderer_draw_rect(vk, renderer, cmd, track_x, SQW_TOOLBAR_H, SQW_SCROLLBAR_THICKNESS, content_view_h,
            0.85f, 0.85f, 0.85f, viewport_w, viewport_h);
        float thumb_h = content_view_h * (content_view_h / content_h);
        if (thumb_h < SQW_SCROLLBAR_MIN_THUMB) thumb_h = SQW_SCROLLBAR_MIN_THUMB;
        float track_free = content_view_h - thumb_h;
        float max_scroll = content_h - content_view_h;
        float thumb_y = SQW_TOOLBAR_H + ((max_scroll > 0) ? (scroll_y / max_scroll) * track_free : 0.0f);
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

/* Geometry of the Back button, URL bar, and Go button, shared by
 * draw_toolbar() (so rendering matches) and sqw_handle_event()'s own
 * toolbar click-hit-testing (so a click lands exactly where it visually
 * looks like it should) -- computing this in two places from scratch
 * would eventually drift out of sync. Back sits leftmost (standard
 * browser layout), URL bar shrinks to make room for it. */
static void toolbar_geometry(float viewport_w, LayoutBox *out_back, LayoutBox *out_bar, LayoutBox *out_go) {
    float bar_h = SQW_TOOLBAR_H - 2.0f * SQW_URLBAR_PAD;
    out_back->x = SQW_URLBAR_PAD; out_back->y = SQW_URLBAR_PAD;
    out_back->w = SQW_BACK_BTN_W; out_back->h = bar_h;
    out_bar->x = out_back->x + out_back->w + SQW_URLBAR_PAD; out_bar->y = SQW_URLBAR_PAD;
    out_bar->w = viewport_w - 4.0f * SQW_URLBAR_PAD - SQW_BACK_BTN_W - SQW_URLBAR_GO_W;
    if (out_bar->w < 10.0f) out_bar->w = 10.0f;
    out_bar->h = bar_h;
    out_go->x = out_bar->x + out_bar->w + SQW_URLBAR_PAD; out_go->y = SQW_URLBAR_PAD;
    out_go->w = SQW_URLBAR_GO_W; out_go->h = bar_h;
}

/* Draws the toolbar strip: background, the green Back button (dimmed when
 * `can_go_back` is false -- i.e. the history stack is empty, nothing to
 * go back to), URL text box (border tints blue while focused, matching
 * real browsers' own focus-ring convention), the typed text with a simple
 * end-of-text caret when focused (editing is append/backspace-at-the-end
 * only -- see sqw_handle_event()'s SDL_EVENT_TEXT_INPUT/BACKSPACE handling
 * -- so the caret is always exactly at the text's own end, no separate
 * cursor-position tracking needed), and the Go button. */
static void draw_toolbar(SqwVkContext *vk, SqwRenderer *renderer, SqwTextRenderer *tr, VkCommandBuffer cmd,
                          const char *url_text, int url_focused, int can_go_back, float viewport_w, float viewport_h) {
    sqw_renderer_draw_rect(vk, renderer, cmd, 0.0f, 0.0f, viewport_w, SQW_TOOLBAR_H,
        0.90f, 0.90f, 0.90f, viewport_w, viewport_h);

    LayoutBox back, bar, go;
    toolbar_geometry(viewport_w, &back, &bar, &go);

    /* Green when there's history to go back to, a dulled/greyed green
     * when the stack is empty -- still visible, but reads as disabled. */
    float bkr = can_go_back ? 0.13f : 0.55f, bkg = can_go_back ? 0.55f : 0.60f, bkb = can_go_back ? 0.20f : 0.55f;
    sqw_renderer_draw_rect(vk, renderer, cmd, back.x, back.y, back.w, back.h, bkr, bkg, bkb, viewport_w, viewport_h);
    /* SQW_TOOLBAR_TEXT_MARGIN_TOP/BOTTOM: the taller (48px, was 40px)
     * toolbar gave the bar itself 8 extra px, but the text inside it
     * still needs to actually use that room on BOTH edges -- without
     * this the extra space just became unused margin on one side while
     * the glyphs kept clipping on the other, since text was drawn at a
     * fixed 3px offset from the bar's own top with no matching reserve
     * at the bottom. Applied to every piece of toolbar text/the caret so
     * they all stay vertically aligned with each other inside the bar. */
    float text_y = 3.0f + SQW_TOOLBAR_TEXT_MARGIN_TOP;
    float text_bottom_margin = 3.0f + SQW_TOOLBAR_TEXT_MARGIN_BOTTOM;

    sqw_text_draw_string(tr, back.x + 11.0f, back.y + text_y, "<", 1,
        SQW_TEXT_SCALE, 1.0f, 1.0f, 1.0f, 1.0f, viewport_w, viewport_h);

    float br = url_focused ? 0.20f : 0.65f, bg = url_focused ? 0.45f : 0.65f, bb = url_focused ? 0.85f : 0.65f;
    sqw_renderer_draw_rect(vk, renderer, cmd, bar.x - 1.5f, bar.y - 1.5f, bar.w + 3.0f, bar.h + 3.0f,
        br, bg, bb, viewport_w, viewport_h);
    sqw_renderer_draw_rect(vk, renderer, cmd, bar.x, bar.y, bar.w, bar.h, 1.0f, 1.0f, 1.0f, viewport_w, viewport_h);

    int url_len = (int)strlen(url_text);
    sqw_text_draw_string(tr, bar.x + 6.0f, bar.y + text_y, url_text, url_len,
        SQW_TEXT_SCALE, 0.05f, 0.05f, 0.05f, 1.0f, viewport_w, viewport_h);
    if (url_focused) {
        float caret_x = bar.x + 6.0f + sqw_text_measure(url_text, url_len, SQW_TEXT_SCALE);
        sqw_renderer_draw_rect(vk, renderer, cmd, caret_x, bar.y + text_y, 2.0f, bar.h - text_y - text_bottom_margin,
            0.1f, 0.1f, 0.1f, viewport_w, viewport_h);
    }

    sqw_renderer_draw_rect(vk, renderer, cmd, go.x, go.y, go.w, go.h, 0.20f, 0.45f, 0.85f, viewport_w, viewport_h);
    sqw_text_draw_string(tr, go.x + 16.0f, go.y + text_y, "Go", 2,
        SQW_TEXT_SCALE, 1.0f, 1.0f, 1.0f, 1.0f, viewport_w, viewport_h);
}

#define SQW_LOADBAR_H 6.0f

/* Thin progress strip fixed to the bottom edge of the render window while a
 * navigation is in flight -- the requested "loading progress" indicator.
 * Screen-space (drawn at a fixed viewport_h-relative Y, same as every other
 * toolbar/scrollbar draw in this file -- NEVER offset by scroll_x/scroll_y),
 * so it stays pinned to the bottom of the window regardless of where the
 * still-visible old page is scrolled to.
 * Deliberately covers only the two things that can actually be measured
 * ahead of time -- Vulkan/renderer bring-up (already fully complete by the
 * time ANY frame, including this bar, can be drawn at all -- see this
 * function's own weight comment below) and the network fetch itself (real
 * bytes-received/content-length, via SqwAppState's fetch_content_length/
 * fetch_bytes_received, refreshed once per frame by
 * sqw_check_pending_fetch()) -- NOT the render of the fetched page's own
 * content: this bar is drawn every frame the OLD page is still what's on
 * screen (sqw_draw_frame() calls this before pending_fetch has been
 * swapped in), and it disappears the instant sqw_check_pending_fetch()
 * swaps the new DOM in and starts laying it out/painting it, at which
 * point tracking is deliberately no longer this function's concern -- see
 * sqw_draw_frame()'s own call site.
 *
 * Progress is a weighted sum of two anticipated phases:
 *   - SQW_LOADBAR_VK_WEIGHT (25%): Vulkan/renderer readiness. There is no
 *     way to show *partial* credit for this on-screen (nothing can be
 *     presented until the swapchain/pipelines already exist -- see
 *     main()'s own startup-progress fprintf()s for the only place actual
 *     Vulkan-init sub-steps are visible, since that happens before any
 *     window content can be drawn at all), so by the time this function
 *     can run even once, that share is always already earned in full.
 *   - The remaining 75%: real network progress, `bytes_received /
 *     content_length` when the server sent a Content-Length header (the
 *     common case), scaled into that remaining share. When the length
 *     isn't known ahead of time (chunked/close-delimited responses -- see
 *     content_length's own -1 convention), falls back to a slow
 *     back-and-forth pulse driven by frame_count instead of a real
 *     fraction, same convention every real browser's indeterminate spinner
 *     uses for a response whose total size genuinely isn't knowable yet. */
#define SQW_LOADBAR_VK_WEIGHT 0.25f

static void sqw_draw_loading_bar(SqwVkContext *vk, SqwRenderer *renderer, VkCommandBuffer cmd,
                                  long bytes_received, long content_length, int frame_count,
                                  float viewport_w, float viewport_h) {
    float frac;
    if (content_length > 0) {
        frac = (float)bytes_received / (float)content_length;
        if (frac > 1.0f) frac = 1.0f;
        if (frac < 0.0f) frac = 0.0f;
        frac = SQW_LOADBAR_VK_WEIGHT + frac * (1.0f - SQW_LOADBAR_VK_WEIGHT);
    } else {
        /* Indeterminate: a ~120px band sweeping left-to-right-to-left
         * across the bar every ~90 frames, confined to the post-Vulkan
         * share of the bar (never dips back below SQW_LOADBAR_VK_WEIGHT)
         * so it still visibly communicates "Vulkan/renderer is ready,
         * fetch is in progress" even without a real byte count. */
        float t = (float)(frame_count % 180);
        float sweep = (t < 90.0f) ? (t / 90.0f) : (2.0f - t / 90.0f);
        frac = SQW_LOADBAR_VK_WEIGHT + sweep * (1.0f - SQW_LOADBAR_VK_WEIGHT);
    }

    float bar_y = viewport_h - SQW_LOADBAR_H;
    sqw_renderer_draw_rect(vk, renderer, cmd, 0.0f, bar_y, viewport_w, SQW_LOADBAR_H,
        0.80f, 0.80f, 0.80f, viewport_w, viewport_h);
    sqw_renderer_draw_rect(vk, renderer, cmd, 0.0f, bar_y, viewport_w * frac, SQW_LOADBAR_H,
        0.20f, 0.45f, 0.85f, viewport_w, viewport_h);
}

/* Bundles every piece of state that used to live as one of main()'s own
 * several-dozen local variables, spanning its entire ~700-line body, into
 * one heap-allocated struct instead (malloc'd once in main(), same
 * off-the-C-stack rationale as vk/renderer/text_renderer/hist below --
 * see their own original comments). This is the actual fix for SQW's
 * "empty window" bug: main() used to be a single huge function carrying
 * every one of these as a live local for its whole body (window/vk state,
 * scroll/drag/hover/focus tracking, URL-bar/history/fetch state, the
 * SQW_TEST_* synthetic-input knobs, ...) -- a real, confirmed squash
 * codegen bug where a function with too many simultaneously-live locals
 * silently miscompiles instead of erroring (the same documented bug class
 * layout.c's own PlaceSpec comment already worked around elsewhere in this
 * project). Concretely, this build's Vulkan draw calls all reported
 * VK_SUCCESS every frame but nothing ever actually reached the swapchain
 * image -- confirmed via a direct VRAM readback (vkCmdCopyImageToBuffer),
 * bypassing X11/the compositor entirely -- while a byte-identical
 * renderer/pipeline in a much smaller standalone program rendered
 * correctly. Splitting main()'s body into the several SqwAppState-taking
 * functions below (each with a far smaller, non-overlapping live-local set
 * of its own) fixes it. */
typedef struct {
    SDL_Window *window;
    SqwVkContext *vk;
    SqwRenderer *renderer;
    SqwTextRenderer *text_renderer;
    SqwImageRenderer *image_renderer;

    DomNode *root;
    LayoutList boxes;
    SqwHistoryStack *hist;

    char current_dir[SQW_PATH_MAX];
    char current_base_url[SQW_PATH_MAX];
    char current_url[SQW_PATH_MAX];
    char pending_fetch_url[SQW_PATH_MAX];
    SqwNetResult *pending_fetch; /* non-NULL while an http(s):// fetch is outstanding */
    /* Loading-progress bar state -- see sqw_draw_loading_bar()'s own
     * comment. Only meaningful while pending_fetch is non-NULL; snapshot
     * of pending_fetch's own progress fields, refreshed once per frame by
     * sqw_check_pending_fetch() (a single mutex-protected read, rather than
     * the draw call locking pending_fetch->mutex itself) so the toolbar
     * draw code never needs to touch net_client.h's threading at all. */
    long fetch_content_length; /* -1 = unknown (falls back to an indeterminate pulse) */
    long fetch_bytes_received;

    float viewport_w, viewport_h;
    float scroll_x, scroll_y;

    DomNode *hover_node;
    DomNode *active_node;
    DomNode *focused_input; /* mirrors DomNode::form_focused, see its own comment */

    int dragging_v, dragging_h;
    float drag_anchor_mouse, drag_anchor_scroll;
    float mouse_x, mouse_y;

    char url_bar_text[SQW_PATH_MAX];
    int url_bar_focused;

    int running;
    int frame_count;

    /* SQW_TEST_* env-var-driven synthetic input -- see
     * sqw_push_test_events()'s own comment. */
    int test_click_x, test_click_y;
    int test_click2_x, test_click2_y;
    int test_click3_x, test_click3_y;
    SDL_Scancode test_key;
    const char *test_type_text;
    /* SQW_TEST_SCREENSHOT_FRAME/SQW_TEST_SCREENSHOT_PATH: dumps swapchain
     * image N straight from VRAM to a PPM file (sqw_debug_screenshot())
     * once frame_count reaches this value -- -1 (the default) means never.
     * See sqw_debug_screenshot()'s own comment for why this exists. */
    int test_screenshot_frame;
    char test_screenshot_path[SQW_PATH_MAX];
} SqwAppState;

/* Test-only synthetic input hook (SQW_TEST_CLICK_X/Y and friends): pushes
 * real SDL events through SDL_PushEvent() -- not a shortcut that bypasses
 * the event loop, the exact same SDL_PollEvent() path a real XTest-injected
 * or physical click takes -- at fixed frames so local automated testing
 * doesn't depend on XTest actually reaching this window (confirmed
 * unreliable under this environment's Xwayland setup:
 * XTestFakeMotionEvent/ButtonEvent calls succeeded but SQW never received a
 * single resulting SDL_EVENT_MOUSE_MOTION). Off by default; only active
 * with the relevant env var(s) set. */
static void sqw_push_test_events(SqwAppState *st) {
    if (st->test_click_x >= 0 && st->frame_count == 30) {
        SDL_Event mv; memset(&mv, 0, sizeof mv);
        mv.type = SDL_EVENT_MOUSE_MOTION;
        mv.motion.x = (float)st->test_click_x; mv.motion.y = (float)st->test_click_y;
        SDL_PushEvent(&mv);
    }
    if (st->test_click_x >= 0 && st->frame_count == 60) {
        SDL_Event bd; memset(&bd, 0, sizeof bd);
        bd.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        bd.button.button = 1; bd.button.down = 1;
        bd.button.x = (float)st->test_click_x; bd.button.y = (float)st->test_click_y;
        SDL_PushEvent(&bd);
        SDL_Event bu; memset(&bu, 0, sizeof bu);
        bu.type = SDL_EVENT_MOUSE_BUTTON_UP;
        bu.button.button = 1; bu.button.down = 0;
        bu.button.x = (float)st->test_click_x; bu.button.y = (float)st->test_click_y;
        SDL_PushEvent(&bu);
    }
    /* SQW_TEST_TYPE_TEXT: clicks the URL bar to focus it (frame 10), then
     * pushes one real SDL_EVENT_TEXT_INPUT carrying the whole string (frame
     * 20) -- exercises the exact same append path a sequence of real
     * per-character events would (sqw_handle_event()'s own handler just
     * appends whatever ev->text.text contains, whether that's one
     * character or many), without needing a separate synthetic event per
     * character. */
    if (st->test_type_text && st->frame_count == 10) {
        SDL_Event mv; memset(&mv, 0, sizeof mv);
        mv.type = SDL_EVENT_MOUSE_MOTION;
        mv.motion.x = 100.0f; mv.motion.y = 20.0f;
        SDL_PushEvent(&mv);
        SDL_Event bd; memset(&bd, 0, sizeof bd);
        bd.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        bd.button.button = 1; bd.button.down = 1;
        bd.button.x = 100.0f; bd.button.y = 20.0f;
        SDL_PushEvent(&bd);
        SDL_Event bu; memset(&bu, 0, sizeof bu);
        bu.type = SDL_EVENT_MOUSE_BUTTON_UP;
        bu.button.button = 1; bu.button.down = 0;
        bu.button.x = 100.0f; bu.button.y = 20.0f;
        SDL_PushEvent(&bu);
    }
    if (st->test_type_text && st->frame_count == 20) {
        SDL_Event ti; memset(&ti, 0, sizeof ti);
        ti.type = SDL_EVENT_TEXT_INPUT;
        ti.text.text = st->test_type_text;
        SDL_PushEvent(&ti);
    }
    /* SQW_TEST_CLICK2_X/Y (frame 300, well after the first click's own
     * network fetch at frame 60 has had time to complete): a SECOND
     * synthetic click, for testing multi-step navigation (e.g. "click a
     * network anchor, then click a relative link on the page it fetched")
     * that a single test click can't exercise. Same real-event-loop
     * rationale as SQW_TEST_CLICK_X/Y above. */
    if (st->test_click2_x >= 0 && st->frame_count == 270) {
        SDL_Event mv; memset(&mv, 0, sizeof mv);
        mv.type = SDL_EVENT_MOUSE_MOTION;
        mv.motion.x = (float)st->test_click2_x; mv.motion.y = (float)st->test_click2_y;
        SDL_PushEvent(&mv);
    }
    if (st->test_click2_x >= 0 && st->frame_count == 290) {
        SDL_Event bd; memset(&bd, 0, sizeof bd);
        bd.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        bd.button.button = 1; bd.button.down = 1;
        bd.button.x = (float)st->test_click2_x; bd.button.y = (float)st->test_click2_y;
        SDL_PushEvent(&bd);
        SDL_Event bu; memset(&bu, 0, sizeof bu);
        bu.type = SDL_EVENT_MOUSE_BUTTON_UP;
        bu.button.button = 1; bu.button.down = 0;
        bu.button.x = (float)st->test_click2_x; bu.button.y = (float)st->test_click2_y;
        SDL_PushEvent(&bu);
    }
    /* SQW_TEST_CLICK3_X/Y (frame 500, well after CLICK2's own frame 290 --
     * plenty of margin for that navigation, local or network, to have
     * settled): a THIRD synthetic click, added specifically to exercise
     * the Back button end-to-end in headless/automated runs (click
     * somewhere that navigates, click again, then click Back and confirm
     * it actually lands back on the first page). Same real-event-loop
     * rationale as SQW_TEST_CLICK_X/Y above. */
    if (st->test_click3_x >= 0 && st->frame_count == 480) {
        SDL_Event mv; memset(&mv, 0, sizeof mv);
        mv.type = SDL_EVENT_MOUSE_MOTION;
        mv.motion.x = (float)st->test_click3_x; mv.motion.y = (float)st->test_click3_y;
        SDL_PushEvent(&mv);
    }
    if (st->test_click3_x >= 0 && st->frame_count == 500) {
        SDL_Event bd; memset(&bd, 0, sizeof bd);
        bd.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        bd.button.button = 1; bd.button.down = 1;
        bd.button.x = (float)st->test_click3_x; bd.button.y = (float)st->test_click3_y;
        SDL_PushEvent(&bd);
        SDL_Event bu; memset(&bu, 0, sizeof bu);
        bu.type = SDL_EVENT_MOUSE_BUTTON_UP;
        bu.button.button = 1; bu.button.down = 0;
        bu.button.x = (float)st->test_click3_x; bu.button.y = (float)st->test_click3_y;
        SDL_PushEvent(&bu);
    }
    if (st->test_key != SDL_SCANCODE_UNKNOWN && st->frame_count == 30) {
        SDL_Event kd; memset(&kd, 0, sizeof kd);
        kd.type = SDL_EVENT_KEY_DOWN;
        kd.key.scancode = st->test_key; kd.key.down = true;
        SDL_PushEvent(&kd);
    }
}

/* Handles exactly one already-polled SDL event -- the entire body of what
 * used to be main()'s own "while (SDL_PollEvent(&ev))" loop, unchanged
 * logic-for-logic, just addressing every piece of persistent state through
 * `st` instead of a same-named local. See SqwAppState's own comment for
 * why this split exists. */
/* Maps the small fixed set of scancodes this project actually handles
 * (see sqw_handle_event()'s own SDL_EVENT_KEY_DOWN branches) to the
 * matching real KeyboardEvent.key string, for js_dispatch_keydown()'s
 * benefit -- not a full keymap (this project has no general character-key
 * scancode table, only real text via SDL_EVENT_TEXT_INPUT), so any other
 * scancode maps to "" rather than guessing. */
static const char *sqw_scancode_key_name(SDL_Scancode sc) {
    if (sc == SDL_SCANCODE_BACKSPACE) return "Backspace";
    if (sc == SDL_SCANCODE_RETURN) return "Enter";
    if (sc == SDL_SCANCODE_ESCAPE) return "Escape";
    if (sc == SDL_SCANCODE_PAGEDOWN) return "PageDown";
    if (sc == SDL_SCANCODE_PAGEUP) return "PageUp";
    if (sc == SDL_SCANCODE_HOME) return "Home";
    if (sc == SDL_SCANCODE_END) return "End";
    if (sc == SDL_SCANCODE_DOWN) return "ArrowDown";
    if (sc == SDL_SCANCODE_UP) return "ArrowUp";
    return "";
}

static void sqw_handle_event(SqwAppState *st, SDL_Event *ev) {
    if (ev->type == SDL_EVENT_QUIT || ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        st->running = 0;
    } else if (ev->type == SDL_EVENT_WINDOW_RESIZED) {
        int new_w = ev->window.data1, new_h = ev->window.data2;
        if (new_w > 0 && new_h > 0) {
            uint32_t packed = ((uint32_t)new_w << 16) | (uint32_t)new_h;
            sqw_vk_recreate_swapchain(st->vk, packed);
            st->viewport_w = (float)new_w; st->viewport_h = (float)new_h;
            /* Re-resolve CSS before relayout, not just relayout alone --
             * a "@media (min-width/max-width: ...)" rule's applicability
             * can change with the new width (see css_apply()'s own
             * comment), so without this a responsive breakpoint would
             * only ever take effect at initial page load, never live on
             * resize. */
            if (g_current_css_sheet_valid) css_apply(st->root, &g_current_css_sheet, st->viewport_w);
            layout_list_free(&st->boxes);
            layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes);
            st->scroll_x = clamp_scroll(st->scroll_x, st->boxes.content_w, st->viewport_w);
            st->scroll_y = clamp_scroll(st->scroll_y, st->boxes.content_h, st->viewport_h - SQW_TOOLBAR_H);
        }
    } else if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        st->mouse_x = ev->motion.x; st->mouse_y = ev->motion.y;
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[motion] x=%d y=%d\n", (int)st->mouse_x, (int)st->mouse_y); fflush(stderr); }
        if (st->dragging_v) {
            float content_view_h = st->viewport_h - SQW_TOOLBAR_H;
            float thumb_h = content_view_h * (content_view_h / st->boxes.content_h);
            if (thumb_h < SQW_SCROLLBAR_MIN_THUMB) thumb_h = SQW_SCROLLBAR_MIN_THUMB;
            float track_free = content_view_h - thumb_h;
            float delta_mouse = st->mouse_y - st->drag_anchor_mouse;
            float scale = (track_free > 0) ? (st->boxes.content_h - content_view_h) / track_free : 0.0f;
            st->scroll_y = clamp_scroll(st->drag_anchor_scroll + delta_mouse * scale, st->boxes.content_h, content_view_h);
        } else if (st->dragging_h) {
            float thumb_w = st->viewport_w * (st->viewport_w / st->boxes.content_w);
            if (thumb_w < SQW_SCROLLBAR_MIN_THUMB) thumb_w = SQW_SCROLLBAR_MIN_THUMB;
            float track_free = st->viewport_w - thumb_w;
            float delta_mouse = st->mouse_x - st->drag_anchor_mouse;
            float scale = (track_free > 0) ? (st->boxes.content_w - st->viewport_w) / track_free : 0.0f;
            st->scroll_x = clamp_scroll(st->drag_anchor_scroll + delta_mouse * scale, st->boxes.content_w, st->viewport_w);
        } else if (st->mouse_y >= SQW_TOOLBAR_H) {
            float cx = st->mouse_x + st->scroll_x, cy = (st->mouse_y - SQW_TOOLBAR_H) + st->scroll_y;
            int hit = layout_hit_test(&st->boxes, cx, cy);
            DomNode *new_hover = NULL;
            if (hit >= 0) {
                SqwBoxKind k = st->boxes.boxes[hit].kind;
                if (k == SQW_BOX_A || k == SQW_BOX_BUTTON) new_hover = st->boxes.boxes[hit].node;
                else if (k == SQW_BOX_TEXT) new_hover = interactive_ancestor(st->boxes.boxes[hit].node);
            }
            if (new_hover != st->hover_node) {
                if (st->hover_node) st->hover_node->hover = 0;
                if (new_hover) new_hover->hover = 1;
                /* Live ":hover" CSS re-resolve -- see g_current_css_sheet's
                 * own comment. Re-resolving only these two nodes (not the
                 * whole tree) is cheap enough to do on every hover change;
                 * doesn't reflow (a ":hover" rule that changes something
                 * layout-affecting won't visually take effect until the
                 * next real relayout -- see css_apply_one()'s own comment). */
                if (g_current_css_sheet_valid) {
                    if (st->hover_node) css_apply_one(st->hover_node, &g_current_css_sheet, st->viewport_w);
                    if (new_hover) css_apply_one(new_hover, &g_current_css_sheet, st->viewport_w);
                }
                st->hover_node = new_hover;
            }
        } else if (st->hover_node) {
            /* Cursor moved up into the toolbar -- clear any page hover
             * state so a link doesn't stay highlighted while the mouse is
             * nowhere near it. */
            st->hover_node->hover = 0;
            if (g_current_css_sheet_valid) css_apply_one(st->hover_node, &g_current_css_sheet, st->viewport_w);
            st->hover_node = NULL;
        }
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.button == 1) {
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[button-down] x=%d y=%d\n", (int)ev->button.x, (int)ev->button.y); fflush(stderr); }
        if (ev->button.y < SQW_TOOLBAR_H) {
            /* Toolbar interactions (Back/bar/Go) can all trigger a fresh
             * navigation that frees the current DOM tree -- drop keyboard
             * focus from any page input first so focused_input never ends
             * up pointing at a freed node (mirrors url_bar_focused's own
             * unconditional reset a few lines below in each branch). */
            if (st->focused_input) { st->focused_input->form_focused = 0; st->focused_input = NULL; }
            LayoutBox back, bar, go;
            toolbar_geometry(st->viewport_w, &back, &bar, &go);
            if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[toolbar-click] back=(%d,%d,%d,%d) bar=(%d,%d,%d,%d) go=(%d,%d,%d,%d)\n", (int)back.x,(int)back.y,(int)back.w,(int)back.h,(int)bar.x,(int)bar.y,(int)bar.w,(int)bar.h,(int)go.x,(int)go.y,(int)go.w,(int)go.h); fflush(stderr); }
            if (point_in_rect(ev->button.x, ev->button.y, &back)) {
                st->url_bar_focused = 0;
                char popped_url[SQW_PATH_MAX];
                if (sqw_history_pop(st->hist, popped_url, sizeof popped_url)) {
                    sqw_go_navigate(popped_url, 0, st->hist, st->current_url, &st->pending_fetch, st->pending_fetch_url,
                                    &st->root, &st->boxes, st->current_dir, st->current_base_url, st->viewport_w, st->viewport_h,
                                    &st->scroll_x, &st->scroll_y, &st->hover_node, &st->active_node);
                    strncpy(st->url_bar_text, popped_url, sizeof st->url_bar_text - 1);
                    st->url_bar_text[sizeof st->url_bar_text - 1] = 0;
                }
            } else if (point_in_rect(ev->button.x, ev->button.y, &bar)) {
                st->url_bar_focused = 1;
            } else if (point_in_rect(ev->button.x, ev->button.y, &go)) {
                st->url_bar_focused = 0;
                sqw_go_navigate(st->url_bar_text, 1, st->hist, st->current_url, &st->pending_fetch, st->pending_fetch_url,
                                &st->root, &st->boxes, st->current_dir, st->current_base_url, st->viewport_w, st->viewport_h,
                                &st->scroll_x, &st->scroll_y, &st->hover_node, &st->active_node);
            } else {
                st->url_bar_focused = 0;
            }
        } else {
            st->url_bar_focused = 0; /* clicking the page always exits URL-bar editing, same as a real browser */
            LayoutBox vthumb, hthumb;
            float content_view_h = st->viewport_h - SQW_TOOLBAR_H;
            vthumb.w = 0; hthumb.h = 0;
            if (st->boxes.content_h > content_view_h) {
                float thumb_h = content_view_h * (content_view_h / st->boxes.content_h);
                if (thumb_h < SQW_SCROLLBAR_MIN_THUMB) thumb_h = SQW_SCROLLBAR_MIN_THUMB;
                float track_free = content_view_h - thumb_h;
                float max_scroll = st->boxes.content_h - content_view_h;
                float thumb_y = SQW_TOOLBAR_H + ((max_scroll > 0) ? (st->scroll_y / max_scroll) * track_free : 0.0f);
                vthumb.x = st->viewport_w - SQW_SCROLLBAR_THICKNESS; vthumb.y = thumb_y;
                vthumb.w = SQW_SCROLLBAR_THICKNESS; vthumb.h = thumb_h;
            }
            if (st->boxes.content_w > st->viewport_w) {
                float thumb_w = st->viewport_w * (st->viewport_w / st->boxes.content_w);
                if (thumb_w < SQW_SCROLLBAR_MIN_THUMB) thumb_w = SQW_SCROLLBAR_MIN_THUMB;
                float track_free = st->viewport_w - thumb_w;
                float max_scroll = st->boxes.content_w - st->viewport_w;
                float thumb_x = (max_scroll > 0) ? (st->scroll_x / max_scroll) * track_free : 0.0f;
                hthumb.x = thumb_x; hthumb.y = st->viewport_h - SQW_SCROLLBAR_THICKNESS;
                hthumb.w = thumb_w; hthumb.h = SQW_SCROLLBAR_THICKNESS;
            }
            LayoutBox vtrack, htrack;
            vtrack.x = st->viewport_w - SQW_SCROLLBAR_THICKNESS; vtrack.y = SQW_TOOLBAR_H;
            vtrack.w = SQW_SCROLLBAR_THICKNESS; vtrack.h = content_view_h;
            htrack.x = 0.0f; htrack.y = st->viewport_h - SQW_SCROLLBAR_THICKNESS;
            htrack.w = st->viewport_w; htrack.h = SQW_SCROLLBAR_THICKNESS;
            if (vthumb.w > 0 && point_in_rect(ev->button.x, ev->button.y, &vthumb)) {
                st->dragging_v = 1; st->drag_anchor_mouse = ev->button.y; st->drag_anchor_scroll = st->scroll_y;
            } else if (hthumb.h > 0 && point_in_rect(ev->button.x, ev->button.y, &hthumb)) {
                st->dragging_h = 1; st->drag_anchor_mouse = ev->button.x; st->drag_anchor_scroll = st->scroll_x;
            } else if (vthumb.w > 0 && point_in_rect(ev->button.x, ev->button.y, &vtrack)) {
                /* Clicked the empty ("white") vertical track above/below
                 * the thumb -- classic scrollbar UX (matches every desktop
                 * toolkit's own track-click behavior) is to page up/down
                 * by one viewport-height toward the click, NOT to jump the
                 * thumb straight to the click position (that jump-to-click
                 * behavior is scrollbar THUMB-drag/click behavior, a
                 * different, more abrupt interaction some platforms use
                 * only as an opt-in setting) and NOT to start a drag -- a
                 * single track click is one discrete page step, not a drag
                 * gesture. */
                if (ev->button.y < vthumb.y) st->scroll_y = clamp_scroll(st->scroll_y - content_view_h, st->boxes.content_h, content_view_h);
                else st->scroll_y = clamp_scroll(st->scroll_y + content_view_h, st->boxes.content_h, content_view_h);
            } else if (hthumb.h > 0 && point_in_rect(ev->button.x, ev->button.y, &htrack)) {
                if (ev->button.x < hthumb.x) st->scroll_x = clamp_scroll(st->scroll_x - st->viewport_w, st->boxes.content_w, st->viewport_w);
                else st->scroll_x = clamp_scroll(st->scroll_x + st->viewport_w, st->boxes.content_w, st->viewport_w);
            } else {
                float cx = ev->button.x + st->scroll_x, cy = (ev->button.y - SQW_TOOLBAR_H) + st->scroll_y;
                int hit = layout_hit_test(&st->boxes, cx, cy);
                if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[hit-test] cx=%d cy=%d hit=%d kind=%d\n", (int)cx, (int)cy, hit, hit>=0?(int)st->boxes.boxes[hit].kind:-1); fflush(stderr); }
                if (hit >= 0) {
                    LayoutBox *hb = &st->boxes.boxes[hit];
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
                    /* JS onclick dispatch -- walks up from the resolved
                     * target (covers a plain "<div onclick=...>" too, not
                     * just <a>/<button>) looking for the nearest ancestor
                     * (including itself) with a registered handler (either
                     * a real "onclick" HTML attribute, compiled once by
                     * js_wire_onclick_attrs(), or a script's own
                     * addEventListener('click',...)/".onclick = fn" --
                     * see js_engine.h's own comment). Run BEFORE the
                     * ordinary <a>/<button> handling below so a handler
                     * that mutates the DOM (innerHTML, etc) is reflected
                     * immediately; if it did mutate anything, this click
                     * is treated as fully handled by JS and the function
                     * returns right here -- target_node/hb/st->boxes may
                     * all be stale after a DOM mutation (a relayout just
                     * freed and rebuilt st->boxes), so nothing below this
                     * point may safely touch them. A handler that did NOT
                     * mutate the DOM (e.g. just a console.log) falls
                     * through to the normal href/submit handling below,
                     * so "<a onclick=... href=...>" still navigates too --
                     * this engine has no real preventDefault() to
                     * suppress that, a documented, deliberate gap. */
                    {
                        DomNode *oc_node = target_node;
                        while (oc_node && !oc_node->js_onclick) oc_node = oc_node->parent;
                        if (oc_node) {
                            int relayout = 0;
                            js_dispatch_click(g_current_js_interp, oc_node, &relayout);
                            if (relayout) {
                                layout_list_free(&st->boxes);
                                layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes);
                                return;
                            }
                        }
                    }
                    /* Clicking outside any input/textarea drops keyboard
                     * focus from whichever one had it -- reassigned below
                     * if this exact click lands on one instead. */
                    if (st->focused_input) {
                        DomNode *blurred = st->focused_input;
                        blurred->form_focused = 0; st->focused_input = NULL;
                        int relayout = 0;
                        js_dispatch_change(g_current_js_interp, blurred, &relayout);
                        if (relayout) { layout_list_free(&st->boxes); layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes); return; }
                    }
                    if (eff_kind == SQW_BOX_A && find_enclosing_form(target_node)) {
                        /* Per this project's own explicit spec (an anchor
                         * inside a <form> submits it, same as a real
                         * submit button -- non-standard real HTML5
                         * behavior, deliberately added here on request
                         * rather than following the href normally). */
                        target_node->visited = 1;
                        target_node->active = 1;
                        st->active_node = target_node;
                        submit_form(find_enclosing_form(target_node), st->hist, st->current_url,
                                    &st->pending_fetch, st->pending_fetch_url, &st->root, &st->boxes,
                                    st->current_dir, st->current_base_url, st->viewport_w, st->viewport_h,
                                    &st->scroll_x, &st->scroll_y, &st->hover_node, &st->active_node);
                    } else if (eff_kind == SQW_BOX_A) {
                        target_node->visited = 1;
                        target_node->active = 1;
                        st->active_node = target_node;
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
                            DomNode *target = find_by_id(st->root, href + 1);
                            LayoutBox *tb = target ? find_box_for_node(&st->boxes, target) : NULL;
                            if (tb) st->scroll_y = clamp_scroll(tb->y, st->boxes.content_h, content_view_h);
                        } else if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
                            if (st->pending_fetch) {
                                /* A previous fetch is still outstanding --
                                 * abandon it (NOT sqw_net_result_free() --
                                 * see that function's own comment: freeing
                                 * it here while its background worker
                                 * thread might still be running is a real
                                 * use-after-free, confirmed as an actual
                                 * "free(): invalid pointer" crash during
                                 * this project's own live testing) rather
                                 * than leak it or race two responses
                                 * against one DOM swap. */
                                sqw_net_result_abandon(st->pending_fetch);
                            }
                            sqw_history_push(st->hist, st->current_url);
                            fprintf(stderr, "SQW: fetching %s ...\n", href); fflush(stdout);
                            strncpy(st->pending_fetch_url, href, sizeof st->pending_fetch_url - 1);
                            st->pending_fetch_url[sizeof st->pending_fetch_url - 1] = 0;
                            st->pending_fetch = sqw_net_fetch_async(href);
                        } else if (href[0] && st->current_base_url[0]) {
                            /* A plain relative href on a page that was
                             * itself reached over the network resolves
                             * against THAT page's own URL and is fetched
                             * over the network too -- real browser
                             * behavior, and the actual fix for "page 3
                             * (reached via the network anchor) links back
                             * to page 1 by a plain href=\"index.html\";
                             * clicking it did nothing" (see
                             * sqw_navigate_to_html()'s own comment for the
                             * full story: current_base_url is empty for a
                             * LOCALLY loaded page, which is what routes
                             * this same href through the local branch
                             * below instead). */
                            char full_url[SQW_PATH_MAX];
                            snprintf(full_url, sizeof full_url, "%s%s", st->current_base_url, href);
                            if (st->pending_fetch) sqw_net_result_abandon(st->pending_fetch);
                            sqw_history_push(st->hist, st->current_url);
                            fprintf(stderr, "SQW: fetching %s ...\n", full_url); fflush(stdout);
                            strncpy(st->pending_fetch_url, full_url, sizeof st->pending_fetch_url - 1);
                            st->pending_fetch_url[sizeof st->pending_fetch_url - 1] = 0;
                            st->pending_fetch = sqw_net_fetch_async(full_url);
                        } else if (href[0]) {
                            /* Local relative path: resolve against the
                             * CURRENT page's own directory, not a fixed
                             * base -- see sqw_dirname()'s comment. */
                            char full_path[SQW_PATH_MAX];
                            snprintf(full_path, sizeof full_path, "%s%s", st->current_dir, href);
                            sqw_history_push(st->hist, st->current_url);
                            sqw_navigate_to(full_path, &st->root, &st->boxes, st->current_dir, st->current_base_url, st->viewport_w, st->viewport_h);
                            strncpy(st->url_bar_text, full_path, sizeof st->url_bar_text - 1); st->url_bar_text[sizeof st->url_bar_text - 1] = 0;
                            strncpy(st->current_url, full_path, sizeof st->current_url - 1); st->current_url[sizeof st->current_url - 1] = 0;
                            st->scroll_x = 0.0f; st->scroll_y = 0.0f;
                            st->hover_node = NULL; st->active_node = NULL; st->focused_input = NULL; /* old DOM (and target_node) is gone */
                        }
                    } else if (eff_kind == SQW_BOX_BUTTON) {
                        target_node->active = 1;
                        st->active_node = target_node;
                        /* A real <button> with no explicit "type" is
                         * itself submit-by-default inside a form (the
                         * actual HTML5 rule, not this project's own
                         * relaxation); <input type="submit"> obviously
                         * always is. type="button"/"reset" (an explicit
                         * non-submit <button>, or an <input
                         * type="button">) never submits. */
                        const char *type = dom_get_attr(target_node, "type");
                        int is_submit;
                        if (strcmp(target_node->tag, "input") == 0) is_submit = type && strcmp(type, "submit") == 0;
                        else is_submit = !type || strcmp(type, "submit") == 0;
                        if (is_submit) {
                            DomNode *form = find_enclosing_form(target_node);
                            if (form) submit_form(form, st->hist, st->current_url, &st->pending_fetch, st->pending_fetch_url,
                                                   &st->root, &st->boxes, st->current_dir, st->current_base_url, st->viewport_w, st->viewport_h,
                                                   &st->scroll_x, &st->scroll_y, &st->hover_node, &st->active_node);
                        }
                    } else if (hb->kind == SQW_BOX_INPUT_TEXT || hb->kind == SQW_BOX_TEXTAREA) {
                        target_node->form_focused = 1;
                        st->focused_input = target_node;
                    } else if (hb->kind == SQW_BOX_INPUT_CHECK) {
                        const char *type = dom_get_attr(target_node, "type");
                        if (type && strcmp(type, "radio") == 0) {
                            /* Real radio-group exclusivity: only one
                             * same-"name" radio within the same enclosing
                             * <form> may be checked at once -- clear every
                             * sibling radio in that group first, then
                             * check this one (a click on an
                             * already-checked radio stays checked, it just
                             * can't be unchecked by clicking it again --
                             * real browser behavior). */
                            const char *name = dom_get_attr(target_node, "name");
                            DomNode *form = find_enclosing_form(target_node);
                            if (name && form) {
                                RadioGroupCtx rgctx; rgctx.name = name; rgctx.skip = target_node;
                                dom_walk(form, radio_group_clear_visit, &rgctx);
                            }
                            target_node->form_checked = 1;
                        } else {
                            target_node->form_checked = !target_node->form_checked;
                        }
                        {
                            int relayout = 0;
                            js_dispatch_change(g_current_js_interp, target_node, &relayout);
                            if (relayout) { layout_list_free(&st->boxes); layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes); return; }
                        }
                    }
                }
            }
        }
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP && ev->button.button == 1) {
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[button-up] x=%d y=%d\n", (int)ev->button.x, (int)ev->button.y); fflush(stderr); }
        st->dragging_v = 0; st->dragging_h = 0;
        if (st->active_node) { st->active_node->active = 0; st->active_node = NULL; }
    } else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        st->scroll_y = clamp_scroll(st->scroll_y - ev->wheel.y * ((SQW_FONT_CELL_H * SQW_TEXT_SCALE * 3.0f)), st->boxes.content_h, st->viewport_h - SQW_TOOLBAR_H);
        if (ev->wheel.x != 0.0f) st->scroll_x = clamp_scroll(st->scroll_x - ev->wheel.x * ((SQW_FONT_CELL_H * SQW_TEXT_SCALE * 3.0f)), st->boxes.content_w, st->viewport_w);
    } else if (ev->type == SDL_EVENT_KEY_DOWN && st->url_bar_focused) {
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[key-down-focused] scancode=%d\n", (int)ev->key.scancode); fflush(stderr); }
        /* URL bar editing: Backspace trims the last character, Enter/
         * Return submits (same as clicking Go), Escape cancels editing and
         * reverts the bar to the current page's own URL/path. Everything
         * else (including the page-scroll keys handled in the other
         * branch below) is deliberately ignored while editing -- real
         * browsers don't scroll the page out from under you while you're
         * typing in the address bar either. */
        int ulen = (int)strlen(st->url_bar_text);
        if (ev->key.scancode == SDL_SCANCODE_BACKSPACE) {
            if (ulen > 0) st->url_bar_text[ulen - 1] = 0;
        } else if (ev->key.scancode == SDL_SCANCODE_RETURN) {
            st->url_bar_focused = 0;
            sqw_go_navigate(st->url_bar_text, 1, st->hist, st->current_url, &st->pending_fetch, st->pending_fetch_url,
                            &st->root, &st->boxes, st->current_dir, st->current_base_url, st->viewport_w, st->viewport_h,
                            &st->scroll_x, &st->scroll_y, &st->hover_node, &st->active_node);
        } else if (ev->key.scancode == SDL_SCANCODE_ESCAPE) {
            st->url_bar_focused = 0;
            if (st->current_base_url[0]) { strncpy(st->url_bar_text, st->pending_fetch_url, sizeof st->url_bar_text - 1); }
            else { snprintf(st->url_bar_text, sizeof st->url_bar_text, "%sindex.html", st->current_dir); }
            st->url_bar_text[sizeof st->url_bar_text - 1] = 0;
        }
    } else if (ev->type == SDL_EVENT_KEY_DOWN && st->focused_input) {
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[key-down-input-focused] scancode=%d\n", (int)ev->key.scancode); fflush(stderr); }
        {
            int relayout = 0;
            js_dispatch_keydown(g_current_js_interp, st->focused_input, sqw_scancode_key_name(ev->key.scancode), &relayout);
            if (relayout) { layout_list_free(&st->boxes); layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes); return; }
        }
        int flen = (int)strlen(st->focused_input->form_value);
        if (ev->key.scancode == SDL_SCANCODE_BACKSPACE) {
            if (flen > 0) {
                st->focused_input->form_value[flen - 1] = 0;
                int relayout = 0;
                js_dispatch_input(g_current_js_interp, st->focused_input, &relayout);
                if (relayout) { layout_list_free(&st->boxes); layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes); return; }
            }
        } else if (ev->key.scancode == SDL_SCANCODE_RETURN) {
            if (strcmp(st->focused_input->tag, "textarea") == 0) {
                /* Real <textarea> behavior: Enter inserts a literal
                 * newline into the field's own value instead of doing
                 * anything form-wide -- draw_layout_text() already splits
                 * form_value on '\n' into separate rendered lines. */
                if (flen < (int)sizeof st->focused_input->form_value - 1) {
                    st->focused_input->form_value[flen] = '\n';
                    st->focused_input->form_value[flen + 1] = 0;
                    int relayout = 0;
                    js_dispatch_input(g_current_js_interp, st->focused_input, &relayout);
                    if (relayout) { layout_list_free(&st->boxes); layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes); return; }
                }
            } else {
                /* Real HTML5 behavior: Enter in a single-line text field
                 * submits its enclosing form, same as clicking that
                 * form's own submit control. */
                DomNode *form = find_enclosing_form(st->focused_input);
                if (form) {
                    DomNode *submitted_input = st->focused_input;
                    st->focused_input->form_focused = 0; st->focused_input = NULL;
                    {
                        int relayout = 0;
                        js_dispatch_change(g_current_js_interp, submitted_input, &relayout);
                        if (relayout) { layout_list_free(&st->boxes); layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes); return; }
                    }
                    submit_form(form, st->hist, st->current_url, &st->pending_fetch, st->pending_fetch_url,
                                &st->root, &st->boxes, st->current_dir, st->current_base_url, st->viewport_w, st->viewport_h,
                                &st->scroll_x, &st->scroll_y, &st->hover_node, &st->active_node);
                }
            }
        }
    } else if (ev->type == SDL_EVENT_TEXT_INPUT && st->focused_input) {
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[text-input-focused] text=\"%s\"\n", ev->text.text ? ev->text.text : "(null)"); fflush(stderr); }
        int flen = (int)strlen(st->focused_input->form_value);
        int tlen = (int)strlen(ev->text.text);
        int room = (int)sizeof(st->focused_input->form_value) - 1 - flen;
        if (tlen > room) tlen = room;
        if (tlen > 0) {
            memcpy(st->focused_input->form_value + flen, ev->text.text, (size_t)tlen); st->focused_input->form_value[flen + tlen] = 0;
            int relayout = 0;
            js_dispatch_input(g_current_js_interp, st->focused_input, &relayout);
            if (relayout) { layout_list_free(&st->boxes); layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes); return; }
        }
    } else if (ev->type == SDL_EVENT_TEXT_INPUT && st->url_bar_focused) {
        /* Real, keyboard-layout-aware printable text (see
         * PRIVATE_PumpEvents' own XLookupString comment) -- append-only,
         * capped so it always leaves room for the final NUL. */
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[text-input] text=\"%s\"\n", ev->text.text ? ev->text.text : "(null)"); fflush(stderr); }
        int ulen = (int)strlen(st->url_bar_text);
        int tlen = (int)strlen(ev->text.text);
        int room = (int)sizeof(st->url_bar_text) - 1 - ulen;
        if (tlen > room) tlen = room;
        if (tlen > 0) { memcpy(st->url_bar_text + ulen, ev->text.text, (size_t)tlen); st->url_bar_text[ulen + tlen] = 0; }
    } else if (ev->type == SDL_EVENT_TEXT_INPUT) {
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[text-input] IGNORED (url_bar_focused=%d) text=\"%s\"\n", st->url_bar_focused, ev->text.text ? ev->text.text : "(null)"); fflush(stderr); }
    } else if (ev->type == SDL_EVENT_KEY_DOWN) {
        if (getenv("SQW_INPUT_DEBUG")) { fprintf(stderr, "[key-down] scancode=%d\n", (int)ev->key.scancode); fflush(stderr); }
        /* Page Up/Down page by one viewport (matches the scrollbar
         * track-click behavior -- see its own comment), Home/End jump to
         * the very top/bottom, Up/Down nudge by one text line -- the
         * standard keyboard scrolling set every desktop browser/reader
         * supports, translated from real X11 key events by
         * SDL3_Build/sdl_core.inc's own PRIVATE_PumpEvents (only this
         * small fixed set of navigation keys, not a full keymap -- see
         * that function's own comment). */
        float content_view_h = st->viewport_h - SQW_TOOLBAR_H;
        if (ev->key.scancode == SDL_SCANCODE_PAGEDOWN) {
            st->scroll_y = clamp_scroll(st->scroll_y + content_view_h, st->boxes.content_h, content_view_h);
        } else if (ev->key.scancode == SDL_SCANCODE_PAGEUP) {
            st->scroll_y = clamp_scroll(st->scroll_y - content_view_h, st->boxes.content_h, content_view_h);
        } else if (ev->key.scancode == SDL_SCANCODE_HOME) {
            st->scroll_y = 0.0f;
        } else if (ev->key.scancode == SDL_SCANCODE_END) {
            st->scroll_y = clamp_scroll(st->boxes.content_h, st->boxes.content_h, content_view_h);
        } else if (ev->key.scancode == SDL_SCANCODE_DOWN) {
            st->scroll_y = clamp_scroll(st->scroll_y + (SQW_FONT_CELL_H * SQW_TEXT_SCALE * 3.0f), st->boxes.content_h, content_view_h);
        } else if (ev->key.scancode == SDL_SCANCODE_UP) {
            st->scroll_y = clamp_scroll(st->scroll_y - (SQW_FONT_CELL_H * SQW_TEXT_SCALE * 3.0f), st->boxes.content_h, content_view_h);
        }
    }
}

/* Polls the outstanding background fetch (if any) started by an anchor
 * click/form submit/Go-Enter/Back, and swaps in the fetched page once it's
 * ready -- see net_client.h's own comment on the async model. */
static void sqw_check_pending_fetch(SqwAppState *st) {
    if (!st->pending_fetch) return;
    pthread_mutex_lock(&st->pending_fetch->mutex);
    int fetch_ready = st->pending_fetch->ready;
    int fetch_success = st->pending_fetch->success;
    char *fetch_body = st->pending_fetch->body;
    st->fetch_content_length = st->pending_fetch->content_length;
    st->fetch_bytes_received = st->pending_fetch->bytes_received;
    pthread_mutex_unlock(&st->pending_fetch->mutex);
    if (!fetch_ready) return;
    if (fetch_success) {
        sqw_navigate_to_html(fetch_body, st->pending_fetch_url, &st->root, &st->boxes, st->current_dir, st->current_base_url, st->viewport_w, st->viewport_h);
        strncpy(st->url_bar_text, st->pending_fetch_url, sizeof st->url_bar_text - 1); st->url_bar_text[sizeof st->url_bar_text - 1] = 0;
        /* current_url tracks whatever page is ACTUALLY loaded, so it's
         * only updated here on a successful fetch, not when the fetch was
         * merely requested (that's where the Back-button history push
         * already happened, see the anchor-click/sqw_go_navigate call
         * sites) -- a failed fetch leaves current_url (and the Back stack)
         * exactly as they were, so Back still correctly retreats to
         * wherever the user actually was. */
        strncpy(st->current_url, st->pending_fetch_url, sizeof st->current_url - 1); st->current_url[sizeof st->current_url - 1] = 0;
        st->scroll_x = 0.0f; st->scroll_y = 0.0f;
        st->hover_node = NULL; st->active_node = NULL; st->focused_input = NULL; /* old DOM is gone */
    } else {
        fprintf(stderr, "SQW: fetch failed\n"); fflush(stdout);
    }
    sqw_net_result_free(st->pending_fetch);
    st->pending_fetch = NULL;
}

/* Renders and presents exactly one frame -- skips (returns without
 * incrementing frame_count, matching the original "continue") on a 0x0
 * minimized-window extent, the one case sqw_vk_begin_frame() itself
 * signals by returning NULL. */
/* Debug-only: copies swapchain image `imageIndex` (already presented --
 * caller must vkQueueWaitIdle() first) straight out of VRAM into a binary
 * PPM file, bypassing X11/the window system entirely -- for local
 * automated testing where a real X11 screenshot tool isn't available/
 * reliable (a headless or otherwise unusual X server can fail an ordinary
 * XGetImage against this project's own window with BadMatch even though
 * the frame itself rendered fine). Triggered by SQW_TEST_SCREENSHOT_FRAME/
 * SQW_TEST_SCREENSHOT_PATH, see sqw_draw_frame()'s own use of it -- same
 * SQW_TEST_* local-testing-only convention as the synthetic click/key
 * env vars already in SqwAppState. Synchronous and slow (one-shot command
 * buffer + a full queue wait) -- fine for a single debug capture, never
 * called on a normal run. */
static void sqw_debug_screenshot(SqwVkContext *vk, uint32_t imageIndex, const char *path) {
    uint32_t w = vk->extent.width, h = vk->extent.height;
    VkDeviceSize size = (VkDeviceSize)w * (VkDeviceSize)h * 4;

    VkBuffer stagingBuf;
    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk->device, &bufInfo, NULL, &stagingBuf) != VK_SUCCESS) return;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(vk->device, stagingBuf, &memReq);
    VkMemoryAllocateInfo allocInfo;
    memset(&allocInfo, 0, sizeof(allocInfo));
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = sqw_vk_find_memory_type(vk, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem;
    if (vkAllocateMemory(vk->device, &allocInfo, NULL, &stagingMem) != VK_SUCCESS) return;
    vkBindBufferMemory(vk->device, stagingBuf, stagingMem, 0);

    VkCommandBufferAllocateInfo cbInfo;
    memset(&cbInfo, 0, sizeof(cbInfo));
    cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbInfo.commandPool = vk->commandPool;
    cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(vk->device, &cbInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo;
    memset(&beginInfo, 0, sizeof(beginInfo));
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toSrc;
    memset(&toSrc, 0, sizeof(toSrc));
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = 0xFFFFFFFFu;
    toSrc.dstQueueFamilyIndex = 0xFFFFFFFFu;
    toSrc.image = vk->swapImages[imageIndex];
    toSrc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toSrc.subresourceRange.levelCount = 1;
    toSrc.subresourceRange.layerCount = 1;
    toSrc.srcAccessMask = 0;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &toSrc);

    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = w;
    region.imageExtent.height = h;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, vk->swapImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuf, 1, &region);

    VkImageMemoryBarrier toPresent = toSrc;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, NULL, 0, NULL, 1, &toPresent);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(vk->queue, 1, &submitInfo, NULL);
    vkQueueWaitIdle(vk->queue);
    vkFreeCommandBuffers(vk->device, vk->commandPool, 1, &cmd);

    void *mapped = NULL;
    vkMapMemory(vk->device, stagingMem, 0, size, 0, &mapped);
    unsigned char *px = (unsigned char *)mapped;

    FILE *fp = fopen(path, "wb");
    if (fp) {
        fprintf(fp, "P6\n%u %u\n255\n", w, h);
        uint32_t i;
        /* Swapchain format is B8G8R8A8_UNORM (see vk_context.c's own
         * chosenFormat) -- swap B/R, drop alpha, PPM wants plain RGB. */
        for (i = 0; i < w * h; i++) {
            unsigned char b = px[i * 4 + 0], g = px[i * 4 + 1], r = px[i * 4 + 2];
            fputc(r, fp); fputc(g, fp); fputc(b, fp);
        }
        fclose(fp);
        fprintf(stderr, "SQW: screenshot written to %s (%ux%u)\n", path, w, h); fflush(stdout);
    }

    vkUnmapMemory(vk->device, stagingMem);
    vkDestroyBuffer(vk->device, stagingBuf, NULL);
    vkFreeMemory(vk->device, stagingMem, NULL);
}

static void sqw_draw_frame(SqwAppState *st) {
    uint32_t imageIndex = 0;
    VkCommandBuffer cmd = sqw_vk_begin_frame(st->vk, 0.95f, 0.95f, 0.95f, 1.0f, &imageIndex);
    if (!cmd) return;
    /* "scroll_y - SQW_TOOLBAR_H" (not raw scroll_y): both draw calls
     * compute each box's screen Y as "box.y - scroll_y", so passing a
     * SMALLER effective scroll value shifts every drawn box DOWN by
     * exactly the difference -- i.e. by SQW_TOOLBAR_H -- without needing
     * to touch renderer_vk.c or draw_layout_text() at all. The real
     * scroll_y (unshifted) is still what every hit-test/scrollbar/paging
     * computation in sqw_handle_event() uses -- only these two draw calls
     * see the adjusted value. */
    float draw_scroll_y = st->scroll_y - SQW_TOOLBAR_H;
    sqw_renderer_draw(st->vk, st->renderer, cmd, &st->boxes, st->viewport_w, st->viewport_h, st->scroll_x, draw_scroll_y);
    draw_layout_borders(st->vk, st->renderer, cmd, &st->boxes, st->viewport_w, st->viewport_h, st->scroll_x, draw_scroll_y);
    sqw_image_renderer_begin_frame(st->image_renderer);
    draw_layout_images(st->image_renderer, st->vk, cmd, &st->boxes, st->viewport_w, st->viewport_h, st->scroll_x, draw_scroll_y);
    draw_layout_text(st->text_renderer, st->vk, st->renderer, cmd, &st->boxes, st->viewport_w, st->viewport_h, st->scroll_x, draw_scroll_y);
    LayoutBox vthumb_dummy, hthumb_dummy;
    draw_scrollbars(st->vk, st->renderer, cmd, st->boxes.content_w, st->boxes.content_h, st->viewport_w, st->viewport_h, st->scroll_x, st->scroll_y, &vthumb_dummy, &hthumb_dummy);
    draw_toolbar(st->vk, st->renderer, st->text_renderer, cmd, st->url_bar_text, st->url_bar_focused, st->hist->count > 0, st->viewport_w, st->viewport_h);
    if (st->pending_fetch) {
        sqw_draw_loading_bar(st->vk, st->renderer, cmd, st->fetch_bytes_received, st->fetch_content_length,
            st->frame_count, st->viewport_w, st->viewport_h);
    }
    sqw_text_renderer_flush(st->vk, st->text_renderer, cmd, st->viewport_w, st->viewport_h);
    sqw_vk_end_frame(st->vk, cmd, imageIndex);

    st->frame_count++;
    if (st->frame_count % 300 == 0) { fprintf(stderr, "SQW: frame=%d\n", st->frame_count); fflush(stdout); }

    if (st->test_screenshot_frame >= 0 && st->frame_count == st->test_screenshot_frame) {
        vkQueueWaitIdle(st->vk->queue); /* wait for THIS frame's present before reading it back */
        sqw_debug_screenshot(st->vk, imageIndex, st->test_screenshot_path[0] ? st->test_screenshot_path : "SQW/screenshot.ppm");
    }
}

int main(void) {
    squash_init_private_bootstrap();
    js_set_fetch_hook(sqw_js_fetch_start, 0); /* wires JS's fetch() to net_client.c's real async HTTP client -- see js_engine.h's own comment */

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

    /* Heap-allocated, not a stack local (same rationale as vk/renderer/
     * text_renderer/hist below): see SqwAppState's own comment -- this IS
     * the fix for squash's too-many-live-locals codegen bug, applied to
     * main() itself. */
    SqwAppState *st = (SqwAppState *)malloc(sizeof(SqwAppState));
    memset(st, 0, sizeof(*st));
    st->window = window;

    /* Textual startup-progress reporting for the Vulkan/renderer bring-up
     * phase -- the ONLY place these steps' progress can be shown at all:
     * nothing can be presented to the window until the swapchain and every
     * renderer's pipeline already exist (sqw_renderer_draw_rect() and
     * friends all require them), so the on-screen loading bar
     * (sqw_draw_loading_bar(), used once real page navigation starts)
     * can't draw anything DURING this phase -- by the time it can run for
     * the first time, this phase is always already 100% done. Anticipated
     * weights below are rough (not measured timings), just enough to give
     * this init sequence a legible sense of progress on the console. */
    fprintf(stderr, "SQW: loading 0%% (starting Vulkan init)\n"); fflush(stdout);
    st->vk = (SqwVkContext *)malloc(sizeof(SqwVkContext));
#ifdef __linux__
    if (!sqw_vk_context_init(st->vk, dpy, win, ((uint32_t)SQW_VIEWPORT_W << 16) | (uint32_t)SQW_VIEWPORT_H)) {
#else
    if (!sqw_vk_context_init(st->vk, hinstance, hwnd, ((uint32_t)SQW_VIEWPORT_W << 16) | (uint32_t)SQW_VIEWPORT_H)) {
#endif
        fprintf(stderr, "SQW: Vulkan init failed\n"); fflush(stdout);
        return 1;
    }
    fprintf(stderr, "SQW: loading 40%% (Vulkan device/swapchain ready)\n"); fflush(stdout);

    st->renderer = (SqwRenderer *)malloc(sizeof(SqwRenderer));
    if (!sqw_renderer_init(st->vk, st->renderer)) {
        fprintf(stderr, "SQW: renderer init failed\n"); fflush(stdout);
        return 1;
    }
    fprintf(stderr, "SQW: loading 60%% (shape renderer ready)\n"); fflush(stdout);

    st->text_renderer = (SqwTextRenderer *)malloc(sizeof(SqwTextRenderer));
    if (!sqw_text_renderer_init(st->vk, st->text_renderer)) {
        fprintf(stderr, "SQW: text renderer init failed\n"); fflush(stdout);
        return 1;
    }
    fprintf(stderr, "SQW: loading 80%% (text renderer ready)\n"); fflush(stdout);

    st->image_renderer = (SqwImageRenderer *)malloc(sizeof(SqwImageRenderer));
    if (!sqw_image_renderer_init(st->vk, st->image_renderer)) {
        fprintf(stderr, "SQW: image renderer init failed\n"); fflush(stdout);
        return 1;
    }
    sqw_image_cache_init(st->vk, st->image_renderer);
    layout_set_image_size_lookup(sqw_image_cache_get_size);
    fprintf(stderr, "SQW: loading 100%% (Vulkan/renderer init complete)\n"); fflush(stdout);

    sqw_dirname(SQW_INITIAL_PAGE, st->current_dir);
    /* Non-empty only when the CURRENT page was reached over the network
     * (e.g. "http://127.0.0.1:8080/") -- see sqw_navigate_to_html()'s own
     * comment for why a plain relative href needs this to resolve
     * correctly on such a page instead of being (wrongly) treated as a
     * local file path. */
    st->current_base_url[0] = '\0';
    /* Set right before starting a fetch, read back once it completes, so
     * sqw_navigate_to_html() knows what URL the fetched page's own
     * relative hrefs should resolve against next. */
    st->pending_fetch_url[0] = '\0';
    /* Tracks the actual currently-loaded page, independent of the
     * editable url_bar_text (which holds whatever the user is mid-typing
     * and gets clobbered character-by-character -- see url_bar_text's own
     * comment below) -- this is what actually gets pushed onto the Back
     * button's history stack, and what a page navigated AWAY from is
     * identified by. */
    strncpy(st->current_url, SQW_INITIAL_PAGE, sizeof st->current_url - 1);
    st->current_url[sizeof st->current_url - 1] = 0;

    st->hist = (SqwHistoryStack *)malloc(sizeof(SqwHistoryStack));
    memset(st->hist, 0, sizeof(*st->hist));

    char *initial_html = sqw_read_file(SQW_INITIAL_PAGE);
    if (!initial_html) {
        fprintf(stderr, "SQW: cannot open initial page %s\n", SQW_INITIAL_PAGE); fflush(stdout);
        return 1;
    }
    st->root = dom_parse(initial_html);
    free(initial_html);
    st->viewport_w = SQW_VIEWPORT_W; st->viewport_h = SQW_VIEWPORT_H;
    sqw_apply_css(st->root, st->current_dir, st->current_base_url, st->viewport_w);
    sqw_resolve_image_urls(st->root, st->current_dir, st->current_base_url);
    layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes);
    fprintf(stderr, "SQW: DOM parsed, layout computed (%d boxes, content %.0fx%.0f)\n",
        st->boxes.count, st->boxes.content_w, st->boxes.content_h); fflush(stdout);

    /* URL/search bar state -- always shows the current page's own path/URL
     * (updated after every successful navigation, local or network, same
     * as a real browser's address bar) unless the user is actively
     * editing it (url_bar_focused). Editing is append/backspace-at-the-end
     * only (see sqw_handle_event()'s SDL_EVENT_TEXT_INPUT/BACKSPACE
     * handling) -- no mid-string cursor movement, arrow-key editing, or
     * selection; a minimal but fully usable "type a URL, press Enter or
     * click Go" bar, not a full text-field widget. */
    strncpy(st->url_bar_text, SQW_INITIAL_PAGE, sizeof st->url_bar_text - 1);
    st->url_bar_text[sizeof st->url_bar_text - 1] = 0;

    /* SQW_TEST_GOTO_URL: pre-fills the URL bar as if the user had typed it
     * (bypassing per-character SDL_EVENT_TEXT_INPUT simulation, a separate
     * concern already covered by real SDL3's own SDL_SendKeyboardText
     * plumbing) so a synthetic Go-button click (see SQW_TEST_CLICK2_X/Y
     * timed to land on the Go button) exercises the real navigation path
     * end-to-end for local-run testing. */
    {
        const char *tu = getenv("SQW_TEST_GOTO_URL");
        if (tu) { strncpy(st->url_bar_text, tu, sizeof st->url_bar_text - 1); st->url_bar_text[sizeof st->url_bar_text - 1] = 0; }
    }
    st->test_type_text = getenv("SQW_TEST_TYPE_TEXT");
    if (st->test_type_text) st->url_bar_text[0] = 0; /* clean slate so the appended text is clearly visible */

    st->test_click_x = -1; st->test_click_y = -1;
    {
        const char *ex = getenv("SQW_TEST_CLICK_X");
        const char *ey = getenv("SQW_TEST_CLICK_Y");
        if (ex && ey) { st->test_click_x = atoi(ex); st->test_click_y = atoi(ey); }
    }
    st->test_click2_x = -1; st->test_click2_y = -1;
    {
        const char *ex = getenv("SQW_TEST_CLICK2_X");
        const char *ey = getenv("SQW_TEST_CLICK2_Y");
        if (ex && ey) { st->test_click2_x = atoi(ex); st->test_click2_y = atoi(ey); }
    }
    st->test_click3_x = -1; st->test_click3_y = -1;
    {
        const char *ex = getenv("SQW_TEST_CLICK3_X");
        const char *ey = getenv("SQW_TEST_CLICK3_Y");
        if (ex && ey) { st->test_click3_x = atoi(ex); st->test_click3_y = atoi(ey); }
    }
    /* SQW_TEST_KEY: same rationale as SQW_TEST_CLICK_X/Y above -- names one
     * of "pagedown"/"pageup"/"home"/"end"/"up"/"down", pushed as a real
     * SDL_EVENT_KEY_DOWN so it exercises the exact same code path a real
     * keypress does. */
    st->test_key = SDL_SCANCODE_UNKNOWN;
    {
        const char *tk = getenv("SQW_TEST_KEY");
        if (tk) {
            if (!strcmp(tk,"pagedown")) st->test_key = SDL_SCANCODE_PAGEDOWN;
            else if (!strcmp(tk,"pageup")) st->test_key = SDL_SCANCODE_PAGEUP;
            else if (!strcmp(tk,"home")) st->test_key = SDL_SCANCODE_HOME;
            else if (!strcmp(tk,"end")) st->test_key = SDL_SCANCODE_END;
            else if (!strcmp(tk,"up")) st->test_key = SDL_SCANCODE_UP;
            else if (!strcmp(tk,"down")) st->test_key = SDL_SCANCODE_DOWN;
        }
    }
    st->test_screenshot_frame = -1;
    st->test_screenshot_path[0] = 0;
    {
        const char *sf = getenv("SQW_TEST_SCREENSHOT_FRAME");
        if (sf) st->test_screenshot_frame = atoi(sf);
        const char *sp = getenv("SQW_TEST_SCREENSHOT_PATH");
        if (sp) { strncpy(st->test_screenshot_path, sp, sizeof st->test_screenshot_path - 1); st->test_screenshot_path[sizeof st->test_screenshot_path - 1] = 0; }
    }
    if (getenv("SQW_DUMP_BOXES")) {
        int bi;
        for (bi = 0; bi < st->boxes.count; bi++) {
            LayoutBox *db = &st->boxes.boxes[bi];
            fprintf(stderr, "[box] i=%d kind=%d x=%d y=%d w=%d h=%d\n",
                bi, db->kind, (int)db->x, (int)db->y, (int)db->w, (int)db->h);
        }
        fflush(stderr);
    }

    fprintf(stderr, "SQW: entering render loop\n"); fflush(stdout);
    st->running = 1;
    st->frame_count = 0;
    while (st->running) {
        sqw_push_test_events(st);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) sqw_handle_event(st, &ev);
        if (!st->running) break;

        sqw_check_pending_fetch(st);
        /* setTimeout()/setInterval() -- see js_run_timers()'s own comment
         * (js_engine.h) for why this engine needs the HOST render loop to
         * drive its timers at all. Real wall-clock time (SDL_GetTicks(),
         * ms since SDL_Init()), not a frame-count-based approximation --
         * a script's own delay values are real milliseconds. */
        if (g_current_js_interp) {
            int js_relayout = 0;
            js_run_timers(g_current_js_interp, (double)SDL_GetTicks(), &js_relayout);
            if (js_relayout) {
                layout_list_free(&st->boxes);
                layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes);
            }
        }
        {
            int fetch_relayout = 0;
            sqw_poll_js_fetches(&fetch_relayout);
            if (fetch_relayout) {
                layout_list_free(&st->boxes);
                layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes);
            }
        }
        /* A real decoded size becoming known for the first time this poll
         * means every <img> box layout.c sized off the SQW_IMG_SIZE
         * placeholder is now stale -- see sqw_image_cache_poll()'s own
         * comment; a full relayout is cheap enough at this project's scale
         * to just always do it when that happens, same as a window resize
         * already does. */
        if (sqw_image_cache_poll()) {
            layout_list_free(&st->boxes);
            layout_compute(st->root, st->viewport_w, st->viewport_h, &st->boxes);
        }
        sqw_draw_frame(st);
    }

    fprintf(stderr, "SQW: render loop finished, frame_count=%d\n", st->frame_count); fflush(stdout);

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
