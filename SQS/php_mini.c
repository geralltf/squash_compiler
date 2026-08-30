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
#include <math.h>

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
/* g_globals[]'s own capacity -- see that array's own comment for the
 * real bug this constant, split out from PHP_MAX_VARS above, fixes.
 * PHP_MAX_VARS (a per-FUNCTION-CALL local variable table, embedded
 * directly in PhpState -- see PhpState.vars' own comment) stays small
 * on purpose: PhpState is allocated per call (deep call chains can
 * nest 100+ frames, see PHP_CALL_DEPTH_MAX), so a bigger PHP_MAX_VARS
 * would multiply every one of those frames' size and risk a real
 * native C-stack overflow under real recursion depth, entirely
 * separate from this file's own intentional "degrade past
 * PHP_CALL_DEPTH_MAX" safety net. g_globals[] has no such problem --
 * it's a single, one-time, request-lifetime table, not allocated per
 * call -- so it can and must be sized for how many DISTINCT names a
 * real WordPress boot actually globalizes across ALL of its files
 * combined (confirmed this session, via SQS_TRACE_EMIT/echo
 * breadcrumbs narrowing an "index.php renders nothing" symptom all
 * the way down to this: `wp_set_template_globals()`'s own `global
 * $wp_stylesheet_path, $wp_template_path;` assignment silently losing
 * its value -- traced to `php_global_find_or_create()` returning NULL
 * once `g_nglobals >= PHP_MAX_VARS`, which `php_var_set()` then
 * silently no-ops on ("if (!v) return;"), with every READ of that same
 * name then ALSO returning empty via `php_global_find()` finding
 * nothing -- exactly matching the observed "assign a known-correct
 * value, read back empty, no error" symptom. A real, full WordPress
 * boot easily globalizes 100+ distinct names across core alone
 * ($wpdb, $wp_filter, $wp_actions, $wp_current_filter, $current_user,
 * $wp_theme_directories, $wp_stylesheet_path, $wp_template_path,
 * $wp_query, $wp_rewrite, $wp_registered_widgets, $wp_taxonomies,
 * $wp_post_types, $wp_locale, $table_prefix, $wp_object_cache, ... --
 * the OLD 64-name cap (== PHP_MAX_VARS, its only reason for being that
 * value was reusing the same constant, per that array's own original
 * comment's now-confirmed-wrong assumption that "only a small,
 * genuinely-shared set of names ... ever gets globalized in
 * practice") was exhausted partway through a normal boot, silently
 * breaking every DISTINCT global name declared after the 64th, with
 * no error/trace at all -- almost certainly THE direct, single
 * highest-leverage cause of the reported "index.php renders nothing"
 * symptom, since template-path resolution (wp_set_template_globals)
 * happens to be roughly where a real boot's own name count crosses
 * that old threshold. */
#define PHP_MAX_GLOBALS 1024
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
#define PHP_LIST_MAX 16 /* max targets in a "list($a, $b, ...) = EXPR;" destructuring assignment -- see that handler's own comment */
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
    /* Real parameter DEFAULT VALUES -- see PhpFunc.param_defaults' own
       comment for the real, high-impact bug this fixes (session 1 through
       THIS session: default values were parsed but silently discarded,
       so any call omitting a trailing optional argument bound it to ""
       instead of the function's own declared default -- e.g. real
       WordPress's own add_filter($hook, $cb) [no explicit $priority/
       $accepted_args] silently got $accepted_args="" instead of 1,
       corrupting WP_Hook::apply_filters()'s own accepted_args>=num_args
       branch selection and array_slice() call -- confirmed as the direct
       cause of every filter callback receiving NO arguments at all).
       "" (not a special sentinel) for a parameter with no explicit "="
       in its declaration -- already this engine's own existing "missing
       arg -> empty string" behavior, so a parameter that never had a
       default keeps behaving exactly as before. */
    char param_defaults[PHP_FUNC_PARAM_MAX][PHP_VAL_MAX];
    int nparams;
    /* "...$name" variadic marker -- see PhpFunc.variadic's own comment
     * for the general mechanism (plain functions had this already; a
     * CLASS METHOD's own parameter list parsing and call-binding never
     * got the same treatment, so a method/constructor declared with a
     * variadic last parameter -- e.g. real WordPress's own
     * _WP_Dependency::__construct(...$args), backing every registered
     * script/style handle -- silently bound that parameter to nothing
     * at all (0 real args collected regardless of how many were passed)
     * instead of a real array of the trailing positional arguments,
     * confirmed as a real, high-impact bug this session: with ->handle/
     * ->src/etc left permanently empty, no registered style or script
     * ever printed a real tag on any page). */
    int variadic[PHP_FUNC_PARAM_MAX];
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
    /* "static $name [= init];" class properties -- unlike prop_names/
       prop_defaults above (per-INSTANCE, reseeded fresh into every
       php_object_new()'d object's own props table), these are CLASS-level
       shared storage: one slot per class, mutated in place by
       "self::$name = ...;"/"ClassName::$name = ...;" and read back the
       same way, persisting across every "new ClassName()" and every call.
       Previously entirely unimplemented -- "static" was consumed as an
       inert modifier keyword (see php_parse_class_decl's own modifier-
       loop comment) and "self::$name"/"ClassName::$name" wasn't
       recognized as a distinct shape at all, so e.g. real WordPress's
       WP_User::__construct() -- "if (!isset(self::$back_compat_keys)) {
       self::$back_compat_keys = array(...); }" -- silently degraded to:
       isset() on a "self::"-prefixed argument always resolving false
       (php_resolve_varref only understands a leading '$', so it returned
       an empty PhpVarRef for input starting with the identifier "self"),
       and the assignment desyncing into two garbage statements (an empty
       "self::" evaluated as a no-op constant read, immediately followed
       by a bare "$back_compat_keys = array(...);" landing in the
       CALLER's own local scope instead of anywhere shared) -- so the
       "only initialize once" cache never actually cached anything,
       silently re-running class-level init work on every single object
       construction. Confirmed via an isolated repro this session
       (t_static_prop.php: expected "INIT CACHED CACHED", got
       "INIT INIT INIT" before this fix). */
    char static_prop_names[PHP_CLASS_PROP_MAX][64];
    char static_prop_vals[PHP_CLASS_PROP_MAX][PHP_VAL_MAX];
    int nstatic_props;
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
    /* Real parameter DEFAULT VALUES, evaluated ONCE at function-
       declaration parse time and stored here -- found MISSING entirely
       this session: default values were being parsed (php_eval_expr,
       correctly) then thrown away, so calling a function while omitting
       a trailing optional argument always bound it to "" instead of the
       real declared default. A real, long-standing, high-impact gap
       (documented as unfixed since session 1) -- finally root-caused
       THIS session as the direct cause of WordPress's own hook/filter
       system silently dropping every callback's own filtered VALUE
       argument: add_filter($hook, $callback) [omitting $priority/
       $accepted_args] left $accepted_args as "" instead of add_filter()'s
       own declared default of 1, which corrupted WP_Hook::apply_filters()'s
       "$the_['accepted_args'] >= $num_args" branch selection enough that
       it ended up calling every callback with ZERO of its real arguments
       via array_slice($args, 0, "") -- effectively atoi("")=0. "" (same
       as this engine's existing "missing arg" behavior) for a parameter
       declared with no explicit "=" default at all -- unaffected. Only
       SIMPLE scalar defaults are supported (a literal number/string/
       true/false/null, or a simple expression using only those,
       evaluated once at parse time) -- a default that depends on another
       parameter's own value ("function f($a, $b = $a)", real PHP doesn't
       allow that either) or needs a fresh array/object per call ("$b =
       array()") is a documented, narrower gap than the SAME class of bug
       php_object_new()'s own array_deep_copy fix addresses for class
       property defaults -- not yet extended here, since no real
       WordPress call site reached during this session's own testing
       needed an array-valued default parameter to confirm one way or
       the other. */
    char param_defaults[PHP_FUNC_PARAM_MAX][PHP_VAL_MAX];
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
 * $wpdb. PHP_MAX_GLOBALS-sized -- see that constant's own comment for
 * why this is a MUCH bigger, and separate, capacity than the per-call
 * PHP_MAX_VARS local-variable table. */
static PhpVar g_globals[PHP_MAX_GLOBALS];
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

/* Set by a fired "die"/"exit" (real PHP: language constructs, not
 * functions -- unlike "return", they must halt the WHOLE script, not
 * just the current function's own scope). A per-PhpState flag like
 * `returning` can't do this alone: php_call_function()/php_call_method()/
 * php_call_static() deliberately do NOT propagate a callee's own
 * `returning` up to the caller (a nested function's "return" must not
 * make the top-level script return too) -- but "exit" needs exactly the
 * opposite, so it's tracked here as global, process-wide state instead,
 * checked at every "should I run the next statement/keep looping"
 * decision point alongside the existing `st->returning` checks (see
 * every "if (st->returning...) return;"-shaped site in this file), all
 * the way up through every nested loop/if/switch/function-call boundary
 * to php_run()'s own top-level driver. */
static int g_exiting = 0;

/* Real short-circuit evaluation for "&&"/"||" -- see php_eval_and's own
 * comment (right above its definition) for the full history: this was a
 * KNOWN, deliberately-accepted limitation from early in this project
 * (both sides of "&&"/"||" were always evaluated, since implementing a
 * real "parse but don't execute" mode seemed like more machinery than
 * this subset's scope justified). Confirmed this session as a real,
 * SEVERE bug, not just a theoretical gap: real WordPress's own
 * map_meta_cap()/is_super_admin() rely on "is_multisite() && ...
 * is_super_admin(...) ..."-shaped guards to skip is_super_admin() ENTIRELY
 * on a single-site install -- without real short-circuiting,
 * is_super_admin() ran unconditionally every time, and its own body
 * (which itself calls back into capability-checking machinery) recursed
 * deeply enough to blow the native C stack and SEGFAULT the whole
 * server on a real login attempt. A counter (not a bool) so nested
 * short-circuited expressions ("$a && ($b || expensive())") compose
 * correctly -- ANY enclosing suppression, however many levels, still
 * means "don't actually run this call". Checked at the very top of
 * php_call_function()/php_call_method()/php_call_static(), AFTER the
 * caller has already evaluated/parsed the argument list (so source
 * position stays correct either way) but BEFORE running the callee's own
 * body or dispatching to a builtin -- skips exactly the "actually do the
 * work, including any further recursive calls" part, which is what
 * matters for both correctness (no unwanted side effects from a
 * short-circuited branch) and this crash specifically (no unwanted
 * recursion). A narrower fix than a full "skip whole sub-expressions
 * including their own nested calls without evaluating anything at all"
 * -- a call inside a short-circuited call's own ARGUMENT list (e.g.
 * "false && foo(bar())") still evaluates bar() for real, since argument
 * evaluation happens in the caller before php_call_function() is ever
 * reached -- but covers the overwhelmingly common real-world shape
 * (a cheap guard on the left, a plain function/method call with simple
 * arguments on the right), including the exact one that was crashing
 * the server. */
static int g_suppress_calls = 0;

/* Real PHP output buffering (ob_start()/ob_get_clean()/etc) -- previously
 * entirely UNIMPLEMENTED. A real, high-impact gap: real WordPress core
 * uses this constantly to CAPTURE a template/widget/shortcode's own
 * `echo`'d output as a plain string instead of sending it straight to
 * the page (get_search_form(), dynamic_sidebar()-adjacent widget
 * rendering, many shortcode handlers, wp_print_inline_script_tag()'s own
 * callers, ...) -- without it, any code shaped like "ob_start(); ...
 * echo-heavy template...; $html = ob_get_clean();" silently captured
 * NOTHING (ob_get_clean() itself didn't exist, degrading to ""), so
 * every such captured fragment was simply missing from the final page.
 * A small GLOBAL stack (not per-PhpState) since PhpState.out/out_len/
 * out_cap are themselves a SHARED pointer propagated across an entire
 * call tree (see "callee->out = caller->out;" throughout this file) --
 * ob_start() redirects THAT shared pointer to a fresh buffer (saving the
 * previous one here), so every echo/print anywhere in the call tree
 * until the matching ob_*_clean()/ob_end_flush() lands in the new
 * buffer, exactly matching real PHP's own request-global (not call-
 * frame-local) output-buffer-stack semantics. */
#define PHP_OB_STACK_MAX 8
static char *g_ob_saved_out[PHP_OB_STACK_MAX];
static int g_ob_saved_len[PHP_OB_STACK_MAX];
static int g_ob_saved_cap[PHP_OB_STACK_MAX];
static int g_ob_depth = 0;

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
    g_exiting = 0; /* a "die"/"exit" from the PREVIOUS request must not
                      also halt the next one on this same long-running
                      server -- see g_exiting's own comment */
    /* An unbalanced ob_start() (no matching ob_*_clean()/ob_end_flush())
     * left over from the previous request must not leave the NEXT
     * request's own g_ob_depth stack looking non-empty. Frees every
     * INTERMEDIATE buffer this file itself malloc'd during those pushes
     * (index 0 is always the ORIGINAL, request-owned buffer -- never
     * ours to free); the one buffer that was still ACTIVE (top of stack)
     * when the leak happened is unreachable by this point (the PhpState
     * that referenced it was already freed when the previous php_run()
     * returned) and is a real, small, bounded, documented leak for this
     * one misuse pattern -- not a correctness bug for the well-behaved
     * "every ob_start() has a matching ob_*_clean()" code real WordPress
     * core actually uses. */
    for (i = 1; i < g_ob_depth; i++) { if (g_ob_saved_out[i]) free(g_ob_saved_out[i]); }
    g_ob_depth = 0;
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

/* Same parent-chain walk as php_class_find_const, but returns a pointer
 * directly into the class's own static_prop_vals[] slot -- callers both
 * READ through it (a plain char* deref) and WRITE through it (mutating
 * the shared, class-level storage in place), matching real PHP's
 * "static $x;" semantics: the slot lives once per declaring class and is
 * shared across every instance/call, not reseeded per-object the way
 * prop_defaults is. Returns NULL if `name` isn't a declared static
 * property anywhere up the chain (not: don't silently fall back to an
 * instance property of the same name -- self::$x and $this->x are
 * distinct storage in real PHP, and conflating them would reintroduce
 * exactly the kind of silent-wrong-value bug this feature was added to
 * fix). */
static char *php_class_find_static_prop(PhpClass *cls, const char *name) {
    int i, guard = 0;
    while (cls && guard++ < 32) {
        for (i = 0; i < cls->nstatic_props; i++) {
            if (strcmp(cls->static_prop_names[i], name) == 0) return cls->static_prop_vals[i];
        }
        cls = cls->parent_name[0] ? php_class_find(cls->parent_name) : NULL;
    }
    return NULL;
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
     * $_COOKIE from the request's own "Cookie:" header, $_SERVER
     * ['REQUEST_METHOD'] from the real HTTP method. $_COOKIE was
     * previously entirely UNIMPLEMENTED (not even a recognized name in
     * php_eval_factor's/php_resolve_varref's own "_GET"/"_POST" special-
     * case dispatch) -- a real, high-impact gap found while chasing why
     * wp-login.php's own submit button appeared to do nothing: real WP's
     * own "cookies blocked" guard ("elseif ( isset( $_POST['testcookie']
     * ) && empty( $_COOKIE[ TEST_COOKIE ] ) )") ALWAYS fired, no matter
     * what cookie a real browser (or curl) actually sent, because
     * "$_COOKIE[...]" always read as an undefined/empty variable --
     * silently overwriting a genuinely SUCCESSFUL wp_signon() with a
     * bogus WP_Error immediately afterward, every single time. */
    PhpKVArray get;
    PhpKVArray post;
    PhpKVArray cookie;
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

    /* Pending HTTP response headers -- header()/setcookie() append raw
     * "Name: value\n"-lines here instead of ever reaching a real socket
     * directly (this file has no I/O of its own; the caller, php_run(),
     * hands the accumulated text back to sqs_main.c, which is the one
     * place that actually owns the connection). `headers_buf` is a
     * SHARED pointer (set once in php_run(), copied to every callee
     * exactly like `out` is -- see php_call_function's own "callee->out
     * = caller->out;") so a header() call deep inside some function
     * (e.g. wp_redirect()'s own "header('Location: ' . $location, ...)")
     * still lands in the one real request-lifetime buffer; `headers_len`
     * is a plain int, synced back up the call chain the same manual way
     * `out_len` already is at every call site. Previously entirely
     * UNIMPLEMENTED (header()/setcookie() both silently no-op'd) -- a
     * real, high-impact gap: wp_signon()'s own successful-login path
     * calls wp_set_auth_cookie() (setcookie()) then wp_safe_redirect()
     * (header('Location: ...')) -- with neither having any real effect,
     * a real, successful login produced a blank page with no visible
     * feedback and no way to reach any authenticated page, which is
     * indistinguishable from "the submit button does nothing" even
     * though authentication itself was by then already working
     * correctly underneath. */
    char *headers_buf;
    int headers_len;
    int headers_cap;
} PhpState;

/* Real PHP: "$str[N]" on a plain (non-array, non-object) string is
 * BYTE-indexing into the string itself (a single-character substring,
 * empty if out of range; a negative N counts from the end) -- previously
 * entirely UNIMPLEMENTED at every "$var[key]"-shaped read site (each one
 * only ever checked "is this an array reference" / "is this an object
 * reference", falling straight to "" for a plain string with no third
 * case at all). A real, high-impact, previously-undiscovered gap: real
 * WordPress core's own is_serialized() (wp-includes/functions.php,
 * gating EVERY maybe_unserialize() call -- i.e. every options/postmeta/
 * usermeta read of a stored array/object value) does "if (':' !==
 * $data[1]) return false;" as its very first real check -- with
 * "$data[1]" always empty, is_serialized() always returned false, so
 * maybe_unserialize() always returned the raw serialized STRING
 * unchanged instead of the real decoded array -- confirmed as the
 * direct, final cause of WP_User_Meta_Session_Tokens's own session data
 * always looking unset even once every other layer (real SQL storage,
 * ARRAY_A row conversion, array_map()/array_filter(), the "%" operator
 * it also depends on transitively) was already fixed and confirmed
 * correct in isolation. */
static void php_string_char_at(const char *s, const char *key, char *out, int outcap) {
    char *endp;
    long idx = strtol(key, &endp, 10);
    if (key[0] == 0 || *endp != 0) { out[0] = 0; return; }
    int len = (int)strlen(s);
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len || outcap < 2) { out[0] = 0; return; }
    out[0] = s[idx];
    out[1] = 0;
}
/* Real PHP date()/gmdate() format-character support -- previously
 * entirely UNIMPLEMENTED (date()/gmdate()/mktime()/strtotime() all fell
 * through to this file's own generic "unknown function -> ''" degrade),
 * a real, high-impact gap found in this session's own audit: real
 * WordPress core displays a post/comment's own date on essentially every
 * single-post/archive/comment-listing page via mysql2date()/
 * get_the_date()/... which all eventually reach a real date()-shaped
 * call. This subset always operates in UTC (via gmtime(), never the
 * process's own local timezone, and never a real IANA timezone
 * database) -- a real, documented simplification matching this file's
 * "no per-site timezone configuration modeled" scope; covers the
 * overwhelmingly common real format characters, not the full real PHP
 * set (no ISO week-year "o", no timezone-abbreviation "e"/"T" beyond a
 * hardcoded "UTC", no Swatch Internet time "B"). */
static void php_format_date(const char *fmt, time_t t, char *out, int outcap) {
    struct tm tmv;
    gmtime_r(&t, &tmv);
    static const char *mon_full[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
    static const char *mon_abbr[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    static const char *day_full[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    static const char *day_abbr[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    int o = 0;
    const char *p;
    char buf[32];
    for (p = fmt; *p && o < outcap - 1; p++) {
        int n = -1;
        buf[0] = 0;
        switch (*p) {
            case '\\': if (p[1] && o < outcap - 1) { out[o++] = p[1]; p++; } continue;
            case 'Y': snprintf(buf, sizeof buf, "%d", tmv.tm_year + 1900); break;
            case 'y': snprintf(buf, sizeof buf, "%02d", (tmv.tm_year + 1900) % 100); break;
            case 'm': snprintf(buf, sizeof buf, "%02d", tmv.tm_mon + 1); break;
            case 'n': snprintf(buf, sizeof buf, "%d", tmv.tm_mon + 1); break;
            case 'd': snprintf(buf, sizeof buf, "%02d", tmv.tm_mday); break;
            case 'j': snprintf(buf, sizeof buf, "%d", tmv.tm_mday); break;
            case 'H': snprintf(buf, sizeof buf, "%02d", tmv.tm_hour); break;
            case 'G': snprintf(buf, sizeof buf, "%d", tmv.tm_hour); break;
            case 'h': snprintf(buf, sizeof buf, "%02d", tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12); break;
            case 'g': snprintf(buf, sizeof buf, "%d", tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12); break;
            case 'i': snprintf(buf, sizeof buf, "%02d", tmv.tm_min); break;
            case 's': snprintf(buf, sizeof buf, "%02d", tmv.tm_sec); break;
            case 'D': strncpy(buf, day_abbr[tmv.tm_wday], sizeof buf - 1); break;
            case 'l': strncpy(buf, day_full[tmv.tm_wday], sizeof buf - 1); break;
            case 'M': strncpy(buf, mon_abbr[tmv.tm_mon], sizeof buf - 1); break;
            case 'F': strncpy(buf, mon_full[tmv.tm_mon], sizeof buf - 1); break;
            case 'A': strncpy(buf, tmv.tm_hour < 12 ? "AM" : "PM", sizeof buf - 1); break;
            case 'a': strncpy(buf, tmv.tm_hour < 12 ? "am" : "pm", sizeof buf - 1); break;
            case 'N': snprintf(buf, sizeof buf, "%d", tmv.tm_wday == 0 ? 7 : tmv.tm_wday); break;
            case 'w': snprintf(buf, sizeof buf, "%d", tmv.tm_wday); break;
            case 'z': snprintf(buf, sizeof buf, "%d", tmv.tm_yday); break;
            case 'U': snprintf(buf, sizeof buf, "%ld", (long)t); break;
            case 't': { static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31}; int y = tmv.tm_year + 1900; int d = dim[tmv.tm_mon]; if (tmv.tm_mon == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) d = 29; snprintf(buf, sizeof buf, "%d", d); break; }
            case 'L': { int y = tmv.tm_year + 1900; snprintf(buf, sizeof buf, "%d", (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0); break; }
            case 'S': { int d = tmv.tm_mday; const char *suf = "th"; if (d % 10 == 1 && d != 11) suf = "st"; else if (d % 10 == 2 && d != 12) suf = "nd"; else if (d % 10 == 3 && d != 13) suf = "rd"; strncpy(buf, suf, sizeof buf - 1); break; }
            case 'e': case 'T': strncpy(buf, "UTC", sizeof buf - 1); break;
            case 'P': strncpy(buf, "+00:00", sizeof buf - 1); break;
            case 'O': strncpy(buf, "+0000", sizeof buf - 1); break;
            case 'Z': strncpy(buf, "0", sizeof buf - 1); break;
            case 'u': strncpy(buf, "000000", sizeof buf - 1); break;
            case 'v': strncpy(buf, "000", sizeof buf - 1); break;
            case 'c': php_format_date("Y-m-d\\TH:i:sP", t, buf, sizeof buf); break;
            case 'r': php_format_date("D, d M Y H:i:s O", t, buf, sizeof buf); break;
            default: out[o++] = *p; continue;
        }
        (void)n;
        int bl = (int)strlen(buf);
        if (bl > outcap - 1 - o) bl = outcap - 1 - o;
        memcpy(out + o, buf, (size_t)bl); o += bl;
    }
    out[o] = 0;
}
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
/* Removes `key` from `arr` if present (swap-with-last, since element
   ORDER within a PHP array normally matters for foreach/etc, but real
   unset() doesn't promise to preserve insertion order of the REMAINING
   elements either -- matches this file's own existing "correctness over
   micro-fidelity" convention elsewhere). A no-op if `key` isn't present,
   never an error -- unset() on an already-absent key is valid, common
   real PHP. */
static void php_kv_remove(PhpKVArray *arr, const char *key) {
    int i;
    for (i = 0; i < arr->count; i++) {
        if (strcmp(arr->items[i].key, key) == 0) {
            arr->items[i] = arr->items[arr->count - 1];
            arr->count--;
            return;
        }
    }
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

/* Deep-copies the array at g_arrays[src_id] into a FRESH slot (any
 * nested array VALUES are recursively deep-copied too, so the whole
 * structure becomes fully independent; a nested OBJECT reference is left
 * as-is and shared, matching real PHP's own "arrays are value types,
 * objects are reference types" distinction). Returns the new array's id,
 * or `src_id` itself (i.e. a no-op, NOT a crash) if a fresh slot
 * couldn't be allocated (PHP_ARR_MAX exhausted) -- same "degrade to the
 * old, at least previously-working behavior rather than losing data"
 * convention as every other allocation-failure path in this file.
 *
 * Why this exists: found via a real, high-impact bug this session --
 * `php_object_new()` used to copy a class's own property DEFAULT value
 * (e.g. "public $callbacks = array();") into every new instance VERBATIM,
 * including for an array-valued default. Since php_eval_expr() evaluates
 * "array()" to an arrref token ONCE, at CLASS-DECLARATION parse time
 * (see php_parse_class_decl()'s own "$"-branch), that single arrref
 * -- and therefore the ONE underlying g_arrays[] slot it points to --
 * was being shared across literally EVERY instance of that class ever
 * constructed. Real PHP always gives each new object its own independent
 * copy of an array-valued default. This one bug fully explained
 * WordPress's entire hook/filter system misbehaving: every `WP_Hook`
 * object shares ONE `$this->callbacks` array class-wide, so
 * `add_filter('pre_kses', ...)` and `add_filter('sanitize_text_field',
 * ...)` (two DIFFERENT WP_Hook instances in real PHP) were actually
 * mutating the exact same shared array -- confirmed via a minimal
 * standalone repro (three objects of one class, each given a different
 * nested-array property value, all three ending up with the SAME merged
 * contents) before this fix, and each independently correct after it. */
#define PHP_ARRAY_COPY_DEPTH_MAX 64
static int php_array_deep_copy_depth(int src_id, int depth) {
    if (src_id < 0 || src_id >= PHP_ARR_MAX || !g_arr_alive[src_id]) return src_id;
    /* Same "generous but bounded, degrade rather than blow the native
       stack" philosophy as every other depth cap in this file -- real
       PHP arrays can't literally contain themselves via ordinary value
       copy, but nothing stops a genuinely deep nested structure (e.g. a
       big decoded options blob) from existing; past this depth, deeper
       levels are simply left SHARED (a rare, narrow divergence from real
       PHP's full-depth copy-on-write, not a crash). */
    if (depth >= PHP_ARRAY_COPY_DEPTH_MAX) return src_id;
    int dst_id = php_array_new();
    if (dst_id < 0) return src_id; /* table exhausted -- fall back to sharing rather than losing the value entirely */
    PhpKVArray *src = &g_arrays[src_id];
    PhpKVArray *dst = &g_arrays[dst_id];
    int i;
    for (i = 0; i < src->count; i++) {
        int nested_id = php_arrref_decode(src->items[i].val);
        if (nested_id >= 0) {
            int copied_id = php_array_deep_copy_depth(nested_id, depth + 1);
            char copied_ref[32];
            php_arrref_encode(copied_id, copied_ref, sizeof copied_ref);
            php_kv_add(dst, src->items[i].key, copied_ref);
        } else {
            php_kv_add(dst, src->items[i].key, src->items[i].val);
        }
    }
    dst->cursor = src->cursor;
    return dst_id;
}
static int php_array_deep_copy(int src_id) { return php_array_deep_copy_depth(src_id, 0); }

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
            for (i = 0; i < cls->nprops; i++) {
                /* An array-valued default ("public $callbacks = array();")
                   must be its own independent array PER INSTANCE, not the
                   one shared arrref the class declaration itself evaluated
                   to ONCE at parse time -- see php_array_deep_copy()'s own
                   comment for the real, high-impact bug this fixes (every
                   object of a class silently sharing ONE mutable array).
                   A non-array default (plain scalar, or an object
                   reference -- real PHP objects ARE reference types, never
                   copied here either) is set as-is, unchanged. */
                int arr_id = php_arrref_decode(cls->prop_defaults[i]);
                if (arr_id >= 0) {
                    int copy_id = php_array_deep_copy(arr_id);
                    char copy_ref[32];
                    php_arrref_encode(copy_id, copy_ref, sizeof copy_ref);
                    php_kv_set(&o->props, cls->prop_names[i], copy_ref);
                } else {
                    php_kv_set(&o->props, cls->prop_names[i], cls->prop_defaults[i]);
                }
            }
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
    if (g_nglobals >= PHP_MAX_GLOBALS) return NULL;
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
    if (len > 0) {
        if (getenv("SQS_TRACE_EMIT")) { char snip[80]; int sn = len < 79 ? len : 79; memcpy(snip, s, (size_t)sn); snip[sn] = 0; fprintf(stderr, "[EMIT +%d @%d] %s\n", len, st->out_len, snip); }
        memcpy(st->out + st->out_len, s, (size_t)len); st->out_len += len; st->out[st->out_len] = 0;
    }
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
        if (q == '\'' && c == '\\' && (p[1] == '\'' || p[1] == '\\')) {
            /* Real PHP single-quoted strings support exactly TWO escapes
             * -- "\\'" (a literal quote) and "\\\\" (a literal backslash)
             * -- everything else stays literal (no "\\n"/"\\t"/etc
             * interpretation, real PHP's own rule). Previously MISSING
             * entirely: the loop's own "while (c && c != q)" terminator
             * check happened before any escape handling ran for a
             * single-quoted string, so "\\'" inside one was read as "a
             * literal backslash, then the STRING ENDS HERE" -- silently
             * truncating the string at the first escaped quote and
             * leaving everything after it to be misparsed as PHP code.
             * Confirmed as a real, high-impact bug this session: any
             * string literal containing an escaped apostrophe -- an
             * extremely common shape, e.g. real WordPress's own esc_url()
             * regex "'|[...\\'()...]|i'" -- silently corrupted the rest
             * of the file's own parsing from that point on. */
            p++;
            c = *p;
            if (i < bufcap - 1) buf[i++] = c;
            p++;
            c = *p;
            continue;
        }
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
                    else if (php_objref_decode(cur) >= 0) cur[0] = 0;
                    else { char sc[2]; php_string_char_at(cur, key, sc, sizeof sc); strncpy(cur, sc, sizeof cur - 1); cur[sizeof cur - 1] = 0; }
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
static void php_eval_factor(PhpState *st, char *out, int outcap); /* forward: dynamic "->$field" member-name evaluation needs this before its own definition */
static void php_invoke_callable(PhpState *caller, const char *callable, char **args, int nargs, char *out, int outcap); /* forward: usort()/uasort()/uksort()'s own custom-comparator dispatch needs this before its own definition */
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
    /* "BASE->member[key]" -- e.g. "$this->registered[$handle]",
     * WP_Dependencies::add()'s own "if (isset($this->registered[$handle]))
     * { return false; }" guard. A real, common, high-impact shape found
     * missing this session: without it, isset()/empty()/count() on this
     * pattern fell back to checking has_member alone -- i.e. whether
     * $this->registered ITSELF is set, which (being initialized to
     * "array()") is ALWAYS true, so isset() on the SPECIFIC key always
     * returned a false positive regardless of whether that key was
     * actually registered. Confirmed as the direct cause of
     * wp_default_styles()'s own $styles->add('login', ...) always
     * silently no-opping (add() always saw isset()==true and returned
     * false before ever storing anything) -- WordPress's own real login
     * page never got its CSS/JS registered at all. Only THIS one
     * additional level (member, then a single trailing key) is resolved,
     * not a further chain -- good enough for the real call shapes this
     * was found against, same "narrow but real" scope every other
     * PhpVarRef extension in this struct already has. */
    int has_member_key;
    char member_key[128];
    /* "$_GET[key]"/"$_POST[key]" -- these superglobals are read directly
     * from PhpState.get/post (see php_eval_factor's own "_GET"/"_POST"
     * special case, needed so they're visible in every function's own
     * scope without a "global" declaration, exactly like real PHP), NOT
     * through the ordinary PhpVar table at all -- so `ref->var` is always
     * NULL for one of these and every check above that falls back to
     * "var is NULL, so treat as unset" gave the WRONG answer. Confirmed
     * as a real, high-impact bug: isset($_GET['step'])/count($_GET) were
     * ALWAYS false/0 respectively even though a direct "$_GET['step']"
     * read (going through the unrelated, working php_eval_factor path)
     * returned the real value -- wp-admin/install.php's own "$step =
     * isset($_GET['step']) ? (int)$_GET['step'] : 0;" (a real, common
     * WordPress pattern used constantly for request routing) always fell
     * back to 0, so the installer could never advance past step 0 no
     * matter what was actually submitted. */
    int is_superglobal;
    PhpKVArray *sg_arr;
    /* "$_REQUEST[key]" -- a MERGE of $_POST and $_GET (see php_eval_
     * factor's own "_REQUEST" comment), so unlike is_superglobal's
     * single `sg_arr` pointer this needs BOTH `post`/`get` checked (POST
     * wins on collision, matching real PHP precedence). A separate flag
     * rather than folding into is_superglobal since the two consumers
     * genuinely need different lookup logic. */
    int is_request;
    PhpState *st_for_request;
} PhpVarRef;

/* Resolves the real PhpKVArray* backing PhpVar `v`, whether it's a
 * NATIVE array (v->is_array, data embedded directly in v->arr) or a
 * PASSED/RETURNED array snapshot (v->is_array is 0, but v->val holds an
 * arrref-encoded token string -- see php_arrref_encode/decode's own
 * comments, and php_var_set()'s own comment on why a plain assignment
 * -- including binding a function CALL's argument to its callee's own
 * parameter, php_call_function/method/static's shared binding logic --
 * always clears is_array and just copies the value string verbatim).
 * Returns NULL if `v` is a genuine scalar with no array behind it at
 * all. count()/is_array() already had this exact fallback inline (see
 * their own call sites) so a snapshotted array's real element count
 * read correctly; isset()/empty() never did -- a real, high-impact gap:
 * EVERY array received as a function PARAMETER is unconditionally
 * is_array=0 with an arrref token in ->val (that's how ALL arguments,
 * not just arrays, get bound), so "isset($param['key'])"/
 * "empty($param['key'])" on any array-typed function parameter --an
 * extremely common real PHP/WordPress pattern, e.g.
 * WP_Query::fill_query_vars()'s own "foreach ($keys as $key) { if
 * (!isset($query_vars[$key])) { $query_vars[$key] = ''; } }" -- always
 * silently evaluated as "not set"/"empty" regardless of the parameter's
 * REAL content, clobbering every already-set key back to '' on the very
 * next line. Confirmed via a minimal repro this session
 * (isset($arr['s']) on a function parameter array returned false even
 * though count($arr) on the very same parameter correctly returned 1). */
static PhpKVArray *php_var_real_array(PhpVar *v) {
    if (!v) return NULL;
    if (v->is_array) return &v->arr;
    int aid = php_arrref_decode(v->val);
    return (aid >= 0) ? &g_arrays[aid] : NULL;
}

/* Same idea as php_var_real_array(), but for the several real by-
 * reference array builtins (sort()/rsort()/ksort()/.../array_push()/
 * array_shift()/...) that need to MUTATE the caller's real array
 * in place, not just read it -- auto-vivifies a real (empty) array
 * container if `v` isn't already array-shaped, matching real PHP's own
 * "sort($x)" on an unset $x silently making it an empty array. Shared
 * factoring of the exact by-ref-array-resolution logic array_unshift()
 * pioneered (see its own comment for the full "why a bare read snapshots
 * instead of sharing storage" story) -- every one of these sort/mutate
 * builtins needs the identical "resolve the raw variable, not a
 * stringified value" special-cased argument parsing isset()/empty()/
 * unset()/array_unshift() already have, hence living in that same
 * special-cased dispatch block in php_eval_factor rather than the
 * ordinary php_call_function() strcmp chain. */
/* Real PHP's own "loose" default ordering: two values that both LOOK
 * numeric compare numerically, otherwise plain byte-string comparison --
 * matches this file's own existing "<"/">"-shaped comparisons (see
 * php_eval_cmp's own php_to_num()-based numeric coercion), used here for
 * sort()/rsort()/asort()/arsort()/ksort()/krsort()'s own default
 * (SORT_REGULAR-shaped) comparator. */
static int php_default_cmp(const char *a, const char *b) {
    char *ea, *eb;
    double da = strtod(a, &ea), db = strtod(b, &eb);
    int a_num = (ea != a && *ea == 0 && a[0] != 0);
    int b_num = (eb != b && *eb == 0 && b[0] != 0);
    if (a_num && b_num) { if (da < db) return -1; if (da > db) return 1; return 0; }
    return strcmp(a, b);
}
/* A simple insertion sort (this file's own established "simple first,
 * real WordPress array sizes never need anything fancier" convention,
 * matching e.g. the flat-file DB engine's own scope-limit philosophy) --
 * O(n^2), fine for the small option/meta/query-result arrays real
 * WordPress core actually sorts. `cmp_by_key` sorts by each item's KEY
 * instead of its VALUE (ksort()/krsort()); `reverse` flips the ordering
 * (rsort()/arsort()/krsort()); `reindex` renumbers keys 0..n-1 after
 * sorting (sort()/rsort() -- real PHP semantics: these two specifically
 * DISCARD the original keys, unlike every other sort variant here). */
static void php_array_sort_by(PhpKVArray *a, int cmp_by_key, int reverse, int reindex) {
    int i, j;
    for (i = 1; i < a->count; i++) {
        PhpKV tmp = a->items[i];
        j = i - 1;
        while (j >= 0) {
            const char *ka = cmp_by_key ? a->items[j].key : a->items[j].val;
            const char *kb = cmp_by_key ? tmp.key : tmp.val;
            int c = php_default_cmp(ka, kb);
            if (reverse) c = -c;
            if (c <= 0) break;
            a->items[j + 1] = a->items[j];
            j--;
        }
        a->items[j + 1] = tmp;
    }
    if (reindex) {
        for (i = 0; i < a->count; i++) {
            char k[16]; snprintf(k, sizeof k, "%d", i);
            strncpy(a->items[i].key, k, sizeof a->items[i].key - 1); a->items[i].key[sizeof a->items[i].key - 1] = 0;
        }
    }
    a->cursor = 0;
}
static PhpKVArray *php_varref_mutable_array(PhpVarRef *ref) {
    if (!ref->var || ref->has_key || ref->has_member) return NULL;
    if (ref->var->is_array) return &ref->var->arr;
    int aid = php_arrref_decode(ref->var->val);
    if (aid < 0) {
        aid = php_array_new();
        if (aid < 0) return NULL;
        php_arrref_encode(aid, ref->var->val, sizeof ref->var->val);
    }
    return &g_arrays[aid];
}

/* Same idea as php_var_real_array(), for an OBJECT PROPERTY's value
 * instead of a plain variable's -- looks `member` up on `obj_id`'s own
 * props (always stored as a flat string, an arrref token when the
 * property itself holds an array -- object properties have no separate
 * "is_array" flag the way a PhpVar does) and decodes it. Returns NULL if
 * the object/property doesn't exist or isn't array-valued. Backs
 * isset()/empty()/count()/is_array() on a "$obj->member[key]" chain --
 * see PhpVarRef.has_member_key's own comment for the real bug this
 * fixes. */
static PhpKVArray *php_member_real_array(int obj_id, const char *member) {
    if (obj_id < 0) return NULL;
    char tmp[PHP_VAL_MAX];
    if (!php_kv_has(&g_objects[obj_id].props, member, tmp, sizeof tmp)) return NULL;
    int aid = php_arrref_decode(tmp);
    return (aid >= 0) ? &g_arrays[aid] : NULL;
}

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
    } else if (strcmp(name, "_GET") == 0 || strcmp(name, "_POST") == 0 || strcmp(name, "_COOKIE") == 0) {
        ref->is_superglobal = 1;
        ref->sg_arr = (strcmp(name, "_GET") == 0) ? &st->get : (strcmp(name, "_POST") == 0) ? &st->post : &st->cookie;
    } else if (strcmp(name, "_REQUEST") == 0) {
        ref->is_request = 1;
        ref->st_for_request = st;
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
        if (*st->src == '$') { /* "isset($obj->$field)" etc -- see php_eval_factor's own comment on this same dynamic-member shape */
            char dyn[PHP_VAL_MAX];
            php_eval_factor(st, dyn, sizeof dyn);
            strncpy(member, dyn, sizeof member - 1); member[sizeof member - 1] = 0;
        } else {
            php_read_ident(st, member, sizeof member);
        }
        strncpy(ref->member, member, sizeof ref->member - 1); ref->member[sizeof ref->member - 1] = 0;
        ref->has_member = 1;
        ref->obj_id = php_objref_decode(base_val);
        php_skip_ws(st);
        c = *st->src;
        if (c == '[') {
            st->src++;
            php_skip_ws(st);
            c = *st->src;
            if (c != ']') {
                char kb[PHP_VAL_MAX];
                php_eval_expr(st, kb, sizeof kb);
                strncpy(ref->member_key, kb, sizeof ref->member_key - 1); ref->member_key[sizeof ref->member_key - 1] = 0;
                ref->has_member_key = 1;
                php_skip_ws(st);
            }
            c = *st->src;
            if (c == ']') st->src++;
        }
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
    /* Real PHP: array assignment is always a VALUE copy, never aliasing
     * -- "$this->b = $this->a;" (or "$arr2[$k] = $arr1['x'];", any
     * "container[key] = <bare-expression-yielding-an-array>;" shape)
     * must give $this->b its OWN independent array, not point it at the
     * exact same g_arrays[] slot $this->a already uses. This is a
     * DIFFERENT code path from the "array(...)"/"[...]" literal-RHS
     * branches just above (those already allocate a genuinely fresh
     * g_arrays[] slot on every assignment, since a literal always
     * builds new storage) -- this is specifically the "RHS already
     * evaluated to an existing arrref TOKEN, pointing at storage some
     * OTHER variable/property already owns" case, which php_eval_expr's
     * own "$name" bare-read path already handles correctly for a PLAIN
     * local variable (see its own "snapshot into a fresh slot" comment)
     * but this container-based lvalue path never did. Confirmed as a
     * real, severe, previously-undiscovered bug via a direct repro
     * ("$this->b = $this->a; $this->b['y'] = 2;" also mutating
     * $this->a) traced back from real WordPress core's own
     * WP_Query::parse_query() ("$this->query = wp_parse_args($query);
     * $this->query_vars = $this->query;" -- two DIFFERENT properties
     * meant to independently diverge, since $this->query_vars is
     * immediately afterward filled in with ~60 more default keys via
     * fill_query_vars() while $this->query is supposed to stay exactly
     * the caller's own original, unfilled keys) -- with the two
     * properties actually ALIASED, $this->query ended up with every
     * one of those same ~60 default keys too, so "isset($this->query
     * ['s'])" (WordPress's own real way of asking "did the ORIGINAL
     * request explicitly include a search term") was always true
     * (every request has an empty 's' DEFAULT key once fill_query_vars
     * runs), permanently misclassifying every single page load as a
     * search query -- is_home()/is_front_page()/the document title/
     * the whole template-selection conditional-tag chain downstream of
     * that one flag were all silently wrong as a direct result. */
    int aid = php_arrref_decode(val);
    if (aid >= 0) {
        int copy_id = php_array_deep_copy(aid);
        if (copy_id >= 0) php_arrref_encode(copy_id, val, sizeof val);
    }
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
        if (*st->src == '$') { /* "$this->$field = ..." -- see php_eval_factor's own comment on this same dynamic-member shape */
            char dyn[PHP_VAL_MAX];
            php_eval_factor(st, dyn, sizeof dyn);
            strncpy(member, dyn, sizeof member - 1); member[sizeof member - 1] = 0;
        } else {
            php_read_ident(st, member, sizeof member);
        }
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
            if (!has_key) { snprintf(kb, sizeof kb, "%d", v->next_index); has_key = 1; }
            strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
            char *endp;
            long n = strtol(key, &endp, 10);
            if (key[0] && *endp == 0 && n >= v->next_index) v->next_index = (int)n + 1;
            if (v->is_array) {
                container = &v->arr;
            } else {
                /* `v` might already hold an array BY REFERENCE (an
                 * arrref token in v->val, is_array left 0) -- the shape
                 * every function-PARAMETER array (php_var_set() always
                 * clears is_array and just copies the value string, see
                 * its own comment) and every array read back from a call
                 * is stored as. Reuse that SAME underlying g_arrays[]
                 * slot rather than unconditionally switching to `v`'s
                 * own embedded native storage below and silently
                 * orphaning whatever the token pointed to -- confirmed
                 * as a real, high-impact bug this session:
                 * "$param['newkey'] = x;" on ANY array-typed function
                 * parameter discarded every pre-existing key the caller
                 * had set (e.g. WP_Query::fill_query_vars()'s own
                 * "$query_vars[$key] = '';" loop wiping out the caller's
                 * real 's'/'p'/etc values the very first time it filled
                 * in ANY missing default), because this code always
                 * created a fresh, empty v->arr here regardless of what
                 * v->val already referenced. Only a genuinely non-array
                 * `v` (no arrref token at all -- a real scalar, or an
                 * unset variable) still auto-vivifies into `v`'s own
                 * embedded storage, matching real PHP's "$x['k'] = v;"
                 * on an unset/scalar $x turning it into a fresh array. */
                int aid = php_arrref_decode(v->val);
                if (aid >= 0) {
                    container = &g_arrays[aid];
                } else {
                    v->is_array = 1;
                    container = &v->arr;
                }
            }
        }
    } else if (c == '-' && st->src[1] == '>') {
        st->src += 2;
        char member[64];
        if (*st->src == '$') { /* "$obj->$field = ..." -- see php_eval_factor's own comment on this same dynamic-member shape */
            char dyn[PHP_VAL_MAX];
            php_eval_factor(st, dyn, sizeof dyn);
            strncpy(member, dyn, sizeof member - 1); member[sizeof member - 1] = 0;
        } else {
            php_read_ident(st, member, sizeof member);
        }
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
    if (c == '@') {
        /* "@expr" -- real PHP's error-suppression operator (silences
         * any warning/notice the expression would otherwise raise).
         * This subset has no warnings/notices to suppress in the first
         * place, so the correct simplification is a plain pass-through
         * to the following expression -- NOT a no-op that leaves "@"
         * unconsumed. Before this case existed, a leading "@" matched
         * nothing in this whole function (same failure shape as "&"'s
         * own fix just above: falls through everything else, including
         * the number-literal scanner at the bottom, which needs a
         * digit/'.'/'-' and "@" has none) -- st->src never advanced,
         * so the expression evaluated to "" and desynced whatever
         * parsing happened next. Confirmed as a real bug via
         * maybe_unserialize()'s own "return @unserialize(trim($data));"
         * (wp-includes/functions.php) -- the single most common real
         * use of "@" across WordPress core -- always returning empty
         * instead of the real unserialized value. */
        st->src++;
        php_eval_factor(st, out, outcap);
        return;
    }
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
        if (strcmp(name, "_GET") == 0 || strcmp(name, "_POST") == 0 || strcmp(name, "_SERVER") == 0 || strcmp(name, "_COOKIE") == 0 || strcmp(name, "_REQUEST") == 0) {
            php_skip_ws(st);
            char key[128]; key[0] = 0;
            c = *st->src;
            if (c == '[') {
                st->src++;
                php_skip_ws(st);
                char kb[PHP_VAL_MAX];
                c = *st->src;
                if (c == '"' || c == '\'') php_read_string_lit(st, kb, sizeof kb);
                else {
                    /* An unquoted bareword here ("$_COOKIE[LOGGED_IN_
                     * COOKIE]", a real, common WordPress shape -- auth
                     * cookies are always looked up by a CONSTANT, never
                     * a literal string) is a CONSTANT reference in real
                     * PHP, not a literal string -- resolve it as one
                     * (falling back to the bareword text itself if no
                     * such constant is defined, this file's usual "safe
                     * default" convention). Previously always read as a
                     * literal identifier string, so "$_COOKIE[LOGGED_IN_
                     * COOKIE]" looked up the literal 17-character key
                     * "LOGGED_IN_COOKIE" instead of the real cookie name
                     * ("wordpress_logged_in_<hash>") the constant
                     * actually holds -- always missing, even when the
                     * real cookie was present and correctly parsed,
                     * silently breaking wp_validate_auth_cookie()'s own
                     * very first lookup and therefore ALL cookie-based
                     * login-persistence checks site-wide. */
                    char ident[128];
                    php_read_ident(st, ident, sizeof ident);
                    const char *cv = php_const_find(ident);
                    strncpy(kb, cv ? cv : ident, sizeof kb - 1); kb[sizeof kb - 1] = 0;
                }
                strncpy(key, kb, sizeof key - 1); key[sizeof key - 1] = 0;
                php_skip_ws(st);
                c = *st->src;
                if (c == ']') st->src++;
            }
            if (strcmp(name, "_GET") == 0) php_kv_lookup(&st->get, key, out, outcap);
            else if (strcmp(name, "_POST") == 0) php_kv_lookup(&st->post, key, out, outcap);
            else if (strcmp(name, "_COOKIE") == 0) php_kv_lookup(&st->cookie, key, out, outcap);
            else if (strcmp(name, "_REQUEST") == 0) {
                /* Real PHP: $_REQUEST is GET+POST merged (COOKIE excluded
                 * by this project's own chosen request_order, matching
                 * the common modern PHP default "GP") -- POST wins on a
                 * key collision, matching real PHP's own default
                 * variables_order precedence. Previously entirely
                 * UNIMPLEMENTED (not in this dispatch's name list at
                 * all), so "$_REQUEST[...]" always silently read as an
                 * empty/undefined variable -- a real, common WordPress
                 * pattern (wp-login.php's own "isset( $_REQUEST[
                 * 'redirect_to'] )"/"$_REQUEST['reauth']" among many
                 * others) that this quietly broke. */
                char tmp[PHP_VAL_MAX];
                if (php_kv_has(&st->post, key, tmp, sizeof tmp)) { strncpy(out, tmp, outcap - 1); out[outcap - 1] = 0; }
                else php_kv_lookup(&st->get, key, out, outcap);
            }
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
                    else if (php_objref_decode(v->val) >= 0) cur[0] = 0;
                    else php_string_char_at(v->val, key, cur, sizeof cur);
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
                php_skip_ws(st);
                /* "$obj->$field" -- a DYNAMIC member name (the name
                 * itself is a variable, not a literal identifier).
                 * php_read_ident() returns an empty string and consumes
                 * NOTHING when the next char is "$" (not an identifier-
                 * start character), which used to leave the "$field..."
                 * text completely unconsumed -- badly desyncing every-
                 * thing parsed after it. Real, common WordPress pattern
                 * (e.g. WP_List_Util::pluck()'s own "$newlist[$key] =
                 * $value->$field;", confirmed as a real, previously-
                 * unfixed blocker -- see this session's own WP_User::
                 * init() comment for the concrete case that finally
                 * needed it: copying $data's columns onto $this via a
                 * "foreach (...) { $this->$col = $data->$col; }" loop). */
                if (*st->src == '$') {
                    char dyn[PHP_VAL_MAX];
                    php_eval_factor(st, dyn, sizeof dyn);
                    strncpy(member, dyn, sizeof member - 1); member[sizeof member - 1] = 0;
                } else {
                    php_read_ident(st, member, sizeof member);
                }
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
                if (aid >= 0) {
                    php_kv_lookup(&g_arrays[aid], key, cur, sizeof cur);
                } else if (php_objref_decode(cur) >= 0) {
                    cur[0] = 0;
                } else {
                    char sc[2]; php_string_char_at(cur, key, sc, sizeof sc);
                    strncpy(cur, sc, sizeof cur - 1); cur[sizeof cur - 1] = 0;
                }
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
            php_skip_ws(st);
            /* "ClassName::$prop" (a static-property READ, e.g.
             * "self::$back_compat_keys") -- a completely different shape
             * from "ClassName::method"/"ClassName::CONST"/"ClassName::
             * class" (all of which read a plain identifier here); must be
             * checked BEFORE calling php_read_ident below, since '$' isn't
             * an identifier-start character and would otherwise read an
             * EMPTY `member`, silently leaving "$prop..." unconsumed in
             * st->src for the caller to mis-parse next -- see
             * PhpClass.static_prop_names' own comment for the real-
             * WordPress bug this caused. */
            if (*st->src == '$') {
                st->src++;
                char sname[64];
                php_read_ident(st, sname, sizeof sname);
                const char *resolved_class2 = name;
                PhpClass *self_cls2 = NULL;
                if ((strcmp(name, "self") == 0 || strcmp(name, "static") == 0 || strcmp(name, "parent") == 0) && st->has_this && st->this_obj_id >= 0) {
                    self_cls2 = php_class_find(g_objects[st->this_obj_id].class_name);
                    if (strcmp(name, "parent") == 0 && self_cls2) {
                        self_cls2 = self_cls2->parent_name[0] ? php_class_find(self_cls2->parent_name) : NULL;
                    }
                    resolved_class2 = self_cls2 ? self_cls2->name : name;
                }
                PhpClass *cls2 = self_cls2 ? self_cls2 : php_class_find(resolved_class2);
                char *slot = php_class_find_static_prop(cls2, sname);
                if (slot) { strncpy(out, slot, outcap - 1); out[outcap - 1] = 0; }
                else out[0] = 0;
                return;
            }
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
            php_skip_ws(st);
            if (*st->src == '$') {
                /* "new $manager($user_id)" -- a DYNAMIC class name (the
                 * name itself is a variable, e.g. built from a filtered
                 * string like WP_Session_Tokens::get_instance()'s own
                 * "$manager = apply_filters('session_token_manager',
                 * 'WP_User_Meta_Session_Tokens'); return new
                 * $manager($user_id);") -- a real, common PHP pattern
                 * for pluggable/filterable class instantiation.
                 * php_read_ident() returns an empty string and consumes
                 * NOTHING when the next char is "$", which used to leave
                 * "$manager(...)" completely unconsumed, badly desyncing
                 * everything parsed after it. Same fix, same reasoning,
                 * as the "$obj->$field" dynamic-member-name fix. */
                char dyn[PHP_VAL_MAX];
                php_eval_factor(st, dyn, sizeof dyn);
                strncpy(cname, dyn, sizeof cname - 1); cname[sizeof cname - 1] = 0;
            } else {
                php_read_ident(st, cname, sizeof cname);
            }
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
                php_skip_ws(st);
                /* "isset(self::$prop)"/"isset(ClassName::$prop)" -- a
                 * static-property check, a completely different shape
                 * from every other isset()/empty() argument this handler
                 * supports (all of which start with '$'; see
                 * php_resolve_varref's own "if (c != '$') return;" early-
                 * out). Without this, php_resolve_varref returned a fully
                 * zeroed PhpVarRef for a "self::..."-shaped argument
                 * (ref.var stays NULL), so isset() always evaluated
                 * false/empty() always evaluated true here regardless of
                 * whether the static property actually held a value --
                 * see PhpClass.static_prop_names' own comment for the
                 * real WordPress bug (WP_User::__construct()'s "if
                 * (!isset(self::$back_compat_keys))" cache guard) this
                 * caused. */
                {
                    const char *p2 = st->src;
                    if ((*p2 >= 'a' && *p2 <= 'z') || (*p2 >= 'A' && *p2 <= 'Z') || *p2 == '_') {
                        char cname2[64];
                        int ci = 0;
                        while (((*p2>='a'&&*p2<='z')||(*p2>='A'&&*p2<='Z')||(*p2>='0'&&*p2<='9')||*p2=='_') && ci < (int)sizeof cname2 - 1) { cname2[ci++] = *p2; p2++; }
                        cname2[ci] = 0;
                        while (*p2==' '||*p2=='\t') p2++;
                        if (p2[0] == ':' && p2[1] == ':' && p2[2] == '$') {
                            st->src = p2 + 3;
                            char sname2[64];
                            php_read_ident(st, sname2, sizeof sname2);
                            php_skip_ws(st);
                            if (*st->src == ')') st->src++;
                            const char *resolved3 = cname2;
                            PhpClass *self_cls3 = NULL;
                            if ((strcmp(cname2, "self") == 0 || strcmp(cname2, "static") == 0 || strcmp(cname2, "parent") == 0) && st->has_this && st->this_obj_id >= 0) {
                                self_cls3 = php_class_find(g_objects[st->this_obj_id].class_name);
                                if (strcmp(cname2, "parent") == 0 && self_cls3) self_cls3 = self_cls3->parent_name[0] ? php_class_find(self_cls3->parent_name) : NULL;
                                resolved3 = self_cls3 ? self_cls3->name : cname2;
                            }
                            PhpClass *cls3 = self_cls3 ? self_cls3 : php_class_find(resolved3);
                            char *slot3 = php_class_find_static_prop(cls3, sname2);
                            int result3;
                            if (strcmp(name, "isset") == 0) result3 = (slot3 != NULL && slot3[0] != 0);
                            else result3 = (slot3 == NULL || !php_truthy(slot3));
                            strncpy(out, result3 ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
                            return;
                        }
                    }
                }
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
                    if (ref.is_superglobal) {
                        char tmp[PHP_VAL_MAX];
                        result = ref.has_key ? php_kv_has(ref.sg_arr, ref.key, tmp, sizeof tmp) : (ref.sg_arr->count > 0);
                    }
                    else if (ref.is_request) {
                        char tmp[PHP_VAL_MAX];
                        result = ref.has_key && (php_kv_has(&ref.st_for_request->post, ref.key, tmp, sizeof tmp) || php_kv_has(&ref.st_for_request->get, ref.key, tmp, sizeof tmp));
                    }
                    else if (ref.has_member && ref.has_member_key) {
                        char tmp[PHP_VAL_MAX];
                        PhpKVArray *a = php_member_real_array(ref.obj_id, ref.member);
                        result = a && php_kv_has(a, ref.member_key, tmp, sizeof tmp);
                    }
                    else if (ref.has_member) {
                        char tmp[PHP_VAL_MAX];
                        result = ref.obj_id >= 0 && php_kv_has(&g_objects[ref.obj_id].props, ref.member, tmp, sizeof tmp);
                    }
                    else if (!ref.var) result = 0;
                    else if (ref.has_key) {
                        char tmp[PHP_VAL_MAX];
                        PhpKVArray *a = php_var_real_array(ref.var);
                        result = a && php_kv_has(a, ref.key, tmp, sizeof tmp);
                    }
                    else result = 1;
                } else { /* empty() */
                    if (ref.is_superglobal) {
                        char tmp[PHP_VAL_MAX];
                        if (ref.has_key) {
                            result = !php_kv_has(ref.sg_arr, ref.key, tmp, sizeof tmp) || !php_truthy(tmp);
                        } else result = (ref.sg_arr->count == 0);
                    }
                    else if (ref.is_request) {
                        char tmp[PHP_VAL_MAX];
                        if (!ref.has_key) result = 1;
                        else if (php_kv_has(&ref.st_for_request->post, ref.key, tmp, sizeof tmp)) result = !php_truthy(tmp);
                        else if (php_kv_has(&ref.st_for_request->get, ref.key, tmp, sizeof tmp)) result = !php_truthy(tmp);
                        else result = 1;
                    }
                    else if (ref.has_member && ref.has_member_key) {
                        char tmp[PHP_VAL_MAX];
                        PhpKVArray *a = php_member_real_array(ref.obj_id, ref.member);
                        if (!a || !php_kv_has(a, ref.member_key, tmp, sizeof tmp)) result = 1;
                        else result = !php_truthy(tmp);
                    }
                    else if (ref.has_member) {
                        char tmp[PHP_VAL_MAX];
                        if (ref.obj_id < 0 || !php_kv_has(&g_objects[ref.obj_id].props, ref.member, tmp, sizeof tmp)) result = 1;
                        else result = !php_truthy(tmp);
                    }
                    else if (!ref.var) result = 1;
                    else if (ref.has_key) {
                        char tmp[PHP_VAL_MAX];
                        PhpKVArray *a = php_var_real_array(ref.var);
                        if (!a || !php_kv_has(a, ref.key, tmp, sizeof tmp)) result = 1;
                        else result = !php_truthy(tmp);
                    } else {
                        PhpKVArray *a = php_var_real_array(ref.var);
                        if (a) result = (a->count == 0);
                        else result = !php_truthy(ref.var->val);
                    }
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
                if (ref.is_superglobal) {
                    if (ref.has_key) {
                        char tmp[PHP_VAL_MAX];
                        int aid;
                        if (php_kv_has(ref.sg_arr, ref.key, tmp, sizeof tmp)) {
                            aid = php_arrref_decode(tmp);
                            n = (aid >= 0) ? g_arrays[aid].count : 1;
                        } else n = 0;
                    } else n = ref.sg_arr->count;
                }
                else if (ref.has_member && ref.has_member_key) {
                    /* "count($obj->member[key])" -- same has_member_key
                     * shape isset()/empty() now handle, see
                     * PhpVarRef.has_member_key's own comment. */
                    PhpKVArray *a = php_member_real_array(ref.obj_id, ref.member);
                    char tmp[PHP_VAL_MAX];
                    if (a && php_kv_has(a, ref.member_key, tmp, sizeof tmp)) {
                        int aid = php_arrref_decode(tmp);
                        n = (aid >= 0) ? g_arrays[aid].count : 1;
                    } else n = 0;
                }
                else if (ref.has_member) {
                    char tmp[PHP_VAL_MAX];
                    if (ref.obj_id >= 0 && php_kv_has(&g_objects[ref.obj_id].props, ref.member, tmp, sizeof tmp)) {
                        int aid = php_arrref_decode(tmp);
                        n = (aid >= 0) ? g_arrays[aid].count : 1;
                    } else n = 0;
                }
                else if (ref.has_key) {
                    /* "count($arr['key'])" -- MUST count the NESTED
                     * value at container[key], not $arr itself. This
                     * branch was missing entirely (has_key was never
                     * checked here at all), so count() on a subscripted
                     * expression silently fell through to counting the
                     * OUTER array's own key count instead -- confirmed
                     * as a real, high-impact bug this session:
                     * WP_Query::parse_search()'s own "count($query_vars
                     * ['search_terms']) > 9" check (deciding whether to
                     * treat the search as a multi-word query or fall
                     * back to whole-sentence matching) was actually
                     * reading count($query_vars) -- the ~40-plus keys
                     * fill_query_vars() always populates -- which is
                     * ALWAYS > 9 regardless of how many real search
                     * terms there are, so the "> 9 terms, use fallback"
                     * branch fired unconditionally, immediately
                     * overwriting search_terms with something that
                     * itself depended on this same broken count() chain
                     * elsewhere. */
                    PhpKVArray *outer = ref.var ? php_var_real_array(ref.var) : NULL;
                    char cur[PHP_VAL_MAX];
                    if (outer && php_kv_has(outer, ref.key, cur, sizeof cur)) {
                        int aid = php_arrref_decode(cur);
                        n = (aid >= 0) ? g_arrays[aid].count : 1;
                    } else n = 0;
                }
                else if (ref.var) {
                    PhpKVArray *a = php_var_real_array(ref.var);
                    n = a ? a->count : 1;
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
                if (ref.is_superglobal) {
                    if (ref.has_key) {
                        char tmp[PHP_VAL_MAX];
                        isarr = php_kv_has(ref.sg_arr, ref.key, tmp, sizeof tmp) && php_arrref_decode(tmp) >= 0;
                    } else isarr = 1; /* $_GET/$_POST themselves are always arrays */
                }
                else if (ref.has_member && ref.has_member_key) {
                    PhpKVArray *a = php_member_real_array(ref.obj_id, ref.member);
                    char tmp[PHP_VAL_MAX];
                    isarr = a && php_kv_has(a, ref.member_key, tmp, sizeof tmp) && php_arrref_decode(tmp) >= 0;
                } else if (ref.has_member) {
                    char tmp[PHP_VAL_MAX];
                    isarr = ref.obj_id >= 0 && php_kv_has(&g_objects[ref.obj_id].props, ref.member, tmp, sizeof tmp) && php_arrref_decode(tmp) >= 0;
                } else if (ref.has_key) {
                    /* "is_array($arr['key'])" -- same missing-has_key
                     * gap count() above had (see its own comment): must
                     * check the NESTED value at container[key], not
                     * $arr itself. */
                    PhpKVArray *outer = ref.var ? php_var_real_array(ref.var) : NULL;
                    char cur[PHP_VAL_MAX];
                    isarr = outer && php_kv_has(outer, ref.key, cur, sizeof cur) && php_arrref_decode(cur) >= 0;
                } else {
                    isarr = ref.var && (ref.var->is_array || php_arrref_decode(ref.var->val) >= 0);
                }
                strncpy(out, isarr ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
                return;
            }
            if (strcmp(name, "unset") == 0) {
                /* Same "needs the raw variable/array, not a stringified
                   value" reasoning as isset()/empty()/count()/is_array()
                   above -- see PhpVarRef's own comment for exactly which
                   shapes are resolved ("$name", "$name[key]",
                   "$base->member", one level each; NOT a deeper chain
                   like "$this->prop[k1][k2]" -- a documented, narrow gap,
                   real WordPress's own WP_Hook::remove_filter() is the
                   one place in a typical page load that would need it,
                   and removal during normal page-render boot is rare).
                   Found missing entirely this session (2000+ real calls
                   silently no-op'ing on every WordPress page load --
                   see this session's own notes on why that's a real,
                   high-impact gap, not just a theoretical one). */
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                c = *st->src;
                if (c != ')') php_skip_to_paren_close(st);
                c = *st->src;
                if (c == ')') st->src++;
                if (ref.has_member && ref.has_member_key) {
                    /* "unset($obj->member[key])" -- same has_member_key
                     * shape isset()/empty()/count()/is_array() now
                     * handle; without this, the whole `member` array got
                     * wiped instead of just the one key (e.g.
                     * WP_Dependencies::add()'s own
                     * "unset($this->queued_before_register[$handle]);"). */
                    PhpKVArray *a = php_member_real_array(ref.obj_id, ref.member);
                    if (a) php_kv_remove(a, ref.member_key);
                } else if (ref.has_member) {
                    if (ref.obj_id >= 0) php_kv_remove(&g_objects[ref.obj_id].props, ref.member);
                } else if (ref.var && ref.has_key) {
                    if (ref.var->is_array) php_kv_remove(&ref.var->arr, ref.key);
                    else { int aid = php_arrref_decode(ref.var->val); if (aid >= 0) php_kv_remove(&g_arrays[aid], ref.key); }
                } else if (ref.var) {
                    ref.var->val[0] = 0;
                    ref.var->is_array = 0;
                }
                out[0] = 0;
                return;
            }
            if (strcmp(name, "array_unshift") == 0) {
                /* Real PHP declares this "function array_unshift(array
                   &$array, mixed ...$values): int" -- the FIRST argument
                   is BY REFERENCE, mutated in place. This file's ordinary
                   argument evaluation (php_parse_args -> php_eval_expr)
                   has no by-reference parameter concept at all -- for a
                   PLAIN LOCAL variable holding a native array, reading it
                   bare as a function argument actually SNAPSHOTS it into
                   a FRESH g_arrays[] slot first (see the "$name" read
                   path's own comment on why: correct real-PHP by-VALUE
                   semantics for an ORDINARY function call) -- so the old
                   array_unshift(), despite mutating whatever slot it was
                   handed, was mutating a disposable COPY, never the
                   caller's real variable. Confirmed as the actual root
                   cause of WordPress's own WP_Hook::apply_filters()
                   silently dropping the filtered VALUE argument to every
                   single callback (its "array_unshift($args, $value);"
                   call had no effect) -- found via a minimal standalone
                   repro before tracing it back to this exact builtin.
                   Fixed the same way isset()/empty()/unset() above
                   already have to: resolve the raw variable reference
                   FIRST (php_resolve_varref, bypassing php_eval_expr
                   entirely for this one argument), so the mutation lands
                   on the real, shared storage instead of a copy. Only a
                   bare "$name" first argument is supported (real PHP
                   requires an actual variable here too -- passing
                   anything else is a fatal TypeError in real PHP, so
                   silently doing nothing for a non-variable first arg is
                   a safe, unsurprising degrade). */
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                char newvals[PHP_ARG_MAX][PHP_VAL_MAX];
                int nnew = 0;
                while (*st->src == ',' && nnew < PHP_ARG_MAX) {
                    st->src++;
                    php_eval_expr(st, newvals[nnew], sizeof newvals[nnew]);
                    nnew++;
                    php_skip_ws(st);
                }
                if (*st->src == ')') st->src++;
                if (ref.var && !ref.has_key && !ref.has_member && nnew > 0) {
                    PhpKVArray *a;
                    if (ref.var->is_array) {
                        a = &ref.var->arr;
                    } else {
                        int aid = php_arrref_decode(ref.var->val);
                        if (aid < 0) { aid = php_array_new(); if (aid >= 0) { php_arrref_encode(aid, ref.var->val, sizeof ref.var->val); } }
                        a = (aid >= 0) ? &g_arrays[aid] : NULL;
                    }
                    if (a && php_kv_ensure_cap(a, a->count + nnew)) {
                        int old_count = a->count;
                        int ii;
                        for (ii = old_count - 1; ii >= 0; ii--) {
                            char k[16]; snprintf(k, sizeof k, "%d", ii + nnew);
                            strncpy(a->items[ii + nnew].key, k, sizeof a->items[0].key - 1); a->items[ii + nnew].key[sizeof a->items[0].key - 1] = 0;
                            strncpy(a->items[ii + nnew].val, a->items[ii].val, sizeof a->items[0].val - 1); a->items[ii + nnew].val[sizeof a->items[0].val - 1] = 0;
                        }
                        for (ii = 0; ii < nnew; ii++) {
                            char k[16]; snprintf(k, sizeof k, "%d", ii);
                            strncpy(a->items[ii].key, k, sizeof a->items[0].key - 1); a->items[ii].key[sizeof a->items[0].key - 1] = 0;
                            strncpy(a->items[ii].val, newvals[ii], sizeof a->items[0].val - 1); a->items[ii].val[sizeof a->items[0].val - 1] = 0;
                        }
                        a->count = old_count + nnew;
                        a->cursor = 0;
                    }
                    snprintf(out, outcap, "%d", a ? a->count : 0);
                } else {
                    out[0] = 0;
                }
                return;
            }
            if (strcmp(name, "sort") == 0 || strcmp(name, "rsort") == 0 || strcmp(name, "asort") == 0 ||
                strcmp(name, "arsort") == 0 || strcmp(name, "ksort") == 0 || strcmp(name, "krsort") == 0) {
                /* Real PHP declares every one of these "function
                 * sort(array &$array, ...): bool" -- by reference, same
                 * "resolve the raw variable" story as array_unshift()
                 * above (see its own comment for why a bare read
                 * otherwise snapshots instead of sharing storage).
                 * Previously entirely UNIMPLEMENTED -- found in this
                 * session's own audit of real builtins the vendored
                 * WordPress tree calls vs. what this file implements. */
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                if (*st->src == ',') { st->src++; php_eval_expr(st, out, outcap); php_skip_ws(st); } /* optional $flags arg, evaluated for side effects/consumption then ignored -- SORT_REGULAR-shaped default only, see php_default_cmp's own comment */
                if (*st->src == ')') st->src++;
                PhpKVArray *a = php_varref_mutable_array(&ref);
                if (a) {
                    int by_key = (strcmp(name, "ksort") == 0 || strcmp(name, "krsort") == 0);
                    int reverse = (strcmp(name, "rsort") == 0 || strcmp(name, "arsort") == 0 || strcmp(name, "krsort") == 0);
                    int reindex = (strcmp(name, "sort") == 0 || strcmp(name, "rsort") == 0);
                    php_array_sort_by(a, by_key, reverse, reindex);
                }
                strncpy(out, "1", outcap - 1); out[outcap - 1] = 0;
                return;
            }
            if (strcmp(name, "usort") == 0 || strcmp(name, "uasort") == 0 || strcmp(name, "uksort") == 0) {
                /* Same by-reference story, plus a real user callback
                 * (a bare insertion sort, same "simple first" scope as
                 * php_array_sort_by's own comment -- calls the callback
                 * via php_invoke_callable for every comparison, same
                 * mechanism call_user_func() already uses). */
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                char callable[PHP_VAL_MAX]; callable[0] = 0;
                if (*st->src == ',') { st->src++; php_eval_expr(st, callable, sizeof callable); php_skip_ws(st); }
                if (*st->src == ')') st->src++;
                PhpKVArray *a = php_varref_mutable_array(&ref);
                if (a && callable[0]) {
                    int by_key = (strcmp(name, "uksort") == 0);
                    int reindex = (strcmp(name, "usort") == 0);
                    int i, j;
                    for (i = 1; i < a->count; i++) {
                        PhpKV tmp = a->items[i];
                        j = i - 1;
                        while (j >= 0) {
                            char *cargs[2];
                            char c0[128], c1[128];
                            cargs[0] = c0; cargs[1] = c1;
                            strncpy(c0, by_key ? a->items[j].key : a->items[j].val, sizeof c0 - 1); c0[sizeof c0 - 1] = 0;
                            strncpy(c1, by_key ? tmp.key : tmp.val, sizeof c1 - 1); c1[sizeof c1 - 1] = 0;
                            char r[PHP_VAL_MAX];
                            php_invoke_callable(st, callable, cargs, 2, r, sizeof r);
                            if (atoi(r) <= 0) break;
                            a->items[j + 1] = a->items[j];
                            j--;
                        }
                        a->items[j + 1] = tmp;
                    }
                    if (reindex) {
                        for (i = 0; i < a->count; i++) {
                            char k[16]; snprintf(k, sizeof k, "%d", i);
                            strncpy(a->items[i].key, k, sizeof a->items[i].key - 1); a->items[i].key[sizeof a->items[i].key - 1] = 0;
                        }
                    }
                    a->cursor = 0;
                }
                strncpy(out, "1", outcap - 1); out[outcap - 1] = 0;
                return;
            }
            if (strcmp(name, "array_push") == 0) {
                /* "function array_push(array &$array, mixed ...$values): int"
                 * -- by reference, same story as array_unshift() (its own
                 * mirror-image sibling) but appending at the END instead
                 * of the front, with fresh auto-incrementing integer
                 * keys (real PHP semantics -- array_push() always uses
                 * plain 0..n-1-shaped NEXT keys, even on an array with
                 * non-sequential/string keys already in it). */
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                PhpKVArray *a = php_varref_mutable_array(&ref);
                while (*st->src == ',') {
                    st->src++;
                    char v[PHP_VAL_MAX];
                    php_eval_expr(st, v, sizeof v);
                    php_skip_ws(st);
                    if (a) {
                        int maxk = -1, ii;
                        for (ii = 0; ii < a->count; ii++) { char *e; long n = strtol(a->items[ii].key, &e, 10); if (*e == 0 && (int)n > maxk) maxk = (int)n; }
                        char k[16]; snprintf(k, sizeof k, "%d", maxk + 1);
                        php_kv_set(a, k, v);
                    }
                }
                if (*st->src == ')') st->src++;
                snprintf(out, outcap, "%d", a ? a->count : 0);
                return;
            }
            if (strcmp(name, "array_shift") == 0) {
                /* "function array_shift(array &$array): mixed" -- by
                 * reference; removes and returns the FIRST element,
                 * shifting every remaining element down and (real PHP
                 * semantics) renumbering every INTEGER key back to
                 * 0..n-2 (string keys are left untouched). */
                st->src++;
                PhpVarRef ref;
                php_resolve_varref(st, &ref);
                php_skip_ws(st);
                if (*st->src == ')') st->src++;
                PhpKVArray *a = php_varref_mutable_array(&ref);
                if (a && a->count > 0) {
                    strncpy(out, a->items[0].val, outcap - 1); out[outcap - 1] = 0;
                    int i, next_idx = 0;
                    for (i = 1; i < a->count; i++) {
                        char *e; strtol(a->items[i].key, &e, 10);
                        int is_int_key = (*e == 0 && a->items[i].key[0] != 0);
                        a->items[i - 1] = a->items[i];
                        if (is_int_key) {
                            char k[16]; snprintf(k, sizeof k, "%d", next_idx++);
                            strncpy(a->items[i - 1].key, k, sizeof a->items[i - 1].key - 1); a->items[i - 1].key[sizeof a->items[i - 1].key - 1] = 0;
                        }
                    }
                    a->count--;
                    a->cursor = 0;
                } else {
                    out[0] = 0;
                }
                return;
            }
            if (strcmp(name, "parse_str") == 0) {
                /* "function parse_str(string $string, array &$result): void"
                 * -- the SECOND argument is by reference, same story as
                 * array_unshift()/sort()/etc above (see array_unshift's
                 * own comment). Previously entirely UNIMPLEMENTED. Parses
                 * a real query-string ("a=1&b[]=2&c=3") the same way
                 * php_parse_kv_string() already does for $_GET/$_POST
                 * (this file's own request-parsing helper), reused here
                 * directly. */
                st->src++;
                char qs[PHP_VAL_MAX];
                php_eval_expr(st, qs, sizeof qs);
                php_skip_ws(st);
                if (*st->src == ',') {
                    st->src++;
                    PhpVarRef ref;
                    php_resolve_varref(st, &ref);
                    php_skip_ws(st);
                    PhpKVArray *a = php_varref_mutable_array(&ref);
                    if (a) {
                        a->count = 0; a->cursor = 0; /* real parse_str() REPLACES the array's own content */
                        php_parse_kv_string(qs, a);
                    }
                }
                if (*st->src == ')') st->src++;
                out[0] = 0;
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
        if (c == '*' || c == '/' || c == '%') {
            /* "%" is real PHP's modulo operator at this same precedence
             * level (alongside "*"/"/") -- previously entirely
             * UNIMPLEMENTED (not checked for at all in this loop), a
             * real, high-impact, previously-undiscovered gap: "$n % 2"
             * silently evaluated to just "$n" itself (the "% 2" text was
             * simply left unconsumed, dangling for whatever ran next to
             * deal with) -- confirmed via a direct repro ("$n % 2"
             * returning 4 instead of 0 for $n=4). A real, common
             * operator (even/odd checks, alternating-row template
             * classes, cyclic pagination, hashing). Real PHP modulo
             * casts both operands to int first (unlike "*"/"/", which
             * stay floating-point) and is undefined/zero here for a
             * zero divisor, matching this file's own existing "/"-by-
             * zero "degrade to 0.0" choice rather than a fatal
             * DivisionByZeroError. */
            char op = c; st->src++;
            char rhs[PHP_VAL_MAX];
            php_eval_factor(st, rhs, sizeof rhs);
            double a = php_to_num(lhs), b = php_to_num(rhs);
            double r;
            if (op == '*') r = a * b;
            else if (op == '/') r = (b != 0 ? a / b : 0.0);
            else { long ib = (long)b; r = ib != 0 ? (double)((long)a % ib) : 0.0; }
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
            /* Real PHP: "$a + $b" is ARRAY UNION when both operands are
             * arrays (left's keys/values win on collision, right
             * contributes only keys the left doesn't already have) --
             * completely different from "+" on scalars. This subset
             * previously always ran the numeric path below regardless of
             * operand shape: php_to_num() on an arrref-token string (no
             * leading digit) parses as 0, so "$data + compact(...)"
             * (used constantly in real WordPress to merge two associative
             * arrays, e.g. wp_insert_user()'s own "$data = $data +
             * compact('user_login');") silently collapsed $data to the
             * scalar string "0" -- confirmed as the direct cause of
             * wp_insert_user() always returning a WP_Error("empty_data")
             * (its own "empty($data) || !is_array($data)" guard, right
             * after this exact union, caught the corrupted non-array
             * value) so the real installer could never actually create
             * the admin user despite validating and rendering correctly
             * otherwise. "-" has no array meaning in real PHP either
             * (a fatal TypeError) -- left as the numeric path
             * unconditionally, matching this file's "cover the real,
             * common shape" scope limit. */
            int aid_l = (op == '+') ? php_arrref_decode(acc) : -1;
            int aid_r = (op == '+') ? php_arrref_decode(rhs) : -1;
            if (aid_l >= 0 && aid_r >= 0) {
                int nid = php_array_new();
                if (nid >= 0) {
                    int i;
                    for (i = 0; i < g_arrays[aid_l].count; i++) {
                        php_kv_set(&g_arrays[nid], g_arrays[aid_l].items[i].key, g_arrays[aid_l].items[i].val);
                    }
                    for (i = 0; i < g_arrays[aid_r].count; i++) {
                        char tmp[PHP_VAL_MAX];
                        if (!php_kv_has(&g_arrays[nid], g_arrays[aid_r].items[i].key, tmp, sizeof tmp)) {
                            php_kv_set(&g_arrays[nid], g_arrays[aid_r].items[i].key, g_arrays[aid_r].items[i].val);
                        }
                    }
                    php_arrref_encode(nid, acc, sizeof acc);
                }
            } else {
                double a = php_to_num(acc), b = php_to_num(rhs);
                double r = (op == '+') ? a + b : a - b;
                php_num_to_str(r, acc, sizeof acc);
            }
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

/* and := cmp ("&&" cmp)* -- the right-hand side is always PARSED (source
 * position must still advance correctly either way) but its CALLS are
 * suppressed via g_suppress_calls when the left side already makes the
 * whole "&&" false, giving real short-circuit semantics for side effects
 * (see g_suppress_calls's own comment for the real crash this fixes and
 * the one narrower remaining gap: a call inside the right-hand side's
 * own nested call ARGUMENTS still evaluates for real). */
static void php_eval_and(PhpState *st, char *out, int outcap) {
    char acc[PHP_VAL_MAX];
    php_eval_cmp(st, acc, sizeof acc);
    for (;;) {
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (c0 == '&' && c1 == '&') {
            st->src += 2;
            char rhs[PHP_VAL_MAX];
            int lhs_falsy = !php_truthy(acc);
            if (lhs_falsy) g_suppress_calls++;
            php_eval_cmp(st, rhs, sizeof rhs);
            if (lhs_falsy) g_suppress_calls--;
            int r = php_truthy(acc) && php_truthy(rhs);
            strncpy(acc, r ? "1" : "0", sizeof acc - 1); acc[sizeof acc - 1] = 0;
        } else break;
    }
    strncpy(out, acc, outcap - 1); out[outcap - 1] = 0;
}

/* or := and ("||" and)* -- see php_eval_and's own comment on real
 * short-circuit evaluation via g_suppress_calls; the same mechanism
 * applies here (right side's calls suppressed when the left is already
 * truthy). */
static void php_eval_or(PhpState *st, char *out, int outcap) {
    char acc[PHP_VAL_MAX];
    php_eval_and(st, acc, sizeof acc);
    for (;;) {
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (c0 == '|' && c1 == '|') {
            st->src += 2;
            char rhs[PHP_VAL_MAX];
            int lhs_truthy = php_truthy(acc);
            if (lhs_truthy) g_suppress_calls++;
            php_eval_and(st, rhs, sizeof rhs);
            if (lhs_truthy) g_suppress_calls--;
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
    /* "??" (null-coalescing) -- MUST be checked before the single-"?"
     * ternary branch below, and consumed as its own two-character
     * operator: falling into the ternary branch instead (the OLD, only
     * behavior here) consumed just the FIRST "?", then tried to parse
     * the ternary's own "true" branch starting at the SECOND "?" -- not
     * a valid expression start, so php_eval_or/php_eval_factor returned
     * garbage/empty for it regardless of what `cond` itself correctly
     * evaluated to, and THAT (not `cond`) is what a truthy `cond` then
     * selected. Confirmed as a real, high-impact bug this session: "??"
     * was previously not a special case at ALL, so EVERY "X ?? Y"
     * expression anywhere in real WordPress code -- extremely common
     * for "use this value if set, else a default" -- silently evaluated
     * to empty/garbage regardless of X's own real value. Root-caused via
     * WP_Query::get_posts()'s own "$where = $clauses['where'] ?? '';"
     * (and six sibling lines, twice over) silently discarding a
     * correctly-built WHERE clause (confirmed correct one line earlier)
     * right before assembling the final SQL -- the last of several real
     * bugs found this session while chasing why WordPress's own content
     * search returned no results despite a real matching post existing.
     * This subset has no distinct "null" value from "" (empty string) --
     * matching every other place in this file that already conflates
     * "unset"/"empty" (isset()/empty()'s own implementations, etc), "??"
     * here uses `cond`'s value if non-empty, otherwise evaluates and
     * uses the right-hand side -- the same "empty string means absent"
     * convention, not a literal is-strictly-NULL check real PHP uses
     * (a real, narrow, documented difference: "0" ?? 'x' behaves the
     * same either way since "0" is non-empty/non-null in both, but ""
     * ?? 'x' correctly gives 'x' here where real PHP would give ""
     * since "" is a real, non-null value there -- not a shape any real
     * code in this project's own vendored tree was found relying on). */
    if (c == '?' && st->src[1] == '?') {
        st->src += 2;
        php_skip_ws(st);
        char rhs[PHP_VAL_MAX];
        php_eval_expr(st, rhs, sizeof rhs);
        strncpy(out, cond[0] ? cond : rhs, outcap - 1); out[outcap - 1] = 0;
        g_expr_depth--;
        return;
    }
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

/* Scans st->src forward (WITHOUT executing/consuming anything but the
 * scan itself) to the next "case"/"default" keyword or the closing '}',
 * all at the switch body's OWN nesting depth (0, relative to st->src's
 * starting position, which must already be just inside the switch's own
 * "{") -- strings/comments/nested "{...}" blocks are skipped as opaque
 * units (php_skip_atomic + brace-depth tracking), the same convention
 * php_skip_to_brace_close() above already uses, so a nested if/while/
 * function-body/array-literal's own braces (or a string literal
 * containing "case"-looking text) never get mistaken for this switch's
 * own structure. Used by the "switch" statement handler's own label-
 * finding scan -- see its own top comment for the full two-pass design.
 * Leaves st->src pointing AT the found keyword's first character, or AT
 * '}', or at a NUL byte if the source ran out first (malformed input:
 * every caller here already treats that as "nothing more to find",
 * degrading safely rather than looping). */
static void php_switch_skip_to_label_or_close(PhpState *st) {
    const char *p = st->src;
    int depth = 0;
    for (;;) {
        char c = *p;
        if (!c) { st->src = p; return; }
        if (depth == 0 && c == '}') { st->src = p; return; }
        const char *after = php_skip_atomic(p);
        if (after != p) { p = after; continue; }
        if (c == '{') { depth++; p++; continue; }
        if (c == '}') { depth--; p++; continue; }
        if (depth == 0 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
            const char *idstart = p;
            while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) p++;
            int len = (int)(p - idstart);
            if ((len == 4 && strncmp(idstart, "case", 4) == 0) || (len == 7 && strncmp(idstart, "default", 7) == 0)) {
                st->src = idstart;
                return;
            }
            continue; /* some other identifier (a real statement's own text) -- keep scanning */
        }
        p++;
    }
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
        /* Visibility/modifier keywords -- mostly consumed, not modeled
         * (no access-control enforcement in this subset); "static" is the
         * one exception, tracked via `saw_static` below so the "$name"
         * branch just past this loop can route a "static $x;" property
         * into cls->static_prop_* (class-level, shared) instead of the
         * ordinary per-instance prop_names/prop_defaults -- see
         * PhpClass.static_prop_names' own comment for why that
         * distinction matters. */
        int saw_static = 0;
        for (;;) {
            const char *save2 = st->src;
            char mkw[16];
            php_read_ident(st, mkw, sizeof mkw);
            if (strcmp(mkw, "public") == 0 || strcmp(mkw, "private") == 0 || strcmp(mkw, "protected") == 0 ||
                strcmp(mkw, "static") == 0 || strcmp(mkw, "final") == 0 || strcmp(mkw, "abstract") == 0 ||
                strcmp(mkw, "readonly") == 0) {
                if (strcmp(mkw, "static") == 0) saw_static = 1;
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
            if (saw_static && cls && cls->nstatic_props < PHP_CLASS_PROP_MAX) {
                strncpy(cls->static_prop_names[cls->nstatic_props], pname, sizeof cls->static_prop_names[cls->nstatic_props] - 1);
                cls->static_prop_names[cls->nstatic_props][sizeof cls->static_prop_names[cls->nstatic_props] - 1] = 0;
                strncpy(cls->static_prop_vals[cls->nstatic_props], defval, sizeof cls->static_prop_vals[cls->nstatic_props] - 1);
                cls->static_prop_vals[cls->nstatic_props][sizeof cls->static_prop_vals[cls->nstatic_props] - 1] = 0;
                cls->nstatic_props++;
            } else if (cls && cls->nprops < PHP_CLASS_PROP_MAX) {
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
                    /* "...$name" variadic marker -- same detection
                     * php_run_statement's own top-level "function"
                     * handler already uses for PhpFunc.variadic, now
                     * mirrored here for PhpMethod.variadic (see that
                     * field's own comment on the real bug this fixes). */
                    int is_variadic = (c == '$' && st->src[-1] == '.' && st->src[-2] == '.' && st->src[-3] == '.');
                    if (c == '$') {
                        st->src++;
                        char pname[64];
                        php_read_ident(st, pname, sizeof pname);
                        int this_param_slot = -1;
                        if (m && nparams < PHP_FUNC_PARAM_MAX) {
                            strncpy(m->params[nparams], pname, sizeof m->params[nparams] - 1);
                            m->params[nparams][sizeof m->params[nparams] - 1] = 0;
                            m->variadic[nparams] = is_variadic;
                            m->param_defaults[nparams][0] = 0;
                            this_param_slot = nparams;
                            nparams++;
                        }
                        php_skip_ws(st);
                        c = *st->src;
                        /* "= default" -- see PhpMethod.param_defaults' own
                           comment (PhpFunc's identical field) for the real
                           bug storing this, instead of discarding it,
                           fixes. */
                        if (c == '=') {
                            st->src++;
                            if (this_param_slot >= 0) {
                                php_eval_expr(st, m->param_defaults[this_param_slot], sizeof m->param_defaults[this_param_slot]);
                            } else {
                                char dummy[PHP_VAL_MAX];
                                php_eval_expr(st, dummy, sizeof dummy);
                            }
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
static void php_enter_html_passthrough(PhpState *st); /* forward: switch's own PASS 2 dispatch loop needs this too */
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
    {
        /* "self::$prop = EXPR;" / "ClassName::$prop = EXPR;" -- a static-
         * property ASSIGNMENT statement, the write-side counterpart of
         * the "ClassName::$prop" READ handled in php_eval_factor's own
         * "::" branch (see that comment, and PhpClass.static_prop_names'
         * own comment, for the real WordPress bug this fixes:
         * WP_User::__construct()'s "self::$back_compat_keys = array(...);"
         * cache-populate line). Without this, the statement fell through
         * to the generic bare-expression-statement fallback at the
         * bottom of this function, which (via php_eval_factor's "::"
         * read path) evaluated "self::" to nothing and left "$prop =
         * ...;" sitting unconsumed to be mis-parsed as a SEPARATE
         * statement assigning a same-named LOCAL variable instead --
         * silently discarding the class-level write entirely. Only
         * simple "= EXPR" is handled (matches every real static-property
         * write in the vendored tree found so far); compound "+="/".="
         * etc. on a static property is not (a further, narrower,
         * documented gap, same shape as this file's other compound-
         * operator support being $-variable-only). */
        const char *psave = st->src;
        php_skip_ws(st);
        if (*st->src == ':' && st->src[1] == ':' && st->src[2] == '$') {
            st->src += 3;
            char sname4[64];
            php_read_ident(st, sname4, sizeof sname4);
            php_skip_ws(st);
            if (*st->src == '=' && st->src[1] != '=') {
                st->src++;
                char rhs4[PHP_VAL_MAX];
                php_eval_expr(st, rhs4, sizeof rhs4);
                php_skip_ws(st);
                if (*st->src == ';') st->src++;
                const char *resolved4 = kw;
                PhpClass *self_cls4 = NULL;
                if ((strcmp(kw, "self") == 0 || strcmp(kw, "static") == 0 || strcmp(kw, "parent") == 0) && st->has_this && st->this_obj_id >= 0) {
                    self_cls4 = php_class_find(g_objects[st->this_obj_id].class_name);
                    if (strcmp(kw, "parent") == 0 && self_cls4) self_cls4 = self_cls4->parent_name[0] ? php_class_find(self_cls4->parent_name) : NULL;
                    resolved4 = self_cls4 ? self_cls4->name : kw;
                }
                PhpClass *cls4 = self_cls4 ? self_cls4 : php_class_find(resolved4);
                char *slot4 = php_class_find_static_prop(cls4, sname4);
                if (slot4) { strncpy(slot4, rhs4, PHP_VAL_MAX - 1); slot4[PHP_VAL_MAX - 1] = 0; }
                return;
            }
        }
        st->src = psave;
    }
    if (strcmp(kw, "list") == 0) {
        /* "list($a, $b, ...) = EXPR;" -- real PHP array-destructuring
         * assignment, positionally unpacking EXPR's own array values
         * ("0", "1", "2", ... keys) into each named target in order. A
         * target can be a bare "$name", or (the shape that surfaced
         * this gap) "$this->member" -- reuses the SAME
         * php_resolve_lvalue_chain() this file's ordinary "$x[k]=.."/
         * "$this->prop=.." assignment statements already use, so
         * whatever lvalue shapes THAT supports, this does too. An empty
         * slot ("list($a, , $c) = ...", skipping an element) is
         * accepted and simply ignored, matching real PHP. Previously
         * entirely UNIMPLEMENTED -- "list" isn't a reserved word this
         * file's parser recognized at all, so "list(...) = EXPR;" fell
         * through to the generic bare-expression-statement fallback,
         * which parsed "list(...)" as an ORDINARY FUNCTION CALL (name
         * "list", unrecognized -> silently degrades to "", per this
         * file's own "unknown function" convention) and then left the
         * trailing "= EXPR;" as leftover text to be independently (and
         * wrongly) re-parsed as some UNRELATED next statement. Confirmed
         * as a real, high-impact bug this session: real WordPress's own
         * _WP_Dependency::__construct() -- the class backing EVERY
         * registered script/style handle, core to wp_enqueue_style()/
         * wp_enqueue_script() -- is exactly "list($this->handle,
         * $this->src, $this->deps, $this->ver, $this->args) = $args;"
         * (itself fed by a "...$args" variadic constructor param) --
         * with list() a no-op, EVERY SINGLE registered style/script
         * object silently ended up with all-empty properties (a real,
         * completely empty ->src in particular), so nothing ever
         * printed a real <link>/<script> tag on any page, including
         * wp-login.php's own missing CSS that first surfaced this. */
        php_skip_ws(st);
        if (*st->src == '(') st->src++;
        php_skip_ws(st);
        int n_targets = 0;
        int is_plain[PHP_LIST_MAX];
        char plain_name[PHP_LIST_MAX][64];
        PhpKVArray *containers[PHP_LIST_MAX];
        char keys[PHP_LIST_MAX][128];
        for (;;) {
            php_skip_ws(st);
            char cc = *st->src;
            if (cc == ')' || cc == 0) break;
            if (cc == ',') {
                /* Empty slot -- record as "nothing to assign" so
                 * positional indexing of LATER targets stays correct. */
                if (n_targets < PHP_LIST_MAX) { is_plain[n_targets] = -1; containers[n_targets] = NULL; n_targets++; }
                st->src++;
                continue;
            }
            if (cc == '$') {
                st->src++;
                char tname[64];
                php_read_ident(st, tname, sizeof tname);
                php_skip_ws(st);
                char cc2 = *st->src;
                if (cc2 == '[' || (cc2 == '-' && st->src[1] == '>')) {
                    PhpKVArray *container = NULL;
                    char key[128];
                    if (php_resolve_lvalue_chain(st, tname, cc2, &container, key, sizeof key) && n_targets < PHP_LIST_MAX) {
                        is_plain[n_targets] = 0;
                        containers[n_targets] = container;
                        strncpy(keys[n_targets], key, sizeof keys[n_targets] - 1); keys[n_targets][sizeof keys[n_targets] - 1] = 0;
                        n_targets++;
                    }
                } else if (n_targets < PHP_LIST_MAX) {
                    is_plain[n_targets] = 1;
                    strncpy(plain_name[n_targets], tname, sizeof plain_name[n_targets] - 1); plain_name[n_targets][sizeof plain_name[n_targets] - 1] = 0;
                    n_targets++;
                }
            }
            php_skip_ws(st);
            cc = *st->src;
            if (cc == ',') { st->src++; continue; }
            break;
        }
        php_skip_ws(st);
        if (*st->src == ')') st->src++;
        php_skip_ws(st);
        if (*st->src == '=' && st->src[1] != '=') st->src++;
        php_skip_ws(st);
        char rhs[PHP_VAL_MAX];
        php_eval_expr(st, rhs, sizeof rhs);
        php_skip_ws(st);
        if (*st->src == ';') st->src++;
        int aid = php_arrref_decode(rhs);
        if (aid >= 0) {
            int i;
            for (i = 0; i < n_targets; i++) {
                if (is_plain[i] < 0) continue; /* empty slot */
                char ikey[16]; snprintf(ikey, sizeof ikey, "%d", i);
                char val[PHP_VAL_MAX];
                if (!php_kv_has(&g_arrays[aid], ikey, val, sizeof val)) val[0] = 0;
                if (is_plain[i]) php_var_set(st, plain_name[i], val);
                else if (containers[i]) php_kv_set(containers[i], keys[i], val);
            }
        }
        return;
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
    if (strcmp(kw, "die") == 0 || strcmp(kw, "exit") == 0) {
        /* Real PHP: "die"/"exit" are language constructs (exact
         * synonyms), not functions -- optionally parenthesized, with an
         * optional single argument: a STRING argument is printed before
         * halting (die($message)), an INTEGER argument is a bare exit
         * code and prints nothing (exit($status)) -- this subset has no
         * real int/string type distinction, so a purely-decimal value is
         * treated as a status code (nothing printed), matching the
         * overwhelmingly common real usage (wp_die()'s own chain, and
         * every "already installed"/"requirements not met"-shaped early
         * guard in wp-admin/install.php, all pass a real HTML/message
         * string). See g_exiting's own comment for why this can't just
         * set st->returning like an ordinary function-local "return". */
        php_skip_ws(st);
        int had_parens = (*st->src == '(');
        if (had_parens) {
            st->src++;
            php_skip_ws(st);
        }
        char msg[PHP_VAL_MAX];
        msg[0] = 0;
        if (had_parens && *st->src != ')') {
            php_eval_expr(st, msg, sizeof msg);
            php_skip_ws(st);
        }
        if (had_parens && *st->src == ')') st->src++;
        php_skip_ws(st);
        if (*st->src == ';') st->src++;
        if (msg[0]) {
            int is_status_code = 1;
            const char *p = msg;
            if (*p == '-' || *p == '+') p++;
            if (!*p) is_status_code = 0;
            for (; *p; p++) { if (*p < '0' || *p > '9') { is_status_code = 0; break; } }
            if (!is_status_code) php_emit(st, msg, (int)strlen(msg));
        }
        g_exiting = 1;
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
                    int this_param_slot = -1;
                    if (fn && nparams < PHP_FUNC_PARAM_MAX) {
                        strncpy(fn->params[nparams], pname, sizeof fn->params[nparams] - 1);
                        fn->params[nparams][sizeof fn->params[nparams] - 1] = 0;
                        fn->variadic[nparams] = is_variadic;
                        fn->param_defaults[nparams][0] = 0;
                        this_param_slot = nparams;
                        nparams++;
                    }
                    php_skip_ws(st);
                    c = *st->src;
                    /* "= default" -- see PhpFunc.param_defaults' own
                       comment for the real bug storing this (instead of
                       throwing it away, the old behavior) fixes. */
                    if (c == '=') {
                        st->src++;
                        if (this_param_slot >= 0) {
                            php_eval_expr(st, fn->param_defaults[this_param_slot], sizeof fn->param_defaults[this_param_slot]);
                        } else {
                            char dummy[PHP_VAL_MAX];
                            php_eval_expr(st, dummy, sizeof dummy);
                        }
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
        if (getenv("SQS_TRACE_INCLUDE")) fprintf(stderr, "[INCLUDE %s] %s\n", kw, path);
        if (once && php_was_included(path)) return;
        FILE *f = fopen(path, "rb");
        if (!f) { if (getenv("SQS_TRACE_INCLUDE")) fprintf(stderr, "[INCLUDE FAILED] %s\n", path); return; } /* silently skip on failure -- see this file's
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
                    if (st->returning || st->breaking || st->continuing || g_exiting) {
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
                    if (st->returning || st->breaking || st->continuing || g_exiting) return;
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
                    if (st->returning || st->breaking || st->continuing || g_exiting) return;
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
                        if (st->returning || st->breaking || st->continuing || g_exiting) {
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
                        if (st->returning || st->breaking || st->continuing || g_exiting) return;
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
                        if (st->returning || st->breaking || st->continuing || g_exiting) return;
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
            if (st->returning || g_exiting) {
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
                if (st->returning || g_exiting) return;
            } else {
                php_run_statements(st);
                if (st->returning || g_exiting) {
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
                if (st->returning || g_exiting) return;
            } else {
                php_run_statements(st);
                if (st->returning || g_exiting) {
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
        /* A bare, single-statement body ("foreach ($a as $v) echo $v;",
         * no braces) was previously simply UNHANDLED: this whole
         * function returned immediately without consuming even the one
         * statement that follows, silently skipping both the loop AND
         * whatever real code came after it (left for php_run_statements'
         * own stuck-guard to eat one character at a time). Found this
         * session via a repro exercising a bare foreach body for
         * apparently the first time in this project's whole testing
         * history -- if/while's own bare-body support (see their own
         * comments) was fixed long ago, foreach's never was. Mirrors
         * that same "execute exactly one real statement via
         * php_run_statement() when iterating, php_skip_one_statement()
         * to consume it inertly once nothing (or nothing more) needs to
         * run" pattern. */
        int braced = (c == '{');
        if (!braced && !alt) {
            /* bare: body_start is the statement itself, nothing to skip past first */
        } else {
            st->src++;
        }
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
                    if (st->returning || g_exiting) return;
                } else if (braced) {
                    php_run_statements(st);
                    if (st->returning || g_exiting) {
                        st->src = body_start; php_skip_to_brace_close(st);
                        if (*st->src == '}') st->src++;
                        return;
                    }
                } else {
                    php_run_statement(st);
                    if (st->returning || g_exiting) return;
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
        if (!braced) { php_skip_one_statement(st); return; }
        php_skip_to_brace_close(st);
        c = *st->src;
        if (c == '}') st->src++;
        return;
    }
    if (strcmp(kw, "switch") == 0) {
        /* Real switch/case/default support -- found completely missing
           this session (fell into the generic "class/try/switch/etc ->
           skip the whole braced body as an inert, never-executed block"
           list below, the same fate this file's own top comment already
           documented since session 1). Confirmed as a genuinely high-
           impact gap, not just a theoretical one: WP_User::get_data_by()
           -- itself deep in the mutually-recursive user-bootstrap call
           chain (_wp_get_current_user() -> wp_set_current_user() ->
           setup_userdata() -> get_current_user_id() -> ...) this session
           was investigating -- uses a switch($field){...} specifically
           to short-circuit via wp_cache_get() BEFORE ever reaching a real
           database query; with switch a no-op, $user_id/$db_field never
           got set, so every call fell through to a real, uncached
           $wpdb->get_row() query -- helping explain both the excessive
           real work AND (via cache misses feeding back into more of the
           same recursive chain) some of the recursion depth this session
           was chasing.

           No AST exists in this file (see its own top comment), so this
           works in two passes over the SAME raw source text, exactly
           like do/while/for/foreach's own re-parse-from-a-saved-position
           style: PASS 1 scans (WITHOUT executing anything) through the
           switch body, real-evaluating each "case EXPR:" expression in
           source order (matching real PHP's own "checked in order until
           the first match" semantics, and its own "case bodies with a
           side effect only run on/after a real match" rule) via the
           SAME strcmp()-based equality this file's own "=="/"===" share
           (see php_eval_cmp's own comment on why that's correct here,
           not merely simplified), to find either the first matching case
           or, failing that, a "default:" label -- remembering ONLY the
           byte position immediately after its ":" and its default-vs-
           case identity, never anything about the STATEMENTS in between
           each label (php_switch_skip_to_label_or_close() skips those
           opaquely, brace/string/comment-aware, the same way every other
           body-skip in this file already does). If nothing matched (no
           case, no default), the whole switch is correctly a no-op, same
           as real PHP. PASS 2 then re-enters from that ONE found
           position and runs REAL statements (php_run_statement(), so a
           nested if/while/switch/etc fully recurses and consumes its own
           terminator exactly like every other body-skip/run pair in this
           file already relies on) until hitting "break;" (stops, consumed
           and cleared), "continue;" (also stops the switch -- real PHP's
           own post-7.3 behavior: bare "continue" inside a switch acts
           like "break" for the switch itself, not the same case's own
           label), "return"/an OUTER loop's own "break"/"continue" signal
           (propagates up unconsumed, exactly like every OTHER body-
           runner in this file), the switch's closing '}', OR another
           "case"/"default" label at this body's own depth -- reaching a
           label without an intervening break is real PHP's own
           documented FALL-THROUGH behavior: the label itself is
           consumed (its expression, if a "case", is re-evaluated only to
           stay in sync with the source position -- a case label with a
           genuine side effect in its own expression is exotic, arguably
           malformed real PHP to begin with) and execution just continues
           into the next section's statements without re-matching
           anything. */
        php_skip_ws(st);
        c = *st->src;
        char switch_val[PHP_VAL_MAX]; switch_val[0] = 0;
        if (c == '(') {
            st->src++;
            php_eval_expr(st, switch_val, sizeof switch_val);
            php_skip_ws(st);
            if (*st->src != ')') php_skip_to_paren_close(st);
            if (*st->src == ')') st->src++;
        }
        php_skip_ws(st);
        c = *st->src;
        if (c != '{') return; /* malformed: degrade safely, same as every other handler here */
        st->src++;
        const char *body_start = st->src;

        const char *match_pos = NULL;
        const char *default_pos = NULL;
        for (;;) {
            php_switch_skip_to_label_or_close(st);
            char lc = *st->src;
            if (!lc || lc == '}') break;
            char kw2[8];
            php_read_ident(st, kw2, sizeof kw2);
            if (strcmp(kw2, "case") == 0) {
                php_skip_ws(st);
                char caseval[PHP_VAL_MAX];
                php_eval_expr(st, caseval, sizeof caseval);
                php_skip_ws(st);
                if (*st->src == ':') st->src++;
                else if (*st->src == ';') st->src++;
                if (!match_pos && strcmp(switch_val, caseval) == 0) { match_pos = st->src; break; }
            } else {
                php_skip_ws(st);
                if (*st->src == ':') st->src++;
                if (!default_pos) default_pos = st->src;
            }
        }
        const char *run_from = match_pos ? match_pos : default_pos;
        if (!run_from) {
            st->src = body_start;
            php_skip_to_brace_close(st);
            if (*st->src == '}') st->src++;
            return;
        }
        st->src = run_from;
        for (;;) {
            php_skip_ws(st);
            char c0 = st->src[0], c1 = st->src[1];
            if (!c0) break;
            if (c0 == '?' && c1 == '>') {
                st->src += 2;
                php_enter_html_passthrough(st);
                if (!*st->src) break;
                continue;
            }
            if (c0 == '}') break;
            if ((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z') || c0 == '_') {
                const char *save = st->src;
                char kw2[8];
                php_read_ident(st, kw2, sizeof kw2);
                if (strcmp(kw2, "case") == 0) {
                    php_skip_ws(st);
                    char dummy[PHP_VAL_MAX];
                    php_eval_expr(st, dummy, sizeof dummy);
                    php_skip_ws(st);
                    if (*st->src == ':') st->src++;
                    else if (*st->src == ';') st->src++;
                    continue;
                } else if (strcmp(kw2, "default") == 0) {
                    php_skip_ws(st);
                    if (*st->src == ':') st->src++;
                    continue;
                }
                st->src = save;
            }
            const char *before = st->src;
            php_run_statement(st);
            if (st->returning || g_exiting) {
                st->src = body_start;
                php_skip_to_brace_close(st);
                if (*st->src == '}') st->src++;
                return;
            }
            if (st->breaking) { st->breaking = 0; break; }
            if (st->continuing) { st->continuing = 0; break; }
            if (st->src == before) st->src++;
        }
        st->src = body_start;
        php_skip_to_brace_close(st);
        if (*st->src == '}') st->src++;
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
        strcmp(kw, "do") == 0 || /* "switch" removed -- see its own real handler, much earlier in this dispatch chain, same as "do"'s own real handler already made this entry dead for that keyword */
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
        if (st->returning || st->breaking || st->continuing || g_exiting) return;
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
        if (st->returning || st->breaking || st->continuing || g_exiting) return -1;
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

static int regex_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Reads ONE character-class MEMBER at *pp (a plain byte, or a backslash
 * escape -- "\xHH" for a hex-coded byte, or "\C" for any other char C,
 * meaning "this literal character" -- the universal regex convention for
 * escaping a class-special character like "]"/"["/"-"/"\" itself inside
 * a "[...]"), advancing *pp past whatever it consumed. Used for both a
 * class member's own value AND (recursively, via the SAME call site
 * used twice) a range's end value, so "[\x80-\xff]"/"[\]\[]"/etc all
 * resolve correctly instead of only a bare, unescaped byte doing so. */
static unsigned char regex_cls_read_char(const char **pp) {
    const char *p = *pp;
    if (*p == '\\' && p[1]) {
        if ((p[1] == 'x' || p[1] == 'X') && regex_hex_digit(p[2]) >= 0 && regex_hex_digit(p[3]) >= 0) {
            unsigned char v = (unsigned char)((regex_hex_digit(p[2]) << 4) | regex_hex_digit(p[3]));
            *pp = p + 4;
            return v;
        }
        *pp = p + 2;
        return (unsigned char)p[1];
    }
    *pp = p + 1;
    return (unsigned char)*p;
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
            /* "p[0] == '\\'" in the loop condition (checked BEFORE the
             * plain "*p != ']'" terminator check) is what makes an
             * escaped "\]" a real class MEMBER instead of prematurely
             * closing the class right there -- see
             * regex_cls_read_char()'s own comment for why a class member
             * needs its own escape-aware reader at all (this was
             * confirmed as a real bug this session: WordPress's own
             * esc_url() regex, "[^...\[\]\x80-\xff]", closed at the
             * FIRST "\]" instead of the real closing "]" near the end,
             * desyncing the rest of the pattern and the compiled class
             * matching nothing useful -- silently reducing every real
             * URL passed through it to ""). */
            while (*p && (p[0] == '\\' || *p != ']' || first)) {
                first = 0;
                unsigned char c0 = regex_cls_read_char(&p);
                if (*p == '-' && p[1] && p[1] != ']') {
                    /* Only treat "-" as a range operator when something
                     * real follows it (not immediately the closing "]",
                     * which real regex syntax treats as a literal
                     * trailing hyphen instead -- e.g. "[a-z-]"). */
                    const char *after_dash = p + 1;
                    unsigned char c1 = regex_cls_read_char(&after_dash);
                    if (c1 >= c0) { regex_cls_add_range(&t, c0, c1); p = after_dash; }
                    else regex_cls_add_range(&t, c0, c0);
                } else {
                    regex_cls_add_range(&t, c0, c0);
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
 * a statement string handed to sqdb_exec()/sqdb_query() -- doubles `'`,
 * REAL SQL's own (and real SQLite's own) only string-literal escape
 * convention. Used to double `\` too (matching the OLD flat-file
 * engine's own hand-rolled sqdb_value() parser, which treated a
 * backslash as ITS OWN escape-sequence lead-in, e.g. "\'" ambiguity
 * right before the closing quote) -- but db_engine.c is now real SQLite
 * (see its own top comment), which has no backslash-escape convention
 * at all: a literal backslash in a SQL string literal is just a literal
 * backslash. Doubling it here now would have silently corrupted any
 * stored value containing a real backslash (storing "\\\\" for an
 * original "\\") the moment the engine swap landed -- fixed by dropping
 * the backslash-doubling, matching real SQL semantics. `out` must be at
 * least 2*strlen(in)+3 bytes. */
static void php_db_sql_quote(const char *in, char *out, int outcap) {
    int o = 0;
    const char *p;
    if (o < outcap - 1) out[o++] = '\'';
    for (p = in; *p && o < outcap - 3; p++) {
        if (*p == '\'') out[o++] = '\'';
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
 * touch st->get/st->post/st->cookie -- those are a shallow, shared-
 * ownership copy of the CALLER's (or, at the top level, php_run()'s own
 * caller-supplied) get/post/cookie arrays (see "callee->get =
 * caller->get;" below),
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
/* compact()'s own per-name helper: looks `varname` up in the CALLING
 * scope (`caller`) and, if found, sets result-array `id`'s own
 * `varname` key to that variable's current value -- a fresh, real copy
 * of its own array content (not just sharing the same g_arrays[] slot)
 * when the variable itself holds a native array, matching real PHP's
 * "compact() copies, doesn't alias" value semantics; a plain scalar
 * (or a snapshotted/parameter array already living behind an arrref
 * token in ->val) is copied through as-is, same as everywhere else in
 * this file. A name with no such variable in scope is silently skipped
 * -- real PHP's own compact() behavior for a missing name. */
static void php_compact_add(PhpState *caller, int id, const char *varname) {
    PhpVar *v = php_var_find(caller, varname);
    if (!v) return;
    if (v->is_array) {
        int nid = php_array_new();
        if (nid < 0) return;
        int j;
        for (j = 0; j < v->arr.count; j++) php_kv_set(&g_arrays[nid], v->arr.items[j].key, v->arr.items[j].val);
        char enc[32]; php_arrref_encode(nid, enc, sizeof enc);
        php_kv_set(&g_arrays[id], varname, enc);
    } else {
        php_kv_set(&g_arrays[id], varname, v->val);
    }
}

/* Real PHP serialize()/unserialize() -- previously entirely UNIMPLEMENTED
 * (fell through to this file's generic "unknown function -> ''"
 * degrade). A real, pervasive gap: maybe_serialize()/maybe_unserialize()
 * (wp-includes/functions.php) are what EVERY options/postmeta/usermeta
 * row holding a non-scalar (array or object) value goes through --
 * confirmed as the direct cause of WP_User_Meta_Session_Tokens (backing
 * "stay logged in across requests") silently losing its own session
 * list, since update_user_meta()'s own array-valued "session_tokens"
 * entry was being maybe_serialize()'d into "" and read back as nothing.
 * This subset has no real variable TYPE tracking (every value is just a
 * string, or a magic-prefixed token for an array/object reference -- see
 * php_arrref_encode/php_objref_encode's own comments) so a scalar's
 * serialized TYPE tag (i:/d:/s:/b:) is inferred from its own text shape
 * (an integer-looking string serializes as i:N;, etc) rather than a real
 * preserved type -- documented, matches this file's usual "good enough
 * for the real shapes WordPress actually stores, not a from-scratch PHP
 * VM" scope limit. Recursive, depth-capped at 32 (same "degrade safely
 * on pathological/adversarial input rather than blow the native stack"
 * convention every other recursive structure in this file already
 * follows -- see php_array_deep_copy's own comment). */
static void php_serialize_append(char *out, int *pos, int cap, const char *s) {
    int n = (int)strlen(s);
    if (*pos >= cap) return;
    if (n > cap - *pos) n = cap - *pos;
    memcpy(out + *pos, s, (size_t)n);
    *pos += n;
    if (*pos < cap) out[*pos] = 0;
}
static void php_serialize_value(const char *val, char *out, int *pos, int cap, int depth) {
    if (depth > 32) { php_serialize_append(out, pos, cap, "N;"); return; }
    int oid = php_objref_decode(val);
    int aid = php_arrref_decode(val);
    if (oid >= 0) {
        PhpObject *o = &g_objects[oid];
        char head[160];
        snprintf(head, sizeof head, "O:%d:\"%s\":%d:{", (int)strlen(o->class_name), o->class_name, o->props.count);
        php_serialize_append(out, pos, cap, head);
        int i;
        for (i = 0; i < o->props.count; i++) {
            char kh[160];
            snprintf(kh, sizeof kh, "s:%d:\"%s\";", (int)strlen(o->props.items[i].key), o->props.items[i].key);
            php_serialize_append(out, pos, cap, kh);
            php_serialize_value(o->props.items[i].val, out, pos, cap, depth + 1);
        }
        php_serialize_append(out, pos, cap, "}");
    } else if (aid >= 0) {
        PhpKVArray *a = &g_arrays[aid];
        char head[32];
        snprintf(head, sizeof head, "a:%d:{", a->count);
        php_serialize_append(out, pos, cap, head);
        int i;
        for (i = 0; i < a->count; i++) {
            const char *k = a->items[i].key;
            char *endp;
            long kn = strtol(k, &endp, 10);
            if (k[0] && *endp == 0 && !(k[0] == '0' && k[1] != 0)) {
                char kh[32]; snprintf(kh, sizeof kh, "i:%ld;", kn);
                php_serialize_append(out, pos, cap, kh);
            } else {
                char kh[160]; snprintf(kh, sizeof kh, "s:%d:\"%s\";", (int)strlen(k), k);
                php_serialize_append(out, pos, cap, kh);
            }
            php_serialize_value(a->items[i].val, out, pos, cap, depth + 1);
        }
        php_serialize_append(out, pos, cap, "}");
    } else {
        /* Scalar -- infer a type tag from the string's own shape (see
         * this whole helper's own top comment on why). */
        if (!val[0]) {
            php_serialize_append(out, pos, cap, "s:0:\"\";");
            return;
        }
        const char *p = val;
        if (*p == '-') p++;
        int all_digit = *p != 0;
        const char *pp = p;
        for (; *pp; pp++) if (*pp < '0' || *pp > '9') { all_digit = 0; break; }
        int clean_int = all_digit && !(p[0] == '0' && p[1] != 0);
        if (clean_int) {
            char t[160]; snprintf(t, sizeof t, "i:%s;", val);
            php_serialize_append(out, pos, cap, t);
            return;
        }
        char *fendp;
        double fv = strtod(val, &fendp);
        if (fendp != val && *fendp == 0 && strchr(val, '.')) {
            char t[192]; snprintf(t, sizeof t, "d:%.17g;", fv);
            php_serialize_append(out, pos, cap, t);
            return;
        }
        char t[64]; snprintf(t, sizeof t, "s:%d:\"", (int)strlen(val));
        php_serialize_append(out, pos, cap, t);
        php_serialize_append(out, pos, cap, val);
        php_serialize_append(out, pos, cap, "\";");
    }
}
/* Parses exactly one serialized value starting at `s`, writes this
 * subset's own value representation (a scalar string, or a fresh
 * arrref-/objref-encoded token) into `out`, and returns a pointer to the
 * character immediately after the parsed value (so a caller walking an
 * array/object body can keep calling this for each key/value pair in
 * turn) -- or NULL on malformed input (this file's usual "degrade
 * safely" response to bad data, not a crash). */
static const char *php_unserialize_value(const char *s, char *out, int outcap, int depth) {
    if (!s || !*s || depth > 32) { out[0] = 0; return NULL; }
    char tag = s[0];
    if (tag == 'N' && s[1] == ';') { out[0] = 0; return s + 2; }
    if (tag == 'b' && s[1] == ':') {
        int v = s[2] == '1';
        strncpy(out, v ? "1" : "", outcap - 1); out[outcap - 1] = 0;
        const char *semi = strchr(s + 2, ';');
        return semi ? semi + 1 : NULL;
    }
    if ((tag == 'i' || tag == 'd') && s[1] == ':') {
        const char *semi = strchr(s + 2, ';');
        if (!semi) return NULL;
        int n = (int)(semi - (s + 2)); if (n >= outcap) n = outcap - 1;
        memcpy(out, s + 2, (size_t)n); out[n] = 0;
        return semi + 1;
    }
    if (tag == 's' && s[1] == ':') {
        int len = atoi(s + 2);
        const char *q = strchr(s + 2, '"');
        if (!q) return NULL;
        q++;
        if (len < 0) len = 0;
        int n = len; if (n >= outcap) n = outcap - 1;
        memcpy(out, q, (size_t)n); out[n] = 0;
        const char *after = q + len;
        if (after[0] == '"') after++;
        if (after[0] == ';') after++;
        return after;
    }
    if (tag == 'a' && s[1] == ':') {
        int count = atoi(s + 2);
        const char *brace = strchr(s + 2, '{');
        if (!brace) return NULL;
        const char *p = brace + 1;
        int aid = php_array_new();
        int i;
        for (i = 0; i < count && p; i++) {
            char key[128];
            p = php_unserialize_value(p, key, sizeof key, depth + 1);
            if (!p) break;
            char val[PHP_VAL_MAX];
            p = php_unserialize_value(p, val, sizeof val, depth + 1);
            if (!p) break;
            if (aid >= 0) php_kv_set(&g_arrays[aid], key, val);
        }
        if (p && *p == '}') p++;
        if (aid >= 0) php_arrref_encode(aid, out, outcap); else out[0] = 0;
        return p;
    }
    if (tag == 'O' && s[1] == ':') {
        int nlen = atoi(s + 2);
        const char *q = strchr(s + 2, '"');
        if (!q) return NULL;
        q++;
        char cname[64];
        int n = nlen; if (n >= (int)sizeof cname) n = (int)sizeof cname - 1;
        memcpy(cname, q, (size_t)n); cname[n] = 0;
        const char *after_name = q + nlen;
        if (after_name[0] == '"') after_name++;
        const char *colon2 = strchr(after_name, ':');
        if (!colon2) return NULL;
        int count = atoi(colon2 + 1);
        const char *brace = strchr(colon2, '{');
        if (!brace) return NULL;
        const char *p = brace + 1;
        int oid = php_object_new(cname);
        int i;
        for (i = 0; i < count && p; i++) {
            char key[128];
            p = php_unserialize_value(p, key, sizeof key, depth + 1);
            if (!p) break;
            char val[PHP_VAL_MAX];
            p = php_unserialize_value(p, val, sizeof val, depth + 1);
            if (!p) break;
            if (oid >= 0) php_kv_set(&g_objects[oid].props, key, val);
        }
        if (p && *p == '}') p++;
        if (oid >= 0) php_objref_encode(oid, out, outcap); else out[0] = 0;
        return p;
    }
    out[0] = 0;
    return NULL;
}

static void php_call_function(PhpState *caller, const char *name, char **args, int nargs, char *out, int outcap) {
    if (g_suppress_calls > 0) { out[0] = 0; return; } /* short-circuited "&&"/"||" -- see g_suppress_calls's own comment */
    if (strcmp(name, "serialize") == 0 && nargs >= 1) {
        int pos = 0;
        out[0] = 0;
        php_serialize_value(args[0], out, &pos, outcap, 0);
        return;
    }
    if (strcmp(name, "unserialize") == 0 && nargs >= 1) {
        php_unserialize_value(args[0], out, outcap, 0);
        return;
    }
    if (strcmp(name, "is_serialized") == 0 && nargs >= 1) {
        const char *v = args[0];
        int ok = 0;
        if (v[0] && v[1] == ':') {
            char t = v[0];
            if (t == 'N' || t == 'b' || t == 'i' || t == 'd' || t == 's' || t == 'a' || t == 'O') ok = 1;
        } else if (strcmp(v, "N;") == 0) ok = 1;
        strncpy(out, ok ? "1" : "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "header") == 0 && nargs >= 1) {
        /* See PhpState.headers_buf's own comment. Appends the raw header
         * line as-is (real PHP's own header() takes the literal
         * "Name: value" string) -- sqs_main.c is the one place that
         * actually turns this into a real HTTP response header, once
         * php_run() hands the accumulated buffer back to it. Real PHP's
         * header() has ONE special case: a literal status-line string
         * ("HTTP/1.0 302 Found", no "Name:" at all -- exactly what
         * wp_redirect()'s own internals send) changes the RESPONSE
         * STATUS LINE itself, not an ordinary header -- forwarding it
         * verbatim as if it were a normal header line produced a real,
         * malformed HTTP response (a header with no colon, embedding a
         * second bogus "HTTP/1.0 ..." status line into the MIDDLE of the
         * header block), which curl (and any real browser) rejects
         * outright. This subset already infers a real 302 from the
         * presence of a "Location:" header (see sqs_send_response_ex's
         * own comment in sqs_main.c) -- which wp_redirect() always also
         * sends -- so the correct, simplest fix is to just NOT forward
         * this one special shape as a literal header line at all. */
        if (strncmp(args[0], "HTTP/", 5) == 0) { out[0] = 0; return; }
        if (caller->headers_buf && caller->headers_len < caller->headers_cap - 2) {
            int n = snprintf(caller->headers_buf + caller->headers_len,
                              (size_t)(caller->headers_cap - caller->headers_len), "%s\n", args[0]);
            if (n > 0) caller->headers_len += n;
            if (caller->headers_len > caller->headers_cap) caller->headers_len = caller->headers_cap;
        }
        out[0] = 0;
        return;
    }
    if (strcmp(name, "setcookie") == 0 && nargs >= 1) {
        /* Real PHP's setcookie() takes many optional args (expires, path,
         * domain, secure, httponly, or a single options array in PHP 7.3+)
         * -- this subset covers just enough for real WordPress login/
         * logout to work (a cookie's name/value need to round-trip
         * through a real "Set-Cookie:" response header and back into
         * $_COOKIE on the NEXT request): only $name/$value are used,
         * every other real attribute (expires/path/domain/secure/
         * httponly) is a documented, narrower gap -- the cookie is
         * simply set with no expiry/path restriction, which is safe
         * (over-permissive, not under) for this project's own local
         * dev/test scope. */
        char line[PHP_VAL_MAX + 64];
        snprintf(line, sizeof line, "Set-Cookie: %s=%s", args[0], nargs >= 2 ? args[1] : "");
        if (caller->headers_buf && caller->headers_len < caller->headers_cap - 2) {
            int n = snprintf(caller->headers_buf + caller->headers_len,
                              (size_t)(caller->headers_cap - caller->headers_len), "%s\n", line);
            if (n > 0) caller->headers_len += n;
            if (caller->headers_len > caller->headers_cap) caller->headers_len = caller->headers_cap;
        }
        strncpy(out, "1", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "headers_sent") == 0) {
        /* This subset never streams output before the response is fully
         * built (php_run() hands sqs_main.c one complete buffer at the
         * end, not incremental writes) -- so headers are, in this
         * project's own architecture, never "already sent" by the time
         * any PHP code could ask. Real WordPress code (wp-login.php's
         * own "if (headers_sent()) { ... }" cookie-blocked-detection
         * branch) treats a false return as the normal, expected case. */
        strncpy(out, "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "ob_start") == 0) {
        /* See g_ob_depth's own comment for the full design. */
        if (g_ob_depth < PHP_OB_STACK_MAX) {
            g_ob_saved_out[g_ob_depth] = caller->out;
            g_ob_saved_len[g_ob_depth] = caller->out_len;
            g_ob_saved_cap[g_ob_depth] = caller->out_cap;
            g_ob_depth++;
            char *buf = (char *)malloc(PHP_OUT_MAX);
            if (buf) { buf[0] = 0; caller->out = buf; caller->out_len = 0; caller->out_cap = PHP_OUT_MAX; }
        }
        strncpy(out, "1", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "ob_get_contents") == 0) {
        /* Real PHP returns false if no buffer is active -- this file's
         * usual "" for a falsy return. */
        strncpy(out, g_ob_depth > 0 ? caller->out : "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "ob_get_clean") == 0 || strcmp(name, "ob_end_clean") == 0 || strcmp(name, "ob_end_flush") == 0 || strcmp(name, "ob_get_flush") == 0) {
        /* "*_clean" discards the captured content into the return value
         * (ob_get_clean()) or nowhere (ob_end_clean(), real bool return);
         * "*_flush" instead APPENDS it onto the now-restored parent
         * buffer (real PHP: sends it "to the browser"/the next buffer
         * down) before restoring -- ob_get_flush() does both (appends
         * AND returns it). */
        if (g_ob_depth > 0) {
            char *cur = caller->out;
            int cur_len = caller->out_len;
            g_ob_depth--;
            caller->out = g_ob_saved_out[g_ob_depth];
            caller->out_len = g_ob_saved_len[g_ob_depth];
            caller->out_cap = g_ob_saved_cap[g_ob_depth];
            int want_flush = (strcmp(name, "ob_end_flush") == 0 || strcmp(name, "ob_get_flush") == 0);
            int want_return = (strcmp(name, "ob_get_clean") == 0 || strcmp(name, "ob_get_flush") == 0);
            if (want_flush && caller->out && caller->out_len < caller->out_cap - 1) {
                int room = caller->out_cap - caller->out_len - 1;
                int n = cur_len < room ? cur_len : room;
                memcpy(caller->out + caller->out_len, cur, (size_t)n);
                caller->out_len += n;
                caller->out[caller->out_len] = 0;
            }
            if (want_return) { strncpy(out, cur, outcap - 1); out[outcap - 1] = 0; }
            else { strncpy(out, "1", outcap - 1); out[outcap - 1] = 0; }
            free(cur);
        } else {
            out[0] = 0;
        }
        return;
    }
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
                "str_contains", "implode", "join", "extension_loaded", "str_replace", NULL
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
        /* DEAD for the common case -- a direct "array_unshift(...)" call
         * is now intercepted much earlier (php_eval_factor's own raw-
         * variable-reference special case, alongside isset()/unset()) so
         * it can mutate the CALLER's real array storage instead of a
         * disposable snapshot copy -- see that version's own comment for
         * the real bug this fixed (WP_Hook::apply_filters() silently
         * dropping every filter callback's own $value argument). Left
         * here, unreachable from ordinary syntax but still reachable via
         * an INDIRECT call (call_user_func('array_unshift', $args, ...)),
         * as a harmless (if imperfect -- same old no-real-mutation
         * limitation) fallback for that much rarer path, since args[0] is
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
    if (strcmp(name, "intval") == 0 && nargs >= 1) {
        /* Previously entirely UNIMPLEMENTED -- fell through to this
         * file's generic "unknown function -> ''" degrade, i.e. always
         * "" (a real, high-impact bug for any caller doing numeric
         * comparison/arithmetic on the "converted" result -- confirmed
         * this session via array_map('intval', $object_ids) inside
         * WordPress core's own update_meta_cache()). long, not atoi()'s
         * int, to match this file's own (long)strtol()-based `is_int()`
         * -- consistent overflow behavior between the two. */
        long v = strtol(args[0], NULL, 10);
        snprintf(out, outcap, "%ld", v);
        return;
    }
    if (strcmp(name, "floatval") == 0 || strcmp(name, "doubleval") == 0) {
        double v = nargs >= 1 ? php_to_num(args[0]) : 0.0;
        php_num_to_str(v, out, outcap);
        return;
    }
    if (strcmp(name, "strval") == 0) {
        strncpy(out, nargs >= 1 ? args[0] : "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "boolval") == 0) {
        strncpy(out, (nargs >= 1 && php_truthy(args[0])) ? "1" : "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "array_map") == 0 && nargs >= 2) {
        /* Previously entirely UNIMPLEMENTED -- fell through to this
         * file's generic "unknown function -> ''" degrade, which then
         * count()s as a single scalar (empty string), not an array at
         * all. One of THE most-used real PHP array builtins in
         * WordPress core (array_map('intval', ...), array_map(
         * 'sanitize_text_field', ...), array_map(array($this,'method'),
         * ...), ...) -- confirmed as a real, severe, high-impact bug
         * while chasing why session persistence never worked:
         * update_meta_cache()'s own "$object_ids = array_map('intval',
         * $object_ids);" silently collapsed a real multi-element id list
         * down to a single non-element, so wp_cache_get_multiple()
         * never even looked up the real id, and every subsequent
         * "was this already cached" check silently treated EVERY
         * object id as already-cached-and-empty. `args[0]` is the
         * callable (a plain function name, "Class::method", or an
         * arrref-encoded "array($obj,'method')"/"array('Class',
         * 'method')" pair -- same callable shapes php_invoke_callable()
         * already resolves for call_user_func()), `args[1]` the array
         * to map (real PHP supports mapping MULTIPLE arrays in
         * parallel -- not modeled here, a documented, narrower gap;
         * the single-array form is by far the dominant real shape).
         * Preserves the original array's own keys, matching real PHP's
         * own single-array array_map() semantics. */
        int aid = php_arrref_decode(args[1]);
        int nid = php_array_new();
        if (aid >= 0 && nid >= 0) {
            int i;
            for (i = 0; i < g_arrays[aid].count; i++) {
                char one_arg_storage[PHP_VAL_MAX];
                char *one_arg = one_arg_storage;
                strncpy(one_arg, g_arrays[aid].items[i].val, PHP_VAL_MAX - 1); one_arg[PHP_VAL_MAX - 1] = 0;
                char mapped[PHP_VAL_MAX];
                php_invoke_callable(caller, args[0], &one_arg, 1, mapped, sizeof mapped);
                php_kv_set(&g_arrays[nid], g_arrays[aid].items[i].key, mapped);
            }
        }
        if (nid >= 0) php_arrref_encode(nid, out, outcap); else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_filter") == 0 && nargs >= 1) {
        /* Previously entirely UNIMPLEMENTED, same "unknown function ->
         * ''" degrade as array_map() above. Used directly by real
         * WordPress core's own WP_Session_Tokens::get_sessions() ("
         * return array_filter($sessions, array($this,
         * 'is_still_valid'));", gating which sessions are still valid
         * on EVERY logged-in page load) among many other real call
         * sites. With no `$callback` (args[1] absent), real PHP keeps
         * every truthy element -- modeled here too. Re-indexes to
         * consecutive integer keys ONLY when the source array was
         * itself plain-integer-indexed from 0 (matching real PHP's own
         * "preserve original keys" behavior for array_filter(), a
         * common point of confusion but the real, documented
         * semantics -- keys are NEVER renumbered by array_filter()
         * itself). */
        int aid = php_arrref_decode(args[0]);
        int nid = php_array_new();
        if (aid >= 0 && nid >= 0) {
            int i;
            for (i = 0; i < g_arrays[aid].count; i++) {
                int keep;
                if (nargs >= 2 && args[1][0]) {
                    char one_arg_storage[PHP_VAL_MAX];
                    char *one_arg = one_arg_storage;
                    strncpy(one_arg, g_arrays[aid].items[i].val, PHP_VAL_MAX - 1); one_arg[PHP_VAL_MAX - 1] = 0;
                    char r[PHP_VAL_MAX];
                    php_invoke_callable(caller, args[1], &one_arg, 1, r, sizeof r);
                    keep = php_truthy(r);
                } else {
                    keep = php_truthy(g_arrays[aid].items[i].val);
                }
                if (keep) php_kv_set(&g_arrays[nid], g_arrays[aid].items[i].key, g_arrays[aid].items[i].val);
            }
        }
        if (nid >= 0) php_arrref_encode(nid, out, outcap); else out[0] = 0;
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
    if (strcmp(name, "hash") == 0 && nargs >= 2) {
        /* Real PHP's hash() supports many algorithms -- this subset only
         * has a real MD5 primitive (see php_md5_hex's own comment), same
         * documented tradeoff as hash_hmac()'s own "ignore $algo, always
         * use the one real hash this file has" choice. Needed by
         * WP_Session_Tokens::hash_token() ("return hash('sha256',
         * $token);", used to verify a session's auth-cookie token on
         * every logged-in page load) -- previously entirely
         * unimplemented, silently degrading to "" (falsy), which broke
         * session-token verification unconditionally regardless of
         * whether the real token was correct. */
        int raw = (nargs >= 3) && php_truthy(args[2]);
        char hex[33];
        php_md5_hex((const unsigned char *)args[1], (unsigned long)strlen(args[1]), hex);
        if (raw) {
            unsigned char rawbytes[16];
            int i;
            for (i = 0; i < 16; i++) {
                unsigned int hi = (unsigned int)regex_hex_digit(hex[i * 2]);
                unsigned int lo = (unsigned int)regex_hex_digit(hex[i * 2 + 1]);
                rawbytes[i] = (unsigned char)((hi << 4) | lo);
            }
            int n = 16; if (n > outcap - 1) n = outcap - 1;
            memcpy(out, rawbytes, (size_t)n); out[n] = 0;
        } else {
            strncpy(out, hex, outcap - 1); out[outcap - 1] = 0;
        }
        return;
    }
    if (strcmp(name, "hash_hmac") == 0 && nargs >= 3) {
        /* Real PHP's hash_hmac() supports many algorithms ('sha384' is
         * what wp_hash_password()/wp_check_password() actually use, via
         * "hash_hmac('sha384', trim($password), 'wp-sha384', true)"
         * before ever reaching password_hash()/password_verify()).
         * Previously entirely UNIMPLEMENTED (fell through to this file's
         * generic "unknown function -> ''" degrade) -- a real, severe
         * bug: with hash_hmac() always returning "", EVERY password
         * (regardless of its actual content) normalized to the exact
         * same "$password_to_hash" input, so every user account would
         * have ended up with the SAME password hash no matter what
         * password was chosen at account-creation time, and any
         * password at all would have "verified" correctly against any
         * account -- a correctness bug that also happens to be a serious
         * security bug (login is meaningless without normalized password
         * hashes correctly encoding the user's own real password), found
         * while fixing the OUTER "wp-login.php submit button does
         * nothing" issue. Ignores `$algo` (this subset has no real
         * SHA-384 implementation -- see password_hash()'s own comment on
         * the same "real primitive not yet worth building, MD5 is
         * already real and available" tradeoff) and always computes a
         * standard, real HMAC-MD5 construction instead -- correct HMAC
         * semantics (block-size-normalized key, ipad/opad XOR, two-pass
         * MD5), just a different, smaller (128-bit, not 384-bit)
         * underlying hash than real PHP would use for this specific
         * call. Since this subset's own login round-trip only needs
         * hash_hmac() to be a REAL, deterministic, key-dependent
         * function of its input (not to bit-for-bit match PHP's own
         * SHA-384 HMAC), this is sufficient -- documented as a real,
         * narrower gap versus real WordPress's own on-disk hash format,
         * not a silent one. */
        const char *key_in = args[2];
        int raw = (nargs >= 4) && php_truthy(args[3]);
        unsigned char key_block[64];
        memset(key_block, 0, sizeof key_block);
        int klen = (int)strlen(key_in);
        if (klen > 64) {
            char keyhash[33];
            php_md5_hex((const unsigned char *)key_in, (unsigned long)klen, keyhash);
            /* keyhash is hex text, not raw bytes -- fine here, still a
             * real, deterministic, key-dependent 32-byte block. */
            memcpy(key_block, keyhash, 32);
        } else {
            memcpy(key_block, key_in, (size_t)klen);
        }
        unsigned char ipad[64], opad[64];
        int i;
        for (i = 0; i < 64; i++) { ipad[i] = key_block[i] ^ 0x36; opad[i] = key_block[i] ^ 0x5c; }
        int dlen = (int)strlen(args[1]);
        unsigned char *inner_msg = (unsigned char *)malloc((size_t)(64 + dlen));
        memcpy(inner_msg, ipad, 64);
        memcpy(inner_msg + 64, args[1], (size_t)dlen);
        char inner_hex[33];
        php_md5_hex(inner_msg, (unsigned long)(64 + dlen), inner_hex);
        free(inner_msg);
        unsigned char outer_msg[64 + 32];
        memcpy(outer_msg, opad, 64);
        memcpy(outer_msg + 64, inner_hex, 32);
        char outer_hex[33];
        php_md5_hex(outer_msg, sizeof outer_msg, outer_hex);
        if (raw) {
            /* Pack the 32 hex chars back down to 16 raw bytes, matching
             * real hash_hmac()'s $raw_output shape (a binary string, not
             * hex text) -- callers base64_encode() this immediately. */
            unsigned char rawbytes[16];
            for (i = 0; i < 16; i++) {
                unsigned int hi = (unsigned int)regex_hex_digit(outer_hex[i * 2]);
                unsigned int lo = (unsigned int)regex_hex_digit(outer_hex[i * 2 + 1]);
                rawbytes[i] = (unsigned char)((hi << 4) | lo);
            }
            int n = 16; if (n > outcap - 1) n = outcap - 1;
            memcpy(out, rawbytes, (size_t)n); out[n] = 0;
        } else {
            strncpy(out, outer_hex, outcap - 1); out[outcap - 1] = 0;
        }
        return;
    }
    if (strcmp(name, "hash_equals") == 0 && nargs >= 2) {
        /* Not timing-safe (real hash_equals()'s whole point) -- this
         * subset already documents its crypto as "real enough for a
         * local dev/test environment, not hardened for production" (see
         * password_hash()'s own comment); a plain strcmp() is consistent
         * with that stance and this file has no constant-time-compare
         * primitive to reach for anyway. */
        int eq = (strlen(args[0]) == strlen(args[1])) && (memcmp(args[0], args[1], strlen(args[0])) == 0);
        strncpy(out, eq ? "1" : "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "hash_hmac_algos") == 0) {
        /* Previously entirely UNIMPLEMENTED -- fell through to this
         * file's generic "unknown function -> ''" degrade, which is
         * FALSY -- real WordPress's own wp_hash() (pluggable.php, backing
         * wp_validate_auth_cookie()'s own HMAC check, i.e. every cookie-
         * based "am I still logged in" check on every page load) guards
         * its own hash_hmac() call with "if (!in_array($algo,
         * hash_hmac_algos(), true)) { throw ...; }" -- an empty-string
         * (non-array) haystack made in_array() return false, so the
         * guard always threw, and this subset has no real try/catch
         * exception handling to recover from that (a real, separate,
         * documented gap) -- the thrown exception simply propagated as
         * an unhandled construct, silently breaking wp_hash() (and
         * therefore auth-cookie validation) entirely. Lists exactly the
         * one algorithm this subset's own hash_hmac() actually
         * implements ('md5' -- see hash_hmac()'s own comment on why a
         * real HMAC-MD5, not real SHA-256/384, backs every hash_hmac()
         * call here regardless of the requested algorithm) plus 'sha256'
         * (the specific one wp_hash()'s own default $algo parameter
         * requests), so the guard passes and falls through to the real
         * call -- correct for THIS subset's own purposes, since
         * hash_hmac() itself already ignores $algo and always computes
         * the same real HMAC-MD5 construction regardless of which of
         * these names was requested. */
        int aid = php_array_new();
        if (aid >= 0) {
            php_kv_set(&g_arrays[aid], "0", "md5");
            php_kv_set(&g_arrays[aid], "1", "sha256");
            php_arrref_encode(aid, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "base64_encode") == 0 && nargs >= 1) {
        static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const unsigned char *p = (const unsigned char *)args[0];
        int len = (int)strlen(args[0]);
        int o = 0, i = 0;
        for (i = 0; i + 2 < len && o < outcap - 5; i += 3) {
            unsigned int v = ((unsigned int)p[i] << 16) | ((unsigned int)p[i+1] << 8) | p[i+2];
            out[o++] = b64tab[(v >> 18) & 0x3F]; out[o++] = b64tab[(v >> 12) & 0x3F];
            out[o++] = b64tab[(v >> 6) & 0x3F]; out[o++] = b64tab[v & 0x3F];
        }
        int rem = len - i;
        if (rem == 1 && o < outcap - 5) {
            unsigned int v = (unsigned int)p[i] << 16;
            out[o++] = b64tab[(v >> 18) & 0x3F]; out[o++] = b64tab[(v >> 12) & 0x3F];
            out[o++] = '='; out[o++] = '=';
        } else if (rem == 2 && o < outcap - 5) {
            unsigned int v = ((unsigned int)p[i] << 16) | ((unsigned int)p[i+1] << 8);
            out[o++] = b64tab[(v >> 18) & 0x3F]; out[o++] = b64tab[(v >> 12) & 0x3F];
            out[o++] = b64tab[(v >> 6) & 0x3F]; out[o++] = '=';
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "base64_decode") == 0 && nargs >= 1) {
        const char *p = args[0];
        int o = 0;
        unsigned int buf = 0; int bits = 0;
        for (; *p && o < outcap - 1; p++) {
            char c = *p;
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            else continue; /* '=' padding or whitespace -- skip */
            buf = (buf << 6) | (unsigned int)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out[o++] = (char)((buf >> bits) & 0xFF);
            }
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "password_hash") == 0 && nargs >= 1) {
        /* Real PHP's password_hash() is bcrypt (a deliberately slow,
         * salted KDF) -- this subset doesn't have a real bcrypt
         * implementation (a much bigger undertaking than the rest of
         * this file's crypto -- see php_md5_hex's own comment on why MD5
         * was worth a real implementation but bcrypt wasn't attempted
         * yet). Previously entirely UNIMPLEMENTED (fell through to the
         * generic "unknown function -> ''" degrade), which broke real
         * WordPress password creation/login SILENTLY: wp_hash_password()
         * (wp-includes/pluggable.php) does "return '$wp' .
         * password_hash($password_to_hash, $algorithm, $options);" -- so
         * every real user's stored password hash was just the literal
         * 3-byte string "$wp", identical for every account regardless of
         * password, and wp_check_password()'s own password_verify() call
         * (see below) would have had nothing real to check against
         * either. A random-salted MD5 (already a real, tested primitive
         * in this file -- see php_md5_hex) is NOT cryptographically
         * appropriate for a real production password store, but IS
         * sufficient for this project's own stated scope (a local dev/
         * test WordPress environment, not a hardened multi-user
         * deployment) and is genuinely salted/verifiable, unlike the
         * total no-op this replaces. Format: "$sqsmd5$<16-hex-salt>$
         * <32-hex-digest>", digest = md5(salt + password) -- own format,
         * not real bcrypt, so this subset's own login flow is
         * self-consistent but NOT compatible with a real WordPress
         * database's own bcrypt hashes (a real, documented limitation,
         * not a silent one). */
        unsigned char saltbytes[8];
        int i;
        for (i = 0; i < 8; i++) saltbytes[i] = (unsigned char)(rand() & 0xFF);
        char salt_hex[17];
        for (i = 0; i < 8; i++) snprintf(salt_hex + i * 2, 3, "%02x", saltbytes[i]);
        char combined[PHP_VAL_MAX + 32];
        snprintf(combined, sizeof combined, "%s%s", salt_hex, args[0]);
        char digest_hex[33];
        php_md5_hex((const unsigned char *)combined, (unsigned long)strlen(combined), digest_hex);
        snprintf(out, outcap, "$sqsmd5$%s$%s", salt_hex, digest_hex);
        return;
    }
    if (strcmp(name, "password_verify") == 0 && nargs >= 2) {
        const char *password = args[0];
        const char *hash = args[1];
        int ok = 0;
        if (strncmp(hash, "$sqsmd5$", 8) == 0) {
            const char *salt_start = hash + 8;
            const char *dollar2 = strchr(salt_start, '$');
            if (dollar2 && (dollar2 - salt_start) == 16) {
                char salt_hex[17];
                memcpy(salt_hex, salt_start, 16); salt_hex[16] = 0;
                char combined[PHP_VAL_MAX + 32];
                snprintf(combined, sizeof combined, "%s%s", salt_hex, password);
                char digest_hex[33];
                php_md5_hex((const unsigned char *)combined, (unsigned long)strlen(combined), digest_hex);
                ok = (strcmp(dollar2 + 1, digest_hex) == 0);
            }
        }
        strncpy(out, ok ? "1" : "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "password_needs_rehash") == 0 && nargs >= 1) {
        /* Always "no" -- this subset only ever produces its own
         * "$sqsmd5$..." format itself, so there's no real algorithm/cost
         * migration to detect (see password_hash()'s own comment on the
         * documented real-bcrypt-compatibility gap this doesn't solve). */
        out[0] = 0;
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
    if (strcmp(name, "str_replace") == 0 && nargs >= 3) {
        /* Found completely missing this session -- ~100 real calls per
           WordPress page load silently no-op'd (this file's own "unknown
           function -> no-op" convention meant str_replace(...) always
           returned "", not the unmodified subject, actively CORRUPTING
           every string it touched rather than just leaving it unchanged
           -- one of the most consequential single-builtin gaps found,
           given how pervasively real PHP code uses str_replace() for
           basic string assembly/escaping). Only the scalar
           search/replace/subject form is supported -- real PHP also
           accepts an ARRAY for $search (and/or $replace), replacing each
           pair in turn; that form isn't implemented (a documented,
           narrower gap than the scalar form's total absence was). */
        const char *search = args[0];
        const char *replace = args[1];
        const char *subject = args[2];
        int slen = (int)strlen(search);
        if (slen == 0) { strncpy(out, subject, outcap - 1); out[outcap - 1] = 0; return; }
        int o = 0;
        const char *p = subject;
        while (*p && o < outcap - 1) {
            if (strncmp(p, search, (size_t)slen) == 0) {
                int rlen = (int)strlen(replace);
                int room = outcap - 1 - o;
                if (rlen > room) rlen = room;
                memcpy(out + o, replace, (size_t)rlen);
                o += rlen;
                p += slen;
            } else {
                out[o++] = *p++;
            }
        }
        out[o] = 0;
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
    if (strcmp(name, "compact") == 0) {
        /* "compact('a', 'b', ...)" / "compact($namesArray)" / a mix of
         * both -- builds an associative array from the CALLING scope's
         * own variables, one entry per given name, key = the variable's
         * own name, value = its current value (an arrref token copied
         * through as-is if that variable itself holds an array, matching
         * how every other array-valued value already flows through this
         * file's plain-string machinery). A named variable that doesn't
         * exist in the caller's scope is simply skipped (real PHP does
         * the same -- compact() never warns/errors for a missing name).
         * Previously entirely UNIMPLEMENTED -- fell through to this
         * file's generic "unknown function -> empty string" default,
         * which is a real, high-impact bug wherever real code uses the
         * extremely common "compact(...) then pass through a filter"
         * pattern: WP_Query::get_posts()'s own "$clauses = (array)
         * apply_filters_ref_array('posts_clauses', array(compact(
         * $pieces), &$this)); $where = $clauses['where'] ?? ''; ..."
         * -- with compact() silently returning "" instead of a real
         * array, $clauses ends up empty, and EVERY ONE of $where/
         * $fields/$join/$groupby/$orderby/$distinct/$limits -- no
         * matter how correctly each was built earlier in the very same
         * function -- gets silently reset to '' by the "?? ''"
         * fallback, discarding the entire WHERE/SELECT/JOIN clause
         * (including a real, correctly-built search LIKE clause) right
         * before the final SQL string is assembled. Confirmed as the
         * true root cause of a whole session's worth of "WordPress
         * search returns nothing" investigation, once every OTHER
         * upstream bug (isset()/empty()/count()/is_array() not
         * handling a parameter array's arrref-token shape, a missing
         * stripslashes() builtin) had already been fixed and this was
         * the only thing still standing between a correctly-built WHERE
         * clause and the final query. */
        int id = php_array_new();
        if (id >= 0) {
            int ai;
            for (ai = 0; ai < nargs; ai++) {
                int aid = php_arrref_decode(args[ai]);
                if (aid >= 0) {
                    int i;
                    for (i = 0; i < g_arrays[aid].count; i++) {
                        php_compact_add(caller, id, g_arrays[aid].items[i].val);
                    }
                } else {
                    php_compact_add(caller, id, args[ai]);
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
    if (strcmp(name, "array_reverse") == 0 && nargs >= 1) {
        /* Real PHP: `$preserve_keys` (2nd arg) keeps original keys when
         * true; when false/absent (the default, and by far the more
         * common real call shape), integer keys are renumbered
         * 0..n-1 -- string keys are ALWAYS preserved either way. */
        int aid = php_arrref_decode(args[0]);
        int preserve = nargs >= 2 && php_truthy(args[1]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i, ki = 0;
                for (i = g_arrays[aid].count - 1; i >= 0; i--) {
                    const char *k = g_arrays[aid].items[i].key;
                    char *endp; strtol(k, &endp, 10);
                    int is_int_key = (k[0] != 0 && *endp == 0);
                    if (preserve || !is_int_key) php_kv_set(&g_arrays[id], k, g_arrays[aid].items[i].val);
                    else { char nk[16]; snprintf(nk, sizeof nk, "%d", ki++); php_kv_add(&g_arrays[id], nk, g_arrays[aid].items[i].val); }
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_sum") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        double sum = 0;
        if (aid >= 0) { int i; for (i = 0; i < g_arrays[aid].count; i++) sum += php_to_num(g_arrays[aid].items[i].val); }
        php_num_to_str(sum, out, outcap);
        return;
    }
    if (strcmp(name, "array_product") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        double prod = 1;
        if (aid >= 0) { int i; for (i = 0; i < g_arrays[aid].count; i++) prod *= php_to_num(g_arrays[aid].items[i].val); }
        else prod = 0;
        php_num_to_str(prod, out, outcap);
        return;
    }
    if (strcmp(name, "array_fill") == 0 && nargs >= 3) {
        int start = atoi(args[0]), count = atoi(args[1]);
        int id = php_array_new();
        if (id >= 0) {
            int i; for (i = 0; i < count; i++) { char k[16]; snprintf(k, sizeof k, "%d", start + i); php_kv_set(&g_arrays[id], k, args[2]); }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_fill_keys") == 0 && nargs >= 2) {
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) { int i; for (i = 0; i < g_arrays[aid].count; i++) php_kv_set(&g_arrays[id], g_arrays[aid].items[i].val, args[1]); }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_combine") == 0 && nargs >= 2) {
        int kaid = php_arrref_decode(args[0]), vaid = php_arrref_decode(args[1]);
        int id = php_array_new();
        if (id >= 0) {
            if (kaid >= 0 && vaid >= 0) {
                int n = g_arrays[kaid].count < g_arrays[vaid].count ? g_arrays[kaid].count : g_arrays[vaid].count;
                int i; for (i = 0; i < n; i++) php_kv_set(&g_arrays[id], g_arrays[kaid].items[i].val, g_arrays[vaid].items[i].val);
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if ((strcmp(name, "array_diff") == 0 || strcmp(name, "array_diff_key") == 0 || strcmp(name, "array_diff_assoc") == 0) && nargs >= 2) {
        /* array_diff(): keep items whose VALUE doesn't appear in any
         * other argument array. array_diff_key(): keep items whose KEY
         * doesn't appear. array_diff_assoc(): keep items whose key=>value
         * PAIR doesn't appear (both must match to exclude). */
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i, ai;
                for (i = 0; i < g_arrays[aid].count; i++) {
                    int found = 0;
                    for (ai = 1; ai < nargs && !found; ai++) {
                        int oaid = php_arrref_decode(args[ai]);
                        if (oaid < 0) continue;
                        int j;
                        for (j = 0; j < g_arrays[oaid].count; j++) {
                            int key_match = strcmp(g_arrays[oaid].items[j].key, g_arrays[aid].items[i].key) == 0;
                            int val_match = strcmp(g_arrays[oaid].items[j].val, g_arrays[aid].items[i].val) == 0;
                            if (strcmp(name, "array_diff") == 0 && val_match) { found = 1; break; }
                            if (strcmp(name, "array_diff_key") == 0 && key_match) { found = 1; break; }
                            if (strcmp(name, "array_diff_assoc") == 0 && key_match && val_match) { found = 1; break; }
                        }
                    }
                    if (!found) php_kv_set(&g_arrays[id], g_arrays[aid].items[i].key, g_arrays[aid].items[i].val);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if ((strcmp(name, "array_intersect") == 0 || strcmp(name, "array_intersect_key") == 0 || strcmp(name, "array_intersect_assoc") == 0) && nargs >= 2) {
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i, ai;
                for (i = 0; i < g_arrays[aid].count; i++) {
                    int in_all = 1;
                    for (ai = 1; ai < nargs && in_all; ai++) {
                        int oaid = php_arrref_decode(args[ai]);
                        int found = 0;
                        if (oaid >= 0) {
                            int j;
                            for (j = 0; j < g_arrays[oaid].count; j++) {
                                int key_match = strcmp(g_arrays[oaid].items[j].key, g_arrays[aid].items[i].key) == 0;
                                int val_match = strcmp(g_arrays[oaid].items[j].val, g_arrays[aid].items[i].val) == 0;
                                if (strcmp(name, "array_intersect") == 0 && val_match) { found = 1; break; }
                                if (strcmp(name, "array_intersect_key") == 0 && key_match) { found = 1; break; }
                                if (strcmp(name, "array_intersect_assoc") == 0 && key_match && val_match) { found = 1; break; }
                            }
                        }
                        if (!found) in_all = 0;
                    }
                    if (in_all) php_kv_set(&g_arrays[id], g_arrays[aid].items[i].key, g_arrays[aid].items[i].val);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_column") == 0 && nargs >= 2) {
        /* Real PHP's 3rd $index_key arg (use a column's own value as the
         * result's key instead of 0..n-1) -- supported when given. Each
         * row is expected to be an array (real WordPress core's own use,
         * e.g. pluck-shaped code on $wpdb->get_results(..., ARRAY_A)
         * rows) OR an object (real PHP also supports this; this subset
         * reads an object property the same way via g_objects[].props). */
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i;
                for (i = 0; i < g_arrays[aid].count; i++) {
                    const char *rowval = g_arrays[aid].items[i].val;
                    int raid = php_arrref_decode(rowval);
                    int roid = php_objref_decode(rowval);
                    char colval[PHP_VAL_MAX] = "";
                    char idxval[PHP_VAL_MAX] = "";
                    int has_col = 0, has_idx = (nargs < 3);
                    if (raid >= 0) {
                        has_col = php_kv_has(&g_arrays[raid], args[1], colval, sizeof colval);
                        if (nargs >= 3) has_idx = php_kv_has(&g_arrays[raid], args[2], idxval, sizeof idxval);
                    } else if (roid >= 0) {
                        has_col = php_kv_has(&g_objects[roid].props, args[1], colval, sizeof colval);
                        if (nargs >= 3) has_idx = php_kv_has(&g_objects[roid].props, args[2], idxval, sizeof idxval);
                    }
                    if (has_col) {
                        if (nargs >= 3 && has_idx) php_kv_set(&g_arrays[id], idxval, colval);
                        else php_kv_add(&g_arrays[id], "", colval);
                    }
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_reduce") == 0 && nargs >= 2) {
        int aid = php_arrref_decode(args[0]);
        char acc[PHP_VAL_MAX];
        strncpy(acc, nargs >= 3 ? args[2] : "", sizeof acc - 1); acc[sizeof acc - 1] = 0;
        if (aid >= 0) {
            int i;
            for (i = 0; i < g_arrays[aid].count; i++) {
                char *cargs[2]; char c0[PHP_VAL_MAX], c1[PHP_VAL_MAX];
                cargs[0] = c0; cargs[1] = c1;
                strncpy(c0, acc, sizeof c0 - 1); c0[sizeof c0 - 1] = 0;
                strncpy(c1, g_arrays[aid].items[i].val, sizeof c1 - 1); c1[sizeof c1 - 1] = 0;
                php_invoke_callable(caller, args[1], cargs, 2, acc, sizeof acc);
            }
        }
        strncpy(out, acc, outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "array_walk") == 0 && nargs >= 2) {
        /* No true by-reference callback parameter (this subset's own
         * documented scope limit -- see array_unshift's own comment on
         * why a bare read normally snapshots) -- the callback CAN still
         * observe/use each value, just can't mutate the array through
         * its own first parameter the way real array_walk() allows;
         * good enough for the dominant real "read each element, do a
         * side effect" call shape. */
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0) {
            int i;
            for (i = 0; i < g_arrays[aid].count; i++) {
                char *cargs[3]; char c0[PHP_VAL_MAX], c1[128], c2[PHP_VAL_MAX];
                cargs[0] = c0; cargs[1] = c1; cargs[2] = c2;
                strncpy(c0, g_arrays[aid].items[i].val, sizeof c0 - 1); c0[sizeof c0 - 1] = 0;
                strncpy(c1, g_arrays[aid].items[i].key, sizeof c1 - 1); c1[sizeof c1 - 1] = 0;
                int na = 2;
                if (nargs >= 3) { strncpy(c2, args[2], sizeof c2 - 1); c2[sizeof c2 - 1] = 0; na = 3; }
                char r[PHP_VAL_MAX];
                php_invoke_callable(caller, args[1], cargs, na, r, sizeof r);
            }
        }
        strncpy(out, "1", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "array_count_values") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (aid >= 0) {
                int i;
                for (i = 0; i < g_arrays[aid].count; i++) {
                    char cur[PHP_VAL_MAX];
                    int n = php_kv_has(&g_arrays[id], g_arrays[aid].items[i].val, cur, sizeof cur) ? atoi(cur) : 0;
                    char nv[16]; snprintf(nv, sizeof nv, "%d", n + 1);
                    php_kv_set(&g_arrays[id], g_arrays[aid].items[i].val, nv);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_is_list") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        int is_list = 1;
        if (aid >= 0) {
            int i; for (i = 0; i < g_arrays[aid].count; i++) { char k[16]; snprintf(k, sizeof k, "%d", i); if (strcmp(g_arrays[aid].items[i].key, k) != 0) { is_list = 0; break; } }
        } else is_list = 0;
        strncpy(out, is_list ? "1" : "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if ((strcmp(name, "array_key_first") == 0 || strcmp(name, "array_key_last") == 0) && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        if (aid >= 0 && g_arrays[aid].count > 0) {
            int idx = strcmp(name, "array_key_first") == 0 ? 0 : g_arrays[aid].count - 1;
            strncpy(out, g_arrays[aid].items[idx].key, outcap - 1); out[outcap - 1] = 0;
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "array_merge_recursive") == 0) {
        /* A documented, narrower gap: behaves like array_merge() (no
         * real recursive merging of nested-array values under the same
         * key) -- true recursive merging is rare enough in real
         * WordPress core call sites to not be worth the extra
         * complexity here; this is still strictly more correct than
         * the old "unknown function -> ''" degrade (which discarded
         * every argument's data entirely). */
        int id = php_array_new();
        if (id >= 0) {
            int ai;
            for (ai = 0; ai < nargs; ai++) {
                int aid = php_arrref_decode(args[ai]);
                if (aid < 0) continue;
                int i;
                for (i = 0; i < g_arrays[aid].count; i++) {
                    const char *k = g_arrays[aid].items[i].key;
                    char *endp; strtol(k, &endp, 10);
                    if (k[0] != 0 && *endp == 0) php_kv_add(&g_arrays[id], k, g_arrays[aid].items[i].val);
                    else php_kv_set(&g_arrays[id], k, g_arrays[aid].items[i].val);
                }
            }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "range") == 0 && nargs >= 2) {
        double start = php_to_num(args[0]), end = php_to_num(args[1]);
        double step = nargs >= 3 ? fabs(php_to_num(args[2])) : 1;
        if (step <= 0) step = 1;
        int id = php_array_new();
        if (id >= 0) {
            int idx = 0;
            if (start <= end) { double v; for (v = start; v <= end + 1e-9; v += step) { char k[16]; snprintf(k, sizeof k, "%d", idx++); php_num_to_str(v, out, outcap); php_kv_add(&g_arrays[id], k, out); } }
            else { double v; for (v = start; v >= end - 1e-9; v -= step) { char k[16]; snprintf(k, sizeof k, "%d", idx++); php_num_to_str(v, out, outcap); php_kv_add(&g_arrays[id], k, out); } }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "get_object_vars") == 0 && nargs >= 1) {
        int oid = php_objref_decode(args[0]);
        int id = php_array_new();
        if (id >= 0) {
            if (oid >= 0) { int i; for (i = 0; i < g_objects[oid].props.count; i++) php_kv_set(&g_arrays[id], g_objects[oid].props.items[i].key, g_objects[oid].props.items[i].val); }
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
    if (strcmp(name, "round") == 0 && nargs >= 1) {
        /* Previously entirely UNIMPLEMENTED -- fell through to this
         * file's own generic "unknown function -> ''" degrade. Found in
         * this session's own audit of real PHP builtins the vendored
         * WordPress tree actually calls vs. what this file implements
         * -- round()/ceil()/floor() are all real, common, previously-
         * missing math primitives (pagination math, image-dimension
         * calculations, price/number formatting, ...). */
        double v = strtod(args[0], NULL);
        int prec = nargs >= 2 ? atoi(args[1]) : 0;
        double mult = pow(10.0, prec);
        double r = (v >= 0) ? floor(v * mult + 0.5) / mult : ceil(v * mult - 0.5) / mult;
        if (prec <= 0 && r == (long)r) snprintf(out, outcap, "%ld", (long)r);
        else snprintf(out, outcap, "%.*f", prec > 0 ? prec : 0, r);
        return;
    }
    if (strcmp(name, "ceil") == 0 && nargs >= 1) {
        double r = ceil(strtod(args[0], NULL));
        php_num_to_str(r, out, outcap);
        return;
    }
    if (strcmp(name, "floor") == 0 && nargs >= 1) {
        double r = floor(strtod(args[0], NULL));
        php_num_to_str(r, out, outcap);
        return;
    }
    if (strcmp(name, "sqrt") == 0 && nargs >= 1) {
        double v = strtod(args[0], NULL);
        php_num_to_str(v < 0 ? 0.0 / 0.0 : sqrt(v), out, outcap);
        return;
    }
    if (strcmp(name, "pow") == 0 && nargs >= 2) {
        double r = pow(strtod(args[0], NULL), strtod(args[1], NULL));
        php_num_to_str(r, out, outcap);
        return;
    }
    if (strcmp(name, "fmod") == 0 && nargs >= 2) {
        double r = fmod(strtod(args[0], NULL), strtod(args[1], NULL));
        php_num_to_str(r, out, outcap);
        return;
    }
    if (strcmp(name, "log") == 0 && nargs >= 1) {
        double v = strtod(args[0], NULL);
        double r = (nargs >= 2) ? (log(v) / log(strtod(args[1], NULL))) : log(v);
        php_num_to_str(r, out, outcap);
        return;
    }
    if (strcmp(name, "log10") == 0 && nargs >= 1) {
        php_num_to_str(log10(strtod(args[0], NULL)), out, outcap);
        return;
    }
    if (strcmp(name, "pi") == 0) {
        snprintf(out, outcap, "%.16f", 3.14159265358979323846);
        return;
    }
    if ((strcmp(name, "is_nan") == 0 || strcmp(name, "is_infinite") == 0) && nargs >= 1) {
        double v = strtod(args[0], NULL);
        int r = strcmp(name, "is_nan") == 0 ? (v != v) : (v != 0 && v == v && (v * 2 == v) && v != 0);
        strncpy(out, r ? "1" : "", outcap - 1); out[outcap - 1] = 0;
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
    if (strcmp(name, "str_repeat") == 0 && nargs >= 2) {
        int n = atoi(args[1]);
        int o = 0, sl = (int)strlen(args[0]);
        int i;
        for (i = 0; i < n && o < outcap - 1 - sl; i++) { memcpy(out + o, args[0], (size_t)sl); o += sl; }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "strrpos") == 0 && nargs >= 2) {
        const char *hay = args[0], *needle = args[1];
        int nlen = (int)strlen(needle);
        if (!nlen) { out[0] = 0; return; }
        const char *p = hay, *last = NULL;
        while ((p = strstr(p, needle)) != NULL) { last = p; p++; }
        if (last) snprintf(out, outcap, "%ld", (long)(last - hay));
        else out[0] = 0;
        return;
    }
    if ((strcmp(name, "strcasecmp") == 0 || strcmp(name, "strcmp") == 0 || strcmp(name, "strncasecmp") == 0 || strcmp(name, "strncmp") == 0) && nargs >= 2) {
        int r;
        if (strcmp(name, "strcmp") == 0) r = strcmp(args[0], args[1]);
        else if (strcmp(name, "strcasecmp") == 0) {
            const char *a = args[0], *b = args[1];
            while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
            r = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        } else {
            int n = nargs >= 3 ? atoi(args[2]) : 0;
            r = (strcmp(name, "strncmp") == 0) ? strncmp(args[0], args[1], (size_t)n) : strncasecmp(args[0], args[1], (size_t)n);
        }
        snprintf(out, outcap, "%d", r < 0 ? -1 : (r > 0 ? 1 : 0));
        return;
    }
    if (strcmp(name, "substr_count") == 0 && nargs >= 2) {
        const char *p = args[0]; int n = 0, nlen = (int)strlen(args[1]);
        if (nlen) { while ((p = strstr(p, args[1])) != NULL) { n++; p += nlen; } }
        snprintf(out, outcap, "%d", n);
        return;
    }
    if (strcmp(name, "strrev") == 0 && nargs >= 1) {
        int len = (int)strlen(args[0]);
        int i, n = len < outcap - 1 ? len : outcap - 1;
        for (i = 0; i < n; i++) out[i] = args[0][len - 1 - i];
        out[n] = 0;
        return;
    }
    if (strcmp(name, "ucfirst") == 0 && nargs >= 1) {
        strncpy(out, args[0], outcap - 1); out[outcap - 1] = 0;
        if (out[0]) out[0] = (char)toupper((unsigned char)out[0]);
        return;
    }
    if (strcmp(name, "lcfirst") == 0 && nargs >= 1) {
        strncpy(out, args[0], outcap - 1); out[outcap - 1] = 0;
        if (out[0]) out[0] = (char)tolower((unsigned char)out[0]);
        return;
    }
    if (strcmp(name, "ucwords") == 0 && nargs >= 1) {
        const char *delims = nargs >= 2 ? args[1] : " \t\r\n\f\v";
        strncpy(out, args[0], outcap - 1); out[outcap - 1] = 0;
        int i, start = 1;
        for (i = 0; out[i]; i++) {
            if (start) out[i] = (char)toupper((unsigned char)out[i]);
            start = strchr(delims, out[i]) != NULL;
        }
        return;
    }
    if (strcmp(name, "str_pad") == 0 && nargs >= 2) {
        int len = (int)strlen(args[0]);
        int want = atoi(args[1]);
        const char *pad = nargs >= 3 && args[2][0] ? args[2] : " ";
        int padtype = nargs >= 4 ? atoi(args[3]) : 1; /* 0=STR_PAD_LEFT,1=STR_PAD_RIGHT,2=STR_PAD_BOTH */
        if (want <= len || (int)strlen(pad) == 0) { strncpy(out, args[0], outcap - 1); out[outcap - 1] = 0; return; }
        int total_pad = want - len;
        int left_pad = (padtype == 0) ? total_pad : (padtype == 2 ? total_pad / 2 : 0);
        int right_pad = total_pad - left_pad;
        int o = 0, plen = (int)strlen(pad), i;
        for (i = 0; i < left_pad && o < outcap - 1; i++) out[o++] = pad[i % plen];
        int n = len < outcap - 1 - o ? len : outcap - 1 - o;
        memcpy(out + o, args[0], (size_t)n); o += n;
        for (i = 0; i < right_pad && o < outcap - 1; i++) out[o++] = pad[i % plen];
        out[o] = 0;
        return;
    }
    if (strcmp(name, "wordwrap") == 0 && nargs >= 1) {
        /* A documented, narrower gap: passes the string through
         * unchanged rather than really wrapping -- real usage in
         * WordPress core is almost always for terminal/email output
         * this project's own scope doesn't render anyway; correct-but-
         * unwrapped is a safe degrade (no data loss) vs the old
         * "silently wipe to empty string" default. */
        strncpy(out, args[0], outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "chunk_split") == 0 && nargs >= 1) {
        int len = nargs >= 2 ? atoi(args[1]) : 76;
        const char *end = nargs >= 3 ? args[2] : "\r\n";
        if (len <= 0) len = 76;
        int o = 0, elen = (int)strlen(end), i = 0, slen = (int)strlen(args[0]);
        while (i < slen && o < outcap - 1) {
            int n = slen - i < len ? slen - i : len;
            if (n > outcap - 1 - o) n = outcap - 1 - o;
            memcpy(out + o, args[0] + i, (size_t)n); o += n; i += n;
            if (o < outcap - 1 - elen) { memcpy(out + o, end, (size_t)elen); o += elen; }
        }
        out[o] = 0;
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
    if (strcmp(name, "stripslashes") == 0 && nargs >= 1) {
        /* Reverses addslashes() -- real PHP semantics: a backslash drops
         * itself and takes whatever follows it literally (a lone
         * trailing backslash at the very end of the string is just
         * dropped). Previously entirely UNIMPLEMENTED -- fell through to
         * this file's generic "unknown function -> degrade to empty
         * string" default (see php_func_find()'s own caller, further
         * down) -- confirmed as a real, high-impact bug this session:
         * WP_Query::parse_search()'s very first line is "$query_vars['s']
         * = stripslashes( $query_vars['s'] );", so every real search
         * silently overwrote its own search term with an empty string
         * before ever building any SQL, regardless of how correctly
         * every OTHER part of the query-building pipeline worked. */
        int o = 0;
        const char *p;
        for (p = args[0]; *p && o < outcap - 1; p++) {
            if (*p == '\\' && p[1]) { p++; out[o++] = *p; }
            else if (*p == '\\') { /* trailing lone backslash: dropped */ }
            else out[o++] = *p;
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "strip_tags") == 0 && nargs >= 1) {
        /* Previously entirely UNIMPLEMENTED -- fell through to this
         * file's generic "unknown function -> degrade to empty string"
         * default. Confirmed as a real, high-impact bug: wp_strip_all_
         * tags() (used by sanitize_user() and dozens of other core
         * sanitizers) calls this as its own LAST real step before
         * trim()'ing and returning -- with strip_tags() silently
         * wiping to "", EVERY string passed through it (not just ones
         * that actually contain tags) came out empty, so
         * sanitize_user('admin', true) returned '' instead of 'admin',
         * making wp-admin/install.php's own "sanitize_user($user_name,
         * true) !== $user_name" check ALWAYS fail -- no username could
         * ever pass installer validation, regardless of its actual
         * content. No `allowed_tags` support (this subset's usual
         * "cover the common real case, document the narrower gap"
         * scope limit) -- strips every "<...>" run unconditionally,
         * matching strip_tags($s) with no second argument, by far the
         * dominant real call shape in WordPress core. */
        int o = 0, in_tag = 0;
        const char *p;
        for (p = args[0]; *p && o < outcap - 1; p++) {
            char ch = *p;
            if (ch == '<') { in_tag = 1; continue; }
            if (ch == '>') { in_tag = 0; continue; }
            if (!in_tag) out[o++] = ch;
        }
        out[o] = 0;
        return;
    }
    if ((strcmp(name, "htmlspecialchars") == 0 || strcmp(name, "htmlentities") == 0) && nargs >= 1) {
        /* Previously entirely UNIMPLEMENTED -- fell through to this
         * file's own generic "unknown function -> ''" degrade. The
         * single highest-impact builtin found in an audit this session
         * of real PHP builtins the vendored WordPress tree actually
         * calls vs. what this file implements: real WordPress core's
         * own esc_html() (wp-includes/formatting.php) -- used to output
         * essentially every dynamic string on every single real page,
         * post titles/widget text/menu labels/form values/... -- routes
         * through _wp_specialchars(), whose own real, final step is
         * "return htmlspecialchars($text, $quote_style, $charset,
         * $double_encode);". With htmlspecialchars() always "",
         * esc_html() on ANY input always returned an empty string,
         * silently blanking every single value it ever touched across
         * the whole site -- confirmed via a direct repro. Real PHP's
         * `$quote_style` selects WHICH quote characters get escaped
         * (ENT_QUOTES/ENT_COMPAT/ENT_NOQUOTES/...) -- this subset has no
         * real predefined ENT_* constants at all (nothing pre-registers
         * them, so an undefined "ENT_QUOTES" bareword just evaluates to
         * "" via this file's own safe-default constant-lookup fallback),
         * so rather than try to interpret an unreliable style argument,
         * this always escapes all five real HTML-special characters
         * (&, <, >, ", ') -- the modern PHP 8.1+ DEFAULT (ENT_QUOTES |
         * ENT_SUBSTITUTE | ENT_HTML401) and the overwhelmingly common
         * real-world intent regardless of which legacy style a caller
         * asked for. htmlentities() is treated identically (a real,
         * documented, narrower gap -- real htmlentities() also encodes
         * non-ASCII/accented characters via named entities, not modeled
         * here; every REAL WordPress core call site in the audited
         * source only ever needs the five-character escape). */
        int o = 0;
        const char *p;
        for (p = args[0]; *p && o < outcap - 6; p++) {
            const char *rep = NULL;
            switch (*p) {
                case '&': rep = "&amp;"; break;
                case '<': rep = "&lt;"; break;
                case '>': rep = "&gt;"; break;
                case '"': rep = "&quot;"; break;
                case '\'': rep = "&#039;"; break;
                default: break;
            }
            if (rep) { int rl = (int)strlen(rep); memcpy(out + o, rep, (size_t)rl); o += rl; }
            else out[o++] = *p;
        }
        out[o] = 0;
        return;
    }
    if ((strcmp(name, "htmlspecialchars_decode") == 0 || strcmp(name, "html_entity_decode") == 0) && nargs >= 1) {
        /* Reverses the five-character escape above -- previously
         * entirely UNIMPLEMENTED, same "" degrade. Real html_entity_
         * decode() also decodes the FULL named/numeric HTML entity set
         * (&nbsp;, &amp;copy;, &#233;, ...) -- this subset only handles
         * the same five ENT_QUOTES-shaped entities its own encode side
         * produces, a real, documented, narrower gap matching the
         * dominant real "decode what WE encoded" round-trip shape. */
        int o = 0;
        const char *p = args[0];
        while (*p && o < outcap - 1) {
            if (!strncmp(p, "&amp;", 5)) { out[o++] = '&'; p += 5; }
            else if (!strncmp(p, "&lt;", 4)) { out[o++] = '<'; p += 4; }
            else if (!strncmp(p, "&gt;", 4)) { out[o++] = '>'; p += 4; }
            else if (!strncmp(p, "&quot;", 6)) { out[o++] = '"'; p += 6; }
            else if (!strncmp(p, "&#039;", 6)) { out[o++] = '\''; p += 6; }
            else if (!strncmp(p, "&apos;", 6)) { out[o++] = '\''; p += 6; }
            else { out[o++] = *p; p++; }
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
                "min", "max", "addslashes", "stripslashes", "class_exists", "is_string", "is_array",
                "is_object", "is_numeric", "is_int", "is_bool", "intval", "floatval", "doubleval",
                "strval", "boolval", "array_map", "array_filter", "serialize", "unserialize",
                "is_serialized", "hash", "hash_hmac", "hash_hmac_algos", "hash_equals",
                "base64_encode", "base64_decode", "strip_tags", "time", NULL
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
    if (strcmp(name, "time") == 0) {
        /* Previously entirely UNIMPLEMENTED -- fell through to this
         * file's generic "unknown function -> ''" degrade, which
         * evaluates as 0 in every numeric context. time() is one of the
         * single most-used PHP builtins in real WordPress core (cache/
         * transient expiry, cron scheduling, post dates, auth-cookie
         * expiration, ...) -- confirmed as a real, high-impact bug this
         * session via wp_set_auth_cookie()'s own "$expiration = time() +
         * $expire;": with time() always 0, every issued auth cookie's
         * own expiration field was just the bare duration ($expire,
         * e.g. 172800 for a 2-day cookie) instead of a real future UNIX
         * timestamp -- looking like a moment in 1970, not 2 days from
         * now -- so wp_validate_auth_cookie()'s own "if ($expired <
         * time())" check treated every cookie as already expired the
         * instant time() stopped also secretly returning 0 (e.g. once a
         * real timestamp was available anywhere else in the same
         * comparison), silently breaking session persistence across
         * requests. */
        snprintf(out, outcap, "%ld", (long)time(NULL));
        return;
    }
    if ((strcmp(name, "date") == 0 || strcmp(name, "gmdate") == 0) && nargs >= 1) {
        /* See php_format_date's own comment -- always UTC, no real
         * per-site timezone support modeled (a documented, real gap;
         * this file's own scope has no timezone-database access at
         * all). date() and gmdate() are treated identically for exactly
         * that reason (no local-vs-UTC distinction exists here). */
        time_t t = nargs >= 2 ? (time_t)atol(args[1]) : time(NULL);
        php_format_date(args[0], t, out, outcap);
        return;
    }
    if ((strcmp(name, "mktime") == 0 || strcmp(name, "gmmktime") == 0) && nargs >= 1) {
        /* function mktime(hour=now,minute=now,second=now,month=now,day=now,year=now) */
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        if (nargs >= 1) tmv.tm_hour = atoi(args[0]);
        if (nargs >= 2) tmv.tm_min = atoi(args[1]);
        if (nargs >= 3) tmv.tm_sec = atoi(args[2]);
        if (nargs >= 4) tmv.tm_mon = atoi(args[3]) - 1;
        if (nargs >= 5) tmv.tm_mday = atoi(args[4]);
        if (nargs >= 6) tmv.tm_year = atoi(args[5]) - 1900;
        time_t r = timegm(&tmv);
        snprintf(out, outcap, "%ld", (long)r);
        return;
    }
    if (strcmp(name, "checkdate") == 0 && nargs >= 3) {
        int m = atoi(args[0]), d = atoi(args[1]), y = atoi(args[2]);
        static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        int maxd = (m >= 1 && m <= 12) ? dim[m - 1] : 0;
        if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) maxd = 29;
        int ok = (m >= 1 && m <= 12 && d >= 1 && d <= maxd && y >= 1 && y <= 32767);
        strncpy(out, ok ? "1" : "", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "date_default_timezone_set") == 0 || strcmp(name, "date_default_timezone_get") == 0) {
        /* This subset always operates in UTC (see php_format_date's own
         * comment) -- accepted and ignored rather than left as an
         * "unknown function" (real WordPress core's own wp_timezone()/
         * bootstrap calls this), matching real PHP's own "UTC" default
         * when nothing else is configured. */
        strncpy(out, "UTC", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "strtotime") == 0 && nargs >= 1) {
        /* Real PHP's strtotime() understands a huge range of formats and
         * relative expressions ("+1 day", "next monday", ...) -- this
         * subset covers the dominant real shape stored data actually
         * uses: ISO-ish "YYYY-MM-DD[ HH:MM:SS]" (exactly what MySQL's
         * own DATETIME columns produce, which is what mysql2date() --
         * the function EVERY real post/comment date in WordPress core
         * passes through -- hands to it) and the bare literal "now".
         * Previously entirely UNIMPLEMENTED; a real, documented,
         * narrower gap for anything outside these two shapes (returns
         * false/"" the same way real strtotime() does for unparseable
         * input, not a crash). */
        const char *s = args[0];
        while (*s == ' ') s++;
        if (!strcasecmp(s, "now") || !s[0]) { snprintf(out, outcap, "%ld", (long)time(NULL)); return; }
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
        int n = sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
        if (n < 3) n = sscanf(s, "%d/%d/%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
        if (n >= 3) {
            struct tm tmv; memset(&tmv, 0, sizeof tmv);
            tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
            tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = se;
            time_t r = timegm(&tmv);
            snprintf(out, outcap, "%ld", (long)r);
        } else {
            out[0] = 0;
        }
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
    if (strcmp(name, "preg_replace_callback") == 0 && nargs >= 3) {
        /* Real preg_replace_callback($pattern, $callback, $subject)
         * would need to actually invoke $callback (a callable, itself
         * not representable as one of this file's own flat-string args
         * -- see call_user_func()'s own documented scope limit) once per
         * match, passing it a real $matches array WITH capture groups
         * (this engine's own regex_search()/RToken have no group-
         * tracking at all -- see preg_match()'s own comment on the
         * identical, pre-existing "no &$matches support" limit) --
         * genuinely out of scope for this subset's regex engine as it
         * stands. Rather than leave this entirely UNIMPLEMENTED (falling
         * through to this file's generic "unknown function -> empty
         * string" default, which is actively WORSE than a no-op: it
         * unconditionally WIPES the subject to "" regardless of whether
         * there was ever a real match to replace), this degrades to a
         * genuine no-op -- return the subject unchanged -- which is
         * correct for the overwhelmingly common real case (no match at
         * all) and merely incomplete (not wrong) for the rarer case
         * where a real match existed and should have been transformed.
         * Confirmed as a real, high-impact bug this session:
         * wp_kses_normalize_entities() (itself reached from
         * esc_url()'s own "replace ampersands... only when displaying"
         * step) calls this 2-3 times on every escaped URL, and the OLD
         * "wipe to empty" behavior fired regardless of whether the URL
         * contained any HTML entity references at all -- silently
         * blanking every href/src attribute site-wide, the last of a
         * long chain of bugs (variadic methods, list() destructuring,
         * "??", single-quoted "\\'" string escapes, regex character-class
         * escapes) found this session while chasing why wp-login.php's
         * own CSS never rendered. */
        strncpy(out, args[2], outcap - 1); out[outcap - 1] = 0;
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
    if (strcmp(name, "basename") == 0 && nargs >= 1) {
        /* Previously entirely UNIMPLEMENTED -- found in this session's
         * own audit of real PHP builtins the vendored WordPress tree
         * actually calls vs. what this file implements (50 real call
         * sites in the audited source, dirname()'s own long-implemented
         * sibling). Optional 2nd $suffix arg strips a trailing suffix
         * (e.g. basename($path, '.php')), matching real PHP. */
        const char *p = args[0];
        int len = (int)strlen(p);
        while (len > 1 && p[len - 1] == '/') len--; /* trailing slashes stripped, matching real basename() */
        const char *slash = NULL;
        { int i; for (i = len - 1; i >= 0; i--) if (p[i] == '/') { slash = p + i; break; } }
        const char *base = slash ? slash + 1 : p;
        int blen = (int)(p + len - base);
        if (nargs >= 2) {
            int suflen = (int)strlen(args[1]);
            if (suflen > 0 && suflen < blen && strncmp(base + blen - suflen, args[1], (size_t)suflen) == 0) blen -= suflen;
        }
        if (blen >= outcap) blen = outcap - 1;
        memcpy(out, base, (size_t)blen); out[blen] = 0;
        return;
    }
    if (strcmp(name, "pathinfo") == 0 && nargs >= 1) {
        /* Real PHP's own 2nd $flags arg selects just ONE component
         * (PATHINFO_DIRNAME/BASENAME/EXTENSION/FILENAME) as a plain
         * string instead of the full associative array -- this subset
         * has no real predefined PATHINFO_* constants (same story as
         * ENT_QUOTES, see htmlspecialchars()'s own comment), so this
         * always returns the full array; the by-far more common real
         * WordPress call shape. */
        const char *p = args[0];
        const char *slash = strrchr(p, '/');
        const char *base = slash ? slash + 1 : p;
        const char *dot = strrchr(base, '.');
        int id = php_array_new();
        if (id >= 0) {
            char dirname[PHP_VAL_MAX];
            if (slash) { int dl = (int)(slash - p); if (dl == 0) dl = 1; if (dl >= (int)sizeof dirname) dl = (int)sizeof dirname - 1; memcpy(dirname, p, (size_t)dl); dirname[dl] = 0; }
            else strcpy(dirname, ".");
            php_kv_set(&g_arrays[id], "dirname", dirname);
            php_kv_set(&g_arrays[id], "basename", base);
            if (dot && dot != base) { php_kv_set(&g_arrays[id], "extension", dot + 1); char fn[PHP_VAL_MAX]; int fl = (int)(dot - base); if (fl >= (int)sizeof fn) fl = (int)sizeof fn - 1; memcpy(fn, base, (size_t)fl); fn[fl] = 0; php_kv_set(&g_arrays[id], "filename", fn); }
            else php_kv_set(&g_arrays[id], "filename", base);
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if ((strcmp(name, "urlencode") == 0 || strcmp(name, "rawurlencode") == 0) && nargs >= 1) {
        /* urlencode() encodes a space as "+"; rawurlencode() (RFC 3986)
         * encodes it as "%20" -- the one real difference between them. */
        int raw = strcmp(name, "rawurlencode") == 0;
        int o = 0; const char *p;
        for (p = args[0]; *p && o < outcap - 4; p++) {
            unsigned char c = (unsigned char)*p;
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || (raw && c == '~')) out[o++] = (char)c;
            else if (!raw && c == ' ') out[o++] = '+';
            else { snprintf(out + o, 4, "%%%02X", c); o += 3; }
        }
        out[o] = 0;
        return;
    }
    if ((strcmp(name, "urldecode") == 0 || strcmp(name, "rawurldecode") == 0) && nargs >= 1) {
        int raw = strcmp(name, "rawurldecode") == 0;
        int o = 0; const char *p = args[0];
        while (*p && o < outcap - 1) {
            if (*p == '%' && regex_hex_digit(p[1]) >= 0 && regex_hex_digit(p[2]) >= 0) {
                out[o++] = (char)((regex_hex_digit(p[1]) << 4) | regex_hex_digit(p[2])); p += 3;
            } else if (!raw && *p == '+') { out[o++] = ' '; p++; }
            else { out[o++] = *p; p++; }
        }
        out[o] = 0;
        return;
    }
    if (strcmp(name, "parse_url") == 0 && nargs >= 1) {
        /* Real PHP's own 2nd $component arg selects just one piece
         * (PHP_URL_SCHEME/HOST/PORT/...) -- same "no predefined
         * constants for this shape" story, always returns the full
         * array. Hand-written parse (scheme://user:pass@host:port/path
         * ?query#fragment), a real, common, previously-UNIMPLEMENTED
         * builtin (85 real call sites in the audited source -- the
         * single most-used missing builtin found in this session's own
         * audit). */
        const char *p = args[0];
        int id = php_array_new();
        if (id >= 0) {
            const char *scheme_end = strstr(p, "://");
            if (scheme_end) {
                char sc[32]; int sl = (int)(scheme_end - p); if (sl >= (int)sizeof sc) sl = (int)sizeof sc - 1;
                memcpy(sc, p, (size_t)sl); sc[sl] = 0;
                php_kv_set(&g_arrays[id], "scheme", sc);
                p = scheme_end + 3;
            }
            const char *frag = strchr(p, '#');
            if (frag) { php_kv_set(&g_arrays[id], "fragment", frag + 1); }
            const char *authority_end = frag ? frag : p + strlen(p);
            const char *query = memchr(p, '?', (size_t)(authority_end - p));
            if (query) { char qs[PHP_VAL_MAX]; int ql = (int)(authority_end - query - 1); if (ql >= (int)sizeof qs) ql = (int)sizeof qs - 1; if (ql > 0) memcpy(qs, query + 1, (size_t)ql); qs[ql > 0 ? ql : 0] = 0; php_kv_set(&g_arrays[id], "query", qs); authority_end = query; }
            const char *path_start = memchr(p, '/', (size_t)(authority_end - p));
            const char *hostport_end = path_start ? path_start : authority_end;
            if (path_start && path_start < authority_end) { char path[PHP_VAL_MAX]; int pl = (int)(authority_end - path_start); if (pl >= (int)sizeof path) pl = (int)sizeof path - 1; memcpy(path, path_start, (size_t)pl); path[pl] = 0; php_kv_set(&g_arrays[id], "path", path); }
            const char *at = memchr(p, '@', (size_t)(hostport_end - p));
            const char *host_start = at ? at + 1 : p;
            if (at) { char userinfo[128]; int ul = (int)(at - p); if (ul >= (int)sizeof userinfo) ul = (int)sizeof userinfo - 1; memcpy(userinfo, p, (size_t)ul); userinfo[ul] = 0; char *colon = strchr(userinfo, ':'); if (colon) { *colon = 0; php_kv_set(&g_arrays[id], "user", userinfo); php_kv_set(&g_arrays[id], "pass", colon + 1); } else php_kv_set(&g_arrays[id], "user", userinfo); }
            const char *portcolon = memchr(host_start, ':', (size_t)(hostport_end - host_start));
            const char *host_end = portcolon ? portcolon : hostport_end;
            if (host_end > host_start) { char host[256]; int hl = (int)(host_end - host_start); if (hl >= (int)sizeof host) hl = (int)sizeof host - 1; memcpy(host, host_start, (size_t)hl); host[hl] = 0; php_kv_set(&g_arrays[id], "host", host); }
            if (portcolon) { char port[16]; int pl = (int)(hostport_end - portcolon - 1); if (pl >= (int)sizeof port) pl = (int)sizeof port - 1; if (pl > 0) memcpy(port, portcolon + 1, (size_t)pl); port[pl > 0 ? pl : 0] = 0; if (port[0]) php_kv_set(&g_arrays[id], "port", port); }
            php_arrref_encode(id, out, outcap);
        } else out[0] = 0;
        return;
    }
    if (strcmp(name, "http_build_query") == 0 && nargs >= 1) {
        int aid = php_arrref_decode(args[0]);
        int o = 0;
        if (aid >= 0) {
            int i;
            for (i = 0; i < g_arrays[aid].count; i++) {
                if (o > 0 && o < outcap - 1) out[o++] = '&';
                char enc_k[256], enc_v[PHP_VAL_MAX];
                char *ka[1]; ka[0] = g_arrays[aid].items[i].key;
                php_call_function(caller, "urlencode", ka, 1, enc_k, sizeof enc_k);
                char *va[1]; va[0] = g_arrays[aid].items[i].val;
                php_call_function(caller, "urlencode", va, 1, enc_v, sizeof enc_v);
                int n = snprintf(out + o, (size_t)(outcap - o), "%s=%s", enc_k, enc_v);
                if (n > 0) o += n;
                if (o >= outcap - 1) break;
            }
        }
        out[o] = 0;
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
    if (strcmp(name, "__db_last_result_as_arrays") == 0) {
        /* Real wpdb::get_results($query, $output)/get_row(...) support an
         * ARRAY_A output mode (associative-array rows, not stdClass
         * objects) -- this project's own class-wpdb.php previously
         * IGNORED `$output` entirely, always returning the same object
         * rows __db_query() already built. A real, high-impact bug:
         * wp-includes/meta.php's own update_meta_cache() (backing EVERY
         * get_user_meta()/get_post_meta()/etc call) explicitly requests
         * "$wpdb->get_results(..., ARRAY_A)" then reads
         * "$metarow[$column]"/"$metarow['meta_key']" -- bracket access on
         * what was actually still an OBJECT silently read as empty
         * (php_arrref_decode() returns -1 for an object reference, so
         * this file's own "$var[key]" read path degrades to ""), so the
         * entire per-row cache-population loop silently did nothing
         * useful, which was the direct cause of session persistence
         * (get_user_meta($uid, 'session_tokens', true)) always coming
         * back empty even once the row itself was stored and fetched
         * correctly by the underlying SQL. Re-parses the ALREADY-cached
         * g_db_last_raw (the same packed result the just-run query()
         * call populated) into real PhpKVArray rows instead of stdClass
         * objects -- same packed-format parsing as __db_query() itself,
         * just building an array per row instead of an object. */
        int arr_id = php_array_new();
        if (arr_id < 0) { out[0] = 0; return; }
        char cols[PHP_KV_MAX][128];
        int ncols = 0;
        const char *p = g_db_last_raw;
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
            int row_aid = php_array_new();
            if (row_aid >= 0) {
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
                    php_kv_set(&g_arrays[row_aid], cols[ci], unesc);
                    ci++;
                    cp = tab ? tab + 1 : cend;
                }
                char enc[32], idxbuf[16];
                php_arrref_encode(row_aid, enc, sizeof enc);
                snprintf(idxbuf, sizeof idxbuf, "%d", rowidx);
                php_kv_set(&g_arrays[arr_id], idxbuf, enc);
                rowidx++;
            }
            if (!lend) break;
            p = lend + 1;
        }
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
        /* Same escaping as php_db_sql_quote() (see its own comment on
         * why backslash is no longer doubled, now that db_engine.c is
         * real SQLite), minus the surrounding quotes -- real
         * wpdb::escape()/_real_escape() return the escaped text
         * unquoted; callers wrap it in quotes themselves. */
        int o = 0;
        const char *p;
        for (p = args[0]; *p && o < outcap - 2; p++) {
            if (*p == '\'') out[o++] = '\'';
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
    if (g_call_depth > 100 && getenv("SQS_TRACE_DEEP")) fprintf(stderr, "[DEEP %d] %s()\n", g_call_depth, name);
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
    callee->cookie = caller->cookie;
    callee->post = caller->post;
    strncpy(callee->server_method, caller->server_method, sizeof callee->server_method - 1);
    callee->out = caller->out;
    callee->headers_buf = caller->headers_buf; callee->headers_cap = caller->headers_cap; callee->headers_len = caller->headers_len;
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
            php_var_set(callee, fn->params[i], i < nargs ? args[i] : fn->param_defaults[i]);
        }
    }
    callee->src = fn->body;
    php_run_statements(callee);
    caller->out_len = callee->out_len; /* propagate anything the function echoed */
    caller->headers_len = callee->headers_len;
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
    if (g_suppress_calls > 0) { out[0] = 0; return; } /* short-circuited "&&"/"||" -- see g_suppress_calls's own comment */
    /* See php_call_function's own comment on g_call_depth -- same
     * runaway-recursion safety net, needed here too since the actual
     * crashing chain found this session (_wp_get_current_user() ->
     * wp_set_current_user() -> setup_userdata() -> get_current_user_id())
     * is a mix of plain function AND method calls. */
    if (g_call_depth >= PHP_CALL_DEPTH_MAX) {
        if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[CALL DEPTH LIMIT] ->%s() at depth %d\n", m->name, g_call_depth);
        out[0] = 0; return;
    }
    if (g_call_depth > 100 && getenv("SQS_TRACE_DEEP")) fprintf(stderr, "[DEEP %d] ->%s()\n", g_call_depth, m->name);
    g_call_depth++;
    /* Heap-allocated -- see php_call_function's own comment on why a
     * stack-local PhpState here is a real stack-overflow risk now that
     * arrays exist. */
    PhpState *callee = (PhpState *)malloc(sizeof *callee);
    memset(callee, 0, sizeof *callee);
    callee->get = caller->get;
    callee->cookie = caller->cookie;
    callee->post = caller->post;
    strncpy(callee->server_method, caller->server_method, sizeof callee->server_method - 1);
    callee->out = caller->out;
    callee->headers_buf = caller->headers_buf; callee->headers_cap = caller->headers_cap; callee->headers_len = caller->headers_len;
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
        if (m->variadic[i]) {
            /* "...$name" -- see PhpFunc.variadic's/PhpMethod.variadic's
             * own comments; same real-array-collection logic
             * php_call_function() already has for a plain function,
             * mirrored here so a METHOD/constructor's own variadic
             * parameter (e.g. _WP_Dependency::__construct(...$args))
             * gets a real array too, not an always-empty one. */
            PhpVar *v = php_var_find_or_create(callee, m->params[i]);
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
            php_var_set(callee, m->params[i], i < nargs ? args[i] : m->param_defaults[i]);
        }
    }
    callee->src = m->body;
    php_run_statements(callee);
    caller->out_len = callee->out_len;
    caller->headers_len = callee->headers_len;
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
    if (g_suppress_calls > 0) { out[0] = 0; return; } /* short-circuited "&&"/"||" -- see g_suppress_calls's own comment */
    /* See php_call_function's own comment on g_call_depth. */
    if (g_call_depth >= PHP_CALL_DEPTH_MAX) {
        if (getenv("SQS_TRACE_CALLS")) fprintf(stderr, "[CALL DEPTH LIMIT] ::%s() at depth %d\n", m->name, g_call_depth);
        out[0] = 0; return;
    }
    if (g_call_depth > 100 && getenv("SQS_TRACE_DEEP")) fprintf(stderr, "[DEEP %d] ::%s()\n", g_call_depth, m->name);
    g_call_depth++;
    PhpState *callee = (PhpState *)malloc(sizeof *callee);
    memset(callee, 0, sizeof *callee);
    callee->get = caller->get;
    callee->cookie = caller->cookie;
    callee->post = caller->post;
    strncpy(callee->server_method, caller->server_method, sizeof callee->server_method - 1);
    callee->out = caller->out;
    callee->headers_buf = caller->headers_buf; callee->headers_cap = caller->headers_cap; callee->headers_len = caller->headers_len;
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
        if (m->variadic[i]) {
            PhpVar *v = php_var_find_or_create(callee, m->params[i]);
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
            php_var_set(callee, m->params[i], i < nargs ? args[i] : m->param_defaults[i]);
        }
    }
    callee->src = m->body;
    php_run_statements(callee);
    caller->out_len = callee->out_len;
    caller->headers_len = callee->headers_len;
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
static void php_run(const char *source, const char *file_path, PhpKVArray *get, PhpKVArray *post, PhpKVArray *cookie, const char *method,
                     char *out, int outcap, char *headers_out, int headers_out_cap) {
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
    st->cookie = *cookie;
    strncpy(st->server_method, method, sizeof st->server_method - 1);
    st->out = out;
    st->out_len = 0;
    st->out_cap = outcap;
    out[0] = 0;
    st->headers_buf = headers_out;
    st->headers_len = 0;
    st->headers_cap = headers_out_cap;
    if (headers_out) headers_out[0] = 0;
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
    php_kv_free(&st->cookie);
    free(st);
}
