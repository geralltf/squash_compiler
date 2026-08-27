/* php_mini.c: a genuinely minimal, single-pass interpreter for a SUBSET of
 * PHP -- enough for simple dynamic test pages (variables, string/number
 * literals, "." concatenation, plus/minus/times/divide arithmetic, echo/print, a basic
 * if/else, the $_GET/$_POST/$_SERVER superglobals, define()/defined()
 * constants, __DIR__/__FILE__, require_once/require/include/include_once,
 * and user-defined functions with parameters and return) embedded in
 * "<?php ... ?>" blocks within an otherwise-literal HTML file. This is
 * STILL NOT a real PHP implementation -- no real arrays beyond the three
 * fixed superglobals, no classes, no loops (foreach/for/while), no
 * "global" keyword, no namespaces -- but it is a real incremental step
 * beyond the original three-superglobals-and-arithmetic subset, aimed at
 * getting further into real-world PHP files (e.g. WordPress's own boot
 * chain under SQW/testpages/wordpress-develop) one feature at a time. See
 * this file's own top-level project memory / conversation history for
 * what's been tried against real WordPress files and where it currently
 * stops -- reaching full WordPress compatibility is a long, multi-session
 * effort, not a single pass.
 *
 * IMPORTANT squash-compiler workaround, applies throughout this file: a
 * real squash codegen bug (confirmed via several minimal standalone
 * repros in the scratchpad, each gcc-compiled control behaving correctly
 * while the squash-compiled build did not) makes comparing a dereferenced
 * POINTER-TYPED STRUCT FIELD accessed through "->" or "." directly
 * against a character literal -- "*st->src == 'X'", "st->src[0] == 'X'",
 * in an if condition, a while condition, anywhere -- unreliable: the
 * comparison can evaluate false even immediately after an adjacent printf
 * of the exact same expression shows the character does match. Reading
 * the value (e.g. into a printf) works fine; WRITING through it
 * (st->src++, st->src = p) works fine; only COMPARING the dereferenced
 * value in place is broken. Wrapping the read in a plain function that
 * returns *st->src does NOT fix it either -- only copying it into a
 * genuine local variable first does. Every function in this file
 * therefore follows one rule: never compare "*st->src" or "st->src[N]"
 * directly -- always assign it to a local char first ("char c =
 * *st->src;" or, for lookahead, a second local), then compare the local.
 * This is a workaround, not a squash compiler fix -- a fix for the root
 * cause (in squash's own codegen.c) may land separately; this workaround
 * should stay in place regardless since it's harmless once the compiler
 * bug is fixed. (A plain local pointer, e.g. "const char *p" scanned via
 * "*p"/"p++", is NOT affected -- only the struct-field-via-arrow/dot form
 * is.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

/* Explicit prototype -- squash's own <stdlib.h> shim doesn't declare
 * realpath(), and letting it fall through to an IMPLICIT declaration
 * (like several other libc calls already do harmlessly elsewhere in
 * this file, e.g. strtok_r/strncasecmp) is unsafe specifically here:
 * confirmed as a real, gcc-vs-squash-DIVERGENT bug -- squash's implicit-
 * declaration handling assumes a 32-bit `int` return for an unprototyped
 * call, silently truncating realpath()'s real 64-bit `char *` return
 * value (gcc's own implicit-declaration warning doesn't do this, so the
 * gcc build worked fine while the squash build always saw a corrupted/
 * NULL pointer). A real prototype in scope removes the ambiguity. */
extern char *realpath(const char *path, char *resolved_path);

/* Same idea, for opendir()/readdir()/closedir() -- squash's shim headers
 * (see the "include/" dir this file's own Makefile points -Iinclude at)
 * have no <dirent.h> at all, so glob() (added session 8, see its own
 * comment at the call site) needs its own explicit prototypes rather
 * than risk the exact same implicit-declaration 64-bit-pointer-
 * truncation bug documented above for realpath() -- opendir()/readdir()
 * both return a pointer (DIR-pointer / dirent-pointer), the identical
 * shape of call realpath() already proved unsafe left implicit under
 * squash.
 * `struct __sqs_dirent` mirrors glibc's real `struct dirent` field-for-
 * field layout on 64-bit Linux (the only platform this project's own
 * Makefile.SQS.linux/sqs_main.c target) -- only `d_name` is actually
 * read here, but the leading fields must still match glibc's real
 * layout byte-for-byte since the OS itself writes this struct's memory
 * directly. DIR itself stays a deliberately-opaque, never-dereferenced
 * type (only ever passed around as a pointer). */
typedef struct __sqs_DIR __sqs_DIR;
struct __sqs_dirent { long d_ino; long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[256]; };
extern __sqs_DIR *opendir(const char *name);
extern struct __sqs_dirent *readdir(__sqs_DIR *dirp);
extern int closedir(__sqs_DIR *dirp);

/* The native database engine -- a genuinely separate, independently
 * compilable/testable C file (SQS/db_engine.c, unit-tested on its own via
 * SQS/tests/db_engine_test.c against both gcc and squash) that knows
 * nothing about PhpObject/PhpKVArray. The __db_* builtins below (in
 * php_call_function()) are the ONLY point of contact between this
 * interpreter and it, and are what turn a sqdb_query() row-text result
 * into real PHP row objects -- that split is what keeps the native
 * engine swappable later without touching the interpreter.
 *
 * squash CAN compile db_engine.c to its own standalone ".sqo" object
 * (`squash -c -linux -64 SQS/db_engine.c -o SQS/db_engine.sqo`, see that
 * target in Makefile.SQS.linux) and db_engine.c is written and tested to
 * work that way -- but linking that .sqo into another program hits a
 * real, pre-existing squash bug in cross-object symbol resolution (the
 * exact same class of bug already documented in Makefile.SQW.linux/
 * Makefile.SDL3.linux's own "sdl_part_video.sqo compiles cleanly but
 * crashes" notes: confirmed here too via a minimal repro -- the merged
 * binary segfaults at process start, before main() even runs). Rather
 * than build SQS/SQS against a currently-broken link path, this
 * `#include`s db_engine.c directly (the same single-translation-unit
 * workaround SQW/SDL3 already use for the identical bug), so it's
 * compiled together with this file rather than merged in as a separate
 * .sqo -- once that linker bug is fixed, swapping this #include for a
 * real .sqo link in Makefile.SQS.linux needs no other change on either
 * side. */
#include "db_engine.h"
#include "db_engine.c"

#define PHP_MAX_VARS 64
#define PHP_VAL_MAX 1024
#define PHP_KV_MAX 32
/* Hard ceiling on how many DISTINCT keys any single PhpKVArray may grow
 * to (see php_kv_ensure_cap/php_kv_free just below PhpKVArray's own
 * definition) -- session 9 found this fixed-32-slot cap was the ACTUAL
 * root cause of WordPress's entire hook system silently no-opping on a
 * real boot: $wp_filter (one global array, one entry per DISTINCT hook
 * NAME) needs 234+ entries just from wp-includes/default-filters.php
 * alone, so the 32nd-and-later "$wp_filter[$hook_name] = new WP_Hook();"
 * call silently dropped (php_kv_add's old "if count >= PHP_KV_MAX
 * return;" guard), leaving that hook's slot permanently unset --
 * every LATER "$wp_filter[$hook_name]->add_filter(...)" for a hook
 * past the 32nd distinct name then ran ->add_filter() on an empty
 * value ([METHOD CALL ON NON-OBJECT] in SQS_TRACE_CALLS), so roughly
 * 500+ of WordPress's own default hooks/actions were simply never
 * registered. PHP_KV_MAX itself is intentionally left at 32 (still used
 * as an unrelated fixed-size LOCAL buffer bound in a couple of places,
 * e.g. the DB-column-name scratch array in the wpdb row-decode path --
 * a table rarely has anywhere near 32 columns) -- this new constant
 * governs PhpKVArray's own dynamic growth instead. See PHP_ARR_MAX's own comment for why PhpKVArray.items became a
 * lazily-`malloc`/`realloc`-grown pointer instead of a fixed embedded
 * PHP_KV_MAX-sized array: growing the FIXED cap itself (instead of
 * making it dynamic) would have multiplied EVERY PhpKVArray in the
 * whole file -- every PhpVar (locals, globals, statics), every object's
 * property table, every one of PHP_ARR_MAX's 8192 array slots -- by the
 * same much-larger constant, even though the overwhelming majority of
 * those never hold anywhere near this many elements; on this project's
 * own dev machine (~600MB of just g_arrays+g_objects static memory
 * already, confirmed via `size` on the built binary) that multiplication
 * would have meant multi-GB of static memory for no real benefit. */
#define PHP_KV_HARD_MAX 4096
#define PHP_OUT_MAX 65536
/* PHP_CONST_MAX/PHP_FUNC_MAX/PHP_INCLUDED_MAX/PHP_BUF_MAX (and, further
 * below, PHP_CLASS_MAX/PHP_CLASS_METHOD_MAX/PHP_OBJ_MAX/PHP_ARR_MAX)
 * were all originally sized much smaller (64) -- fine for this file's
 * own hand-written test pages, but a real request-lifetime table gating
 * silent no-op degradation ("table full: don't register/allocate,
 * degrade safely") is a genuinely different kind of limit once real
 * WordPress source is involved: `require_once`-ing its own bundled
 * sodium_compat polyfill library alone defines enough top-level
 * functions to exhaust a 64-slot table before wp-includes/load.php's
 * OWN functions ever get a turn to register -- confirmed as a real bug
 * this session (every load.php function silently failed to register,
 * making every call to one a silent no-op, with zero output and no
 * error to explain why). This wasn't hit by earlier sessions' testing
 * only because earlier, still-unfixed parsing bugs were themselves
 * silently truncating how much of that same code got scanned in the
 * first place -- fixing those bugs let MORE real code parse correctly,
 * which is what finally exposed this dormant capacity limit. Sized
 * generously below (real WordPress has thousands of functions/many
 * dozens of classes across its full codebase, even though only a
 * subset loads per request) rather than tightly, since the cost is
 * just static memory (not stack, not per-request/per-call -- these are
 * all `static` global tables), and a silently-too-small limit is a much
 * worse failure mode (wrong behavior with no error) than a generous one
 * that's never fully used. */
/* PHP_FUNC_MAX bumped again (1024 -> 8192) this session: confirmed via
 * direct instrumentation (a temporary "table full" fprintf at the
 * php_func_find registration site) that a real front-end WordPress
 * request exhausts even the previous 1024-slot table WAY before
 * wp-includes/pluggable.php's own functions (wp_get_current_user(),
 * get_userdata(), ...) get a turn to register -- over 2200 later
 * top-level function definitions in the boot chain (pluggable.php,
 * pluggable-deprecated.php, and everything wp-settings.php requires
 * after those) silently failed to register as a result, exactly the
 * same silent-degradation failure mode PHP_FUNC_MAX's own original
 * comment above describes. WordPress core alone defines on the order of
 * several thousand top-level (non-method) functions across the files a
 * single front-end page load can reach (wp-includes/*.php), so 1024 was
 * still far too small despite being 16x the original 64. */
#define PHP_CONST_MAX 512
#define PHP_FUNC_MAX 8192
#define PHP_FUNC_PARAM_MAX 8
#define PHP_INCLUDED_MAX 512
#define PHP_BUF_MAX 512
#define PHP_ARG_MAX 8
/* Hard cap on iterations for a single while/for/foreach loop -- a real
 * safety net now that loops actually execute (see php_run_statement's
 * own comment on why), so a genuine infinite loop in the PHP source
 * degrades to "stop iterating" instead of hanging the whole HTTP
 * request forever. Generous: real WordPress loops (option lists, hook
 * arrays, post arrays) are in the hundreds/low thousands of elements at
 * most, never anywhere near this. */
#define PHP_LOOP_MAX 200000
#define PHP_PATH_MAX 512

typedef struct {
    char key[128];
    char val[PHP_VAL_MAX];
} PhpKV;

typedef struct {
    /* Session 9: was a fixed `PhpKV items[PHP_KV_MAX]` (32-slot) embed
     * -- see PHP_KV_HARD_MAX's own comment for the real bug that forced
     * this to become a lazily grown pointer instead (php_kv_ensure_cap
     * allocates/grows on first/further write, php_kv_free releases it).
     * A zero-initialized PhpKVArray (items=NULL, count=0, cap=0) is
     * already a correct, valid empty array -- every read path is
     * bounded by `count`, which stays 0 until the first successful
     * write, so nothing ever dereferences `items` while it's NULL. */
    PhpKV *items;
    int count;
    int cap; /* allocated capacity of `items`, in elements -- 0 until
                the first write actually grows it */
    /* PHP's "array internal pointer" position -- see current()/next()/
     * reset()/end()/key()/prev()'s own comment (session 8) for why this
     * exists: WP_Hook::apply_filters() (the method every add_filter()/
     * add_action() callback actually runs through) iterates its own
     * priority list using exactly these functions, so without tracking
     * a cursor here, EVERY filter/action across all of WordPress
     * silently never fired despite the surrounding object/hook dispatch
     * machinery all working correctly. */
    int cursor;
} PhpKVArray;

/* Grows `arr->items` (realloc, doubling) so it can hold at least `need`
 * elements -- a no-op if it already can. Returns 0 (leaving `arr`
 * untouched) if `need` exceeds PHP_KV_HARD_MAX or realloc itself fails,
 * so every caller's existing "couldn't grow -> degrade, don't crash"
 * convention still applies unchanged. See PHP_KV_HARD_MAX's own comment
 * for why this replaced a fixed-size embedded array. */
static int php_kv_ensure_cap(PhpKVArray *arr, int need) {
    if (need <= arr->cap) return 1;
    if (need > PHP_KV_HARD_MAX) return 0;
    int newcap = arr->cap ? arr->cap * 2 : 8;
    while (newcap < need) newcap *= 2;
    if (newcap > PHP_KV_HARD_MAX) newcap = PHP_KV_HARD_MAX;
    PhpKV *ni = (PhpKV *)realloc(arr->items, (size_t)newcap * sizeof(PhpKV));
    if (!ni) return 0;
    memset(ni + arr->cap, 0, (size_t)(newcap - arr->cap) * sizeof(PhpKV));
    arr->items = ni;
    arr->cap = newcap;
    return 1;
}
/* Releases `arr`'s own backing storage and resets it to a valid empty
 * array -- called once per array/object/global/static SLOT at
 * php_globals_reset() (the request boundary) and once per PhpVar at
 * PhpState teardown (php_call_function/method/static's `callee`, and
 * php_run()'s own top-level `st`), matching the one-malloc-one-free
 * discipline every other request-lifetime allocation in this file
 * already follows (see g_bufs' own comment) -- without this, every
 * array-valued variable's `items` block would leak for the rest of the
 * server process's life instead of just this one request's. */
static void php_kv_free(PhpKVArray *arr) {
    if (arr->items) free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->cap = 0;
    arr->cursor = 0;
}

typedef struct {
    char name[64];
    char val[PHP_VAL_MAX]; /* used when !is_array && !is_object */
    int is_array;
    PhpKVArray arr;        /* used when is_array -- reuses the same
                               fixed-capacity (PHP_KV_MAX) KV table
                               $_GET/$_POST already use, so string AND
                               integer keys are both just stored as their
                               string form ("0", "1", ..., or a real
                               string key like "exit") */
    int next_index;        /* next auto-increment numeric key for a
                               "$arr[] = val;" push */
} PhpVar;

/* See PHP_FUNC_MAX's own comment (top of file) on why these are sized
 * generously -- real WordPress source (just its bundled sodium_compat
 * polyfill library, required indirectly by wp-includes/compat.php)
 * defines 90+ classes and a single one of them (ParagonIE_Sodium_Compat)
 * has 130+ methods; both of the old limits (32, 24) were silently
 * exhausted by that alone. */
#define PHP_CLASS_MAX 128
#define PHP_CLASS_PROP_MAX 32
#define PHP_CLASS_METHOD_MAX 160
#define PHP_CLASS_CONST_MAX 24
/* PHP_OBJ_MAX/PHP_ARR_MAX (see PHP_ARR_MAX's own comment further down)
 * were both 256 until now -- fine while WordPress's own "foreach"/
 * "for"/"while" loops were all parsed-and-skipped no-ops (see
 * php_run_statement's own comment on why they now actually execute),
 * since huge swaths of real array/object-creating code simply never ran.
 * Once loops started really executing, a single real page load through
 * the full wp-load.php->wp-settings.php boot chain (hook/filter
 * registration, translation caches, query building, etc.) exhausted
 * BOTH tables completely (confirmed via direct instrumentation:
 * g_narrays=256/256 AND g_nobjects=256/256 by the time $wpdb->insert()
 * ran) -- with the table silently full, php_array_new()/php_object_new()
 * degrade to returning -1 (this file's standard "don't allocate, don't
 * crash" convention), so a brand new array literal like
 * "array('option_name'=>...)" quietly evaluates to an empty non-array
 * value instead. That's what made $wpdb->insert() itself (already
 * verified correct via a dozen other direct tests) mysteriously fail
 * only once reached through the REAL full boot sequence: __db_insert()
 * received a plain empty string instead of a real array argument.
 * Bumped 4x, matching this project's own established "generous, cost is
 * just static memory" convention (see PHP_FUNC_MAX/PHP_CLASS_MAX's
 * identical story above) -- not unbounded, since each slot embeds a
 * whole ~36KB PhpKVArray, but comfortably past what one real page load
 * needs.
 *
 * Bumped AGAIN this session (1024 -> 4096, both PHP_OBJ_MAX and
 * PHP_ARR_MAX): confirmed via a temporary "table full" fprintf at
 * php_array_new()'s own allocation-failure return that a real front-end
 * WordPress request exhausts the previous 1024-slot array table (400+
 * failed allocations logged before the request even finished) -- these
 * tables are never reclaimed mid-request (see php_array_new's/
 * php_object_new's own "NOT freed/reused until the next
 * php_globals_reset()" comment), and a full boot chain's cumulative
 * array(...)  literals/apply_filters()/get_option() calls add up well
 * past 1024 over the life of one request. Every failed allocation
 * silently degrades a real array value to "" (php_arrref_encode never
 * runs), which is exactly as corrupting as PHP_FUNC_MAX's own silent
 * degradation -- e.g. wp_autoload_values_to_autoload()'s "array( 'yes',
 * 'on', 'auto-on', 'auto' )" literal itself started failing partway
 * through this same request once the table filled, turning a
 * previously-correct "autoload IN (...)" SQL clause back into the
 * "autoload IN ('')" no-match form. PHP_OBJ_MAX bumped the same amount
 * even though it wasn't observed exhausted THIS session, purely to keep
 * the two request-lifetime reference tables' headroom proportional
 * (objects are created by the same kind of loop/filter-heavy code paths
 * arrays are, so it's a matter of when, not if, without this). */
/* Bumped AGAIN this session (4096 -> 16384): confirmed via SQS_TRACE_CALLS
 * that a real front-end request now exhausts the previous 4096-slot
 * ARRAY table nearly 3000 times over (see PHP_ARR_MAX's own updated
 * comment) once "$GLOBALS[...]"/"instanceof" support (this session's
 * other fixes) let WordPress's real WP_Object_Cache actually run for the
 * whole boot chain instead of silently no-opping -- same "each real fix
 * unblocks more real code, which is what finally exhausts the next
 * capacity wall" pattern this file's history keeps hitting. PHP_OBJ_MAX
 * bumped the same proportion to keep headroom matched, per the existing
 * convention (see its own prior comment). */
/* Bumped AGAIN this session (8192 -> 131072): now that PhpKVArray.items
 * is a lazily-grown malloc'd pointer instead of a fixed 32-slot embed
 * (see PHP_KV_HARD_MAX's own comment -- that same session also
 * confirmed via SQS_TRACE_CALLS that a real front-end request exhausts
 * the OLD 8192-slot ARRAY table (see PHP_ARR_MAX below) many tens of
 * thousands of allocations over, once WordPress's own block/pattern/
 * style registries -- WP_Block_Type_Registry::get_instance() and
 * friends, called from deep in the block-editor scaffolding every
 * front-end page loads regardless of whether blocks are even used --
 * actually run for real), a PhpObject slot itself is now tiny (~90
 * bytes: a class name, an alive flag, and a 24-byte PhpKVArray struct
 * that stays that size no matter how many properties the object ends up
 * with) instead of ~37KB, so a MUCH larger table costs comparatively
 * little static memory (131072 * ~90 bytes is single-digit MB) where the
 * OLD embedded-array design would have made the equivalent bump
 * multi-GB. */
#define PHP_OBJ_MAX 131072

typedef struct {
    char name[64];
    char params[PHP_FUNC_PARAM_MAX][64];
    int nparams;
    const char *body; /* see PhpFunc's own comment -- same convention */
} PhpMethod;

typedef struct {
    char name[64];
    char parent_name[64]; /* first "extends" target, if any -- see
                              php_parse_class_decl's own comment; "" if
                              this class has no parent. Used both for
                              real (if shallow) inheritance of methods/
                              properties (php_class_find_method/
                              php_object_new walk this chain) and to
                              resolve "parent::method()" static-call
                              syntax. */
    char prop_names[PHP_CLASS_PROP_MAX][64];
    char prop_defaults[PHP_CLASS_PROP_MAX][PHP_VAL_MAX];
    int nprops;
    char const_names[PHP_CLASS_CONST_MAX][64];
    char const_vals[PHP_CLASS_CONST_MAX][PHP_VAL_MAX];
    int nconsts;
    PhpMethod methods[PHP_CLASS_METHOD_MAX];
    int nmethods;
} PhpClass;

/* A live object instance -- request-lifetime storage (like g_funcs/
 * g_classes), NOT freed/reused until the next php_globals_reset() (i.e.
 * once per HTTP request). No real garbage collection -- fine for a
 * single request's bounded PHP_OBJ_MAX object budget, see this file's
 * top comment on scope throughout. */
typedef struct {
    char class_name[64];
    PhpKVArray props; /* property name -> string value; reuses the same
                          KV machinery arrays/superglobals already use */
    int alive;
} PhpObject;

typedef struct {
    char name[128];
    char val[PHP_VAL_MAX];
} PhpConst;

typedef struct {
    char name[64];
    char params[PHP_FUNC_PARAM_MAX][64];
    int nparams;
    /* Set for the LAST parameter of a "...$name" variadic declaration
     * (real PHP only allows variadic on the final parameter) -- session
     * 8, see php_call_function's own param-binding comment for why:
     * WordPress's own apply_filters($hook_name, $value, ...$args) needs
     * every argument PAST $value collected into a real array named
     * $args, not just the next single positional value. */
    int variadic[PHP_FUNC_PARAM_MAX];
    const char *body; /* points just past the function's opening '{',
                          into a buffer owned by g_bufs (kept alive for
                          the lifetime of the request) */
} PhpFunc;

/* Request-lifetime global tables -- NOT per-PhpState, so a function
 * defined in one included file is callable from any other file included
 * later in the same request, and so a nested php_call_function() (which
 * builds its own PhpState for the callee) still sees every constant and
 * every other function. Reset at the top of every php_run() (i.e. once
 * per HTTP request) by php_globals_reset(). */
static PhpConst g_consts[PHP_CONST_MAX];
static int g_nconsts = 0;
static PhpFunc g_funcs[PHP_FUNC_MAX];
static int g_nfuncs = 0;
/* See php_func_find's own comment (further down, where the actual hash
 * functions live) for why this hash index over g_funcs[] exists --
 * declared here (rather than right next to php_func_find/
 * php_func_hash_insert) only because php_globals_reset() (which needs
 * to clear it every request) is itself defined earlier in this file
 * than those functions are. */
#define PHP_FUNC_HASH_SIZE 16384
static int g_func_hash_head[PHP_FUNC_HASH_SIZE];
static int g_func_hash_next[PHP_FUNC_MAX];
static char g_included[PHP_INCLUDED_MAX][PHP_PATH_MAX];
static int g_nincluded = 0;
static char *g_bufs[PHP_BUF_MAX]; /* malloc'd file contents; function
                                      bodies point into these, so they
                                      must outlive the whole request */
static int g_nbufs = 0;
static PhpClass g_classes[PHP_CLASS_MAX];
static int g_nclasses = 0;
static PhpObject g_objects[PHP_OBJ_MAX];
static int g_nobjects = 0;

/* Nested arrays -- a top-level "$var = array(...)" still stores its
 * elements directly in that PhpVar's OWN embedded PhpKVArray (unchanged
 * from session 3), but an array VALUE stored one level deeper (e.g.
 * "$x[a][b] = 1;", or an object property holding an array, e.g. real
 * WordPress's "$this->callbacks[$priority][$idx] = array(...);" in
 * WP_Hook::add_filter()) needs somewhere to live that isn't a fixed
 * PhpVar struct field. Handled exactly like objects (see
 * php_objref_encode's own comment): a nested array is just another
 * request-lifetime container in a global table, referenced by an
 * ordinary string value with its own magic prefix. */
#define PHP_ARR_MAX 524288 /* see PHP_OBJ_MAX's own updated comment -- bumped 8192 -> 524288 the same session, for the same reason: g_arrays[] slots are now ~24 bytes apiece (a pointer + 3 ints; see PHP_KV_HARD_MAX's own comment) instead of ~37KB, so this whole table is still under 16MB even at half a million slots -- comfortably past the hundreds of thousands of cumulative, never-freed-until-request-end array(...) literals/apply_filters() calls SQS_TRACE_CALLS showed a real front-end WordPress boot needing once block-registry code (WP_Block_Type_Registry et al) started actually running. */
static PhpKVArray g_arrays[PHP_ARR_MAX];
static int g_arr_alive[PHP_ARR_MAX];
static int g_narrays = 0;

/* Error text from the most recent __db_exec()/__db_query() call, read by
 * the $wpdb-compatible class's own last_error handling -- request-
 * lifetime, like every other g_* table on this page, reset alongside
 * them. */
/* Real "global $x;" support: a shared, request-lifetime table of
 * PhpVar's that any function's "global $name;" declaration redirects
 * that name's lookups into for the rest of that function -- see
 * php_var_find()'s own comment. Before this existed, "global $x;" was
 * parsed and silently skipped (still is, for every OTHER not-yet-
 * modeled construct -- see php_run_statement's big skip-list), which is
 * a real, confirmed-impactful gap: WordPress's own require_wp_db() does
 * "global $wpdb; ... $wpdb = new wpdb(...);" -- entirely inside that
 * function's own previously-isolated local scope, so the real wpdb
 * object it built was simply discarded the moment the function
 * returned, and every OTHER function that also does "global $wpdb;"
 * (essentially all of WordPress core) saw an empty, never-connected
 * $wpdb. PHP_MAX_VARS-sized (not request-huge) since only a small,
 * genuinely-shared set of names (wpdb, wp_query, wp_filter, ...) ever
 * gets globalized in practice. */
static PhpVar g_globals[PHP_MAX_VARS];
static int g_nglobals = 0;

/* See php_call_function's own comment (further down, at the actual guard
 * site, near PHP_EXPR_DEPTH_MAX's declaration) for the full story -- a
 * separate depth counter from that expression-nesting one, since that
 * one only bounds a SINGLE expression's own internal nesting (it resets
 * to 0 between distinct top-level statements), not the accumulated
 * depth of a chain of distinct function/method calls recursing into
 * each other, which is what a real, confirmed native-stack-overflow
 * crash this session (WordPress's own mutually-recursive user-
 * bootstrapping functions) actually needs bounded. Declared here (not
 * next to PHP_EXPR_DEPTH_MAX) only because php_globals_reset() is
 * itself defined earlier in this file than that is. */
/* Dialed down from an initial 500 guess after confirming (see the big
 * comment at the glob() builtin's own now-heap-allocated `matches`
 * buffer) that an UNOPTIMIZED build's per-call stack frame here is much
 * larger than a quick estimate suggests -- every local variable
 * declared ANYWHERE in the giant php_call_function()/php_eval_factor()
 * functions gets stack space reserved on every single call, not just
 * the branch actually taken, so even after fixing the single biggest
 * contributor (glob()'s old 128KB stack array) there's still a
 * meaningful amount of "dead" per-frame stack from the many smaller
 * PHP_VAL_MAX-sized buffers scattered across dozens of unreached
 * builtin branches. 150 is a conservative value confirmed (via the same
 * repro that crashed at 500) to stay safely within a 64MB thread stack
 * even for this worst-case real WordPress recursive call chain -- still
 * far deeper than any legitimate WordPress recursion pattern needs. */
#define PHP_CALL_DEPTH_MAX 150
static int g_call_depth = 0;

/* Real function-local "static $x [= init];" support (PHP's per-function-
 * persisted local, e.g. "static $first_init = true;" used as a run-once
 * guard) -- session 8: previously a complete no-op (fell into the
 * generic modifier-keyword skip list alongside "abstract"/"final"/
 * "public"/etc., so the initializer never ran and the variable was
 * permanently empty/falsy on every call, EVERY time, forever). Confirmed
 * as a real, high-impact, previously-undiscovered gap this session: real
 * WordPress's own wp_start_object_cache() (wp-includes/load.php) does
 * "static $first_init = true;" then "if ( $first_init && ... )" -- with
 * $first_init always reading as "" (falsy), that whole branch (which is
 * what actually calls wp_cache_init() the FIRST time) never ran, so
 * $wp_object_cache was NEVER constructed at all; every wp_cache_get()/
 * wp_cache_set() call downstream silently no-op'd on an empty base value
 * for the rest of the request, which in turn meant get_option()/
 * translation-loading/etc. never got any caching benefit and kept
 * recomputing from scratch on every single call -- a real, confirmed
 * (via SQS_TRACE_CALLS showing thousands of repeating ->get()/->add()/
 * file_get_contents() cycles) performance blowup, not just a correctness
 * nit. Modeled the same way g_globals[] models "global $x;" -- a single
 * shared, request-lifetime PhpVar per declaration SITE (not per name:
 * two different functions can each have their own "static $count = 0;"
 * without colliding) -- keyed by the SOURCE POINTER where the "static"
 * keyword itself was read (php_run_statement's own `stmt_start`), which
 * is stable/repeatable across calls since this interpreter always re-
 * parses a function body from the SAME saved source position each call
 * (see this file's own top comment on the "no AST, re-parse from a saved
 * position" architecture) -- so the same textual "static $x = ...;" site
 * always maps to the exact same slot no matter how many times it's
 * reached. */
#define PHP_STATIC_MAX 256
typedef struct {
    const char *site;
    PhpVar var;
    int inited; /* initializer already ran once for this slot -- every
                    LATER visit must skip evaluating the "= init" part
                    entirely (matches real PHP: the initializer is a
                    one-time thing, not re-assigned every call) while
                    still consuming its source text so parsing doesn't
                    desync. */
} PhpStaticSlot;
static PhpStaticSlot g_statics[PHP_STATIC_MAX];
static int g_nstatics = 0;
static PhpVar *php_static_find_or_create(const char *site) {
    int i;
    for (i = 0; i < g_nstatics; i++) if (g_statics[i].site == site) return &g_statics[i].var;
    if (g_nstatics >= PHP_STATIC_MAX) return NULL;
    PhpStaticSlot *s = &g_statics[g_nstatics++];
    memset(s, 0, sizeof *s);
    s->site = site;
    return &s->var;
}
static int php_static_already_inited(const char *site) {
    int i;
    for (i = 0; i < g_nstatics; i++) if (g_statics[i].site == site) return g_statics[i].inited;
    return 0;
}
static void php_static_mark_inited(const char *site) {
    int i;
    for (i = 0; i < g_nstatics; i++) if (g_statics[i].site == site) { g_statics[i].inited = 1; return; }
}

static char g_db_last_error[512] = "";
/* Raw packed text from the most recent successful __db_query() (see
 * db_engine.c's own "PACKED ROW FORMAT" comment) -- cached so
 * __db_get_var()/__db_get_col() can answer "row R, column C" directly
 * without needing PHP-level introspection of an object's property names
 * (this interpreter subset has no get_object_vars()/reflection, and
 * real wpdb's get_var()/get_col() are column-POSITION-based, not
 * name-based, so this is enough). */
static char g_db_last_raw[PHP_OUT_MAX] = "";

static void php_const_set(const char *name, const char *val); /* forward: seeded below, defined later in this file */

/* PHP_VERSION/PHP_OS/PHP_INT_MAX/... -- confirmed completely missing this
 * session via a direct trace against the real wordpress-develop boot
 * chain: wp-includes/load.php's own wp_check_php_mysql_versions() reads
 * PHP_VERSION via version_compare(PHP_VERSION, ...) to enforce WordPress's
 * documented minimum PHP version, and an undefined constant evaluates to
 * "" here (see php_const_find's own NULL-means-undefined contract) --
 * "" always compares as OLDER than any real minimum, so this single
 * missing constant made every request wp_die() with a fake "your PHP
 * version is too old" error instead of ever reaching real page content,
 * regardless of how much of the rest of the interpreter worked. A real,
 * plausible current version (matching the version this project's own PHP
 * subset design targets, not literally the container's own php binary,
 * which may not exist at all) unblocks that check the same safe way
 * MULTISITE/WP_INSTALLING already read as "undefined" until explicitly
 * defined by a real config file. */
static void php_seed_builtin_consts(void) {
    php_const_set("PHP_VERSION", "8.1.27");
    php_const_set("PHP_MAJOR_VERSION", "8");
    php_const_set("PHP_MINOR_VERSION", "1");
    php_const_set("PHP_RELEASE_VERSION", "27");
    php_const_set("PHP_OS", "Linux");
    php_const_set("PHP_OS_FAMILY", "Linux");
    php_const_set("PHP_EOL", "\n");
    php_const_set("PHP_INT_MAX", "9223372036854775807");
    php_const_set("PHP_INT_MIN", "-9223372036854775808");
    php_const_set("PHP_INT_SIZE", "8");
    php_const_set("PHP_SAPI", "cli");
    php_const_set("DIRECTORY_SEPARATOR", "/");
    php_const_set("PATH_SEPARATOR", ":");
}

static void php_globals_reset(void) {
    int i;
    g_nconsts = 0;
    g_nfuncs = 0;
    for (i = 0; i < PHP_FUNC_HASH_SIZE; i++) g_func_hash_head[i] = -1; /* see php_func_hash_insert's own comment */
    g_nincluded = 0;
    for (i = 0; i < g_nbufs; i++) free(g_bufs[i]);
    g_nbufs = 0;
    g_nclasses = 0;
    /* Free every object/array/global/static's own array storage (see
     * PHP_KV_HARD_MAX's own comment -- PhpKVArray.items is now a
     * lazily-grown malloc'd/realloc'd block, not a fixed embed) from
     * the request that just ended, BEFORE zeroing the counts below --
     * these tables are never reclaimed mid-request (only here, at the
     * next request's own php_globals_reset()), so this is the one place
     * that can still see how many of each were actually used. Without
     * this, a long-running server would leak one more `items` block per
     * array-valued object property / array / globalized array / static
     * array on EVERY request, forever. g_objects[]/g_arrays[] are
     * scanned across their whole PHP_OBJ_MAX/PHP_ARR_MAX range (matching
     * the existing "alive = 0" loops just below, which already touch
     * every slot); g_globals[]/g_statics[] only need the prior request's
     * own g_nglobals/g_nstatics count, since slots past that were never
     * touched this request. */
    for (i = 0; i < PHP_OBJ_MAX; i++) { if (g_objects[i].props.items) php_kv_free(&g_objects[i].props); g_objects[i].alive = 0; }
    g_nobjects = 0;
    for (i = 0; i < PHP_ARR_MAX; i++) { if (g_arrays[i].items) php_kv_free(&g_arrays[i]); g_arr_alive[i] = 0; }
    g_narrays = 0;
    for (i = 0; i < g_nglobals; i++) if (g_globals[i].arr.items) php_kv_free(&g_globals[i].arr);
    g_nglobals = 0;
    g_call_depth = 0; /* see its own comment -- symmetric incr/decr should
                          already return this to 0 naturally, reset here
                          too as a defensive per-request baseline. */
    for (i = 0; i < g_nstatics; i++) if (g_statics[i].var.arr.items) php_kv_free(&g_statics[i].var.arr);
    g_nstatics = 0; /* see g_statics' own comment -- request-lifetime like
                        every other table here; the next request reloads
                        its source into a fresh buffer anyway (different
                        pointers), so stale site-pointer slots from a
                        PRIOR request would never match again regardless,
                        but resetting the count also reclaims the slots
                        instead of letting them accumulate forever across
                        a long-running server's many requests. */
    g_db_last_error[0] = 0;
    g_db_last_raw[0] = 0;
    php_seed_builtin_consts();
}

static PhpClass *php_class_find(const char *name) {
    int i;
    for (i = 0; i < g_nclasses; i++) if (strcmp(g_classes[i].name, name) == 0) return &g_classes[i];
    return NULL;
}
/* Walks the "extends" chain (see PhpClass.parent_name's own comment) --
 * a method not found on `cls` itself is looked up on its parent, then
 * its parent's parent, and so on, matching real PHP inheritance for the
 * (common, and the only one this subset needs) single-inheritance case.
 * `guard` bounds the walk against a malformed/cyclic "extends" chain
 * (shouldn't happen with real source, but a bounded loop is cheap
 * insurance against ever hanging). */
static PhpMethod *php_class_find_method(PhpClass *cls, const char *name) {
    int i, guard = 0;
    while (cls && guard++ < 32) {
        for (i = 0; i < cls->nmethods; i++) if (strcmp(cls->methods[i].name, name) == 0) return &cls->methods[i];
        cls = cls->parent_name[0] ? php_class_find(cls->parent_name) : NULL;
    }
    return NULL;
}
/* Same parent-chain walk as php_class_find_method, for a class constant
 * ("ClassName::FOO") -- looked up on `cls` itself first, then up the
 * "extends" chain, matching real PHP constant inheritance. */
static int php_class_find_const(PhpClass *cls, const char *name, char *out, int outcap) {
    int i, guard = 0;
    while (cls && guard++ < 32) {
        for (i = 0; i < cls->nconsts; i++) {
            if (strcmp(cls->const_names[i], name) == 0) {
                strncpy(out, cls->const_vals[i], outcap - 1); out[outcap - 1] = 0;
                return 1;
            }
        }
        cls = cls->parent_name[0] ? php_class_find(cls->parent_name) : NULL;
    }
    return 0;
}

static const char *php_const_find(const char *name) {
    int i;
    for (i = 0; i < g_nconsts; i++) if (strcmp(g_consts[i].name, name) == 0) return g_consts[i].val;
    return NULL;
}
static void php_const_set(const char *name, const char *val) {
    int i;
    for (i = 0; i < g_nconsts; i++) {
        if (strcmp(g_consts[i].name, name) == 0) {
            strncpy(g_consts[i].val, val, sizeof g_consts[i].val - 1);
            g_consts[i].val[sizeof g_consts[i].val - 1] = 0;
            return;
        }
    }
    if (g_nconsts >= PHP_CONST_MAX) return;
    strncpy(g_consts[g_nconsts].name, name, sizeof g_consts[g_nconsts].name - 1);
    g_consts[g_nconsts].name[sizeof g_consts[g_nconsts].name - 1] = 0;
    strncpy(g_consts[g_nconsts].val, val, sizeof g_consts[g_nconsts].val - 1);
    g_consts[g_nconsts].val[sizeof g_consts[g_nconsts].val - 1] = 0;
    g_nconsts++;
}
/* Hash index over g_funcs[], keyed by function name -- session 8, a real
 * PERFORMANCE fix (not a correctness one): php_func_find() used to be a
 * plain O(g_nfuncs) linear strcmp scan, which was tolerable while most
 * of a real WordPress boot's own code paths were silently no-op'd by
 * earlier missing features (do-while, $GLOBALS, instanceof, static
 * locals, foreach-over-a-property-chain -- all fixed earlier this same
 * session). Once those fixes let WordPress's real control flow actually
 * run (apply_filters() genuinely invoking every registered callback,
 * loops genuinely iterating, etc.), the SAME real page load started
 * making vastly more actual function calls against a function table
 * that had grown to several thousand real entries (WordPress core alone
 * registers 2000+ functions) -- an O(g_nfuncs) scan on every single one
 * of those calls compounds into a real, confirmed (via SQS_TRACE_CALLS
 * timestamps showing execution getting dramatically SLOWER, not just
 * slow, as the boot progressed and more functions/calls accumulated)
 * quadratic-ish slowdown, taking the full boot from sub-second to well
 * past a minute. A simple separate-chaining hash table (bucket array of
 * indices into g_funcs[], PHP_FUNC_HASH_SIZE buckets, `next` chain
 * array parallel to g_funcs[] itself) turns every lookup back into an
 * O(1)-ish operation regardless of how many functions are registered. */
static unsigned int php_str_hash(const char *s) {
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}
static void php_func_hash_insert(int idx) {
    unsigned int b = php_str_hash(g_funcs[idx].name) % PHP_FUNC_HASH_SIZE;
    g_func_hash_next[idx] = -1;
    int head = g_func_hash_head[b];
    if (head < 0) { g_func_hash_head[b] = idx; return; }
    /* Append at the tail of this bucket's chain -- preserves "first
     * registered wins" lookup order on a (rare, function_exists()-
     * guarded in practice) duplicate name, matching the old linear
     * scan's own behavior exactly rather than silently flipping it. */
    int cur = head;
    while (g_func_hash_next[cur] >= 0) cur = g_func_hash_next[cur];
    g_func_hash_next[cur] = idx;
}
static PhpFunc *php_func_find(const char *name) {
    unsigned int b = php_str_hash(name) % PHP_FUNC_HASH_SIZE;
    int i = g_func_hash_head[b];
    while (i >= 0) {
        if (strcmp(g_funcs[i].name, name) == 0) return &g_funcs[i];
        i = g_func_hash_next[i];
    }
    return NULL;
}
static int php_was_included(const char *path) {
    int i;
    for (i = 0; i < g_nincluded; i++) if (strcmp(g_included[i], path) == 0) return 1;
    return 0;
}
static void php_mark_included(const char *path) {
    if (g_nincluded >= PHP_INCLUDED_MAX) return;
    strncpy(g_included[g_nincluded], path, PHP_PATH_MAX - 1);
    g_included[g_nincluded][PHP_PATH_MAX - 1] = 0;
    g_nincluded++;
}

typedef struct {
    /* Superglobals -- populated by the caller (sqs_handle_request) before
     * running a script: $_GET from the request's own query string,
     * $_POST from an application/x-www-form-urlencoded request body,
     * $_SERVER['REQUEST_METHOD'] from the real HTTP method. */
    PhpKVArray get;
    PhpKVArray post;
    char server_method[16];

    PhpVar vars[PHP_MAX_VARS];
    int nvars;

    const char *src;   /* current parse position, moves forward as we go */
    char *out;         /* output buffer being built */
    int out_len;
    int out_cap;

    char cur_dir[PHP_PATH_MAX];  /* __DIR__ of the file currently executing */
    char cur_file[PHP_PATH_MAX]; /* __FILE__ of the file currently executing */

    int returning;            /* set by a "return" statement */
    char retval[PHP_VAL_MAX]; /* value passed to "return EXPR;", if any */
    /* Set by "break;"/"continue;" (see their own statement handlers) --
     * unwind exactly like `returning` through php_run_statements()/the
     * "if" handler, but are consumed (reset to 0) by the nearest
     * enclosing while/for/foreach loop handler instead of propagating
     * all the way out of the function. A stray break/continue outside
     * any loop is simply never consumed by anything and is harmless
     * (matches this file's usual "degrade safely" convention). */
    int breaking;
    int continuing;

    int has_this;   /* set on the callee PhpState php_call_function()
                        builds for a "$obj->method(...)" call -- "$this"
                        then reads as an object reference to this id,
                        same representation php_var_find()'s own "is
                        this var an object" check uses everywhere else
                        (see PhpVar's own comment) */
    int this_obj_id;

    /* Names this scope has declared "global $name;" for -- see
     * php_var_find()'s own comment on how this redirects lookups to the
     * shared g_globals[] table instead of this PhpState's own local
     * vars[]. Originally sized 8 on the assumption that a single
     * FUNCTION's own "global $a, $b, ...;" line never lists more than a
     * handful of names -- true for individual functions, but wrong for
     * the TOP-LEVEL scope: wp-settings.php's own top-level "global
     * $wp_version, $wp_db_version, $tinymce_version, $required_php_
     * version, $required_php_extensions, $required_mysql_version,
     * $wp_local_package;" alone is 7 names in ONE statement, and since
     * every top-level require_once shares that SAME PhpState (see
     * php_run_statement's "require_once" handling), later top-level
     * "global $blog_id;" / "global $wpdb;" lines keep accumulating into
     * the same scope's list rather than starting fresh. Confirmed as a
     * real, actively-harmful bug: the 9th name ("wpdb") silently failed
     * the "st->n_globalized < 8" capacity check in the "global" statement
     * handler below, so it was NEVER added to this scope's globalized
     * list -- "global $wpdb;" at wp-settings.php's own top level became a
     * silent no-op, and every later top-level "$wpdb->..." read a plain,
     * never-assigned LOCAL variable instead of the real, correctly-
     * connected object require_wp_db() had built in g_globals[]. Sized to
     * PHP_MAX_VARS now (matches g_globals[]'s own capacity -- no point
     * letting one scope claim more distinct global names than total
     * global slots exist). */
    char globalized[PHP_MAX_VARS][64];
    int n_globalized;

    /* Names this scope has a "static $name [= init];" declaration for,
     * and the specific g_statics[] slot (see its own comment) each one
     * redirects to -- same idea as `globalized` above, just backed by a
     * per-DECLARATION-SITE table instead of a per-NAME one. Sized 8
     * (not PHP_MAX_VARS like `globalized`) since, unlike `global`, real
     * WordPress code never accumulates many distinct "static" locals in
     * one function, let alone across the shared top-level scope the way
     * "global" names do. */
    char staticized[8][64];
    PhpVar *staticized_ptr[8];
    int n_staticized;

    /* The RAW positional arguments this scope's own function/method call
     * was actually invoked with (before any named-parameter binding) --
     * populated once, at call time, by php_call_function/php_call_method
     * -- backing func_get_args()/func_num_args() (session 8; WordPress's
     * own apply_filters() and several other core functions use these to
     * see every argument actually passed, including ones past the last
     * declared parameter). */
    char raw_args[PHP_ARG_MAX][PHP_VAL_MAX];
    int n_raw_args;
} PhpState;

static void php_kv_lookup(PhpKVArray *arr, const char *key, char *out, int outcap) {
    int i;
    for (i = 0; i < arr->count; i++) {
        if (strcmp(arr->items[i].key, key) == 0) {
            strncpy(out, arr->items[i].val, outcap - 1);
            out[outcap - 1] = 0;
            return;
        }
    }
    out[0] = 0;
}

static void php_kv_add(PhpKVArray *arr, const char *key, const char *val) {
    if (arr->count >= PHP_KV_HARD_MAX) return;
    if (!php_kv_ensure_cap(arr, arr->count + 1)) return;
    strncpy(arr->items[arr->count].key, key, sizeof arr->items[arr->count].key - 1);
    arr->items[arr->count].key[sizeof arr->items[arr->count].key - 1] = 0;
    strncpy(arr->items[arr->count].val, val, sizeof arr->items[arr->count].val - 1);
    arr->items[arr->count].val[sizeof arr->items[arr->count].val - 1] = 0;
    arr->count++;
}
/* Like php_kv_add, but updates the value in place if `key` already
 * exists instead of appending a duplicate -- real PHP array-element
 * assignment ("$arr['x'] = 1; $arr['x'] = 2;") overwrites, it doesn't
 * grow the array. php_kv_add itself is left as-is (used by $_GET/$_POST
 * parsing, where "add" is the actually-correct real-request semantics,
 * even for a repeated key). */
static void php_kv_set(PhpKVArray *arr, const char *key, const char *val) {
    int i;
    for (i = 0; i < arr->count; i++) {
        if (strcmp(arr->items[i].key, key) == 0) {
            strncpy(arr->items[i].val, val, sizeof arr->items[i].val - 1);
            arr->items[i].val[sizeof arr->items[i].val - 1] = 0;
            return;
        }
    }
    php_kv_add(arr, key, val);
}

/* An object reference is just an ordinary string value -- like every
 * other value in this file -- with a magic prefix ("\x01O:" -- \x01 is
 * not a byte real PHP source or form input has any legitimate reason to
 * contain) followed by its g_objects[] index. Storing it this way,
 * rather than adding an "is_object" flag/id pair to PhpVar the way
 * arrays got (is_array/arr), means "$x = new Foo();" is just an
 * ordinary php_var_set() -- no special-casing needed at assignment,
 * array-element-storage, function-return, or (crucially) PROPERTY-VALUE
 * storage time, since PhpObject's own props are themselves a
 * PhpKVArray of plain strings. That last part is what makes arbitrary
 * chaining ("$a->b->c", "$arr[key]->method()", a property that itself
 * holds another object) fall out for free: whatever produced the
 * string, decoding it with php_objref_decode() at the next "->" is
 * always enough to find the right object, with no extra bookkeeping. */
#define PHP_OBJREF_PREFIX "\x01O:"
static void php_objref_encode(int obj_id, char *out, int outcap) {
    snprintf(out, outcap, "%s%d", PHP_OBJREF_PREFIX, obj_id);
}
/* Returns the object id, or -1 if `s` isn't an object-reference string
 * (a plain scalar, or an object that's since been reset via
 * php_globals_reset() and no longer "alive" -- see php_object_new's own
 * comment on this table's lifetime). */
static int php_objref_decode(const char *s) {
    if (strncmp(s, PHP_OBJREF_PREFIX, 3) != 0) return -1;
    int id = atoi(s + 3);
    if (id < 0 || id >= PHP_OBJ_MAX || !g_objects[id].alive) return -1;
    return id;
}

/* Same idea as PHP_OBJREF_PREFIX/php_objref_encode/php_objref_decode,
 * for a NESTED array container instead of an object -- see g_arrays'
 * own comment. A different prefix byte (still \x01, but 'A' not 'O')
 * keeps the two kinds of reference distinguishable, though nothing in
 * this file currently needs to tell them apart by inspecting the string
 * itself (a "[key]" access always calls php_arrref_decode and a
 * "->member" access always calls php_objref_decode -- the SYNTAX at the
 * use site already says which one is meant). */
/* "\x01" "A:" -- NOT "\x01A:" -- is deliberate: in C, a "\x" hex escape
 * is greedy and consumes every following hex-digit character ('A' IS a
 * valid hex digit, unlike PHP_OBJREF_PREFIX's 'O'), so "\x01A:" is
 * actually the SINGLE byte 0x1A followed by ':' -- not the two bytes
 * 0x01, 'A' this was supposed to be. Confirmed as a real bug this
 * session (via a hex dump of the decoded string -- it visually LOOKED
 * like the prefix was simply missing its 'A', since 0x1A is a
 * different-looking control byte, not because the byte count was off).
 * Splitting into two adjacent string literals forces the hex escape to
 * end at "\x01", with "A:" then just ordinary literal characters --
 * standard C adjacent-string-literal concatenation joins them back into
 * one string at compile time either way. */
#define PHP_ARRREF_PREFIX "\x01" "A:"
static void php_arrref_encode(int arr_id, char *out, int outcap) {
    snprintf(out, outcap, "%s%d", PHP_ARRREF_PREFIX, arr_id);
}
static int php_arrref_decode(const char *s) {
    if (strncmp(s, PHP_ARRREF_PREFIX, 3) != 0) return -1;
    int id = atoi(s + 3);
    if (id < 0 || id >= PHP_ARR_MAX || !g_arr_alive[id]) return -1;
    return id;
}
/* Allocates a fresh, empty nested-array container. Returns its id
 * (index into g_arrays), or -1 if the table is full. */
static int php_array_new(void) {
    /* Was a linear "for (i=0..PHP_ARR_MAX) if (!g_arr_alive[i])" scan
     * from slot 0 on EVERY call -- correct (g_arr_alive[] slots are
     * never cleared again until the next php_globals_reset(), so the
     * scan always did land on g_narrays itself), but O(current table
     * size) per allocation, so a whole request's worth of N array
     * allocations cost O(N^2) total scanning. Harmless back when
     * PHP_ARR_MAX was small, but a real, confirmed (via timing this
     * session, once WordPress's block/pattern/style registry code
     * started actually running and needing hundreds of thousands of
     * cumulative array allocations over one request) quadratic-blowup
     * bottleneck -- since slots are allocated strictly in order and
     * never freed mid-request, g_narrays (the existing "high water
     * mark") already, exactly, IS the next free slot; no scan needed at
     * all. */
    if (g_narrays >= PHP_ARR_MAX) {
        if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[ARRAY TABLE FULL] PHP_ARR_MAX=%d exhausted\n", PHP_ARR_MAX);
        return -1;
    }
    int slot = g_narrays++;
    memset(&g_arrays[slot], 0, sizeof g_arrays[slot]);
    g_arr_alive[slot] = 1;
    return slot;
}

/* Allocates a fresh object of class `class_name`, seeding its properties
 * from that class's own declared defaults (real PHP semantics: an
 * object starts with each property at its class-declared default, not
 * empty). Returns the new object's id (index into g_objects), or -1 if
 * the class is unknown or the object table is full. */
static int php_object_new(const char *class_name) {
    /* See php_array_new's own comment -- same fix, same reason: slots
     * are allocated strictly in order and never freed mid-request, so
     * g_nobjects already IS the next free slot; no O(table size) scan
     * needed. */
    if (g_nobjects >= PHP_OBJ_MAX) {
        if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[OBJECT TABLE FULL] PHP_OBJ_MAX=%d exhausted\n", PHP_OBJ_MAX);
        return -1;
    }
    int slot = g_nobjects++;
    PhpObject *o = &g_objects[slot];
    memset(o, 0, sizeof *o);
    o->alive = 1;
    strncpy(o->class_name, class_name, sizeof o->class_name - 1); o->class_name[sizeof o->class_name - 1] = 0;
    /* Seed declared-property defaults from the whole "extends" chain
     * (see PhpClass.parent_name's own comment), ROOT-most parent first
     * so a child class's own re-declaration of the same property name
     * (real PHP allows overriding a default) wins via the later
     * php_kv_set() call -- matches real PHP's "new" semantics where an
     * object starts with every inherited AND own property already set,
     * not just its own class's. */
    {
        PhpClass *chain[32];
        int nchain = 0;
        PhpClass *walk = php_class_find(class_name);
        while (walk && nchain < 32) {
            chain[nchain++] = walk;
            walk = walk->parent_name[0] ? php_class_find(walk->parent_name) : NULL;
        }
        int ci, i;
        for (ci = nchain - 1; ci >= 0; ci--) {
            PhpClass *cls = chain[ci];
            for (i = 0; i < cls->nprops; i++) php_kv_set(&o->props, cls->prop_names[i], cls->prop_defaults[i]);
        }
    }
    return slot;
}

/* Decodes a application/x-www-form-urlencoded (or URL query) string
 * ("a=1&b=hello+world&c=%2Fx") into key/value pairs -- '+' as space and
 * "%XX" hex escapes, the two encodings real form submissions and query
 * strings actually use. Shared by $_GET (from the request path's own
 * query string) and $_POST (from the request body) parsing. Operates on
 * plain local pointers/array indices throughout, not a struct field --
 * not subject to the squash comparison bug described at the top of this
 * file, kept as-is. */
static int php_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void php_urldecode(const char *in, int inlen, char *out, int outcap) {
    int i = 0, o = 0;
    while (i < inlen && o < outcap - 1) {
        if (in[i] == '+') { out[o++] = ' '; i++; }
        else if (in[i] == '%' && i + 2 < inlen) {
            out[o++] = (char)((php_hex_val(in[i+1]) << 4) | php_hex_val(in[i+2]));
            i += 3;
        } else { out[o++] = in[i++]; }
    }
    out[o] = 0;
}
static void php_parse_kv_string(const char *qs, PhpKVArray *arr) {
    const char *p = qs;
    while (*p) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        if (!amp) amp = p + strlen(p);
        char key[128], val[PHP_VAL_MAX];
        if (eq && eq < amp) {
            php_urldecode(p, (int)(eq - p), key, sizeof key);
            php_urldecode(eq + 1, (int)(amp - (eq + 1)), val, sizeof val);
        } else {
            php_urldecode(p, (int)(amp - p), key, sizeof key);
            val[0] = 0;
        }
        if (key[0]) php_kv_add(arr, key, val);
        p = (*amp) ? amp + 1 : amp;
    }
}

static int php_name_is_globalized(PhpState *st, const char *name) {
    int i;
    for (i = 0; i < st->n_globalized; i++) if (strcmp(st->globalized[i], name) == 0) return 1;
    return 0;
}
static PhpVar *php_global_find(const char *name) {
    int i;
    for (i = 0; i < g_nglobals; i++) if (strcmp(g_globals[i].name, name) == 0) return &g_globals[i];
    return NULL;
}
static PhpVar *php_global_find_or_create(const char *name) {
    PhpVar *v = php_global_find(name);
    if (v) return v;
    if (g_nglobals >= PHP_MAX_VARS) return NULL;
    v = &g_globals[g_nglobals++];
    memset(v, 0, sizeof *v);
    strncpy(v->name, name, sizeof v->name - 1); v->name[sizeof v->name - 1] = 0;
    return v;
}
/* `name`'s storage -- ordinarily this scope's OWN st->vars[], but if
 * this scope ran "global $name;" (see php_run_statement's real handling
 * of it now), every read/write instead redirects to the single shared
 * g_globals[] slot for that name, exactly matching real PHP's semantics
 * for a globalized variable (every function that globalizes the same
 * name shares the one true value, which is what makes WordPress's own
 * "global $wpdb;"-in-every-function convention work at all). See
 * g_globals' own comment for why this exists. */
static PhpVar *php_staticized_find(PhpState *st, const char *name) {
    int i;
    for (i = 0; i < st->n_staticized; i++) if (strcmp(st->staticized[i], name) == 0) return st->staticized_ptr[i];
    return NULL;
}
static PhpVar *php_var_find(PhpState *st, const char *name) {
    int i;
    if (php_name_is_globalized(st, name)) return php_global_find(name);
    { PhpVar *sv = php_staticized_find(st, name); if (sv) return sv; }
    for (i = 0; i < st->nvars; i++) if (strcmp(st->vars[i].name, name) == 0) return &st->vars[i];
    return NULL;
}
/* Finds `name`, creating a fresh (scalar, empty-string, non-array) var
 * for it if it doesn't exist yet -- used by array-element assignment
 * ("$arr['x'] = 1;" on a not-yet-seen $arr) so it doesn't need its own
 * separate create-if-missing logic. */
static PhpVar *php_var_find_or_create(PhpState *st, const char *name) {
    if (php_name_is_globalized(st, name)) return php_global_find_or_create(name);
    PhpVar *v = php_var_find(st, name);
    if (v) return v;
    if (st->nvars >= PHP_MAX_VARS) return NULL;
    v = &st->vars[st->nvars++];
    memset(v, 0, sizeof *v);
    strncpy(v->name, name, sizeof v->name - 1); v->name[sizeof v->name - 1] = 0;
    return v;
}
static void php_var_set(PhpState *st, const char *name, const char *val) {
    PhpVar *v = php_var_find_or_create(st, name);
    if (!v) return;
    /* If this var previously held its OWN array content (v->arr, the
     * PhpVar-embedded PhpKVArray -- see PHP_KV_HARD_MAX's own comment on
     * why that's now a lazily-grown malloc'd block, not a fixed embed),
     * a plain scalar reassignment here would otherwise orphan that
     * block: `is_array` flips to 0 below, so nothing else would ever
     * find or free it again for the rest of this request. Free it now,
     * before it's unreachable. Cheap no-op (php_kv_free on an
     * already-empty/never-allocated array) for the overwhelmingly
     * common case of a var that was never an array to begin with. */
    if (v->is_array) php_kv_free(&v->arr);
    v->is_array = 0; /* reassigning a plain "$x = expr;" always makes it
                         scalar again, even if it used to hold an array
                         (matches real PHP: a variable's "type" is just
                         whatever its last assignment made it) */
    strncpy(v->val, val, sizeof v->val - 1); v->val[sizeof v->val - 1] = 0;
}

/* Real PHP truthiness rules: an ARRAY is falsy iff it has zero elements
 * (regardless of what its elements/keys actually contain), an OBJECT is
 * ALWAYS truthy (even with no properties), everything else follows the
 * usual "" / "0" scalar rule. Before this checked array/object-ness at
 * all, EVERY array or object reference (see php_arrref_encode's/
 * php_objref_encode's own comments -- a nested/returned array or an
 * object is always represented as a short, non-empty, non-"0" TOKEN
 * string like "\x01A:7", never the array's own contents) was
 * unconditionally truthy no matter how many elements it actually held --
 * a real, broadly-impactful bug: any "if (!$results)"/"if ($array)"-
 * shaped check (extremely common throughout WordPress, e.g.
 * wp_load_alloptions()'s own "if ( ! $alloptions_db ) { <fall back to a
 * broader query> }") always took the "truthy"/"has results" branch even
 * when the underlying query/array genuinely had zero rows/elements. */
static int php_truthy(const char *s) {
    int aid = php_arrref_decode(s);
    if (aid >= 0) return g_arrays[aid].count > 0;
    if (php_objref_decode(s) >= 0) return 1;
    return s[0] != 0 && strcmp(s, "0") != 0;
}

static void php_emit(PhpState *st, const char *s, int len) {
    int room = st->out_cap - st->out_len - 1;
    if (len > room) len = room;
    if (len > 0) { memcpy(st->out + st->out_len, s, (size_t)len); st->out_len += len; st->out[st->out_len] = 0; }
}
static void php_emit_str(PhpState *st, const char *s) { php_emit(st, s, (int)strlen(s)); }

/* Skips whitespace AND comments ("//...", "#...", "/*...*​/") between
 * tokens -- real PHP treats both exactly like whitespace anywhere a
 * token boundary can occur, so this is called everywhere a plain
 * whitespace-skip would otherwise be. Without genuine comment handling
 * here, the only fallback left (php_run_statement's own "unrecognized
 * character: skip one and retry" path, for whatever a comment's FIRST
 * character happens to be) only ever eats the comment's opening
 * delimiter one byte at a time -- the moment it lands on a letter inside
 * the comment text, that word gets parsed as a real identifier/statement
 * start, corrupting everything after it. (Confirmed as a real bug this
 * way, not a squash-compiler issue -- a gcc-compiled build of this same
 * pre-fix code produced the identical wrong output.) Scans on a local
 * `p` (a PLAIN pointer, not a struct field -- see this file's top
 * comment), writes back to st->src once at the end. */
static void php_skip_ws(PhpState *st) {
    const char *p = st->src;
    for (;;) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p++; continue; }
        if (c == '#') {
            p++;
            char c2 = *p;
            while (c2 && c2 != '\n') { p++; c2 = *p; }
            continue;
        }
        if (!c) break; /* true end of source -- MUST check before reading
                           p[1] below: this function used to read it
                           unconditionally, a real heap-buffer-overflow
                           READ one byte past the end of every source
                           buffer whenever whitespace-skipping landed
                           exactly on the terminating NUL (extremely
                           common -- found via fuzzing php_run() directly
                           this session, ~13% of random mutated inputs
                           triggered it; this is the interpreter's own
                           core tokenizer helper, called from nearly
                           every parsing function in this file, so this
                           was about as reachable as a bug gets). */
        char c1 = p[1];
        if (c == '/' && c1 == '/') {
            p += 2;
            char c2 = *p;
            while (c2 && c2 != '\n') { p++; c2 = *p; }
            continue;
        }
        if (c == '/' && c1 == '*') {
            p += 2;
            char c2 = *p, c3 = p[1];
            while (c2 && !(c2 == '*' && c3 == '/')) { p++; c2 = *p; c3 = p[1]; }
            if (c2) p += 2; /* consume the closing "*​/" */
            continue;
        }
        break;
    }
    st->src = p;
}

/* Reads a bare identifier (variable name after '$', or a keyword like
 * "echo"/"if", or a function/constant name). */
static void php_read_ident(PhpState *st, char *buf, int bufcap) {
    const char *p = st->src;
    int i = 0;
    char c = *p;
    while ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
        if (i < bufcap - 1) buf[i++] = c;
        p++;
        c = *p;
    }
    buf[i] = 0;
    st->src = p;
}

static void php_eval_expr(PhpState *st, char *out, int outcap); /* forward: "{$expr}" string interpolation below needs this */

/* Reads a single-or-double-quoted string literal (the opening quote is
 * already known to be at *st->src). Only supports \\, \", \', \n, \t
 * escapes inside double quotes -- single-quoted strings are literal
 * (real PHP's own distinction, kept here too).
 *
 * Double-quoted strings ALSO interpolate variables, real PHP's own two
 * forms: "simple syntax" ($name, one level of $name[key] or
 * $name->prop -- no quotes needed on a bare-word array key, no deeper
 * chaining) and "complex syntax" ({$anyExpression}, which can be
 * arbitrarily deep -- "{$wpdb->options}", "{$row->option_name}", etc).
 * Before this existed, EVERY "$var"/"{$expr}" inside a double-quoted
 * string was emitted completely literally -- confirmed as an enormous,
 * previously-invisible gap once real WordPress SQL queries started
 * actually reaching this engine (see db_engine.c/php_mini.c's own
 * __db_* builtins): essentially every WordPress core query is built as
 * "...FROM $wpdb->options..."-shaped double-quoted strings, so every
 * single one of them was sending the LITERAL text "$wpdb->options" as
 * a table name instead of the real "wp_options", failing outright
 * ("sqdb: malformed SELECT" -- $ isn't a valid identifier character).
 * This one gap alone meant no real WordPress database query could ever
 * have worked, regardless of how correct the rest of the DB layer is. */
static void php_read_string_lit(PhpState *st, char *buf, int bufcap) {
    const char *p = st->src;
    char q = *p; p++;
    int i = 0;
    char c = *p;
    while (c && c != q) {
        if (q == '"' && c == '\\' && p[1]) {
            p++;
            char e = *p;
            /* Real PHP only treats a fixed, small set of characters after
             * "\" as a real escape inside a double-quoted string ("\n",
             * "\t", "\\", "\$", "\"", a few others this subset doesn't
             * bother with like "\r"/"\v"/"\0"/"\xNN"/"\uNNNN") -- for
             * anything else, the backslash is kept LITERALLY (e.g. "\s"
             * stays as the two characters '\' and 's', not just 's').
             * This mattered for real regex patterns written as double-
             * quoted strings ("\d+", "\s+", ...): silently DROPPING the
             * backslash on any unrecognized escape (this file's old,
             * unconditional "else c = e;") corrupted every such pattern
             * before it ever reached preg_match()/preg_replace(). */
            if (e == 'n') { c = '\n'; p++; }
            else if (e == 't') { c = '\t'; p++; }
            else if (e == 'r') { c = '\r'; p++; }
            else if (e == '\\' || e == '"' || e == '$') { c = e; p++; }
            else {
                if (i < bufcap - 1) buf[i++] = '\\';
                c = e; p++;
            }
            if (i < bufcap - 1) buf[i++] = c;
            c = *p;
            continue;
        }
        if (q == '"' && c == '{' && p[1] == '$') {
            /* Complex syntax: "{$anyExpression}" -- st->src/php_eval_expr
             * need to take over from here, so sync `p` into st->src
             * first and resync back into `p` afterward (this function
             * otherwise walks a plain local pointer, see this file's top
             * comment on why). */
            st->src = p + 1; /* position at the '$' */
            char val[PHP_VAL_MAX];
            php_eval_expr(st, val, sizeof val);
            php_skip_ws(st);
            if (*st->src == '}') st->src++;
            const char *vp = val;
            while (*vp && i < bufcap - 1) buf[i++] = *vp++;
            p = st->src;
            c = *p;
            continue;
        }
        if (q == '"' && c == '$' && (p[1] == '_' || (p[1] >= 'a' && p[1] <= 'z') || (p[1] >= 'A' && p[1] <= 'Z'))) {
            /* Simple syntax: "$name", "$name[key]" (bare-word key, no
             * quotes -- real PHP's own rule for THIS form specifically),
             * or "$name->prop" (one level only -- "$a->b->c" inside a
             * plain simple-syntax interpolation stops after "->b" in
             * real PHP too; deeper chains need {$a->b->c}). */
            st->src = p + 1; /* skip '$' */
            char name[64];
            php_read_ident(st, name, sizeof name);
            PhpVar *v = php_var_find(st, name);
            char cur[PHP_VAL_MAX];
            if (v) { strncpy(cur, v->val, sizeof cur - 1); cur[sizeof cur - 1] = 0; } else cur[0] = 0;
            if (*st->src == '[') {
                st->src++;
                char key[128]; int ki = 0;
                if (*st->src == '$') {
                    st->src++;
                    char idxname[64]; php_read_ident(st, idxname, sizeof idxname);
                    PhpVar *iv = php_var_find(st, idxname);
                    if (iv) { strncpy(key, iv->val, sizeof key - 1); key[sizeof key - 1] = 0; } else key[0] = 0;
                } else {
                    while (*st->src && *st->src != ']' && ki < (int)sizeof key - 1) { key[ki++] = *st->src; st->src++; }
                    key[ki] = 0;
                }
                if (*st->src == ']') st->src++;
                if (v && v->is_array) php_kv_lookup(&v->arr, key, cur, sizeof cur);
                else {
                    int aid = v ? php_arrref_decode(v->val) : -1;
                    if (aid >= 0) php_kv_lookup(&g_arrays[aid], key, cur, sizeof cur);
                    else cur[0] = 0;
                }
            } else if (st->src[0] == '-' && st->src[1] == '>' &&
                       (st->src[2] == '_' || (st->src[2] >= 'a' && st->src[2] <= 'z') || (st->src[2] >= 'A' && st->src[2] <= 'Z'))) {
                st->src += 2;
                char member[64]; php_read_ident(st, member, sizeof member);
                int oid = v ? php_objref_decode(v->val) : -1;
                if (oid >= 0) php_kv_lookup(&g_objects[oid].props, member, cur, sizeof cur);
                else cur[0] = 0;
            }
            const char *vp = cur;
            while (*vp && i < bufcap - 1) buf[i++] = *vp++;
            p = st->src;
            c = *p;
            continue;
        }
        if (i < bufcap - 1) buf[i++] = c;
        p++;
        c = *p;
    }
    buf[i] = 0;
    if (c == q) p++;
    st->src = p;
}

static double php_to_num(const char *s) { return atof(s); }
static void php_num_to_str(double d, char *buf, int bufcap) {
    if (d == (long long)d) snprintf(buf, bufcap, "%lld", (long long)d);
    else snprintf(buf, bufcap, "%g", d);
}

/* Minimal sprintf()-alike: supports "%s", "%d", plain "%%", and PHP's
 * positional "%1$s" / "%2$d" form (used constantly throughout real
 * WordPress source, e.g. sprintf('%1$s requires %2$s', $a, $b)). Nothing
 * fancier (no width/precision/padding) -- deliberately bounded, see this
 * file's top comment. `all_args`/`arg_base`/`nvalues` (rather than a
 * pre-shifted "args + 1" pointer, and the caller passing a plain
 * "args + 1") deliberately avoid pointer arithmetic on a char** value: a
 * real squash codegen bug (confirmed via a minimal standalone repro,
 * gcc-compiled control behaving correctly while the squash-compiled
 * build segfaulted) makes "somePtrPtr + N" -- pointer arithmetic on a
 * char**, whether a parameter or a plain local -- compute a wrong
 * (1-byte-stride, not 8-byte-stride) offset. A fix may land in squash's
 * own codegen.c separately; passing an explicit base index instead sidesteps
 * it regardless. */
static void php_sprintf(const char *fmt, char **all_args, int arg_base, int nvalues, char *out, int outcap) {
    const char *p = fmt;
    int o = 0;
    int next_arg = 0;
    char c = *p;
    while (c && o < outcap - 1) {
        if (c != '%') { out[o++] = c; p++; c = *p; continue; }
        p++; /* consume '%' */
        c = *p;
        if (c == '%') { out[o++] = '%'; p++; c = *p; continue; }
        /* Optional positional prefix: digits followed by '$'. */
        int pos = -1;
        {
            const char *dp = p;
            int val = 0;
            char dc = *dp;
            int had_digit = 0;
            while (dc >= '0' && dc <= '9') { val = val * 10 + (dc - '0'); dp++; dc = *dp; had_digit = 1; }
            if (had_digit && dc == '$') { pos = val - 1; p = dp + 1; c = *p; }
        }
        /* Conversion specifier (only 's' and 'd' understood). */
        int use_idx = (pos >= 0) ? pos : next_arg;
        const char *val_str = (use_idx >= 0 && use_idx < nvalues) ? all_args[arg_base + use_idx] : "";
        if (c == 's') {
            int len = (int)strlen(val_str);
            int room = outcap - 1 - o;
            if (len > room) len = room;
            if (len > 0) { memcpy(out + o, val_str, (size_t)len); o += len; }
            p++; c = *p;
        } else if (c == 'd') {
            char numbuf[64];
            snprintf(numbuf, sizeof numbuf, "%lld", (long long)php_to_num(val_str));
            int len = (int)strlen(numbuf);
            int room = outcap - 1 - o;
            if (len > room) len = room;
            if (len > 0) { memcpy(out + o, numbuf, (size_t)len); o += len; }
            p++; c = *p;
        } else {
            /* Unknown specifier: emit literally so malformed formats
             * degrade instead of vanishing. */
            out[o++] = '%';
            continue;
        }
        if (pos < 0) next_arg++;
    }
    out[o] = 0;
}

static void php_eval_expr(PhpState *st, char *out, int outcap);
static void php_call_function(PhpState *caller, const char *name, char **args, int nargs, char *out, int outcap);
static void php_call_method(PhpState *caller, int obj_id, PhpMethod *m, char **args, int nargs, char *out, int outcap);
static void php_call_static(PhpState *caller, PhpMethod *m, char **args, int nargs, char *out, int outcap);
static void php_skip_to_paren_close(PhpState *st);

/* Parses a parenthesized, comma-separated argument list: "(" already NOT
 * yet consumed -- st->src is at '(' on entry. Fills args[]/*, returns
 * nargs. Bounded to PHP_ARG_MAX arguments (extra ones are evaluated, for
 * side effects, but not kept). `args` is an array of PHP_ARG_MAX
 * caller-owned char* buffers, each PHP_VAL_MAX bytes -- deliberately a
 * plain array-of-pointers (not a 2D "char[][PHP_VAL_MAX]" parameter): a
 * real squash codegen bug (confirmed via a minimal standalone repro,
 * gcc-compiled control behaving correctly while the squash-compiled
 * build segfaulted) makes a 2D char array passed as a function
 * parameter and indexed inside the callee crash. A fix may land in
 * squash's own codegen.c separately; this array-of-pointers shape sidesteps
 * it regardless and is a perfectly normal C pattern on its own. */
static int php_parse_args(PhpState *st, char **args) {
    int n = 0;
    st->src++; /* consume '(' */
    php_skip_ws(st);
    char c = *st->src;
    if (c == ')') { st->src++; return 0; }
    for (;;) {
        char tmp[PHP_VAL_MAX];
        php_eval_expr(st, tmp, sizeof tmp);
        if (n < PHP_ARG_MAX) { strncpy(args[n], tmp, PHP_VAL_MAX - 1); args[n][PHP_VAL_MAX - 1] = 0; n++; }
        php_skip_ws(st);
        c = *st->src;
        if (c == ',') { st->src++; php_skip_ws(st); continue; }
        break;
    }
    c = *st->src;
    if (c == ')') st->src++;
    return n;
}

/* Like php_kv_lookup, but reports whether `key` exists at all rather
 * than just returning "" either way -- needed for isset()/empty(),
 * which (per real PHP semantics) must distinguish "key absent" from
 * "key present with an empty value". */
static int php_kv_has(PhpKVArray *arr, const char *key, char *out, int outcap) {
    int i;
    for (i = 0; i < arr->count; i++) {
        if (strcmp(arr->items[i].key, key) == 0) {
            strncpy(out, arr->items[i].val, outcap - 1);
            out[outcap - 1] = 0;
            return 1;
        }
    }
    return 0;
}

/* A resolved "$name" or "$name[key]" reference, WITHOUT stringifying
 * whatever it points to -- isset()/empty()/count()/is_array() all need
 * this (they must look at a variable's real existence/array-ness/
 * element-count directly, not the flattened string value every other
 * expression construct in this file deals in). `var` is NULL if the
 * bare variable itself was never assigned. */
typedef struct {
    PhpVar *var;
    int has_key;
    char key[128];
    /* "BASE->member" instead of "BASE"/"BASE[key]" -- e.g. "$wpdb->error",
     * "$post->ID". A real, common shape (see this struct's own comment
     * on why it's here) that the OLD version of this struct/function
     * couldn't represent at all -- callers had no way to tell "this was
     * a ->member access" from "this was a bare, unset variable", so
     * isset()/empty()/count()/is_array() on one gave the WRONG answer
     * (checking the BASE object's own truthiness instead of the
     * member's) rather than just an incomplete one. Only ONE level of
     * "->member" is resolved (not "$a->b->c") -- good enough for the
     * real call shapes this was found against. */
    int has_member;
    int obj_id;   /* decoded object id for the has_member case, -1 if the base isn't a live object */
    char member[64];
} PhpVarRef;

/* Parses a bare "$name", "$name[keyExpr]", or "$name->member" (also
 * "$this->member") starting at the current position (skipping leading
 * whitespace first) -- does NOT consume a trailing ')' or ',', callers
 * handle that themselves the same way php_parse_args' own callers do. If
 * the current position isn't a "$", `ref->var` is left NULL and nothing
 * is consumed (a non-variable argument to isset()/count()/etc. isn't
 * meaningful in real PHP either, so this subset just treats it as
 * "doesn't exist" rather than actually evaluating it as a general
 * expression). Real WordPress code routinely calls isset()/empty() on a
 * "->member" access (e.g. wp-includes/load.php's "empty($wpdb->error)")
 * -- confirmed as a real bug this session when that shape wasn't
 * recognized at all: the caller's own resync-to-')' logic kept parsing
 * from going off the rails, but "$wpdb->error" still evaluated based on
 * $wpdb's OWN truthiness (always true, since $wpdb holds a valid object)
 * rather than the actual (unset, so falsy) property, misfiring
 * wp_set_wpdb_vars()'s "if (!empty($wpdb->error)) dead_db();" gate. */
static void php_resolve_varref(PhpState *st, PhpVarRef *ref) {
    memset(ref, 0, sizeof *ref);
    ref->obj_id = -1;
    php_skip_ws(st);
    char c = *st->src;
    if (c != '$') return;
    st->src++;
    char name[64];
    php_read_ident(st, name, sizeof name);
    char base_val[PHP_VAL_MAX]; base_val[0] = 0;
    if (strcmp(name, "this") == 0) {
        if (st->has_this) php_objref_encode(st->this_obj_id, base_val, sizeof base_val);
    } else {
        ref->var = php_var_find(st, name);
        if (ref->var) { strncpy(base_val, ref->var->val, sizeof base_val - 1); base_val[sizeof base_val - 1] = 0; }
    }
    php_skip_ws(st);
    c = *st->src;
    /* "st->src[1]" (a POINTER-TYPED STRUCT FIELD dereferenced via "->")
     * compared directly against a char literal, inline -- exactly the
     * confirmed real squash codegen bug this file's own top comment
     * documents throughout ("never compare *st->src/st->src[N] directly
     * -- always assign it to a local char first"). This ONE site had
     * been missed (it predates that convention being applied file-wide,
     * or was simply never exercised deeply enough under a squash-
     * compiled build to surface it before): confirmed this session via a
     * real gcc-vs-squash divergence -- the gcc-compiled harness handled
     * a real WordPress page load correctly end-to-end, the SAME source
     * compiled with squash instead segfaulted inside this exact
     * function. Copying to a genuine local first (c1) is the file's own
     * established, already-proven fix for this bug class. */
    char c1 = st->src[1];
    if (c == '-' && c1 == '>') {
        st->src += 2;
        char member[64];
        php_read_ident(st, member, sizeof member);
        strncpy(ref->member, member, sizeof ref->member - 1); ref->member[sizeof ref->member - 1] = 0;
        ref->has_member = 1;
        ref->obj_id = php_objref_decode(base_val);
        php_skip_ws(st);
        return;
    }
    if (c == '[') {
        st->src++;
        php_skip_ws(st);
        c = *st->src;
        if (c != ']') {
            char kb[PHP_VAL_MAX];
            php_eval_expr(st, kb, sizeof kb);
            strncpy(ref->key, kb, sizeof ref->key - 1); ref->key[sizeof ref->key - 1] = 0;
            ref->has_key = 1;
            php_skip_ws(st);
        }
        c = *st->src;
        if (c == ']') st->src++;
    }
}

/* Parses a comma-separated array-literal body -- "key => value" or bare
 * "value" (auto-incrementing integer key, same as real PHP) elements --
 * up to and consuming the given `closer` ('(' 's own "array(...)" uses
 * ')', a short "[...]" literal uses ']'; the OPENING delimiter must
 * already be consumed by the caller). Populates `v` as an array,
 * overwriting whatever it held before (matches real PHP: reassigning a
 * variable replaces it outright). An explicit integer key bumps
 * next_index past it if needed, so a literal mixing explicit and
 * implicit keys ("array(5 => 'x', 'y')" -> 'y' lands at key 6, not 0)
 * continues auto-incrementing from the right place, same as real PHP. */
static void php_parse_array_literal(PhpState *st, PhpKVArray *arr, char closer, int *out_next_index) {
    arr->count = 0;
    int next_index = 0;
    php_skip_ws(st);
    char c = *st->src;
    if (c == closer) { st->src++; if (out_next_index) *out_next_index = next_index; return; }
    for (;;) {
        char first[PHP_VAL_MAX];
        php_eval_expr(st, first, sizeof first);
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (c0 == '=' && c1 == '>') {
            st->src += 2;
            char val[PHP_VAL_MAX];
            php_eval_expr(st, val, sizeof val);
            php_kv_set(arr, first, val);
            char *endp;
            long n = strtol(first, &endp, 10);
            if (first[0] && *endp == 0 && n >= next_index) next_index = (int)n + 1;
        } else {
            char autokey[16];
            snprintf(autokey, sizeof autokey, "%d", next_index++);
            php_kv_set(arr, autokey, first);
        }
        php_skip_ws(st);
        c0 = *st->src;
        if (c0 == ',') {
            st->src++;
            php_skip_ws(st);
            c0 = *st->src;
            if (c0 == closer) { st->src++; if (out_next_index) *out_next_index = next_index; return; } /* trailing comma */
            continue;
        }
        break;
    }
    php_skip_ws(st);
    c = *st->src;
    if (c == closer) st->src++;
    if (out_next_index) *out_next_index = next_index;
}

/* Evaluates a "$name = EXPR;"-style right-hand side and writes it into
 * `container[key]` -- used by php_resolve_lvalue_chain's own caller
 * once it's found the write target. Handles an "array(...)"/"[...]"
 * literal RHS the same special way the top-level "$var = array(...);"
 * assignment already does (see that call site's own comment on why a
 * generic php_eval_expr can't represent a whole array as its single
 * flat-string result) -- except here the literal's elements go into a
 * FRESH nested-array container (php_array_new), with an
 * arrref-encoded string referencing it stored into `container[key]`,
 * since (unlike the top-level case) there's no PhpVar of its own for
 * the literal to live in directly. */
static void php_lvalue_assign(PhpState *st, PhpKVArray *container, const char *key) {
    php_skip_ws(st);
    const char *rhs_start = st->src;
    char maybe_kw[8];
    php_read_ident(st, maybe_kw, sizeof maybe_kw);
    php_skip_ws(st);
    char cc = *st->src;
    if (strcmp(maybe_kw, "array") == 0 && cc == '(') {
        st->src++;
        int id = php_array_new();
        if (id >= 0) {
            php_parse_array_literal(st, &g_arrays[id], ')', NULL);
            if (container) { char enc[32]; php_arrref_encode(id, enc, sizeof enc); php_kv_set(container, key, enc); }
        } else {
            PhpKVArray dummy; memset(&dummy, 0, sizeof dummy);
            php_parse_array_literal(st, &dummy, ')', NULL);
        }
        return;
    }
    st->src = rhs_start;
    cc = *st->src;
    if (cc == '[') {
        st->src++;
        int id = php_array_new();
        if (id >= 0) {
            php_parse_array_literal(st, &g_arrays[id], ']', NULL);
            if (container) { char enc[32]; php_arrref_encode(id, enc, sizeof enc); php_kv_set(container, key, enc); }
        } else {
            PhpKVArray dummy; memset(&dummy, 0, sizeof dummy);
            php_parse_array_literal(st, &dummy, ']', NULL);
        }
        return;
    }
    char val[PHP_VAL_MAX];
    php_eval_expr(st, val, sizeof val);
    if (container) php_kv_set(container, key, val);
}

/* Same idea as php_lvalue_assign, but for "$GLOBALS['name'] = EXPR;"
 * specifically -- see the "$GLOBALS" handling's own comment (in both
 * php_run_statement's "$"-branch and php_eval_factor's) for why this
 * needs its OWN write path instead of reusing php_lvalue_assign's
 * container/key model: a $GLOBALS entry's storage is a PhpVar in the
 * shared g_globals[] table (the SAME table "global $name;" redirects
 * to -- see g_globals' own comment), not a PhpKVArray slot, so the RHS
 * (scalar, or "array(...)"/"[...]" literal) needs to land in `gv->val`
 * (flat scalar, or an arrref-encoded token for an array literal) rather
 * than via php_kv_set. */
static void php_global_lvalue_assign(PhpState *st, PhpVar *gv) {
    php_skip_ws(st);
    const char *rhs_start = st->src;
    char maybe_kw[8];
    php_read_ident(st, maybe_kw, sizeof maybe_kw);
    php_skip_ws(st);
    char cc = *st->src;
    if (strcmp(maybe_kw, "array") == 0 && cc == '(') {
        st->src++;
        int id = php_array_new();
        if (id >= 0) {
            php_parse_array_literal(st, &g_arrays[id], ')', NULL);
            if (gv) { gv->is_array = 0; char enc[32]; php_arrref_encode(id, enc, sizeof enc); strncpy(gv->val, enc, sizeof gv->val - 1); gv->val[sizeof gv->val - 1] = 0; }
        } else {
            PhpKVArray dummy; memset(&dummy, 0, sizeof dummy);
            php_parse_array_literal(st, &dummy, ')', NULL);
        }
        return;
    }
    st->src = rhs_start;
    cc = *st->src;
    if (cc == '[') {
        st->src++;
        int id = php_array_new();
        if (id >= 0) {
            php_parse_array_literal(st, &g_arrays[id], ']', NULL);
            if (gv) { gv->is_array = 0; char enc[32]; php_arrref_encode(id, enc, sizeof enc); strncpy(gv->val, enc, sizeof gv->val - 1); gv->val[sizeof gv->val - 1] = 0; }
        } else {
            PhpKVArray dummy; memset(&dummy, 0, sizeof dummy);
            php_parse_array_literal(st, &dummy, ']', NULL);
        }
        return;
    }
    char val[PHP_VAL_MAX];
    php_eval_expr(st, val, sizeof val);
    if (gv) { gv->is_array = 0; strncpy(gv->val, val, sizeof gv->val - 1); gv->val[sizeof gv->val - 1] = 0; }
}

/* Resolves an lvalue access chain following a base "$name" already read
 * (st->src positioned right after the identifier, `c` holding the
 * current lookahead character) -- any mix of "[key]" / "->member"
 * segments. All but the LAST segment are treated as reads, auto-
 * vivifying a missing intermediate ARRAY slot into a fresh nested array
 * (matching real PHP's own "$x[a][b] = 1;" behavior when $x[a] didn't
 * exist yet) but NOT auto-vivifying a missing object (real PHP doesn't
 * either -- "$x->a->b = 1;" when $x->a isn't already an object just
 * silently fails to write, same degrade-safely convention as
 * everywhere else in this file).
 *
 * On success (there really was a "[" or "->" chain here), returns 1,
 * fills *out_container/out_key with the FINAL segment's container + key
 * (NULL container if the chain became invalid partway -- caller should
 * still finish consuming the statement, just skip the actual write),
 * and leaves st->src positioned right after the last segment (i.e. at
 * what should be "=" for an assignment, or anything else for a bare
 * read/call statement the caller falls back on). Returns 0 (st->src
 * COMPLETELY UNCONSUMED, still at the position it was called with) if
 * there's no "[" or "->" at all -- callers use that to fall back to
 * their own plain "$name = expr;" handling untouched. */
static int php_resolve_lvalue_chain(PhpState *st, const char *name, char c, PhpKVArray **out_container, char *out_key, int out_key_cap) {
    PhpKVArray *container = NULL;
    char key[128]; key[0] = 0;

    if (strcmp(name, "this") == 0) {
        if (!(c == '-' && st->src[1] == '>')) return 0;
        int oid = st->has_this ? st->this_obj_id : -1;
        st->src += 2;
        char member[64];
        php_read_ident(st, member, sizeof member);
        strncpy(key, member, sizeof key - 1); key[sizeof key - 1] = 0;
        container = (oid >= 0) ? &g_objects[oid].props : NULL;
    } else if (c == '[') {
        st->src++;
        php_skip_ws(st);
        char kb[PHP_VAL_MAX]; int has_key = 0;
        char cc = *st->src;
        if (cc != ']') { php_eval_expr(st, kb, sizeof kb); has_key = 1; php_skip_ws(st); }
        cc = *st->src;
        if (cc == ']') st->src++;
        PhpVar *v = php_var_find_or_create(st, name);
        if (v) {
            v->is_array = 1;
            if (!has_key) { snprintf(kb, sizeof kb, "%d", v->next_index); has_key = 1; }
            strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
            char *endp;
            long n = strtol(key, &endp, 10);
            if (key[0] && *endp == 0 && n >= v->next_index) v->next_index = (int)n + 1;
            container = &v->arr;
        }
    } else if (c == '-' && st->src[1] == '>') {
        st->src += 2;
        char member[64];
        php_read_ident(st, member, sizeof member);
        strncpy(key, member, sizeof key - 1); key[sizeof key - 1] = 0;
        PhpVar *v = php_var_find(st, name);
        int oid = v ? php_objref_decode(v->val) : -1;
        container = (oid >= 0) ? &g_objects[oid].props : NULL;
    } else {
        return 0;
    }

    for (;;) {
        php_skip_ws(st);
        char cc = st->src[0], cc1 = st->src[1];
        if (cc == '[') {
            int nid = -1;
            if (container) {
                char cur[PHP_VAL_MAX];
                if (php_kv_has(container, key, cur, sizeof cur)) nid = php_arrref_decode(cur);
                if (nid < 0) {
                    nid = php_array_new();
                    if (nid >= 0) { char enc[32]; php_arrref_encode(nid, enc, sizeof enc); php_kv_set(container, key, enc); }
                }
            }
            st->src++;
            php_skip_ws(st);
            char kb[PHP_VAL_MAX]; int has_key = 0;
            cc = *st->src;
            if (cc != ']') { php_eval_expr(st, kb, sizeof kb); has_key = 1; php_skip_ws(st); }
            cc = *st->src;
            if (cc == ']') st->src++;
            if (!has_key) strncpy(kb, "0", sizeof kb); /* nested "$x[a][] = v;" push -- without per-container next_index tracking this always targets key "0"; a known narrow gap, not the common shape */
            strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
            container = (nid >= 0) ? &g_arrays[nid] : NULL;
            continue;
        }
        if (cc == '-' && cc1 == '>') {
            int oid = -1;
            if (container) {
                char cur[PHP_VAL_MAX];
                if (php_kv_has(container, key, cur, sizeof cur)) oid = php_objref_decode(cur);
            }
            st->src += 2;
            char member[64];
            php_read_ident(st, member, sizeof member);
            strncpy(key, member, sizeof key - 1); key[sizeof key - 1] = 0;
            container = (oid >= 0) ? &g_objects[oid].props : NULL;
            continue;
        }
        break;
    }
    *out_container = container;
    strncpy(out_key, key, out_key_cap - 1); out_key[out_key_cap - 1] = 0;
    return 1;
}

/* A single value: string/number literal, "$var", a superglobal array
 * access "$_GET['key']" / "$_POST['key']" / "$_SERVER['key']", a
 * parenthesized sub-expression, a leading "!" (boolean not), a bare
 * identifier that's either a call "name(args)" or a constant lookup
 * (define()'d, or the magic __DIR__/__FILE__, or true/false/null). */
static void php_eval_factor(PhpState *st, char *out, int outcap) {
    php_skip_ws(st);
    char c = *st->src;
    if (c == '!') {
        st->src++;
        char inner[PHP_VAL_MAX];
        php_eval_factor(st, inner, sizeof inner);
        /* php_truthy(), not an inline re-check -- this used to have its
         * OWN separate "inner[0]!=0 && strcmp(inner,\"0\")!=0" copy of
         * plain-scalar truthiness, predating (and never updated when)
         * php_truthy() gained real array/object-aware rules -- so
         * "!$emptyArray" stayed permanently wrong (always false) even
         * after that fix, since "$emptyArray ? a : b" (which DOES call
         * php_truthy()) and "!$emptyArray" disagreed on the exact same
         * value. */
        int truthy = php_truthy(inner);
        strncpy(out, truthy ? "0" : "1", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (c == '&') {
        /* "&$var" -- PHP's reference operator, e.g. "$l10n[$domain] =
         * &$noop_translations;" (wp-includes/l10n.php's
         * get_translations_for_domain()). This subset has no true
         * references (a variable's value is always a plain copied
         * string/arrref/objref token, see this file's top comment), so
         * the correct simplification is a plain value copy of whatever
         * follows -- NOT a no-op. Before this case existed, a leading
         * "&" matched nothing in this whole function (not "!"/quote/
         * paren/"$"), so control fell all the way through to the
         * number-literal scanner at the bottom, which requires at least
         * one digit/'.'/'-' to match -- "&" itself doesn't, so it
         * matched ZERO characters and returned "" WITHOUT ADVANCING
         * st->src. Confirmed as a real, actively-harmful bug: it
         * silently evaluated "&$noop_translations" to an empty string
         * (instead of failing loudly or advancing past the unparsed
         * "&"), corrupting whatever assignment it fed -- exactly what
         * made get_translations_for_domain()'s own translation cache
         * permanently poisoned with an empty entry once "global $l10n;"
         * started working for real (see g_globals' own comment). */
        st->src++;
        php_eval_factor(st, out, outcap);
        return;
    }
    if (c == '"' || c == '\'') {
        php_read_string_lit(st, out, outcap);
        return;
    }
    if (c == '(') {
        /* PHP type-cast operator -- "(int)"/"(integer)", "(string)",
         * "(bool)"/"(boolean)", "(float)"/"(double)"/"(real)", "(array)",
         * "(object)" immediately followed by the value being cast, e.g.
         * "(array) $wp_theme_directories". Completely unhandled before
         * (this whole "(" branch always treated everything between the
         * parens as one full parenthesized sub-expression) -- confirmed
         * as a real, previously-undiscovered parsing gap this session
         * via wp-includes/theme.php's own repeated "(array)
         * $wp_theme_directories" casts inside in_array()'s 2nd argument
         * (register_theme_directory(), get_theme_root(), several
         * others): "(array)" alone was misread as a complete
         * parenthesized expression evaluating the bare identifier
         * "array" (an undefined constant, "" here) -- with the following
         * " $wp_theme_directories" then left as unconsumed dangling text
         * that desynced the ENCLOSING call's own argument list, silently
         * dropping in_array()'s 3rd ("strict") argument and making the
         * whole check see an empty haystack instead of the real,
         * already-correctly-populated $wp_theme_directories array.
         * Detected by peeking for one of these exact type-name idents
         * immediately followed by ')' -- indistinguishable from a false
         * positive in practice (no real PHP expression is a bare
         * constant named exactly "array"/"int"/"string"/... alone inside
         * parens). Binds to the following FACTOR (php_eval_factor, not
         * php_eval_expr) -- matches real PHP's own precedence, where a
         * cast binds tighter than any binary operator. */
        const char *save_paren = st->src;
        st->src++;
        php_skip_ws(st);
        char maybe_type[16];
        php_read_ident(st, maybe_type, sizeof maybe_type);
        php_skip_ws(st);
        char cast = 0;
        if (*st->src == ')') {
            if (strcmp(maybe_type, "int") == 0 || strcmp(maybe_type, "integer") == 0) cast = 'i';
            else if (strcmp(maybe_type, "string") == 0 || strcmp(maybe_type, "binary") == 0) cast = 's';
            else if (strcmp(maybe_type, "bool") == 0 || strcmp(maybe_type, "boolean") == 0) cast = 'b';
            else if (strcmp(maybe_type, "float") == 0 || strcmp(maybe_type, "double") == 0 || strcmp(maybe_type, "real") == 0) cast = 'f';
            else if (strcmp(maybe_type, "array") == 0) cast = 'a';
            else if (strcmp(maybe_type, "object") == 0) cast = 'o';
        }
        if (cast) {
            st->src++; /* consume ')' */
            char inner[PHP_VAL_MAX];
            php_eval_factor(st, inner, sizeof inner);
            switch (cast) {
                case 'i': { long v = strtol(inner, NULL, 10); snprintf(out, outcap, "%ld", v); break; }
                case 'f': { double v = strtod(inner, NULL); snprintf(out, outcap, "%g", v); break; }
                case 'b': strncpy(out, php_truthy(inner) ? "1" : "", outcap - 1); out[outcap - 1] = 0; break;
                case 's': strncpy(out, inner, outcap - 1); out[outcap - 1] = 0; break;
                case 'a':
                    if (php_arrref_decode(inner) >= 0) {
                        strncpy(out, inner, outcap - 1); out[outcap - 1] = 0;
                    } else {
                        int id = php_array_new();
                        if (id >= 0) {
                            if (inner[0] || php_objref_decode(inner) >= 0) php_kv_add(&g_arrays[id], "0", inner);
                            php_arrref_encode(id, out, outcap);
                        } else out[0] = 0;
                    }
                    break;
                default: /* '(object)' -- not modeled as a real conversion, pass the value through unchanged */
                    strncpy(out, inner, outcap - 1); out[outcap - 1] = 0;
                    break;
            }
            return;
        }
        st->src = save_paren;
        st->src++;
        php_eval_expr(st, out, outcap);
        php_skip_ws(st);
        c = *st->src;
        if (c == ')') st->src++;
        return;
    }
    if (c == '$') {
        st->src++;
        char name[64];
        php_read_ident(st, name, sizeof name);
        if (strcmp(name, "_GET") == 0 || strcmp(name, "_POST") == 0 || strcmp(name, "_SERVER") == 0) {
            php_skip_ws(st);
            char key[128]; key[0] = 0;
            c = *st->src;
            if (c == '[') {
                st->src++;
                php_skip_ws(st);
                char kb[PHP_VAL_MAX];
                c = *st->src;
                if (c == '"' || c == '\'') php_read_string_lit(st, kb, sizeof kb);
                else { php_read_ident(st, kb, sizeof kb); }
                strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
                php_skip_ws(st);
                c = *st->src;
                if (c == ']') st->src++;
            }
            if (strcmp(name, "_GET") == 0) php_kv_lookup(&st->get, key, out, outcap);
            else if (strcmp(name, "_POST") == 0) php_kv_lookup(&st->post, key, out, outcap);
            else {
                if (strcmp(key, "REQUEST_METHOD") == 0) { strncpy(out, st->server_method, outcap - 1); out[outcap-1]=0; }
                else out[0] = 0;
            }
            return;
        }
        /* Base value: "$this" (bound per-call via
         * PhpState.has_this/this_obj_id -- see php_call_method) reads as
         * an object-reference string, same representation any other
         * object-valued variable has; a plain "$name" reads its var
         * (optionally through one "[key]" if it's an array); either way
         * the result lands in `cur`, ready for the SAME generic postfix
         * loop below to walk any further "[key]"/"->member" segments --
         * unifying "$this" and a plain variable here (rather than two
         * separate near-duplicate chain-walking loops, which is what
         * this file had before) is what makes "$this->arr[$k1][$k2]"-
         * shaped chains (exactly what WordPress's own WP_Hook methods
         * use) work without yet another bespoke case. */
        char cur[PHP_VAL_MAX];
        if (strcmp(name, "this") == 0) {
            if (st->has_this) php_objref_encode(st->this_obj_id, cur, sizeof cur);
            else cur[0] = 0;
        } else if (strcmp(name, "GLOBALS") == 0) {
            /* "$GLOBALS['name']" read -- see php_run_statement's own
             * "$GLOBALS" write-side comment for the full story (WordPress's
             * wp_cache_init() and 150+ other real sites use this instead
             * of "global $name;"). Sourced from the SAME shared
             * g_globals[] table "global $name;" redirects to
             * (php_global_find), not a normal PhpVar lookup -- only the
             * single-level "$GLOBALS['name']" form is recognized (a bare
             * "$GLOBALS" with no index degrades to "", matching this
             * file's everywhere-else convention for an unsupported
             * shape); the postfix loop right below still applies
             * afterward, so "$GLOBALS['wp_object_cache']->get(...)"
             * chains correctly once `cur` is populated here. */
            php_skip_ws(st);
            c = *st->src;
            if (c == '[') {
                st->src++;
                php_skip_ws(st);
                char key[128]; key[0] = 0;
                c = *st->src;
                if (c != ']') {
                    char kb[PHP_VAL_MAX];
                    php_eval_expr(st, kb, sizeof kb);
                    strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
                    php_skip_ws(st);
                }
                c = *st->src;
                if (c == ']') st->src++;
                PhpVar *gv = php_global_find(key);
                if (gv && gv->is_array) {
                    int id = php_array_new();
                    if (id >= 0) {
                        int ii;
                        for (ii = 0; ii < gv->arr.count; ii++) php_kv_set(&g_arrays[id], gv->arr.items[ii].key, gv->arr.items[ii].val);
                        php_arrref_encode(id, cur, sizeof cur);
                    } else cur[0] = 0;
                } else if (gv) {
                    strncpy(cur, gv->val, sizeof cur - 1); cur[sizeof cur - 1] = 0;
                } else cur[0] = 0;
            } else {
                cur[0] = 0;
            }
        } else {
            PhpVar *v = php_var_find(st, name);
            php_skip_ws(st);
            c = *st->src;
            if (c == '[') {
                st->src++;
                php_skip_ws(st);
                char key[128]; key[0] = 0;
                c = *st->src;
                if (c != ']') {
                    char kb[PHP_VAL_MAX];
                    php_eval_expr(st, kb, sizeof kb);
                    strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
                    php_skip_ws(st);
                }
                c = *st->src;
                if (c == ']') st->src++;
                if (v && v->is_array) php_kv_lookup(&v->arr, key, cur, sizeof cur);
                else if (v) {
                    /* `v` isn't a NATIVE array (no "$v = array(...)" ever
                     * ran for it), but its string VALUE might still be an
                     * arrref token -- e.g. a function/method PARAMETER
                     * that received an array argument (php_call_function/
                     * php_call_method bind params via a plain
                     * php_var_set(), which always clears is_array, same
                     * as any other assignment -- see php_var_set's own
                     * comment), or "$b = $a;" copying an array-holding
                     * variable's value into a fresh non-array one. Every
                     * SUBSEQUENT "[key]" in a chain (anything past the
                     * first, e.g. "$this->arr[$k]"/"$a[$k1][$k2]")
                     * already decodes `cur` as a possible arrref
                     * regardless of any flag -- this was the ONE
                     * remaining place a first-level "$name[key]" access
                     * didn't, silently returning "" instead of the real
                     * value for exactly the "$data['col']"-shaped access
                     * a $wpdb-style class needs on its own array
                     * parameters. */
                    int aid = php_arrref_decode(v->val);
                    if (aid >= 0) php_kv_lookup(&g_arrays[aid], key, cur, sizeof cur);
                    else cur[0] = 0;
                }
                else cur[0] = 0;
            } else if (v && v->is_array) {
                /* A top-level array variable read BARE (no index) -- e.g.
                 * passed as a function/method argument, "$b = $a;",
                 * "return $arr;". Top-level arrays store their elements
                 * directly in the PhpVar's own embedded PhpKVArray (see
                 * this file's top comment on why), not through an arrref
                 * token the way a NESTED array value does, so reading
                 * `v->val` here (meaningless for an array -- it's never
                 * written for an array-typed PhpVar) previously handed
                 * the caller an empty string instead of anything usable,
                 * silently breaking every "pass a whole array by value"
                 * pattern. Confirmed a real, practical gap this session
                 * while wiring up "$wpdb->insert($table, $data)"-shaped
                 * calls, a real WordPress call shape (wp-includes/
                 * option.php builds $data as a local variable, then
                 * passes it). Snapshot the array's current contents into
                 * a FRESH nested-array container and hand back an arrref
                 * to THAT (a copy, not a live alias -- matching real
                 * PHP's own copy-on-write array value semantics) so the
                 * callee can index/iterate it via the same
                 * php_arrref_decode() path as any other array value. */
                int id = php_array_new();
                if (id >= 0) {
                    int ii;
                    for (ii = 0; ii < v->arr.count; ii++) php_kv_set(&g_arrays[id], v->arr.items[ii].key, v->arr.items[ii].val);
                    php_arrref_encode(id, cur, sizeof cur);
                } else cur[0] = 0;
            } else {
                if (v) { strncpy(cur, v->val, sizeof cur - 1); cur[sizeof cur - 1] = 0; }
                else cur[0] = 0;
            }
        }
        php_skip_ws(st);
        c = *st->src;
        while ((c == '-' && st->src[1] == '>') || c == '[') {
            /* "->prop" / "->method(args)" / "[key]", any mix, chainable
             * to arbitrary depth ("$a->b->c", "$x[k1][k2]",
             * "$this->callbacks[$priority][$idx]", "$arr[key]->method()
             * ->prop", ...) -- decoding `cur` AGAIN as an object/array
             * reference at each step (rather than tracking "what kind of
             * thing is `cur` right now" as separate loop state) is what
             * makes arbitrary chain depth/shape fall out for free, see
             * php_objref_decode's/php_arrref_decode's own comments. */
            if (c == '-' && st->src[1] == '>') {
                st->src += 2;
                char member[64];
                php_read_ident(st, member, sizeof member);
                php_skip_ws(st);
                c = *st->src;
                int oid = php_objref_decode(cur);
                if (c == '(') {
                    char arg_storage[PHP_ARG_MAX][PHP_VAL_MAX];
                    char *margs[PHP_ARG_MAX];
                    int ai;
                    for (ai = 0; ai < PHP_ARG_MAX; ai++) margs[ai] = arg_storage[ai];
                    int nargs = php_parse_args(st, margs);
                    if (oid >= 0) {
                        PhpClass *cls = php_class_find(g_objects[oid].class_name);
                        PhpMethod *m = php_class_find_method(cls, member);
                        if (m) php_call_method(st, oid, m, margs, nargs, cur, sizeof cur);
                        else { if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[UNKNOWN METHOD] %s::%s()\n", cls ? cls->name : "?", member); cur[0] = 0; }
                    } else { if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[METHOD CALL ON NON-OBJECT] ->%s() cur='%.40s'\n", member, cur); cur[0] = 0; }
                } else {
                    if (oid >= 0) php_kv_lookup(&g_objects[oid].props, member, cur, sizeof cur);
                    else cur[0] = 0;
                }
            } else { /* c == '[' */
                int aid = php_arrref_decode(cur);
                st->src++;
                php_skip_ws(st);
                char key[128]; key[0] = 0;
                c = *st->src;
                if (c != ']') {
                    char kb[PHP_VAL_MAX];
                    php_eval_expr(st, kb, sizeof kb);
                    strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
                    php_skip_ws(st);
                }
                c = *st->src;
                if (c == ']') st->src++;
                if (aid >= 0) php_kv_lookup(&g_arrays[aid], key, cur, sizeof cur);
                else cur[0] = 0;
            }
            php_skip_ws(st);
            c = *st->src;
        }
        strncpy(out, cur, outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        char name[64];
        php_read_ident(st, name, sizeof name);
        php_skip_ws(st);
        c = *st->src;
        if (c == ':' && st->src[1] == ':') {
            /* "ClassName::method(args)" / "self::method(args)" /
             * "parent::method(args)" / "static::method(args)" /
             * "ClassName::CONST_NAME" / "ClassName::class" -- previously
             * unrecognized entirely, so `name` (the class name) fell
             * through to the generic bare-identifier path below and the
             * "::member" was left in st->src, silently desyncing the
             * parse; the (rare) case that happened to still look like a
             * function call downstream showed up as an "unknown func"
             * trace for `method`/`get_instance`/etc with the class name
             * quietly dropped (see this file's known-gaps list). Resolve
             * `name` against self/parent/static (the only class identity
             * this interpreter tracks per call is the CURRENT object's
             * runtime class via PhpState.has_this/this_obj_id -- see
             * php_call_static's own comment -- so "self"/"static" both
             * resolve to that object's own class; real PHP's
             * self-means-declaring-class-not-runtime-class distinction
             * only matters once a method is actually overridden AND
             * called via self:: from the PARENT's own body, an edge case
             * this subset doesn't need to get exactly right) to a real
             * PhpClass, then dispatch either a method call (method
             * lookup walks the "extends" chain, see
             * php_class_find_method) or a constant/::class read. */
            st->src += 2;
            char member[64];
            php_read_ident(st, member, sizeof member);
            php_skip_ws(st);
            c = *st->src;
            const char *resolved_class = name;
            PhpClass *self_cls = NULL;
            if ((strcmp(name, "self") == 0 || strcmp(name, "static") == 0 || strcmp(name, "parent") == 0) && st->has_this && st->this_obj_id >= 0) {
                self_cls = php_class_find(g_objects[st->this_obj_id].class_name);
                if (strcmp(name, "parent") == 0 && self_cls) {
                    self_cls = self_cls->parent_name[0] ? php_class_find(self_cls->parent_name) : NULL;
                }
                resolved_class = self_cls ? self_cls->name : name;
            }
            PhpClass *cls = self_cls ? self_cls : php_class_find(resolved_class);
            if (c == '(') {
                char arg_storage[PHP_ARG_MAX][PHP_VAL_MAX];
                char *margs[PHP_ARG_MAX];
                int ai;
                for (ai = 0; ai < PHP_ARG_MAX; ai++) margs[ai] = arg_storage[ai];
                int nargs = php_parse_args(st, margs);
                PhpMethod *m = php_class_find_method(cls, member);
                if (m) php_call_static(st, m, margs, nargs, out, outcap);
                else {
                    if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[UNKNOWN STATIC METHOD] %s::%s() (class %s)\n", name, member, cls ? "found" : "NOT FOUND");
                    out[0] = 0; /* unknown static method: degrade to empty, don't crash */
                }
                return;
            }
            if (strcmp(member, "class") == 0) {
                /* "Foo::class" magic constant -- evaluates to the class's
                 * own name as a plain string, real PHP semantics. */
                strncpy(out, cls ? cls->name : name, outcap - 1); out[outcap - 1] = 0;
                return;
            }
            if (!php_class_find_const(cls, member, out, outcap)) out[0] = 0;
            return;
        }
        if (strcmp(name, "array") == 0 && c == '(') {
            /* An "array(...)" literal used directly as a general
             * expression VALUE -- e.g. "$wpdb->insert($t, array('a'=>1))",
             * a real, common WordPress call shape (confirmed in this
             * project's own vendored source, wp-includes/option.php).
             * The "$var = array(...)"/"container[key] = array(...)"
             * assignment forms already special-case this (see
             * php_lvalue_assign's own comment on why a flat string can't
             * represent a whole array), but that special-casing lived
             * only in the two lvalue-assignment call sites -- an
             * array-literal appearing as an ORDINARY sub-expression (a
             * function argument, here) fell through to the generic
             * fallback below instead and evaluated to garbage/nothing.
             * Same fix shape: allocate a fresh nested-array container,
             * parse the literal into it, and evaluate to an arrref
             * string pointing at it -- from here on it's just another
             * array value, indexable/iterable by the callee exactly like
             * any other array reference. */
            st->src++;
            int id = php_array_new();
            if (id >= 0) {
                php_parse_array_literal(st, &g_arrays[id], ')', NULL);
                php_arrref_encode(id, out, outcap);
            } else {
                PhpKVArray dummy; memset(&dummy, 0, sizeof dummy);
                php_parse_array_literal(st, &dummy, ')', NULL);
                out[0] = 0;
            }
            return;
        }
        if (strcmp(name, "new") == 0) {
            /* "new ClassName(args)" -- allocates a fresh object (see
             * php_object_new) and calls its __construct(), if one's
             * defined, with `args`. Evaluates to an object-reference
             * string (php_objref_encode) like any other value -- see
             * that helper's own comment on why that's enough to make
             * "$x = new Foo();" and everything downstream (property/
             * method access via "->", storing it in an array, passing
             * it as a function argument, ...) just work through the
             * existing plain-string machinery with no special-casing. */
            char cname[64];
            php_read_ident(st, cname, sizeof cname);
            php_skip_ws(st);
            c = *st->src;
            char arg_storage[PHP_ARG_MAX][PHP_VAL_MAX];
            char *args[PHP_ARG_MAX];
            int ai;
            for (ai = 0; ai < PHP_ARG_MAX; ai++) args[ai] = arg_storage[ai];
            int nargs = 0;
            if (c == '(') nargs = php_parse_args(st, args);
            int id = php_object_new(cname);
            if (id >= 0) {
                PhpClass *cls = php_class_find(cname);
                PhpMethod *ctor = php_class_find_method(cls, "__construct");
                if (ctor) {
                    char discard[PHP_VAL_MAX];
                    php_call_method(st, id, ctor, args, nargs, discard, sizeof discard);
                }
                php_objref_encode(id, out, outcap);
            } else {
                out[0] = 0; /* unknown class or object table full: degrade to empty, don't crash */
            }
            return;
        }
        if (c == '(') {
            /* isset()/empty()/count()/sizeof()/is_array() all need the
             * raw variable (existence, array-ness, element count), not
             * a stringified value -- generic php_parse_args would
             * evaluate "$arr['x']" down to a flat string and lose
             * exactly the information these need, so each gets its own
             * argument parse via php_resolve_varref instead of falling
             * into the generic call path below. */
            if (strcmp(name, "isset") == 0 || strcmp(name, "empty") == 0) {
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                c = *st->src;
                /* php_resolve_varref() also handles the "BASE->member"
                 * shape now (see its own comment on the real bug this
                 * fixed) -- but it never consumes exactly up to a ')'
                 * for every possible input shape, so a resync here is
                 * still cheap insurance against any OTHER expression
                 * shape it doesn't recognize leaving st->src desynced. */
                if (c != ')') php_skip_to_paren_close(st);
                c = *st->src;
                if (c == ')') st->src++;
                int result;
                if (strcmp(name, "isset") == 0) {
                    if (ref.has_member) {
                        char tmp[PHP_VAL_MAX];
                        result = ref.obj_id >= 0 && php_kv_has(&g_objects[ref.obj_id].props, ref.member, tmp, sizeof tmp);
                    }
                    else if (!ref.var) result = 0;
                    else if (ref.has_key) { char tmp[PHP_VAL_MAX]; result = ref.var->is_array && php_kv_has(&ref.var->arr, ref.key, tmp, sizeof tmp); }
                    else result = 1;
                } else { /* empty() */
                    if (ref.has_member) {
                        char tmp[PHP_VAL_MAX];
                        if (ref.obj_id < 0 || !php_kv_has(&g_objects[ref.obj_id].props, ref.member, tmp, sizeof tmp)) result = 1;
                        else result = !php_truthy(tmp);
                    }
                    else if (!ref.var) result = 1;
                    else if (ref.has_key) {
                        char tmp[PHP_VAL_MAX];
                        if (!ref.var->is_array || !php_kv_has(&ref.var->arr, ref.key, tmp, sizeof tmp)) result = 1;
                        else result = !php_truthy(tmp);
                    } else if (ref.var->is_array) result = (ref.var->arr.count == 0);
                    else result = !php_truthy(ref.var->val);
                }
                strncpy(out, result ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
                return;
            }
            if (strcmp(name, "count") == 0 || strcmp(name, "sizeof") == 0) {
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                c = *st->src;
                /* php_resolve_varref() also handles the "BASE->member"
                 * shape now (see its own comment on the real bug this
                 * fixed) -- but it never consumes exactly up to a ')'
                 * for every possible input shape, so a resync here is
                 * still cheap insurance against any OTHER expression
                 * shape it doesn't recognize leaving st->src desynced. */
                if (c != ')') php_skip_to_paren_close(st);
                c = *st->src;
                if (c == ')') st->src++;
                /* A plain non-array-flagged var whose STRING VALUE is an
                 * arrref token (e.g. "$results = __db_query(...);" --
                 * see php_var_set's own comment: an ordinary assignment
                 * always clears is_array) still needs its real element
                 * count here, not the "1" a scalar would get -- same
                 * arrref-string fallback as the "$name[key]" read path
                 * above, see that comment for the full story. */
                int n;
                if (ref.has_member) {
                    char tmp[PHP_VAL_MAX];
                    if (ref.obj_id >= 0 && php_kv_has(&g_objects[ref.obj_id].props, ref.member, tmp, sizeof tmp)) {
                        int aid = php_arrref_decode(tmp);
                        n = (aid >= 0) ? g_arrays[aid].count : 1;
                    } else n = 0;
                }
                else if (ref.var && ref.var->is_array) n = ref.var->arr.count;
                else if (ref.var) {
                    int aid = php_arrref_decode(ref.var->val);
                    n = (aid >= 0) ? g_arrays[aid].count : 1;
                } else n = 0;
                snprintf(out, outcap, "%d", n);
                return;
            }
            if (strcmp(name, "is_array") == 0) {
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                c = *st->src;
                /* php_resolve_varref() also handles the "BASE->member"
                 * shape now (see its own comment on the real bug this
                 * fixed) -- but it never consumes exactly up to a ')'
                 * for every possible input shape, so a resync here is
                 * still cheap insurance against any OTHER expression
                 * shape it doesn't recognize leaving st->src desynced. */
                if (c != ')') php_skip_to_paren_close(st);
                c = *st->src;
                if (c == ')') st->src++;
                int isarr;
                if (ref.has_member) {
                    char tmp[PHP_VAL_MAX];
                    isarr = ref.obj_id >= 0 && php_kv_has(&g_objects[ref.obj_id].props, ref.member, tmp, sizeof tmp) && php_arrref_decode(tmp) >= 0;
                } else {
                    isarr = ref.var && (ref.var->is_array || php_arrref_decode(ref.var->val) >= 0);
                }
                strncpy(out, isarr ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
                return;
            }
            char arg_storage[PHP_ARG_MAX][PHP_VAL_MAX];
            char *args[PHP_ARG_MAX];
            int ai;
            for (ai = 0; ai < PHP_ARG_MAX; ai++) args[ai] = arg_storage[ai];
            int nargs = php_parse_args(st, args);
            php_call_function(st, name, args, nargs, out, outcap);
            return;
        }
        if (strcmp(name, "true") == 0) { strncpy(out, "1", outcap - 1); out[outcap-1]=0; return; }
        if (strcmp(name, "false") == 0 || strcmp(name, "null") == 0) { out[0] = 0; return; }
        if (strcmp(name, "__DIR__") == 0) { strncpy(out, st->cur_dir, outcap - 1); out[outcap-1]=0; return; }
        if (strcmp(name, "__FILE__") == 0) { strncpy(out, st->cur_file, outcap - 1); out[outcap-1]=0; return; }
        {
            const char *cv = php_const_find(name);
            if (cv) { strncpy(out, cv, outcap - 1); out[outcap - 1] = 0; return; }
        }
        out[0] = 0; /* unknown bare identifier: degrade to empty, don't crash */
        return;
    }
    /* Number literal -- scans on a local `p`, see this file's top comment. */
    {
        const char *p = st->src;
        const char *start = p;
        char pc = *p;
        while ((pc >= '0' && pc <= '9') || pc == '.' || pc == '-') { p++; pc = *p; }
        int len = (int)(p - start);
        if (len > 0 && len < outcap) { memcpy(out, start, (size_t)len); out[len] = 0; }
        else out[0] = 0;
        st->src = p;
    }
}

/* term := factor (("*"|"/") factor)* -- basic arithmetic, numeric only. */
static void php_eval_term(PhpState *st, char *out, int outcap) {
    char lhs[PHP_VAL_MAX];
    php_eval_factor(st, lhs, sizeof lhs);
    for (;;) {
        php_skip_ws(st);
        char c = *st->src;
        if (c == '*' || c == '/') {
            char op = c; st->src++;
            char rhs[PHP_VAL_MAX];
            php_eval_factor(st, rhs, sizeof rhs);
            double a = php_to_num(lhs), b = php_to_num(rhs);
            double r = (op == '*') ? a * b : (b != 0 ? a / b : 0.0);
            php_num_to_str(r, lhs, sizeof lhs);
        } else break;
    }
    strncpy(out, lhs, outcap - 1); out[outcap - 1] = 0;
}

/* addsub := term (("."|"+"|"-") term)* -- "." is string concatenation
 * (real PHP's own operator for it), "+"/"-" numeric. This is the OLD
 * "php_eval_expr" body -- renamed because php_eval_expr itself is now the
 * top of a full precedence chain (see php_eval_cmp/php_eval_and/
 * php_eval_or below), so every existing call site that already called
 * "php_eval_expr" (echo/print, assignments, return, function args,
 * parenthesized sub-expressions, ...) transparently gains "=="/"!="/
 * "<"/">"/"<="/">="/"&&"/"||" support for free, matching how real PHP
 * allows a full boolean expression anywhere a value expression is
 * allowed, not just inside "if (...)". */
static void php_eval_addsub(PhpState *st, char *out, int outcap) {
    char acc[PHP_VAL_MAX];
    php_eval_term(st, acc, sizeof acc);
    for (;;) {
        php_skip_ws(st);
        char c = *st->src;
        if (c == '.') {
            st->src++;
            char rhs[PHP_VAL_MAX];
            php_eval_term(st, rhs, sizeof rhs);
            char combined[PHP_VAL_MAX];
            snprintf(combined, sizeof combined, "%s%s", acc, rhs);
            strncpy(acc, combined, sizeof acc - 1); acc[sizeof acc - 1] = 0;
        } else if (c == '+' || c == '-') {
            char op = c; st->src++;
            char rhs[PHP_VAL_MAX];
            php_eval_term(st, rhs, sizeof rhs);
            double a = php_to_num(acc), b = php_to_num(rhs);
            double r = (op == '+') ? a + b : a - b;
            php_num_to_str(r, acc, sizeof acc);
        } else break;
    }
    strncpy(out, acc, outcap - 1); out[outcap - 1] = 0;
}


/* instanceof := addsub ("instanceof" (ClassName | "self"|"static"|"parent"))*
 * -- session 8: previously COMPLETELY unhandled (no case anywhere in this
 * file), which is a real, high-impact parsing bug of the exact same shape
 * as the historical "&&"/"elseif"/"===" ones documented above: real
 * WordPress code does "if ( ! $x instanceof Foo ) { ... }" constantly (68
 * files in the vendored tree use it) -- with no handling at all, "$x"
 * evaluates fine but "instanceof Foo" is left completely unconsumed,
 * desyncing whatever follows (the enclosing "if"'s own ")" search fails to
 * match at the expected spot). Found via SQS_TRACE_CALLS tracing showing
 * thousands of "->has()"/"->add_filter()" calls landing on an empty
 * (non-object) base value -- traced to wp-includes/load.php's own
 * "if ( ! $wp_textdomain_registry instanceof WP_Textdomain_Registry )
 * { $wp_textdomain_registry = new WP_Textdomain_Registry(); }" never
 * correctly constructing the registry object (the corrupted parse fell
 * through some other way), and separately plugin.php's own hook-object
 * pattern nearby suffering the same class of corruption from unrelated
 * "instanceof" checks upstream in the same request. Since every value in
 * this subset is an ordinary string (objects are a magic-prefixed
 * string token, see php_objref_encode/decode), this decodes the LHS as an
 * object reference and walks its class's real single-inheritance parent
 * chain (php_class_find, matching how "extends"-based method/const
 * lookup already works) -- interfaces ("implements") aren't modeled here
 * either, matching every other place in this file that parses but
 * doesn't act on "implements". */
static void php_eval_instanceof(PhpState *st, char *out, int outcap) {
    char acc[PHP_VAL_MAX];
    php_eval_addsub(st, acc, sizeof acc);
    for (;;) {
        const char *save = st->src;
        php_skip_ws(st);
        char kw[16];
        php_read_ident(st, kw, sizeof kw);
        if (strcmp(kw, "instanceof") != 0) { st->src = save; break; }
        php_skip_ws(st);
        char cls[128];
        php_read_ident(st, cls, sizeof cls);
        int oid = php_objref_decode(acc);
        int match = 0;
        if (oid >= 0) {
            const char *want = cls;
            if (strcmp(cls, "self") == 0 || strcmp(cls, "static") == 0) want = g_objects[oid].class_name;
            PhpClass *c = php_class_find(g_objects[oid].class_name);
            int guard = 0;
            while (c && guard++ < 32) {
                if (strcmp(c->name, want) == 0) { match = 1; break; }
                if (!c->parent_name[0]) break;
                c = php_class_find(c->parent_name);
            }
        }
        strncpy(acc, match ? "1" : "0", sizeof acc - 1); acc[sizeof acc - 1] = 0;
    }
    strncpy(out, acc, outcap - 1); out[outcap - 1] = 0;
}

/* cmp := addsub (("=="|"!="|"<="|">="|"<"|">") addsub)? -- numeric
 * comparison for "<"/">"/"<="/">=" (real PHP does looser type-juggling
 * comparisons than this; not modeled), string equality for "=="/"!=". */
static void php_eval_cmp(PhpState *st, char *out, int outcap) {
    char lhs[PHP_VAL_MAX];
    php_eval_instanceof(st, lhs, sizeof lhs);
    php_skip_ws(st);
    char c0 = st->src[0], c1 = st->src[1];
    /* Only read a 3rd lookahead byte when c0/c1 are BOTH confirmed
     * non-NUL -- this file's buffers only guarantee 2 trailing zero
     * bytes past real content (see php_run_statement's own "require"
     * handling and sqs_main.c's matching "+2" allocation comment for
     * why), so blindly reading st->src[2] whenever st->src sits near the
     * true end of a buffer would be a real one-byte-past-the-guaranteed-
     * safe-region overread -- the exact bug class this file's own top
     * comment already documents fuzzing having found elsewhere. */
    char c2 = (c0 && c1) ? st->src[2] : 0;
    int result;
    int matched = 1;
    if (c0 == '=' && c1 == '=' && c2 == '=') {
        /* "===" (strict equality) -- a REAL, previously-undiscovered
         * parsing bug confirmed this session: without this 3-char check,
         * "===" fell into the "==" branch below, which consumes only 2
         * of its 3 '=' characters and then evaluates the RHS starting
         * from the leftover THIRD '=' -- not a valid expression start,
         * so php_eval_factor's fallback returns "" without advancing,
         * silently corrupting the right-hand operand into an empty
         * string instead of the real value (confirmed via wp-includes/
         * template.php's own "if ( $load && '' !== $located )" inside
         * locate_template(): the stray leftover '=' from a misparsed
         * "!==" made "'' !== $located" always evaluate false regardless
         * of $located's real value, so load_template() -- the call that
         * actually require_once()s header.php/footer.php/sidebar.php/
         * any get_template_part() -- silently NEVER ran, no matter how
         * correctly the rest of theme resolution worked). This subset
         * has no real type system to make "===" behave differently from
         * "==" (every value is a plain string, or an array/object
         * reference token -- see this file's top comment), so treating
         * them identically (plain strcmp) is the correct, not merely
         * expedient, semantics here -- the bug was purely the missing
         * 3-character lookahead breaking the PARSE, not a missing
         * strict-vs-loose behavioral distinction. */
        st->src += 3;
        char rhs[PHP_VAL_MAX];
        php_eval_instanceof(st, rhs, sizeof rhs);
        result = strcmp(lhs, rhs) == 0;
    } else if (c0 == '!' && c1 == '=' && c2 == '=') {
        /* "!==" (strict inequality) -- see "===" branch's own comment;
         * the exact same 3-vs-2-character bug, just for "!=". */
        st->src += 3;
        char rhs[PHP_VAL_MAX];
        php_eval_instanceof(st, rhs, sizeof rhs);
        result = strcmp(lhs, rhs) != 0;
    } else if (c0 == '=' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_instanceof(st, rhs, sizeof rhs);
        result = strcmp(lhs, rhs) == 0;
    } else if (c0 == '!' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_instanceof(st, rhs, sizeof rhs);
        result = strcmp(lhs, rhs) != 0;
    } else if (c0 == '<' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_instanceof(st, rhs, sizeof rhs);
        result = php_to_num(lhs) <= php_to_num(rhs);
    } else if (c0 == '>' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_instanceof(st, rhs, sizeof rhs);
        result = php_to_num(lhs) >= php_to_num(rhs);
    } else if (c0 == '<') {
        st->src += 1;
        char rhs[PHP_VAL_MAX];
        php_eval_instanceof(st, rhs, sizeof rhs);
        result = php_to_num(lhs) < php_to_num(rhs);
    } else if (c0 == '>') {
        st->src += 1;
        char rhs[PHP_VAL_MAX];
        php_eval_instanceof(st, rhs, sizeof rhs);
        result = php_to_num(lhs) > php_to_num(rhs);
    } else {
        matched = 0;
        result = 0;
    }
    if (matched) strncpy(out, result ? "1" : "0", outcap - 1), out[outcap - 1] = 0;
    else { strncpy(out, lhs, outcap - 1); out[outcap - 1] = 0; }
}

/* and := cmp ("&&" cmp)* -- evaluates (and parses) BOTH sides always
 * (no short-circuit skip-without-evaluating -- this single-pass
 * interpreter would need a real "parse but don't execute" mode to skip
 * evaluating an unneeded right-hand side, which is more machinery than
 * this subset's scope justifies yet; see this file's top comment). Fine
 * for read-only conditions like "file_exists(...) && is_dir(...)"; NOT
 * fine for a right-hand side relied on for a side effect that should be
 * skipped (e.g. "$done || do_the_thing()") -- a real limitation, not
 * modeled here. */
static void php_eval_and(PhpState *st, char *out, int outcap) {
    char acc[PHP_VAL_MAX];
    php_eval_cmp(st, acc, sizeof acc);
    for (;;) {
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (c0 == '&' && c1 == '&') {
            st->src += 2;
            char rhs[PHP_VAL_MAX];
            php_eval_cmp(st, rhs, sizeof rhs);
            int r = php_truthy(acc) && php_truthy(rhs);
            strncpy(acc, r ? "1" : "0", sizeof acc - 1); acc[sizeof acc - 1] = 0;
        } else break;
    }
    strncpy(out, acc, outcap - 1); out[outcap - 1] = 0;
}

/* or := and ("||" and)* -- see php_eval_and's own comment on the lack of
 * true short-circuit evaluation; the same limitation applies here. */
static void php_eval_or(PhpState *st, char *out, int outcap) {
    char acc[PHP_VAL_MAX];
    php_eval_and(st, acc, sizeof acc);
    for (;;) {
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (c0 == '|' && c1 == '|') {
            st->src += 2;
            char rhs[PHP_VAL_MAX];
            php_eval_and(st, rhs, sizeof rhs);
            int r = php_truthy(acc) || php_truthy(rhs);
            strncpy(acc, r ? "1" : "0", sizeof acc - 1); acc[sizeof acc - 1] = 0;
        } else break;
    }
    strncpy(out, acc, outcap - 1); out[outcap - 1] = 0;
}

/* expr := or ("?" expr ":" expr)? -- the public entry point every other
 * part of this file calls; see php_eval_addsub's own comment on why the
 * full boolean precedence chain lives above the old arithmetic/
 * concatenation level instead of being a separate "condition-only"
 * parser. The ternary is right-recursive (calls php_eval_expr, not
 * php_eval_or, for both branches), matching real PHP's own right
 * associativity for chained "a ? b : c ? d : e"; only the FALSE branch
 * is where that recursion actually matters since the true branch is
 * always terminated by a mandatory ":" first.
 *
 * Getting this right matters beyond just ternaries reading correctly:
 * before this existed, a ternary anywhere left the parser sitting on the
 * unconsumed "? ... : ..." tail, which cascaded into misparsing
 * everything after it as PHP code (confirmed as a real bug this
 * session, not hypothetical -- real WordPress source uses ternaries
 * constantly). php_run_statements' own stuck-guard (see its comment)
 * exists as a backstop for whatever the NEXT unsupported construct like
 * this turns out to be, but fixing the construct itself, like this, is
 * always the better fix when it's practical. */
/* Recursion-depth guard shared by every php_eval_expr() call -- found
 * this session via a real, confirmed gcc-vs-squash divergence: the
 * SAME real WordPress page (SQW/testpages/wordpress-develop's own
 * index.php through the twentyseventeen theme) that rendered correctly
 * end-to-end under a gcc-compiled build of this file SEGFAULTED under
 * the identical source compiled with squash -- confirmed via gdb to be
 * a genuine stack overflow (crash rsp landed just past the mapped
 * stack's low end), and confirmed via raising the process's stack
 * ulimit all the way to 1GB (still crashed identically) that this is
 * NOT merely "needs a bit more stack" but effectively RUNAWAY recursion
 * through the expression evaluator (php_eval_expr -> ... ->
 * php_resolve_varref -> php_eval_expr -> ..., for an isset()/empty()/
 * count()/is_array() call whose argument is itself a "$arr[KEY]"
 * subscript expression) -- gcc's much smaller/more-optimized per-frame
 * stack usage apparently tolerated whatever real depth this specific
 * page's real markup reaches, while squash's larger, unoptimized
 * per-local stack frames did not, which is what surfaced this as a
 * squash-specific crash even though the underlying deep-recursion risk
 * exists in BOTH builds. Rather than chase the exact expression shape
 * that reaches this depth (a moving target across different real-world
 * PHP source, and not this session's only priority), this applies the
 * SAME "generous bound, safe degradation" convention already used
 * throughout this file for every other unbounded-execution risk (see
 * PHP_LOOP_MAX's own comment) directly at php_eval_expr's own top-level
 * entry point -- the single most-recursed-into function in the whole
 * evaluator, so guarding here catches a runaway chain through ANY path
 * (ternary self-recursion, varref key-expression recursion, a future
 * one not yet found), not just the one path found this session. A
 * request that hits this ceiling gets a wrong/truncated expression
 * result instead of crashing the whole server process -- exactly the
 * same tradeoff this file already makes everywhere else. */
#define PHP_EXPR_DEPTH_MAX 300
static int g_expr_depth = 0;
static void php_eval_expr(PhpState *st, char *out, int outcap) {
    if (g_expr_depth >= PHP_EXPR_DEPTH_MAX) { out[0] = 0; return; }
    g_expr_depth++;
    char cond[PHP_VAL_MAX];
    php_eval_or(st, cond, sizeof cond);
    php_skip_ws(st);
    char c = *st->src;
    if (c == '?') {
        st->src++;
        char tval[PHP_VAL_MAX], fval[PHP_VAL_MAX];
        php_eval_expr(st, tval, sizeof tval);
        php_skip_ws(st);
        c = *st->src;
        if (c == ':') st->src++;
        php_eval_expr(st, fval, sizeof fval);
        strncpy(out, php_truthy(cond) ? tval : fval, outcap - 1); out[outcap - 1] = 0;
        g_expr_depth--;
        return;
    }
    strncpy(out, cond, outcap - 1); out[outcap - 1] = 0;
    g_expr_depth--;
}

/* If `p` is sitting on a string literal ('...'/"..." with real \-escape
 * handling), a "//"/"#" line comment, or a "/* ... *‍/" block comment,
 * returns the position right past the WHOLE thing in one step; otherwise
 * returns `p` unchanged (caller advances one plain character itself).
 * Every depth-counting brace/paren skip below calls this before looking
 * at '{'/'}'/'('/')' -- without it, a '{' or '}' CHARACTER INSIDE A
 * STRING LITERAL gets miscounted as a real brace and desyncs the whole
 * skip. Confirmed as a real, actively-triggered bug this session, not a
 * theoretical one: real WordPress's own is_serialized()
 * (wp-includes/functions.php) contains the literal single-quoted string
 * "'}'" (checking whether a character equals '}') partway through its
 * body; php_skip_to_brace_close() (used to skip PAST a function
 * definition's body without executing it, recording only where it
 * starts) miscounted that quoted '}' as the function's own real closing
 * brace, stopped there -- mid-function -- and let the REMAINING
 * statements of is_serialized()'s body leak out and execute as
 * top-level file-scope code, including a top-level "return false;" that
 * silently ended the ENTIRE script's execution right there (this is
 * exactly what a real WordPress test-page run was blocked on when this
 * was found: no crash, no error, just empty output with no explanation
 * until traced with gdb). */
static const char *php_skip_atomic(const char *p) {
    char c = *p;
    if (c == '\'' || c == '"') {
        char q = c;
        p++;
        c = *p;
        while (c && c != q) {
            if (c == '\\' && p[1]) { p += 2; c = *p; continue; }
            p++;
            c = *p;
        }
        if (c == q) p++;
        return p;
    }
    if (c == '/' && p[1] == '/') {
        p += 2;
        c = *p;
        while (c && c != '\n') { p++; c = *p; }
        return p;
    }
    if (c == '#') {
        p++;
        c = *p;
        while (c && c != '\n') { p++; c = *p; }
        return p;
    }
    if (c == '/' && p[1] == '*') {
        p += 2;
        char c2 = *p, c3 = p[1];
        while (c2 && !(c2 == '*' && c3 == '/')) { p++; c2 = *p; c3 = p[1]; }
        if (c2) p += 2;
        return p;
    }
    if (c == '?' && p[1] == '>') {
        /* Real PHP's alternate template syntax: "?>" drops OUT of PHP
         * code into raw HTML/CSS/JS passthrough until the next
         * "<?php"/"<?" (or EOF) -- see php_enter_html_passthrough's own
         * comment for the execution-side twin of this (which actually
         * EMITS the HTML; this is the skip-side twin, used when
         * recording a function/class body's extent WITHOUT executing
         * it, so nothing gets emitted here). Skipped as one atomic unit
         * for the exact same reason a string literal is: the HTML in
         * between can contain its own '{'/'}' characters (a raw
         * "<style>selector { ... }</style>" block, confirmed as a real
         * bug this session via real WordPress's own
         * _default_wp_die_handler()) that must never be mistaken for
         * PHP code structure by the depth-counting skip loops that call
         * this. */
        p += 2;
        const char *open_long = strstr(p, "<?php");
        const char *open_short = strstr(p, "<?");
        const char *open = open_long ? open_long : open_short;
        if (open && open_short && (!open_long || open_short < open_long)) open = open_short;
        if (!open) return p + strlen(p);
        p = open;
        if (strncmp(p, "<?php", 5) == 0) p += 5;
        else p += 2;
        return p;
    }
    return p;
}

/* Advances st->src past a "{...}" block whose opening '{' has ALREADY
 * been consumed (st->src is the first character inside it), tracking
 * nested braces, WITHOUT executing anything. Used both to skip an
 * unexecuted if/else branch and to fast-forward past the remainder of a
 * block after a "return" statement has already fired partway through it
 * (real execution up to that point kept brace depth balanced, so this
 * still lands on the correct matching '}'). Scans on a local `p`, see
 * this file's top comment. Leaves st->src pointing AT the matching '}'
 * (not past it) -- same convention php_run_statements' own "c0=='}'"
 * early-return already uses, so callers handle consuming it uniformly. */
static void php_skip_to_brace_close(PhpState *st) {
    const char *p = st->src;
    int depth = 1;
    for (;;) {
        char pc = *p;
        if (!pc || depth <= 0) break;
        const char *after = php_skip_atomic(p);
        if (after != p) { p = after; continue; }
        if (pc == '{') depth++;
        else if (pc == '}') depth--;
        if (depth > 0) p++;
    }
    st->src = p;
}

/* Advances st->src forward PAST one bare, non-brace statement (e.g. the
 * body of "if (cond) return foo();" with no "{...}") WITHOUT executing
 * it -- scans atomically (string/comment-aware, via php_skip_atomic, same
 * as php_skip_to_brace_close/php_skip_to_paren_close) for the first ';'
 * that sits at paren/bracket/brace depth 0. This correctly handles a
 * nested unbraced control structure too ("if (x) if (y) foo();" -- the
 * inner statement's own terminating ';' is also, transitively, the ONE
 * depth-0 ';' for the whole outer statement, so a single scan naturally
 * covers arbitrary nesting without this function needing to understand
 * "if"/"while"/etc. as keywords at all) as well as a body that opens a
 * real brace block partway through (an inline anonymous function/array
 * literal) via the same depth counter. Leaves st->src just PAST the
 * consumed ';' (or at EOF, degrading safely, if none is found). */
static void php_skip_one_statement(PhpState *st) {
    const char *p = st->src;
    int depth = 0;
    for (;;) {
        char pc = *p;
        if (!pc) break;
        const char *after = php_skip_atomic(p);
        if (after != p) { p = after; continue; }
        if (pc == '(' || pc == '[' || pc == '{') depth++;
        else if (pc == ')' || pc == ']' || pc == '}') { if (depth > 0) depth--; }
        else if (pc == ';' && depth == 0) { p++; break; }
        p++;
    }
    st->src = p;
}

/* Advances st->src forward to the ')' matching the '(' that logically
 * opened the current position (i.e. depth 0 relative to here), tracking
 * nested parens, WITHOUT executing anything -- a defensive resync used
 * when an "if (...)" condition expression didn't land exactly on the
 * closing ')' it should have (see the "if" handling's own comment).
 * Leaves st->src pointing AT the matching ')' (not past it), same
 * convention php_skip_to_brace_close uses. Scans on a local `p`, see
 * this file's top comment. */
static void php_skip_to_paren_close(PhpState *st) {
    const char *p = st->src;
    int depth = 0;
    for (;;) {
        char pc = *p;
        if (!pc) break;
        const char *after = php_skip_atomic(p);
        if (after != p) { p = after; continue; }
        if (pc == '(') depth++;
        else if (pc == ')') {
            if (depth == 0) break;
            depth--;
        }
        p++;
    }
    st->src = p;
}

/* Forward declarations -- php_skip_clause_chain() and php_skip_alt_block()
 * below are mutually recursive (a nested "if"/"while"/"for"/"foreach"
 * found while skipping one of these needs the OTHER to fully skip past
 * it before this scan resumes). */
static void php_skip_clause_chain(PhpState *st, const char *kwid);
static int php_skip_alt_block(PhpState *st, const char **stopkws, int nstop);

/* Advances st->src past an alt-syntax (":" ... "endif;"/"endwhile;"/...)
 * BODY, stopping (WITHOUT consuming it) at whichever of `stopkws` is
 * reached next at THIS body's own level -- the skip-only (non-executing)
 * counterpart of php_run_statements_alt() (see that function's own
 * comment for the full story on why this syntax needs its own scanner at
 * all: it has no "}" to balance-skip against the way php_skip_to_brace_
 * close() does for brace-form bodies). Used to skip an UNTAKEN alt-if/
 * alt-else branch, or an unrecognized bare "{...}" block, without
 * running any side effects. A nested control structure -- in EITHER
 * brace or alt-syntax form, any of if/elseif/while/for/foreach/else --
 * is fully skipped via php_skip_clause_chain() before this loop's own
 * scan resumes, so only a keyword genuinely belonging to OUR OWN body
 * can ever be seen here; no manual nesting-depth counter is needed.
 * Returns the matched stopkws index, or -1 on EOF (malformed input --
 * degrades safely rather than hanging, matching every other skip
 * function in this file). */
static int php_skip_alt_block(PhpState *st, const char **stopkws, int nstop) {
    for (;;) {
        php_skip_ws(st);
        char c = *st->src;
        if (!c) return -1;
        if (c == '{') {
            /* A brace block with no recognized keyword just before it
             * isn't valid PHP either way -- treat it as an opaque
             * balanced unit, same safety-first degradation this file
             * uses everywhere else for unrecognized-but-structured
             * input. */
            st->src++;
            php_skip_to_brace_close(st);
            if (*st->src == '}') st->src++;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            const char *save = st->src;
            char id[16];
            php_read_ident(st, id, sizeof id);
            int i;
            for (i = 0; i < nstop; i++) if (strcmp(id, stopkws[i]) == 0) { st->src = save; return i; }
            if (strcmp(id, "if") == 0 || strcmp(id, "while") == 0 || strcmp(id, "for") == 0 || strcmp(id, "foreach") == 0) {
                php_skip_clause_chain(st, id);
            }
            /* Any other bare identifier (a function call, a constant,
             * "echo", ...) isn't structurally significant to this scan
             * -- already consumed by php_read_ident, just resume. */
            continue;
        }
        {
            const char *after = php_skip_atomic(st->src);
            if (after != st->src) st->src = after;
            else st->src++;
        }
    }
}

/* Skips one FULL control-structure clause chain WITHOUT executing
 * anything -- `kwid` is the keyword that already introduced it ("if",
 * "elseif", "while", "for", "foreach"), with st->src positioned right
 * after that keyword (before its header). Handles both a brace "{...}"
 * body and an alternative ":" ... "endif;"/"endwhile;"/... body,
 * chaining through any "elseif"/"else" continuations for the if-family
 * exactly the way the real EXECUTING "if" statement handler does (just
 * skip-only) -- used both by php_skip_alt_block() above (to skip a
 * nested construct found inside a body being scanned) and directly by
 * the executing "if"/"while"/"foreach" handlers themselves (to skip an
 * UNTAKEN branch/an already-exhausted loop's remaining body). */
static void php_skip_clause_chain(PhpState *st, const char *kwid) {
    int is_if_family = kwid && (strcmp(kwid, "if") == 0 || strcmp(kwid, "elseif") == 0);
    if (kwid) {
        php_skip_ws(st);
        if (*st->src == '(') {
            st->src++;
            php_skip_to_paren_close(st);
            if (*st->src == ')') st->src++;
        }
    }
    php_skip_ws(st);
    char c = *st->src;
    if (c == '{') {
        st->src++;
        php_skip_to_brace_close(st);
        if (*st->src == '}') st->src++;
        if (!is_if_family) return;
        php_skip_ws(st);
        const char *save = st->src;
        char kw2[16];
        php_read_ident(st, kw2, sizeof kw2);
        if (strcmp(kw2, "elseif") == 0) { php_skip_clause_chain(st, "elseif"); return; }
        if (strcmp(kw2, "else") == 0) {
            php_skip_ws(st);
            const char *save2 = st->src;
            char kw3[16];
            php_read_ident(st, kw3, sizeof kw3);
            if (strcmp(kw3, "if") == 0) { php_skip_clause_chain(st, "elseif"); return; }
            st->src = save2;
            php_skip_clause_chain(st, NULL);
            return;
        }
        st->src = save;
        return;
    }
    if (c == ':') {
        st->src++;
        const char *stopkws_if[3]; stopkws_if[0] = "elseif"; stopkws_if[1] = "else"; stopkws_if[2] = "endif";
        const char *stopkws_one[1];
        const char **stopkws; int nstop;
        if (is_if_family) { stopkws = stopkws_if; nstop = 3; }
        else if (!kwid) { stopkws_one[0] = "endif"; stopkws = stopkws_one; nstop = 1; } /* bodiless "else" alt body */
        else if (strcmp(kwid, "while") == 0) { stopkws_one[0] = "endwhile"; stopkws = stopkws_one; nstop = 1; }
        else if (strcmp(kwid, "for") == 0) { stopkws_one[0] = "endfor"; stopkws = stopkws_one; nstop = 1; }
        else { stopkws_one[0] = "endforeach"; stopkws = stopkws_one; nstop = 1; }
        int which = php_skip_alt_block(st, stopkws, nstop);
        if (which < 0) return; /* EOF: malformed, degrade safely */
        char matched[16];
        php_read_ident(st, matched, sizeof matched); /* consume the matched keyword itself */
        if (is_if_family && strcmp(matched, "elseif") == 0) { php_skip_clause_chain(st, "elseif"); return; }
        if (is_if_family && strcmp(matched, "else") == 0) {
            php_skip_ws(st);
            const char *save2 = st->src;
            char kw3[16];
            php_read_ident(st, kw3, sizeof kw3);
            if (strcmp(kw3, "if") == 0) { php_skip_clause_chain(st, "elseif"); return; }
            st->src = save2;
            php_skip_clause_chain(st, NULL);
            return;
        }
        /* matched is the real terminator (endif/endwhile/endfor/endforeach) */
        php_skip_ws(st);
        if (*st->src == ';') st->src++;
        return;
    }
    /* malformed: no body at all -- nothing sane to skip */
}

/* Skips an optional PHP 7+ return-type declaration -- "): string {",
 * "): ?array {", "): int|false {", "): self {", "): void;" (an
 * interface/abstract method with no body) -- st->src is expected to be
 * positioned right after a function/method's closing ")" (php_skip_ws
 * already applied). Does nothing if there's no ":" there (most
 * functions in real-world code still have no return type). A missing
 * skip here isn't a cosmetic gap: without it, "function foo(): string
 * {" left st->src sitting at ":" where neither the "{" nor ";" branch
 * that decides whether/how to record the function matched -- the
 * function silently never got registered, AND (worse) parsing carried
 * on from ":" as if it were the start of the NEXT class member, which
 * badly desynced everything after it (confirmed as the actual root
 * cause of a real WordPress class -- wp-includes/l10n/class-wp-
 * translation-file-php.php's own private var_export(): string method --
 * silently corrupting the rest of that file's parsing and leaking a
 * stray "}" into the page's own HTML output, the exact bug this
 * session's WordPress test run was chasing when this was found). Only
 * a bare type expression is meaningful here (letters/digits/"_"/"\\"
 * for a namespaced class name/"|"/"&" for union/intersection types/"?"
 * for nullable) -- consumed and discarded either way, since return
 * types aren't enforced in this subset. */
static void php_skip_return_type(PhpState *st) {
    char c = *st->src;
    if (c != ':') return;
    st->src++;
    php_skip_ws(st);
    for (;;) {
        c = *st->src;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '\\' || c == '|' || c == '&' || c == '?') { st->src++; continue; }
        if (c == ' ' || c == '\t') { st->src++; continue; }
        break;
    }
}

/* Parses "class Name [extends Base] [implements X, Y] { ... }" for
 * real -- registering it in g_classes -- instead of the inert
 * balanced-brace skip every other not-yet-supported construct
 * (interface/trait/enum/try/switch/loops/...) still gets. "extends"/
 * "implements" are recognized and their target name(s) consumed (so
 * parsing stays in sync) but NOT modeled -- no inherited
 * properties/methods, no interface conformance checking (this subset
 * has no polymorphism at all; a method lookup only ever checks the
 * object's own exact class, see php_class_find_method's own call
 * sites). A property declaration ("[visibility] [static] $name [=
 * default];") records the property name and its default value (only a
 * simple literal/expression default is meaningful -- evaluated once
 * here as PLAIN text, not against any object's own state, since no
 * object exists yet); a method declaration ("[visibility] [static]
 * function name(...) { ... }") records it the same way php_run_statement's
 * own top-level "function" handling already does for a plain function,
 * just into this class's own method table instead of the global
 * g_funcs one. A PHP 8 "#[Attribute(...)]" is skipped as a whole
 * bracket-matched block (never meaningful here). Anything else inside
 * the body that isn't recognized (e.g. "const NAME = ...;") is skipped
 * to its own next ';' -- the same safe-degradation convention used
 * throughout this file. */
static void php_parse_class_decl(PhpState *st) {
    php_skip_ws(st);
    char cname[64];
    php_read_ident(st, cname, sizeof cname);
    php_skip_ws(st);
    char pname[64]; pname[0] = 0; /* first "extends" target, if any -- see PhpClass.parent_name's own comment */
    for (;;) {
        const char *save = st->src;
        char kw2[16];
        php_read_ident(st, kw2, sizeof kw2);
        if (strcmp(kw2, "extends") == 0 || strcmp(kw2, "implements") == 0) {
            int first = 1;
            php_skip_ws(st);
            for (;;) {
                char tmp[64];
                php_read_ident(st, tmp, sizeof tmp);
                if (strcmp(kw2, "extends") == 0 && first) {
                    strncpy(pname, tmp, sizeof pname - 1); pname[sizeof pname - 1] = 0;
                }
                first = 0;
                php_skip_ws(st);
                char c2 = *st->src;
                if (c2 == ',') { st->src++; php_skip_ws(st); continue; }
                break;
            }
            php_skip_ws(st);
            continue;
        }
        st->src = save;
        break;
    }
    char c = *st->src;
    PhpClass *cls = NULL;
    if (g_nclasses < PHP_CLASS_MAX) {
        cls = &g_classes[g_nclasses];
        memset(cls, 0, sizeof *cls);
        strncpy(cls->name, cname, sizeof cls->name - 1); cls->name[sizeof cls->name - 1] = 0;
        strncpy(cls->parent_name, pname, sizeof cls->parent_name - 1); cls->parent_name[sizeof cls->parent_name - 1] = 0;
    }
    if (c != '{') return;
    st->src++;
    php_skip_ws(st);
    c = *st->src;
    while (c && c != '}') {
        /* Visibility/modifier keywords -- consumed, not modeled (no
         * access-control enforcement in this subset). */
        for (;;) {
            const char *save2 = st->src;
            char mkw[16];
            php_read_ident(st, mkw, sizeof mkw);
            if (strcmp(mkw, "public") == 0 || strcmp(mkw, "private") == 0 || strcmp(mkw, "protected") == 0 ||
                strcmp(mkw, "static") == 0 || strcmp(mkw, "final") == 0 || strcmp(mkw, "abstract") == 0 ||
                strcmp(mkw, "readonly") == 0) {
                php_skip_ws(st);
                continue;
            }
            st->src = save2;
            break;
        }
        php_skip_ws(st);
        c = *st->src;
        if (c == '#') {
            st->src++;
            php_skip_ws(st);
            c = *st->src;
            if (c == '[') {
                st->src++;
                int depth = 1;
                char pc = *st->src;
                while (pc && depth > 0) {
                    if (pc == '[') depth++;
                    else if (pc == ']') depth--;
                    if (depth > 0) { st->src++; pc = *st->src; }
                }
                if (*st->src == ']') st->src++;
            }
            php_skip_ws(st);
            c = *st->src;
            continue;
        }
        if (c == '$') {
            st->src++;
            char pname[64];
            php_read_ident(st, pname, sizeof pname);
            php_skip_ws(st);
            c = *st->src;
            char defval[PHP_VAL_MAX]; defval[0] = 0;
            if (c == '=') {
                st->src++;
                php_eval_expr(st, defval, sizeof defval);
                php_skip_ws(st);
                c = *st->src;
            }
            if (c == ';') st->src++;
            if (cls && cls->nprops < PHP_CLASS_PROP_MAX) {
                strncpy(cls->prop_names[cls->nprops], pname, sizeof cls->prop_names[cls->nprops] - 1);
                cls->prop_names[cls->nprops][sizeof cls->prop_names[cls->nprops] - 1] = 0;
                strncpy(cls->prop_defaults[cls->nprops], defval, sizeof cls->prop_defaults[cls->nprops] - 1);
                cls->prop_defaults[cls->nprops][sizeof cls->prop_defaults[cls->nprops] - 1] = 0;
                cls->nprops++;
            }
            php_skip_ws(st);
            c = *st->src;
            continue;
        }
        char mkw2[24];
        {
            const char *save3 = st->src;
            php_read_ident(st, mkw2, sizeof mkw2);
            if (mkw2[0] == 0) { st->src = save3; st->src++; php_skip_ws(st); c = *st->src; continue; }
        }
        if (strcmp(mkw2, "function") == 0) {
            php_skip_ws(st);
            char fname[64];
            php_read_ident(st, fname, sizeof fname);
            php_skip_ws(st);
            c = *st->src;
            PhpMethod *m = (cls && cls->nmethods < PHP_CLASS_METHOD_MAX) ? &cls->methods[cls->nmethods] : NULL;
            int nparams = 0;
            if (c == '(') {
                st->src++;
                php_skip_ws(st);
                c = *st->src;
                while (c != 0 && c != ')') {
                    if (c == '$') {
                        st->src++;
                        char pname[64];
                        php_read_ident(st, pname, sizeof pname);
                        if (m && nparams < PHP_FUNC_PARAM_MAX) {
                            strncpy(m->params[nparams], pname, sizeof m->params[nparams] - 1);
                            m->params[nparams][sizeof m->params[nparams] - 1] = 0;
                            nparams++;
                        }
                        php_skip_ws(st);
                        c = *st->src;
                        if (c == '=') {
                            st->src++;
                            char dummy[PHP_VAL_MAX];
                            php_eval_expr(st, dummy, sizeof dummy);
                            php_skip_ws(st);
                            c = *st->src;
                        }
                    } else {
                        st->src++;
                    }
                    c = *st->src;
                    if (c == ',') { st->src++; php_skip_ws(st); c = *st->src; }
                }
                c = *st->src;
                if (c == ')') st->src++;
            }
            php_skip_ws(st);
            php_skip_return_type(st);
            php_skip_ws(st);
            c = *st->src;
            if (c == '{') {
                st->src++;
                if (m) {
                    strncpy(m->name, fname, sizeof m->name - 1); m->name[sizeof m->name - 1] = 0;
                    m->nparams = nparams;
                    m->body = st->src;
                    cls->nmethods++;
                }
                php_skip_to_brace_close(st);
                c = *st->src;
                if (c == '}') st->src++;
            } else if (c == ';') {
                st->src++; /* abstract/interface method declaration, no body */
            }
            php_skip_ws(st);
            c = *st->src;
            continue;
        }
        if (strcmp(mkw2, "const") == 0) {
            /* "const NAME = expr [, NAME2 = expr2 ...];" -- each constant
             * evaluated once here as plain text (same convention as a
             * property default above) and recorded into this class's own
             * const table, looked up later via php_class_find_const() for
             * "ClassName::NAME" (see the "::" handling in php_eval_factor).
             * No expression-level "self::OTHER_CONST" support in a const's
             * own default value (would need the enclosing class identity
             * threaded through php_eval_expr) -- rare enough in practice
             * to skip for now; such a default just evaluates to "" via the
             * ordinary unknown-bare-identifier fallback. */
            php_skip_ws(st);
            for (;;) {
                char cn[64];
                php_read_ident(st, cn, sizeof cn);
                php_skip_ws(st);
                c = *st->src;
                char cval[PHP_VAL_MAX]; cval[0] = 0;
                if (c == '=') {
                    st->src++;
                    php_eval_expr(st, cval, sizeof cval);
                    php_skip_ws(st);
                    c = *st->src;
                }
                if (cls && cls->nconsts < PHP_CLASS_CONST_MAX) {
                    strncpy(cls->const_names[cls->nconsts], cn, sizeof cls->const_names[cls->nconsts] - 1);
                    cls->const_names[cls->nconsts][sizeof cls->const_names[cls->nconsts] - 1] = 0;
                    strncpy(cls->const_vals[cls->nconsts], cval, sizeof cls->const_vals[cls->nconsts] - 1);
                    cls->const_vals[cls->nconsts][sizeof cls->const_vals[cls->nconsts] - 1] = 0;
                    cls->nconsts++;
                }
                if (c == ',') { st->src++; php_skip_ws(st); continue; }
                break;
            }
            c = *st->src;
            if (c == ';') st->src++;
            php_skip_ws(st);
            c = *st->src;
            continue;
        }
        /* Unrecognized member (use-trait, ...): skip to its own
         * next ';' or the class's closing '}' -- string/comment-aware
         * (php_skip_atomic), same reasoning as php_skip_to_brace_close's
         * own comment: a ';' or '}' INSIDE a quoted string (e.g. a
         * "const FOO = 'a;b';" default value) must not be mistaken for
         * the real one. */
        {
            const char *p = st->src;
            char pc = *p;
            while (pc && pc != ';' && pc != '}') {
                const char *after = php_skip_atomic(p);
                if (after != p) { p = after; pc = *p; continue; }
                p++; pc = *p;
            }
            if (pc == ';') p++;
            st->src = p;
        }
        php_skip_ws(st);
        c = *st->src;
    }
    c = *st->src;
    if (c == '}') st->src++;
    if (cls) g_nclasses++;
}

static void php_run_statements(PhpState *st); /* forward: if-bodies and function calls recurse */
static void php_run_statement(PhpState *st); /* forward: php_run_statements_alt below recurses through this */
static int php_run_statements_alt(PhpState *st, const char **stopkws, int nstop); /* forward: alt-syntax if/while/for/foreach control structures below */
void php_run_source(PhpState *st, const char *source); /* forward: require/include recurse into this */

/* Runs ONE assignment/increment/decrement/bare-expression, for its side
 * effect only (the resulting value, if any, is discarded) -- does NOT
 * consume a trailing ';'/','/')' , unlike php_run_statement (the caller
 * decides what follows). Written for "for (init; cond; step)"'s own
 * init/step clauses, which are exactly this shape ("$i = 0", "$i++",
 * "$i = $i + 1") but are NOT full ";"-terminated statements the way
 * php_run_statement expects -- before this existed, the "for" loop
 * handler tried to evaluate them with the plain read-only php_eval_expr,
 * which has no idea what "=" (an assignment) or "++"/"--" even are: it
 * silently stopped at the first character it didn't recognize, leaving
 * st->src desynced mid-clause and corrupting everything parsed
 * afterward (confirmed as a real bug this session -- "for ($j=0; $j<5;
 * $j=$j+1) { ... }" derailed so badly that not even the ECHO statement
 * AFTER the whole loop ever ran). Also fills a real, separate gap this
 * uncovered: "++"/"--"/"+="/"-="/"." = had NO support anywhere in this
 * file before, not even as an ordinary top-level statement -- real
 * WordPress code uses "$i++;"/"$count += ...;" constantly, so
 * php_run_statement's own "$" dispatch now tries this helper FIRST too
 * (see its own call site). */
static void php_run_expr_stmt(PhpState *st) {
    php_skip_ws(st);
    const char *start = st->src;
    char c = *st->src;
    if ((c == '+' && st->src[1] == '+') || (c == '-' && st->src[1] == '-')) {
        int inc = (c == '+');
        st->src += 2;
        php_skip_ws(st);
        if (*st->src == '$') {
            st->src++;
            char name[64];
            php_read_ident(st, name, sizeof name);
            PhpVar *v = php_var_find_or_create(st, name);
            if (v) {
                double n = php_to_num(v->val) + (inc ? 1 : -1);
                char buf[64]; php_num_to_str(n, buf, sizeof buf);
                php_var_set(st, name, buf);
            }
            return;
        }
        st->src = start; /* wasn't really "++$x"/"--$x" -- fall through below */
    }
    if (*st->src == '$') {
        st->src++;
        char name[64];
        php_read_ident(st, name, sizeof name);
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (c0 == '+' && c1 == '+') {
            st->src += 2;
            PhpVar *v = php_var_find_or_create(st, name);
            if (v) { double n = php_to_num(v->val) + 1; char buf[64]; php_num_to_str(n, buf, sizeof buf); php_var_set(st, name, buf); }
            return;
        }
        if (c0 == '-' && c1 == '-') {
            st->src += 2;
            PhpVar *v = php_var_find_or_create(st, name);
            if (v) { double n = php_to_num(v->val) - 1; char buf[64]; php_num_to_str(n, buf, sizeof buf); php_var_set(st, name, buf); }
            return;
        }
        if ((c0 == '+' || c0 == '-' || c0 == '.') && c1 == '=') {
            char op = c0;
            st->src += 2;
            php_skip_ws(st);
            char rhs[PHP_VAL_MAX];
            php_eval_expr(st, rhs, sizeof rhs);
            PhpVar *v = php_var_find_or_create(st, name);
            if (v) {
                char buf[PHP_VAL_MAX];
                if (op == '.') snprintf(buf, sizeof buf, "%s%s", v->val, rhs);
                else { double n = php_to_num(v->val) + (op == '+' ? php_to_num(rhs) : -php_to_num(rhs)); php_num_to_str(n, buf, sizeof buf); }
                php_var_set(st, name, buf);
            }
            return;
        }
        if (c0 == '=' && c1 != '=') {
            /* Plain "$name = expr" or an lvalue chain ("$name[key]=...",
             * "$name->prop=..."): reuse the exact same machinery
             * php_run_statement's own "$" dispatch uses, rewinding to
             * "$name" first since php_resolve_lvalue_chain expects to
             * start right after the identifier with `c` as its own
             * lookahead. */
            st->src = start;
            st->src++;
            char name2[64];
            php_read_ident(st, name2, sizeof name2);
            php_skip_ws(st);
            char cc = *st->src;
            PhpKVArray *container = NULL;
            char key[128];
            if (php_resolve_lvalue_chain(st, name2, cc, &container, key, sizeof key)) {
                php_skip_ws(st);
                if (st->src[0] == '=' && st->src[1] != '=') {
                    st->src++;
                    php_lvalue_assign(st, container, key);
                    return;
                }
            }
            st->src = start;
            st->src++;
            php_read_ident(st, name2, sizeof name2);
            php_skip_ws(st);
            if (st->src[0] == '=' && st->src[1] != '=') {
                st->src++;
                php_skip_ws(st);
                char val[PHP_VAL_MAX];
                php_eval_expr(st, val, sizeof val);
                php_var_set(st, name2, val);
                return;
            }
        }
        st->src = start; /* not an assignment/increment after all */
    }
    char tmp[PHP_VAL_MAX];
    php_eval_expr(st, tmp, sizeof tmp);
}

/* One statement: "$var = expr;" | "echo expr (, expr)* ;" | "print expr ;"
 * | "if (cond) { stmts } [else { stmts }]" (see this file's top comment:
 * the if/else body must stay inside one continuous php block, no
 * exiting/re-entering "<?php"/"?>" mid-block) | "function name($a, $b) {
 * ... }" | "return [expr];" | "require[_once]/include[_once] expr;" | a
 * bare function-call expression statement, e.g. "some_func();". */
static void php_run_statement(PhpState *st) {
    php_skip_ws(st);
    char c = *st->src;
    if ((c == '+' && st->src[1] == '+') || (c == '-' && st->src[1] == '-')) {
        /* Prefix "++$x;"/"--$x;" as a standalone statement -- see
         * php_run_expr_stmt's own comment on why this (and postfix
         * "$x++;"/compound "+="/"-="/".=") had no support anywhere in
         * this file before. */
        php_run_expr_stmt(st);
        php_skip_ws(st);
        if (*st->src == ';') st->src++;
        return;
    }
    if (c == '$') {
        /* Postfix "$x++;"/"$x--;" or compound "$x += ...;"/"-="/".=" as a
         * standalone statement -- peek past the identifier without
         * committing to it, so a PLAIN "$x = ...;"/"$x[...] = ...;"/bare-
         * expression statement (everything below) is completely
         * unaffected when none of these operators are actually there. */
        {
            const char *peek = st->src + 1;
            char pname[64]; int pi = 0;
            while ((peek[pi] == '_' || (peek[pi]>='a'&&peek[pi]<='z') || (peek[pi]>='A'&&peek[pi]<='Z') || (peek[pi]>='0'&&peek[pi]<='9')) && pi < (int)sizeof pname - 1) { pname[pi]=peek[pi]; pi++; }
            const char *after = peek + pi;
            while (*after==' '||*after=='\t') after++;
            if (pi > 0 && ((after[0]=='+'&&after[1]=='+') || (after[0]=='-'&&after[1]=='-') ||
                           ((after[0]=='+'||after[0]=='-'||after[0]=='.') && after[1]=='='))) {
                php_run_expr_stmt(st);
                php_skip_ws(st);
                if (*st->src == ';') st->src++;
                return;
            }
        }
        const char *save = st->src;
        st->src++;
        char name[64];
        php_read_ident(st, name, sizeof name);
        php_skip_ws(st);
        c = *st->src;

        /* "$GLOBALS['name'] = EXPR;" -- WordPress's own wp_cache_init()
         * does exactly this ("$GLOBALS['wp_object_cache'] = new
         * WP_Object_Cache();"), and 153 more sites across the vendored
         * tree use the same "$GLOBALS[...]" pattern as an alternative to
         * "global $name;" (real PHP treats them as two spellings of the
         * SAME underlying storage). Before this, "$GLOBALS" wasn't
         * recognized as anything special at all -- it fell through to
         * being an ordinary local variable named "GLOBALS", so an
         * assignment through it was completely invisible to every OTHER
         * function's own "global $name;" declaration of that same name.
         * Confirmed as a real, high-impact, previously-undiscovered gap
         * this session via SQS_TRACE_CALLS tracing showing ~1700+ method
         * calls landing on an empty (non-object) base value across the
         * real WordPress boot -- root cause traced to wp_cache_init()'s
         * $wp_object_cache never actually reaching any function that
         * later does "global $wp_object_cache;" to read it back. Routes
         * through the SAME shared g_globals[] table "global $name;"
         * already uses (php_global_find_or_create) -- see
         * php_global_lvalue_assign's own comment for why the RHS write
         * needs its own helper instead of reusing php_lvalue_assign's
         * PhpKVArray-container model. Only the single-level
         * "$GLOBALS['name']" form is handled (matches every real usage
         * found in the vendored tree); a further chained "$GLOBALS['x']
         * ->prop = 1;"-shaped WRITE is not (the READ side in
         * php_eval_factor's own "$GLOBALS" handling below does support
         * chaining past the first segment, same as any other variable). */
        if (strcmp(name, "GLOBALS") == 0 && c == '[') {
            st->src++;
            php_skip_ws(st);
            char key[128]; key[0] = 0;
            char cc = *st->src;
            if (cc != ']') {
                char kb[PHP_VAL_MAX];
                php_eval_expr(st, kb, sizeof kb);
                strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
                php_skip_ws(st);
            }
            cc = *st->src;
            if (cc == ']') st->src++;
            php_skip_ws(st);
            char c0 = st->src[0], c1 = st->src[1];
            if (c0 == '=' && c1 != '=') {
                st->src++;
                PhpVar *gv = key[0] ? php_global_find_or_create(key) : NULL;
                php_global_lvalue_assign(st, gv);
                php_skip_ws(st);
                c0 = *st->src;
                if (c0 == ';') st->src++;
                return;
            }
            /* No "=" after all (e.g. a bare "$GLOBALS['x'];" statement,
             * or "$GLOBALS['x']->method();") -- rewind to `save` and
             * handle it as an ordinary bare expression statement (NOT by
             * falling through into the generic lvalue-chain code just
             * below, which expects st->src positioned right after `name`
             * with `c` as its own fresh lookahead -- both would be stale/
             * inconsistent here since this branch already consumed the
             * "[...]" itself). php_eval_factor's own "$GLOBALS" read
             * support (added alongside this) handles the actual value/
             * chained "->method()" access. */
            st->src = save;
            char discard[PHP_VAL_MAX];
            php_eval_expr(st, discard, sizeof discard);
            php_skip_ws(st);
            c = *st->src;
            if (c == ';') st->src++;
            return;
        }

        /* Assignment through an lvalue chain: "$arr[key] = val;",
         * "$this->prop = val;", "$x[a][b] = val;", "$obj->prop[k] =
         * val;", any mix/depth thereof -- see php_resolve_lvalue_chain's
         * own comment for exactly what it handles (including
         * auto-vivifying a missing intermediate array, matching real
         * PHP's own "$x[a][b] = 1;" on a not-yet-existing $x[a]). Only
         * commits (returns) once a trailing "=" is actually confirmed;
         * otherwise falls through to the plain-scalar/bare-expression
         * handling below, which re-parses the WHOLE statement from
         * `save` and already walks these same chains correctly on the
         * read side (php_eval_factor) -- covers "$this->method();",
         * "$arr[key]->method();", a pointless-but-must-not-hang bare
         * "$arr[k1][k2];", etc. */
        {
            PhpKVArray *container = NULL;
            char key[128];
            if (php_resolve_lvalue_chain(st, name, c, &container, key, sizeof key)) {
                php_skip_ws(st);
                char c0 = st->src[0], c1 = st->src[1];
                if (c0 == '=' && c1 != '=') {
                    st->src++;
                    php_lvalue_assign(st, container, key);
                    php_skip_ws(st);
                    c0 = *st->src;
                    if (c0 == ';') st->src++;
                    return;
                }
            }
        }
        /* No lvalue-chain assignment after all: full rewind, re-read
         * `name` (cheap, no side effects), and handle the two remaining
         * shapes -- plain "$x = val;" (scalar or array-literal RHS) or
         * a bare expression statement. */
        st->src = save;
        st->src++;
        php_read_ident(st, name, sizeof name);
        php_skip_ws(st);
        c = *st->src;
        char c0 = st->src[0], c1 = st->src[1];
        if (c0 == '=' && c1 != '=') {
            st->src++;
            php_skip_ws(st);
            /* An array-literal RHS ("array(...)" or "[...]") needs its
             * own dedicated parse (php_parse_array_literal) instead of
             * the generic scalar php_eval_expr -- there's no way to
             * represent a whole array as the single flat string every
             * other expression in this file evaluates to. Peeking for
             * the "array" keyword consumes it on a false match too (a
             * bare identifier "array" used as some other kind of value
             * isn't meaningful in real PHP either), so it's always
             * rewound via `rhs_start` when it doesn't turn out to be
             * "array(". */
            const char *rhs_start = st->src;
            int handled = 0;
            {
                char maybe_kw[8];
                php_read_ident(st, maybe_kw, sizeof maybe_kw);
                php_skip_ws(st);
                char cc = *st->src;
                if (strcmp(maybe_kw, "array") == 0 && cc == '(') {
                    st->src++;
                    PhpVar *v = php_var_find_or_create(st, name);
                    if (v) { v->is_array = 1; php_parse_array_literal(st, &v->arr, ')', &v->next_index); }
                    else { PhpKVArray dummy; memset(&dummy, 0, sizeof dummy); php_parse_array_literal(st, &dummy, ')', NULL); }
                    handled = 1;
                } else {
                    st->src = rhs_start;
                }
            }
            if (!handled) {
                char cc = *st->src;
                if (cc == '[') {
                    st->src++;
                    PhpVar *v = php_var_find_or_create(st, name);
                    if (v) { v->is_array = 1; php_parse_array_literal(st, &v->arr, ']', &v->next_index); }
                    else { PhpKVArray dummy; memset(&dummy, 0, sizeof dummy); php_parse_array_literal(st, &dummy, ']', NULL); }
                } else {
                    char val[PHP_VAL_MAX];
                    php_eval_expr(st, val, sizeof val);
                    php_var_set(st, name, val);
                }
            }
        } else {
            /* Not actually an assignment -- e.g. a bare "$x;" statement,
             * or (see php_run_statements' own stuck-guard comment) a
             * corrupted parse position left over from an earlier
             * unsupported construct landing mid-expression on a "$name"
             * that isn't followed by "=". Re-parse from `save` as a
             * plain expression statement instead of just resetting and
             * returning: resetting-without-consuming here used to be a
             * genuine infinite loop (confirmed: php_run_statements would
             * call this function again, see the identical "$" at the
             * identical position, and repeat forever) whenever a
             * malformed/unsupported expression upstream left the parser
             * sitting on a lone "$var". */
            st->src = save;
            char discard[PHP_VAL_MAX];
            php_eval_expr(st, discard, sizeof discard);
        }
        php_skip_ws(st);
        c = *st->src;
        if (c == ';') st->src++;
        return;
    }
    const char *stmt_start = st->src;
    char kw[24];
    {
        const char *save = st->src;
        php_read_ident(st, kw, sizeof kw);
        if (kw[0] == 0) { st->src = save; st->src++; return; } /* unrecognized char: skip it, don't hang */
    }
    if (strcmp(kw, "echo") == 0 || strcmp(kw, "print") == 0) {
        for (;;) {
            char val[PHP_VAL_MAX];
            php_eval_expr(st, val, sizeof val);
            php_emit_str(st, val);
            php_skip_ws(st);
            c = *st->src;
            if (c == ',') { st->src++; continue; }
            break;
        }
        php_skip_ws(st);
        c = *st->src;
        if (c == ';') st->src++;
        return;
    }
    if (strcmp(kw, "return") == 0) {
        php_skip_ws(st);
        c = *st->src;
        if (c == ';') {
            st->retval[0] = 0;
        } else {
            php_eval_expr(st, st->retval, sizeof st->retval);
            php_skip_ws(st);
            c = *st->src;
        }
        if (c == ';') st->src++;
        st->returning = 1;
        return;
    }
    if (strcmp(kw, "class") == 0) {
        php_parse_class_decl(st);
        return;
    }
    if (strcmp(kw, "final") == 0 || strcmp(kw, "abstract") == 0) {
        /* "final class Foo {...}" / "abstract class Foo {...}" -- a
         * leading modifier means `kw` (this statement's first word)
         * isn't literally "class" even though it IS a real class
         * declaration, so the plain check just above never fires and
         * this would otherwise fall all the way through to the
         * unsupported-construct skip-safelist below (which still lists
         * "final"/"abstract" for the OTHER things they prefix, like a
         * method declaration inside an already-open class body) --
         * silently skipping the entire class as an inert block instead
         * of really parsing it. Real WordPress declares its own most
         * important class this way ("final class WP_Hook implements
         * Iterator, ArrayAccess { ... }", wp-includes/class-wp-hook.php)
         * -- confirmed as a real gap this session: without this check,
         * WP_Hook silently never actually got parsed by
         * php_parse_class_decl at all, defeating class support for
         * exactly the class that matters most for unblocking
         * apply_filters(). A second modifier ("abstract final" isn't
         * real PHP, but a stray repeated keyword shouldn't desync
         * anything either) is tolerated by the same peek-and-rewind
         * pattern used elsewhere in this file. */
        const char *save2 = st->src;
        php_skip_ws(st);
        char kw2[16];
        php_read_ident(st, kw2, sizeof kw2);
        php_skip_ws(st);
        if (strcmp(kw2, "class") == 0) {
            php_parse_class_decl(st);
            return;
        }
        st->src = save2;
    }
    if (strcmp(kw, "function") == 0) {
        php_skip_ws(st);
        char fname[64];
        php_read_ident(st, fname, sizeof fname);
        php_skip_ws(st);
        c = *st->src;
        PhpFunc *fn = NULL;
        if (g_nfuncs < PHP_FUNC_MAX) { fn = &g_funcs[g_nfuncs]; }
        int nparams = 0;
        if (c == '(') {
            st->src++;
            php_skip_ws(st);
            c = *st->src;
            while (c != 0 && c != ')') {
                /* "...$name" variadic marker -- see PhpFunc.variadic's
                 * own comment. Detected by looking one char back: this
                 * loop's "else { st->src++; }" fallback (for a type-hint
                 * character, "int"/"?Foo"/etc.) already consumes each
                 * "." of "..." one at a time before reaching the "$", so
                 * by the time `c == '$'` is seen here the three dots (if
                 * any) are already behind st->src -- checking the THREE
                 * bytes immediately preceding it is what actually
                 * detects them without needing a separate lookahead
                 * earlier in the loop. */
                int is_variadic = (c == '$' && st->src[-1] == '.' && st->src[-2] == '.' && st->src[-3] == '.');
                if (c == '$') {
                    st->src++;
                    char pname[64];
                    php_read_ident(st, pname, sizeof pname);
                    if (fn && nparams < PHP_FUNC_PARAM_MAX) {
                        strncpy(fn->params[nparams], pname, sizeof fn->params[nparams] - 1);
                        fn->params[nparams][sizeof fn->params[nparams] - 1] = 0;
                        fn->variadic[nparams] = is_variadic;
                        nparams++;
                    }
                    php_skip_ws(st);
                    c = *st->src;
                    /* Skip a "= default" if present -- default values
                     * aren't evaluated/used yet (deliberately bounded,
                     * see this file's top comment); still needs to be
                     * consumed so parsing stays in sync. */
                    if (c == '=') {
                        st->src++;
                        char dummy[PHP_VAL_MAX];
                        php_eval_expr(st, dummy, sizeof dummy);
                        php_skip_ws(st);
                        c = *st->src;
                    }
                } else {
                    st->src++;
                }
                c = *st->src;
                if (c == ',') { st->src++; php_skip_ws(st); c = *st->src; }
            }
            c = *st->src;
            if (c == ')') st->src++;
        }
        php_skip_ws(st);
        php_skip_return_type(st);
        php_skip_ws(st);
        c = *st->src;
        if (c == '{') {
            st->src++;
            if (fn) {
                strncpy(fn->name, fname, sizeof fn->name - 1); fn->name[sizeof fn->name - 1] = 0;
                fn->nparams = nparams;
                fn->body = st->src;
                php_func_hash_insert(g_nfuncs);
                g_nfuncs++;
            } else if (getenv("SQS_TRACE_CALLS")) {
                fprintf(stderr, "[FUNC TABLE FULL] could not register '%s' (g_nfuncs=%d PHP_FUNC_MAX=%d)\n", fname, g_nfuncs, PHP_FUNC_MAX);
            }
            php_skip_to_brace_close(st);
            c = *st->src;
            if (c == '}') st->src++;
        }
        return;
    }
    if (strcmp(kw, "require_once") == 0 || strcmp(kw, "require") == 0 ||
        strcmp(kw, "include_once") == 0 || strcmp(kw, "include") == 0) {
        int once = (strcmp(kw, "require_once") == 0 || strcmp(kw, "include_once") == 0);
        char path[PHP_VAL_MAX];
        php_eval_expr(st, path, sizeof path);
        php_skip_ws(st);
        c = *st->src;
        if (c == ';') st->src++;
        if (once && php_was_included(path)) return;
        FILE *f = fopen(path, "rb");
        if (!f) return; /* silently skip on failure -- see this file's
                            top comment; a real "require" would be fatal,
                            not modeled here yet */
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0 || g_nbufs >= PHP_BUF_MAX) { fclose(f); return; }
        /* "+ 2", not "+ 1" -- and BOTH trailing bytes zeroed, not just the
         * logical NUL terminator at buf[rd]. This engine's tokenizer
         * reads a 2-character lookahead ("char c0 = p[0], c1 = p[1];")
         * in dozens of places throughout this file (==, !=, <=, >=, &&,
         * ||, ->, ?>, comments, ...) WITHOUT checking c0 for NUL first in
         * most of them -- so whenever parsing reaches exactly the last
         * real byte of a source buffer, that same read pattern reads ONE
         * PAST the allocation. Found via fuzzing php_run() directly this
         * session: a real, easily-reachable heap-buffer-overflow READ
         * (php_skip_ws's own copy of this pattern, fixed directly since
         * it's the most-called single instance, but auditing and fixing
         * every OTHER lookahead site individually would be much more
         * fragile than just guaranteeing the extra byte always exists).
         * A trailing NUL read as either c0 or c1 never matches any real
         * operator this file's parser looks for, so this is a pure safety
         * fix with no behavior change for well-formed input. */
        char *buf = (char *)malloc((size_t)sz + 2);
        if (!buf) { fclose(f); return; }
        size_t rd = fread(buf, 1, (size_t)sz, f);
        fclose(f);
        buf[rd] = 0;
        buf[rd + 1] = 0;
        g_bufs[g_nbufs++] = buf;
        if (once) php_mark_included(path);

        char saved_dir[PHP_PATH_MAX], saved_file[PHP_PATH_MAX];
        strncpy(saved_dir, st->cur_dir, sizeof saved_dir - 1); saved_dir[sizeof saved_dir - 1] = 0;
        strncpy(saved_file, st->cur_file, sizeof saved_file - 1); saved_file[sizeof saved_file - 1] = 0;
        strncpy(st->cur_file, path, sizeof st->cur_file - 1); st->cur_file[sizeof st->cur_file - 1] = 0;
        {
            const char *slash = strrchr(path, '/');
            if (slash) {
                int dl = (int)(slash - path);
                if (dl >= (int)sizeof st->cur_dir) dl = (int)sizeof st->cur_dir - 1;
                memcpy(st->cur_dir, path, (size_t)dl);
                st->cur_dir[dl] = 0;
            } else {
                st->cur_dir[0] = '.'; st->cur_dir[1] = 0;
            }
        }

        const char *saved_src = st->src;
        php_run_source(st, buf);
        st->src = saved_src;
        st->returning = 0; /* a top-level "return" inside an included
                               file ends only that file, not the includer
                               -- real PHP scoping nuances beyond that
                               (e.g. include from inside a function
                               sharing that function's locals) aren't
                               modeled yet */
        strncpy(st->cur_dir, saved_dir, sizeof st->cur_dir - 1); st->cur_dir[sizeof st->cur_dir - 1] = 0;
        strncpy(st->cur_file, saved_file, sizeof st->cur_file - 1); st->cur_file[sizeof st->cur_file - 1] = 0;
        return;
    }
    if (strcmp(kw, "if") == 0) {
        /* Chain: "if (c1) {...} [elseif (c2) {...}]* [else if (c3) {...}]*
         * [else {...}]". `any_matched` tracks whether some earlier clause
         * in this SAME chain already ran -- once true, every later
         * elseif/else clause is a pure skip regardless of its own
         * condition (matches real PHP's "first true branch wins, the
         * rest aren't even evaluated" semantics). Before this existed,
         * "elseif" wasn't recognized as part of the if/else grammar at
         * all: it fell through to being parsed as an ordinary statement,
         * where "elseif (cond)" got misread as a bare CALL to a function
         * named "elseif" (consuming the "(cond)" as that call's own
         * argument list, discarding the result), and the clause's "{...}"
         * body then ran completely UNCONDITIONALLY as a top-level block
         * -- with its closing '}' read by php_run_statements as if it
         * were the end of the *enclosing* function/if/block, truncating
         * everything after it. This wasn't a cosmetic gap: it silently
         * changed control flow and cut real function bodies short the
         * moment they used "elseif" (confirmed this session -- WordPress
         * source uses it constantly, including near the very top of its
         * own wp_die()). */
        int any_matched = 0;
        for (;;) {
            php_skip_ws(st);
            int cond = 0;
            c = *st->src;
            if (c == '(') {
                st->src++;
                char condval[PHP_VAL_MAX];
                php_eval_expr(st, condval, sizeof condval);
                cond = php_truthy(condval);
                php_skip_ws(st);
                c = *st->src;
                /* Defensive: if the condition expression didn't land
                 * exactly on the closing ')' (e.g. an operator this
                 * subset doesn't understand yet was left unconsumed),
                 * skip forward to the real matching ')' instead of
                 * silently mis-parsing whatever follows as the next
                 * statement -- see php_eval_and's own comment for the
                 * kind of gap this guards against; this bug class (a
                 * stray unconsumed operator desyncing the parser and
                 * causing an if-body's statements to run unconditionally
                 * at top level) was found and fixed for "&&" specifically
                 * this session, but this net catches the next one too. */
                if (c != ')') { php_skip_to_paren_close(st); c = *st->src; }
                if (c == ')') st->src++;
            }
            int take = cond && !any_matched;
            php_skip_ws(st);
            c = *st->src;
            if (c == '{') {
                st->src++;
                if (take) {
                    php_run_statements(st);
                    if (st->returning || st->breaking || st->continuing) {
                        php_skip_to_brace_close(st);
                        c = *st->src;
                        if (c == '}') st->src++;
                        return; /* don't try to parse a trailing
                                   elseif/else past a return/break/continue
                                   -- matches how php_run_statements itself
                                   stops; an enclosing while/for/foreach
                                   loop handler is the one that actually
                                   consumes breaking/continuing */
                    }
                } else {
                    php_skip_to_brace_close(st);
                }
                c = *st->src;
                if (c == '}') st->src++;
            } else if (c == ':') {
                /* Alternative ("if (...) : ... elseif (...) : ... else :
                 * ... endif;") syntax -- see php_run_statements_alt's/
                 * php_skip_alt_block's own comments for the full story on
                 * why this needed its own scan machinery (no "}" to
                 * balance-skip against). Scans (executing if `take`,
                 * skip-only otherwise) until reaching "elseif"/"else"/
                 * "endif" at THIS clause's own level, leaving st->src
                 * positioned right AT (not past) whichever matched. An
                 * "endif" ends the whole chain right here; "elseif"/
                 * "else" are deliberately left UNCONSUMED so the SAME
                 * kw2-detection code just below (already handling the
                 * brace-form body's own trailing elseif/else) reads it
                 * fresh and takes over uniformly for both body forms. */
                st->src++;
                const char *stopkws[3]; stopkws[0] = "elseif"; stopkws[1] = "else"; stopkws[2] = "endif";
                int which;
                if (take) {
                    which = php_run_statements_alt(st, stopkws, 3);
                    if (st->returning || st->breaking || st->continuing) return;
                    if (which < 0) return; /* EOF: malformed, degrade safely */
                } else {
                    which = php_skip_alt_block(st, stopkws, 3);
                    if (which < 0) return;
                }
                if (which == 2) { /* "endif" -- whole chain done */
                    char tmp[16]; php_read_ident(st, tmp, sizeof tmp);
                    php_skip_ws(st);
                    if (*st->src == ';') st->src++;
                    if (take) any_matched = 1;
                    return;
                }
                /* which == 0 ("elseif") or 1 ("else"): left unconsumed on
                 * purpose -- fall through. */
            } else {
                /* Bare single-statement body, no "{...}" and no ":" alt-
                 * syntax ("if (cond) return foo();", "if (cond) $x = 1;")
                 * -- previously NOT HANDLED AT ALL here: neither executed
                 * nor skipped, so control simply fell through to the
                 * elseif/else-detection code below with st->src still
                 * sitting right at the body statement. That code doesn't
                 * recognize an arbitrary statement as anything of its
                 * own, so it always took the final "st->src = save;
                 * return;" fallback (rewinding past its own failed
                 * identifier read) -- leaving the body COMPLETELY
                 * unconsumed for the ENCLOSING php_run_statements loop to
                 * pick up and run as the very next statement,
                 * UNCONDITIONALLY, regardless of `take`/the real
                 * condition. A real, previously-undiscovered, silently-
                 * wrong-control-flow bug (confirmed via gcc-vs-squash-
                 * agreeing-but-both-wrong output on a minimal repro, not
                 * a compiler bug) -- found via unrelated "instanceof"
                 * testing, not real WordPress source (WP core's own
                 * coding standards mandate braces on every control
                 * structure, so this essentially never fires against the
                 * vendored wordpress-develop tree; fixed anyway since a
                 * "runs unconditionally no matter what" failure mode is
                 * exactly the class of bug this file's own history singles
                 * out as worth never leaving in place). Mirrors the
                 * brace-form handling immediately above: execute exactly
                 * one real statement via php_run_statement() when `take`,
                 * otherwise skip exactly one statement's worth of source
                 * (php_skip_one_statement, string/comment/nesting-aware)
                 * without evaluating anything in it. */
                if (take) {
                    php_run_statement(st);
                    if (st->returning || st->breaking || st->continuing) return;
                } else {
                    php_skip_one_statement(st);
                }
            }
            if (take) any_matched = 1;

            php_skip_ws(st);
            const char *save = st->src;
            char kw2[16];
            php_read_ident(st, kw2, sizeof kw2);
            if (strcmp(kw2, "elseif") == 0) continue;
            if (strcmp(kw2, "else") == 0) {
                php_skip_ws(st);
                const char *save2 = st->src;
                char kw3[16];
                php_read_ident(st, kw3, sizeof kw3);
                if (strcmp(kw3, "if") == 0) continue; /* "else if" == "elseif" */
                st->src = save2;
                php_skip_ws(st);
                c = *st->src;
                if (c == '{') {
                    st->src++;
                    if (!any_matched) {
                        php_run_statements(st);
                        if (st->returning || st->breaking || st->continuing) {
                            php_skip_to_brace_close(st);
                            c = *st->src;
                            if (c == '}') st->src++;
                            return;
                        }
                    } else {
                        php_skip_to_brace_close(st);
                    }
                    c = *st->src;
                    if (c == '}') st->src++;
                } else if (c == ':') {
                    /* Final "else :" clause's own alt-syntax body -- see
                     * the earlier "if (...) :" branch's own comment;
                     * exactly the same idea, just a single "endif" to
                     * look for since there's no further elseif/else
                     * after a plain else. */
                    st->src++;
                    const char *stopkws2[1]; stopkws2[0] = "endif";
                    int which2;
                    if (!any_matched) {
                        which2 = php_run_statements_alt(st, stopkws2, 1);
                        if (st->returning || st->breaking || st->continuing) return;
                    } else {
                        which2 = php_skip_alt_block(st, stopkws2, 1);
                    }
                    if (which2 == 0) {
                        char tmp[16]; php_read_ident(st, tmp, sizeof tmp);
                        php_skip_ws(st);
                        if (*st->src == ';') st->src++;
                    }
                } else {
                    /* Bare single-statement "else" body, no "{...}"/":" --
                     * same fix, same reasoning, as the "if"/"elseif"
                     * clause's own bare-body branch above. */
                    if (!any_matched) {
                        php_run_statement(st);
                        if (st->returning || st->breaking || st->continuing) return;
                    } else {
                        php_skip_one_statement(st);
                    }
                }
                return;
            }
            st->src = save;
            return;
        }
    }
    /* Statement kinds deliberately not supported yet -- interfaces,
     * traits, enums (real "class" IS supported now, see
     * php_parse_class_decl above), namespaces, loops, try/catch, switch,
     * "global", visibility/type keywords -- skip the whole construct so
     * a script using one degrades to "that construct does nothing"
     * instead of being mis-parsed as an expression statement. Anything
     * NOT in this list falls through below and is parsed as a bare
     * expression statement (covers plain function-call statements like
     * "some_func();").
     *
     * A "simple" construct (no '{' before the next ';', e.g. "const FOO =
     * 1;" or a local "static $x = 1;") just skips to that ';'. A "block"
     * construct (a '{' comes first, e.g. "interface Foo { ... }", "try {
     * ... }") skips the WHOLE brace-matched block via
     * php_skip_to_brace_close -- stopping at the first ';' OR '}' seen
     * anywhere (the previous approach) is wrong for any block with more
     * than one statement inside it: it lands on the FIRST inner '}', not
     * the construct's own closing brace, and dumps the rest as raw text
     * (confirmed as a real bug this session, reached once class
     * definitions in real WordPress source started coming through --
     * "class" itself no longer takes this path at all, but the same
     * shape of bug could still hit "interface"/"trait" the same way).
     * String/comment-aware (php_skip_atomic) -- a bare naive scan here
     * was a confirmed real bug this session (see php_skip_to_brace_
     * close's own comment for the full story: a quoted '}' inside a
     * plain FUNCTION's body, a different skip site than this one,
     * silently truncated the function and let the rest of its
     * statements execute as top-level code) -- fixed at every skip site
     * that does this kind of scan, not just the one that happened to
     * get hit first. */
    /* "global $a, $b, ...;" -- for real now, not parsed-and-skipped (see
     * g_globals' own comment on why: WordPress's own require_wp_db()
     * does "global $wpdb; ... $wpdb = new wpdb(...);", and essentially
     * every other core function that touches the database starts with
     * "global $wpdb;" too -- without this, the real connected $wpdb
     * object was silently discarded the instant require_wp_db()
     * returned). Just records each named variable as globalized in THIS
     * scope (php_var_find()/php_var_find_or_create() do the actual
     * redirect-to-g_globals[] work from here on) -- doesn't itself read
     * or write any value. */
    if (strcmp(kw, "global") == 0) {
        php_skip_ws(st);
        for (;;) {
            char c2 = *st->src;
            if (c2 != '$') break;
            st->src++;
            char vname[64];
            php_read_ident(st, vname, sizeof vname);
            if (st->n_globalized < (int)(sizeof st->globalized / sizeof st->globalized[0])) {
                strncpy(st->globalized[st->n_globalized], vname, sizeof st->globalized[0] - 1);
                st->globalized[st->n_globalized][sizeof st->globalized[0] - 1] = 0;
                st->n_globalized++;
            }
            php_skip_ws(st);
            c2 = *st->src;
            if (c2 == ',') { st->src++; php_skip_ws(st); continue; }
            break;
        }
        php_skip_ws(st);
        if (*st->src == ';') st->src++;
        return;
    }
    if (strcmp(kw, "break") == 0) {
        st->breaking = 1;
        php_skip_ws(st);
        if (*st->src == ';') st->src++;
        return;
    }
    if (strcmp(kw, "continue") == 0) {
        st->continuing = 1;
        php_skip_ws(st);
        if (*st->src == ';') st->src++;
        return;
    }
    /* while/for/foreach -- for real now, not parsed-and-skipped. This
     * interpreter has no AST -- it walks the SOURCE TEXT directly (see
     * this file's top comment) -- so "executing a loop body more than
     * once" means literally re-running php_run_statements() from the
     * same saved source position each iteration, re-evaluating the
     * condition (for while/for) from ITS OWN saved position each time
     * too. PHP_LOOP_MAX bounds every loop against a genuine infinite
     * loop (a real risk now that loops actually run) hanging the whole
     * HTTP request forever -- degrades to "stop iterating" rather than
     * a hang, same "generous bound, safe degradation" convention as
     * every other capacity limit in this file. This was, by a wide
     * margin, the single highest-impact remaining gap for running real
     * WordPress code: "foreach" alone appears throughout WordPress core
     * to walk arrays of options/posts/results/hooks -- e.g.
     * wp_load_alloptions()'s own "foreach ($alloptions_db as $o) {
     * $alloptions[$o->option_name] = $o->option_value; }" NEVER
     * populated $alloptions before this, so is_blog_installed() (and
     * everything downstream of it) could never see the site as
     * installed no matter what the database actually contained. */
    /* "do { BODY } while ( COND );" -- session 8: previously a complete
     * no-op, lumped into the generic unsupported-construct skip list
     * alongside "switch"/"try"/interfaces/etc. (see that list's own
     * comment) -- meaning the WHOLE body was skipped, unconditionally,
     * EVERY time, no matter what. Confirmed as a real, extremely
     * high-impact gap this session: WP_Hook::apply_filters() (wp-
     * includes/class-wp-hook.php) -- the method every single
     * add_filter()/add_action() callback across all of WordPress
     * actually runs through -- is itself built around a "do { ... }
     * while ( false !== next(...) );" loop over its own priority list.
     * With "do" a no-op, that whole method's body (including the
     * foreach that invokes every registered callback) NEVER ran even
     * ONCE, so no hook or filter fired anywhere in a real WordPress
     * request regardless of how correct everything else (object
     * dispatch, array storage/auto-vivification, call_user_func_array,
     * current()/next(), the "$GLOBALS"/instanceof/static-local fixes
     * earlier this session) already was -- found via a minimal add_
     * filter()/apply_filters() repro that visibly did nothing end to
     * end. Only the real "{...} while (...);" brace-body shape is
     * handled (real PHP has no "do ... : ... enddo" alternate syntax at
     * all, unlike if/while/for/foreach, so there's no alt-syntax case to
     * add here). Semantics: body runs UNCONDITIONALLY at least once,
     * THEN the condition gates whether it runs again -- the one thing
     * that structurally distinguishes "do-while" from "while" (which
     * checks first). */
    if (strcmp(kw, "do") == 0) {
        php_skip_ws(st);
        c = *st->src;
        if (c != '{') { php_skip_to_brace_close(st); if (*st->src=='}') st->src++; return; } /* malformed: degrade safely */
        st->src++;
        const char *body_start = st->src;
        int iterations = 0;
        for (;;) {
            if (++iterations > PHP_LOOP_MAX) break;
            if (getenv("SQS_TRACE_CALLS") && iterations == 5000) fprintf(stderr, "[DO-WHILE 5000+ ITERATIONS] body_start=%.60s\n", body_start);
            st->src = body_start;
            php_run_statements(st);
            if (st->returning) {
                st->src = body_start; php_skip_to_brace_close(st);
                if (*st->src == '}') st->src++;
                return;
            }
            if (st->breaking) { st->breaking = 0; break; }
            if (st->continuing) st->continuing = 0;
            /* Body done (or skipped straight past on a re-iteration --
             * php_run_statements itself stops at the matching '}') --
             * find the closing brace, then "while ( COND );". */
            st->src = body_start; php_skip_to_brace_close(st);
            if (*st->src == '}') st->src++;
            php_skip_ws(st);
            const char *savekw = st->src;
            char kw2[8]; php_read_ident(st, kw2, sizeof kw2);
            if (strcmp(kw2, "while") != 0) { st->src = savekw; break; } /* malformed: stop, degrade safely */
            php_skip_ws(st);
            int cond = 0;
            if (*st->src == '(') {
                st->src++;
                char condval[PHP_VAL_MAX];
                php_eval_expr(st, condval, sizeof condval);
                cond = php_truthy(condval);
                php_skip_ws(st);
                if (*st->src != ')') php_skip_to_paren_close(st);
                if (*st->src == ')') st->src++;
            }
            php_skip_ws(st);
            if (*st->src == ';') st->src++;
            if (!cond) break;
            /* Loop again: re-enter the body from body_start (already
             * saved above), condition already consumed for real this
             * time so the NEXT pass through this for(;;) will re-parse
             * "while (...);" fresh from body_start's own closing brace
             * again -- matches how the "while"/"for" handlers above
             * re-run their own condition text from a saved position each
             * iteration (this interpreter has no AST, see this file's
             * own top comment). */
        }
        return;
    }
    if (strcmp(kw, "while") == 0) {
        php_skip_ws(st);
        c = *st->src;
        const char *cond_start = NULL;
        if (c == '(') {
            cond_start = st->src;
            st->src++;
            char condval[PHP_VAL_MAX];
            php_eval_expr(st, condval, sizeof condval);
            php_skip_ws(st);
            c = *st->src;
            if (c != ')') { php_skip_to_paren_close(st); c = *st->src; }
            if (c == ')') st->src++;
        }
        php_skip_ws(st);
        c = *st->src;
        /* Alternative ("while (...) : ... endwhile;") syntax -- see
         * php_run_statements_alt's/php_skip_clause_chain's own comments;
         * needed for real WordPress theme code, most notably the classic
         * Loop pattern itself ("while ( have_posts() ) : the_post(); ...
         * endwhile;", confirmed used verbatim in wp-content/themes/
         * twentyseventeen's own index.php -- without this, "while (...)
         * :" fell through this handler's original "not '{': malformed,
         * do nothing" bailout, so the ENTIRE Loop body silently never
         * ran at all (not even once). */
        int alt = (c == ':');
        if (c != '{' && !alt) return; /* malformed: nothing sane to do, degrade safely */
        st->src++;
        const char *body_start = st->src;
        int iterations = 0;
        const char *stopkws[1]; stopkws[0] = "endwhile";
        for (;;) {
            int cond = 0;
            if (cond_start && ++iterations <= PHP_LOOP_MAX) {
            if (getenv("SQS_TRACE_CALLS") && iterations == 5000) fprintf(stderr, "[LOOP 5000+ ITERATIONS] cond_start=%.60s\n", cond_start);
                const char *save = st->src;
                st->src = cond_start + 1;
                char condval[PHP_VAL_MAX];
                php_eval_expr(st, condval, sizeof condval);
                cond = php_truthy(condval);
                st->src = save;
            }
            if (!cond) break;
            st->src = body_start;
            if (alt) {
                php_run_statements_alt(st, stopkws, 1);
                if (st->returning) return;
            } else {
                php_run_statements(st);
                if (st->returning) {
                    st->src = body_start; php_skip_to_brace_close(st);
                    if (*st->src == '}') st->src++;
                    return;
                }
            }
            if (st->breaking) { st->breaking = 0; break; }
            if (st->continuing) st->continuing = 0;
        }
        st->src = body_start;
        if (alt) {
            int which = php_skip_alt_block(st, stopkws, 1);
            if (which == 0) {
                char tmp[16]; php_read_ident(st, tmp, sizeof tmp);
                php_skip_ws(st);
                if (*st->src == ';') st->src++;
            }
        } else {
            php_skip_to_brace_close(st);
            c = *st->src;
            if (c == '}') st->src++;
        }
        return;
    }
    if (strcmp(kw, "for") == 0) {
        php_skip_ws(st);
        c = *st->src;
        const char *cond_start = NULL;
        const char *step_start = NULL;
        const char *paren_close = NULL;
        if (c == '(') {
            st->src++;
            php_skip_ws(st);
            if (*st->src != ';') {
                for (;;) {
                    php_run_expr_stmt(st);
                    php_skip_ws(st);
                    if (*st->src == ',') { st->src++; php_skip_ws(st); continue; }
                    break;
                }
            }
            php_skip_ws(st);
            if (*st->src == ';') st->src++;
            php_skip_ws(st);
            cond_start = (*st->src != ';') ? st->src : NULL;
            if (cond_start) {
                char tmp[PHP_VAL_MAX];
                php_eval_expr(st, tmp, sizeof tmp);
            }
            php_skip_ws(st);
            if (*st->src == ';') st->src++;
            php_skip_ws(st);
            step_start = (*st->src != ')') ? st->src : NULL;
            if (step_start) {
                /* Find where the step clause ENDS without executing it --
                 * a real, previously-unnoticed bug confirmed this session
                 * (a minimal "for ($j=0; $j<3; $j++) { echo $j; }" repro
                 * printed "12", not "012"): this used to call
                 * php_run_expr_stmt() here directly, which actually RUNS
                 * "$j++" for real while merely parsing the for-header,
                 * before the loop body has executed even once -- so by
                 * the time the condition is first checked and the body
                 * first runs, $j had already been incremented past its
                 * real starting value. php_skip_to_paren_close() just
                 * locates the matching ')' (atomic/string-aware, depth-
                 * counted for any nested parens in the step expression)
                 * WITHOUT evaluating anything; the step clause is (and
                 * always was) correctly evaluated for real later, once
                 * per iteration, via "st->src = step_start;
                 * php_run_expr_stmt(st);" below. */
                php_skip_to_paren_close(st);
            }
            php_skip_ws(st);
            paren_close = st->src;
            if (*st->src == ')') st->src++;
        }
        php_skip_ws(st);
        c = *st->src;
        /* Alternative ("for (...) : ... endfor;") syntax -- see the
         * "while" handler's own comment; rarer in real WordPress than
         * alt-if/alt-while/alt-foreach but cheap to support uniformly
         * now that the shared php_run_statements_alt/php_skip_alt_block
         * machinery exists. */
        int alt = (c == ':');
        if (c != '{' && !alt) return;
        st->src++;
        const char *body_start = st->src;
        int iterations = 0;
        const char *stopkws[1]; stopkws[0] = "endfor";
        for (;;) {
            int cond = 1;
            if (cond_start && ++iterations <= PHP_LOOP_MAX) {
            if (getenv("SQS_TRACE_CALLS") && iterations == 5000) fprintf(stderr, "[LOOP 5000+ ITERATIONS] cond_start=%.60s\n", cond_start);
                const char *save = st->src;
                st->src = cond_start;
                char condval[PHP_VAL_MAX];
                php_eval_expr(st, condval, sizeof condval);
                cond = php_truthy(condval);
                st->src = save;
            } else if (cond_start) {
                cond = 0; /* hit PHP_LOOP_MAX */
            }
            if (!cond) break;
            st->src = body_start;
            if (alt) {
                php_run_statements_alt(st, stopkws, 1);
                if (st->returning) return;
            } else {
                php_run_statements(st);
                if (st->returning) {
                    st->src = body_start; php_skip_to_brace_close(st);
                    if (*st->src == '}') st->src++;
                    return;
                }
            }
            if (st->breaking) { st->breaking = 0; break; }
            if (st->continuing) st->continuing = 0;
            if (step_start) {
                const char *save = st->src;
                st->src = step_start;
                for (;;) {
                    php_run_expr_stmt(st);
                    php_skip_ws(st);
                    if (*st->src == ',') { st->src++; php_skip_ws(st); continue; }
                    break;
                }
                st->src = save;
            }
            (void)paren_close;
        }
        st->src = body_start;
        if (alt) {
            int which = php_skip_alt_block(st, stopkws, 1);
            if (which == 0) {
                char tmp[16]; php_read_ident(st, tmp, sizeof tmp);
                php_skip_ws(st);
                if (*st->src == ';') st->src++;
            }
        } else {
            php_skip_to_brace_close(st);
            c = *st->src;
            if (c == '}') st->src++;
        }
        return;
    }
    if (strcmp(kw, "foreach") == 0) {
        php_skip_ws(st);
        c = *st->src;
        PhpKVArray *src_arr = NULL;
        char keyname[64]; keyname[0] = 0;
        char valname[64]; valname[0] = 0;
        if (c == '(') {
            st->src++;
            php_skip_ws(st);
            {
                /* Evaluate the iterable expression generically (via the
                 * SAME full expression evaluator every other read site in
                 * this file uses) instead of only special-casing a bare
                 * "$name" -- session 8: a bare-"$name"-only parse here
                 * was a real, high-impact gap, found via a WP_Hook-
                 * shaped repro ("foreach ($h->priorities as $p)"): the
                 * OLD code read just "$h" as `aname`, found $h itself
                 * (an OBJECT reference, not an array) via php_var_find,
                 * failed both the is_array and arrref-decode checks, and
                 * -- critically -- left "->priorities as $p) { ... }"
                 * COMPLETELY UNCONSUMED, since nothing after the bare
                 * "$name" read was ever examined for a trailing "->"/
                 * "[" chain. That desynced the parser badly enough that
                 * this whole foreach statement (condition AND body) was
                 * silently never executed, with the leftover text then
                 * consumed one stray character at a time by
                 * php_run_statements' own stuck-guard. This exact shape
                 * ("foreach ($this->callbacks[$priority] as $the_)") is
                 * WP_Hook::apply_filters()'s own inner loop -- the one
                 * that actually invokes every add_filter()/add_action()
                 * callback -- so this bug alone silently defeated
                 * WordPress's entire hook/filter system regardless of
                 * how correct everything upstream (object dispatch,
                 * array storage, current()/next(), call_user_func_array)
                 * already was. php_eval_expr already handles a bare
                 * native-array "$name" correctly too (see its own "bare
                 * array variable read" snapshot-to-arrref comment), so
                 * this generic path is a strict superset of the old
                 * bare-name-only one, not just a different special case. */
                char tmp[PHP_VAL_MAX];
                php_eval_expr(st, tmp, sizeof tmp);
                int aid = php_arrref_decode(tmp);
                if (aid >= 0) src_arr = &g_arrays[aid];
            }
            php_skip_ws(st);
            const char *save_as = st->src;
            char kwas[8]; php_read_ident(st, kwas, sizeof kwas);
            if (strcmp(kwas, "as") == 0) {
                php_skip_ws(st);
                if (*st->src == '&') { st->src++; php_skip_ws(st); } /* "as &$v" -- no true references in this subset, treat as by-value */
                if (*st->src == '$') { st->src++; php_read_ident(st, valname, sizeof valname); }
                php_skip_ws(st);
                if (st->src[0] == '=' && st->src[1] == '>') {
                    st->src += 2;
                    strncpy(keyname, valname, sizeof keyname - 1); keyname[sizeof keyname - 1] = 0;
                    valname[0] = 0;
                    php_skip_ws(st);
                    if (*st->src == '&') { st->src++; php_skip_ws(st); }
                    if (*st->src == '$') { st->src++; php_read_ident(st, valname, sizeof valname); }
                }
            } else {
                st->src = save_as;
            }
            php_skip_ws(st);
            if (*st->src == ')') st->src++;
        }
        php_skip_ws(st);
        c = *st->src;
        /* Alternative ("foreach (...) : ... endforeach;") syntax -- see
         * the "while" handler's own comment; extremely common throughout
         * real WordPress theme/plugin templates (nav menus, widget
         * lists, block/attribute iteration, ...), not just the Loop. */
        int alt = (c == ':');
        if (c != '{' && !alt) return;
        st->src++;
        const char *body_start = st->src;
        const char *stopkws[1]; stopkws[0] = "endforeach";
        if (src_arr) {
            int i, iterations = 0;
            int n = src_arr->count; /* snapshot: a body that appends to the
                                        SAME array being iterated (rare)
                                        won't visit the new elements --
                                        close enough to real PHP's own
                                        "foreach iterates a copy" semantics,
                                        and far safer than re-reading a
                                        live, possibly-growing count */
            for (i = 0; i < n && i < src_arr->count; i++) {
                if (++iterations > PHP_LOOP_MAX) break;
                if (keyname[0]) php_var_set(st, keyname, src_arr->items[i].key);
                if (valname[0]) php_var_set(st, valname, src_arr->items[i].val);
                st->src = body_start;
                if (alt) {
                    php_run_statements_alt(st, stopkws, 1);
                    if (st->returning) return;
                } else {
                    php_run_statements(st);
                    if (st->returning) {
                        st->src = body_start; php_skip_to_brace_close(st);
                        if (*st->src == '}') st->src++;
                        return;
                    }
                }
                if (st->breaking) { st->breaking = 0; break; }
                if (st->continuing) st->continuing = 0;
            }
        }
        st->src = body_start;
        if (alt) {
            int which = php_skip_alt_block(st, stopkws, 1);
            if (which == 0) {
                char tmp[16]; php_read_ident(st, tmp, sizeof tmp);
                php_skip_ws(st);
                if (*st->src == ';') st->src++;
            }
            return;
        }
        php_skip_to_brace_close(st);
        c = *st->src;
        if (c == '}') st->src++;
        return;
    }
    /* "static $name [= init];" -- a function-local persisted variable
     * (real PHP semantics: the initializer runs exactly ONCE ever, on
     * the first call that reaches it; every later call reuses whatever
     * value the variable was left holding, exactly like g_globals[]'s
     * own "global $x;" sharing except keyed by declaration SITE instead
     * of by name -- see g_statics[]'s own top comment for why this was a
     * real, high-impact gap (silently defeated WordPress's own object-
     * cache bootstrap guard). Must be checked BEFORE the generic
     * modifier-keyword skip-list below (which also matches "static",
     * historically treating it identically to "abstract"/"final" and
     * skipping the whole statement inertly) -- but only for the real
     * local-variable-declaration shape ("static $name..."); "static::"
     * (late static binding, e.g. a bare "static::init();" statement) is
     * a completely different construct that happens to start with the
     * same keyword, so it's deliberately left to fall through to the
     * generic bare-expression-statement handling instead (checked via
     * the following character NOT being "$"). */
    if (strcmp(kw, "static") == 0 && *st->src != ':') {
        php_skip_ws(st);
        char c2 = *st->src;
        if (c2 == '$') {
            st->src++;
            char vname[64];
            php_read_ident(st, vname, sizeof vname);
            PhpVar *sv = php_static_find_or_create(stmt_start);
            if (sv && st->n_staticized < (int)(sizeof st->staticized / sizeof st->staticized[0])) {
                strncpy(st->staticized[st->n_staticized], vname, sizeof st->staticized[0] - 1);
                st->staticized[st->n_staticized][sizeof st->staticized[0] - 1] = 0;
                st->staticized_ptr[st->n_staticized] = sv;
                st->n_staticized++;
            }
            php_skip_ws(st);
            c2 = *st->src;
            int already = php_static_already_inited(stmt_start);
            if (c2 == '=') {
                st->src++;
                php_skip_ws(st);
                if (already) {
                    /* Already initialized on an earlier call -- consume
                     * the initializer's source WITHOUT evaluating it
                     * (matches this file's usual "parse but don't
                     * execute" convention for skip-only paths), same
                     * array-literal-vs-scalar shapes php_lvalue_assign
                     * handles, discarding into a throwaway var/array. */
                    const char *rhs_start = st->src;
                    char maybe_kw[8];
                    php_read_ident(st, maybe_kw, sizeof maybe_kw);
                    php_skip_ws(st);
                    char cc = *st->src;
                    if (strcmp(maybe_kw, "array") == 0 && cc == '(') {
                        st->src++;
                        PhpKVArray dummy; memset(&dummy, 0, sizeof dummy);
                        php_parse_array_literal(st, &dummy, ')', NULL);
                    } else {
                        st->src = rhs_start;
                        cc = *st->src;
                        if (cc == '[') {
                            st->src++;
                            PhpKVArray dummy; memset(&dummy, 0, sizeof dummy);
                            php_parse_array_literal(st, &dummy, ']', NULL);
                        } else {
                            char discard[PHP_VAL_MAX];
                            php_eval_expr(st, discard, sizeof discard);
                        }
                    }
                } else if (sv) {
                    /* First time reaching this site -- run the
                     * initializer for real and remember it's done. */
                    const char *rhs_start = st->src;
                    char maybe_kw[8];
                    php_read_ident(st, maybe_kw, sizeof maybe_kw);
                    php_skip_ws(st);
                    char cc = *st->src;
                    if (strcmp(maybe_kw, "array") == 0 && cc == '(') {
                        st->src++;
                        sv->is_array = 1;
                        php_parse_array_literal(st, &sv->arr, ')', &sv->next_index);
                    } else {
                        st->src = rhs_start;
                        cc = *st->src;
                        if (cc == '[') {
                            st->src++;
                            sv->is_array = 1;
                            php_parse_array_literal(st, &sv->arr, ']', &sv->next_index);
                        } else {
                            char val[PHP_VAL_MAX];
                            php_eval_expr(st, val, sizeof val);
                            sv->is_array = 0;
                            strncpy(sv->val, val, sizeof sv->val - 1); sv->val[sizeof sv->val - 1] = 0;
                        }
                    }
                    php_static_mark_inited(stmt_start);
                }
            }
            /* No "=" (e.g. "static $x;" with no initializer, leaving it
             * "" the first time, same as any other never-assigned var):
             * nothing further to do either way. */
            php_skip_ws(st);
            c2 = *st->src;
            if (c2 == ';') st->src++;
            return;
        }
        /* "static" not followed by "$" -- not a local-static-variable
         * declaration after all (most likely "static::..."); fall
         * through to the generic modifier-keyword skip-list below,
         * matching this file's prior (safe, if inert) handling of that
         * shape. `kw` is already "static" so the check just below still
         * matches it normally. */
    }
    if (strcmp(kw, "interface") == 0 || strcmp(kw, "trait") == 0 ||
        strcmp(kw, "enum") == 0 || strcmp(kw, "namespace") == 0 || strcmp(kw, "use") == 0 ||
        strcmp(kw, "try") == 0 || strcmp(kw, "catch") == 0 || strcmp(kw, "finally") == 0 ||
        strcmp(kw, "switch") == 0 || strcmp(kw, "do") == 0 ||
        strcmp(kw, "static") == 0 || strcmp(kw, "abstract") == 0 || strcmp(kw, "final") == 0 ||
        strcmp(kw, "public") == 0 || strcmp(kw, "private") == 0 || strcmp(kw, "protected") == 0 ||
        strcmp(kw, "const") == 0) {
        const char *p = st->src;
        char pc = *p;
        while (pc && pc != ';' && pc != '{') {
            const char *after = php_skip_atomic(p);
            if (after != p) { p = after; pc = *p; continue; }
            p++; pc = *p;
        }
        if (pc == '{') {
            p++;
            st->src = p;
            php_skip_to_brace_close(st);
            p = st->src;
            pc = *p;
            if (pc == '}') p++;
        } else if (pc == ';') {
            p++;
        }
        st->src = p;
        return;
    }
    /* Bare expression statement: re-parse from the start of the
     * statement (before the identifier was consumed) as a full
     * expression -- covers plain calls like "wp_fix_server_vars();" and
     * any builtin call used for side effects, e.g. "define(...)". */
    st->src = stmt_start;
    {
        char discard[PHP_VAL_MAX];
        php_eval_expr(st, discard, sizeof discard);
    }
    php_skip_ws(st);
    c = *st->src;
    if (c == ';') st->src++;
}

/* Emits literal HTML from st->src up to (not including) the next
 * "<?php"/"<?" tag opener, then re-enters PHP mode by consuming that
 * opener -- leaving st->src positioned right after it, ready for
 * statement execution to resume. If no more tag openers exist in the
 * remaining source, emits everything through EOF and leaves st->src at
 * the terminating NUL. This is real PHP's own "alternate template
 * syntax" (exiting to raw HTML with "?>" then re-entering with "<?php"
 * later) -- called from php_run_statements() itself (see its own
 * comment) whenever "?>" is encountered, which is what makes this work
 * at ANY nesting depth (inside a function/if/loop body), not just at
 * the true top level of a file. Before this existed, only the
 * outermost php_run_source() loop understood this transition at all;
 * anything nested (php_run_statements' own "?>" handling used to just
 * RETURN, leaving the caller -- an if-body, a function body -- with no
 * idea it should keep going after the HTML) treated the embedded HTML's
 * own characters (most importantly its OWN curly braces, e.g. a raw
 * "<style>" block's "selector { property: value; }" rules) as if they
 * were PHP code structure, corrupting everything after. Confirmed as a
 * real, high-impact bug this session: real WordPress's own
 * _default_wp_die_handler() does exactly this (drops to HTML to emit
 * its error page's inline <style> block), and every function
 * definition-time skip (php_skip_to_brace_close, via the matching fix
 * in php_skip_atomic) needs the identical treatment for the SAME
 * reason, just without ever calling php_emit (nothing should be
 * emitted from code that isn't actually executing). */
static void php_enter_html_passthrough(PhpState *st) {
    const char *p = st->src;
    const char *open_long = strstr(p, "<?php");
    const char *open_short = strstr(p, "<?");
    const char *open = open_long ? open_long : open_short;
    if (open && open_short && (!open_long || open_short < open_long)) open = open_short;
    if (!open) {
        st->src = p;
        php_emit_str(st, p);
        st->src = p + strlen(p);
        return;
    }
    php_emit(st, p, (int)(open - p));
    p = open;
    if (strncmp(p, "<?php", 5) == 0) p += 5;
    else p += 2; /* "<?" short tag */
    st->src = p;
}

/* Runs statements until an unmatched "}", true EOF, or a fired "return"
 * (the caller -- either the top-level php_run_source() or an if/else
 * body or a function call -- is responsible for consuming/handling the
 * terminator itself). A "?>" is NOT a stopping point -- it's handled
 * internally via php_enter_html_passthrough (see that function's own
 * comment) and the loop just continues once PHP mode resumes, so a
 * function/if/loop body that drops to raw HTML and back works
 * correctly at any nesting depth. Guards against php_run_statement
 * making zero forward progress (a genuine infinite-loop bug class found
 * this session -- an unsupported construct upstream, e.g. a
 * not-yet-supported operator, can leave st->src sitting on a character
 * sequence some statement handler resets-and-bails on without consuming
 * anything; SQS is a long-running server, so a request that hangs it
 * forever on malformed/unsupported PHP input is a real problem, not
 * just a cosmetic bug) -- if a statement genuinely consumes nothing,
 * force one character of progress rather than looping forever. This is
 * a safety net on top of, not a replacement for, fixing each such gap
 * at its own source (e.g. php_run_statement's own "$" handling was
 * fixed directly). */
static void php_run_statements(PhpState *st) {
    for (;;) {
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (!c0) return;
        if (c0 == '?' && c1 == '>') {
            st->src += 2;
            php_enter_html_passthrough(st);
            if (!*st->src) return; /* ran out of source entirely while in
                                       HTML passthrough -- nothing left to
                                       run, even if we were logically
                                       "inside" some block; a source file
                                       that never re-closes what it opened
                                       is malformed either way */
            continue;
        }
        if (c0 == '}') return;
        const char *before = st->src;
        php_run_statement(st);
        /* "break"/"continue" (see their own statement handlers, and the
         * while/for/foreach loop handlers that consume these flags)
         * unwind exactly like "return" does here -- stop running further
         * statements in THIS block and let the caller (an enclosing if/
         * loop body) decide what to do, all the way up to the nearest
         * actual loop. */
        if (st->returning || st->breaking || st->continuing) return;
        if (st->src == before) st->src++;
    }
}

/* Runs statements the same way php_run_statements() does, but for a body
 * that has no closing "}" to stop at -- PHP's ALTERNATIVE (":" ...
 * "endif;"/"endwhile;"/"endfor;"/"endforeach;") control-structure syntax,
 * used throughout real WordPress theme templates (confirmed as a real,
 * high-impact gap this session: wp-content/themes/twentyseventeen's own
 * index.php uses "if ( is_home() && ! is_front_page() ) : ... else : ...
 * endif;" AND the classic Loop itself, "if ( have_posts() ) : while (
 * have_posts() ) : the_post(); ... endwhile; ... else : ... endif;" --
 * without any real support for this syntax, "if"/"while"'s brace-only
 * handlers below did nothing with the ":" at all, leaving it as
 * unconsumed text that php_run_statements' own "force one character of
 * progress on a stuck parser" safety net then just ate one byte of
 * silently -- with the condition itself completely discarded, EVERY
 * branch's literal HTML content between ":" and the next "elseif"/
 * "else"/"endif" ran unconditionally as ordinary top-level HTML
 * passthrough, regardless of which branch (or none) should actually have
 * been taken; a real "while (...) : ... endwhile;" Loop never actually
 * looped at all this way either, since nothing recognized "endwhile" as
 * anything other than an inert bare identifier).
 *
 * Stops (WITHOUT consuming it) upon reaching, at THIS body's own level, a
 * bare keyword matching one of `stopkws` -- returns that keyword's index.
 * Returns -1 if a "return"/"break"/"continue" fired instead (the caller
 * should stop immediately and propagate, exactly like the brace-based
 * handlers already do -- see php_run_statements' own identical check) or
 * if source ran out entirely (malformed input: degrade safely rather
 * than hang). No explicit nesting-depth counter is needed to tell "our
 * own" endif/endwhile/etc. apart from a NESTED one's: a nested control
 * structure (brace OR alt-syntax form, any kind) always fully consumes
 * its own matching terminator via ordinary recursion through
 * php_run_statement() (which is what parses a nested "if"/"while"/...)
 * before control ever returns to THIS loop -- so by the time we're back
 * here checking the upcoming keyword, only a keyword genuinely belonging
 * to OUR OWN body can be sitting there next. */
static int php_run_statements_alt(PhpState *st, const char **stopkws, int nstop) {
    for (;;) {
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (!c0) return -1;
        if (c0 == '?' && c1 == '>') {
            st->src += 2;
            php_enter_html_passthrough(st);
            if (!*st->src) return -1;
            continue;
        }
        if (c0 == '}') return -1; /* malformed (alt syntax has no "}"): bail safely */
        if ((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z') || c0 == '_') {
            const char *save = st->src;
            char kw[16];
            php_read_ident(st, kw, sizeof kw);
            int i;
            for (i = 0; i < nstop; i++) {
                if (strcmp(kw, stopkws[i]) == 0) { st->src = save; return i; }
            }
            st->src = save;
        }
        const char *before = st->src;
        php_run_statement(st);
        if (st->returning || st->breaking || st->continuing) return -1;
        if (st->src == before) st->src++;
    }
}

/* A tiny, hand-rolled regex engine backing preg_match()/preg_replace() --
 * see those builtins' own comment (in php_call_function) for WHY: a
 * single missing "preg_replace('|[^a-z0-9-]+|', '', $type)" call
 * (wp-includes/template.php's own get_query_template(), sanitizing a
 * template TYPE name like "index") silently wiped a perfectly valid
 * "index" down to "" (every unrecognized function call in this file
 * degrades to returning "" -- fine for a void-ish call, actively
 * destructive for a "return a transformed copy" one), which is what
 * made template resolution find nothing no matter how correct the
 * theme/DB layer was. preg_* is used FAR too pervasively throughout
 * real WordPress (slug sanitization, URL parsing, esc_* helpers, ...)
 * to leave unimplemented the way dozens of other, rarer builtins still
 * are -- this earns being a real (if intentionally small) engine, not
 * just another stub.
 *
 * Deliberately bounded, PCRE subset: literal characters, "." (any),
 * "[...]"/"[^...]" character classes (with "a-z" ranges), "\d"/"\D"/
 * "\w"/"\W"/"\s"/"\S", "^"/"$" anchors, and "*"/"+"/"?" greedy
 * quantifiers (with backtracking) on any single token. NO grouping/
 * alternation/backreferences/capture groups -- preg_match() only
 * reports whether the pattern matched (the 3rd "&$matches" output-
 * array argument real PHP supports isn't implemented), and
 * preg_replace()'s replacement string is inserted literally (no "$1"
 * backreferences). This covers the overwhelming majority of
 * WordPress's own real-world preg_* call shapes (character-class-based
 * sanitization, simple validation checks) without the much larger
 * undertaking a full PCRE engine would be. */
#define RTOK_MAX 128
typedef struct {
    int kind; /* 0=literal char, 1=any ".", 2=character class, 3="^", 4="$" */
    char lit;
    unsigned char cls[32]; /* 256-bit membership bitmap for kind==2 */
    int negate;
    char quant; /* 0 (none), '*', '+', '?' */
} RToken;

static void regex_cls_add_range(RToken *t, unsigned char lo, unsigned char hi) {
    unsigned int cc;
    for (cc = lo; cc <= hi; cc++) t->cls[cc / 8] |= (unsigned char)(1u << (cc % 8));
}

/* Compiles a pattern BODY (delimiters/flags already stripped by the
 * caller) into `toks`. Returns the token count (0 for an empty/
 * unparseable pattern -- an empty token list matches the empty string
 * at every position, which is the same safe "no-op transform" fallback
 * an unrecognized pattern would need anyway). */
static int regex_compile_body(const char *p, RToken *toks, int max) {
    int n = 0;
    while (*p && n < max) {
        RToken t; memset(&t, 0, sizeof t);
        if (*p == '^') { t.kind = 3; p++; }
        else if (*p == '$') { t.kind = 4; p++; }
        else if (*p == '.') { t.kind = 1; p++; }
        else if (*p == '[') {
            p++;
            t.kind = 2;
            if (*p == '^') { t.negate = 1; p++; }
            int first = 1;
            while (*p && (*p != ']' || first)) {
                first = 0;
                unsigned char c0 = (unsigned char)*p;
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    unsigned char c1 = (unsigned char)p[2];
                    if (c1 >= c0) regex_cls_add_range(&t, c0, c1);
                    p += 3;
                } else {
                    regex_cls_add_range(&t, c0, c0);
                    p++;
                }
            }
            if (*p == ']') p++;
        } else if (*p == '\\' && p[1]) {
            char e = p[1];
            if (e == 'd' || e == 'D') { t.kind = 2; regex_cls_add_range(&t, '0', '9'); t.negate = (e == 'D'); }
            else if (e == 'w' || e == 'W') {
                t.kind = 2;
                regex_cls_add_range(&t, 'a', 'z'); regex_cls_add_range(&t, 'A', 'Z');
                regex_cls_add_range(&t, '0', '9'); regex_cls_add_range(&t, '_', '_');
                t.negate = (e == 'W');
            } else if (e == 's' || e == 'S') {
                t.kind = 2;
                regex_cls_add_range(&t, ' ', ' '); regex_cls_add_range(&t, '\t', '\t');
                regex_cls_add_range(&t, '\n', '\n'); regex_cls_add_range(&t, '\r', '\r');
                t.negate = (e == 'S');
            } else { t.kind = 0; t.lit = e; }
            p += 2;
        } else { t.kind = 0; t.lit = *p; p++; }
        char q = *p;
        if (q == '*' || q == '+' || q == '?') { t.quant = q; p++; }
        toks[n++] = t;
    }
    return n;
}

/* Strips a PHP-style delimited pattern ("|body|flags", "/body/flags",
 * "#body#i", ...; bracket delimiters "(){}[]<>" pair with their mirror)
 * and compiles the body. Returns 0 (no tokens: matches empty-string-
 * only) if `pat` is malformed -- degrades safely rather than crashing
 * on an unrecognized delimiter shape. */
static int regex_compile_pattern(const char *pat, RToken *toks, int max) {
    if (!pat[0]) return 0;
    char delim = pat[0];
    char close = delim;
    if (delim == '(') close = ')';
    else if (delim == '{') close = '}';
    else if (delim == '[') close = ']';
    else if (delim == '<') close = '>';
    const char *p = pat + 1;
    const char *end = strrchr(p, close);
    if (!end) return 0;
    char body[PHP_VAL_MAX];
    int blen = (int)(end - p);
    if (blen >= (int)sizeof body) blen = (int)sizeof body - 1;
    if (blen < 0) blen = 0;
    memcpy(body, p, (size_t)blen); body[blen] = 0;
    return regex_compile_body(body, toks, max);
}

static int regex_tok_matches(const RToken *t, char c) {
    if (t->kind == 1) return c != 0;
    if (t->kind == 2) {
        unsigned char uc = (unsigned char)c;
        int in = (t->cls[uc / 8] >> (uc % 8)) & 1;
        return t->negate ? !in : in;
    }
    return c == t->lit;
}

/* Backtracking match of toks[ti..ntoks) against text[si..], returning
 * the end offset on success or -1. Recursion depth is bounded by
 * pattern length (RTOK_MAX), not input length -- the quantifier
 * backtrack loop is iterative, not recursive, so a long run of matched
 * characters doesn't grow the call stack. */
static int regex_match_here(const RToken *toks, int ntoks, int ti, const char *text, int si, int tlen) {
    if (ti >= ntoks) return si;
    const RToken *t = &toks[ti];
    if (t->kind == 3) return (si == 0) ? regex_match_here(toks, ntoks, ti + 1, text, si, tlen) : -1;
    if (t->kind == 4) return (si == tlen) ? regex_match_here(toks, ntoks, ti + 1, text, si, tlen) : -1;
    if (t->quant == 0) {
        if (si < tlen && regex_tok_matches(t, text[si])) return regex_match_here(toks, ntoks, ti + 1, text, si + 1, tlen);
        return -1;
    }
    if (t->quant == '?') {
        if (si < tlen && regex_tok_matches(t, text[si])) {
            int r = regex_match_here(toks, ntoks, ti + 1, text, si + 1, tlen);
            if (r >= 0) return r;
        }
        return regex_match_here(toks, ntoks, ti + 1, text, si, tlen);
    }
    /* '*' / '+': match as many as possible, then backtrack down to the minimum */
    int count = 0;
    while (si + count < tlen && regex_tok_matches(t, text[si + count])) count++;
    int minc = (t->quant == '+') ? 1 : 0;
    int k;
    for (k = count; k >= minc; k--) {
        int r = regex_match_here(toks, ntoks, ti + 1, text, si + k, tlen);
        if (r >= 0) return r;
    }
    return -1;
}

/* Finds the first match anywhere in `text`, filling *mstart/*mend
 * (byte offsets, mend exclusive). Returns 1 if found, 0 otherwise. */
static int regex_search(const RToken *toks, int ntoks, const char *text, int *mstart, int *mend) {
    int tlen = (int)strlen(text);
    int si;
    for (si = 0; si <= tlen; si++) {
        int r = regex_match_here(toks, ntoks, 0, text, si, tlen);
        if (r >= 0) { *mstart = si; *mend = r; return 1; }
    }
    return 0;
}

/* Quotes/escapes a value for embedding as a single-quoted SQL literal in
 * a statement string handed to sqdb_exec()/sqdb_query() -- doubles `'`
 * (matching sqdb_value()'s own '' escape) AND doubles `\` (so a literal
 * backslash in the value round-trips instead of being misread as the
 * START of one of sqdb_value()'s OWN backslash-escapes, e.g. a value
 * ending in a backslash right before the closing quote). `out` must be
 * at least 2*strlen(in)+3 bytes. */
static void php_db_sql_quote(const char *in, char *out, int outcap) {
    int o = 0;
    const char *p;
    if (o < outcap - 1) out[o++] = '\'';
    for (p = in; *p && o < outcap - 3; p++) {
        if (*p == '\'' || *p == '\\') out[o++] = '\\';
        out[o++] = *p;
    }
    if (o < outcap - 1) out[o++] = '\'';
    out[o] = 0;
}

/* Column count / one field of the cached g_db_last_raw packed query
 * result -- see g_db_last_raw's own comment. Shared by __db_get_var()/
 * __db_get_col() so the packed-format parsing logic (identical to
 * __db_query()'s own row-unpacking loop, just reading one field instead
 * of building PHP objects) lives in exactly one place. */
static int php_db_raw_ncols(const char *raw) {
    const char *lend = strchr(raw, '\n');
    int llen = lend ? (int)(lend - raw) : (int)strlen(raw);
    if (llen == 0) return 0;
    int n = 1, i;
    for (i = 0; i < llen; i++) if (raw[i] == ',') n++;
    return n;
}
static int php_db_raw_field(const char *raw, int row_index, int col_index, char *out, int outcap) {
    int ncols = php_db_raw_ncols(raw);
    if (col_index < 0 || col_index >= ncols) { out[0] = 0; return 0; }
    const char *lend = strchr(raw, '\n');
    const char *p = lend ? lend + 1 : raw + strlen(raw);
    int r;
    for (r = 0; r < row_index; r++) {
        if (!*p) { out[0] = 0; return 0; }
        lend = strchr(p, '\n');
        if (!lend) { out[0] = 0; return 0; }
        p = lend + 1;
    }
    if (!*p) { out[0] = 0; return 0; }
    lend = strchr(p, '\n');
    int llen = lend ? (int)(lend - p) : (int)strlen(p);
    const char *cp = p, *cend = p + llen;
    int ci = 0;
    while (ci < col_index) {
        const char *tab = memchr(cp, '\t', (size_t)(cend - cp));
        cp = tab ? tab + 1 : cend;
        ci++;
    }
    const char *tab = memchr(cp, '\t', (size_t)(cend - cp));
    const char *fe = tab ? tab : cend;
    char fieldbuf[PHP_VAL_MAX];
    int flen = (int)(fe - cp);
    if (flen >= (int)sizeof fieldbuf) flen = (int)sizeof fieldbuf - 1;
    memcpy(fieldbuf, cp, (size_t)flen);
    fieldbuf[flen] = 0;
    db_unescape_field(fieldbuf, out, outcap);
    return 1;
}

/* A real, standalone MD5 implementation (RFC 1321) -- added session 8
 * alongside real glob() support (see that builtin's own comment) because
 * WordPress's own wp-includes/class-wp-textdomain-registry.php uses
 * "md5( $path )" to build a wp_cache_get()/wp_cache_set() cache KEY for
 * a directory's translation-file listing. With md5() previously
 * unimplemented (falling through to the generic "unknown function ->
 * ''" degrade), EVERY distinct $path produced the exact SAME empty
 * cache key -- so every text domain's file listing silently collided
 * into ONE shared cache slot, each one overwriting the last, which
 * defeated the whole point of caching per-path and was a real
 * contributing cause of the repeated/expensive glob() re-scans this
 * session traced via SQS_TRACE_CALLS. A real hash (not just "some
 * distinguishing function of the input") is worth having since 37 sites
 * across the vendored tree call md5() and at least some (cache keys
 * displayed/compared, gravatar-style URLs elsewhere in WP core generally)
 * plausibly depend on getting PHP's real 32-hex-digit result, not just
 * an internally-consistent stand-in. */
static void php_md5_hex(const unsigned char *msg, unsigned long len, char out_hex[33]) {
    unsigned int a0 = 0x67452301u, b0 = 0xefcdab89u, c0 = 0x98badcfeu, d0 = 0x10325476u;
    static const unsigned int K[64] = {
        0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
        0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
        0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
        0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
        0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
        0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
        0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
        0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u
    };
    static const unsigned int S[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
    unsigned long padded_len = ((len + 8) / 64 + 1) * 64;
    unsigned char *buf = (unsigned char *)calloc(1, padded_len);
    if (!buf) { out_hex[0] = 0; return; }
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    unsigned long bitlen = len * 8;
    unsigned long i;
    for (i = 0; i < 8; i++) buf[padded_len - 8 + i] = (unsigned char)((bitlen >> (8 * i)) & 0xFF);

    unsigned long chunk;
    for (chunk = 0; chunk < padded_len; chunk += 64) {
        unsigned int M[16];
        for (i = 0; i < 16; i++) {
            const unsigned char *p = buf + chunk + i * 4;
            M[i] = (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
        }
        unsigned int A = a0, B = b0, C = c0, D = d0;
        for (i = 0; i < 64; i++) {
            unsigned int F; unsigned int g;
            if (i < 16) { F = (B & C) | (~B & D); g = (unsigned int)i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (unsigned int)(5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D; g = (unsigned int)(3 * i + 5) % 16; }
            else { F = C ^ (B | ~D); g = (unsigned int)(7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + ((F << S[i]) | (F >> (32 - S[i])));
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    free(buf);
    unsigned char digest[16];
    unsigned int words[4]; words[0] = a0; words[1] = b0; words[2] = c0; words[3] = d0;
    for (i = 0; i < 4; i++) {
        digest[i*4+0] = (unsigned char)(words[i] & 0xFF);
        digest[i*4+1] = (unsigned char)((words[i] >> 8) & 0xFF);
        digest[i*4+2] = (unsigned char)((words[i] >> 16) & 0xFF);
        digest[i*4+3] = (unsigned char)((words[i] >> 24) & 0xFF);
    }
    static const char hexch[] = "0123456789abcdef";
    for (i = 0; i < 16; i++) { out_hex[i*2] = hexch[digest[i] >> 4]; out_hex[i*2+1] = hexch[digest[i] & 0xF]; }
    out_hex[32] = 0;
}

/* Resolves and invokes a PHP "callable" value -- a plain function-name
 * string ("some_func"), an "array($obj, 'method')" / "array('Class',
 * 'method')" 2-element array, or a "'Class::method'" string -- and
 * writes its return value into `out`. Added session 8 alongside real
 * array-of-args call_user_func_array()/current()/next() support (see
 * those builtins' own comments): WP_Hook::apply_filters() (wp-includes/
 * class-wp-hook.php, the method EVERY add_filter()/add_action()
 * callback actually runs through) calls "call_user_func_array(
 * $the_['function'], $args )" where $the_['function'] can be any of
 * these shapes depending on how the original add_filter()/add_action()
 * call registered its callback -- a real WordPress boot registers
 * plenty of the array-callable ("array($this, 'method')", a bound
 * instance method) and 'Class::method' static forms, not just plain
 * function names, so only supporting the plain-string form (this file's
 * prior call_user_func()/call_user_func_array() implementation, see
 * their own now-partially-stale comments) left a large fraction of
 * real registered hooks silently never firing. */
static void php_invoke_callable(PhpState *caller, const char *callable, char **args, int nargs, char *out, int outcap) {
    int aid = php_arrref_decode(callable);
    if (aid >= 0) {
        char c0[PHP_VAL_MAX], c1[128];
        php_kv_lookup(&g_arrays[aid], "0", c0, sizeof c0);
        php_kv_lookup(&g_arrays[aid], "1", c1, sizeof c1);
        int oid = php_objref_decode(c0);
        if (oid >= 0) {
            PhpClass *cls = php_class_find(g_objects[oid].class_name);
            PhpMethod *m = php_class_find_method(cls, c1);
            if (m) { php_call_method(caller, oid, m, args, nargs, out, outcap); return; }
        } else if (c0[0]) {
            /* array('ClassName', 'method') -- static form. */
            PhpClass *cls = php_class_find(c0);
            PhpMethod *m = php_class_find_method(cls, c1);
            if (m) { php_call_static(caller, m, args, nargs, out, outcap); return; }
        }
        out[0] = 0;
        return;
    }
    const char *sep = strstr(callable, "::");
    if (sep) {
        char cls_name[64]; int clen = (int)(sep - callable);
        if (clen >= (int)sizeof cls_name) clen = (int)sizeof cls_name - 1;
        memcpy(cls_name, callable, (size_t)clen); cls_name[clen] = 0;
        PhpClass *cls = php_class_find(cls_name);
        PhpMethod *m = php_class_find_method(cls, sep + 2);
        if (m) { php_call_static(caller, m, args, nargs, out, outcap); return; }
        out[0] = 0;
        return;
    }
    php_call_function(caller, callable, args, nargs, out, outcap);
}

/* Frees every LOCAL var's own array storage (PhpVar.arr.items -- see
 * PHP_KV_HARD_MAX's own comment on why that's now malloc'd/realloc'd
 * instead of a fixed embed) before a PhpState is itself freed. Does NOT
 * touch st->get/st->post -- those are a shallow, shared-ownership copy
 * of the CALLER's (or, at the top level, php_run()'s own caller-
 * supplied) get/post arrays (see "callee->get = caller->get;" below),
 * never a copy this particular PhpState uniquely owns, so only
 * php_run()'s own top-level teardown (the one place that actually
 * received them from OUTSIDE this file, via php_run()'s own `get`/
 * `post` parameters) frees those two. Called at every one of this
 * file's 4 `free(callee)`/`free(st)` sites (php_call_function/
 * php_call_method/php_call_static's `callee`, and php_run()'s own
 * top-level `st`) -- without this, EVERY local variable that ever held
 * an array would leak its backing storage for the rest of the server
 * process's life, not just this one call's. */
static void php_state_free_local_arrays(PhpState *st) {
    int i;
    for (i = 0; i < st->nvars; i++) php_kv_free(&st->vars[i].arr);
}

/* Calls a user-defined function (found via php_func_find) or one of a
 * small set of builtins. Builds a fresh, isolated local-variable scope
 * for a user function (no "global", no closures -- see this file's top
 * comment); the callee shares the SAME output buffer position as the
 * caller (so echo/print inside a function still lands in the right
 * place in the response), and shares the request-lifetime g_consts /
 * g_funcs tables (so functions can call other functions / reference
 * constants regardless of which included file defined them). */
static void php_call_function(PhpState *caller, const char *name, char **args, int nargs, char *out, int outcap) {
    if (strcmp(name, "define") == 0 && nargs >= 2) {
        php_const_set(args[0], args[1]);
        strncpy(out, "1", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "defined") == 0 && nargs >= 1) {
        strncpy(out, php_const_find(args[0]) ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "function_exists") == 0 && nargs >= 1) {
        int found = php_func_find(args[0]) != NULL;
        if (!found) {
            /* NOT "static const char *builtins[] = {...};" -- see
             * preg_quote's own comment on the confirmed real squash bug
             * with "static" LOCAL string-literal initializers; this
             * array-of-pointers shape is affected too (a minimal
             * standalone repro of exactly this pattern printed nothing
             * at all under squash -- the loop below would have silently
             * treated every builtin as "not found"). Non-static costs
             * nothing meaningful here. */
            const char *builtins[] = {
                "define", "defined", "function_exists", "sprintf", "printf",
                "file_exists", "is_dir", "is_file", "is_readable", "is_writable", "is_writeable",
                "realpath", "strlen", "dirname", "str_ends_with", "str_starts_with",
                "str_contains", "implode", "join", "extension_loaded", NULL
            };
            int i;
            for (i = 0; builtins[i]; i++) if (strcmp(args[0], builtins[i]) == 0) { found = 1; break; }
        }
        strncpy(out, found ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "extension_loaded") == 0) {
        /* This interpreter subset has no real concept of a "PHP
         * extension" -- every builtin here is either always available or
         * always absent, independent of any extension grouping (unlike
         * mysqli, which we deliberately do NOT fake -- see
         * wp-content/db.php's own comment; that one implies a specific,
         * consequential capability (a working mysqli_connect()) that
         * genuinely doesn't exist here). "extension_loaded('json')"/
         * "'hash'"-style checks (WordPress's own $required_php_
         * extensions, wp-includes/version.php) are purely informational
         * gates, not something calling code branches its OWN behavior
         * on beyond "should I warn the user" -- unconditionally true
         * here just silences that warning, and any function from a
         * "loaded" extension that isn't actually implemented degrades
         * the exact same safe way every other unimplemented function in
         * this file already does (a no-op, not a crash or a lie about
         * doing real work). */
        strncpy(out, "1", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    /* Array internal-pointer functions (current/next/reset/end/key/prev)
     * -- session 8, see PhpKVArray's own "cursor" field comment for why
     * (WP_Hook::apply_filters()). `args[0]` is whatever the call site's
     * normal argument evaluation already produced -- an arrref-encoded
     * token identifying a g_arrays[] slot -- so mutating that slot's
     * cursor directly (rather than needing a true by-reference parameter
     * mechanism this file doesn't have) is enough: any OTHER expression
     * that reads the same underlying array (e.g. re-evaluating
     * "$this->iterations[$n]" on a later loop iteration) decodes to the
     * SAME arrref id and therefore sees the SAME mutated cursor, exactly
     * matching what real PHP's by-reference semantics achieve here. Real
     * PHP returns `false` at the exhausted/out-of-bounds end; this
     * subset represents that as "" (empty string), same convention
     * "false"/"null" already use everywhere else in this file. */
    if ((strcmp(name, "current") == 0 || strcmp(name, "pos") == 0) && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0 && g_arrays[aid].cursor >= 0 && g_arrays[aid].cursor < g_arrays[aid].count) {
            strncpy(out, g_arrays[aid].items[g_arrays[aid].cursor].val, outcap - 1); out[outcap - 1] = 0;
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "key") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0 && g_arrays[aid].cursor >= 0 && g_arrays[aid].cursor < g_arrays[aid].count) {
            strncpy(out, g_arrays[aid].items[g_arrays[aid].cursor].key, outcap - 1); out[outcap - 1] = 0;
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "next") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0) {
            g_arrays[aid].cursor++;
            if (g_arrays[aid].cursor >= 0 && g_arrays[aid].cursor < g_arrays[aid].count) {
                strncpy(out, g_arrays[aid].items[g_arrays[aid].cursor].val, outcap - 1); out[outcap - 1] = 0;
                return;
            }
        }
        out[0] = 0;
        return;
    }
    if (strcmp(name, "prev") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0) {
            g_arrays[aid].cursor--;
            if (g_arrays[aid].cursor >= 0 && g_arrays[aid].cursor < g_arrays[aid].count) {
                strncpy(out, g_arrays[aid].items[g_arrays[aid].cursor].val, outcap - 1); out[outcap - 1] = 0;
                return;
            }
        }
        out[0] = 0;
        return;
    }
    if (strcmp(name, "reset") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0) {
            g_arrays[aid].cursor = 0;
            if (g_arrays[aid].count > 0) { strncpy(out, g_arrays[aid].items[0].val, outcap - 1); out[outcap - 1] = 0; return; }
        }
        out[0] = 0;
        return;
    }
    if (strcmp(name, "end") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0 && g_arrays[aid].count > 0) {
            g_arrays[aid].cursor = g_arrays[aid].count - 1;
            strncpy(out, g_arrays[aid].items[g_arrays[aid].cursor].val, outcap - 1); out[outcap - 1] = 0;
            return;
        }
        if (aid >= 0) g_arrays[aid].cursor = 0;
        out[0] = 0;
        return;
    }
    /* array_slice($arr, $offset [, $length]) -- only the plain positional
     * (non-preserve-keys) numeric-reindex form real WordPress code
     * actually uses (WP_Hook::apply_filters()'s own "array_slice( $args,
     * 0, $the_['accepted_args'] )"), matching this file's usual "just
     * the real call shapes reached" scope discipline. Negative
     * offset/length (count from the end) supported since they're cheap
     * and real PHP allows them. */
    if (strcmp(name, "array_slice") == 0 && nargs >= 2) {
        int aid = php_arrref_decode(args[0]);
        int id2 = php_array_new();
        if (aid >= 0 && id2 >= 0) {
            int cnt = g_arrays[aid].count;
            int off = atoi(args[1]);
            if (off < 0) { off += cnt; if (off < 0) off = 0; }
            int len = (nargs >= 3 && args[2][0]) ? atoi(args[2]) : (cnt - off);
            if (len < 0) len = (cnt + len) - off;
            if (len < 0) len = 0;
            int end2 = off + len; if (end2 > cnt) end2 = cnt;
            int ii, ki = 0;
            for (ii = off; ii < end2 && ii >= 0; ii++) {
                char k[16]; snprintf(k, sizeof k, "%d", ki++);
                php_kv_set(&g_arrays[id2], k, g_arrays[aid].items[ii].val);
            }
            php_arrref_encode(id2, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "func_num_args") == 0) {
        snprintf(out, outcap, "%d", caller->n_raw_args);
        return;
    }
    if (strcmp(name, "func_get_args") == 0) {
        int id = php_array_new();
        if (id >= 0) {
            int ai2;
            for (ai2 = 0; ai2 < caller->n_raw_args; ai2++) { char k[16]; snprintf(k, sizeof k, "%d", ai2); php_kv_set(&g_arrays[id], k, caller->raw_args[ai2]); }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "func_get_arg") == 0 && nargs >= 1) {
        int idx = atoi(args[0]);
        if (idx >= 0 && idx < caller->n_raw_args) { strncpy(out, caller->raw_args[idx], outcap - 1); out[outcap - 1] = 0; }
        else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_unshift") == 0 && nargs >= 2) {
        /* array_unshift($arr, $val, ...) -- real PHP takes this array
         * BY REFERENCE and re-indexes every existing numeric key after
         * prepending; this file's builtins don't have true by-reference
         * parameters (see this file's top comment/call_user_func_array's
         * own comment on the same limitation), but since args[0] is
         * already an arrref TOKEN identifying a shared g_arrays[] slot
         * (not a copy), mutating that slot directly still correctly
         * affects whatever variable/property the caller's own
         * expression decoded it from -- same trick current()/next()
         * rely on (see their own comment). Only the "$arr" arg needs to
         * already be an array; any further args are prepended, in
         * order, ahead of the existing (renumbered) entries. */
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0) {
            PhpKVArray *a = &g_arrays[aid];
            int old_count = a->count;
            int nnew = nargs - 1;
            if (php_kv_ensure_cap(a, old_count + nnew)) {
                int ii;
                for (ii = old_count - 1; ii >= 0; ii--) {
                    char k[16]; snprintf(k, sizeof k, "%d", ii + nnew);
                    strncpy(a->items[ii + nnew].key, k, sizeof a->items[0].key - 1); a->items[ii+nnew].key[sizeof a->items[0].key-1]=0;
                    strncpy(a->items[ii + nnew].val, a->items[ii].val, sizeof a->items[0].val - 1); a->items[ii+nnew].val[sizeof a->items[0].val-1]=0;
                }
                for (ii = 0; ii < nnew; ii++) {
                    char k[16]; snprintf(k, sizeof k, "%d", ii);
                    strncpy(a->items[ii].key, k, sizeof a->items[0].key - 1); a->items[ii].key[sizeof a->items[0].key-1]=0;
                    strncpy(a->items[ii].val, args[1 + ii], sizeof a->items[0].val - 1); a->items[ii].val[sizeof a->items[0].val-1]=0;
                }
                a->count = old_count + nnew;
                a->cursor = 0;
            }
            snprintf(out, outcap, "%d", a->count);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_pop") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0 && g_arrays[aid].count > 0) {
            PhpKVArray *a = &g_arrays[aid];
            strncpy(out, a->items[a->count - 1].val, outcap - 1); out[outcap - 1] = 0;
            a->count--;
            if (a->cursor > a->count - 1) a->cursor = a->count - 1;
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "call_user_func") == 0 && nargs >= 1) {
        /* Session 8: now goes through php_invoke_callable (see its own
         * comment) instead of always treating args[0] as a bare
         * function-name string -- this is what actually makes an
         * "array($obj, 'method')"/"'Class::method'" callback fire
         * (previously always silently degraded to "unknown function ->
         * ''" for those shapes, since a whole arrref-encoded/"::"
         * string was passed straight to php_call_function as if it were
         * a plain function name). */
        char fwd_storage[PHP_ARG_MAX][PHP_VAL_MAX];
        char *fwd[PHP_ARG_MAX];
        int fi, fn2 = 0;
        for (fi = 0; fi < PHP_ARG_MAX; fi++) fwd[fi] = fwd_storage[fi];
        for (fi = 1; fi < nargs && fn2 < PHP_ARG_MAX; fi++) {
            strncpy(fwd[fn2], args[fi], PHP_VAL_MAX - 1); fwd[fn2][PHP_VAL_MAX - 1] = 0;
            fn2++;
        }
        php_invoke_callable(caller, args[0], fwd, fn2, out, outcap);
        return;
    }
    if (strcmp(name, "call_user_func_array") == 0 && nargs >= 1) {
        /* Session 8: real array-of-args support, not just the 0-arg
         * shape -- args[1], if given, is decoded as an arrref and its
         * elements (in KV insertion/positional order, matching how
         * php_parse_array_literal/"$arr[]=" already store them) become
         * the forwarded positional call arguments. This is specifically
         * what WP_Hook::apply_filters() needs ("call_user_func_array(
         * $the_['function'], $args )", where $args is a REAL array
         * built from the filter's actual value + extra parameters) --
         * without it, every single add_filter()/add_action() callback
         * across all of WordPress silently never received its real
         * arguments (confirmed this session via a minimal add_filter()/
         * apply_filters() repro that visibly did nothing end to end
         * despite the WP_Hook object dispatch itself working correctly). */
        char fwd_storage[PHP_ARG_MAX][PHP_VAL_MAX];
        char *fwd[PHP_ARG_MAX];
        int fi; for (fi = 0; fi < PHP_ARG_MAX; fi++) fwd[fi] = fwd_storage[fi];
        int fn2 = 0;
        if (nargs >= 2) {
            int aid = php_arrref_decode(args[1]);
            if (aid >= 0) {
                int ii;
                for (ii = 0; ii < g_arrays[aid].count && fn2 < PHP_ARG_MAX; ii++) {
                    strncpy(fwd[fn2], g_arrays[aid].items[ii].val, PHP_VAL_MAX - 1); fwd[fn2][PHP_VAL_MAX - 1] = 0;
                    fn2++;
                }
            }
        }
        php_invoke_callable(caller, args[0], fwd, fn2, out, outcap);
        return;
    }
    if ((strcmp(name, "sprintf") == 0 || strcmp(name, "printf") == 0) && nargs >= 1) {
        char formatted[PHP_VAL_MAX];
        php_sprintf(args[0], args, 1, nargs - 1, formatted, sizeof formatted);
        if (strcmp(name, "printf") == 0) php_emit_str(caller, formatted);
        strncpy(out, formatted, outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "file_exists") == 0 && nargs >= 1) {
        struct stat sb;
        strncpy(out, stat(args[0], &sb) == 0 ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "is_dir") == 0 && nargs >= 1) {
        struct stat sb;
        int ok = stat(args[0], &sb) == 0 && S_ISDIR(sb.st_mode);
        strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "is_file") == 0 && nargs >= 1) {
        struct stat sb;
        int ok = stat(args[0], &sb) == 0 && S_ISREG(sb.st_mode);
        strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "is_readable") == 0 && nargs >= 1) {
        strncpy(out, access(args[0], R_OK) == 0 ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "is_writable") == 0 || strcmp(name, "is_writeable") == 0) {
        if (nargs >= 1) { strncpy(out, access(args[0], W_OK) == 0 ? "1" : "0", outcap - 1); out[outcap - 1] = 0; }
        return;
    }
    if (strcmp(name, "realpath") == 0 && nargs >= 1) {
        /* Real (not stubbed) filesystem resolution -- glibc's own
         * realpath(), same call this file's other file/path builtins
         * (stat()-based file_exists/is_dir/is_file) already lean on for
         * "ask the OS, don't reimplement path logic". Real PHP returns
         * `false` (empty string here, this subset's only falsy scalar)
         * when the path doesn't exist -- template-loader.php's own
         * "$template = realpath(...); ... is_file($template) &&
         * is_readable($template)" chain already handles that case
         * correctly by simply not matching, so no special-casing needed
         * here beyond what realpath() itself does. */
        /* NOT realpath(path, NULL) -- that glibc/POSIX.1-2008 extension
         * (malloc a right-sized buffer internally) is correct C and
         * works fine under gcc, but is a real, confirmed squash codegen
         * bug: passing a literal NULL as an external (dynamically-
         * linked) function's argument silently breaks the call --
         * realpath(path, NULL) always returned NULL under a squash-
         * compiled build even for a path that genuinely exists,
         * (gcc-compiled control returned the correct resolved path for
         * the identical source). A minimal standalone repro (no
         * php_mini.c/PHP involved at all) reproduces it too, so this
         * isn't specific to this file -- it's a real squash bug,
         * flagged for its own follow-up fix. Passing an explicit,
         * generously-sized stack buffer instead (the OTHER, equally
         * standard way to call realpath()) sidesteps it entirely and is
         * just as correct. */
        char resolved[4096];
        if (realpath(args[0], resolved)) { strncpy(out, resolved, outcap - 1); out[outcap - 1] = 0; }
        else out[0] = 0;
        return;
    }
    if (strcmp(name, "str_ends_with") == 0 && nargs >= 2) {
        size_t hl = strlen(args[0]), nl = strlen(args[1]);
        int ok = nl <= hl && strcmp(args[0] + (hl - nl), args[1]) == 0;
        strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "str_starts_with") == 0 && nargs >= 2) {
        size_t nl = strlen(args[1]);
        int ok = strncmp(args[0], args[1], nl) == 0;
        strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "str_contains") == 0 && nargs >= 2) {
        int ok = args[1][0] == 0 || strstr(args[0], args[1]) != NULL;
        strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    /* Core string/array/misc builtins added this session -- found
     * completely missing (not merely buggy) via a direct call trace
     * (SQS_TRACE_CALLS) against the real wordpress-develop boot chain:
     * template-loader.php's own theme-resolution path alone routes
     * through trim()/in_array()/array_merge()/abs() (get_option(),
     * map_meta_cap(), current_user_can()'s capability-merging), none of
     * which existed anywhere in this file before -- each one silently
     * degraded to "" via the unknown-function fallback, corrupting
     * everything downstream that depended on its real return value
     * (get_option()'s own "is_scalar($value) && trim($value)" check,
     * for instance, was UNCONDITIONALLY false with no trim()). Kept
     * together as one block, same general shape as the is_string()/
     * is_object() block below: approximate real PHP semantics from this
     * subset's plain-string/arrref-string value representation, degrade
     * safely (never crash) on any shape the subset can't fully model. */
    if (strcmp(name, "trim") == 0 || strcmp(name, "ltrim") == 0 || strcmp(name, "rtrim") == 0) {
        if (nargs >= 1) {
            const char *chars = (nargs >= 2) ? args[1] : " \t\n\r\v\f";
            const char *s = args[0];
            size_t len = strlen(s);
            size_t start = 0, end = len;
            if (strcmp(name, "rtrim") != 0) { while (start < end && strchr(chars, s[start])) start++; }
            if (strcmp(name, "ltrim") != 0) { while (end > start && strchr(chars, s[end - 1])) end--; }
            size_t n = end - start;
            if (n > (size_t)(outcap - 1)) n = (size_t)(outcap - 1);
            memcpy(out, s + start, n);
            out[n] = 0;
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "md5") == 0 && nargs >= 1) {
        char hex[33];
        php_md5_hex((const unsigned char *)args[0], (unsigned long)strlen(args[0]), hex);
        strncpy(out, hex, outcap - 1); out[outcap - 1] = 0;
        return;
    }
    /* glob( "dir/prefix*suffix" ) -- real (not stubbed) directory listing,
     * added session 8 specifically because wp-includes/class-wp-
     * textdomain-registry.php's own get_language_files_from_path() (the
     * function every __()/translation lookup routes through) directly
     * branches on glob()'s real return value ("if (false === $files)
     * $files = array();" then array_merge()s a second glob() call's
     * result in) -- leaving it unimplemented didn't just return wrong
     * data, it meant every translation-file lookup ran this fallback
     * logic from scratch every time with no real listing to cache Only
     * the ONE wildcard shape actually used anywhere in the vendored tree
     * is supported: exactly one directory component followed by a
     * "prefix*suffix" pattern in the final path segment (e.g.
     * "$path*.mo") -- real PHP's glob() supports far more (multiple
     * "*", "?", "[...]", GLOB_BRACE, ...), not modeled here since
     * nothing in this codebase's own real usage needs it (matches this
     * file's usual "just enough for the real call shapes actually
     * reached" scope discipline). Uses opendir()/readdir() (see this
     * file's own top-of-file comment on their explicit prototypes) plus
     * a simple prefix/suffix match instead of real fnmatch() -- squash's
     * shim headers have no <fnmatch.h> either, and prefix/suffix
     * matching is all a single "*" pattern actually needs. Results are
     * sorted (real glob() returns entries in filesystem order, which is
     * unspecified/directory-order in practice -- WP's own code sorts or
     * doesn't care which .mo file comes first among a handful per
     * locale, so alphabetical via a simple insertion sort is a safe,
     * deterministic choice here, and determinism matters more than
     * matching a real filesystem's arbitrary order for THIS interpreter's
     * own test-repeatability). Returns a real array (arrref-encoded),
     * matching real PHP's glob() return shape -- NOT the "false" this
     * file's callers already correctly handle as their "nothing found"
     * case (an empty array degrades to falsy anyway, see php_truthy's
     * own array-emptiness rule, so real WordPress code checking either
     * "false === $files" or "empty($files)" both still work). */
    if (strcmp(name, "glob") == 0 && nargs >= 1) {
        const char *pattern = args[0];
        const char *slash = strrchr(pattern, '/');
        char dir[PHP_PATH_MAX]; char leaf_pat[256];
        if (slash) {
            int dlen = (int)(slash - pattern);
            if (dlen >= (int)sizeof dir) dlen = (int)sizeof dir - 1;
            memcpy(dir, pattern, (size_t)dlen); dir[dlen] = 0;
            strncpy(leaf_pat, slash + 1, sizeof leaf_pat - 1); leaf_pat[sizeof leaf_pat - 1] = 0;
        } else {
            strncpy(dir, ".", sizeof dir - 1); dir[sizeof dir - 1] = 0;
            strncpy(leaf_pat, pattern, sizeof leaf_pat - 1); leaf_pat[sizeof leaf_pat - 1] = 0;
        }
        const char *star = strchr(leaf_pat, '*');
        char prefix[256], suffix[256];
        if (star) {
            int plen = (int)(star - leaf_pat);
            if (plen >= (int)sizeof prefix) plen = (int)sizeof prefix - 1;
            memcpy(prefix, leaf_pat, (size_t)plen); prefix[plen] = 0;
            strncpy(suffix, star + 1, sizeof suffix - 1); suffix[sizeof suffix - 1] = 0;
        } else {
            strncpy(prefix, leaf_pat, sizeof prefix - 1); prefix[sizeof prefix - 1] = 0;
            suffix[0] = 0;
        }
        int plen = (int)strlen(prefix), slen = (int)strlen(suffix);
        /* Heap, NOT a stack local, even though it would otherwise be a
         * perfectly ordinary fixed-size array -- session 8, a real,
         * serious bug found via a native-stack-overflow crash on the
         * real WordPress boot: php_call_function() is one gigantic
         * function housing dozens of builtins' own local variables, and
         * in an UNOPTIMIZED build (squash's own codegen is always
         * effectively unoptimized -- "naive, no register allocation,"
         * see this file's own build-comment history -- and even a
         * plain "gcc -g" debug build has the same property), a stack
         * frame reserves space for EVERY local declared ANYWHERE in the
         * function, not just the branch actually taken. A 512x256 =
         * 128KB stack array here alone made EVERY SINGLE call to
         * php_call_function() -- regardless of which builtin, or even a
         * plain user function -- carry a ~128KB+ stack frame, which
         * multiplied out across a real, legitimate (if deep) recursive
         * WordPress call chain (see g_call_depth's own comment for that
         * story) blew through even a 64MB thread stack well before the
         * 500-deep g_call_depth safety net could ever trigger. Matches
         * this file's own pre-existing "PHP_OUT_MAX is 64KB... keep
         * anything that size off the stack" convention (see __db_query's
         * own raw-buffer comment) -- this just hadn't been applied here
         * yet. A flat malloc'd buffer indexed via MATCH_AT (rather than
         * a "char (*)[256]" pointer-to-array-of-256 cast) since that
         * cast syntax -- valid, ordinary C -- isn't something squash's
         * own (much simpler, hand-written) C parser was confirmed to
         * accept; flat indexing sidesteps the question entirely rather
         * than chasing whether that's a real, separate squash compiler
         * gap worth its own investigation. */
        char *matches = (char *)malloc((size_t)512 * 256);
#define MATCH_AT(i) (matches + ((size_t)(i)) * 256)
        int nmatches = 0;
        if (matches) {
            __sqs_DIR *dp = opendir(dir);
            if (dp) {
                struct __sqs_dirent *ent;
                while ((ent = readdir(dp)) != NULL && nmatches < 512) {
                    const char *fn = ent->d_name;
                    int flen = (int)strlen(fn);
                    if (flen < plen + slen) continue;
                    if (strncmp(fn, prefix, (size_t)plen) != 0) continue;
                    if (slen > 0 && strcmp(fn + flen - slen, suffix) != 0) continue;
                    if (strcmp(fn, ".") == 0 || strcmp(fn, "..") == 0) continue;
                    snprintf(MATCH_AT(nmatches), 256, "%s/%s", dir, fn);
                    nmatches++;
                }
                closedir(dp);
            }
            /* Simple insertion sort -- see this builtin's own comment on
             * why deterministic order (not real filesystem order) is
             * fine here. */
            {
                int ii, jj;
                for (ii = 1; ii < nmatches; ii++) {
                    char key[256]; strncpy(key, MATCH_AT(ii), sizeof key - 1); key[sizeof key - 1] = 0;
                    jj = ii - 1;
                    while (jj >= 0 && strcmp(MATCH_AT(jj), key) > 0) { strncpy(MATCH_AT(jj+1), MATCH_AT(jj), 255); MATCH_AT(jj+1)[255]=0; jj--; }
                    strncpy(MATCH_AT(jj+1), key, 255); MATCH_AT(jj+1)[255]=0;
                }
            }
        }
        int id = php_array_new();
        if (id >= 0 && matches) {
            int ii;
            for (ii = 0; ii < nmatches; ii++) { char k[16]; snprintf(k, sizeof k, "%d", ii); php_kv_set(&g_arrays[id], k, MATCH_AT(ii)); }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        free(matches);
#undef MATCH_AT
        return;
    }
    if (strcmp(name, "strtolower") == 0 || strcmp(name, "strtoupper") == 0) {
        if (nargs >= 1) {
            int lower = strcmp(name, "strtolower") == 0;
            const char *s = args[0];
            int o = 0;
            for (; *s && o < outcap - 1; s++) out[o++] = (char)(lower ? tolower((unsigned char)*s) : toupper((unsigned char)*s));
            out[o] = 0;
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "substr") == 0 && nargs >= 2) {
        int len = (int)strlen(args[0]);
        int start = atoi(args[1]);
        if (start < 0) { start += len; if (start < 0) start = 0; }
        if (start > len) start = len;
        int have = len - start;
        int take = (nargs >= 3) ? atoi(args[2]) : have;
        if (nargs >= 3 && take < 0) { take = have + take; if (take < 0) take = 0; }
        if (take > have) take = have;
        if (take > outcap - 1) take = outcap - 1;
        if (take > 0) memcpy(out, args[0] + start, (size_t)take);
        out[take > 0 ? take : 0] = 0;
        return;
    }
    if ((strcmp(name, "strpos") == 0 || strcmp(name, "stripos") == 0) && nargs >= 2) {
        int offset = (nargs >= 3) ? atoi(args[2]) : 0;
        int hlen = (int)strlen(args[0]);
        if (offset < 0) { offset += hlen; if (offset < 0) offset = 0; }
        if (offset > hlen) { out[0] = 0; return; }
        const char *found = NULL;
        if (strcmp(name, "strpos") == 0) {
            found = strstr(args[0] + offset, args[1]);
        } else {
            /* Case-insensitive search -- no strcasestr() dependency (not
             * universally declared without _GNU_SOURCE); a plain
             * lowercase-both-and-scan is portable and this string subset
             * is never huge. */
            char hay[PHP_VAL_MAX], needle[PHP_VAL_MAX];
            size_t hi; for (hi = 0; args[0][hi] && hi < sizeof hay - 1; hi++) hay[hi] = (char)tolower((unsigned char)args[0][hi]);
            hay[hi] = 0;
            size_t ni; for (ni = 0; args[1][ni] && ni < sizeof needle - 1; ni++) needle[ni] = (char)tolower((unsigned char)args[1][ni]);
            needle[ni] = 0;
            char *f2 = strstr(hay + offset, needle);
            if (f2) found = args[0] + (f2 - hay);
        }
        /* PHP's strpos() returns int|false -- this subset's only falsy
         * scalar is "", which is indistinguishable from a real match at
         * offset 0 if we returned "0" either way, but the two ARE
         * distinguishable here (found vs not found), so return the
         * offset text on a match and "" only when genuinely not found. */
        if (found) snprintf(out, outcap, "%d", (int)(found - args[0]));
        else out[0] = 0;
        return;
    }
    if (strcmp(name, "explode") == 0 && nargs >= 2) {
        const char *delim = args[0];
        int id = php_array_new();
        if (id >= 0 && delim[0]) {
            const char *p = args[1];
            int idx = 0;
            for (;;) {
                const char *hit = strstr(p, delim);
                char key[16]; snprintf(key, sizeof key, "%d", idx++);
                if (hit) {
                    char piece[PHP_VAL_MAX];
                    size_t plen = (size_t)(hit - p);
                    if (plen > sizeof piece - 1) plen = sizeof piece - 1;
                    memcpy(piece, p, plen); piece[plen] = 0;
                    php_kv_add(&g_arrays[id], key, piece);
                    p = hit + strlen(delim);
                } else {
                    php_kv_add(&g_arrays[id], key, p);
                    break;
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "in_array") == 0 && nargs >= 2) {
        int aid = php_arrref_decode(args[1]);
        int ok = 0;
        if (aid >= 0) {
            int i;
            for (i = 0; i < g_arrays[aid].count; i++) {
                if (strcmp(g_arrays[aid].items[i].val, args[0]) == 0) { ok = 1; break; }
            }
        }
        strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "array_key_exists") == 0 || strcmp(name, "key_exists") == 0) {
        if (nargs >= 2) {
            int aid = php_arrref_decode(args[1]);
            int ok = 0;
            if (aid >= 0) {
                int i;
                for (i = 0; i < g_arrays[aid].count; i++) if (strcmp(g_arrays[aid].items[i].key, args[0]) == 0) { ok = 1; break; }
            }
            strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_search") == 0 && nargs >= 2) {
        int aid = php_arrref_decode(args[1]);
        out[0] = 0;
        if (aid >= 0) {
            int i;
            for (i = 0; i < g_arrays[aid].count; i++) {
                if (strcmp(g_arrays[aid].items[i].val, args[0]) == 0) {
                    strncpy(out, g_arrays[aid].items[i].key, outcap - 1); out[outcap - 1] = 0;
                    break;
                }
            }
        }
        return;
    }
    if (strcmp(name, "array_intersect") == 0 || strcmp(name, "array_diff") == 0) {
        /* array_intersect($a, $b, ...): items of $a whose VALUE also
         * appears in every other array. array_diff($a, $b, ...): items
         * of $a whose value appears in NONE of the others. Only the
         * first (base) array's keys are preserved, matching real PHP.
         * Confirmed needed this session: wp-includes/option.php's own
         * wp_autoload_values_to_autoload() calls array_intersect() on
         * every single page load (part of building the "autoload IN
         * (...)" SQL for wp_load_alloptions()) -- completely missing
         * before, degrading to "" and corrupting that query's WHERE
         * clause into always matching zero rows. */
        int is_intersect = strcmp(name, "array_intersect") == 0;
        int baseid = nargs >= 1 ? php_arrref_decode(args[0]) : -1;
        int id = php_array_new();
        if (id >= 0) {
            if (baseid >= 0) {
                int i;
                for (i = 0; i < g_arrays[baseid].count; i++) {
                    const char *val = g_arrays[baseid].items[i].val;
                    int in_all_others = 1, in_any_other = 0;
                    int ai;
                    for (ai = 1; ai < nargs; ai++) {
                        int aid = php_arrref_decode(args[ai]);
                        int found = 0, j;
                        if (aid >= 0) {
                            for (j = 0; j < g_arrays[aid].count; j++) if (strcmp(g_arrays[aid].items[j].val, val) == 0) { found = 1; break; }
                        }
                        if (found) in_any_other = 1; else in_all_others = 0;
                    }
                    int keep = is_intersect ? (nargs < 2 || in_all_others) : !in_any_other;
                    if (keep) php_kv_add(&g_arrays[id], g_arrays[baseid].items[i].key, val);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_merge") == 0) {
        /* Real PHP: string keys from later arrays overwrite earlier ones
         * of the same key; numeric keys are all renumbered sequentially
         * in the result rather than overwriting by numeric value --
         * approximated here by re-pushing every numeric-keyed item under
         * a fresh index (php_kv_add) while string-keyed items go through
         * php_kv_set (overwrite-in-place). A non-array argument (this
         * subset has no way to reach real PHP's fatal TypeError here) is
         * simply skipped rather than crashing. */
        int id = php_array_new();
        if (id >= 0) {
            int ai;
            for (ai = 0; ai < nargs; ai++) {
                int aid = php_arrref_decode(args[ai]);
                if (aid < 0) continue;
                int i;
                for (i = 0; i < g_arrays[aid].count; i++) {
                    const char *k = g_arrays[aid].items[i].key;
                    char *endp;
                    strtol(k, &endp, 10);
                    int numeric_key = (k[0] != 0 && *endp == 0);
                    if (numeric_key) php_kv_add(&g_arrays[id], k, g_arrays[aid].items[i].val);
                    else php_kv_set(&g_arrays[id], k, g_arrays[aid].items[i].val);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_keys") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i; for (i = 0; i < g_arrays[aid].count; i++) {
                    char key[16]; snprintf(key, sizeof key, "%d", i);
                    php_kv_add(&g_arrays[id], key, g_arrays[aid].items[i].key);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_values") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i; for (i = 0; i < g_arrays[aid].count; i++) {
                    char key[16]; snprintf(key, sizeof key, "%d", i);
                    php_kv_add(&g_arrays[id], key, g_arrays[aid].items[i].val);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_unique") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i, j;
                for (i = 0; i < g_arrays[aid].count; i++) {
                    int dup = 0;
                    for (j = 0; j < i; j++) if (strcmp(g_arrays[aid].items[j].val, g_arrays[aid].items[i].val) == 0) { dup = 1; break; }
                    if (!dup) php_kv_add(&g_arrays[id], g_arrays[aid].items[i].key, g_arrays[aid].items[i].val);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_flip") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i; for (i = 0; i < g_arrays[aid].count; i++) php_kv_set(&g_arrays[id], g_arrays[aid].items[i].val, g_arrays[aid].items[i].key);
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "abs") == 0 && nargs >= 1) {
        double v = strtod(args[0], NULL);
        if (v < 0) v = -v;
        if (v == (long)v) snprintf(out, outcap, "%ld", (long)v);
        else snprintf(out, outcap, "%g", v);
        return;
    }
    if ((strcmp(name, "min") == 0 || strcmp(name, "max") == 0) && nargs >= 1) {
        /* No array-argument form ("min(array(...))") -- only the
         * variadic scalar-args form, which is by far the more common
         * shape in WordPress core; falls back to the first argument
         * unmodified if given a single (presumably array) argument, same
         * safe-degrade convention as everywhere else in this file. */
        int ismin = strcmp(name, "min") == 0;
        double best = strtod(args[0], NULL);
        const char *beststr = args[0];
        int i;
        for (i = 1; i < nargs; i++) {
            double v = strtod(args[i], NULL);
            if ((ismin && v < best) || (!ismin && v > best)) { best = v; beststr = args[i]; }
        }
        strncpy(out, beststr, outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "addslashes") == 0 && nargs >= 1) {
        int o = 0;
        const char *p;
        for (p = args[0]; *p && o < outcap - 2; p++) {
            if (*p == '\'' || *p == '"' || *p == '\\' || *p == 0) out[o++] = '\\';
            out[o++] = *p;
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "substr_replace") == 0 && nargs >= 3) {
        int slen = (int)strlen(args[0]);
        int start = atoi(args[1]);
        if (start < 0) { start += slen; if (start < 0) start = 0; }
        if (start > slen) start = slen;
        int have = slen - start;
        int rmlen = (nargs >= 4) ? atoi(args[3]) : have;
        if (nargs >= 4 && rmlen < 0) { rmlen = have + rmlen; if (rmlen < 0) rmlen = 0; }
        if (rmlen > have) rmlen = have;
        int o = 0;
        if (start > 0 && o < outcap - 1) { int n = start; if (n > outcap - 1 - o) n = outcap - 1 - o; memcpy(out + o, args[0], (size_t)n); o += n; }
        {
            const char *rep = args[2];
            int n = (int)strlen(rep); if (n > outcap - 1 - o) n = outcap - 1 - o;
            if (n > 0) { memcpy(out + o, rep, (size_t)n); o += n; }
        }
        {
            const char *rest = args[0] + start + rmlen;
            int n = (int)strlen(rest); if (n > outcap - 1 - o) n = outcap - 1 - o;
            if (n > 0) { memcpy(out + o, rest, (size_t)n); o += n; }
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "version_compare") == 0 && nargs >= 2) {
        /* Simplified dotted-numeric-segment comparison -- real PHP's
         * version_compare() also understands alpha/beta/rc/dev suffixes;
         * not modeled here, but the plain-numeric-dotted case (by far
         * the common WordPress core usage, e.g. comparing PHP_VERSION
         * against a required minimum) works correctly. */
        int cmp = 0;
        const char *a = args[0], *b = args[1];
        while (*a || *b) {
            int an = 0, bn = 0;
            while (*a && *a != '.') { if (*a >= '0' && *a <= '9') an = an * 10 + (*a - '0'); a++; }
            while (*b && *b != '.') { if (*b >= '0' && *b <= '9') bn = bn * 10 + (*b - '0'); b++; }
            if (an != bn) { cmp = an < bn ? -1 : 1; break; }
            if (*a == '.') a++;
            if (*b == '.') b++;
        }
        if (nargs >= 3) {
            const char *op = args[2];
            int ok;
            if (strcmp(op, "<") == 0 || strcmp(op, "lt") == 0) ok = cmp < 0;
            else if (strcmp(op, "<=") == 0 || strcmp(op, "le") == 0) ok = cmp <= 0;
            else if (strcmp(op, ">") == 0 || strcmp(op, "gt") == 0) ok = cmp > 0;
            else if (strcmp(op, ">=") == 0 || strcmp(op, "ge") == 0) ok = cmp >= 0;
            else if (strcmp(op, "!=") == 0 || strcmp(op, "<>") == 0 || strcmp(op, "ne") == 0) ok = cmp != 0;
            else ok = cmp == 0;
            strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        } else snprintf(out, outcap, "%d", cmp);
        return;
    }
    if (strcmp(name, "class_exists") == 0 && nargs >= 1) {
        strncpy(out, php_class_find(args[0]) ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "interface_exists") == 0 || strcmp(name, "trait_exists") == 0 || strcmp(name, "enum_exists") == 0) {
        /* Interfaces/traits/enums aren't modeled as their own kind here
         * (see this file's top comment) -- always false is the safe
         * answer (matches "this thing doesn't exist as a class", which
         * is also literally true for these). */
        if (nargs >= 1) { strncpy(out, "0", outcap - 1); out[outcap - 1] = 0; }
        return;
    }
    if (strcmp(name, "is_callable") == 0 && nargs >= 1) {
        /* Only the "plain function/method name string" form is checked
         * (matches call_user_func's own documented scope limit above) --
         * an [$obj,'method']/['Class','method'] array-callable can't be
         * represented as this subset's flat-string args, so always false
         * for those (safe: nothing here ever tries to actually invoke
         * one of those shapes either, so a false negative here just
         * means the caller's own "can I call this" guard says no,
         * matching what would actually happen if it tried). */
        int found = php_func_find(args[0]) != NULL;
        if (!found) {
            const char *builtins[] = {
                "define", "defined", "function_exists", "sprintf", "printf",
                "file_exists", "is_dir", "is_file", "is_readable", "is_writable",
                "realpath", "strlen", "dirname", "str_ends_with", "str_starts_with",
                "str_contains", "implode", "join", "trim", "ltrim", "rtrim",
                "strtolower", "strtoupper", "substr", "strpos", "stripos", "explode",
                "in_array", "array_merge", "array_keys", "array_values", "abs",
                "min", "max", "addslashes", "class_exists", "is_string", "is_array",
                "is_object", "is_numeric", "is_int", "is_bool", NULL
            };
            int i;
            for (i = 0; builtins[i]; i++) if (strcmp(args[0], builtins[i]) == 0) { found = 1; break; }
        }
        strncpy(out, found ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "microtime") == 0) {
        /* Real wall-clock time, not a fixed stub -- WordPress core uses
         * microtime(true) for genuine elapsed-time measurement (e.g.
         * timer_start()/timer_stop() query-time logging) as well as
         * cache-busting/uniqueness seeds; a constant value would make
         * both silently wrong (a zero measured duration, a non-unique
         * "unique" seed) in a way that's easy to miss. */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        long usec = ts.tv_nsec / 1000;
        int as_float = nargs >= 1 && php_truthy(args[0]);
        if (as_float) snprintf(out, outcap, "%.8f", (double)ts.tv_sec + (double)usec / 1000000.0);
        else snprintf(out, outcap, "0.%06ld %ld", usec, (long)ts.tv_sec);
        return;
    }
    if (strcmp(name, "assert") == 0) {
        /* No real assertion-failure handling modeled -- always "pass" */
        strncpy(out, "1", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "ini_set") == 0 || strcmp(name, "ini_get") == 0 || strcmp(name, "error_reporting") == 0 ||
        strcmp(name, "set_time_limit") == 0 || strcmp(name, "spl_autoload_register") == 0 ||
        strcmp(name, "register_shutdown_function") == 0 || strcmp(name, "set_error_handler") == 0 ||
        strcmp(name, "set_exception_handler") == 0 || strcmp(name, "declare") == 0) {
        /* Genuine no-ops -- this subset has no real PHP runtime
         * configuration, autoloading, or shutdown-hook mechanism to wire
         * these into (see this file's top comment on scope); each is
         * common enough in real WordPress boot code (especially
         * spl_autoload_register(), used throughout wp-includes/class-*
         * bootstrapping and vendored libraries) that leaving it as an
         * unknown-function no-op was already harmless -- registering it
         * explicitly here just documents that this is a deliberate,
         * considered no-op rather than an accidental gap. */
        out[0] = 0;
        return;
    }
    /* is_string()/is_object()/is_numeric()/is_int()/is_null() -- NONE of
     * these existed anywhere before (confirmed as a real, high-impact
     * gap this session: wp-includes/template-loader.php's own
     * "$is_stringy = is_string($template) || (is_object($template) &&
     * method_exists(...));" always evaluated to "" (both branches
     * unknown-function-false), so $template was unconditionally forced
     * to null two lines later regardless of whether template resolution
     * had actually found a real theme file -- the WHOLE reason a fully
     * successful boot chain still rendered a blank page). This
     * interpreter has no real per-variable type tag (every value is
     * just a plain string, with the object/array-reference forms being
     * plain strings with a reserved \x01-prefixed marker -- see
     * php_objref_encode's/php_arrref_encode's own comments), so these
     * approximate real PHP's type predicates from the VALUE's shape
     * rather than a stored type: is_object/is_array are exactly right
     * (the reference-token shape IS the real distinguishing feature),
     * is_string is right for every plain scalar (including ones that
     * happen to look numeric, matching how a value fetched from this
     * subset's own string-typed DB layer would behave in real PHP too),
     * and is_numeric/is_int examine the text shape directly since
     * that's genuinely how PHP's own is_numeric() works regardless of
     * a value's underlying representation. */
    if (strcmp(name, "is_string") == 0 && nargs >= 1) {
        int is_str = php_objref_decode(args[0]) < 0 && php_arrref_decode(args[0]) < 0;
        strncpy(out, is_str ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "is_object") == 0 && nargs >= 1) {
        strncpy(out, php_objref_decode(args[0]) >= 0 ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "is_null") == 0 && nargs >= 1) {
        strncpy(out, args[0][0] == 0 ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "is_numeric") == 0 && nargs >= 1) {
        char *endp;
        strtod(args[0], &endp);
        int ok = args[0][0] != 0 && *endp == 0;
        strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "is_int") == 0 || strcmp(name, "is_integer") == 0 || strcmp(name, "is_long") == 0) {
        if (nargs >= 1) {
            char *endp;
            strtol(args[0], &endp, 10);
            int ok = args[0][0] != 0 && *endp == 0;
            strncpy(out, ok ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        }
        return;
    }
    if (strcmp(name, "is_bool") == 0 || strcmp(name, "is_float") == 0 || strcmp(name, "is_double") == 0 || strcmp(name, "is_scalar") == 0) {
        /* No distinct bool/float type in this subset to check against --
         * is_scalar() specifically is common enough (WordPress core uses
         * it as a cheap "not an array/object" guard) that a safe, useful
         * approximation matters: true for anything that isn't an
         * object/array reference, same shape-based test as is_string(). */
        if (nargs >= 1) {
            if (strcmp(name, "is_scalar") == 0) {
                int is_scalar = php_objref_decode(args[0]) < 0 && php_arrref_decode(args[0]) < 0;
                strncpy(out, is_scalar ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
            } else {
                strncpy(out, "0", outcap - 1); out[outcap - 1] = 0;
            }
        }
        return;
    }
    if (strcmp(name, "method_exists") == 0 && nargs >= 2) {
        int oid = php_objref_decode(args[0]);
        PhpClass *cls = oid >= 0 ? php_class_find(g_objects[oid].class_name) : NULL;
        strncpy(out, php_class_find_method(cls, args[1]) ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "get_class") == 0) {
        int oid = nargs >= 1 ? php_objref_decode(args[0]) : (caller->has_this ? caller->this_obj_id : -1);
        if (oid >= 0) { strncpy(out, g_objects[oid].class_name, outcap - 1); out[outcap - 1] = 0; }
        else out[0] = 0;
        return;
    }
    if (strcmp(name, "preg_match") == 0 && nargs >= 2) {
        /* No 3rd "&$matches" output-array support -- see the regex
         * engine's own top comment. */
        RToken toks[RTOK_MAX];
        int ntoks = regex_compile_pattern(args[0], toks, RTOK_MAX);
        int mstart = 0, mend = 0;
        int found = regex_search(toks, ntoks, args[1], &mstart, &mend);
        snprintf(out, outcap, "%d", found);
        return;
    }
    if (strcmp(name, "preg_match_all") == 0 && nargs >= 2) {
        RToken toks[RTOK_MAX];
        int ntoks = regex_compile_pattern(args[0], toks, RTOK_MAX);
        int count = 0, pos = 0, slen = (int)strlen(args[1]), iterations = 0;
        while (pos <= slen && iterations < PHP_LOOP_MAX) {
            iterations++;
            int mstart = -1, mend = -1, si;
            for (si = pos; si <= slen; si++) {
                int r = regex_match_here(toks, ntoks, 0, args[1], si, slen);
                if (r >= 0) { mstart = si; mend = r; break; }
            }
            if (mstart < 0) break;
            count++;
            pos = (mend > mstart) ? mend : mstart + 1;
        }
        snprintf(out, outcap, "%d", count);
        return;
    }
    if (strcmp(name, "preg_replace") == 0 && nargs >= 3) {
        RToken toks[RTOK_MAX];
        int ntoks = regex_compile_pattern(args[0], toks, RTOK_MAX);
        const char *subj = args[2];
        const char *repl = args[1];
        int slen = (int)strlen(subj);
        int o = 0, pos = 0, iterations = 0;
        for (;;) {
            if (++iterations > PHP_LOOP_MAX) break;
            int mstart = -1, mend = -1, si;
            for (si = pos; si <= slen; si++) {
                int r = regex_match_here(toks, ntoks, 0, subj, si, slen);
                if (r >= 0) { mstart = si; mend = r; break; }
            }
            if (mstart < 0) {
                while (pos < slen && o < outcap - 1) out[o++] = subj[pos++];
                break;
            }
            while (pos < mstart && o < outcap - 1) out[o++] = subj[pos++];
            const char *rp = repl;
            while (*rp && o < outcap - 1) out[o++] = *rp++;
            if (mend == mstart) {
                if (pos < slen && o < outcap - 1) out[o++] = subj[pos];
                pos++;
            } else {
                pos = mend;
            }
            if (pos > slen) break;
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "preg_quote") == 0 && nargs >= 1) {
        /* NOT "static const char specials[] = ...;" -- a real, confirmed
         * squash codegen bug: a LOCAL variable's "static" storage class
         * combined with a string-literal initializer silently breaks
         * (reproduced in a minimal standalone repro with no PHP/this
         * file involved at all: "static const char *p = \"abc\";
         * printf(p);" prints "(null)" under a squash-compiled build,
         * "abc" under gcc; "static char x[] = \"abc\";" prints "(null)"
         * too; "const char x[] = \"abc\";" -- an array, no "static" --
         * segfaults outright). A plain non-static local with the same
         * initializer works correctly and costs nothing extra here
         * (this string is tiny and the function isn't hot), so that's
         * the fix -- the "static" bug itself is flagged for its own
         * dedicated root-cause session, not chased further here. */
        const char *specials = ".\\+*?[^]$(){}=!<>|:-#/";
        int o = 0;
        const char *p;
        for (p = args[0]; *p && o < outcap - 2; p++) {
            if (strchr(specials, *p)) out[o++] = '\\';
            out[o++] = *p;
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "implode") == 0 || strcmp(name, "join") == 0) {
        /* implode($glue, $array) or implode($array) (glue defaults to
         * "") -- iterates the array's KV items directly in C (same
         * "loops belong in the glue builtin, not in this PHP subset's
         * own for/foreach" reasoning as __db_insert()'s own comment,
         * though foreach DOES work now too -- this is just simpler/
         * cheaper than writing it out in PHP for something this common). */
        const char *glue = "";
        int arr_idx = 0;
        if (nargs >= 2) { glue = args[0]; arr_idx = 1; }
        int aid = nargs > arr_idx ? php_arrref_decode(args[arr_idx]) : -1;
        int o = 0;
        if (aid >= 0) {
            PhpKVArray *a = &g_arrays[aid];
            int i;
            for (i = 0; i < a->count; i++) {
                o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "%s%s", i ? glue : "", a->items[i].val);
            }
        }
        if (o == 0) out[0] = 0;
        return;
    }
    if (strcmp(name, "strlen") == 0 && nargs >= 1) {
        snprintf(out, outcap, "%d", (int)strlen(args[0]));
        return;
    }
    if (strcmp(name, "dirname") == 0 && nargs >= 1) {
        const char *slash = strrchr(args[0], '/');
        if (slash) {
            int dl = (int)(slash - args[0]);
            if (dl == 0) dl = 1; /* "/" stays "/" */
            if (dl >= outcap) dl = outcap - 1;
            memcpy(out, args[0], (size_t)dl);
            out[dl] = 0;
        } else {
            strncpy(out, ".", outcap - 1); out[outcap - 1] = 0;
        }
        return;
    }

    /* ── native database engine glue (SQS/db_engine.c) ───────────────
     * These four are the only bridge between this interpreter and the
     * native engine; everything WordPress-shaped (the $wpdb class,
     * table-name properties, prepare()'s placeholder substitution) is
     * real PHP source, not builtins -- see wp-includes/class-wpdb.php in
     * the served WordPress tree. */
    if (strcmp(name, "__db_open") == 0 && nargs >= 1) {
        sqdb_open(args[0]);
        out[0] = 0;
        return;
    }
    if (strcmp(name, "__db_exec") == 0 && nargs >= 1) {
        if (getenv("SQS_TRACE_SQL")) fprintf(stderr, "[SQL EXEC] %s\n", args[0]);
        int r = sqdb_exec(args[0], g_db_last_error, sizeof g_db_last_error);
        if (getenv("SQS_TRACE_SQL") && r < 0) fprintf(stderr, "[SQL EXEC ERR] %s\n", g_db_last_error);
        if (r >= 0) g_db_last_error[0] = 0;
        snprintf(out, outcap, "%d", r);
        return;
    }
    if (strcmp(name, "__db_query") == 0 && nargs >= 1) {
        if (getenv("SQS_TRACE_SQL")) fprintf(stderr, "[SQL QUERY] %s\n", args[0]);
        /* Heap, not a stack local -- PHP_OUT_MAX is 64KB and this file's
         * own convention (see php_call_function's PhpState comment
         * further down) is to keep anything that size off the stack. */
        char *raw = (char *)malloc(PHP_OUT_MAX);
        int r = raw ? sqdb_query(args[0], raw, PHP_OUT_MAX) : -1;
        if (!raw) { snprintf(g_db_last_error, sizeof g_db_last_error, "out of memory"); g_db_last_raw[0] = 0; }
        else if (r < 0) { strncpy(g_db_last_error, raw, sizeof g_db_last_error - 1); g_db_last_error[sizeof g_db_last_error - 1] = 0; g_db_last_raw[0] = 0; if (getenv("SQS_TRACE_SQL")) fprintf(stderr, "[SQL QUERY ERR] %s\n", g_db_last_error); }
        else { g_db_last_error[0] = 0; strncpy(g_db_last_raw, raw, sizeof g_db_last_raw - 1); g_db_last_raw[sizeof g_db_last_raw - 1] = 0; }

        int arr_id = php_array_new();
        if (arr_id < 0) { free(raw); out[0] = 0; return; }
        if (raw && r > 0) {
            /* Packed format from db_engine.c's own header comment: line 1
             * is comma-separated column names, each following line is one
             * row of tab-separated, backslash-escaped field values -- the
             * SAME escaping db_engine.c's own (also #include'd into this
             * translation unit, so directly callable) db_unescape_field()
             * already implements, reused here rather than duplicated. */
            char cols[PHP_KV_MAX][128];
            int ncols = 0;
            const char *p = raw;
            const char *lend = strchr(p, '\n');
            int llen = lend ? (int)(lend - p) : (int)strlen(p);
            const char *cp = p, *cend = p + llen;
            while (cp < cend && ncols < PHP_KV_MAX) {
                const char *comma = memchr(cp, ',', (size_t)(cend - cp));
                const char *fend = comma ? comma : cend;
                int flen = (int)(fend - cp);
                if (flen >= (int)sizeof cols[0]) flen = (int)sizeof cols[0] - 1;
                memcpy(cols[ncols], cp, (size_t)flen);
                cols[ncols][flen] = 0;
                ncols++;
                cp = comma ? comma + 1 : cend;
            }
            p = lend ? lend + 1 : cend;

            int rowidx = 0;
            while (*p) {
                lend = strchr(p, '\n');
                llen = lend ? (int)(lend - p) : (int)strlen(p);
                int oid = php_object_new("stdClass");
                if (oid >= 0) {
                    cp = p; cend = p + llen;
                    int ci = 0;
                    while (ci < ncols) {
                        const char *tab = memchr(cp, '\t', (size_t)(cend - cp));
                        const char *fe = tab ? tab : cend;
                        char fieldbuf[PHP_VAL_MAX];
                        int flen = (int)(fe - cp);
                        if (flen >= (int)sizeof fieldbuf) flen = (int)sizeof fieldbuf - 1;
                        memcpy(fieldbuf, cp, (size_t)flen);
                        fieldbuf[flen] = 0;
                        char unesc[PHP_VAL_MAX];
                        db_unescape_field(fieldbuf, unesc, sizeof unesc);
                        php_kv_set(&g_objects[oid].props, cols[ci], unesc);
                        ci++;
                        cp = tab ? tab + 1 : cend;
                    }
                    char enc[32], idxbuf[16];
                    php_objref_encode(oid, enc, sizeof enc);
                    snprintf(idxbuf, sizeof idxbuf, "%d", rowidx);
                    php_kv_set(&g_arrays[arr_id], idxbuf, enc);
                    rowidx++;
                }
                if (!lend) break;
                p = lend + 1;
            }
        }
        free(raw);
        php_arrref_encode(arr_id, out, outcap);
        return;
    }
    if (strcmp(name, "__db_insert_id") == 0) {
        snprintf(out, outcap, "%ld", sqdb_insert_id());
        return;
    }
    if (strcmp(name, "__db_last_error") == 0) {
        strncpy(out, g_db_last_error, outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "__db_get_var") == 0) {
        int col = nargs >= 1 ? atoi(args[0]) : 0;
        int row = nargs >= 2 ? atoi(args[1]) : 0;
        if (!php_db_raw_field(g_db_last_raw, row, col, out, outcap)) out[0] = 0;
        return;
    }
    if (strcmp(name, "__db_get_col") == 0) {
        int col = nargs >= 1 ? atoi(args[0]) : 0;
        int arr_id = php_array_new();
        if (arr_id < 0) { out[0] = 0; return; }
        int r = 0;
        char val[PHP_VAL_MAX], idxbuf[16];
        while (php_db_raw_field(g_db_last_raw, r, col, val, sizeof val)) {
            snprintf(idxbuf, sizeof idxbuf, "%d", r);
            php_kv_set(&g_arrays[arr_id], idxbuf, val);
            r++;
        }
        php_arrref_encode(arr_id, out, outcap);
        return;
    }
    /* insert()/update()/delete() build their SQL from a PHP array
     * argument (e.g. "$wpdb->insert($table, array('option_name'=>$n,
     * ...))") -- decoding it HERE via php_arrref_decode() and iterating
     * in C, rather than in the $wpdb PHP source itself, sidesteps this
     * interpreter subset having no working "foreach"/"while"/"for" (see
     * php_run_statement's own comment on why those are parsed-and-
     * skipped, not executed) -- there is no PHP-level way to iterate an
     * array of unknown size/keys in this subset today. db_engine.c still
     * never sees a PhpKVArray -- only the finished SQL string. */
    if (strcmp(name, "__db_insert") == 0 && nargs >= 2) {
        int aid = php_arrref_decode(args[1]);
        if (aid < 0) { g_db_last_error[0] = 0; strncpy(out, "-1", outcap - 1); out[outcap - 1] = 0; return; }
        PhpKVArray *data = &g_arrays[aid];
        char *sql = (char *)malloc(PHP_OUT_MAX);
        if (!sql) { strncpy(out, "-1", outcap - 1); out[outcap - 1] = 0; return; }
        int o = snprintf(sql, PHP_OUT_MAX, "INSERT INTO %s (", args[0]);
        int i;
        for (i = 0; i < data->count; i++) o += snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), "%s%s", i ? "," : "", data->items[i].key);
        o += snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), ") VALUES (");
        for (i = 0; i < data->count; i++) {
            char esc[PHP_VAL_MAX * 2 + 4];
            php_db_sql_quote(data->items[i].val, esc, sizeof esc);
            o += snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), "%s%s", i ? "," : "", esc);
        }
        snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), ")");
        int r = sqdb_exec(sql, g_db_last_error, sizeof g_db_last_error);
        if (r >= 0) g_db_last_error[0] = 0;
        free(sql);
        snprintf(out, outcap, "%d", r);
        return;
    }
    if (strcmp(name, "__db_update") == 0 && nargs >= 3) {
        int aid = php_arrref_decode(args[1]);
        int wid = php_arrref_decode(args[2]);
        if (aid < 0) { strncpy(out, "-1", outcap - 1); out[outcap - 1] = 0; return; }
        PhpKVArray *data = &g_arrays[aid];
        char *sql = (char *)malloc(PHP_OUT_MAX);
        if (!sql) { strncpy(out, "-1", outcap - 1); out[outcap - 1] = 0; return; }
        int o = snprintf(sql, PHP_OUT_MAX, "UPDATE %s SET ", args[0]);
        int i;
        for (i = 0; i < data->count; i++) {
            char esc[PHP_VAL_MAX * 2 + 4];
            php_db_sql_quote(data->items[i].val, esc, sizeof esc);
            o += snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), "%s%s=%s", i ? "," : "", data->items[i].key, esc);
        }
        if (wid >= 0 && g_arrays[wid].count > 0) {
            PhpKVArray *w = &g_arrays[wid];
            o += snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), " WHERE ");
            for (i = 0; i < w->count; i++) {
                char esc[PHP_VAL_MAX * 2 + 4];
                php_db_sql_quote(w->items[i].val, esc, sizeof esc);
                o += snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), "%s%s=%s", i ? " AND " : "", w->items[i].key, esc);
            }
        }
        int r = sqdb_exec(sql, g_db_last_error, sizeof g_db_last_error);
        if (r >= 0) g_db_last_error[0] = 0;
        free(sql);
        snprintf(out, outcap, "%d", r);
        return;
    }
    if (strcmp(name, "__db_delete") == 0 && nargs >= 2) {
        int wid = php_arrref_decode(args[1]);
        char *sql = (char *)malloc(PHP_OUT_MAX);
        if (!sql) { strncpy(out, "-1", outcap - 1); out[outcap - 1] = 0; return; }
        int o = snprintf(sql, PHP_OUT_MAX, "DELETE FROM %s", args[0]);
        if (wid >= 0 && g_arrays[wid].count > 0) {
            PhpKVArray *w = &g_arrays[wid];
            o += snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), " WHERE ");
            int i;
            for (i = 0; i < w->count; i++) {
                char esc[PHP_VAL_MAX * 2 + 4];
                php_db_sql_quote(w->items[i].val, esc, sizeof esc);
                o += snprintf(sql + o, (size_t)(PHP_OUT_MAX - o), "%s%s=%s", i ? " AND " : "", w->items[i].key, esc);
            }
        }
        int r = sqdb_exec(sql, g_db_last_error, sizeof g_db_last_error);
        if (r >= 0) g_db_last_error[0] = 0;
        free(sql);
        snprintf(out, outcap, "%d", r);
        return;
    }
    if (strcmp(name, "__db_is_select") == 0 && nargs >= 1) {
        const char *p = args[0];
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        strncpy(out, strncasecmp(p, "SELECT", 6) == 0 ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "__db_escape") == 0 && nargs >= 1) {
        /* Same escaping as php_db_sql_quote(), minus the surrounding
         * quotes -- real wpdb::escape()/_real_escape() return the
         * escaped text unquoted; callers wrap it in quotes themselves. */
        int o = 0;
        const char *p;
        for (p = args[0]; *p && o < outcap - 2; p++) {
            if (*p == '\'' || *p == '\\') out[o++] = '\\';
            out[o++] = *p;
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "__db_prepare") == 0 && nargs >= 1) {
        /* %s/%d/%f placeholder substitution, positional against args[1..]
         * -- this interpreter subset has no variadic params/func_get_args
         * (see this file's top comment on why everything here is
         * deliberately bounded), so the $wpdb-side prepare() method
         * always forwards a FIXED number of slots (see class-wpdb.php's
         * own prepare()); however many of the query's OWN %s/%d/%f
         * tokens there are is what actually determines how many get
         * consumed, so unused trailing slots are simply never read.
         * Real WordPress prepare() also accepts a single array as the
         * second argument instead of N scalar args -- NOT supported
         * here (documented scope limit; the multi-scalar-arg form is by
         * far the more common call shape in WordPress core). */
        int o = 0;
        int argi = 1;
        const char *p;
        for (p = args[0]; *p && o < outcap - 1; p++) {
            if (*p == '%' && p[1] == '%') { out[o++] = '%'; p++; continue; }
            if (*p == '%' && (p[1] == 's' || p[1] == 'd' || p[1] == 'f') && argi < nargs) {
                const char *val = args[argi++];
                if (p[1] == 's') {
                    char esc[PHP_VAL_MAX * 2 + 4];
                    php_db_sql_quote(val, esc, sizeof esc);
                    int el = (int)strlen(esc);
                    if (el > outcap - 1 - o) el = outcap - 1 - o;
                    memcpy(out + o, esc, (size_t)el);
                    o += el;
                } else if (p[1] == 'd') {
                    o += snprintf(out + o, (size_t)(outcap - o), "%ld", strtol(val, NULL, 10));
                } else {
                    o += snprintf(out + o, (size_t)(outcap - o), "%g", strtod(val, NULL));
                }
                p++;
                continue;
            }
            out[o++] = *p;
        }
        out[o] = 0;
        return;
    }

    PhpFunc *fn = php_func_find(name);
    if (!fn) {
        if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[UNKNOWN FUNC] %s()\n", name);
        out[0] = 0; return; /* unknown function: no-op, don't crash */
    }
    /* Real, confirmed-crashing session-8 bug, fixed here as a safety net:
     * a genuine native-C-stack overflow (SIGSEGV), NOT a PHP-level
     * infinite loop -- found via gdb on the gcc-compiled harness against
     * the real WordPress boot after this session's earlier fixes (do-
     * while, $GLOBALS, foreach-over-a-property-chain, instanceof, static
     * locals) let much more of WordPress's real control flow actually
     * execute. Root cause: WordPress's own user-bootstrapping functions
     * (_wp_get_current_user() -> wp_set_current_user() -> setup_userdata()
     * -> get_current_user_id() -> wp_get_current_user() -> ...) are
     * MUTUALLY recursive BY DESIGN in real PHP, relying on a "global
     * $current_user; if (already set) return early;" guard to terminate
     * after exactly one real recursive hop. Isolated minimal repros of
     * that exact guard pattern (global + instanceof + empty()) all
     * terminate correctly in this interpreter (see this session's own
     * notes) -- so the guard logic itself isn't the bug; something
     * specific to the FULL real call chain (not yet fully root-caused --
     * flagged for next session, see this file's own top-of-session
     * report) keeps the recursion going far deeper than the ~1-2 hops
     * real PHP takes, eventually exhausting even a 64MB stack. This
     * counter doesn't fix THAT root cause, but converts the failure mode
     * from "crash the whole server process" into "a deeply-nested call
     * degrades to returning '' instead of recursing further" -- the same
     * safety-net philosophy already applied to expression nesting depth
     * (see PHP_EXPR_DEPTH_MAX's own comment) and loop iteration counts
     * (PHP_LOOP_MAX), just for actual FUNCTION/METHOD call depth, which
     * neither of those existing guards covers (a single php_eval_expr
     * call's own internal nesting resets to 0 between separate top-level
     * statements, so it doesn't accumulate across a chain of distinct
     * function calls the way this new counter does). Generous (500) --
     * real WordPress code, even with legitimate hook/filter recursion,
     * doesn't nest anywhere near this deep; only a genuine runaway
     * pattern like this one does. */
    if (g_call_depth >= PHP_CALL_DEPTH_MAX) {
        if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[CALL DEPTH LIMIT] %s() at depth %d\n", name, g_call_depth);
        out[0] = 0; return;
    }
    g_call_depth++;

    /* Heap-allocated, not a plain stack local -- PhpState is genuinely
     * huge (each of its PHP_MAX_VARS PhpVar slots embeds a full
     * PhpKVArray for array support, ~36KB apiece -- multiple MB per
     * PhpState) now that arrays exist, and every nested function/method
     * call puts one more of these on the stack. Real WordPress code
     * routinely nests function calls several levels deep (confirmed via
     * a real crash this session: wp_check_php_mysql_versions() ->
     * wp_load_translations_early() -> several more calls deep hit a
     * stack overflow with `callee` as a stack local, on both gcc- and
     * squash-compiled builds -- not a compiler bug, just an
     * unsustainable amount of stack per call). A malloc'd PhpState per
     * call costs one heap round-trip instead, which is unbounded by
     * stack depth. */
    PhpState *callee = (PhpState *)malloc(sizeof *callee);
    memset(callee, 0, sizeof *callee);
    callee->get = caller->get;
    callee->post = caller->post;
    strncpy(callee->server_method, caller->server_method, sizeof callee->server_method - 1);
    callee->out = caller->out;
    callee->out_len = caller->out_len;
    callee->out_cap = caller->out_cap;
    strncpy(callee->cur_dir, caller->cur_dir, sizeof callee->cur_dir - 1);
    strncpy(callee->cur_file, caller->cur_file, sizeof callee->cur_file - 1);
    { int ai2; callee->n_raw_args = nargs < PHP_ARG_MAX ? nargs : PHP_ARG_MAX;
      for (ai2 = 0; ai2 < callee->n_raw_args; ai2++) { strncpy(callee->raw_args[ai2], args[ai2], PHP_VAL_MAX - 1); callee->raw_args[ai2][PHP_VAL_MAX - 1] = 0; } }
    int i;
    for (i = 0; i < fn->nparams; i++) {
        if (fn->variadic[i]) {
            /* "...$name" -- collect every remaining positional argument
             * (index i..nargs-1) into a REAL array bound to this param,
             * matching real PHP's variadic semantics -- see PhpFunc.
             * variadic's own comment (WordPress's apply_filters() is the
             * motivating real case). Only meaningful on the LAST
             * parameter (same restriction real PHP enforces at parse
             * time); this file doesn't itself re-validate that, it just
             * does the right thing regardless of position. */
            PhpVar *v = php_var_find_or_create(callee, fn->params[i]);
            if (v) {
                v->is_array = 1;
                int ai, ki = 0;
                for (ai = i; ai < nargs; ai++) {
                    char k[16]; snprintf(k, sizeof k, "%d", ki++);
                    php_kv_set(&v->arr, k, args[ai]);
                }
                v->next_index = ki;
            }
        } else {
            php_var_set(callee, fn->params[i], i < nargs ? args[i] : "");
        }
    }
    callee->src = fn->body;
    php_run_statements(callee);
    caller->out_len = callee->out_len; /* propagate anything the function echoed */
    strncpy(out, callee->returning ? callee->retval : "", outcap - 1); out[outcap - 1] = 0;
    php_state_free_local_arrays(callee);
    free(callee);
    g_call_depth--;
}

/* Calls a method on object `obj_id` -- identical in every respect to
 * php_call_function()'s own user-function-call path (fresh isolated
 * local scope, shared output-buffer position, shared __DIR__/__FILE__),
 * except the callee also gets "$this" bound (has_this/this_obj_id --
 * see PhpState's own comment), which php_eval_factor's "$" branch reads
 * as an object-reference string the same way any other object-valued
 * variable would be. No property auto-binding into local scope (real
 * PHP doesn't do that either -- a method reads its own object's
 * properties via "$this->prop", not a bare "$prop"). */
static void php_call_method(PhpState *caller, int obj_id, PhpMethod *m, char **args, int nargs, char *out, int outcap) {
    /* See php_call_function's own comment on g_call_depth -- same
     * runaway-recursion safety net, needed here too since the actual
     * crashing chain found this session (_wp_get_current_user() ->
     * wp_set_current_user() -> setup_userdata() -> get_current_user_id())
     * is a mix of plain function AND method calls. */
    if (g_call_depth >= PHP_CALL_DEPTH_MAX) {
        if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[CALL DEPTH LIMIT] ->%s() at depth %d\n", m->name, g_call_depth);
        out[0] = 0; return;
    }
    g_call_depth++;
    /* Heap-allocated -- see php_call_function's own comment on why a
     * stack-local PhpState here is a real stack-overflow risk now that
     * arrays exist. */
    PhpState *callee = (PhpState *)malloc(sizeof *callee);
    memset(callee, 0, sizeof *callee);
    callee->get = caller->get;
    callee->post = caller->post;
    strncpy(callee->server_method, caller->server_method, sizeof callee->server_method - 1);
    callee->out = caller->out;
    callee->out_len = caller->out_len;
    callee->out_cap = caller->out_cap;
    strncpy(callee->cur_dir, caller->cur_dir, sizeof callee->cur_dir - 1);
    strncpy(callee->cur_file, caller->cur_file, sizeof callee->cur_file - 1);
    callee->has_this = 1;
    callee->this_obj_id = obj_id;
    { int ai2; callee->n_raw_args = nargs < PHP_ARG_MAX ? nargs : PHP_ARG_MAX;
      for (ai2 = 0; ai2 < callee->n_raw_args; ai2++) { strncpy(callee->raw_args[ai2], args[ai2], PHP_VAL_MAX - 1); callee->raw_args[ai2][PHP_VAL_MAX - 1] = 0; } }
    int i;
    for (i = 0; i < m->nparams; i++) {
        php_var_set(callee, m->params[i], i < nargs ? args[i] : "");
    }
    callee->src = m->body;
    php_run_statements(callee);
    caller->out_len = callee->out_len;
    strncpy(out, callee->returning ? callee->retval : "", outcap - 1); out[outcap - 1] = 0;
    php_state_free_local_arrays(callee);
    free(callee);
    g_call_depth--;
}

/* Calls a method reached via "::" syntax ("ClassName::method()",
 * "self::method()", "parent::method()", "static::method()") -- see the
 * "::" handling in php_eval_factor for how the target PhpMethod/class
 * name are resolved (including the self/parent/static special names).
 * Identical in shape to php_call_method(), except $this is forwarded
 * from the CALLER's own current object (if any) rather than fixed by an
 * explicit obj_id argument -- matching real PHP's behavior for the
 * common "parent::__construct()"/"self::helper()" pattern used from
 * inside an instance method (the callee still sees the same $this the
 * caller had), while a genuinely static call made with no enclosing
 * object (caller->has_this == 0) correctly leaves the callee with no
 * $this either. */
static void php_call_static(PhpState *caller, PhpMethod *m, char **args, int nargs, char *out, int outcap) {
    /* See php_call_function's own comment on g_call_depth. */
    if (g_call_depth >= PHP_CALL_DEPTH_MAX) {
        if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[CALL DEPTH LIMIT] ::%s() at depth %d\n", m->name, g_call_depth);
        out[0] = 0; return;
    }
    g_call_depth++;
    PhpState *callee = (PhpState *)malloc(sizeof *callee);
    memset(callee, 0, sizeof *callee);
    callee->get = caller->get;
    callee->post = caller->post;
    strncpy(callee->server_method, caller->server_method, sizeof callee->server_method - 1);
    callee->out = caller->out;
    callee->out_len = caller->out_len;
    callee->out_cap = caller->out_cap;
    strncpy(callee->cur_dir, caller->cur_dir, sizeof callee->cur_dir - 1);
    strncpy(callee->cur_file, caller->cur_file, sizeof callee->cur_file - 1);
    if (caller->has_this) {
        callee->has_this = 1;
        callee->this_obj_id = caller->this_obj_id;
    }
    int i;
    for (i = 0; i < m->nparams; i++) {
        php_var_set(callee, m->params[i], i < nargs ? args[i] : "");
    }
    callee->src = m->body;
    php_run_statements(callee);
    caller->out_len = callee->out_len;
    strncpy(out, callee->returning ? callee->retval : "", outcap - 1); out[outcap - 1] = 0;
    php_state_free_local_arrays(callee);
    free(callee);
    g_call_depth--;
}

/* Runs one source buffer's worth of literal-HTML-plus-"<?php ... ?>"
 * content, appending to st->out and updating st->src as it goes. Shared
 * by the top-level php_run() entry point and by require_once/require/
 * include/include_once (each included file is scanned exactly the same
 * way its top-level parent is). Stops early, without consuming the rest
 * of `source`, if a top-level "return" fires (st->returning) -- the
 * caller (php_run_statement's require/include handling, or php_run()
 * itself) is responsible for resetting st->returning appropriately.
 *
 * Just one entry into HTML passthrough (php_enter_html_passthrough,
 * handling any LEADING HTML before the first "<?php"/"<?", or the whole
 * file if it has no PHP tag at all) followed by one call to
 * php_run_statements() -- which now handles every SUBSEQUENT "?>...
 * <?php..." cycle internally (see its own comment), so this function no
 * longer needs its own loop for that. */
void php_run_source(PhpState *st, const char *source) {
    st->src = source;
    php_enter_html_passthrough(st);
    if (!*st->src) return; /* no "<?php" anywhere -- a pure-HTML file/fragment, already fully emitted */
    php_run_statements(st);
}

/* Runs a whole .php source file: literal HTML outside "<?php"/"<?" ...
 * "?>" is copied straight to the output buffer; everything inside those
 * tags is interpreted per the grammar above. `file_path`, if non-NULL,
 * seeds __DIR__/__FILE__ for top-level code in `source` (pass NULL, as
 * SQS's own dynamic test pages under SQW/testpages/ do, when there's no
 * real on-disk path backing `source` -- __DIR__/__FILE__ then just read
 * empty). */
static void php_run(const char *source, const char *file_path, PhpKVArray *get, PhpKVArray *post, const char *method,
                     char *out, int outcap) {
    php_globals_reset();
    /* Heap-allocated -- see php_call_function's own comment on PhpState's
     * real size now that arrays exist; not strictly needed here (this
     * is the one top-level, non-recursive PhpState per request, not one
     * more frame in a deep call chain), but SQS runs each connection on
     * its own pthread, and this keeps the "how big a stack frame does
     * running PHP need" answer the same (small) regardless of whichever
     * PhpState it is. */
    PhpState *st = (PhpState *)malloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->get = *get;
    st->post = *post;
    strncpy(st->server_method, method, sizeof st->server_method - 1);
    st->out = out;
    st->out_len = 0;
    st->out_cap = outcap;
    out[0] = 0;
    if (file_path) {
        strncpy(st->cur_file, file_path, sizeof st->cur_file - 1);
        const char *slash = strrchr(file_path, '/');
        if (slash) {
            int dl = (int)(slash - file_path);
            if (dl >= (int)sizeof st->cur_dir) dl = (int)sizeof st->cur_dir - 1;
            memcpy(st->cur_dir, file_path, (size_t)dl);
            st->cur_dir[dl] = 0;
        }
    }
    php_run_source(st, source);
    php_state_free_local_arrays(st);
    php_kv_free(&st->get);
    php_kv_free(&st->post);
    free(st);
}
