/* php_mini.c: a genuinely minimal, single-pass interpreter for a SUBSET of
 * PHP -- enough for simple dynamic test pages (variables, string/number
 * literals, "." concatenation, plus/minus/times/divide arithmetic, echo/print, a basic
 * if/else, and the $_GET/$_POST/$_SERVER superglobals) embedded in
 * "<?php ... ?>" blocks within an otherwise-literal HTML file. This is
 * NOT a real PHP implementation -- no functions, no loops, no real arrays
 * beyond the three fixed superglobals, no classes, no includes, and (see
 * php_run_statement's own "if" handling) an if/else body must stay INSIDE
 * one continuous "<?php ... ?>" block -- real PHP's alternate template
 * syntax of exiting to raw HTML and re-entering PHP mid-if/else
 * ("if (...) { ?> html <?php } else { ?> html <?php }") isn't supported,
 * only a plain HTML passthrough between TOP-LEVEL statements is. It
 * exists purely so SQS/sqs_main.c can serve genuinely DYNAMIC local test
 * pages (a value echoed back from a query-string/POST parameter, a simple
 * if/else branch on one) for exercising SQW's HTTP client end to end, per
 * the project brief's own "get a subset of php program language working
 * in sqs for local dynamic web site testing" phrasing -- not a
 * general-purpose scripting engine. #include-d directly into sqs_main.c
 * (single-TU, matching this whole project's established convention).
 *
 * IMPORTANT squash-compiler workaround, applies throughout this file: a
 * real squash codegen bug (confirmed via several minimal standalone
 * repros in the scratchpad, each gcc-compiled control behaving correctly
 * while the squash-compiled build did not) makes comparing a dereferenced
 * POINTER-TYPED STRUCT FIELD accessed through "->" directly against a
 * character literal -- "*st->src == 'X'", "st->src[0] == 'X'", in an if
 * condition, a while condition, anywhere -- unreliable: the comparison
 * can evaluate false even immediately after an adjacent printf of the
 * exact same expression shows the character does match. Reading the
 * value (e.g. into a printf) works fine; WRITING through it (st->src++,
 * st->src = p) works fine; only COMPARING the dereferenced value in place
 * is broken. Wrapping the read in a plain function that returns *st->src
 * does NOT fix it either -- only copying it into a genuine local variable
 * first does. Every function in this file therefore follows one rule:
 * never compare "*st->src" or "st->src[N]" directly -- always assign it
 * to a local char first ("char c = *st->src;" or, for lookahead, a
 * second local), then compare the local. This is a workaround, not a
 * squash compiler fix -- flagged here in case a future session wants to
 * root-cause and fix the real bug in squash's own codegen.c (a plain
 * local pointer, e.g. "const char *p" scanned via "*p"/"p++", is NOT
 * affected -- only the struct-field-via-arrow form is). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PHP_MAX_VARS 64
#define PHP_VAL_MAX 1024
#define PHP_KV_MAX 32
#define PHP_OUT_MAX 65536

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
    char val[PHP_VAL_MAX];
} PhpVar;

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
static void php_var_set(PhpState *st, const char *name, const char *val) {
    PhpVar *v = php_var_find(st, name);
    if (!v) {
        if (st->nvars >= PHP_MAX_VARS) return;
        v = &st->vars[st->nvars++];
        strncpy(v->name, name, sizeof v->name - 1); v->name[sizeof v->name - 1] = 0;
    }
    strncpy(v->val, val, sizeof v->val - 1); v->val[sizeof v->val - 1] = 0;
}

static void php_emit(PhpState *st, const char *s, int len) {
    int room = st->out_cap - st->out_len - 1;
    if (len > room) len = room;
    if (len > 0) { memcpy(st->out + st->out_len, s, (size_t)len); st->out_len += len; st->out[st->out_len] = 0; }
}
static void php_emit_str(PhpState *st, const char *s) { php_emit(st, s, (int)strlen(s)); }

/* Scans on a local `p` (a PLAIN pointer, not a struct field -- see this
 * file's top comment), writes back to st->src once at the end. */
static void php_skip_ws(PhpState *st) {
    const char *p = st->src;
    char c = *p;
    while (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p++; c = *p; }
    st->src = p;
}

/* Reads a bare identifier (variable name after '$', or a keyword like
 * "echo"/"if") into buf. */
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

static void php_eval_expr(PhpState *st, char *out, int outcap);

/* A single value: string/number literal, "$var", or a superglobal array
 * access "$_GET['key']" / "$_POST['key']" / "$_SERVER['key']", or a
 * parenthesized sub-expression. */
static void php_eval_factor(PhpState *st, char *out, int outcap) {
    php_skip_ws(st);
    char c = *st->src;
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
        PhpVar *v = php_var_find(st, name);
        if (v) { strncpy(out, v->val, outcap - 1); out[outcap - 1] = 0; }
        else out[0] = 0;
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

/* expr := term (("."|"+"|"-") term)* -- "." is string concatenation
 * (real PHP's own operator for it), "+"/"-" numeric. */
static void php_eval_expr(PhpState *st, char *out, int outcap) {
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

/* A minimal comparison for "if" conditions: expr (("=="|"!=") expr)? --
 * no &&/||/</> for this first subset, deliberately (see this file's own
 * top comment on scope). Returns 1/0. */
static int php_eval_condition(PhpState *st) {
    char lhs[PHP_VAL_MAX];
    php_eval_expr(st, lhs, sizeof lhs);
    php_skip_ws(st);
    char c0 = st->src[0], c1 = st->src[1];
    if (c0 == '=' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_expr(st, rhs, sizeof rhs);
        return strcmp(lhs, rhs) == 0;
    }
    if (c0 == '!' && c1 == '=') {
        st->src += 2;
        char rhs[PHP_VAL_MAX];
        php_eval_expr(st, rhs, sizeof rhs);
        return strcmp(lhs, rhs) != 0;
    }
    /* Bare truthy check: non-empty and not "0". */
    return lhs[0] != 0 && strcmp(lhs, "0") != 0;
}

static void php_run_statements(PhpState *st); /* forward: if-bodies recurse */

/* One statement: "$var = expr;" | "echo expr (, expr)* ;" | "print expr ;"
 * | "if (cond) { stmts } [else { stmts }]" (see this file's top comment:
 * the if/else body must stay inside one continuous php block, no
 * exiting/re-entering "<?php"/"?>" mid-block). */
static void php_run_statement(PhpState *st) {
    php_skip_ws(st);
    char c = *st->src;
    if (c == '$') {
        const char *save = st->src;
        st->src++;
        char name[64];
        php_read_ident(st, name, sizeof name);
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (c0 == '=' && c1 != '=') {
            st->src++;
            char val[PHP_VAL_MAX];
            php_eval_expr(st, val, sizeof val);
            php_var_set(st, name, val);
        } else {
            st->src = save; /* not actually an assignment -- bail safely */
        }
        php_skip_ws(st);
        c = *st->src;
        if (c == ';') st->src++;
        return;
    }
    char kw[16];
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
    if (strcmp(kw, "if") == 0) {
        php_skip_ws(st);
        int cond = 0;
        c = *st->src;
        if (c == '(') {
            st->src++;
            cond = php_eval_condition(st);
            php_skip_ws(st);
            c = *st->src;
            if (c == ')') st->src++;
        }
        php_skip_ws(st);
        c = *st->src;
        if (c == '{') {
            st->src++;
            if (cond) php_run_statements(st);
            else {
                /* Skip the true-branch body without executing it (still
                 * need to advance st->src past it correctly, including any
                 * nested braces, so parsing of "else"/what follows stays
                 * in sync). Scans on a local `p`, see this file's top
                 * comment. */
                const char *p = st->src;
                int depth = 1;
                char pc = *p;
                while (pc && depth > 0) {
                    if (pc == '{') depth++;
                    else if (pc == '}') depth--;
                    if (depth > 0) { p++; pc = *p; }
                }
                st->src = p;
            }
            c = *st->src;
            if (c == '}') st->src++;
        }
        php_skip_ws(st);
        {
            const char *save = st->src;
            char kw2[16];
            php_read_ident(st, kw2, sizeof kw2);
            if (strcmp(kw2, "else") == 0) {
                php_skip_ws(st);
                c = *st->src;
                if (c == '{') {
                    st->src++;
                    if (!cond) php_run_statements(st);
                    else {
                        const char *p = st->src;
                        int depth = 1;
                        char pc = *p;
                        while (pc && depth > 0) {
                            if (pc == '{') depth++;
                            else if (pc == '}') depth--;
                            if (depth > 0) { p++; pc = *p; }
                        }
                        st->src = p;
                    }
                    c = *st->src;
                    if (c == '}') st->src++;
                }
            } else {
                st->src = save;
            }
        }
        return;
    }
    /* Unrecognized statement keyword (anything outside this subset --
     * functions, loops, includes, ...): skip to the next ';' or '}' so a
     * script using an unsupported feature degrades to "that one line does
     * nothing" instead of hanging or corrupting the parse position. Scans
     * on a local `p`, see this file's top comment. */
    {
        const char *p = st->src;
        char pc = *p;
        while (pc && pc != ';' && pc != '}') { p++; pc = *p; }
        if (pc == ';') p++;
        st->src = p;
    }
}

/* Runs statements until a "?>" or an unmatched "}" (the caller -- either
 * the top-level php_run() or an if/else body -- is responsible for
 * consuming the terminator itself). */
static void php_run_statements(PhpState *st) {
    for (;;) {
        php_skip_ws(st);
        char c0 = st->src[0], c1 = st->src[1];
        if (!c0) return;
        if (c0 == '?' && c1 == '>') return;
        if (c0 == '}') return;
        php_run_statement(st);
    }
}

/* Runs a whole .php source file: literal HTML outside "<?php"/"<?" ...
 * "?>" is copied straight to the output buffer; everything inside those
 * tags is interpreted per the grammar above. Scans on a local `p`
 * (written into st.src before each nested call, reloaded after), see
 * this file's top comment. */
static void php_run(const char *source, PhpKVArray *get, PhpKVArray *post, const char *method,
                     char *out, int outcap) {
    PhpState st;
    memset(&st, 0, sizeof st);
    st.get = *get;
    st.post = *post;
    strncpy(st.server_method, method, sizeof st.server_method - 1);
    st.src = source;
    st.out = out;
    st.out_len = 0;
    st.out_cap = outcap;
    out[0] = 0;

    const char *p = st.src;
    for (;;) {
        char pc = *p;
        if (!pc) break;
        const char *open_long = strstr(p, "<?php");
        const char *open_short = strstr(p, "<?");
        const char *open = open_long ? open_long : open_short;
        if (open && open_short && (!open_long || open_short < open_long)) open = open_short;
        if (!open) { st.src = p; php_emit_str(&st, p); break; }
        st.src = p;
        php_emit(&st, p, (int)(open - p));
        p = open;
        if (strncmp(p, "<?php", 5) == 0) p += 5;
        else p += 2; /* "<?" short tag */
        st.src = p;
        php_run_statements(&st);
        p = st.src;
        char c0 = p[0], c1 = p[1];
        if (c0 == '?' && c1 == '>') p += 2;
    }
}
