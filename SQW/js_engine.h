#ifndef SQW_JS_ENGINE_H
#define SQW_JS_ENGINE_H
#include "dom.h"

/* A genuinely minimal JavaScript engine: real lexer -> real recursive-
 * descent parser -> real tree-walking interpreter, deliberately scoped to
 * a small subset -- same "real but bounded" convention as css.c and
 * SQS/php_mini.c (see each of their own top comments). This is NOT a
 * spec-compliant JS implementation.
 *
 * Supported: var/let/const (all function-scope-free -- treated as plain
 * block-scoped declarations, a simplification), function declarations and
 * expressions (real closures over their defining scope), if/else,
 * while, C-style for(init;cond;incr), return/break/continue, blocks,
 * numbers (real IEEE double), strings (single/double-quoted, backslash
 * escapes for \\ \" \' \n \t), booleans, null/undefined, arithmetic
 * (+ - * / % including string "+" concatenation), comparison
 * (< > <= >= == != === !==, == and === treated identically -- no real
 * type-coercion table), logical (&& || ! with real short-circuiting),
 * ternary ?:, assignment (= += -= *= /=), member access (a.b),
 * computed index (a[b]), function calls, and a small set of DOM/console
 * builtins (see js_install_builtins() below).
 *
 * NOT supported: arrays/object literals, template literals, arrow
 * functions, classes, try/catch, for-in/for-of, destructuring, spread,
 * async/await, regex, switch, the "new"/"this" keywords, prototypes.
 * Any of these appearing in a script either fails to parse (the whole
 * script is then skipped, with a stderr diagnostic -- never a partial/
 * silently-wrong execution) or, for a few narrow cases inside an
 * otherwise-parseable script, evaluates to `undefined` -- never crashes.
 */

#define JS_MAX_PROPS 32
#define JS_MAX_PARAMS 8
#define JS_MAX_ARGS 8
#define JS_IDENT_MAX 64
#define JS_STR_MAX 1024
#define JS_MAX_TIMERS 32
#define JS_MAX_FETCHES 16

typedef enum {
    JSV_UNDEFINED = 0, JSV_NULL, JSV_BOOL, JSV_NUMBER, JSV_STRING, JSV_OBJECT
} JSValueType;

typedef struct JSObject JSObject;

/* Plain struct, not a C union -- deliberately, see js_engine.c's own top
 * comment for why (a real squash codegen risk this project has hit more
 * than once with less-common language features; a union field's own
 * storage-overlap semantics is exactly the kind of thing not worth
 * risking untested here). A little wasteful, never wrong. */
typedef struct {
    JSValueType type;
    double num;
    char *str;      /* heap, owned; NULL unless type==JSV_STRING */
    int boolean;
    JSObject *obj;  /* NULL unless type==JSV_OBJECT */
} JSValue;

/* No public JSValue constructors -- js_engine.c's own top-of-Values-
 * section comment explains why: a real squash codegen bug makes passing
 * OR returning a multi-field struct like JSValue BY VALUE across a
 * function call boundary unreliable, so nothing in this engine ever does
 * that; every JSValue is built in place through an out-pointer instead.
 * Nothing outside js_engine.c needs to construct one directly. */

/* Runs `src` (a <script> tag's text content, or any other JS source)
 * against `document_root` -- installs document/console/Math/etc builtins
 * bound to that DOM tree, then parses and executes `src` as a top-level
 * program. Returns the interpreter (heap-allocated) so the CALLER can
 * keep it alive for the rest of the page's lifetime (needed for
 * addEventListener/onclick handlers registered during this run to still
 * be callable later, from js_dispatch_click() -- see its own comment) --
 * the caller owns it and must eventually js_interp_free() it. Returns
 * NULL if `src` failed to parse (a diagnostic is printed to stderr;
 * nothing from the script runs). `relayout_needed` is set to 1 if the
 * script mutated anything that affects layout (innerHTML/textContent/
 * appendChild-equivalent, style.* affecting box-model properties) so the
 * caller knows whether to re-run layout_compute() afterward -- always
 * conservatively set to 1 by any DOM-mutating builtin, even ones that
 * only touch paint-only properties (color, background), since this
 * engine has no fine-grained "does this property affect layout" table
 * (real browsers do; out of scope here) and a spurious relayout is cheap
 * and always correct, just not maximally efficient. */
typedef struct JSInterp JSInterp;
JSInterp *js_run_script(const char *src, DomNode *document_root, int *relayout_needed);
void js_interp_free(JSInterp *interp);

/* Calls the given DOM node's own onclick handler (set via either the
 * "onclick" HTML attribute -- parsed as a tiny implicit function body,
 * see js_engine.c's own comment -- or a real addEventListener('click',
 * fn)/.onclick = fn assignment during a previous js_run_script() call),
 * if any. `interp` is whichever JSInterp is currently alive for the page
 * (NULL if no <script> ever ran, in which case this is a cheap no-op).
 * Sets *relayout_needed the same way js_run_script() does. Returns 1 if a
 * handler was found and called, 0 otherwise (no handler registered --
 * NOT an error). */
int js_dispatch_click(JSInterp *interp, DomNode *node, int *relayout_needed);

/* Runs every due setTimeout()/setInterval() callback -- see js_engine.c's
 * own comment on js_run_timers() for exactly how "due" is decided. This
 * engine has no event loop/clock of its own (a tree-walking interpreter
 * with no async machinery at all -- see this header's own top comment on
 * scope), so the HOST render loop must call this once per frame with the
 * current wall-clock time in milliseconds (SDL_GetTicks(), as a double)
 * for setTimeout/setInterval to have any effect at all; never calling it
 * leaves every timer registered but permanently un-fired, not an error. */
void js_run_timers(JSInterp *interp, double now_ms, int *relayout_needed);

/* fetch(url, callback) -- a minimal, callback-style (NOT a real Promise;
 * no .then()/.catch() chaining -- see this header's own top comment on
 * scope) network fetch. Wired to the host's real async HTTP client
 * (SQW/net_client.c) through this decoupled hook pair rather than
 * js_engine.c #include-ing net_client.h directly -- the same "host sets
 * a function pointer, this file stays independent of the host's own
 * headers" pattern layout.c's own layout_set_image_size_lookup() already
 * uses, and for the same reason: this file's own standalone test harness
 * (SQW/tests/test_js.c) has no network stack linked in at all. */
typedef void (*JsFetchStartFn)(const char *url, long fetch_id, void *user_data);
void js_set_fetch_hook(JsFetchStartFn fn, void *user_data);

/* Called by the host once a fetch it started (via the hook above) has
 * completed -- looks up whichever JS callback `interp` has registered
 * for `fetch_id` (a no-op, not an error, if none is found -- e.g. the
 * page navigated away and a fresh JSInterp with no memory of that id is
 * now current) and calls it with `body` ("" on failure) and `success`. */
void js_deliver_fetch_result(JSInterp *interp, long fetch_id, const char *body, int success, int *relayout_needed);

#endif /* SQW_JS_ENGINE_H */
