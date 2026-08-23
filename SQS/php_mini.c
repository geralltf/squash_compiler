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
#define PHP_CONST_MAX 512
#define PHP_FUNC_MAX 1024
#define PHP_FUNC_PARAM_MAX 8
#define PHP_INCLUDED_MAX 512
#define PHP_BUF_MAX 512
#define PHP_ARG_MAX 8
#define PHP_PATH_MAX 512

typedef struct {
    char key[128];
    char val[PHP_VAL_MAX];
} PhpKV;

typedef struct {
    PhpKV items[PHP_KV_MAX];
    int count;
} PhpKVArray;

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
#define PHP_OBJ_MAX 256

typedef struct {
    char name[64];
    char params[PHP_FUNC_PARAM_MAX][64];
    int nparams;
    const char *body; /* see PhpFunc's own comment -- same convention */
} PhpMethod;

typedef struct {
    char name[64];
    char prop_names[PHP_CLASS_PROP_MAX][64];
    char prop_defaults[PHP_CLASS_PROP_MAX][PHP_VAL_MAX];
    int nprops;
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
#define PHP_ARR_MAX 256
static PhpKVArray g_arrays[PHP_ARR_MAX];
static int g_arr_alive[PHP_ARR_MAX];
static int g_narrays = 0;

/* Error text from the most recent __db_exec()/__db_query() call, read by
 * the $wpdb-compatible class's own last_error handling -- request-
 * lifetime, like every other g_* table on this page, reset alongside
 * them. */
static char g_db_last_error[512] = "";
/* Raw packed text from the most recent successful __db_query() (see
 * db_engine.c's own "PACKED ROW FORMAT" comment) -- cached so
 * __db_get_var()/__db_get_col() can answer "row R, column C" directly
 * without needing PHP-level introspection of an object's property names
 * (this interpreter subset has no get_object_vars()/reflection, and
 * real wpdb's get_var()/get_col() are column-POSITION-based, not
 * name-based, so this is enough). */
static char g_db_last_raw[PHP_OUT_MAX] = "";

static void php_globals_reset(void) {
    int i;
    g_nconsts = 0;
    g_nfuncs = 0;
    g_nincluded = 0;
    for (i = 0; i < g_nbufs; i++) free(g_bufs[i]);
    g_nbufs = 0;
    g_nclasses = 0;
    for (i = 0; i < PHP_OBJ_MAX; i++) g_objects[i].alive = 0;
    g_nobjects = 0;
    for (i = 0; i < PHP_ARR_MAX; i++) g_arr_alive[i] = 0;
    g_narrays = 0;
    g_db_last_error[0] = 0;
    g_db_last_raw[0] = 0;
}

static PhpClass *php_class_find(const char *name) {
    int i;
    for (i = 0; i < g_nclasses; i++) if (strcmp(g_classes[i].name, name) == 0) return &g_classes[i];
    return NULL;
}
static PhpMethod *php_class_find_method(PhpClass *cls, const char *name) {
    int i;
    if (!cls) return NULL;
    for (i = 0; i < cls->nmethods; i++) if (strcmp(cls->methods[i].name, name) == 0) return &cls->methods[i];
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
static PhpFunc *php_func_find(const char *name) {
    int i;
    for (i = 0; i < g_nfuncs; i++) if (strcmp(g_funcs[i].name, name) == 0) return &g_funcs[i];
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

    int has_this;   /* set on the callee PhpState php_call_function()
                        builds for a "$obj->method(...)" call -- "$this"
                        then reads as an object reference to this id,
                        same representation php_var_find()'s own "is
                        this var an object" check uses everywhere else
                        (see PhpVar's own comment) */
    int this_obj_id;
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
    if (arr->count >= PHP_KV_MAX) return;
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
    int slot = -1, i;
    for (i = 0; i < PHP_ARR_MAX; i++) if (!g_arr_alive[i]) { slot = i; break; }
    if (slot < 0) return -1;
    memset(&g_arrays[slot], 0, sizeof g_arrays[slot]);
    g_arr_alive[slot] = 1;
    if (slot >= g_narrays) g_narrays = slot + 1;
    return slot;
}

/* Allocates a fresh object of class `class_name`, seeding its properties
 * from that class's own declared defaults (real PHP semantics: an
 * object starts with each property at its class-declared default, not
 * empty). Returns the new object's id (index into g_objects), or -1 if
 * the class is unknown or the object table is full. */
static int php_object_new(const char *class_name) {
    int slot = -1, i;
    for (i = 0; i < PHP_OBJ_MAX; i++) if (!g_objects[i].alive) { slot = i; break; }
    if (slot < 0) return -1;
    PhpObject *o = &g_objects[slot];
    memset(o, 0, sizeof *o);
    o->alive = 1;
    strncpy(o->class_name, class_name, sizeof o->class_name - 1); o->class_name[sizeof o->class_name - 1] = 0;
    if (slot >= g_nobjects) g_nobjects = slot + 1;
    PhpClass *cls = php_class_find(class_name);
    if (cls) {
        for (i = 0; i < cls->nprops; i++) php_kv_set(&o->props, cls->prop_names[i], cls->prop_defaults[i]);
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

static PhpVar *php_var_find(PhpState *st, const char *name) {
    int i;
    for (i = 0; i < st->nvars; i++) if (strcmp(st->vars[i].name, name) == 0) return &st->vars[i];
    return NULL;
}
/* Finds `name`, creating a fresh (scalar, empty-string, non-array) var
 * for it if it doesn't exist yet -- used by array-element assignment
 * ("$arr['x'] = 1;" on a not-yet-seen $arr) so it doesn't need its own
 * separate create-if-missing logic. */
static PhpVar *php_var_find_or_create(PhpState *st, const char *name) {
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
    v->is_array = 0; /* reassigning a plain "$x = expr;" always makes it
                         scalar again, even if it used to hold an array
                         (matches real PHP: a variable's "type" is just
                         whatever its last assignment made it) */
    strncpy(v->val, val, sizeof v->val - 1); v->val[sizeof v->val - 1] = 0;
}

static int php_truthy(const char *s) { return s[0] != 0 && strcmp(s, "0") != 0; }

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

/* Reads a single-or-double-quoted string literal (the opening quote is
 * already known to be at *st->src). Only supports \\, \", \', \n, \t
 * escapes inside double quotes -- single-quoted strings are literal
 * (real PHP's own distinction, kept here too). */
static void php_read_string_lit(PhpState *st, char *buf, int bufcap) {
    const char *p = st->src;
    char q = *p; p++;
    int i = 0;
    char c = *p;
    while (c && c != q) {
        if (q == '"' && c == '\\' && p[1]) {
            p++;
            char e = *p;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else c = e;
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
    if (c == '-' && st->src[1] == '>') {
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
        int truthy = inner[0] != 0 && strcmp(inner, "0") != 0;
        strncpy(out, truthy ? "0" : "1", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (c == '"' || c == '\'') {
        php_read_string_lit(st, out, outcap);
        return;
    }
    if (c == '(') {
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
                        else cur[0] = 0;
                    } else cur[0] = 0;
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


/* cmp := addsub (("=="|"!="|"<="|">="|"<"|">") addsub)? -- numeric
 * comparison for "<"/">"/"<="/">=" (real PHP does looser type-juggling
 * comparisons than this; not modeled), string equality for "=="/"!=". */
static void php_eval_cmp(PhpState *st, char *out, int outcap) {
    char lhs[PHP_VAL_MAX];
    php_eval_addsub(st, lhs, sizeof lhs);
    php_skip_ws(st);
    char c0 = st->src[0], c1 = st->src[1];
    int result;
    int matched = 1;
    if (c0 == '=' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_addsub(st, rhs, sizeof rhs);
        result = strcmp(lhs, rhs) == 0;
    } else if (c0 == '!' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_addsub(st, rhs, sizeof rhs);
        result = strcmp(lhs, rhs) != 0;
    } else if (c0 == '<' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_addsub(st, rhs, sizeof rhs);
        result = php_to_num(lhs) <= php_to_num(rhs);
    } else if (c0 == '>' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_addsub(st, rhs, sizeof rhs);
        result = php_to_num(lhs) >= php_to_num(rhs);
    } else if (c0 == '<') {
        st->src += 1;
        char rhs[PHP_VAL_MAX];
        php_eval_addsub(st, rhs, sizeof rhs);
        result = php_to_num(lhs) < php_to_num(rhs);
    } else if (c0 == '>') {
        st->src += 1;
        char rhs[PHP_VAL_MAX];
        php_eval_addsub(st, rhs, sizeof rhs);
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
static void php_eval_expr(PhpState *st, char *out, int outcap) {
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
        return;
    }
    strncpy(out, cond, outcap - 1); out[outcap - 1] = 0;
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
    for (;;) {
        const char *save = st->src;
        char kw2[16];
        php_read_ident(st, kw2, sizeof kw2);
        if (strcmp(kw2, "extends") == 0 || strcmp(kw2, "implements") == 0) {
            php_skip_ws(st);
            for (;;) {
                char tmp[64];
                php_read_ident(st, tmp, sizeof tmp);
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
        /* Unrecognized member (const, use-trait, ...): skip to its own
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
void php_run_source(PhpState *st, const char *source); /* forward: require/include recurse into this */

/* One statement: "$var = expr;" | "echo expr (, expr)* ;" | "print expr ;"
 * | "if (cond) { stmts } [else { stmts }]" (see this file's top comment:
 * the if/else body must stay inside one continuous php block, no
 * exiting/re-entering "<?php"/"?>" mid-block) | "function name($a, $b) {
 * ... }" | "return [expr];" | "require[_once]/include[_once] expr;" | a
 * bare function-call expression statement, e.g. "some_func();". */
static void php_run_statement(PhpState *st) {
    php_skip_ws(st);
    char c = *st->src;
    if (c == '$') {
        const char *save = st->src;
        st->src++;
        char name[64];
        php_read_ident(st, name, sizeof name);
        php_skip_ws(st);
        c = *st->src;

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
                if (c == '$') {
                    st->src++;
                    char pname[64];
                    php_read_ident(st, pname, sizeof pname);
                    if (fn && nparams < PHP_FUNC_PARAM_MAX) {
                        strncpy(fn->params[nparams], pname, sizeof fn->params[nparams] - 1);
                        fn->params[nparams][sizeof fn->params[nparams] - 1] = 0;
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
                g_nfuncs++;
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
                    if (st->returning) {
                        php_skip_to_brace_close(st);
                        c = *st->src;
                        if (c == '}') st->src++;
                        return; /* don't try to parse a trailing
                                   elseif/else past a return -- matches
                                   how php_run_statements itself stops */
                    }
                } else {
                    php_skip_to_brace_close(st);
                }
                c = *st->src;
                if (c == '}') st->src++;
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
                        if (st->returning) {
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
    if (strcmp(kw, "interface") == 0 || strcmp(kw, "trait") == 0 ||
        strcmp(kw, "enum") == 0 || strcmp(kw, "namespace") == 0 || strcmp(kw, "use") == 0 ||
        strcmp(kw, "try") == 0 || strcmp(kw, "catch") == 0 || strcmp(kw, "finally") == 0 ||
        strcmp(kw, "switch") == 0 || strcmp(kw, "foreach") == 0 || strcmp(kw, "for") == 0 ||
        strcmp(kw, "while") == 0 || strcmp(kw, "do") == 0 || strcmp(kw, "global") == 0 ||
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
        if (st->returning) return;
        if (st->src == before) st->src++;
    }
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
            static const char *builtins[] = {
                "define", "defined", "function_exists", "sprintf", "printf",
                "file_exists", "is_dir", "strlen", "dirname", NULL
            };
            int i;
            for (i = 0; builtins[i]; i++) if (strcmp(args[0], builtins[i]) == 0) { found = 1; break; }
        }
        strncpy(out, found ? "1" : "0", outcap - 1); out[outcap - 1] = 0;
        return;
    }
    if (strcmp(name, "call_user_func") == 0 && nargs >= 1) {
        /* Only the "plain function name string" callable form is
         * supported (e.g. call_user_func('some_func', $a, $b)) -- real
         * PHP also allows [$obj, 'method'] / [$class, 'method'] /
         * 'Class::method' array/string forms, none of which mean
         * anything here since this subset has no classes. This alone is
         * still a meaningful, high-leverage addition: real WordPress
         * routes essentially all of its hooks/filters dispatch through
         * call_user_func()-shaped calls (confirmed this session -- even
         * wp_die() itself, deep in wp-includes/functions.php, resolves
         * its actual handler through one), so without this, that whole
         * dispatch mechanism silently did nothing (an unknown function
         * name just returns "" from the fallback at the bottom of this
         * function, so "call_user_func($callback, ...)" itself looked
         * like a harmless no-op call rather than the wall it actually
         * was). */
        char fwd_storage[PHP_ARG_MAX][PHP_VAL_MAX];
        char *fwd[PHP_ARG_MAX];
        int fi, fn2 = 0;
        for (fi = 0; fi < PHP_ARG_MAX; fi++) fwd[fi] = fwd_storage[fi];
        for (fi = 1; fi < nargs && fn2 < PHP_ARG_MAX; fi++) {
            strncpy(fwd[fn2], args[fi], PHP_VAL_MAX - 1); fwd[fn2][PHP_VAL_MAX - 1] = 0;
            fn2++;
        }
        php_call_function(caller, args[0], fwd, fn2, out, outcap);
        return;
    }
    if (strcmp(name, "call_user_func_array") == 0 && nargs >= 1) {
        /* Real PHP takes the forwarded arguments as one array; this
         * subset has no arrays, so only the 1-argument form (call the
         * named function with no arguments) is meaningful here -- still
         * enough to unblock the common "call_user_func_array($cb, array())"
         * shape. */
        out[0] = 0;
        php_call_function(caller, args[0], args, 0, out, outcap);
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
        int r = sqdb_exec(args[0], g_db_last_error, sizeof g_db_last_error);
        if (r >= 0) g_db_last_error[0] = 0;
        snprintf(out, outcap, "%d", r);
        return;
    }
    if (strcmp(name, "__db_query") == 0 && nargs >= 1) {
        /* Heap, not a stack local -- PHP_OUT_MAX is 64KB and this file's
         * own convention (see php_call_function's PhpState comment
         * further down) is to keep anything that size off the stack. */
        char *raw = (char *)malloc(PHP_OUT_MAX);
        int r = raw ? sqdb_query(args[0], raw, PHP_OUT_MAX) : -1;
        if (!raw) { snprintf(g_db_last_error, sizeof g_db_last_error, "out of memory"); g_db_last_raw[0] = 0; }
        else if (r < 0) { strncpy(g_db_last_error, raw, sizeof g_db_last_error - 1); g_db_last_error[sizeof g_db_last_error - 1] = 0; g_db_last_raw[0] = 0; }
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
    if (!fn) { out[0] = 0; return; } /* unknown function: no-op, don't crash */

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
    int i;
    for (i = 0; i < fn->nparams; i++) {
        php_var_set(callee, fn->params[i], i < nargs ? args[i] : "");
    }
    callee->src = fn->body;
    php_run_statements(callee);
    caller->out_len = callee->out_len; /* propagate anything the function echoed */
    strncpy(out, callee->returning ? callee->retval : "", outcap - 1); out[outcap - 1] = 0;
    free(callee);
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
    int i;
    for (i = 0; i < m->nparams; i++) {
        php_var_set(callee, m->params[i], i < nargs ? args[i] : "");
    }
    callee->src = m->body;
    php_run_statements(callee);
    caller->out_len = callee->out_len;
    strncpy(out, callee->returning ? callee->retval : "", outcap - 1); out[outcap - 1] = 0;
    free(callee);
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
    free(st);
}
