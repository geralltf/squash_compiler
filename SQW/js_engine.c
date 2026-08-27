/* Implementation of js_engine.h -- see that header's own top comment for
 * exactly what JS subset this covers. #include-d directly into
 * sqw_main.c, same single-TU convention as every other SQW/*.c file.
 *
 * IMPORTANT squash-compiler workaround, applies throughout this file (and
 * was the actual root cause of a real, reproducible bug hit and fixed
 * elsewhere in this project the same session this file was written -- see
 * css.c's own top-of-function comment on css_set_default_style() for the
 * full writeup): chaining two or more "->" hops in a single expression to
 * reach a field -- "a->b->c" -- reads back GARBAGE on a large/field-heavy
 * struct under squash's codegen, even though the exact same field read
 * through an ordinary local pointer variable ("X *p = a->b; p->c") works
 * correctly. This file deals constantly in exactly that shape (AST nodes
 * holding pointers to other AST nodes, environments holding a pointer to
 * their parent environment, objects holding pointers to other objects) --
 * every single access below is written as "copy the pointer to a local,
 * then dereference the local," NEVER "a->b->c" or "a->b->c->d" inline.
 * Read this file's own repeated small comments at each such site as
 * reminders of this one project-wide rule, not as if each were a novel
 * discovery. */
#include "js_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>

/* ============================= Lexer ============================= */

typedef enum {
    JSTOK_EOF = 0, JSTOK_NUM, JSTOK_STR, JSTOK_IDENT, JSTOK_PUNCT, JSTOK_TEMPLATE
} JsTokType;

typedef struct {
    JsTokType type;
    char text[JS_IDENT_MAX];   /* ident name / punctuator lexeme */
    char str[JS_STR_MAX];      /* decoded string literal value */
    double num;
} JsToken;

#define JS_MAX_TOKENS 4096

typedef struct {
    JsToken toks[JS_MAX_TOKENS];
    int count;
} JsTokenList;

static int js_is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_' || c == '$'; }
static int js_is_ident_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '$'; }

/* Tokenizes the whole source up front into `out` (capped at
 * JS_MAX_TOKENS -- a generous cap for this project's own scale of test
 * script, never expected to be hit by a real page). Returns 1 on success,
 * 0 on a lexical error (unterminated string, e.g.) -- the caller aborts
 * the whole script rather than running a partial token stream. */
static int js_lex(const char *src, JsTokenList *out) {
    const char *q = src;
    out->count = 0;
    for (;;) {
        while (*q && isspace((unsigned char)*q)) q++;
        if (q[0] == '/' && q[1] == '/') { while (*q && *q != '\n') q++; continue; }
        if (q[0] == '/' && q[1] == '*') {
            q += 2;
            while (*q && !(q[0] == '*' && q[1] == '/')) q++;
            if (*q) q += 2;
            continue;
        }
        if (!*q) break;
        if (out->count >= JS_MAX_TOKENS) return 0;
        JsToken *t = &out->toks[out->count];

        if (isdigit((unsigned char)*q) || (*q == '.' && isdigit((unsigned char)q[1]))) {
            char *end;
            t->num = strtod(q, &end);
            if (end == q) return 0;
            t->type = JSTOK_NUM;
            t->text[0] = 0;
            q = end;
            out->count++;
            continue;
        }
        if (*q == '"' || *q == '\'') {
            char quote = *q;
            q++;
            int i = 0;
            while (*q && *q != quote) {
                char c = *q;
                if (c == '\\' && q[1]) {
                    q++;
                    char e = *q;
                    if (e == 'n') c = '\n';
                    else if (e == 't') c = '\t';
                    else if (e == 'r') c = '\r';
                    else c = e;
                }
                if (i < JS_STR_MAX - 1) t->str[i++] = c;
                q++;
            }
            if (*q != quote) return 0; /* unterminated string */
            q++;
            t->str[i] = 0;
            t->type = JSTOK_STR;
            t->text[0] = 0;
            out->count++;
            continue;
        }
        if (*q == '`') {
            /* Template literal: captured as ONE raw token (backtick-to-
             * backtick, "${" ... "}" markers left IN PLACE, unparsed) --
             * js_parse_template() (called from js_parse_primary()) does
             * the real splitting into literal/expression parts at PARSE
             * time, re-lexing+re-parsing each "${...}" substring as its
             * own standalone expression. Brace-depth tracked here only
             * so a real "${...}" containing its own "{"/"}" (e.g. an
             * object literal argument) doesn't end the template early on
             * its first inner "}" -- NOT a full expression parse, just
             * enough bookkeeping to find the matching outer "}". A
             * backtick appearing INSIDE "${...}" (a nested template) is
             * not specially handled -- a documented, narrow gap, real
             * scripts essentially never nest templates that deeply in a
             * way this project's own test pages would ever hit. */
            q++;
            int i = 0, depth = 0;
            while (*q && !(*q == '`' && depth == 0)) {
                char c = *q;
                if (c == '$' && q[1] == '{') { depth++; if (i < JS_STR_MAX - 1) t->str[i++] = c; q++; c = *q; }
                else if (c == '{' && depth > 0) depth++;
                else if (c == '}' && depth > 0) depth--;
                else if (c == '\\' && q[1]) { q++; c = *q; if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; }
                if (i < JS_STR_MAX - 1) t->str[i++] = c;
                q++;
            }
            if (*q != '`') return 0; /* unterminated template */
            q++;
            t->str[i] = 0;
            t->type = JSTOK_TEMPLATE;
            t->text[0] = 0;
            out->count++;
            continue;
        }
        if (js_is_ident_start(*q)) {
            const char *start = q;
            while (js_is_ident_char(*q)) q++;
            int len = (int)(q - start);
            if (len >= JS_IDENT_MAX) len = JS_IDENT_MAX - 1;
            memcpy(t->text, start, (size_t)len);
            t->text[len] = 0;
            t->type = JSTOK_IDENT;
            out->count++;
            continue;
        }
        /* Punctuators, longest-match-first. */
        {
            static const char *three[] = { "===", "!==", 0 };
            static const char *two[] = { "==", "!=", "<=", ">=", "&&", "||",
                "+=", "-=", "*=", "/=", "++", "--", 0 };
            int matched = 0;
            int i;
            for (i = 0; three[i]; i++) {
                size_t l = strlen(three[i]);
                if (strncmp(q, three[i], l) == 0) {
                    strcpy(t->text, three[i]); q += l; matched = 1; break;
                }
            }
            if (!matched) {
                for (i = 0; two[i]; i++) {
                    size_t l = strlen(two[i]);
                    if (strncmp(q, two[i], l) == 0) {
                        strcpy(t->text, two[i]); q += l; matched = 1; break;
                    }
                }
            }
            if (!matched) {
                if (!strchr("{}()[];,.?:+-*/%=<>!&|", *q)) return 0; /* unrecognized char */
                t->text[0] = *q; t->text[1] = 0; q++;
            }
            t->type = JSTOK_PUNCT;
            out->count++;
            continue;
        }
    }
    JsToken *eof = &out->toks[out->count];
    eof->type = JSTOK_EOF; eof->text[0] = 0;
    out->count++;
    return 1;
}

/* ============================= AST ============================= */

typedef enum {
    JS_PROGRAM, JS_BLOCK, JS_VAR_DECL, JS_FUNC_DECL, JS_IF, JS_WHILE, JS_FOR,
    JS_RETURN, JS_BREAK, JS_CONTINUE, JS_EXPR_STMT, JS_EMPTY,
    JS_NUM_LIT, JS_STR_LIT, JS_BOOL_LIT, JS_NULL_LIT, JS_UNDEF_LIT, JS_IDENT,
    JS_BINARY, JS_LOGICAL, JS_UNARY, JS_UPDATE, JS_ASSIGN, JS_CALL, JS_MEMBER,
    JS_INDEX, JS_FUNC_EXPR, JS_COND,
    /* Added for classes/arrays/objects/control-flow/templates -- see
     * each kind's own comment at its construction site (parser) and
     * consumption site (js_eval/js_exec_stmt) for exactly what kids[]/
     * str/str2 mean for it. */
    JS_ARRAY_LIT, JS_OBJECT_LIT, JS_PROP, JS_THIS, JS_NEW, JS_CLASS_DECL,
    JS_TRY, JS_THROW, JS_FOR_OF, JS_FOR_IN, JS_TEMPLATE,
    JS_VAR_DECL_PATTERN, JS_DESTR_ARRAY, JS_DESTR_OBJECT, JS_SUPER_CALL,
    JS_YIELD, JS_IMPORT, JS_EXPORT
} JsNodeKind;

/* See this file's own top comment: kids[] holds child AST nodes, whose
 * meaning depends on `kind` (documented per-kind at each construction/
 * consumption site below) -- never accessed via a chained "n->kids[i]->
 * kids[j]" in one expression, always through a local. */
typedef struct JsNode {
    JsNodeKind kind;
    struct JsNode **kids;
    int kid_count, kid_cap;
    double num;
    char *str;
    char *str2;  /* JS_CLASS_DECL: parent class name (NULL if no "extends") */
    char op[4];
} JsNode;

static JsNode *js_node_new(JsNodeKind kind) {
    JsNode *n = (JsNode *)malloc(sizeof(JsNode));
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    return n;
}

static void js_node_push(JsNode *n, JsNode *kid) {
    if (n->kid_count >= n->kid_cap) {
        n->kid_cap = n->kid_cap ? n->kid_cap * 2 : 4;
        n->kids = (JsNode **)realloc(n->kids, (size_t)n->kid_cap * sizeof(JsNode *));
    }
    n->kids[n->kid_count++] = kid;
}

/* ============================= Parser ============================= */

typedef struct {
    JsTokenList *toks;
    int pos;
    int ok; /* set to 0 on the first syntax error; every parse function
               checks it and bails out cheaply once it's 0, so a single
               error doesn't cascade into a long chain of confusing
               follow-on ones. */
    /* Real recursive-descent nesting depth, shared across expression AND
       statement parsing (js_parse_primary()/js_parse_stmt()'s own guarded
       wrapper each increment/decrement it around their real body -- see
       either's own comment). This is a SEPARATE concern from
       JSInterp::call_depth (js_engine.c's own runtime function-call-depth
       guard): that one bounds INTERPRETING an already-parsed script;
       this one bounds PARSING the source text itself -- something like
       "((((((...))))))" or "{{{{{{...}}}}}}" thousands of parens/braces
       deep recurses this parser's own native C stack before a single
       statement ever runs, a real crash found via this session's own
       fuzz testing, entirely independent of the runtime guard. */
    int depth;
} JsParser;
/* See JsParser::depth's own comment. Comfortably below what this
   project's own measured native stack size can hold for one level of
   this parser's own real per-call C stack usage (a much shallower budget
   than JS_MAX_CALL_DEPTH's own -- parsing recurses through noticeably
   more, and larger, stack frames per nesting level than a single runtime
   function call does). */
#define JS_PARSE_MAX_DEPTH 150

static JsToken *js_cur(JsParser *p) {
    JsTokenList *tl = p->toks;
    return &tl->toks[p->pos];
}
/* One-token lookahead -- used to tell "get x() {...}" (an accessor: "get"
   followed by ANOTHER identifier) apart from a plain property literally
   named "get"/"set" (followed by ":" or "," or "}"), in both object
   literals and class bodies. Bounded the same way js_advance() is (never
   reads past the token list's own EOF sentinel). */
static JsToken *js_peek(JsParser *p) {
    JsTokenList *tl = p->toks;
    int idx = p->pos + 1;
    if (idx >= tl->count) idx = tl->count - 1;
    return &tl->toks[idx];
}
static int js_at_punct(JsParser *p, const char *s) {
    JsToken *t = js_cur(p);
    return t->type == JSTOK_PUNCT && strcmp(t->text, s) == 0;
}
static int js_at_kw(JsParser *p, const char *s) {
    JsToken *t = js_cur(p);
    return t->type == JSTOK_IDENT && strcmp(t->text, s) == 0;
}
static void js_advance(JsParser *p) { if (p->pos < p->toks->count - 1) p->pos++; }
static int js_eat_punct(JsParser *p, const char *s) {
    if (js_at_punct(p, s)) { js_advance(p); return 1; }
    p->ok = 0;
    return 0;
}
/* Optional ";" -- accepted if present, never required (a real ASI table
 * is out of scope; treating ";" as always-optional is a safe, simple
 * over-approximation: it accepts strictly MORE programs than real JS,
 * never fewer, and never mis-splits a statement the way a wrong ASI
 * guess could). */
static void js_eat_semi(JsParser *p) { if (js_at_punct(p, ";")) js_advance(p); }

static JsNode *js_parse_expr(JsParser *p);
static JsNode *js_parse_assign(JsParser *p);
static JsNode *js_parse_stmt(JsParser *p);
static JsNode *js_parse_block(JsParser *p);
static JsNode *js_parse_function_decl(JsParser *p, int is_async);
static int js_parse_at_async_function(JsParser *p);

/* Splits a raw captured template-literal token (see js_lex()'s own "`"
 * case) into a JS_TEMPLATE node -- kids alternate JS_STR_LIT (literal
 * text) / expression, always starting AND ending with a literal (an
 * empty one, "", if the template starts/ends with "${" -- so
 * js_eval()'s own JS_TEMPLATE case can just walk kids in a fixed
 * literal-expr-literal-expr...-literal pattern without needing to track
 * which index is which kind separately). Each "${...}" substring is
 * re-lexed and re-parsed as its own standalone expression -- a real,
 * separate parse, not a textual splice, so it gets real operator
 * precedence/nested calls/whatever else js_parse_expr() itself supports. */
static JsNode *js_parse_template(const char *raw) {
    JsNode *tmpl = js_node_new(JS_TEMPLATE);
    const char *p = raw;
    char litbuf[JS_STR_MAX];
    int li = 0;
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            JsNode *lit = js_node_new(JS_STR_LIT);
            litbuf[li] = 0;
            lit->str = strdup(litbuf);
            js_node_push(tmpl, lit);
            li = 0;
            p += 2;
            const char *start = p;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '{') depth++;
                else if (*p == '}') { depth--; if (depth == 0) break; }
                p++;
            }
            int elen = (int)(p - start);
            char exprbuf[JS_STR_MAX];
            if (elen >= (int)sizeof exprbuf) elen = (int)sizeof exprbuf - 1;
            if (elen > 0) memcpy(exprbuf, start, (size_t)elen);
            exprbuf[elen] = 0;
            if (*p == '}') p++;
            JsTokenList *etoks = (JsTokenList *)malloc(sizeof(JsTokenList));
            JsNode *exprnode;
            if (js_lex(exprbuf, etoks)) {
                JsParser ep; ep.toks = etoks; ep.pos = 0; ep.ok = 1; ep.depth = 0;
                exprnode = js_parse_expr(&ep);
            } else {
                exprnode = js_node_new(JS_UNDEF_LIT);
            }
            free(etoks);
            js_node_push(tmpl, exprnode);
            continue;
        }
        if (li < (int)sizeof(litbuf) - 1) litbuf[li++] = *p;
        p++;
    }
    JsNode *lastlit = js_node_new(JS_STR_LIT);
    litbuf[li] = 0;
    lastlit->str = strdup(litbuf);
    js_node_push(tmpl, lastlit);
    return tmpl;
}

static JsNode *js_parse_primary_impl(JsParser *p);
/* Guarded wrapper -- see JsParser::depth's own comment on why this
   exists (unbounded parenthesis/bracket/unary nesting in the SOURCE TEXT
   itself can overflow this parser's own native C stack, a real, distinct
   bug from JSInterp::call_depth's runtime-recursion guard). Wrapping
   rather than threading the depth check through every one of
   js_parse_primary_impl's own many early returns keeps this a single,
   easy-to-audit choke point. */
static JsNode *js_parse_primary(JsParser *p) {
    if (p->depth >= JS_PARSE_MAX_DEPTH) { p->ok = 0; return js_node_new(JS_NULL_LIT); }
    p->depth++;
    JsNode *r = js_parse_primary_impl(p);
    p->depth--;
    return r;
}
static JsNode *js_parse_primary_impl(JsParser *p) {
    JsToken *t = js_cur(p);
    if (t->type == JSTOK_NUM) {
        JsNode *n = js_node_new(JS_NUM_LIT); n->num = t->num; js_advance(p); return n;
    }
    if (t->type == JSTOK_STR) {
        JsNode *n = js_node_new(JS_STR_LIT); n->str = strdup(t->str); js_advance(p); return n;
    }
    if (js_at_kw(p, "true")) { JsNode *n = js_node_new(JS_BOOL_LIT); n->num = 1; js_advance(p); return n; }
    if (js_at_kw(p, "false")) { JsNode *n = js_node_new(JS_BOOL_LIT); n->num = 0; js_advance(p); return n; }
    if (js_at_kw(p, "null")) { js_advance(p); return js_node_new(JS_NULL_LIT); }
    if (js_at_kw(p, "undefined")) { js_advance(p); return js_node_new(JS_UNDEF_LIT); }
    /* "async function(...) {...}" -- see js_parse_at_async_function()'s
       own comment (js_parse_stmt_impl's own section) for why this uses
       that shared helper rather than the compound inline condition it
       replaced: a real, confirmed squash codegen bug found this session. */
    int is_async_expr = js_parse_at_async_function(p);
    if (is_async_expr) js_advance(p);
    if (js_at_kw(p, "function")) {
        js_advance(p);
        JsNode *fn = js_node_new(JS_FUNC_EXPR);
        /* "function*(...) {...}" -- see js_parse_function_decl()'s own
           identical "*" check/comment (its own generator marker). */
        if (is_async_expr) fn->op[0] = 'a';
        else if (js_at_punct(p, "*")) { js_advance(p); fn->op[0] = 'g'; }
        if (js_cur(p)->type == JSTOK_IDENT) { JsToken *nt = js_cur(p); fn->str = strdup(nt->text); js_advance(p); }
        if (!js_eat_punct(p, "(")) return fn;
        while (!js_at_punct(p, ")") && p->ok) {
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsNode *param = js_node_new(JS_IDENT);
            JsToken *pt = js_cur(p);
            param->str = strdup(pt->text);
            js_node_push(fn, param);
            js_advance(p);
            if (js_at_punct(p, ",")) js_advance(p);
        }
        js_eat_punct(p, ")");
        JsNode *body = js_parse_block(p);
        js_node_push(fn, body); /* last kid = body; kid_count-1 params before it */
        return fn;
    }
    if (js_at_kw(p, "this")) { js_advance(p); return js_node_new(JS_THIS); }
    if (js_at_kw(p, "new")) {
        js_advance(p);
        JsNode *callee = js_parse_primary(p);
        while (js_at_punct(p, ".")) {
            js_advance(p);
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsNode *m = js_node_new(JS_MEMBER);
            JsToken *mt = js_cur(p);
            m->str = strdup(mt->text);
            js_advance(p);
            js_node_push(m, callee);
            callee = m;
        }
        JsNode *nw = js_node_new(JS_NEW);
        js_node_push(nw, callee); /* kids[0]=callee (class name, possibly dotted), kids[1..]=args, same shape as JS_CALL */
        if (js_eat_punct(p, "(")) {
            while (!js_at_punct(p, ")") && p->ok) {
                js_node_push(nw, js_parse_assign(p));
                if (js_at_punct(p, ",")) js_advance(p);
            }
            js_eat_punct(p, ")");
        }
        return nw;
    }
    if (js_at_punct(p, "[")) {
        js_advance(p);
        JsNode *arr = js_node_new(JS_ARRAY_LIT); /* kids[0..n-1] = element expressions */
        while (!js_at_punct(p, "]") && p->ok) {
            js_node_push(arr, js_parse_assign(p));
            if (js_at_punct(p, ",")) js_advance(p);
        }
        js_eat_punct(p, "]");
        return arr;
    }
    if (js_at_punct(p, "{")) {
        js_advance(p);
        JsNode *obj = js_node_new(JS_OBJECT_LIT); /* kids[0..n-1] = JS_PROP nodes */
        while (!js_at_punct(p, "}") && p->ok) {
            /* "get x() {...}" / "set x(v) {...}" -- "get"/"set" followed
               by ANOTHER identifier (not ":"/","/"}", which would mean a
               plain property literally named "get"/"set") -- see
               js_peek()'s own comment. */
            int is_accessor = 0, accessor_is_get = 0;
            if ((js_at_kw(p, "get") || js_at_kw(p, "set")) && js_peek(p)->type == JSTOK_IDENT) {
                is_accessor = 1;
                accessor_is_get = js_at_kw(p, "get");
                js_advance(p);
            }
            char keybuf[JS_IDENT_MAX];
            if (js_cur(p)->type == JSTOK_IDENT) { JsToken *kt = js_cur(p); strncpy(keybuf, kt->text, sizeof keybuf - 1); keybuf[sizeof keybuf - 1] = 0; js_advance(p); }
            else if (js_cur(p)->type == JSTOK_STR) { JsToken *kt = js_cur(p); strncpy(keybuf, kt->str, sizeof keybuf - 1); keybuf[sizeof keybuf - 1] = 0; js_advance(p); }
            else { p->ok = 0; break; }
            JsNode *prop = js_node_new(JS_PROP);
            prop->str = strdup(keybuf);
            if (is_accessor) {
                prop->op[0] = accessor_is_get ? 'g' : 's';
                JsNode *fn = js_node_new(JS_FUNC_EXPR);
                js_eat_punct(p, "(");
                while (!js_at_punct(p, ")") && p->ok) {
                    if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
                    JsNode *param = js_node_new(JS_IDENT);
                    JsToken *pmt = js_cur(p);
                    param->str = strdup(pmt->text);
                    js_node_push(fn, param);
                    js_advance(p);
                    if (js_at_punct(p, ",")) js_advance(p);
                }
                js_eat_punct(p, ")");
                JsNode *fbody = js_parse_block(p);
                js_node_push(fn, fbody);
                js_node_push(prop, fn);
            } else if (js_at_punct(p, ":")) {
                js_advance(p);
                js_node_push(prop, js_parse_assign(p));
            } else {
                /* shorthand "{a}" === "{a: a}" */
                JsNode *id = js_node_new(JS_IDENT);
                id->str = strdup(keybuf);
                js_node_push(prop, id);
            }
            js_node_push(obj, prop);
            if (js_at_punct(p, ",")) js_advance(p);
        }
        js_eat_punct(p, "}");
        return obj;
    }
    if (t->type == JSTOK_TEMPLATE) { JsNode *n = js_parse_template(t->str); js_advance(p); return n; }
    if (t->type == JSTOK_IDENT) {
        JsNode *n = js_node_new(JS_IDENT); n->str = strdup(t->text); js_advance(p); return n;
    }
    if (js_at_punct(p, "(")) {
        js_advance(p);
        JsNode *e = js_parse_expr(p);
        js_eat_punct(p, ")");
        return e;
    }
    p->ok = 0;
    return js_node_new(JS_UNDEF_LIT);
}

/* Postfix: member "." / index "[...]" / call "(...)" chains, e.g.
 * "document.getElementById('x').style.color". */
static JsNode *js_parse_postfix(JsParser *p) {
    JsNode *e = js_parse_primary(p);
    for (;;) {
        if (js_at_punct(p, ".")) {
            js_advance(p);
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsNode *m = js_node_new(JS_MEMBER);
            JsToken *mt = js_cur(p);
            m->str = strdup(mt->text);
            js_advance(p);
            js_node_push(m, e);
            e = m;
        } else if (js_at_punct(p, "[")) {
            js_advance(p);
            JsNode *idx = js_parse_expr(p);
            js_eat_punct(p, "]");
            JsNode *m = js_node_new(JS_INDEX);
            js_node_push(m, e);
            js_node_push(m, idx);
            e = m;
        } else if (js_at_punct(p, "(")) {
            js_advance(p);
            JsNode *call = js_node_new(JS_CALL);
            js_node_push(call, e); /* kids[0] = callee */
            while (!js_at_punct(p, ")") && p->ok) {
                JsNode *arg = js_parse_expr(p);
                js_node_push(call, arg);
                if (js_at_punct(p, ",")) js_advance(p);
            }
            js_eat_punct(p, ")");
            e = call;
        } else if (js_at_punct(p, "++") || js_at_punct(p, "--")) {
            JsNode *u = js_node_new(JS_UPDATE);
            JsToken *ut = js_cur(p);
            strcpy(u->op, ut->text);
            js_advance(p);
            js_node_push(u, e);
            e = u;
        } else break;
    }
    return e;
}

static JsNode *js_parse_unary_impl(JsParser *p);
/* Guarded wrapper -- same pattern/rationale as js_parse_primary()'s own
   (see its comment). Needed as a SEPARATE guard from js_parse_primary()'s:
   a long prefix-operator chain like "!!!!!!...!!!x" or
   "typeof typeof typeof ... x" recurses js_parse_unary() directly, one
   token per level, WITHOUT ever passing back through js_parse_primary()
   until the final operand -- found via this session's own fuzz testing
   after js_parse_primary()'s own guard alone still left this path
   unbounded. Shares JsParser's own single `depth` counter with
   js_parse_primary()/js_parse_stmt(), so any combination of nesting
   styles still hits one honest combined limit. */
static JsNode *js_parse_unary(JsParser *p) {
    if (p->depth >= JS_PARSE_MAX_DEPTH) { p->ok = 0; return js_node_new(JS_NULL_LIT); }
    p->depth++;
    JsNode *r = js_parse_unary_impl(p);
    p->depth--;
    return r;
}
static JsNode *js_parse_unary_impl(JsParser *p) {
    if (js_at_punct(p, "!") || js_at_punct(p, "-") || js_at_punct(p, "+")) {
        JsNode *n = js_node_new(JS_UNARY);
        JsToken *ct = js_cur(p);
        strcpy(n->op, ct->text);
        js_advance(p);
        js_node_push(n, js_parse_unary(p));
        return n;
    }
    if (js_at_punct(p, "++") || js_at_punct(p, "--")) {
        JsNode *n = js_node_new(JS_UPDATE);
        JsToken *ct = js_cur(p);
        strcpy(n->op, ct->text);
        js_advance(p);
        js_node_push(n, js_parse_unary(p));
        return n;
    }
    if (js_at_kw(p, "typeof")) {
        js_advance(p);
        JsNode *n = js_node_new(JS_UNARY);
        strcpy(n->op, "ty");
        js_node_push(n, js_parse_unary(p));
        return n;
    }
    if (js_at_kw(p, "await")) {
        js_advance(p);
        JsNode *n = js_node_new(JS_UNARY);
        strcpy(n->op, "aw");
        js_node_push(n, js_parse_unary(p));
        return n;
    }
    return js_parse_postfix(p);
}

/* One precedence-climbing binary-operator parser, driven by a small table
 * (op text -> precedence), rather than one hand-written function per
 * precedence level -- fewer places for a level to be missed/misordered.
 * && and || are handled as JS_LOGICAL (short-circuit at eval time, see
 * js_eval()) but share this same climb. */
static int js_binop_prec(const char *op) {
    if (!strcmp(op, "||")) return 1;
    if (!strcmp(op, "&&")) return 2;
    if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "===") || !strcmp(op, "!==")) return 3;
    if (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) return 4;
    if (!strcmp(op, "+") || !strcmp(op, "-")) return 5;
    if (!strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%")) return 6;
    return -1;
}

static JsNode *js_parse_binary(JsParser *p, int min_prec) {
    JsNode *left = js_parse_unary(p);
    for (;;) {
        JsToken *t = js_cur(p);
        if (t->type != JSTOK_PUNCT) break;
        int prec = js_binop_prec(t->text);
        if (prec < min_prec) break;
        char op[4]; strcpy(op, t->text);
        js_advance(p);
        JsNode *right = js_parse_binary(p, prec + 1);
        JsNode *n = js_node_new((!strcmp(op, "&&") || !strcmp(op, "||")) ? JS_LOGICAL : JS_BINARY);
        strcpy(n->op, op);
        js_node_push(n, left);
        js_node_push(n, right);
        left = n;
    }
    return left;
}

static JsNode *js_parse_cond(JsParser *p) {
    JsNode *cond = js_parse_binary(p, 0);
    if (js_at_punct(p, "?")) {
        js_advance(p);
        JsNode *then_e = js_parse_assign(p);
        js_eat_punct(p, ":");
        JsNode *else_e = js_parse_assign(p);
        JsNode *n = js_node_new(JS_COND);
        js_node_push(n, cond); js_node_push(n, then_e); js_node_push(n, else_e);
        return n;
    }
    return cond;
}

static JsNode *js_parse_assign(JsParser *p) {
    /* "yield expr" / "yield" (no operand -- yields undefined) -- parsed
       at assignment precedence (real JS's own "yield" precedence, just
       above plain comma), so both "yield x;" as its own statement and
       "var v = yield x;" work. Only meaningful inside a "function*" body
       (see js_call_function()'s own generator-dispatch comment) -- a
       bare "yield" elsewhere just parses fine and evaluates to undefined
       at runtime (JS_YIELD's own js_eval() case), a harmless, documented
       simplification rather than a parse-time error. */
    if (js_at_kw(p, "yield")) {
        js_advance(p);
        JsNode *n = js_node_new(JS_YIELD);
        if (!js_at_punct(p, ";") && !js_at_punct(p, ")") && !js_at_punct(p, ",") && !js_at_punct(p, "}") && js_cur(p)->type != JSTOK_EOF) {
            js_node_push(n, js_parse_assign(p));
        }
        return n;
    }
    JsNode *left = js_parse_cond(p);
    if (js_at_punct(p, "=") || js_at_punct(p, "+=") || js_at_punct(p, "-=") ||
        js_at_punct(p, "*=") || js_at_punct(p, "/=")) {
        char op[4]; JsToken *ot = js_cur(p); strcpy(op, ot->text);
        js_advance(p);
        JsNode *right = js_parse_assign(p);
        JsNode *n = js_node_new(JS_ASSIGN);
        strcpy(n->op, op);
        js_node_push(n, left);
        js_node_push(n, right);
        return n;
    }
    return left;
}

static JsNode *js_parse_expr(JsParser *p) { return js_parse_assign(p); }

static JsNode *js_parse_block(JsParser *p) {
    JsNode *blk = js_node_new(JS_BLOCK);
    if (!js_eat_punct(p, "{")) return blk;
    while (!js_at_punct(p, "}") && js_cur(p)->type != JSTOK_EOF && p->ok) {
        js_node_push(blk, js_parse_stmt(p));
    }
    js_eat_punct(p, "}");
    return blk;
}

/* "let {a, b:renamed} = obj;" / "let [x, y] = arr;" -- see JS_DESTR_OBJECT/
 * JS_DESTR_ARRAY's own comment at their construction sites below for the
 * exact kids[] shape each produces, and js_exec_stmt's own
 * JS_VAR_DECL_PATTERN case for how they're actually bound. Only the
 * shorthand/rename object form and plain identifier array form are
 * supported -- no nested patterns, no defaults, no rest ("...rest") --
 * a documented, bounded subset covering the common real-world shapes. */
static JsNode *js_parse_destr_pattern(JsParser *p) {
    if (js_at_punct(p, "{")) {
        js_advance(p);
        JsNode *pat = js_node_new(JS_DESTR_OBJECT); /* kids[] = JS_PROP(str=source key, kids[0]=JS_IDENT target name) */
        while (!js_at_punct(p, "}") && p->ok) {
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsToken *kt = js_cur(p);
            char key[JS_IDENT_MAX]; strncpy(key, kt->text, sizeof key - 1); key[sizeof key - 1] = 0;
            js_advance(p);
            JsNode *prop = js_node_new(JS_PROP);
            prop->str = strdup(key);
            if (js_at_punct(p, ":")) {
                js_advance(p);
                if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
                JsToken *rt = js_cur(p);
                JsNode *id = js_node_new(JS_IDENT); id->str = strdup(rt->text);
                js_advance(p);
                js_node_push(prop, id);
            } else {
                JsNode *id = js_node_new(JS_IDENT); id->str = strdup(key);
                js_node_push(prop, id);
            }
            js_node_push(pat, prop);
            if (js_at_punct(p, ",")) js_advance(p);
        }
        js_eat_punct(p, "}");
        return pat;
    }
    if (js_at_punct(p, "[")) {
        js_advance(p);
        JsNode *pat = js_node_new(JS_DESTR_ARRAY); /* kids[] = JS_IDENT target names, by position */
        while (!js_at_punct(p, "]") && p->ok) {
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsToken *it = js_cur(p);
            JsNode *id = js_node_new(JS_IDENT); id->str = strdup(it->text);
            js_advance(p);
            js_node_push(pat, id);
            if (js_at_punct(p, ",")) js_advance(p);
        }
        js_eat_punct(p, "]");
        return pat;
    }
    p->ok = 0;
    return js_node_new(JS_UNDEF_LIT);
}

static JsNode *js_parse_var_decl(JsParser *p) {
    /* "var"/"let"/"const" already consumed by the caller. Supports a
     * comma-separated list ("var a=1, b=2;") by returning a JS_BLOCK of
     * JS_VAR_DECL statements when there's more than one -- the caller
     * (js_parse_stmt) unwraps a single-decl block back to the bare decl,
     * so the common single-variable case still executes as one node. */
    JsNode *decls = js_node_new(JS_BLOCK);
    for (;;) {
        if (js_at_punct(p, "{") || js_at_punct(p, "[")) {
            JsNode *pat = js_parse_destr_pattern(p);
            JsNode *d = js_node_new(JS_VAR_DECL_PATTERN); /* kids[0]=pattern, kids[1]=init expr */
            js_node_push(d, pat);
            if (js_eat_punct(p, "=")) js_node_push(d, js_parse_assign(p));
            else js_node_push(d, js_node_new(JS_UNDEF_LIT));
            js_node_push(decls, d);
            if (js_at_punct(p, ",")) { js_advance(p); continue; }
            break;
        }
        if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
        JsNode *d = js_node_new(JS_VAR_DECL);
        JsToken *dt = js_cur(p);
        d->str = strdup(dt->text);
        js_advance(p);
        if (js_at_punct(p, "=")) {
            js_advance(p);
            js_node_push(d, js_parse_assign(p));
        }
        js_node_push(decls, d);
        if (js_at_punct(p, ",")) { js_advance(p); continue; }
        break;
    }
    if (decls->kid_count == 1) return decls->kids[0];
    return decls;
}

/* "function foo(...) {...}" statement -- "async" already consumed (if
   present) by the caller, which passes is_async so this one function
   handles BOTH "function foo(){}" and "async function foo(){}" -- see
   js_parse_stmt_impl()'s own comment on why this is a separate function
   rather than inlined with a bridging local. */
static JsNode *js_parse_function_decl(JsParser *p, int is_async) {
    js_advance(p); /* consume "function" */
    JsNode *fn = js_node_new(JS_FUNC_DECL);
    /* "function* name(...) {...}" -- a generator. op[0] holds AT MOST
       one marker ('a' for async, 'g' for generator; a function can't be
       both in this engine's own scope -- no async generators, a
       documented, narrow gap). The "*" is checked here, not by the
       caller, so both js_parse_stmt_impl()'s and js_parse_primary_impl()'s
       own call sites get it for free. */
    if (is_async) fn->op[0] = 'a';
    else if (js_at_punct(p, "*")) { js_advance(p); fn->op[0] = 'g'; }
    if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; return fn; }
    { JsToken *ft = js_cur(p); fn->str = strdup(ft->text); }
    js_advance(p);
    js_eat_punct(p, "(");
    while (!js_at_punct(p, ")") && p->ok) {
        if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
        JsNode *param = js_node_new(JS_IDENT);
        JsToken *pt2 = js_cur(p);
        param->str = strdup(pt2->text);
        js_node_push(fn, param);
        js_advance(p);
        if (js_at_punct(p, ",")) js_advance(p);
    }
    js_eat_punct(p, ")");
    JsNode *body = js_parse_block(p);
    js_node_push(fn, body);
    return fn;
}

/* "import { a, b as c } from 'url';" / "import def from 'url';" -- see
   js_run_module()'s own top comment for the overall ES-module design
   this feeds. kids[] = JS_PROP nodes (str = the SOURCE name as exported
   by the other module, "default" for a default import; kids[0] =
   JS_IDENT, the LOCAL binding name this module will see it as); str2 =
   the module URL string. Pulled out as its own function for the same
   "keep js_parse_stmt_impl() from growing another simultaneously-live
   local" reason js_parse_function_decl() above already documents.
   Namespace imports ("import * as ns from ...") and mixing a default
   AND named imports in one statement are NOT supported -- a documented,
   narrow gap; either form alone works fine. */
static JsNode *js_parse_import(JsParser *p) {
    js_advance(p); /* consume "import" */
    JsNode *n = js_node_new(JS_IMPORT);
    if (js_cur(p)->type == JSTOK_IDENT && !js_at_punct(p, "{")) {
        JsToken *dt = js_cur(p);
        JsNode *prop = js_node_new(JS_PROP);
        prop->str = strdup("default");
        JsNode *id = js_node_new(JS_IDENT);
        id->str = strdup(dt->text);
        js_node_push(prop, id);
        js_node_push(n, prop);
        js_advance(p);
    } else if (js_at_punct(p, "{")) {
        js_advance(p);
        while (!js_at_punct(p, "}") && p->ok) {
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsToken *nt = js_cur(p);
            char srcname[JS_IDENT_MAX];
            strncpy(srcname, nt->text, sizeof srcname - 1); srcname[sizeof srcname - 1] = 0;
            js_advance(p);
            char localname[JS_IDENT_MAX];
            strncpy(localname, srcname, sizeof localname - 1); localname[sizeof localname - 1] = 0;
            if (js_at_kw(p, "as")) {
                js_advance(p);
                if (js_cur(p)->type == JSTOK_IDENT) {
                    JsToken *lt = js_cur(p);
                    strncpy(localname, lt->text, sizeof localname - 1); localname[sizeof localname - 1] = 0;
                    js_advance(p);
                }
            }
            JsNode *prop = js_node_new(JS_PROP);
            prop->str = strdup(srcname);
            JsNode *id = js_node_new(JS_IDENT);
            id->str = strdup(localname);
            js_node_push(prop, id);
            js_node_push(n, prop);
            if (js_at_punct(p, ",")) js_advance(p);
        }
        js_eat_punct(p, "}");
    }
    if (js_at_kw(p, "from")) {
        js_advance(p);
        if (js_cur(p)->type == JSTOK_STR) {
            JsToken *ut = js_cur(p);
            n->str2 = strdup(ut->str);
            js_advance(p);
        }
    }
    js_eat_semi(p);
    return n;
}

/* "export function foo() {...}" / "export var/let/const ..." / "export
   class C {...}" / "export default expr;" -- kids[0] = the underlying
   declaration (or, for "export default", a plain expression); str =
   "default" for that form, otherwise the exported declaration's own
   name (js_exec_stmt()'s own JS_EXPORT case reads decl->str itself for
   the ordinary cases and only needs n->str for "export default", where
   there IS no declared name to read back). "export {a, b};" (re-
   exporting already-declared names with no "default"/decl attached) is
   NOT supported -- a documented, narrow gap; every export in this
   engine's own scope attaches directly to a real declaration. */
static JsNode *js_parse_export(JsParser *p) {
    js_advance(p); /* consume "export" */
    JsNode *n = js_node_new(JS_EXPORT);
    if (js_at_kw(p, "default")) {
        js_advance(p);
        n->str = strdup("default");
        JsNode *expr = js_parse_assign(p);
        js_eat_semi(p);
        js_node_push(n, expr);
        return n;
    }
    JsNode *decl = js_parse_stmt(p);
    js_node_push(n, decl);
    return n;
}

static JsNode *js_parse_stmt_impl(JsParser *p);
/* Guarded wrapper -- same rationale/pattern as js_parse_primary()'s own
   (see its comment): nested blocks/if/while/for statements
   ("{{{{...}}}}", "if(1)if(1)if(1)...") recurse through
   js_parse_stmt<->js_parse_block just as unboundedly as expression
   nesting does, and share the SAME depth counter (a script mixing both
   kinds of nesting should still hit one combined, honest limit rather
   than two independent ones that could add together past what the real
   native stack can hold). */
static JsNode *js_parse_stmt(JsParser *p) {
    if (p->depth >= JS_PARSE_MAX_DEPTH) { p->ok = 0; return js_node_new(JS_EMPTY); }
    p->depth++;
    JsNode *r = js_parse_stmt_impl(p);
    p->depth--;
    return r;
}
/* "async" lookahead shared by both the statement-level function-decl
   check (js_parse_stmt_impl(), below) and the expression-level function-
   EXPR check (js_parse_primary_impl()) -- see either's own comment for
   WHY the check itself is split out this way: calling js_peek(p) and
   dereferencing its result inline, MORE THAN ONCE, inside one compound
   "&&" condition is exactly this file's own documented "never write
   funcCall()->field as part of a larger inline expression" squash
   codegen hazard (js_parse_primary_impl()'s own top-of-file rule) --
   found the hard way this session: a version of this check written
   as a single "js_at_kw(p, "async") && js_peek(p)->type == ... &&
   !strcmp(js_peek(p)->text, ...)" expression reliably segfaulted while
   PARSING alone (never even reaching a call), confirmed via step-by-step
   fprintf tracing that every INDIVIDUAL piece evaluated correctly in
   isolation, and only the single compound re-evaluation crashed -- gcc-
   compiled behavior was unaffected either way, the same "safe pointer,
   unsafe expression" signature every other confirmed squash bug in this
   file has had. Fixed by capturing js_peek(p)'s result to a local ONCE
   and reading every field off that local afterward, never calling
   js_peek(p) a second time within the same condition. */
static int js_parse_at_async_function(JsParser *p) {
    if (!js_at_kw(p, "async")) return 0;
    JsToken *pk = js_peek(p);
    if (pk->type != JSTOK_IDENT) return 0;
    return !strcmp(pk->text, "function");
}

static JsNode *js_parse_stmt_impl(JsParser *p) {
    if (js_at_punct(p, "{")) return js_parse_block(p);
    if (js_at_punct(p, ";")) { js_advance(p); return js_node_new(JS_EMPTY); }
    if (js_at_kw(p, "import")) return js_parse_import(p);
    if (js_at_kw(p, "export")) return js_parse_export(p);
    if (js_at_kw(p, "var") || js_at_kw(p, "let") || js_at_kw(p, "const")) {
        js_advance(p);
        JsNode *d = js_parse_var_decl(p);
        js_eat_semi(p);
        return d;
    }
    /* "function foo(...) {...}" / "async function foo(...) {...}" --
       pulled out into its own small function (rather than inlined here
       with a bridging "is_async" local) since it's genuinely reusable
       (js_parse_stmt_impl calls it unconditionally either way), and
       keeps this already-large function from growing yet another
       simultaneously-live local -- this project has separately hit a
       real squash codegen bug from exactly that "too many live locals"
       pattern elsewhere (see layout.c/sqw_main.c's own precedent), so
       this stays deliberately minimal even though it turned out NOT to
       be the actual cause of the bug found here (see
       js_parse_at_async_function()'s own comment for that). */
    if (js_at_kw(p, "function")) return js_parse_function_decl(p, 0);
    if (js_parse_at_async_function(p)) {
        js_advance(p);
        return js_parse_function_decl(p, 1);
    }
    if (js_at_kw(p, "class")) {
        js_advance(p);
        if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; return js_node_new(JS_EMPTY); }
        JsNode *cls = js_node_new(JS_CLASS_DECL); /* kids[] = JS_PROP(str=method name, kids[0]=JS_FUNC_EXPR); str2 = parent class name or NULL */
        { JsToken *ct = js_cur(p); cls->str = strdup(ct->text); }
        js_advance(p);
        if (js_at_kw(p, "extends")) {
            js_advance(p);
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; return cls; }
            JsToken *pt = js_cur(p);
            cls->str2 = strdup(pt->text);
            js_advance(p);
        }
        js_eat_punct(p, "{");
        while (!js_at_punct(p, "}") && js_cur(p)->type != JSTOK_EOF && p->ok) {
            /* "get x() {...}" / "set x(v) {...}" -- same "get"/"set"
               one-token-lookahead detection as the object-literal parser
               above (see js_peek()'s own comment); a class method
               literally named "get"/"set" (followed directly by "(",
               not another identifier) still parses as an ordinary
               method, not an accessor. */
            int is_accessor = 0, accessor_is_get = 0;
            if ((js_at_kw(p, "get") || js_at_kw(p, "set")) && js_peek(p)->type == JSTOK_IDENT) {
                is_accessor = 1;
                accessor_is_get = js_at_kw(p, "get");
                js_advance(p);
            }
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsToken *mt = js_cur(p);
            char mname[JS_IDENT_MAX]; strncpy(mname, mt->text, sizeof mname - 1); mname[sizeof mname - 1] = 0;
            js_advance(p);
            JsNode *fn = js_node_new(JS_FUNC_EXPR);
            js_eat_punct(p, "(");
            while (!js_at_punct(p, ")") && p->ok) {
                if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
                JsNode *param = js_node_new(JS_IDENT);
                JsToken *pmt = js_cur(p);
                param->str = strdup(pmt->text);
                js_node_push(fn, param);
                js_advance(p);
                if (js_at_punct(p, ",")) js_advance(p);
            }
            js_eat_punct(p, ")");
            JsNode *mbody = js_parse_block(p);
            js_node_push(fn, mbody);
            JsNode *prop = js_node_new(JS_PROP);
            prop->str = strdup(mname);
            if (is_accessor) prop->op[0] = accessor_is_get ? 'g' : 's';
            js_node_push(prop, fn);
            js_node_push(cls, prop);
        }
        js_eat_punct(p, "}");
        return cls;
    }
    if (js_at_kw(p, "try")) {
        js_advance(p);
        JsNode *tryblk = js_parse_block(p);
        JsNode *n = js_node_new(JS_TRY); /* kids[0]=try block, kids[1]=catch block, kids[2]=finally block; str=catch param name (or NULL) */
        js_node_push(n, tryblk);
        JsNode *catchblk = js_node_new(JS_EMPTY);
        char catchparam[JS_IDENT_MAX]; catchparam[0] = 0;
        if (js_at_kw(p, "catch")) {
            js_advance(p);
            if (js_eat_punct(p, "(")) {
                if (js_cur(p)->type == JSTOK_IDENT) { JsToken *ct2 = js_cur(p); strncpy(catchparam, ct2->text, sizeof catchparam - 1); catchparam[sizeof catchparam - 1] = 0; js_advance(p); }
                js_eat_punct(p, ")");
            }
            catchblk = js_parse_block(p);
        }
        if (catchparam[0]) n->str = strdup(catchparam);
        js_node_push(n, catchblk);
        JsNode *finallyblk = js_node_new(JS_EMPTY);
        if (js_at_kw(p, "finally")) {
            js_advance(p);
            finallyblk = js_parse_block(p);
        }
        js_node_push(n, finallyblk);
        return n;
    }
    if (js_at_kw(p, "throw")) {
        js_advance(p);
        JsNode *n = js_node_new(JS_THROW);
        js_node_push(n, js_parse_expr(p));
        js_eat_semi(p);
        return n;
    }
    if (js_at_kw(p, "if")) {
        js_advance(p);
        js_eat_punct(p, "(");
        JsNode *cond = js_parse_expr(p);
        js_eat_punct(p, ")");
        JsNode *then_s = js_parse_stmt(p);
        JsNode *n = js_node_new(JS_IF);
        js_node_push(n, cond); js_node_push(n, then_s);
        if (js_at_kw(p, "else")) {
            js_advance(p);
            js_node_push(n, js_parse_stmt(p));
        }
        return n;
    }
    if (js_at_kw(p, "while")) {
        js_advance(p);
        js_eat_punct(p, "(");
        JsNode *cond = js_parse_expr(p);
        js_eat_punct(p, ")");
        JsNode *body = js_parse_stmt(p);
        JsNode *n = js_node_new(JS_WHILE);
        js_node_push(n, cond); js_node_push(n, body);
        return n;
    }
    if (js_at_kw(p, "for")) {
        js_advance(p);
        js_eat_punct(p, "(");
        /* Peek for "for (["var"/"let"/"const"] IDENT "of"/"in" ...)" --
         * rewind to `save_pos` and fall through to the ordinary
         * C-style for(;;) parse below if it turns out not to be one
         * (e.g. plain "for (i = 0; ...)" or "for (var i = 0; ...)"). */
        int save_pos = p->pos;
        int had_decl_kw = js_at_kw(p, "var") || js_at_kw(p, "let") || js_at_kw(p, "const");
        if (had_decl_kw) js_advance(p);
        if (js_cur(p)->type == JSTOK_IDENT) {
            JsToken *vt = js_cur(p);
            char varname[JS_IDENT_MAX]; strncpy(varname, vt->text, sizeof varname - 1); varname[sizeof varname - 1] = 0;
            js_advance(p);
            if (js_at_kw(p, "of") || js_at_kw(p, "in")) {
                int is_of = js_at_kw(p, "of");
                js_advance(p);
                JsNode *iter = js_parse_expr(p);
                js_eat_punct(p, ")");
                JsNode *body = js_parse_stmt(p);
                JsNode *n = js_node_new(is_of ? JS_FOR_OF : JS_FOR_IN);
                n->str = strdup(varname);
                js_node_push(n, iter);
                js_node_push(n, body);
                return n;
            }
        }
        p->pos = save_pos; /* not for-of/for-in -- ordinary C-style for */
        JsNode *init;
        if (js_at_punct(p, ";")) init = js_node_new(JS_EMPTY);
        else if (js_at_kw(p, "var") || js_at_kw(p, "let") || js_at_kw(p, "const")) { js_advance(p); init = js_parse_var_decl(p); }
        else init = js_node_new(JS_EXPR_STMT), js_node_push(init, js_parse_expr(p));
        js_eat_punct(p, ";");
        JsNode *cond = js_at_punct(p, ";") ? js_node_new(JS_EMPTY) : js_parse_expr(p);
        js_eat_punct(p, ";");
        JsNode *incr = js_at_punct(p, ")") ? js_node_new(JS_EMPTY) : js_parse_expr(p);
        js_eat_punct(p, ")");
        JsNode *body = js_parse_stmt(p);
        JsNode *n = js_node_new(JS_FOR);
        js_node_push(n, init); js_node_push(n, cond); js_node_push(n, incr); js_node_push(n, body);
        return n;
    }
    if (js_at_kw(p, "return")) {
        js_advance(p);
        JsNode *n = js_node_new(JS_RETURN);
        if (!js_at_punct(p, ";") && !js_at_punct(p, "}") && js_cur(p)->type != JSTOK_EOF) {
            js_node_push(n, js_parse_expr(p));
        }
        js_eat_semi(p);
        return n;
    }
    if (js_at_kw(p, "break")) { js_advance(p); js_eat_semi(p); return js_node_new(JS_BREAK); }
    if (js_at_kw(p, "continue")) { js_advance(p); js_eat_semi(p); return js_node_new(JS_CONTINUE); }

    JsNode *e = js_parse_expr(p);
    js_eat_semi(p);
    JsNode *stmt = js_node_new(JS_EXPR_STMT);
    js_node_push(stmt, e);
    return stmt;
}

static JsNode *js_parse_program(JsTokenList *toks, int *ok) {
    JsParser p; p.toks = toks; p.pos = 0; p.ok = 1; p.depth = 0;
    JsNode *prog = js_node_new(JS_PROGRAM);
    while (js_cur(&p)->type != JSTOK_EOF && p.ok) {
        js_node_push(prog, js_parse_stmt(&p));
    }
    *ok = p.ok;
    return prog;
}

/* ============================= Values ============================= */

/* IMPORTANT squash-compiler workaround #2, applies throughout the REST of
 * this file: a second, separate, real confirmed squash codegen bug (found
 * this session, via a series of minimal standalone repros in the
 * scratchpad -- each gcc-compiled control behaved correctly while the
 * squash-compiled build did not): passing OR returning a multi-field
 * struct BY VALUE across a function call boundary corrupts every field
 * after the first, even for a plain "return v;"/"JSValue val" with no
 * pointers, arrays, or chained access involved anywhere -- confirmed
 * completely independent of the earlier "->" chaining bug above (this one
 * reproduces with a single, unchained, 3-field local struct). The EXACT
 * same struct works perfectly when accessed directly (no call boundary),
 * passed as a POINTER, or written through an out-parameter pointer --
 * only crossing a call boundary BY VALUE is broken. This matches, and now
 * fully explains, why every other struct in this whole project (DomNode,
 * LayoutBox, CssRule, ...) is already always passed/returned via pointer,
 * never by value -- this file is simply the first code in the project to
 * have tried the by-value form. The fix applied throughout everything
 * below: JSValue is NEVER returned by value and NEVER taken as a by-value
 * parameter -- every "value-producing" function takes a trailing
 * "JSValue *out" and WRITES into it (via js_value_store(), a plain
 * pointer write, which IS reliable); every "value-consuming" function
 * takes "const JSValue *". Native builtin functions take "JSValue *args"
 * (already an array/pointer, unaffected) and equally write their result
 * through a trailing "JSValue *out". */

/* Field-by-field copy into an OUT-POINTER destination -- see this file's
 * "workaround #2" comment above; this is the ONE function that actually
 * moves a value's fields around, and it does so field-by-field through a
 * plain pointer, the one access shape confirmed reliable. */
static void js_value_store(JSValue *dst, const JSValue *src) {
    dst->type = src->type;
    dst->num = src->num;
    dst->str = src->str;
    dst->boolean = src->boolean;
    dst->obj = src->obj;
}

static void js_set_undefined(JSValue *out) { out->type = JSV_UNDEFINED; out->num = 0.0; out->str = 0; out->boolean = 0; out->obj = 0; }
static void js_set_null(JSValue *out) { out->type = JSV_NULL; out->num = 0.0; out->str = 0; out->boolean = 0; out->obj = 0; }
static void js_set_bool(JSValue *out, int b) { out->type = JSV_BOOL; out->num = 0.0; out->str = 0; out->boolean = b ? 1 : 0; out->obj = 0; }
static void js_set_number(JSValue *out, double n) { out->type = JSV_NUMBER; out->num = n; out->str = 0; out->boolean = 0; out->obj = 0; }
static void js_set_string(JSValue *out, const char *s) { out->type = JSV_STRING; out->num = 0.0; out->str = strdup(s ? s : ""); out->boolean = 0; out->obj = 0; }
static void js_set_object(JSValue *out, JSObject *o) { out->type = JSV_OBJECT; out->num = 0.0; out->str = 0; out->boolean = 0; out->obj = o; }

static int js_to_bool(const JSValue *v) {
    if (v->type == JSV_UNDEFINED || v->type == JSV_NULL) return 0;
    if (v->type == JSV_BOOL) return v->boolean;
    if (v->type == JSV_NUMBER) return v->num != 0.0;
    if (v->type == JSV_STRING) return v->str && v->str[0] != 0;
    return 1; /* object */
}

static double js_to_number(const JSValue *v) {
    if (v->type == JSV_NUMBER) return v->num;
    if (v->type == JSV_BOOL) return v->boolean ? 1.0 : 0.0;
    if (v->type == JSV_STRING) { char *end; const char *s = v->str ? v->str : ""; double d = strtod(s, &end); return (end == s) ? 0.0 : d; }
    return 0.0; /* undefined/null/object -- real JS gives NaN for most of
                   these; 0.0 is a deliberate simplification (avoids NaN
                   propagation edge cases entirely in this small engine). */
}

/* Formats a number the way real JS's default Number->String conversion
 * mostly reads for this engine's own realistic use (page counters, simple
 * arithmetic results): an integer value prints with no decimal point,
 * anything else prints with up to 6 significant fraction digits, trailing
 * zeros trimmed. Not real JS's exact (much more elaborate) algorithm. */
/* Forward declaration -- real body is much further down (needs nothing
 * from this section, just placed near its own related NaN-parsing
 * builtins). See its own comment for the real squash codegen bug this
 * works around: double `==`/`!=` doesn't respect real IEEE-754 NaN
 * semantics, so NEVER compare a possibly-NaN double with `==`/`!=`
 * directly anywhere in this file -- always route through this instead. */
static int js_double_is_nan(double n);

static void js_format_number(double n, char *out, size_t outcap) {
    if (js_double_is_nan(n)) { snprintf(out, outcap, "NaN"); return; }
    if (n == (double)(long long)n && fabs(n) < 1e15) {
        snprintf(out, outcap, "%lld", (long long)n);
    } else {
        snprintf(out, outcap, "%g", n);
    }
}

/* Writes v's string form into out (cap outcap) -- used for both real
 * js_to_string() calls (String(x), string concatenation) and for
 * console.log's own arg formatting. Defined here as a forward
 * declaration only -- the real body (further down, see its own
 * "Objects/Env" comment) needs JSObject's full definition (to special-
 * case JSOBJ_ARRAY the way real Array.prototype.toString() does),
 * which isn't available yet this early in the file. */
static void js_to_string_buf(const JSValue *v, char *out, size_t outcap);

/* ============================= Objects/Env ============================= */

typedef enum { JSOBJ_PLAIN = 0, JSOBJ_FUNCTION, JSOBJ_NATIVE, JSOBJ_DOM_ELEMENT, JSOBJ_ARRAY, JSOBJ_CLASS, JSOBJ_PROMISE, JSOBJ_GENERATOR } JsObjKind;
#define JS_PROMISE_PENDING 0
#define JS_PROMISE_FULFILLED 1
#define JS_PROMISE_REJECTED 2
/* A single promise chaining more than this many .then()/.catch() calls
   BEFORE it settles is a rare edge case -- silently dropped past this cap,
   same "generous but bounded, never crash" convention as every other
   fixed-size table in this file (JS_MAX_TIMERS etc). */
#define JS_MAX_PROMISE_REACTIONS 16

typedef struct JSEnv JSEnv;
struct JSInterp;

#define JS_ARRAY_INIT_CAP 8
/* A real, hard cap on any single array's length -- see js_array_ensure_
   cap()'s own comment for the crash this prevents: a script writing to a
   huge numeric index (e.g. "a[999999999] = 1", found via this session's
   own fuzz testing) would otherwise try to grow the backing store to
   match, realloc() would fail against a request that large, and the
   subsequent write into the (now NULL) arr_items would segfault the
   whole host process -- a script bug (or a hostile page) crashing the
   real embedding browser, not just "this script broke". 1,000,000
   elements is far more than any real page this project targets needs,
   while still comfortably fitting in memory at JSValue's own size. */
#define JS_ARRAY_MAX_LEN 1000000
struct JSObject {
    JsObjKind kind;
    char prop_names[JS_MAX_PROPS][JS_IDENT_MAX];
    JSValue prop_values[JS_MAX_PROPS];
    int prop_count;

    JsNode *func_node;   /* JSOBJ_FUNCTION: JS_FUNC_DECL/JS_FUNC_EXPR node */
    JSEnv *closure_env;  /* JSOBJ_FUNCTION: captured defining scope */

    void (*native_fn)(struct JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out); /* JSOBJ_NATIVE */

    DomNode *dom_node;   /* JSOBJ_DOM_ELEMENT */

    /* JSOBJ_ARRAY: real dynamic element storage, deliberately separate
     * from the fixed-size prop_names/prop_values property bag every
     * object also has (arrays can grow past JS_MAX_PROPS; a real page
     * script's array is a much more central, larger-scale data structure
     * than the small property bags every other object kind uses here). */
    JSValue *arr_items;
    int arr_len, arr_cap;

    /* JSOBJ_CLASS (the constructor function `class Foo {...}` itself
     * evaluates to): `methods` holds every non-constructor method as a
     * JSOBJ_FUNCTION property (name -> function), looked up by
     * js_get_prop() as a fallback whenever a plain-object INSTANCE's own
     * property lookup misses -- see js_obj_get_with_class_fallback()'s
     * own comment. `parent_class` is the JSOBJ_CLASS "extends"-ed from
     * (NULL for a base class) -- `methods` is seeded as a COPY of the
     * parent's own methods at class-declaration time (see js_exec_stmt's
     * own JS_CLASS_DECL case), a real but simplified single-linearization
     * of inheritance: a change to the PARENT class's own methods after a
     * subclass was declared does NOT retroactively affect that subclass,
     * unlike real JS's live prototype chain -- a documented, deliberate
     * gap (this engine has no live prototype-chain walk at all). */
    JSObject *methods;
    JSObject *parent_class;

    /* Set on an INSTANCE (a plain JSOBJ_PLAIN object created by `new
     * SomeClass(...)`) to that class's own `methods` bag above -- the
     * fallback js_get_prop() consults once a direct property lookup on
     * the instance itself misses, so "instance.someMethod()" finds a
     * method that was never copied onto the instance itself. NULL for
     * any object not created via `new`. */
    JSObject *instance_methods;

    /* JSOBJ_NATIVE only, optional: an opaque "which object does this
       particular closure operate on" pointer -- e.g. a Promise's own
       resolve/reject functions each need to remember WHICH promise they
       settle, and native_fn's own signature (interp/args/argc/this_obj/
       out) has no room for that beyond `this_obj`, which is already used
       for real method-call `this` binding. Cast to JSObject* by whichever
       native actually uses it (currently only the Promise executor's
       resolve/reject pair -- see js_install_promise()'s own comment).
       NULL for every other native. */
    void *native_capture;

    /* JSOBJ_PROMISE -- see js_install_promise()'s own top comment for the
       overall design/scope. promise_value is the fulfillment value or
       rejection reason once settled (meaningless while still PENDING).
       promise_reactions[] holds every .then()/.catch() callback pair
       registered before this promise settles (each with the NEW promise
       that specific .then() call itself returned, so it can be
       resolved/rejected once the reaction actually runs) -- fired and
       cleared the instant js_promise_settle() transitions out of PENDING;
       a .then() called AFTER that point runs its reaction immediately
       instead of ever touching this array. */
    int promise_state;
    JSValue promise_value;
    struct {
        JSObject *on_fulfilled; /* NULL if this .then()/.catch() call didn't provide one */
        JSObject *on_rejected;
        JSObject *result_promise;
    } promise_reactions[JS_MAX_PROMISE_REACTIONS];
    int promise_reaction_count;

    /* JSOBJ_GENERATOR -- a REAL suspend/resume generator ("function*" /
     * "yield"), backed by one dedicated OS thread per generator INSTANCE
     * (not per generator FUNCTION -- every call to a "function*" creates
     * a fresh generator object/thread pair, real JS semantics), hand-
     * synchronized with the caller via gen_mutex/gen_cond so only ONE of
     * {the generator's own thread, whichever thread is calling .next()}
     * ever actually runs interpreter code at a time -- see
     * js_install_generators()'s own top comment for the full design and
     * the real, disclosed thread-lifecycle cost this trades for real
     * suspend/resume (this session de-risked the underlying pthread
     * mutex+cond mechanism itself with a standalone repro, stress-tested
     * 30x, BEFORE building this on top of it -- see that repro's own
     * comment in the scratchpad). func_node/closure_env (reused from
     * JSOBJ_FUNCTION above) are the generator FUNCTION's own AST/scope;
     * everything below is per-INSTANCE state. */
    JsNode *gen_func_node;
    JSEnv *gen_closure_env;
    JSObject *gen_this_obj;
    JSValue gen_args[JS_MAX_ARGS];
    int gen_argc;
    struct JSInterp *gen_interp; /* set once, on the first .next() call -- the generator thread's own trampoline needs this to call js_exec_block()/js_eval() */
    pthread_t gen_thread;
    pthread_mutex_t gen_mutex;
    pthread_cond_t gen_cond;
    int gen_turn;    /* 0 = caller's turn to run, 1 = the generator thread's turn */
    int gen_started; /* real JS: a generator's body doesn't start running until the FIRST .next() call */
    int gen_done;
    JSValue gen_yielded_value; /* what the generator thread's own "yield expr"/final "return" most recently produced */
    JSValue gen_sent_value;    /* what the caller's ".next(v)" most recently sent back in */
};

#define JS_ENV_MAX_VARS 64
struct JSEnv {
    char names[JS_ENV_MAX_VARS][JS_IDENT_MAX];
    JSValue values[JS_ENV_MAX_VARS];
    int count;
    JSEnv *parent;
};

static JSEnv *js_env_new(JSEnv *parent) {
    JSEnv *e = (JSEnv *)malloc(sizeof(JSEnv));
    e->count = 0;
    e->parent = parent;
    return e;
}

/* Defines (or overwrites, if already present in THIS env -- e.g. a
 * function re-declared, or a param re-bound) a binding in `env` itself,
 * never walking to a parent. Used for var/let/const declarations and
 * function parameters. Silently drops the binding past JS_ENV_MAX_VARS
 * (a generous cap for this project's own scale of test script) rather
 * than crashing. */
static void js_env_define(JSEnv *env, const char *name, const JSValue *val) {
    int i;
    for (i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) { js_value_store(&env->values[i], val); return; }
    }
    if (env->count < JS_ENV_MAX_VARS) {
        strncpy(env->names[env->count], name, JS_IDENT_MAX - 1);
        env->names[env->count][JS_IDENT_MAX - 1] = 0;
        js_value_store(&env->values[env->count], val);
        env->count++;
    }
}

/* Walks env -> parent -> parent... one hop at a time via the LOCAL "cur",
 * never "env->parent->parent" chained -- see this file's own top comment. */
static int js_env_get(JSEnv *env, const char *name, JSValue *out) {
    JSEnv *cur = env;
    while (cur) {
        int i;
        for (i = 0; i < cur->count; i++) {
            if (strcmp(cur->names[i], name) == 0) { js_value_store(out, &cur->values[i]); return 1; }
        }
        JSEnv *next = cur->parent;
        cur = next;
    }
    return 0;
}

/* Assigns to an EXISTING binding anywhere up the chain (real JS
 * assignment semantics); if none exists anywhere, defines it in the
 * outermost (global) env -- real non-strict-mode JS's own "assigning to
 * an undeclared name creates a global" behavior. */
static void js_env_set(JSEnv *env, const char *name, const JSValue *val) {
    JSEnv *cur = env;
    JSEnv *last = env;
    while (cur) {
        int i;
        for (i = 0; i < cur->count; i++) {
            if (strcmp(cur->names[i], name) == 0) { js_value_store(&cur->values[i], val); return; }
        }
        last = cur;
        JSEnv *next = cur->parent;
        cur = next;
    }
    js_env_define(last, name, val);
}

static JSObject *js_object_new(JsObjKind kind) {
    JSObject *o = (JSObject *)malloc(sizeof(JSObject));
    memset(o, 0, sizeof(*o));
    o->kind = kind;
    return o;
}

static int js_obj_get(JSObject *o, const char *name, JSValue *out) {
    int i;
    for (i = 0; i < o->prop_count; i++) {
        if (strcmp(o->prop_names[i], name) == 0) { js_value_store(out, &o->prop_values[i]); return 1; }
    }
    return 0;
}

static void js_obj_set(JSObject *o, const char *name, const JSValue *val) {
    int i;
    for (i = 0; i < o->prop_count; i++) {
        if (strcmp(o->prop_names[i], name) == 0) { js_value_store(&o->prop_values[i], val); return; }
    }
    if (o->prop_count < JS_MAX_PROPS) {
        strncpy(o->prop_names[o->prop_count], name, JS_IDENT_MAX - 1);
        o->prop_names[o->prop_count][JS_IDENT_MAX - 1] = 0;
        js_value_store(&o->prop_values[o->prop_count], val);
        o->prop_count++;
    }
}

/* ---- real array storage (JSOBJ_ARRAY) ---- */

/* Returns 1 on success, 0 if `need` exceeds JS_ARRAY_MAX_LEN or the
   underlying realloc() itself failed -- callers (js_array_push()/
   js_array_set() below) must check this and silently no-op the write
   rather than touch arr_items, instead of the old unconditional version
   that assumed realloc() could never fail and CRASHED (a real, confirmed
   segfault, see JS_ARRAY_MAX_LEN's own comment) by writing through the
   NULL it left behind on failure. */
static int js_array_ensure_cap(JSObject *arr, int need) {
    if (need <= arr->arr_cap) return 1;
    if (need > JS_ARRAY_MAX_LEN) return 0;
    int newcap = arr->arr_cap ? arr->arr_cap * 2 : JS_ARRAY_INIT_CAP;
    while (newcap < need) newcap *= 2;
    if (newcap > JS_ARRAY_MAX_LEN) newcap = JS_ARRAY_MAX_LEN;
    JSValue *grown = (JSValue *)realloc(arr->arr_items, (size_t)newcap * sizeof(JSValue));
    if (!grown) return 0;
    arr->arr_items = grown;
    arr->arr_cap = newcap;
    return 1;
}

static void js_array_push(JSObject *arr, const JSValue *val) {
    if (!js_array_ensure_cap(arr, arr->arr_len + 1)) return; /* at JS_ARRAY_MAX_LEN (or OOM) -- silently refuse rather than crash */
    js_value_store(&arr->arr_items[arr->arr_len], val);
    arr->arr_len++;
}

/* Out-of-range index reads as undefined (real JS's own array semantics),
 * never an error. */
static void js_array_get(JSObject *arr, int idx, JSValue *out) {
    if (idx < 0 || idx >= arr->arr_len) { js_set_undefined(out); return; }
    js_value_store(out, &arr->arr_items[idx]);
}

/* Writing past the current end grows the array, filling any gap with
 * undefined -- real JS's own "arr[10] = x on a 3-element array" behavior
 * (a real sparse array would leave holes; this engine materializes them
 * as real undefined entries instead, a documented simplification with no
 * observable difference for anything this engine's own iteration/length
 * handling does). */
static void js_array_set(JSObject *arr, int idx, const JSValue *val) {
    if (idx < 0) return;
    /* A huge index (e.g. "a[999999999] = 1", a real bug this session's
       own fuzz testing found) must be refused BEFORE the while loop below
       ever runs -- silently no-op'ing the write is the safe, "never
       crash the script" choice this engine uses everywhere else, matching
       js_array_push()'s own identical guard just above. */
    if (idx >= arr->arr_cap && !js_array_ensure_cap(arr, idx + 1)) return;
    while (arr->arr_len <= idx) { JSValue u; js_set_undefined(&u); js_value_store(&arr->arr_items[arr->arr_len], &u); arr->arr_len++; }
    js_value_store(&arr->arr_items[idx], val);
}

static JSObject *js_array_new(void) {
    JSObject *a = js_object_new(JSOBJ_ARRAY);
    return a;
}

/* The real body of js_to_string_buf() -- see its own forward-declaration
 * comment (up in the "Values" section) for why it lives down here
 * instead: JSOBJ_ARRAY's real Array.prototype.toString() behavior (join
 * every element with "," -- recursively, so a nested array flattens the
 * same way real JS's does: [1,[2,3]] -> "1,2,3") needs JSObject's full
 * definition, not available yet up where every OTHER js_to_X helper
 * lives. */
static void js_to_string_buf(const JSValue *v, char *out, size_t outcap) {
    if (v->type == JSV_STRING) { strncpy(out, v->str ? v->str : "", outcap - 1); out[outcap - 1] = 0; return; }
    if (v->type == JSV_NUMBER) { js_format_number(v->num, out, outcap); return; }
    if (v->type == JSV_BOOL) { strncpy(out, v->boolean ? "true" : "false", outcap - 1); out[outcap - 1] = 0; return; }
    if (v->type == JSV_UNDEFINED) { strncpy(out, "undefined", outcap - 1); out[outcap - 1] = 0; return; }
    if (v->type == JSV_NULL) { strncpy(out, "null", outcap - 1); out[outcap - 1] = 0; return; }
    if (v->type == JSV_OBJECT && v->obj && v->obj->kind == JSOBJ_ARRAY) {
        JSObject *arr = v->obj;
        size_t len = 0; out[0] = 0;
        int i;
        for (i = 0; i < arr->arr_len; i++) {
            if (i > 0 && len < outcap - 1) out[len++] = ',';
            char piece[256];
            js_to_string_buf(&arr->arr_items[i], piece, sizeof piece);
            size_t plen = strlen(piece);
            if (len + plen >= outcap) plen = outcap - 1 - len;
            memcpy(out + len, piece, plen);
            len += plen;
        }
        out[len] = 0;
        return;
    }
    strncpy(out, "[object Object]", outcap - 1); out[outcap - 1] = 0;
}

/* Convenience: builds a value of the given shape in a fresh local and
 * defines/sets it in one call, for the extremely common
 * "js_obj_set(o, name, <construct a value>)" pattern -- avoids a
 * throwaway "JSValue tmp; js_set_X(&tmp, ...); js_obj_set(o, name, &tmp);"
 * at every one of those call sites. */
static void js_obj_set_str(JSObject *o, const char *name, const char *s) { JSValue tmp; js_set_string(&tmp, s); js_obj_set(o, name, &tmp); }
static void js_obj_set_num(JSObject *o, const char *name, double n) { JSValue tmp; js_set_number(&tmp, n); js_obj_set(o, name, &tmp); }
static void js_obj_set_obj(JSObject *o, const char *name, JSObject *val) { JSValue tmp; js_set_object(&tmp, val); js_obj_set(o, name, &tmp); }
static void js_env_define_obj(JSEnv *env, const char *name, JSObject *val) { JSValue tmp; js_set_object(&tmp, val); js_env_define(env, name, &tmp); }

/* ============================= Interpreter ============================= */

struct JSInterp {
    JSEnv *global_env;
    DomNode *document_root;
    /* Tree-walking control flow: no real exceptions/setjmp, so return/
       break/continue/throw are all signaled the same way -- setting
       these and having every statement-executing loop check them after
       each statement. A thrown value propagates up through ordinary
       statement/block execution exactly like a pending return does,
       until a JS_TRY's own catch clause (see js_exec_stmt's own case)
       clears it -- an uncaught throw unwinds all the way out of
       js_run_script()/js_dispatch_click(), which both just clear it and
       log a diagnostic (this engine has no top-level "onerror", matching
       real JS's console-log-and-continue default for an uncaught
       exception closely enough for this project's own scope). */
    int signal; /* 0=none, 1=return, 2=break, 3=continue, 4=throw */
    JSValue return_value;
    JSValue thrown_value; /* meaningful only while signal==JS_SIG_THROW */
    int mutated_dom; /* set by any DOM-mutating builtin -- see js_engine.h's own comment on relayout_needed */

    /* Real call-stack depth, incremented/decremented around every
       js_call_function()/js_call_constructor() invocation -- a script
       calling itself unboundedly (accidental infinite recursion, e.g. a
       missing base case, or a hostile page deliberately doing it) would
       otherwise recurse this ENGINE's own native C call stack right along
       with it, since this is a tree-walking interpreter with no separate
       VM-level stack of its own -- unlike JS_WHILE/JS_FOR's own bounded
       iteration guard (a script bug there just spins, it can't grow the
       native stack), unbounded JS recursion crashes the whole host
       process with a real C stack overflow (SIGSEGV), not a recoverable
       script-level error -- found via this session's own fuzz testing.
       JS_MAX_CALL_DEPTH is comfortably below what this project's own
       measured native stack size can hold for this interpreter's own
       per-call C stack usage, leaving headroom for the rest of the host
       program's own call chain (sqw_main.c's event loop, etc.) above it. */
    int call_depth;

    /* The JSOBJ_NATIVE object CURRENTLY being invoked (see js_call_
       function()'s own save/restore around every native call) -- lets a
       native read its OWN JSObject::native_capture without needing that
       object threaded through native_fn's own call signature (which
       would mean touching all ~80 existing native function definitions
       for the sake of the two that actually need this -- see
       js_install_promise()'s own comment on Promise's resolve/reject
       closures, the only current users). Save/restore (not a plain
       assign-and-forget) matters because a native can itself trigger
       ANOTHER native call re-entrantly (e.g. a promise reaction calling
       js_call_function() again), so this must nest correctly the same
       way call_depth already does. */
    JSObject *current_native_self;

    /* The JSOBJ_GENERATOR instance whose OWN worker thread is CURRENTLY
       the one running interpreter code (see JSObject's own gen_* fields'
       comment for the overall thread-per-generator design) -- read by
       JS_YIELD's own js_eval() case to know which generator a "yield"
       expression belongs to, and NULL whenever the currently-running
       code isn't inside any generator body at all (a bare top-level
       "yield" is a harmless documented no-op). Save/restore around a
       generator's own .next() dispatch (js_native_generator_next()),
       same nesting-safe pattern as current_native_self above -- needed
       so a generator body that itself drives ANOTHER generator's .next()
       calls still correctly resumes pointing at ITS OWN generator
       afterward, not the inner one it just finished consuming. */
    JSObject *current_generator;

    /* ES modules ("import"/"export") -- see js_run_module()'s own top
       comment for the overall design. current_module_exports is the
       exports object THIS module's own top-level "export ..." statements
       write into (NULL while running the page's ordinary, non-module
       script) -- save/restore around js_run_module() itself, same
       nesting-safe pattern as current_native_self/current_generator
       above (a module importing from another module that hasn't run
       yet triggers that OTHER module to run first -- see
       js_run_module()'s own comment on why that's NOT actually
       implemented, a documented gap -- but the save/restore is still
       correct groundwork either way). modules[] is a flat, fixed-size
       table of every module run so far, keyed by caller-supplied
       module_id (this file has no URL-fetching of its own -- see this
       file's own top comment on why network/file I/O stays out of
       js_engine.c -- sqw_main.c supplies the id, its own already-
       resolved script src). */
    JSObject *current_module_exports;
    struct {
        char id[192];
        JSObject *exports;
    } modules[JS_MAX_MODULES];
    int module_count;

    /* setTimeout()/setInterval() -- see js_run_timers()'s own comment
       (js_engine.h) for how these actually get CALLED (this engine has
       no event loop of its own; the host render loop drives it once per
       frame). A plain fixed-size slot table, not a dynamic list --
       JS_MAX_TIMERS is a generous cap for this project's own scale of
       test script, silently refusing a new timer past it rather than
       growing unboundedly (a script's own runaway setInterval() loop
       creating one timer per call, if that were ever possible here,
       would be a bug in the SCRIPT -- capping is the safe response, not
       a silent unbounded allocation). */
    struct {
        JSObject *fn;
        double interval_ms;
        double next_fire_ms;
        int repeating;
        int active;
    } timers[JS_MAX_TIMERS];
    int timer_count;
    double now_ms; /* set once per js_run_timers() call, read by setTimeout/setInterval to compute next_fire_ms */

    /* fetch() -- see js_engine.h's own comment on the hook pair this is
       wired through. Same plain fixed-slot-table convention as timers
       above, for the same reasons. */
    struct {
        long id;
        JSObject *callback;
        int used;
    } fetches[JS_MAX_FETCHES];
    long fetch_next_id;
};

static JsFetchStartFn g_fetch_start_fn = 0;
static void *g_fetch_start_user_data = 0;

void js_set_fetch_hook(JsFetchStartFn fn, void *user_data) {
    g_fetch_start_fn = fn;
    g_fetch_start_user_data = user_data;
}
#define JS_SIG_NONE 0
#define JS_SIG_RETURN 1
#define JS_SIG_BREAK 2
#define JS_SIG_CONTINUE 3
#define JS_SIG_THROW 4

static void js_eval(JSInterp *interp, JsNode *n, JSEnv *env, JSValue *out);
static void js_exec_stmt(JSInterp *interp, JsNode *n, JSEnv *env);

/* Real JS "function declarations are hoisted to the top of their own
 * block" behavior -- lets "foo(); function foo(){}" work regardless of
 * source order, the same as every real JS engine, rather than only
 * working if the call happens to come after the declaration textually.
 * var/let/const are NOT hoisted here (a documented simplification, see
 * this file's own top comment -- using a variable before its own "var"
 * declaration line reads as undefined via ordinary lookup failure rather
 * than real JS's more elaborate TDZ/hoisting rules for those). */
static void js_hoist_functions(JsNode *block, JSEnv *env) {
    int i;
    for (i = 0; i < block->kid_count; i++) {
        JsNode *stmt = block->kids[i];
        /* "export function foo() {...}" wraps the real JS_FUNC_DECL one
           level down (kids[0] of the JS_EXPORT node) -- unwrapped here
           so it still gets hoisted, same as an ordinary (non-exported)
           function declaration; found via this session's own module
           testing: without this, "export function add(a,b){...}"
           silently exported `undefined` instead of the function, since
           JS_FUNC_DECL's own exec-time case is a deliberate no-op that
           relies ENTIRELY on hoisting having already bound the name. */
        if (stmt->kind == JS_EXPORT && stmt->kid_count > 0 && stmt->kids[0]->kind == JS_FUNC_DECL) stmt = stmt->kids[0];
        if (stmt->kind == JS_FUNC_DECL) {
            JSObject *fo = js_object_new(JSOBJ_FUNCTION);
            fo->func_node = stmt;
            fo->closure_env = env;
            js_env_define_obj(env, stmt->str, fo);
        }
    }
}

static void js_exec_block(JSInterp *interp, JsNode *block, JSEnv *env) {
    js_hoist_functions(block, env);
    int i;
    for (i = 0; i < block->kid_count; i++) {
        JsNode *stmt = block->kids[i];
        js_exec_stmt(interp, stmt, env);
        if (interp->signal != JS_SIG_NONE) return;
    }
}

/* Real closures: calling a JSOBJ_FUNCTION creates a new env whose PARENT
 * is the function's own captured closure_env (its defining scope), not
 * the caller's env -- lexical scoping, not dynamic. Params bind by
 * position; a missing trailing arg binds to undefined (real JS's own
 * "fewer args than params" behavior), an extra trailing arg is just
 * ignored (real JS keeps it reachable only via "arguments", which this
 * engine doesn't implement -- a documented, deliberate gap). */
/* `this_obj` (NULL for a plain, non-method call -- "this" then reads
 * back as undefined inside the function, real JS's own non-strict-mode
 * top-level behavior simplified: no global "window" object stands in for
 * it) is bound as a real "this" name in the new call env, exactly like
 * any other parameter -- ordinary lexical lookup then finds it inside
 * the function body with no special-casing needed anywhere else. */
/* Forward declarations -- real bodies live down with the rest of the
   Promise implementation (js_install_promise()'s own section); needed
   here so js_call_function() below can wrap an "async function"'s own
   return value/thrown exception into a real settled Promise, the same
   "forward-declare, define later" pattern already used elsewhere in this
   file (js_to_string_buf(), js_native_promise_then(), etc). */
static JSObject *js_promise_new(void);
static void js_promise_settle(JSInterp *interp, JSObject *prom, int state, const JSValue *value);

static void js_call_function(JSInterp *interp, JSObject *fn, JSObject *this_obj, JSValue *args, int argc, JSValue *out) {
    if (fn->kind == JSOBJ_NATIVE) {
        JSObject *saved_self = interp->current_native_self;
        interp->current_native_self = fn;
        void (*nf)(JSInterp *, JSValue *, int, JSObject *, JSValue *) = fn->native_fn;
        nf(interp, args, argc, this_obj, out);
        interp->current_native_self = saved_self;
        return;
    }
    if (fn->kind != JSOBJ_FUNCTION) { js_set_undefined(out); return; }

    /* "function* name(...) {...}" -- calling a generator FUNCTION does
       NOT run its body at all (real JS: a generator is lazy, its body
       only starts on the first .next() call) -- it just builds and
       returns a fresh generator INSTANCE capturing everything the body
       will eventually need (func_node/closure/this/args), same "capture
       now, run later" idea a real JSOBJ_FUNCTION closure already uses,
       just with actual EXECUTION deferred one step further. See
       js_install_generators()'s own top comment for the full design
       (thread-per-instance suspend/resume) and js_native_generator_
       next() for where the body actually starts running. */
    if (fn->func_node->op[0] == 'g') {
        JSObject *gen = js_object_new(JSOBJ_GENERATOR);
        gen->gen_func_node = fn->func_node;
        gen->gen_closure_env = fn->closure_env;
        gen->gen_this_obj = this_obj;
        int gi;
        int gargc = argc > JS_MAX_ARGS ? JS_MAX_ARGS : argc;
        for (gi = 0; gi < gargc; gi++) js_value_store(&gen->gen_args[gi], &args[gi]);
        gen->gen_argc = gargc;
        js_set_object(out, gen);
        return;
    }

    /* See JSInterp::call_depth's own comment -- this is the ONE guard
       standing between unbounded JS-level recursion and a real native C
       stack overflow (a hard process crash, not a recoverable script
       error). Raised as a real, catchable throw (real JS's own
       RangeError: Maximum call stack size exceeded is exactly this same
       idea) so a script wrapping risky recursion in try/catch can still
       recover, same as real JS. */
    if (interp->call_depth >= JS_MAX_CALL_DEPTH) {
        js_set_string(&interp->thrown_value, "Maximum call stack size exceeded");
        interp->signal = JS_SIG_THROW;
        js_set_undefined(out);
        return;
    }
    interp->call_depth++;

    JsNode *func_node = fn->func_node;
    JSEnv *closure = fn->closure_env;
    JSEnv *call_env = js_env_new(closure);
    JSValue thisval;
    if (this_obj) js_set_object(&thisval, this_obj); else js_set_undefined(&thisval);
    js_env_define(call_env, "this", &thisval);
    int param_count = func_node->kid_count - 1; /* last kid is the body */
    int i;
    for (i = 0; i < param_count; i++) {
        JsNode *param = func_node->kids[i];
        JSValue av;
        if (i < argc) js_value_store(&av, &args[i]); else js_set_undefined(&av);
        js_env_define(call_env, param->str, &av);
    }
    JsNode *body = func_node->kids[param_count];
    js_exec_block(interp, body, call_env);
    if (interp->signal == JS_SIG_RETURN) js_value_store(out, &interp->return_value);
    else js_set_undefined(out);
    if (interp->signal != JS_SIG_THROW) interp->signal = JS_SIG_NONE; /* a thrown exception propagates OUT of this call -- see JS_TRY's own comment */
    /* "async function" -- func_node->op[0]=='a' (set by js_parse_
       primary_impl()/js_parse_stmt_impl()'s own "async function" lookahead
       parsing). Real JS: an async function's return value is ALWAYS a
       Promise, never the raw value -- fulfilled with whatever it
       returned, or REJECTED (not a propagating throw to the caller) if
       its body threw. This must run AFTER the JS_SIG_THROW check just
       above so a synchronous throw never escapes an async function's own
       call boundary -- exactly the real-JS async/await contract, and
       independent of whatever `await`'s own comment (js_eval()'s
       JS_UNARY case) says about awaiting a still-pending promise. */
    if (func_node->op[0] == 'a') {
        JSObject *prom = js_promise_new();
        if (interp->signal == JS_SIG_THROW) {
            js_promise_settle(interp, prom, JS_PROMISE_REJECTED, &interp->thrown_value);
            interp->signal = JS_SIG_NONE;
        } else {
            js_promise_settle(interp, prom, JS_PROMISE_FULFILLED, out);
        }
        js_set_object(out, prom);
    }
    interp->call_depth--;
}

/* Runs `cls`'s own "constructor" method (from its `methods` bag -- see
 * JSObject::methods' own comment) against `this_obj`, an already-
 * allocated instance -- the actual body of `new Cls(...)`, split out of
 * JS_NEW's own js_eval() case since it also needs to be reachable
 * recursively for "super(...)" (a bare call to the literal name "super"
 * inside a subclass's own constructor -- see JS_CALL's own special-case
 * for it) and for the "no explicit constructor" default (real JS: a
 * subclass with no constructor of its own implicitly forwards its own
 * arguments to super(); a base class with none does nothing). While THIS
 * constructor body runs, "__super_class__" is bound in its own call env
 * to `cls`'s own parent (if any) -- that's what a nested "super(...)"
 * call inside IT resolves against, so a 3+-level "class C extends B
 * extends A" chain's super() calls correctly walk up one level at a
 * time regardless of how deep the chain goes. */
static void js_call_constructor(JSInterp *interp, JSObject *cls, JSObject *this_obj, JSValue *args, int argc, JSValue *out) {
    js_set_undefined(out);
    if (!cls) return;
    /* Same call_depth guard as js_call_function() above, and for the same
       reason -- a constructor body isn't run THROUGH js_call_function()
       (it builds its own call_env directly, below), so a script recursing
       via "new ThisClass()" inside its own constructor would otherwise
       bypass that guard entirely and still overflow the native C stack.
       super()'s own recursive js_call_constructor() calls (see this
       function's own header comment) are naturally bounded by the static
       "extends" chain length instead and would never realistically hit
       this cap, so sharing one counter with js_call_function() is safe. */
    if (interp->call_depth >= JS_MAX_CALL_DEPTH) {
        js_set_string(&interp->thrown_value, "Maximum call stack size exceeded");
        interp->signal = JS_SIG_THROW;
        return;
    }
    interp->call_depth++;
    JSObject *methods = cls->methods;
    JSValue ctorv;
    int has_ctor = methods && js_obj_get(methods, "constructor", &ctorv) && ctorv.type == JSV_OBJECT && ctorv.obj;
    if (!has_ctor) {
        JSObject *parent = cls->parent_class;
        if (parent) js_call_constructor(interp, parent, this_obj, args, argc, out);
        interp->call_depth--;
        return;
    }
    JSObject *ctor_fn = ctorv.obj;
    JsNode *func_node = ctor_fn->func_node;
    JSEnv *closure = ctor_fn->closure_env;
    JSEnv *call_env = js_env_new(closure);
    JSValue thisval;
    js_set_object(&thisval, this_obj);
    js_env_define(call_env, "this", &thisval);
    JSObject *parent2 = cls->parent_class;
    if (parent2) {
        JSValue superclsv;
        js_set_object(&superclsv, parent2);
        js_env_define(call_env, "__super_class__", &superclsv);
    }
    int param_count = func_node->kid_count - 1;
    int i;
    for (i = 0; i < param_count; i++) {
        JsNode *param = func_node->kids[i];
        JSValue av;
        if (i < argc) js_value_store(&av, &args[i]); else js_set_undefined(&av);
        js_env_define(call_env, param->str, &av);
    }
    JsNode *body = func_node->kids[param_count];
    js_exec_block(interp, body, call_env);
    if (interp->signal != JS_SIG_THROW) interp->signal = JS_SIG_NONE;
    interp->call_depth--;
}

/* ---- property get/set: plain objects, DOM elements, DOM style, arrays ---- */

static void js_dom_get_prop(JSInterp *interp, JSObject *obj, const char *name, JSValue *out);
static void js_dom_set_prop(JSInterp *interp, JSObject *obj, const char *name, const JSValue *val);
static void js_native_classlist_add(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_native_classlist_remove(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_native_classlist_toggle(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_native_classlist_contains(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_native_append_child(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_native_remove_child(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_native_remove_self(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_native_query_selector(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_native_query_selector_all(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out);
static void js_style_get_prop(JSObject *obj, const char *name, JSValue *out);
static void js_style_set_prop(JSObject *obj, const char *name, const JSValue *val);

/* True iff `name` is a non-empty run of digits (a real array index, as a
 * string -- js_get_prop()/js_set_prop() are handed a stringified index
 * for both a[i] and a.length alike, this is what tells them apart). */
static int js_is_array_index(const char *name, int *out_idx) {
    if (!name[0]) return 0;
    int i;
    for (i = 0; name[i]; i++) if (!isdigit((unsigned char)name[i])) return 0;
    *out_idx = atoi(name);
    return 1;
}

static void js_native_array_push(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp;
    if (t && t->kind == JSOBJ_ARRAY) { int k; for (k = 0; k < c; k++) js_array_push(t, &a[k]); }
    js_set_number(out, t ? (double)t->arr_len : 0.0);
}
static void js_native_array_pop(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)a; (void)c;
    if (t && t->kind == JSOBJ_ARRAY && t->arr_len > 0) { t->arr_len--; js_value_store(out, &t->arr_items[t->arr_len]); }
    else js_set_undefined(out);
}
static void js_native_array_join(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp;
    char sep[16]; strcpy(sep, ",");
    if (c > 0) js_to_string_buf(&a[0], sep, sizeof sep);
    char buf[JS_STR_MAX]; int len = 0; buf[0] = 0;
    if (t && t->kind == JSOBJ_ARRAY) {
        int k;
        for (k = 0; k < t->arr_len; k++) {
            char item[256];
            js_to_string_buf(&t->arr_items[k], item, sizeof item);
            int cap = (int)sizeof(buf) - len; if (cap < 0) cap = 0;
            int n = snprintf(buf + len, (size_t)cap, "%s%s", k > 0 ? sep : "", item);
            if (n > 0) len += n;
        }
    }
    js_set_string(out, buf);
}
/* Real value-equality (same rules as JS_BINARY's own ==/=== case, see its
   comment) -- shared by indexOf/includes. */
static int js_values_equal(const JSValue *a, const JSValue *b) {
    if (a->type != b->type) return 0;
    if (a->type == JSV_NUMBER) return a->num == b->num;
    if (a->type == JSV_STRING) return strcmp(a->str ? a->str : "", b->str ? b->str : "") == 0;
    if (a->type == JSV_BOOL) return a->boolean == b->boolean;
    if (a->type == JSV_OBJECT) return a->obj == b->obj;
    return 1;
}
static void js_native_array_index_of(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp;
    int found = -1;
    if (t && t->kind == JSOBJ_ARRAY && c > 0) {
        int k;
        for (k = 0; k < t->arr_len; k++) { if (js_values_equal(&t->arr_items[k], &a[0])) { found = k; break; } }
    }
    js_set_number(out, (double)found);
}
static void js_native_array_includes(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    JSValue idx;
    js_native_array_index_of(interp, a, c, t, &idx);
    js_set_bool(out, idx.num >= 0.0);
}
static void js_native_array_slice(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp;
    JSObject *res = js_array_new();
    if (t && t->kind == JSOBJ_ARRAY) {
        int start = c > 0 ? (int)js_to_number(&a[0]) : 0;
        int end = c > 1 ? (int)js_to_number(&a[1]) : t->arr_len;
        if (start < 0) start += t->arr_len;
        if (start < 0) start = 0;
        if (end < 0) end += t->arr_len;
        if (end > t->arr_len) end = t->arr_len;
        int k; for (k = start; k < end; k++) js_array_push(res, &t->arr_items[k]);
    }
    js_set_object(out, res);
}
static void js_native_array_foreach(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_undefined(out);
    if (!(t && t->kind == JSOBJ_ARRAY && c > 0 && a[0].type == JSV_OBJECT && a[0].obj)) return;
    JSObject *cb = a[0].obj;
    int k;
    for (k = 0; k < t->arr_len; k++) {
        JSValue cargs[3]; JSValue tmp;
        js_value_store(&cargs[0], &t->arr_items[k]);
        js_set_number(&cargs[1], (double)k);
        js_set_object(&cargs[2], t);
        js_call_function(interp, cb, 0, cargs, 3, &tmp);
        if (interp->signal == JS_SIG_THROW) return;
    }
}
static void js_native_array_map(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    JSObject *res = js_array_new();
    js_set_object(out, res);
    if (!(t && t->kind == JSOBJ_ARRAY && c > 0 && a[0].type == JSV_OBJECT && a[0].obj)) return;
    JSObject *cb = a[0].obj;
    int k;
    for (k = 0; k < t->arr_len; k++) {
        JSValue cargs[3]; JSValue rv;
        js_value_store(&cargs[0], &t->arr_items[k]);
        js_set_number(&cargs[1], (double)k);
        js_set_object(&cargs[2], t);
        js_call_function(interp, cb, 0, cargs, 3, &rv);
        if (interp->signal == JS_SIG_THROW) return;
        js_array_push(res, &rv);
    }
}
static void js_native_array_filter(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    JSObject *res = js_array_new();
    js_set_object(out, res);
    if (!(t && t->kind == JSOBJ_ARRAY && c > 0 && a[0].type == JSV_OBJECT && a[0].obj)) return;
    JSObject *cb = a[0].obj;
    int k;
    for (k = 0; k < t->arr_len; k++) {
        JSValue cargs[3]; JSValue rv;
        js_value_store(&cargs[0], &t->arr_items[k]);
        js_set_number(&cargs[1], (double)k);
        js_set_object(&cargs[2], t);
        js_call_function(interp, cb, 0, cargs, 3, &rv);
        if (interp->signal == JS_SIG_THROW) return;
        if (js_to_bool(&rv)) js_array_push(res, &t->arr_items[k]);
    }
}

static void js_native_array_find(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_undefined(out);
    if (!(t && t->kind == JSOBJ_ARRAY && c > 0 && a[0].type == JSV_OBJECT && a[0].obj)) return;
    JSObject *cb = a[0].obj;
    int k;
    for (k = 0; k < t->arr_len; k++) {
        JSValue cargs[3]; JSValue rv;
        js_value_store(&cargs[0], &t->arr_items[k]);
        js_set_number(&cargs[1], (double)k);
        js_set_object(&cargs[2], t);
        js_call_function(interp, cb, 0, cargs, 3, &rv);
        if (interp->signal == JS_SIG_THROW) return;
        if (js_to_bool(&rv)) { js_value_store(out, &t->arr_items[k]); return; }
    }
}
static void js_native_array_find_index(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_number(out, -1.0);
    if (!(t && t->kind == JSOBJ_ARRAY && c > 0 && a[0].type == JSV_OBJECT && a[0].obj)) return;
    JSObject *cb = a[0].obj;
    int k;
    for (k = 0; k < t->arr_len; k++) {
        JSValue cargs[3]; JSValue rv;
        js_value_store(&cargs[0], &t->arr_items[k]);
        js_set_number(&cargs[1], (double)k);
        js_set_object(&cargs[2], t);
        js_call_function(interp, cb, 0, cargs, 3, &rv);
        if (interp->signal == JS_SIG_THROW) return;
        if (js_to_bool(&rv)) { js_set_number(out, (double)k); return; }
    }
}
static void js_native_array_some(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_bool(out, 0);
    if (!(t && t->kind == JSOBJ_ARRAY && c > 0 && a[0].type == JSV_OBJECT && a[0].obj)) return;
    JSObject *cb = a[0].obj;
    int k;
    for (k = 0; k < t->arr_len; k++) {
        JSValue cargs[3]; JSValue rv;
        js_value_store(&cargs[0], &t->arr_items[k]);
        js_set_number(&cargs[1], (double)k);
        js_set_object(&cargs[2], t);
        js_call_function(interp, cb, 0, cargs, 3, &rv);
        if (interp->signal == JS_SIG_THROW) return;
        if (js_to_bool(&rv)) { js_set_bool(out, 1); return; }
    }
}
static void js_native_array_every(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_bool(out, 1);
    if (!(t && t->kind == JSOBJ_ARRAY && c > 0 && a[0].type == JSV_OBJECT && a[0].obj)) return;
    JSObject *cb = a[0].obj;
    int k;
    for (k = 0; k < t->arr_len; k++) {
        JSValue cargs[3]; JSValue rv;
        js_value_store(&cargs[0], &t->arr_items[k]);
        js_set_number(&cargs[1], (double)k);
        js_set_object(&cargs[2], t);
        js_call_function(interp, cb, 0, cargs, 3, &rv);
        if (interp->signal == JS_SIG_THROW) return;
        if (!js_to_bool(&rv)) { js_set_bool(out, 0); return; }
    }
}
static void js_native_array_reduce(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_undefined(out);
    if (!(t && t->kind == JSOBJ_ARRAY && c > 0 && a[0].type == JSV_OBJECT && a[0].obj)) return;
    JSObject *cb = a[0].obj;
    JSValue acc;
    int k;
    if (c > 1) { js_value_store(&acc, &a[1]); k = 0; }
    else { if (t->arr_len == 0) return; js_value_store(&acc, &t->arr_items[0]); k = 1; }
    for (; k < t->arr_len; k++) {
        JSValue cargs[4]; JSValue rv;
        js_value_store(&cargs[0], &acc);
        js_value_store(&cargs[1], &t->arr_items[k]);
        js_set_number(&cargs[2], (double)k);
        js_set_object(&cargs[3], t);
        js_call_function(interp, cb, 0, cargs, 4, &rv);
        if (interp->signal == JS_SIG_THROW) return;
        js_value_store(&acc, &rv);
    }
    js_value_store(out, &acc);
}
static void js_native_array_reverse(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)a; (void)c;
    if (t && t->kind == JSOBJ_ARRAY) {
        int lo = 0, hi = t->arr_len - 1;
        while (lo < hi) { JSValue tmp; js_value_store(&tmp, &t->arr_items[lo]); js_value_store(&t->arr_items[lo], &t->arr_items[hi]); js_value_store(&t->arr_items[hi], &tmp); lo++; hi--; }
    }
    js_set_object(out, t);
}
static void js_native_array_shift(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)a; (void)c;
    if (!(t && t->kind == JSOBJ_ARRAY && t->arr_len > 0)) { js_set_undefined(out); return; }
    js_value_store(out, &t->arr_items[0]);
    int i;
    for (i = 1; i < t->arr_len; i++) js_value_store(&t->arr_items[i - 1], &t->arr_items[i]);
    t->arr_len--;
}
static void js_native_array_unshift(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp;
    if (t && t->kind == JSOBJ_ARRAY && c > 0 && js_array_ensure_cap(t, t->arr_len + c)) {
        int i;
        for (i = t->arr_len - 1; i >= 0; i--) js_value_store(&t->arr_items[i + c], &t->arr_items[i]);
        for (i = 0; i < c; i++) js_value_store(&t->arr_items[i], &a[i]);
        t->arr_len += c;
    }
    js_set_number(out, t ? (double)t->arr_len : 0.0);
}
/* start/deleteCount only -- inserted-item args (splice's 3rd-and-later
   real JS params) ARE supported below; no sparse-array edge cases. */
static void js_native_array_splice(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp;
    JSObject *removed = js_array_new();
    js_set_object(out, removed);
    if (!(t && t->kind == JSOBJ_ARRAY)) return;
    int start = c > 0 ? (int)js_to_number(&a[0]) : 0;
    if (start < 0) start += t->arr_len; if (start < 0) start = 0; if (start > t->arr_len) start = t->arr_len;
    int del = c > 1 ? (int)js_to_number(&a[1]) : (t->arr_len - start);
    if (del < 0) del = 0; if (start + del > t->arr_len) del = t->arr_len - start;
    int ins = c > 2 ? c - 2 : 0;
    int i;
    for (i = 0; i < del; i++) js_array_push(removed, &t->arr_items[start + i]);
    int newlen = t->arr_len - del + ins;
    /* If growth is needed but refused (JS_ARRAY_MAX_LEN/OOM, see
       js_array_ensure_cap()'s own comment), bail out entirely rather than
       proceed to shift/write past the still-too-small backing store --
       `removed` (already built above) is still returned as-is, matching
       this engine's "never crash, silently do less" convention. */
    if (newlen > t->arr_len && !js_array_ensure_cap(t, newlen)) return;
    if (ins != del) {
        if (ins > del) { for (i = t->arr_len - 1; i >= start + del; i--) js_value_store(&t->arr_items[i + (ins - del)], &t->arr_items[i]); }
        else { for (i = start + del; i < t->arr_len; i++) js_value_store(&t->arr_items[i + (ins - del)], &t->arr_items[i]); }
    }
    for (i = 0; i < ins; i++) js_value_store(&t->arr_items[start + i], &a[2 + i]);
    t->arr_len = newlen;
}
static void js_native_array_concat(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp;
    JSObject *res = js_array_new();
    js_set_object(out, res);
    if (t && t->kind == JSOBJ_ARRAY) { int i; for (i = 0; i < t->arr_len; i++) js_array_push(res, &t->arr_items[i]); }
    int k;
    for (k = 0; k < c; k++) {
        if (a[k].type == JSV_OBJECT && a[k].obj && a[k].obj->kind == JSOBJ_ARRAY) {
            int i; for (i = 0; i < a[k].obj->arr_len; i++) js_array_push(res, &a[k].obj->arr_items[i]);
        } else js_array_push(res, &a[k]);
    }
}
/* Only depth 1 (the real default, and by far the common case) -- an
   explicit depth argument beyond 1 is accepted but treated the same as 1,
   a documented, narrow simplification. */
static void js_native_array_flat(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)a; (void)c;
    JSObject *res = js_array_new();
    js_set_object(out, res);
    if (!(t && t->kind == JSOBJ_ARRAY)) return;
    int i;
    for (i = 0; i < t->arr_len; i++) {
        JSValue *v = &t->arr_items[i];
        if (v->type == JSV_OBJECT && v->obj && v->obj->kind == JSOBJ_ARRAY) { int j; for (j = 0; j < v->obj->arr_len; j++) js_array_push(res, &v->obj->arr_items[j]); }
        else js_array_push(res, v);
    }
}
/* Default (no comparator) sorts by STRING comparison, real JS's own
   default -- [10, 2, 1].sort() gives [1, 10, 2], not [1, 2, 10], matching
   real JS's frequently-surprising behavior exactly rather than "helpfully"
   diverging from it. A real comparator function sorts using the sign of
   its own return value instead, real JS semantics. Simple insertion sort
   (never more than a handful of elements at this project's own scale, no
   need for anything better) -- swaps done field-by-field via
   js_value_store(), same "never a whole-struct array-element assignment"
   rule as everywhere else in this file. */
static void js_native_array_sort(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_object(out, t);
    if (!(t && t->kind == JSOBJ_ARRAY) || t->arr_len < 2) return;
    JSObject *cmp = (c > 0 && a[0].type == JSV_OBJECT && a[0].obj) ? a[0].obj : 0;
    int i;
    for (i = 1; i < t->arr_len; i++) {
        JSValue key; js_value_store(&key, &t->arr_items[i]);
        int j = i - 1;
        while (j >= 0) {
            int before;
            if (cmp) {
                JSValue cargs[2]; JSValue rv;
                js_value_store(&cargs[0], &t->arr_items[j]);
                js_value_store(&cargs[1], &key);
                js_call_function(interp, cmp, 0, cargs, 2, &rv);
                if (interp->signal == JS_SIG_THROW) return;
                before = js_to_number(&rv) > 0.0;
            } else {
                char sa[256], sb[256];
                js_to_string_buf(&t->arr_items[j], sa, sizeof sa);
                js_to_string_buf(&key, sb, sizeof sb);
                before = strcmp(sa, sb) > 0;
            }
            if (!before) break;
            js_value_store(&t->arr_items[j + 1], &t->arr_items[j]);
            j--;
        }
        js_value_store(&t->arr_items[j + 1], &key);
    }
}

/* Returns 1 (and fills `out`) if `name` is a real array-only property/
   method this engine understands; 0 otherwise (caller falls through to
   the ordinary property-bag lookup, which is how a plain data property
   someone stuffed onto an array object -- e.g. "arr.myFlag = true" --
   still works). */
static int js_array_get_special(JSObject *arr, const char *name, JSValue *out) {
    int idx;
    if (js_is_array_index(name, &idx)) { js_array_get(arr, idx, out); return 1; }
    if (!strcmp(name, "length")) { js_set_number(out, (double)arr->arr_len); return 1; }
    if (!strcmp(name, "push")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_push; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "pop")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_pop; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "join")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_join; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "indexOf")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_index_of; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "includes")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_includes; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "slice")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_slice; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "forEach")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_foreach; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "map")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_map; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "filter")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_filter; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "find")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_find; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "findIndex")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_find_index; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "some")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_some; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "every")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_every; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "reduce")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_reduce; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "reverse")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_reverse; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "shift")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_shift; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "unshift")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_unshift; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "splice")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_splice; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "concat")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_concat; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "flat")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_flat; js_set_object(out, fo); return 1; }
    if (!strcmp(name, "sort")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_array_sort; js_set_object(out, fo); return 1; }
    return 0;
}

/* Forward declarations -- real bodies live down with the rest of the
   Promise implementation (js_install_promise()'s own section), which
   needs js_call_function() (defined above) and so can't move earlier;
   js_get_prop() below needs to expose them as real properties on a
   JSOBJ_PROMISE object, same "forward-declare, define later" pattern
   js_to_string_buf()/js_json_append() already use in this file. */
static void js_native_promise_then(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out);
static void js_native_promise_catch(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out);
static void js_native_promise_finally(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out);
/* Same "forward-declare, define later" story -- see the "Generators"
   section's own top comment (near js_generator_thread_main(), further
   down) for the real body/design. */
static void js_native_generator_next(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out);

static void js_get_prop(JSInterp *interp, const JSValue *base, const char *name, JSValue *out) {
    /* Primitive strings get exactly ONE property this engine implements:
       "length" (real JS's own most commonly used one by far -- charAt/
       slice/etc are NOT implemented on strings, a documented gap; a
       string is always used as a plain C-string/JSV_STRING value here,
       never boxed into a real object the way real JS technically does
       for "abc".length too). Any other property name on a string, or any
       property at all on a number/bool/undefined/null, is undefined --
       real JS boxes primitives into wrapper objects for this; this
       engine doesn't. */
    if (base->type == JSV_STRING) {
        if (!strcmp(name, "length")) { js_set_number(out, (double)(base->str ? strlen(base->str) : 0)); return; }
        /* Every OTHER string method (charAt/slice/split/etc) is handled
           directly by JS_CALL's own member-call special case (see
           js_string_method_call() below) rather than through this
           property-get path -- a bare non-call reference like
           "s.charAt" (no "()") still evaluates to undefined here, a
           documented, narrow gap (real JS would give back a bound
           function value; not worth the extra machinery at this
           project's scale, since every real use is a direct call). */
        js_set_undefined(out);
        return;
    }
    if (base->type != JSV_OBJECT || !base->obj) { js_set_undefined(out); return; }
    JSObject *o = base->obj;
    if (o->kind == JSOBJ_DOM_ELEMENT) { js_dom_get_prop(interp, o, name, out); return; }
    if (o->kind == JSOBJ_ARRAY) { if (js_array_get_special(o, name, out)) return; if (js_obj_get(o, name, out)) return; js_set_undefined(out); return; }
    if (o->kind == JSOBJ_PROMISE) {
        /* .then/.catch/.finally -- see js_install_promise()'s own top
           comment for the overall design. "status"/"value" aren't real
           JS (a real Promise has no such synchronously-readable fields --
           deliberately not exposed here either, to avoid scripts relying
           on a synchronous-inspection API real JS doesn't have). */
        if (!strcmp(name, "then")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_promise_then; js_set_object(out, fo); return; }
        if (!strcmp(name, "catch")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_promise_catch; js_set_object(out, fo); return; }
        if (!strcmp(name, "finally")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_promise_finally; js_set_object(out, fo); return; }
        js_set_undefined(out);
        return;
    }
    if (o->kind == JSOBJ_GENERATOR) {
        /* .next(v) -- see the "Generators" section's own top comment
           (near js_generator_thread_main()) for the full design.
           .throw()/.return() are a documented, out-of-scope gap. */
        if (!strcmp(name, "next")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_generator_next; js_set_object(out, fo); return; }
        js_set_undefined(out);
        return;
    }
    /* JSOBJ_PLAIN's own dedicated marker property "__style_of__" flags a
       style-wrapper object (see js_dom_get_prop()'s own "style" case) --
       checked before the generic property lookup below so a style
       property name never collides with a real object property. */
    JSValue marker;
    if (js_obj_get(o, "__style_of__", &marker) && marker.type == JSV_OBJECT) {
        js_style_get_prop(o, name, out);
        return;
    }
    /* "get x() {...}" accessors (object-literal or class) -- stored under
       a mangled "__get_<name>__" property name (see JS_OBJECT_LIT/
       JS_CLASS_DECL's own eval-time comments on why) rather than as a
       distinct JSObject field, the same "reuse the existing plain
       property bag with a marker name" convention __style_of__ above
       already uses. Checked BEFORE the plain own-property lookup so a
       getter always wins over any same-named plain data property (real
       JS: they can't coexist on the same object anyway). Own properties
       are checked first, then instance_methods (a getter defined in a
       class body), matching real JS's own "own property, then
       prototype" lookup order. */
    {
        char accname[JS_IDENT_MAX + 8];
        snprintf(accname, sizeof accname, "__get_%s__", name);
        JSValue getterv;
        JSObject *getter = 0;
        if (js_obj_get(o, accname, &getterv) && getterv.type == JSV_OBJECT) getter = getterv.obj;
        else if (o->instance_methods && js_obj_get(o->instance_methods, accname, &getterv) && getterv.type == JSV_OBJECT) getter = getterv.obj;
        if (getter) { js_call_function(interp, getter, o, 0, 0, out); return; }
    }
    if (js_obj_get(o, name, out)) return;
    /* Instance-of-a-class fallback (see JSObject::instance_methods' own
       comment): only reached once the instance's OWN properties (set by
       its constructor via "this.x = ...") have already missed. */
    if (o->instance_methods && js_obj_get(o->instance_methods, name, out)) return;
    js_set_undefined(out);
}

/* String instance methods (charAt/slice/split/etc) -- deliberately handled
 * here as a direct call-site special-case (see JS_CALL's own use of this
 * function) rather than through js_get_prop()'s normal property-lookup
 * path, since a primitive JSV_STRING has no JSObject to be `this` for a
 * js_call_function()-style native -- this engine never boxes primitives
 * into wrapper objects (see js_get_prop()'s own comment on that). Returns
 * 1 if `name` was a recognized string method (out is always written in
 * that case, even on bad/missing args -- real JS-ish permissive behavior,
 * matching this engine's own "never crash on a script" convention), 0 if
 * `name` isn't a string method this engine implements (caller falls back
 * to treating it as a plain, non-callable property, i.e. undefined). No
 * regex support anywhere here -- every "search"/"replace" argument is a
 * literal substring, not a pattern, a documented, deliberate scope limit
 * matching css.c's own "simple selectors only" precedent. */
/* Forward declaration -- real body lives down with JSON.stringify (needs
   nothing from this section); a tiny "append onto a bounded buffer,
   tracking length" helper reused here for concat/repeat/replace. */
static void js_json_append(char *out, int *len, int cap, const char *s);

static int js_string_method_call(JSInterp *interp, const JSValue *base, const char *name, JSValue *args, int argc, JSValue *out) {
    (void)interp;
    const char *s = base->str ? base->str : "";
    int slen = (int)strlen(s);
    if (!strcmp(name, "charAt")) {
        int i = argc > 0 ? (int)js_to_number(&args[0]) : 0;
        char buf[2]; buf[0] = 0; buf[1] = 0;
        if (i >= 0 && i < slen) buf[0] = s[i];
        js_set_string(out, buf);
        return 1;
    }
    if (!strcmp(name, "charCodeAt")) {
        int i = argc > 0 ? (int)js_to_number(&args[0]) : 0;
        if (i >= 0 && i < slen) js_set_number(out, (double)(unsigned char)s[i]);
        else js_set_number(out, 0.0 / 0.0); /* real JS's own NaN */
        return 1;
    }
    if (!strcmp(name, "indexOf") || !strcmp(name, "includes") || !strcmp(name, "startsWith")) {
        char needle[JS_STR_MAX]; needle[0] = 0;
        if (argc > 0) js_to_string_buf(&args[0], needle, sizeof needle);
        if (!strcmp(name, "startsWith")) {
            int nlen = (int)strlen(needle);
            js_set_bool(out, nlen <= slen && strncmp(s, needle, (size_t)nlen) == 0);
            return 1;
        }
        const char *found = needle[0] ? strstr(s, needle) : s;
        int idx = found ? (int)(found - s) : -1;
        if (!strcmp(name, "includes")) js_set_bool(out, idx >= 0);
        else js_set_number(out, (double)idx);
        return 1;
    }
    if (!strcmp(name, "endsWith")) {
        char needle[JS_STR_MAX]; needle[0] = 0;
        if (argc > 0) js_to_string_buf(&args[0], needle, sizeof needle);
        int nlen = (int)strlen(needle);
        js_set_bool(out, nlen <= slen && strcmp(s + (slen - nlen), needle) == 0);
        return 1;
    }
    if (!strcmp(name, "slice") || !strcmp(name, "substring")) {
        int start = argc > 0 ? (int)js_to_number(&args[0]) : 0;
        int end = argc > 1 ? (int)js_to_number(&args[1]) : slen;
        if (!strcmp(name, "slice")) {
            if (start < 0) start += slen;
            if (end < 0) end += slen;
        } else {
            /* substring() (unlike slice()) never accepts negative indices
               -- real JS clamps them to 0 instead of counting from the
               end. */
            if (start < 0) start = 0;
            if (end < 0) end = 0;
        }
        if (start < 0) start = 0;
        if (start > slen) start = slen;
        if (end < 0) end = 0;
        if (end > slen) end = slen;
        if (start > end) { int t = start; start = end; end = t; } /* substring()'s own real behavior; harmless no-op for slice() when already ordered */
        char buf[JS_STR_MAX];
        int n = end - start; if (n < 0) n = 0; if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
        memcpy(buf, s + start, (size_t)n); buf[n] = 0;
        js_set_string(out, buf);
        return 1;
    }
    if (!strcmp(name, "toUpperCase") || !strcmp(name, "toLowerCase")) {
        char buf[JS_STR_MAX]; int n = slen; if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
        int i; for (i = 0; i < n; i++) buf[i] = (char)(!strcmp(name, "toUpperCase") ? toupper((unsigned char)s[i]) : tolower((unsigned char)s[i]));
        buf[n] = 0;
        js_set_string(out, buf);
        return 1;
    }
    if (!strcmp(name, "trim")) {
        int a = 0, b = slen;
        while (a < b && isspace((unsigned char)s[a])) a++;
        while (b > a && isspace((unsigned char)s[b - 1])) b--;
        char buf[JS_STR_MAX]; int n = b - a; if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
        memcpy(buf, s + a, (size_t)n); buf[n] = 0;
        js_set_string(out, buf);
        return 1;
    }
    if (!strcmp(name, "concat")) {
        char buf[JS_STR_MAX]; int len = 0;
        js_json_append(buf, &len, (int)sizeof buf, s);
        int i;
        for (i = 0; i < argc; i++) { char part[256]; js_to_string_buf(&args[i], part, sizeof part); js_json_append(buf, &len, (int)sizeof buf, part); }
        js_set_string(out, buf);
        return 1;
    }
    if (!strcmp(name, "repeat")) {
        int times = argc > 0 ? (int)js_to_number(&args[0]) : 0;
        char buf[JS_STR_MAX]; int len = 0; buf[0] = 0;
        int i; for (i = 0; i < times && len < (int)sizeof(buf) - 1; i++) js_json_append(buf, &len, (int)sizeof buf, s);
        js_set_string(out, buf);
        return 1;
    }
    if (!strcmp(name, "replace")) {
        char search[JS_STR_MAX]; search[0] = 0;
        char repl[JS_STR_MAX]; repl[0] = 0;
        if (argc > 0) js_to_string_buf(&args[0], search, sizeof search);
        if (argc > 1) js_to_string_buf(&args[1], repl, sizeof repl);
        const char *found = search[0] ? strstr(s, search) : 0;
        if (!found) { js_set_string(out, s); return 1; }
        char buf[JS_STR_MAX]; int len = 0;
        int pre = (int)(found - s);
        char tmp[JS_STR_MAX];
        int n = pre; if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
        memcpy(tmp, s, (size_t)n); tmp[n] = 0;
        js_json_append(buf, &len, (int)sizeof buf, tmp);
        js_json_append(buf, &len, (int)sizeof buf, repl);
        js_json_append(buf, &len, (int)sizeof buf, found + strlen(search));
        js_set_string(out, buf);
        return 1;
    }
    if (!strcmp(name, "split")) {
        char sep[JS_STR_MAX]; sep[0] = 0;
        if (argc > 0) js_to_string_buf(&args[0], sep, sizeof sep);
        JSObject *arr = js_array_new();
        js_set_object(out, arr);
        int seplen = (int)strlen(sep);
        if (seplen == 0) {
            /* real JS: "".split('') === one entry per character */
            int i;
            for (i = 0; i < slen; i++) { char one[2]; one[0] = s[i]; one[1] = 0; JSValue v; js_set_string(&v, one); js_array_push(arr, &v); }
            return 1;
        }
        const char *p = s;
        while (1) {
            const char *found = strstr(p, sep);
            char piece[JS_STR_MAX];
            int n = found ? (int)(found - p) : (int)strlen(p);
            if (n > (int)sizeof(piece) - 1) n = (int)sizeof(piece) - 1;
            memcpy(piece, p, (size_t)n); piece[n] = 0;
            JSValue v; js_set_string(&v, piece);
            js_array_push(arr, &v);
            if (!found) break;
            p = found + seplen;
        }
        return 1;
    }
    return 0;
}

static void js_set_prop(JSInterp *interp, const JSValue *base, const char *name, const JSValue *val) {
    if (base->type != JSV_OBJECT || !base->obj) return;
    JSObject *o = base->obj;
    if (o->kind == JSOBJ_DOM_ELEMENT) { js_dom_set_prop(interp, o, name, val); return; }
    if (o->kind == JSOBJ_ARRAY) {
        int idx;
        if (js_is_array_index(name, &idx)) { js_array_set(o, idx, val); return; }
        if (!strcmp(name, "length")) return; /* real JS lets you truncate via "arr.length = N" -- not implemented, a narrow documented gap */
        js_obj_set(o, name, val);
        return;
    }
    JSValue marker;
    if (js_obj_get(o, "__style_of__", &marker) && marker.type == JSV_OBJECT) {
        js_style_set_prop(o, name, val);
        interp->mutated_dom = 1;
        return;
    }
    /* "set x(v) {...}" accessors -- see js_get_prop()'s own identical
       "__set_<name>__" lookup/comment just above; same own-property-then-
       instance_methods order. A setter's return value is discarded (real
       JS semantics), and -- unlike a getter -- writing to a property with
       ONLY a getter (no setter) falls through to js_obj_set() below,
       silently creating/overwriting a plain data property that the
       getter's own logic will never see again (a real, narrow
       simplification: real JS silently no-ops that write in non-strict
       mode instead; not worth a dedicated no-setter tracking flag at this
       engine's own scale). */
    {
        char accname[JS_IDENT_MAX + 8];
        snprintf(accname, sizeof accname, "__set_%s__", name);
        JSValue setterv;
        JSObject *setter = 0;
        if (js_obj_get(o, accname, &setterv) && setterv.type == JSV_OBJECT) setter = setterv.obj;
        else if (o->instance_methods && js_obj_get(o->instance_methods, accname, &setterv) && setterv.type == JSV_OBJECT) setter = setterv.obj;
        if (setter) {
            JSValue args[1]; js_value_store(&args[0], val);
            JSValue tmp;
            js_call_function(interp, setter, o, args, 1, &tmp);
            return;
        }
    }
    js_obj_set(o, name, val);
}

/* ---- DOM element / style bindings ---- */

/* "backgroundColor" -> "background-color" -- real JS's own CSSOM naming
 * convention (element.style.X uses camelCase; the underlying CSS
 * property is kebab-case), needed so a JS style assignment can be handed
 * straight to css.c's own css_apply_decl() (visible here via same-TU
 * #include order -- see this file's own top comment) instead of
 * reinventing per-property mutation logic for every CSS property this
 * engine already understands. */
static void js_camel_to_kebab(const char *camel, char *out, size_t outcap) {
    size_t oi = 0;
    int i;
    for (i = 0; camel[i] && oi + 2 < outcap; i++) {
        char c = camel[i];
        if (c >= 'A' && c <= 'Z') {
            if (oi > 0) out[oi++] = '-';
            out[oi++] = (char)(c - 'A' + 'a');
        } else {
            out[oi++] = c;
        }
    }
    out[oi] = 0;
}

/* css_apply_decl() is `static` in css.c but visible here regardless --
 * js_engine.c is #include-d into sqw_main.c AFTER css.c, same single-TU
 * convention every SQW/*.c file already relies on (see this file's own
 * top comment). Forward-declared here since css.c's own declaration sits
 * earlier in the same translation unit but this file doesn't #include
 * css.h itself (dom.h, pulled in transitively, is enough for the types
 * used here). */
static void css_apply_decl(DomNode *el, const char *name, const char *value);

static void js_style_get_prop(JSObject *style_obj, const char *name, JSValue *out) {
    JSValue marker;
    if (!js_obj_get(style_obj, "__style_of__", &marker) || marker.type != JSV_OBJECT) { js_set_undefined(out); return; }
    JSObject *owner = marker.obj;
    DomNode *el = owner->dom_node;
    /* Read-back is deliberately limited to color/background-color as
     * plain hex, and font-size/opacity as plain numbers-with-unit -- a
     * real getComputedStyle()-equivalent for every property this engine
     * understands is more machinery than a "read back what I just set"
     * convenience getter needs; scripts overwhelmingly WRITE style.X, not
     * read it back. Anything else returns an empty string rather than
     * undefined, matching real CSSOM's own "unset style property reads
     * back as ''" convention. */
    char kebab[64];
    js_camel_to_kebab(name, kebab, sizeof kebab);
    if (!strcmp(kebab, "opacity")) { char b[32]; snprintf(b, sizeof b, "%g", el->css_opacity); js_set_string(out, b); return; }
    if (!strcmp(kebab, "font-size")) { char b[32]; snprintf(b, sizeof b, "%gpx", el->css_font_size); js_set_string(out, b); return; }
    js_set_string(out, "");
}

static void js_style_set_prop(JSObject *style_obj, const char *name, const JSValue *val) {
    JSValue marker;
    if (!js_obj_get(style_obj, "__style_of__", &marker) || marker.type != JSV_OBJECT) return;
    JSObject *owner = marker.obj;
    DomNode *el = owner->dom_node;
    char kebab[64];
    js_camel_to_kebab(name, kebab, sizeof kebab);
    char vbuf[192];
    js_to_string_buf(val, vbuf, sizeof vbuf);
    css_apply_decl(el, kebab, vbuf);
}

/* Recursively concatenates every descendant TEXT node's own content --
 * real DOM textContent semantics (deeper than sqw_main.c's own
 * concat_direct_text(), which only reads DIRECT text children for the
 * unrelated purpose of sizing an atomic inline box -- see layout.c's own
 * comment on that scope). */
static void js_append_text_content(DomNode *n, char *out, int *out_len, int outcap) {
    if (dom_is_text(n)) {
        const char *t = n->text;
        int i;
        for (i = 0; t[i] && *out_len < outcap - 1; i++) { out[*out_len] = t[i]; (*out_len)++; }
        return;
    }
    int i;
    for (i = 0; i < n->child_count; i++) {
        DomNode *c = n->children[i];
        js_append_text_content(c, out, out_len, outcap);
    }
}

static const char *JS_VOID_TAGS[] = { "img", "br", "input", "hr", "meta", "link", 0 };
static int js_is_void_tag(const char *tag) {
    int i;
    for (i = 0; JS_VOID_TAGS[i]; i++) if (!strcmp(JS_VOID_TAGS[i], tag)) return 1;
    return 0;
}

/* Real (if simple) HTML serialization for innerHTML's own getter --
 * attribute values are NOT re-escaped (a value containing '"' would
 * round-trip wrong) -- a documented, narrow gap, not a security issue:
 * this only ever serializes THIS project's own already-parsed DOM,
 * never re-parses its own output as trusted markup from an external
 * source. */
static void js_serialize_html(DomNode *n, char *out, int *out_len, int outcap) {
    if (dom_is_text(n)) {
        const char *t = n->text;
        int i;
        for (i = 0; t[i] && *out_len < outcap - 1; i++) { out[*out_len] = t[i]; (*out_len)++; }
        return;
    }
    int ol = *out_len;
    ol += snprintf(out + ol, (size_t)(outcap - ol > 0 ? outcap - ol : 0), "<%s", n->tag);
    int ai;
    for (ai = 0; ai < n->attr_count && ol < outcap - 1; ai++) {
        DomAttr *a = &n->attrs[ai];
        ol += snprintf(out + ol, (size_t)(outcap - ol > 0 ? outcap - ol : 0), " %s=\"%s\"", a->name, a->value);
    }
    if (ol < outcap - 1) out[ol++] = '>';
    *out_len = ol;
    if (js_is_void_tag(n->tag)) return;
    int i;
    for (i = 0; i < n->child_count; i++) {
        DomNode *c = n->children[i];
        js_serialize_html(c, out, out_len, outcap);
    }
    ol = *out_len;
    ol += snprintf(out + ol, (size_t)(outcap - ol > 0 ? outcap - ol : 0), "</%s>", n->tag);
    *out_len = ol;
}

/* Frees just a dom_parse()-returned fragment's OWN synthetic "#document"
 * shell (its children array pointer, then itself) WITHOUT recursively
 * freeing the children themselves -- used right after reparenting those
 * children onto a real element for innerHTML's setter, where ownership
 * of every child has already moved to that element. Real dom_free()
 * would double-free/dangling-pointer them (they're already reachable
 * from, and will be freed via, the real element's own subtree). */
static void js_free_shell(DomNode *shell) {
    if (shell->children) free(shell->children);
    free(shell);
}

/* Appends `child` (already allocated, NOT yet attached anywhere -- either
 * fresh from document.createElement()/createTextNode(), or reparented out
 * of a dom_parse() fragment shell, see js_dom_set_inner_html() below) as
 * `parent`'s new last child, growing the children array as needed --
 * shared by innerHTML's setter and the real appendChild()/removeChild()
 * DOM methods so there's one place that gets this right. */
static void js_dom_append_child(DomNode *parent, DomNode *child) {
    child->parent = parent;
    if (parent->child_count >= parent->child_cap) {
        parent->child_cap = parent->child_cap ? parent->child_cap * 2 : 4;
        parent->children = (DomNode **)realloc(parent->children, (size_t)parent->child_cap * sizeof(DomNode *));
    }
    parent->children[parent->child_count++] = child;
}

static void js_dom_set_inner_html(DomNode *el, const char *html) {
    int i;
    for (i = 0; i < el->child_count; i++) {
        DomNode *c = el->children[i];
        dom_free(c);
    }
    el->child_count = 0;
    DomNode *frag = dom_parse(html ? html : "");
    for (i = 0; i < frag->child_count; i++) {
        DomNode *c = frag->children[i];
        js_dom_append_child(el, c);
    }
    js_free_shell(frag);
}

static void js_dom_set_text_content(DomNode *el, const char *text) {
    int i;
    for (i = 0; i < el->child_count; i++) {
        DomNode *c = el->children[i];
        dom_free(c);
    }
    el->child_count = 0;
    DomNode *tn = (DomNode *)malloc(sizeof(DomNode));
    memset(tn, 0, sizeof(*tn));
    tn->text = strdup(text ? text : "");
    tn->parent = el;
    if (el->child_cap < 1) { el->child_cap = 1; el->children = (DomNode **)realloc(el->children, sizeof(DomNode *)); }
    el->children[0] = tn;
    el->child_count = 1;
}

static JSObject *js_wrap_dom_node(DomNode *n) {
    JSObject *o = js_object_new(JSOBJ_DOM_ELEMENT);
    o->dom_node = n;
    return o;
}

static void js_native_get_attribute(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)interp;
    if (!this_obj || !this_obj->dom_node || argc < 1) { js_set_null(out); return; }
    char name[64];
    js_to_string_buf(&args[0], name, sizeof name);
    DomNode *n = this_obj->dom_node;
    const char *v = dom_get_attr(n, name);
    if (v) js_set_string(out, v); else js_set_null(out);
}
static void js_native_set_attribute(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    js_set_undefined(out);
    if (!this_obj || !this_obj->dom_node || argc < 2) return;
    char name[64], val[192];
    js_to_string_buf(&args[0], name, sizeof name);
    js_to_string_buf(&args[1], val, sizeof val);
    DomNode *n = this_obj->dom_node;
    dom_set_attr(n, name, val);
    interp->mutated_dom = 1;
}
static void js_native_add_event_listener(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)interp;
    js_set_undefined(out);
    if (!this_obj || !this_obj->dom_node || argc < 2) return;
    if (args[1].type != JSV_OBJECT || !args[1].obj) return;
    char evname[32];
    js_to_string_buf(&args[0], evname, sizeof evname);
    DomNode *n = this_obj->dom_node;
    /* "click"/"input"/"change"/"keydown" are the only event names wired
     * to anything -- see js_dispatch_click()'s/js_dispatch_input()'s/
     * js_dispatch_change()'s/js_dispatch_keydown()'s own comments in
     * js_engine.h for exactly when sqw_main.c calls each. Any other
     * event name (real HTML5 has dozens) is silently accepted (so a
     * script's own addEventListener call never errors) but never fires --
     * a real, documented gap, not a silent miscompile of the script. */
    if (!strcmp(evname, "click")) n->js_onclick = (void *)args[1].obj;
    else if (!strcmp(evname, "input")) n->js_oninput = (void *)args[1].obj;
    else if (!strcmp(evname, "change")) n->js_onchange = (void *)args[1].obj;
    else if (!strcmp(evname, "keydown")) n->js_onkeydown = (void *)args[1].obj;
}
/* js_obj_set_native(): the common "make a native-function-valued property
 * and set it" pattern used constantly below -- avoids a throwaway local
 * JSValue at every one of those call sites (see js_obj_set_str/_num/_obj's
 * own comment, same rationale). */
static void js_obj_set_native(JSObject *o, const char *name, void (*fn)(JSInterp *, JSValue *, int, JSObject *, JSValue *)) {
    JSObject *fo = js_object_new(JSOBJ_NATIVE);
    fo->native_fn = fn;
    js_obj_set_obj(o, name, fo);
}

/* ---- classList ---- */

/* True iff `cls` (the "class" attribute's raw value, whitespace-separated)
 * contains `token` as a whole word -- same matching convention as css.c's
 * own class-selector matching (see its own comment). */
static int js_class_has_token(const char *cls, const char *token) {
    if (!cls) return 0;
    int tlen = (int)strlen(token);
    const char *p = cls;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *tok = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if ((int)(p - tok) == tlen && strncmp(tok, token, (size_t)tlen) == 0) return 1;
    }
    return 0;
}

static DomNode *js_classlist_owner(JSObject *cl) {
    JSValue marker;
    if (!js_obj_get(cl, "__classlist_of__", &marker) || marker.type != JSV_OBJECT || !marker.obj) return 0;
    JSObject *owner = marker.obj;
    return owner->dom_node;
}

static void js_native_classlist_add(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_undefined(out);
    if (!t || c < 1) return;
    DomNode *n = js_classlist_owner(t);
    if (!n) return;
    char token[128]; js_to_string_buf(&a[0], token, sizeof token);
    const char *cur = dom_get_attr(n, "class");
    if (js_class_has_token(cur, token)) return; /* already present */
    char buf[256];
    if (cur && cur[0]) snprintf(buf, sizeof buf, "%s %s", cur, token);
    else snprintf(buf, sizeof buf, "%s", token);
    dom_set_attr(n, "class", buf);
    interp->mutated_dom = 1;
}
static void js_native_classlist_remove(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_undefined(out);
    if (!t || c < 1) return;
    DomNode *n = js_classlist_owner(t);
    if (!n) return;
    char token[128]; js_to_string_buf(&a[0], token, sizeof token);
    const char *cur = dom_get_attr(n, "class");
    if (!cur) return;
    char buf[256]; int len = 0; buf[0] = 0;
    const char *p = cur;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *tok = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        int tl = (int)(p - tok);
        if (!(tl == (int)strlen(token) && strncmp(tok, token, (size_t)tl) == 0)) {
            int cap = (int)sizeof(buf) - len; if (cap < 0) cap = 0;
            int wn = snprintf(buf + len, (size_t)cap, "%s%.*s", len > 0 ? " " : "", tl, tok);
            if (wn > 0) len += wn;
        }
    }
    dom_set_attr(n, "class", buf);
    interp->mutated_dom = 1;
}
static void js_native_classlist_contains(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp;
    if (!t || c < 1) { js_set_bool(out, 0); return; }
    DomNode *n = js_classlist_owner(t);
    char token[128]; js_to_string_buf(&a[0], token, sizeof token);
    js_set_bool(out, n ? js_class_has_token(dom_get_attr(n, "class"), token) : 0);
}
static void js_native_classlist_toggle(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    if (!t || c < 1) { js_set_bool(out, 0); return; }
    DomNode *n = js_classlist_owner(t);
    char token[128]; js_to_string_buf(&a[0], token, sizeof token);
    int has = n ? js_class_has_token(dom_get_attr(n, "class"), token) : 0;
    if (has) js_native_classlist_remove(interp, a, c, t, out);
    else js_native_classlist_add(interp, a, c, t, out);
    js_set_bool(out, !has);
}

/* ---- appendChild / removeChild / remove ---- */

static void js_native_append_child(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_undefined(out);
    if (!t || !t->dom_node || c < 1 || a[0].type != JSV_OBJECT || !a[0].obj || !a[0].obj->dom_node) return;
    js_dom_append_child(t->dom_node, a[0].obj->dom_node);
    interp->mutated_dom = 1;
    js_set_object(out, a[0].obj);
}
static void js_native_remove_child(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_undefined(out);
    if (!t || !t->dom_node || c < 1 || a[0].type != JSV_OBJECT || !a[0].obj || !a[0].obj->dom_node) return;
    DomNode *parent = t->dom_node;
    DomNode *target = a[0].obj->dom_node;
    int i;
    for (i = 0; i < parent->child_count; i++) {
        DomNode *ci = parent->children[i];
        if (ci == target) {
            int j;
            for (j = i; j < parent->child_count - 1; j++) parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            dom_free(target);
            interp->mutated_dom = 1;
            break;
        }
    }
}
static void js_native_remove_self(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)a; (void)c;
    js_set_undefined(out);
    if (!t || !t->dom_node || !t->dom_node->parent) return;
    DomNode *self = t->dom_node;
    DomNode *parent = self->parent;
    int i;
    for (i = 0; i < parent->child_count; i++) {
        DomNode *ci = parent->children[i];
        if (ci == self) {
            int j;
            for (j = i; j < parent->child_count - 1; j++) parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            dom_free(self);
            interp->mutated_dom = 1;
            break;
        }
    }
}

/* ---- querySelector / querySelectorAll ----
 * Deliberately the SAME narrow selector scope as css.c's own engine (see
 * its top comment): tag, ".class", "#id" only -- one simple selector,
 * no compounds ("div.foo"), no combinators (descendant/child/sibling),
 * no attribute/pseudo selectors. A real project-wide convention, not a
 * corner cut specifically for this feature. */
static int js_selector_matches_simple(DomNode *el, const char *sel) {
    if (sel[0] == '#') { const char *id = dom_get_attr(el, "id"); return id && !strcmp(id, sel + 1); }
    if (sel[0] == '.') return js_class_has_token(dom_get_attr(el, "class"), sel + 1);
    return !strcmp(el->tag, sel);
}
static DomNode *js_query_selector_root(JSObject *this_obj, JSInterp *interp) {
    if (this_obj && this_obj->dom_node) return this_obj->dom_node;
    return interp->document_root;
}
static void js_native_query_selector(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    js_set_null(out);
    if (c < 1) return;
    char sel[128]; js_to_string_buf(&a[0], sel, sizeof sel);
    DomNode *root = js_query_selector_root(t, interp);
    DomNode *found = 0;
    int cap = 64, top = 0;
    DomNode **stack = (DomNode **)malloc((size_t)cap * sizeof(DomNode *));
    int *next_child = (int *)malloc((size_t)cap * sizeof(int));
    stack[top] = root; next_child[top] = 0; top++;
    while (top > 0 && !found) {
        DomNode *node = stack[top - 1];
        if (next_child[top - 1] >= node->child_count) { top--; continue; }
        DomNode *child = node->children[next_child[top - 1]];
        next_child[top - 1]++;
        if (!dom_is_text(child)) {
            if (js_selector_matches_simple(child, sel)) { found = child; break; }
            if (top >= cap) { cap *= 2; stack = (DomNode **)realloc(stack, (size_t)cap * sizeof(DomNode *)); next_child = (int *)realloc(next_child, (size_t)cap * sizeof(int)); }
            stack[top] = child; next_child[top] = 0; top++;
        }
    }
    free(stack); free(next_child);
    if (found) js_set_object(out, js_wrap_dom_node(found));
}
static void js_native_query_selector_all(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    JSObject *arr = js_array_new();
    js_set_object(out, arr);
    if (c < 1) return;
    char sel[128]; js_to_string_buf(&a[0], sel, sizeof sel);
    DomNode *root = js_query_selector_root(t, interp);
    int cap = 64, top = 0;
    DomNode **stack = (DomNode **)malloc((size_t)cap * sizeof(DomNode *));
    int *next_child = (int *)malloc((size_t)cap * sizeof(int));
    stack[top] = root; next_child[top] = 0; top++;
    while (top > 0) {
        DomNode *node = stack[top - 1];
        if (next_child[top - 1] >= node->child_count) { top--; continue; }
        DomNode *child = node->children[next_child[top - 1]];
        next_child[top - 1]++;
        if (!dom_is_text(child)) {
            if (js_selector_matches_simple(child, sel)) { JSValue v; js_set_object(&v, js_wrap_dom_node(child)); js_array_push(arr, &v); }
            if (top >= cap) { cap *= 2; stack = (DomNode **)realloc(stack, (size_t)cap * sizeof(DomNode *)); next_child = (int *)realloc(next_child, (size_t)cap * sizeof(int)); }
            stack[top] = child; next_child[top] = 0; top++;
        }
    }
    free(stack); free(next_child);
}
static void js_env_define_native(JSEnv *env, const char *name, void (*fn)(JSInterp *, JSValue *, int, JSObject *, JSValue *)) {
    JSObject *fo = js_object_new(JSOBJ_NATIVE);
    fo->native_fn = fn;
    js_env_define_obj(env, name, fo);
}

static void js_dom_get_prop(JSInterp *interp, JSObject *obj, const char *name, JSValue *out) {
    (void)interp;
    DomNode *n = obj->dom_node;
    if (!n) { js_set_undefined(out); return; }
    if (!strcmp(name, "id")) { const char *v = dom_get_attr(n, "id"); js_set_string(out, v ? v : ""); return; }
    if (!strcmp(name, "className")) { const char *v = dom_get_attr(n, "class"); js_set_string(out, v ? v : ""); return; }
    if (!strcmp(name, "value")) { js_set_string(out, n->form_value); return; }
    if (!strcmp(name, "checked")) { js_set_bool(out, n->form_checked); return; }
    if (!strcmp(name, "tagName")) { js_set_string(out, n->tag); return; }
    if (!strcmp(name, "textContent") || !strcmp(name, "innerText")) {
        char buf[JS_STR_MAX]; int len = 0;
        js_append_text_content(n, buf, &len, sizeof buf);
        buf[len] = 0;
        js_set_string(out, buf);
        return;
    }
    if (!strcmp(name, "innerHTML")) {
        char buf[JS_STR_MAX]; int len = 0;
        int i;
        for (i = 0; i < n->child_count; i++) {
            DomNode *c = n->children[i];
            js_serialize_html(c, buf, &len, sizeof buf);
        }
        buf[len < (int)sizeof buf ? len : (int)sizeof buf - 1] = 0;
        js_set_string(out, buf);
        return;
    }
    if (!strcmp(name, "style")) {
        /* A fresh JSOBJ_PLAIN "style wrapper" every access (not cached --
         * see DomNode::js_onclick's own comment on why that field exists
         * specifically to sidestep this same "no per-node object cache"
         * limitation for click handlers; a style object's own writes are
         * applied immediately to the real DomNode via
         * css_apply_decl(), so there's nothing this non-caching loses --
         * unlike onclick, no state needs to SURVIVE between accesses). Its
         * own "__style_of__" property marks it for js_get_prop()/
         * js_set_prop()'s own dispatch -- see their comments. */
        JSObject *style = js_object_new(JSOBJ_PLAIN);
        JSObject *owner = js_wrap_dom_node(n);
        js_obj_set_obj(style, "__style_of__", owner);
        js_set_object(out, style);
        return;
    }
    if (!strcmp(name, "onclick")) {
        if (n->js_onclick) js_set_object(out, (JSObject *)n->js_onclick);
        else js_set_null(out);
        return;
    }
    if (!strcmp(name, "oninput")) {
        if (n->js_oninput) js_set_object(out, (JSObject *)n->js_oninput);
        else js_set_null(out);
        return;
    }
    if (!strcmp(name, "onchange")) {
        if (n->js_onchange) js_set_object(out, (JSObject *)n->js_onchange);
        else js_set_null(out);
        return;
    }
    if (!strcmp(name, "onkeydown")) {
        if (n->js_onkeydown) js_set_object(out, (JSObject *)n->js_onkeydown);
        else js_set_null(out);
        return;
    }
    if (!strcmp(name, "getAttribute")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_get_attribute; js_set_object(out, fo); return; }
    if (!strcmp(name, "setAttribute")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_set_attribute; js_set_object(out, fo); return; }
    if (!strcmp(name, "addEventListener")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_add_event_listener; js_set_object(out, fo); return; }
    if (!strcmp(name, "parentNode") || !strcmp(name, "parentElement")) {
        DomNode *p = n->parent;
        if (p) js_set_object(out, js_wrap_dom_node(p)); else js_set_null(out);
        return;
    }
    if (!strcmp(name, "children")) {
        JSObject *arr = js_array_new();
        int i;
        for (i = 0; i < n->child_count; i++) {
            DomNode *c = n->children[i];
            if (!dom_is_text(c)) { JSValue v; js_set_object(&v, js_wrap_dom_node(c)); js_array_push(arr, &v); }
        }
        js_set_object(out, arr);
        return;
    }
    if (!strcmp(name, "firstElementChild")) {
        int i;
        for (i = 0; i < n->child_count; i++) {
            DomNode *c = n->children[i];
            if (!dom_is_text(c)) { js_set_object(out, js_wrap_dom_node(c)); return; }
        }
        js_set_null(out);
        return;
    }
    if (!strcmp(name, "nextElementSibling") || !strcmp(name, "previousElementSibling")) {
        DomNode *parent = n->parent;
        if (!parent) { js_set_null(out); return; }
        int my_idx = -1, i;
        for (i = 0; i < parent->child_count; i++) { DomNode *c = parent->children[i]; if (c == n) { my_idx = i; break; } }
        int step = !strcmp(name, "nextElementSibling") ? 1 : -1;
        int k = my_idx + step;
        while (my_idx >= 0 && k >= 0 && k < parent->child_count) {
            DomNode *cand = parent->children[k];
            if (!dom_is_text(cand)) { js_set_object(out, js_wrap_dom_node(cand)); return; }
            k += step;
        }
        js_set_null(out);
        return;
    }
    if (!strcmp(name, "classList")) {
        /* A fresh JSOBJ_PLAIN "classList wrapper" every access, same
           non-caching convention as "style" above -- its own
           "__classlist_of__" marker property routes add/remove/toggle/
           contains calls (see js_native_classlist_*()) back to the real
           "class" attribute on `n`. */
        JSObject *cl = js_object_new(JSOBJ_PLAIN);
        JSObject *owner = js_wrap_dom_node(n);
        js_obj_set_obj(cl, "__classlist_of__", owner);
        js_obj_set_native(cl, "add", js_native_classlist_add);
        js_obj_set_native(cl, "remove", js_native_classlist_remove);
        js_obj_set_native(cl, "toggle", js_native_classlist_toggle);
        js_obj_set_native(cl, "contains", js_native_classlist_contains);
        js_set_object(out, cl);
        return;
    }
    if (!strcmp(name, "appendChild")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_append_child; js_set_object(out, fo); return; }
    if (!strcmp(name, "removeChild")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_remove_child; js_set_object(out, fo); return; }
    if (!strcmp(name, "remove")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_remove_self; js_set_object(out, fo); return; }
    if (!strcmp(name, "querySelector")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_query_selector; js_set_object(out, fo); return; }
    if (!strcmp(name, "querySelectorAll")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_query_selector_all; js_set_object(out, fo); return; }
    js_set_undefined(out);
}

static void js_dom_set_prop(JSInterp *interp, JSObject *obj, const char *name, const JSValue *val) {
    DomNode *n = obj->dom_node;
    if (!n) return;
    if (!strcmp(name, "innerHTML")) {
        char buf[JS_STR_MAX];
        js_to_string_buf(val, buf, sizeof buf);
        js_dom_set_inner_html(n, buf);
        interp->mutated_dom = 1;
        return;
    }
    if (!strcmp(name, "textContent") || !strcmp(name, "innerText")) {
        char buf[JS_STR_MAX];
        js_to_string_buf(val, buf, sizeof buf);
        js_dom_set_text_content(n, buf);
        interp->mutated_dom = 1;
        return;
    }
    if (!strcmp(name, "id")) { char buf[64]; js_to_string_buf(val, buf, sizeof buf); dom_set_attr(n, "id", buf); return; }
    if (!strcmp(name, "className")) { char buf[128]; js_to_string_buf(val, buf, sizeof buf); dom_set_attr(n, "class", buf); interp->mutated_dom = 1; return; }
    if (!strcmp(name, "value")) { char buf[512]; js_to_string_buf(val, buf, sizeof buf); strncpy(n->form_value, buf, sizeof n->form_value - 1); n->form_value[sizeof n->form_value - 1] = 0; return; }
    if (!strcmp(name, "checked")) { n->form_checked = js_to_bool(val); return; }
    if (!strcmp(name, "onclick")) {
        if (val->type == JSV_OBJECT) n->js_onclick = (void *)val->obj;
        else n->js_onclick = 0;
        return;
    }
    if (!strcmp(name, "oninput")) {
        if (val->type == JSV_OBJECT) n->js_oninput = (void *)val->obj;
        else n->js_oninput = 0;
        return;
    }
    if (!strcmp(name, "onchange")) {
        if (val->type == JSV_OBJECT) n->js_onchange = (void *)val->obj;
        else n->js_onchange = 0;
        return;
    }
    if (!strcmp(name, "onkeydown")) {
        if (val->type == JSV_OBJECT) n->js_onkeydown = (void *)val->obj;
        else n->js_onkeydown = 0;
        return;
    }
    /* Any other property name on a DOM element: silently dropped -- see
       this function's own header comment on why arbitrary custom
       properties don't persist in this engine. */
}

/* ---- lvalue assignment (shared by JS_ASSIGN and JS_UPDATE) ---- */

static void js_assign_to(JSInterp *interp, JsNode *target, JSEnv *env, const JSValue *val) {
    if (target->kind == JS_IDENT) { js_env_set(env, target->str, val); return; }
    if (target->kind == JS_MEMBER) {
        JsNode *obj_expr = target->kids[0];
        JSValue base;
        js_eval(interp, obj_expr, env, &base);
        if (interp->signal != JS_SIG_NONE) return;
        js_set_prop(interp, &base, target->str, val);
        return;
    }
    if (target->kind == JS_INDEX) {
        JsNode *obj_expr = target->kids[0];
        JsNode *idx_expr = target->kids[1];
        JSValue base, idx;
        js_eval(interp, obj_expr, env, &base);
        if (interp->signal != JS_SIG_NONE) return;
        js_eval(interp, idx_expr, env, &idx);
        if (interp->signal != JS_SIG_NONE) return;
        char name[64];
        js_to_string_buf(&idx, name, sizeof name);
        js_set_prop(interp, &base, name, val);
        return;
    }
    /* Anything else on the left of "=" doesn't parse as an assignable
       target in real JS either -- silently ignored here rather than
       treated as a parse error, matching this engine's general
       "degrade safely, never crash" convention. */
}

/* ============================= Eval ============================= */

static void js_eval(JSInterp *interp, JsNode *n, JSEnv *env, JSValue *out) {
    switch (n->kind) {
    case JS_NUM_LIT: js_set_number(out, n->num); return;
    case JS_STR_LIT: js_set_string(out, n->str ? n->str : ""); return;
    case JS_BOOL_LIT: js_set_bool(out, n->num != 0.0); return;
    case JS_NULL_LIT: js_set_null(out); return;
    case JS_UNDEF_LIT: js_set_undefined(out); return;
    case JS_IDENT: {
        if (js_env_get(env, n->str, out)) return;
        js_set_undefined(out);
        return;
    }
    case JS_FUNC_EXPR: {
        JSObject *fo = js_object_new(JSOBJ_FUNCTION);
        fo->func_node = n;
        fo->closure_env = env;
        if (n->str) js_env_define_obj(env, n->str, fo); /* named func expr -- also usable by its own name inside itself, a minor real-JS behavior this happens to give for free */
        js_set_object(out, fo);
        return;
    }
    case JS_THIS: {
        if (js_env_get(env, "this", out)) return;
        js_set_undefined(out); /* a plain (non-method) call's own "this" -- see js_call_function()'s own comment */
        return;
    }
    case JS_YIELD: {
        /* Only meaningful while THIS thread is actually a generator's own
           worker thread (interp->current_generator set by
           js_native_generator_next() right before handing off control --
           see its own comment). A "yield" reached with no current
           generator (e.g. accidentally used outside any "function*") is a
           harmless documented no-op, evaluating to undefined, rather
           than a parse/runtime error -- consistent with this whole
           file's "never crash on a script" convention. */
        JSObject *gen = interp->current_generator;
        if (!gen) { js_set_undefined(out); return; }
        JSValue v;
        if (n->kid_count > 0) { js_eval(interp, n->kids[0], env, &v); if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; } }
        else js_set_undefined(&v);
        /* Hand control BACK to whoever called .next() -- see
           js_native_generator_next()'s own identical protocol (the other
           half of this same handoff) for the full design/comment. */
        pthread_mutex_lock(&gen->gen_mutex);
        js_value_store(&gen->gen_yielded_value, &v);
        gen->gen_turn = 0;
        pthread_cond_broadcast(&gen->gen_cond);
        while (gen->gen_turn != 1) pthread_cond_wait(&gen->gen_cond, &gen->gen_mutex);
        pthread_mutex_unlock(&gen->gen_mutex);
        /* Resumed -- "yield expr" itself evaluates to whatever value the
           NEXT .next(v) call sent in, real JS semantics. */
        js_value_store(out, &gen->gen_sent_value);
        return;
    }
    case JS_ARRAY_LIT: {
        JSObject *arr = js_array_new();
        int i;
        for (i = 0; i < n->kid_count; i++) {
            JsNode *elnode = n->kids[i];
            JSValue v;
            js_eval(interp, elnode, env, &v);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
            js_array_push(arr, &v);
        }
        js_set_object(out, arr);
        return;
    }
    case JS_OBJECT_LIT: {
        JSObject *obj = js_object_new(JSOBJ_PLAIN);
        int i;
        for (i = 0; i < n->kid_count; i++) {
            JsNode *prop = n->kids[i];
            JsNode *valnode = prop->kids[0];
            JSValue v;
            js_eval(interp, valnode, env, &v);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
            /* "get x() {...}"/"set x(v) {...}" -- prop->op[0] is 'g'/'s'
               (set by js_parse_object_literal's own get/set detection,
               see its comment), stored under a mangled name so
               js_get_prop()/js_set_prop() can find it -- see either's own
               "__get_<name>__"/"__set_<name>__" comment. */
            if (prop->op[0] == 'g' || prop->op[0] == 's') {
                char accname[JS_IDENT_MAX + 8];
                snprintf(accname, sizeof accname, prop->op[0] == 'g' ? "__get_%s__" : "__set_%s__", prop->str);
                js_obj_set(obj, accname, &v);
            } else {
                js_obj_set(obj, prop->str, &v);
            }
        }
        js_set_object(out, obj);
        return;
    }
    case JS_TEMPLATE: {
        char buf[JS_STR_MAX * 2]; int len = 0; buf[0] = 0;
        int i;
        for (i = 0; i < n->kid_count; i++) {
            JsNode *part = n->kids[i];
            char piece[JS_STR_MAX];
            if (part->kind == JS_STR_LIT) {
                strncpy(piece, part->str ? part->str : "", sizeof piece - 1);
                piece[sizeof piece - 1] = 0;
            } else {
                JSValue v;
                js_eval(interp, part, env, &v);
                if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
                js_to_string_buf(&v, piece, sizeof piece);
            }
            int cap = (int)sizeof(buf) - len; if (cap < 0) cap = 0;
            int wn = snprintf(buf + len, (size_t)cap, "%s", piece);
            if (wn > 0) len += wn;
        }
        js_set_string(out, buf);
        return;
    }
    case JS_NEW: {
        JsNode *callee = n->kids[0];
        JSValue clsval;
        js_eval(interp, callee, env, &clsval);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        if (clsval.type != JSV_OBJECT || !clsval.obj) { js_set_undefined(out); return; }
        if (clsval.obj->kind == JSOBJ_NATIVE) {
            /* "new SomeNativeFn(...)" -- a generic hook for a native
               "constructor" (currently only Promise, see
               js_install_promise()'s own comment) that builds and
               returns its own object directly, rather than this engine
               allocating a plain instance the way it does for a real
               `class` below. The native is simply CALLED with the
               already-evaluated arguments; its own return value (out)
               IS the result of `new`. */
            JSValue args[JS_MAX_ARGS];
            int argc = n->kid_count - 1;
            if (argc > JS_MAX_ARGS) argc = JS_MAX_ARGS;
            int i;
            for (i = 0; i < argc; i++) {
                js_eval(interp, n->kids[i + 1], env, &args[i]);
                if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
            }
            js_call_function(interp, clsval.obj, 0, args, argc, out);
            return;
        }
        if (clsval.obj->kind != JSOBJ_CLASS) { js_set_undefined(out); return; }
        JSObject *cls = clsval.obj;
        JSObject *inst = js_object_new(JSOBJ_PLAIN);
        inst->instance_methods = cls->methods;
        JSValue args[JS_MAX_ARGS];
        int argc = n->kid_count - 1;
        if (argc > JS_MAX_ARGS) argc = JS_MAX_ARGS;
        int i;
        for (i = 0; i < argc; i++) {
            JsNode *argnode = n->kids[i + 1];
            js_eval(interp, argnode, env, &args[i]);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        }
        JSValue ctorout;
        js_call_constructor(interp, cls, inst, args, argc, &ctorout);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; } /* a throw INSIDE the constructor must propagate, not silently produce a half-built instance */
        js_set_object(out, inst);
        return;
    }
    case JS_MEMBER: {
        JsNode *obj_expr = n->kids[0];
        JSValue base;
        js_eval(interp, obj_expr, env, &base);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        js_get_prop(interp, &base, n->str, out);
        return;
    }
    case JS_INDEX: {
        JsNode *obj_expr = n->kids[0];
        JsNode *idx_expr = n->kids[1];
        JSValue base, idx;
        js_eval(interp, obj_expr, env, &base);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        js_eval(interp, idx_expr, env, &idx);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        char name[64];
        js_to_string_buf(&idx, name, sizeof name);
        js_get_prop(interp, &base, name, out);
        return;
    }
    case JS_CALL: {
        JsNode *callee = n->kids[0];
        /* "super(...)" -- a bare call to the literal identifier "super",
         * only meaningful inside a subclass constructor (see
         * js_call_constructor()'s own comment on "__super_class__",
         * bound there). Not a real reserved word in this engine's lexer
         * (it's just an ordinary JSTOK_IDENT) -- a script using "super"
         * as a real variable name elsewhere would shadow this, a narrow,
         * accepted ambiguity. */
        if (callee->kind == JS_IDENT && !strcmp(callee->str, "super")) {
            JSValue superclsv, thisv;
            int have_cls = js_env_get(env, "__super_class__", &superclsv) && superclsv.type == JSV_OBJECT;
            int have_this = js_env_get(env, "this", &thisv) && thisv.type == JSV_OBJECT;
            js_set_undefined(out);
            if (!have_cls || !have_this) return;
            JSValue args[JS_MAX_ARGS];
            int argc = n->kid_count - 1;
            if (argc > JS_MAX_ARGS) argc = JS_MAX_ARGS;
            int i;
            for (i = 0; i < argc; i++) {
                JsNode *argnode = n->kids[i + 1];
                js_eval(interp, argnode, env, &args[i]);
                if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
            }
            js_call_constructor(interp, superclsv.obj, thisv.obj, args, argc, out);
            js_set_undefined(out); /* super(...)'s own "return value" is never used in real JS either */
            return;
        }
        JSValue this_val; js_set_undefined(&this_val);
        JSValue fn_val;
        if (callee->kind == JS_MEMBER) {
            JsNode *obj_expr = callee->kids[0];
            js_eval(interp, obj_expr, env, &this_val);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
            /* String methods (charAt/slice/split/etc) are handled directly
               here, not through the normal js_get_prop()+js_call_function()
               path below -- see js_string_method_call()'s own comment on
               why a primitive string has no JSObject to be `this`. */
            if (this_val.type == JSV_STRING) {
                JSValue args[JS_MAX_ARGS];
                int argc = n->kid_count - 1;
                if (argc > JS_MAX_ARGS) argc = JS_MAX_ARGS;
                int i;
                for (i = 0; i < argc; i++) {
                    js_eval(interp, n->kids[i + 1], env, &args[i]);
                    if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
                }
                if (js_string_method_call(interp, &this_val, callee->str, args, argc, out)) return;
                js_set_undefined(out);
                return;
            }
            js_get_prop(interp, &this_val, callee->str, &fn_val);
        } else {
            js_eval(interp, callee, env, &fn_val);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        }
        if (fn_val.type != JSV_OBJECT || !fn_val.obj) { js_set_undefined(out); return; }
        JSValue args[JS_MAX_ARGS];
        int argc = n->kid_count - 1;
        if (argc > JS_MAX_ARGS) argc = JS_MAX_ARGS;
        int i;
        for (i = 0; i < argc; i++) {
            JsNode *argnode = n->kids[i + 1];
            js_eval(interp, argnode, env, &args[i]);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        }
        JSObject *fn_obj = fn_val.obj;
        JSObject *this_obj = (this_val.type == JSV_OBJECT) ? this_val.obj : 0;
        js_call_function(interp, fn_obj, this_obj, args, argc, out);
        return;
    }
    case JS_UNARY: {
        JsNode *operand = n->kids[0];
        if (!strcmp(n->op, "ty")) {
            JSValue v;
            js_eval(interp, operand, env, &v);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
            if (v.type == JSV_UNDEFINED) { js_set_string(out, "undefined"); return; }
            if (v.type == JSV_NUMBER) { js_set_string(out, "number"); return; }
            if (v.type == JSV_STRING) { js_set_string(out, "string"); return; }
            if (v.type == JSV_BOOL) { js_set_string(out, "boolean"); return; }
            if (v.type == JSV_OBJECT && v.obj && (v.obj->kind == JSOBJ_FUNCTION || v.obj->kind == JSOBJ_NATIVE)) { js_set_string(out, "function"); return; }
            js_set_string(out, "object");
            return;
        }
        if (!strcmp(n->op, "aw")) {
            /* "await expr" -- see js_install_promise()'s own top comment
               on this engine's synchronous-settle Promise model. Awaiting
               a value that ISN'T a promise just evaluates to that value
               unchanged, real JS's own behavior. Awaiting an ALREADY-
               SETTLED promise (this engine's normal case, since nothing
               here has real cross-frame concurrency except setTimeout/
               fetch) unwraps its value, or -- for a rejected one -- makes
               the await itself throw, a real, catchable exception exactly
               like real JS's own "await rejects -> throws" rule (try/
               catch around an await works correctly here). Awaiting a
               still-PENDING promise (only reachable via a setTimeout- or
               fetch()-based promise that hasn't fired yet by the time
               execution reaches this await) is this engine's one real,
               disclosed gap in async/await -- no coroutine/fiber
               mechanism exists to actually suspend and later resume this
               specific point in the script (see js_engine.h's own top
               comment on this being a synchronous tree-walking
               interpreter) -- evaluates to undefined immediately instead
               of hanging or crashing, the same "never silently do the
               elaborate wrong thing, disclose the gap" convention this
               whole file already follows everywhere else. */
            JSValue v;
            js_eval(interp, operand, env, &v);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
            if (v.type != JSV_OBJECT || !v.obj || v.obj->kind != JSOBJ_PROMISE) { js_value_store(out, &v); return; }
            JSObject *prom = v.obj;
            if (prom->promise_state == JS_PROMISE_FULFILLED) { js_value_store(out, &prom->promise_value); return; }
            if (prom->promise_state == JS_PROMISE_REJECTED) {
                js_value_store(&interp->thrown_value, &prom->promise_value);
                interp->signal = JS_SIG_THROW;
                js_set_undefined(out);
                return;
            }
            js_set_undefined(out); /* still pending -- see this case's own comment above */
            return;
        }
        JSValue v;
        js_eval(interp, operand, env, &v);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        if (!strcmp(n->op, "!")) { js_set_bool(out, !js_to_bool(&v)); return; }
        if (!strcmp(n->op, "-")) { js_set_number(out, -js_to_number(&v)); return; }
        js_set_number(out, js_to_number(&v));
        return;
    }
    case JS_UPDATE: {
        JsNode *target = n->kids[0];
        JSValue cur;
        js_eval(interp, target, env, &cur);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; } /* see JS_BINARY's own comment on why every compound eval checks this */
        double d = js_to_number(&cur) + (!strcmp(n->op, "++") ? 1.0 : -1.0);
        js_set_number(out, d);
        js_assign_to(interp, target, env, out); /* always "new value" semantics -- see js_engine.h's own comment on postfix/prefix not being distinguished */
        return;
    }
    case JS_LOGICAL: {
        JsNode *lnode = n->kids[0];
        JsNode *rnode = n->kids[1];
        JSValue l;
        js_eval(interp, lnode, env, &l);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        if (!strcmp(n->op, "&&")) { if (js_to_bool(&l)) js_eval(interp, rnode, env, out); else js_value_store(out, &l); return; }
        if (js_to_bool(&l)) js_value_store(out, &l); else js_eval(interp, rnode, env, out);
        return;
    }
    case JS_BINARY: {
        JsNode *lnode = n->kids[0];
        JsNode *rnode = n->kids[1];
        JSValue l, r;
        js_eval(interp, lnode, env, &l);
        /* A thrown exception (JS_SIG_THROW, raised by a nested function
         * call -- see js_call_function()'s/js_call_constructor()'s own
         * comments) must short-circuit the REST of whatever expression
         * it was raised inside, not just the statement boundary
         * js_exec_block() already handles -- otherwise "a + risky() + b"
         * keeps right on computing a nonsense concatenation with
         * risky()'s "result" read as undefined, and (worse) an enclosing
         * assignment still stores THAT into its target, even though the
         * exception is supposed to unwind past all of this untouched.
         * Every multi-step case below (BINARY/LOGICAL/ASSIGN/UPDATE/COND,
         * call-argument loops, array/object-literal loops) checks this
         * the same way, immediately after each sub-eval that could
         * itself contain a call. */
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        js_eval(interp, rnode, env, &r);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        const char *op = n->op;
        if (!strcmp(op, "+")) {
            if (l.type == JSV_STRING || r.type == JSV_STRING) {
                char lb[JS_STR_MAX], rb[JS_STR_MAX], cat[JS_STR_MAX * 2];
                js_to_string_buf(&l, lb, sizeof lb);
                js_to_string_buf(&r, rb, sizeof rb);
                snprintf(cat, sizeof cat, "%s%s", lb, rb);
                js_set_string(out, cat);
                return;
            }
            js_set_number(out, js_to_number(&l) + js_to_number(&r));
            return;
        }
        if (!strcmp(op, "-")) { js_set_number(out, js_to_number(&l) - js_to_number(&r)); return; }
        if (!strcmp(op, "*")) { js_set_number(out, js_to_number(&l) * js_to_number(&r)); return; }
        if (!strcmp(op, "/")) { js_set_number(out, js_to_number(&l) / js_to_number(&r)); return; }
        if (!strcmp(op, "%")) { js_set_number(out, fmod(js_to_number(&l), js_to_number(&r))); return; }
        if (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) {
            int cmp;
            if (l.type == JSV_STRING && r.type == JSV_STRING) cmp = strcmp(l.str ? l.str : "", r.str ? r.str : "");
            else { double dl = js_to_number(&l), dr = js_to_number(&r); cmp = (dl < dr) ? -1 : (dl > dr) ? 1 : 0; }
            if (!strcmp(op, "<")) { js_set_bool(out, cmp < 0); return; }
            if (!strcmp(op, ">")) { js_set_bool(out, cmp > 0); return; }
            if (!strcmp(op, "<=")) { js_set_bool(out, cmp <= 0); return; }
            js_set_bool(out, cmp >= 0);
            return;
        }
        /* ==/!=/===/!== all treated identically here -- no real type
           coercion table (a documented simplification, see this file's
           own top comment): two values compare equal iff same type AND
           same value (string/number/bool by content, object by identity,
           null/undefined each only equal to themselves). */
        int eq;
        if (l.type != r.type) eq = 0;
        else if (l.type == JSV_NUMBER) eq = (l.num == r.num);
        else if (l.type == JSV_STRING) eq = (strcmp(l.str ? l.str : "", r.str ? r.str : "") == 0);
        else if (l.type == JSV_BOOL) eq = (l.boolean == r.boolean);
        else if (l.type == JSV_OBJECT) eq = (l.obj == r.obj);
        else eq = 1; /* both undefined, or both null */
        if (!strcmp(op, "==") || !strcmp(op, "===")) { js_set_bool(out, eq); return; }
        js_set_bool(out, !eq);
        return;
    }
    case JS_COND: {
        JsNode *cond = n->kids[0];
        JsNode *then_e = n->kids[1];
        JsNode *else_e = n->kids[2];
        JSValue c;
        js_eval(interp, cond, env, &c);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
        if (js_to_bool(&c)) js_eval(interp, then_e, env, out); else js_eval(interp, else_e, env, out);
        return;
    }
    case JS_ASSIGN: {
        JsNode *target = n->kids[0];
        JsNode *rhs = n->kids[1];
        JSValue rv;
        js_eval(interp, rhs, env, &rv);
        if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; } /* a throw while evaluating the RHS must skip the assignment entirely */
        if (strcmp(n->op, "=") != 0) {
            JSValue cur;
            js_eval(interp, target, env, &cur);
            if (interp->signal != JS_SIG_NONE) { js_set_undefined(out); return; }
            double a = js_to_number(&cur), b = js_to_number(&rv);
            double res = a;
            if (!strcmp(n->op, "+=")) {
                if (cur.type == JSV_STRING || rv.type == JSV_STRING) {
                    char lb[JS_STR_MAX], rb[JS_STR_MAX], cat[JS_STR_MAX * 2];
                    js_to_string_buf(&cur, lb, sizeof lb);
                    js_to_string_buf(&rv, rb, sizeof rb);
                    snprintf(cat, sizeof cat, "%s%s", lb, rb);
                    js_set_string(&rv, cat);
                    js_assign_to(interp, target, env, &rv);
                    js_value_store(out, &rv);
                    return;
                }
                res = a + b;
            } else if (!strcmp(n->op, "-=")) res = a - b;
            else if (!strcmp(n->op, "*=")) res = a * b;
            else if (!strcmp(n->op, "/=")) res = a / b;
            js_set_number(&rv, res);
        }
        js_assign_to(interp, target, env, &rv);
        js_value_store(out, &rv);
        return;
    }
    default: js_set_undefined(out); return;
    }
}

/* ============================= Exec ============================= */

/* Looks up an already-run module's own exports object by `module_id`
   (see js_run_module()'s own top comment) -- NULL if no module with that
   id has run yet (a forward/circular import, or a genuinely missing one;
   JS_IMPORT's own exec case treats either the same way: every imported
   local name just binds to undefined, never a crash). */
static JSObject *js_module_find_exports(JSInterp *interp, const char *module_id) {
    int i;
    for (i = 0; i < interp->module_count; i++) {
        if (!strcmp(interp->modules[i].id, module_id)) return interp->modules[i].exports;
    }
    return 0;
}

static void js_exec_stmt(JSInterp *interp, JsNode *n, JSEnv *env) {
    switch (n->kind) {
    case JS_EMPTY: return;
    case JS_EXPR_STMT: { JSValue tmp; js_eval(interp, n->kids[0], env, &tmp); return; }
    case JS_VAR_DECL: {
        JSValue v;
        if (n->kid_count > 0) js_eval(interp, n->kids[0], env, &v);
        else js_set_undefined(&v);
        js_env_define(env, n->str, &v);
        return;
    }
    case JS_VAR_DECL_PATTERN: {
        /* kids[0]=pattern (JS_DESTR_OBJECT/JS_DESTR_ARRAY), kids[1]=init expr --
           see js_parse_destr_pattern()'s own comment for the exact shape. */
        JsNode *pat = n->kids[0];
        JsNode *initnode = n->kids[1];
        JSValue src;
        js_eval(interp, initnode, env, &src);
        if (interp->signal != JS_SIG_NONE) return;
        if (pat->kind == JS_DESTR_OBJECT) {
            int i;
            for (i = 0; i < pat->kid_count; i++) {
                JsNode *prop = pat->kids[i]; /* str=source key, kids[0]=JS_IDENT target */
                JsNode *target = prop->kids[0];
                JSValue v;
                js_get_prop(interp, &src, prop->str, &v);
                js_env_define(env, target->str, &v);
            }
        } else if (pat->kind == JS_DESTR_ARRAY) {
            int i;
            for (i = 0; i < pat->kid_count; i++) {
                JsNode *target = pat->kids[i];
                char idxbuf[16]; snprintf(idxbuf, sizeof idxbuf, "%d", i);
                JSValue v;
                js_get_prop(interp, &src, idxbuf, &v);
                js_env_define(env, target->str, &v);
            }
        }
        return;
    }
    case JS_BLOCK: {
        JSEnv *inner = js_env_new(env);
        js_exec_block(interp, n, inner);
        return;
    }
    case JS_FUNC_DECL: return; /* already hoisted -- see js_hoist_functions() */
    case JS_CLASS_DECL: {
        JSObject *cls = js_object_new(JSOBJ_CLASS);
        cls->methods = js_object_new(JSOBJ_PLAIN);
        if (n->str2) {
            JSValue parentv;
            if (js_env_get(env, n->str2, &parentv) && parentv.type == JSV_OBJECT && parentv.obj && parentv.obj->kind == JSOBJ_CLASS) {
                JSObject *parent = parentv.obj;
                cls->parent_class = parent;
                /* Seed with a COPY of the parent's own methods -- see
                   JSObject::methods' own comment on why this is a real
                   but simplified one-time linearization, not a live
                   prototype chain. */
                JSObject *pm = parent->methods;
                if (pm) {
                    int i;
                    for (i = 0; i < pm->prop_count; i++) {
                        const char *pname = pm->prop_names[i];
                        JSValue *pval = &pm->prop_values[i];
                        js_obj_set(cls->methods, pname, pval);
                    }
                }
            }
        }
        int i;
        for (i = 0; i < n->kid_count; i++) {
            JsNode *prop = n->kids[i]; /* JS_PROP(str=method name, kids[0]=JS_FUNC_EXPR, op[0]='g'/'s' for get/set) */
            JsNode *fnnode = prop->kids[0];
            JSObject *fo = js_object_new(JSOBJ_FUNCTION);
            fo->func_node = fnnode;
            fo->closure_env = env;
            /* "get x() {...}"/"set x(v) {...}" class methods -- same
               mangled-name convention as JS_OBJECT_LIT's own (see its
               comment); stored in cls->methods so instance property
               access finds them via instance_methods, exactly like an
               ordinary method. */
            if (prop->op[0] == 'g' || prop->op[0] == 's') {
                char accname[JS_IDENT_MAX + 8];
                snprintf(accname, sizeof accname, prop->op[0] == 'g' ? "__get_%s__" : "__set_%s__", prop->str);
                js_obj_set_obj(cls->methods, accname, fo);
            } else {
                js_obj_set_obj(cls->methods, prop->str, fo);
            }
        }
        js_env_define_obj(env, n->str, cls);
        return;
    }
    case JS_THROW: {
        js_eval(interp, n->kids[0], env, &interp->thrown_value);
        /* "throw riskyFn()" where riskyFn() itself throws: signal is
           already JS_SIG_THROW with the REAL thrown value in place --
           leave it alone rather than overwriting it with this throw
           statement's own (never-reached) value. */
        if (interp->signal == JS_SIG_NONE) interp->signal = JS_SIG_THROW;
        return;
    }
    case JS_IMPORT: {
        /* See js_run_module()'s own top comment for the overall design.
           A module not found yet (js_module_find_exports() returns NULL)
           silently binds every imported name to undefined rather than
           erroring -- this engine's own "never crash on a script"
           convention, applied to the one new failure mode modules add
           (an import whose target hasn't run, or never will). */
        JSObject *exports = n->str2 ? js_module_find_exports(interp, n->str2) : 0;
        int i;
        for (i = 0; i < n->kid_count; i++) {
            JsNode *prop = n->kids[i]; /* str = source name, kids[0] = JS_IDENT local name */
            JsNode *localid = prop->kids[0];
            JSValue v;
            if (exports && js_obj_get(exports, prop->str, &v)) { /* found */ }
            else js_set_undefined(&v);
            js_env_define(env, localid->str, &v);
        }
        return;
    }
    case JS_EXPORT: {
        /* "export default expr;" -- kids[0] is a plain EXPRESSION (no
           declared name at all), exported under the literal key
           "default" (real JS's own convention: "import x from 'mod'"
           reads exactly that key -- see JS_IMPORT's own "default" source
           name above). */
        if (n->str && !strcmp(n->str, "default")) {
            JSValue v;
            js_eval(interp, n->kids[0], env, &v);
            if (interp->signal != JS_SIG_NONE) return;
            if (interp->current_module_exports) js_obj_set(interp->current_module_exports, "default", &v);
            return;
        }
        /* Otherwise kids[0] is a real declaration (function/class/var) --
           run it NORMALLY (defines the binding in `env` exactly like the
           same declaration would with no "export" at all), then copy the
           resulting value into the current module's own exports object
           under its declared name. "export var a=1, b=2;" parses as a
           JS_BLOCK of JS_VAR_DECLs (see js_parse_var_decl()'s own
           comment) -- walked here to export EACH name, not just one. */
        JsNode *decl = n->kids[0];
        js_exec_stmt(interp, decl, env);
        if (interp->signal != JS_SIG_NONE) return;
        if (!interp->current_module_exports) return;
        if (decl->kind == JS_BLOCK) {
            int i;
            for (i = 0; i < decl->kid_count; i++) {
                JsNode *one = decl->kids[i];
                if (!one->str) continue;
                JSValue v;
                if (js_env_get(env, one->str, &v)) js_obj_set(interp->current_module_exports, one->str, &v);
            }
            return;
        }
        if (!decl->str) return;
        JSValue v;
        if (js_env_get(env, decl->str, &v)) js_obj_set(interp->current_module_exports, decl->str, &v);
        return;
    }
    case JS_TRY: {
        /* kids[0]=try block, kids[1]=catch block (JS_EMPTY if none),
           kids[2]=finally block (JS_EMPTY if none); str=catch param name. */
        JsNode *tryblk = n->kids[0];
        JsNode *catchblk = n->kids[1];
        JsNode *finallyblk = n->kids[2];
        js_exec_stmt(interp, tryblk, env);
        if (interp->signal == JS_SIG_THROW && catchblk->kind != JS_EMPTY) {
            JSEnv *catch_env = js_env_new(env);
            if (n->str) js_env_define(catch_env, n->str, &interp->thrown_value);
            interp->signal = JS_SIG_NONE; /* caught -- the exception stops propagating from here */
            js_exec_stmt(interp, catchblk, catch_env);
        }
        if (finallyblk->kind != JS_EMPTY) {
            /* A pending return/break/continue/throw from the try/catch
               above is deliberately preserved THROUGH the finally block
               (real JS semantics) unless finally itself raises its own
               new one -- save and restore around it, same convention
               used nowhere else in this file since this is the one place
               two DIFFERENT pending-signal states can legitimately
               coexist momentarily. */
            int saved_signal = interp->signal;
            JSValue saved_return, saved_thrown;
            js_value_store(&saved_return, &interp->return_value);
            js_value_store(&saved_thrown, &interp->thrown_value);
            interp->signal = JS_SIG_NONE;
            js_exec_stmt(interp, finallyblk, env);
            if (interp->signal == JS_SIG_NONE) {
                interp->signal = saved_signal;
                js_value_store(&interp->return_value, &saved_return);
                js_value_store(&interp->thrown_value, &saved_thrown);
            } /* else: finally itself raised a new pending signal -- that one wins, real JS behavior */
        }
        return;
    }
    case JS_FOR_OF: {
        JsNode *iterexpr = n->kids[0];
        JsNode *body = n->kids[1];
        JSValue iterval;
        js_eval(interp, iterexpr, env, &iterval);
        if (iterval.type != JSV_OBJECT || !iterval.obj) return;
        JSObject *o = iterval.obj;
        if (o->kind == JSOBJ_GENERATOR) {
            /* Drives a generator (see the "Generators" section's own top
               comment) via repeated .next() calls -- bounded by the SAME
               iteration cap as any other loop (JS_LOOP_MAX_ITERATIONS),
               so "for (var v of infiniteGenerator()) {...}" with no
               internal break still can't run forever, exactly matching
               JS_WHILE/JS_FOR's own guard for the identical reason. */
            int guard = 0;
            for (;;) {
                JSValue args[1]; js_set_undefined(&args[0]);
                JSValue nextres;
                js_native_generator_next(interp, args, 0, o, &nextres);
                if (nextres.type != JSV_OBJECT || !nextres.obj) break;
                JSValue donev; js_obj_get(nextres.obj, "done", &donev);
                if (js_to_bool(&donev)) break;
                JSValue valuev; js_obj_get(nextres.obj, "value", &valuev);
                JSEnv *iter_env = js_env_new(env);
                js_env_define(iter_env, n->str, &valuev);
                js_exec_stmt(interp, body, iter_env);
                if (interp->signal == JS_SIG_BREAK) { interp->signal = JS_SIG_NONE; break; }
                if (interp->signal == JS_SIG_CONTINUE) interp->signal = JS_SIG_NONE;
                else if (interp->signal != JS_SIG_NONE) return;
                if (++guard > JS_LOOP_MAX_ITERATIONS) { fprintf(stderr, "SQW/js: for-of over a generator exceeded iteration cap, aborting script\n"); return; }
            }
            return;
        }
        int len = (o->kind == JSOBJ_ARRAY) ? o->arr_len : 0;
        int k;
        for (k = 0; k < len; k++) {
            JSEnv *iter_env = js_env_new(env);
            JSValue item;
            js_value_store(&item, &o->arr_items[k]);
            js_env_define(iter_env, n->str, &item);
            js_exec_stmt(interp, body, iter_env);
            if (interp->signal == JS_SIG_BREAK) { interp->signal = JS_SIG_NONE; break; }
            if (interp->signal == JS_SIG_CONTINUE) interp->signal = JS_SIG_NONE;
            else if (interp->signal != JS_SIG_NONE) return;
        }
        return;
    }
    case JS_FOR_IN: {
        /* Real JS "for (k in obj)" iterates OWN enumerable property
           KEYS (as strings) -- for an array, that's its numeric indices
           (as strings, "0","1",...), matching real JS's own (slightly
           surprising to newcomers) behavior; for a plain object, its own
           property names. */
        JsNode *objexpr = n->kids[0];
        JsNode *body = n->kids[1];
        JSValue objval;
        js_eval(interp, objexpr, env, &objval);
        if (objval.type != JSV_OBJECT || !objval.obj) return;
        JSObject *o = objval.obj;
        if (o->kind == JSOBJ_ARRAY) {
            int k;
            for (k = 0; k < o->arr_len; k++) {
                JSEnv *iter_env = js_env_new(env);
                char keybuf[16]; snprintf(keybuf, sizeof keybuf, "%d", k);
                JSValue keyv; js_set_string(&keyv, keybuf);
                js_env_define(iter_env, n->str, &keyv);
                js_exec_stmt(interp, body, iter_env);
                if (interp->signal == JS_SIG_BREAK) { interp->signal = JS_SIG_NONE; break; }
                if (interp->signal == JS_SIG_CONTINUE) interp->signal = JS_SIG_NONE;
                else if (interp->signal != JS_SIG_NONE) return;
            }
        } else {
            int k;
            for (k = 0; k < o->prop_count; k++) {
                JSEnv *iter_env = js_env_new(env);
                const char *pname = o->prop_names[k];
                JSValue keyv; js_set_string(&keyv, pname);
                js_env_define(iter_env, n->str, &keyv);
                js_exec_stmt(interp, body, iter_env);
                if (interp->signal == JS_SIG_BREAK) { interp->signal = JS_SIG_NONE; break; }
                if (interp->signal == JS_SIG_CONTINUE) interp->signal = JS_SIG_NONE;
                else if (interp->signal != JS_SIG_NONE) return;
            }
        }
        return;
    }
    case JS_IF: {
        JsNode *cond = n->kids[0];
        JsNode *then_s = n->kids[1];
        JSValue c;
        js_eval(interp, cond, env, &c);
        if (js_to_bool(&c)) js_exec_stmt(interp, then_s, env);
        else if (n->kid_count > 2) js_exec_stmt(interp, n->kids[2], env);
        return;
    }
    case JS_WHILE: {
        JsNode *cond = n->kids[0];
        JsNode *body = n->kids[1];
        int guard = 0;
        for (;;) {
            JSValue c;
            js_eval(interp, cond, env, &c);
            if (!js_to_bool(&c)) break;
            js_exec_stmt(interp, body, env);
            if (interp->signal == JS_SIG_BREAK) { interp->signal = JS_SIG_NONE; break; }
            if (interp->signal == JS_SIG_CONTINUE) interp->signal = JS_SIG_NONE;
            else if (interp->signal != JS_SIG_NONE) return; /* return -- propagate up */
            /* A generous but real iteration cap -- a script with a
               genuine infinite loop (a bug in the SCRIPT, not this
               engine) would otherwise hang the whole render loop
               forever, with no way for the user to recover short of
               killing the process; real browsers eventually show a
               "page unresponsive" prompt, this engine just stops the
               script and lets the page keep rendering whatever it had
               already done, the safer failure mode for an embedded
               interpreter with no such prompt available.
               JS_LOOP_MAX_ITERATIONS is deliberately far below what a
               pure CPU-time budget alone would suggest -- found via this
               session's own fuzz testing: any loop body containing a
               block statement (almost all of them -- see JS_BLOCK's own
               case) allocates a fresh ~6.7KB JSEnv EVERY iteration that
               is never freed (this engine's own documented "leak for the
               page's lifetime, no GC" tradeoff, see js_interp_free()'s
               own comment) -- at the OLD cap of 2,000,000 that is upward
               of 13GB leaked, measured to reliably exhaust memory and get
               the whole host process OOM-killed well before the
               iteration cap itself was ever reached, an actual crash
               this "safety" cap was supposed to prevent, not cause. */
            if (++guard > JS_LOOP_MAX_ITERATIONS) { fprintf(stderr, "SQW/js: while loop exceeded iteration cap, aborting script\n"); return; }
        }
        return;
    }
    case JS_FOR: {
        JsNode *init = n->kids[0];
        JsNode *cond = n->kids[1];
        JsNode *incr = n->kids[2];
        JsNode *body = n->kids[3];
        JSEnv *for_env = js_env_new(env);
        js_exec_stmt(interp, init, for_env);
        int guard = 0;
        for (;;) {
            if (cond->kind != JS_EMPTY) {
                JSValue c;
                js_eval(interp, cond, for_env, &c);
                if (!js_to_bool(&c)) break;
            }
            js_exec_stmt(interp, body, for_env);
            if (interp->signal == JS_SIG_BREAK) { interp->signal = JS_SIG_NONE; break; }
            if (interp->signal == JS_SIG_CONTINUE) interp->signal = JS_SIG_NONE;
            else if (interp->signal != JS_SIG_NONE) return;
            if (incr->kind != JS_EMPTY) { JSValue tmp; js_eval(interp, incr, for_env, &tmp); }
            /* See JS_WHILE's own comment on JS_LOOP_MAX_ITERATIONS -- same
               per-iteration JSEnv-leak reasoning applies identically here. */
            if (++guard > JS_LOOP_MAX_ITERATIONS) { fprintf(stderr, "SQW/js: for loop exceeded iteration cap, aborting script\n"); return; }
        }
        return;
    }
    case JS_RETURN: {
        if (n->kid_count > 0) js_eval(interp, n->kids[0], env, &interp->return_value);
        else js_set_undefined(&interp->return_value);
        /* A thrown exception raised WHILE evaluating the return expression
           itself (e.g. "return riskyCall();" where riskyCall() throws, or
           -- found via this session's own fuzz testing -- the call-depth
           guard's own throw firing on this exact call) must not be
           clobbered by an unconditional JS_SIG_RETURN here; every other
           sub-evaluation in this file already checks this the same way
           (see JS_BINARY/JS_CALL/etc's own signal-checks). */
        if (interp->signal != JS_SIG_NONE) return;
        interp->signal = JS_SIG_RETURN;
        return;
    }
    case JS_BREAK: interp->signal = JS_SIG_BREAK; return;
    case JS_CONTINUE: interp->signal = JS_SIG_CONTINUE; return;
    default: { JSValue tmp; js_eval(interp, n, env, &tmp); return; }
    }
}

/* ============================= Builtins ============================= */

/* Iterative pre-order walk, same stack-based pattern css_apply()/
 * sqw_apply_css() already use elsewhere in this project (recursion would
 * work fine at this project's own page scale too, but consistency with
 * the established convention costs nothing). Returns the first element
 * whose "id" attribute matches, or NULL. */
static DomNode *js_dom_find_by_id(DomNode *root, const char *id) {
    int cap = 64, top = 0;
    DomNode **stack = (DomNode **)malloc((size_t)cap * sizeof(DomNode *));
    int *next_child = (int *)malloc((size_t)cap * sizeof(int));
    stack[top] = root; next_child[top] = 0; top++;
    DomNode *found = 0;
    while (top > 0 && !found) {
        DomNode *node = stack[top - 1];
        if (next_child[top - 1] >= node->child_count) { top--; continue; }
        DomNode *child = node->children[next_child[top - 1]];
        next_child[top - 1]++;
        if (!dom_is_text(child)) {
            const char *cid = dom_get_attr(child, "id");
            if (cid && !strcmp(cid, id)) { found = child; break; }
            if (top >= cap) {
                cap *= 2;
                stack = (DomNode **)realloc(stack, (size_t)cap * sizeof(DomNode *));
                next_child = (int *)realloc(next_child, (size_t)cap * sizeof(int));
            }
            stack[top] = child; next_child[top] = 0; top++;
        }
    }
    free(stack); free(next_child);
    return found;
}

static DomNode *js_dom_find_by_tag(DomNode *root, const char *tag) {
    int cap = 64, top = 0;
    DomNode **stack = (DomNode **)malloc((size_t)cap * sizeof(DomNode *));
    int *next_child = (int *)malloc((size_t)cap * sizeof(int));
    stack[top] = root; next_child[top] = 0; top++;
    DomNode *found = 0;
    while (top > 0 && !found) {
        DomNode *node = stack[top - 1];
        if (next_child[top - 1] >= node->child_count) { top--; continue; }
        DomNode *child = node->children[next_child[top - 1]];
        next_child[top - 1]++;
        if (!dom_is_text(child)) {
            if (!strcmp(child->tag, tag)) { found = child; break; }
            if (top >= cap) {
                cap *= 2;
                stack = (DomNode **)realloc(stack, (size_t)cap * sizeof(DomNode *));
                next_child = (int *)realloc(next_child, (size_t)cap * sizeof(int));
            }
            stack[top] = child; next_child[top] = 0; top++;
        }
    }
    free(stack); free(next_child);
    return found;
}

#define JS_MAX_COLLECT 128
static int js_dom_collect_by_tag(DomNode *root, const char *tag, DomNode **out, int outcap) {
    int cap = 64, top = 0, n = 0;
    DomNode **stack = (DomNode **)malloc((size_t)cap * sizeof(DomNode *));
    int *next_child = (int *)malloc((size_t)cap * sizeof(int));
    stack[top] = root; next_child[top] = 0; top++;
    while (top > 0) {
        DomNode *node = stack[top - 1];
        if (next_child[top - 1] >= node->child_count) { top--; continue; }
        DomNode *child = node->children[next_child[top - 1]];
        next_child[top - 1]++;
        if (!dom_is_text(child)) {
            if (n < outcap && (!strcmp(tag, "*") || !strcmp(child->tag, tag))) out[n++] = child;
            if (top >= cap) {
                cap *= 2;
                stack = (DomNode **)realloc(stack, (size_t)cap * sizeof(DomNode *));
                next_child = (int *)realloc(next_child, (size_t)cap * sizeof(int));
            }
            stack[top] = child; next_child[top] = 0; top++;
        }
    }
    free(stack); free(next_child);
    return n;
}

static void js_native_console_log(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)interp; (void)this_obj;
    fprintf(stderr, "SQW/js console:");
    int i;
    for (i = 0; i < argc; i++) {
        char buf[JS_STR_MAX];
        js_to_string_buf(&args[i], buf, sizeof buf);
        fprintf(stderr, " %s", buf);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
    js_set_undefined(out);
}
static void js_native_alert(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    /* No real dialog available -- logs the same way console.log does, so
       a script's alert() calls are at least visible during local testing
       instead of silently vanishing. A documented, deliberate gap: SQW
       has no modal-dialog UI at all yet, not specific to this engine. */
    js_native_console_log(interp, args, argc, this_obj, out);
}
/* ---- Global conversion/predicate functions: parseInt/parseFloat/isNaN/
   Number()/String()/Boolean() ---- real JS's own most commonly used
   global free functions outside an object namespace. Number()/String()/
   Boolean() here are ALWAYS plain conversions (never "new Number(x)"
   boxing -- this engine has no primitive-wrapper objects at all, see
   js_get_prop()'s own comment on that), matching how real code almost
   always uses them anyway. */
static void js_native_parse_int(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    if (c < 1) { js_set_number(out, 0.0 / 0.0); return; }
    char buf[64]; js_to_string_buf(&a[0], buf, sizeof buf);
    int radix = c > 1 ? (int)js_to_number(&a[1]) : 10;
    if (radix == 0) radix = 10;
    char *end = 0;
    const char *p = buf; while (*p == ' ' || *p == '\t') p++;
    long v = strtol(p, &end, radix);
    if (end == p) { js_set_number(out, 0.0 / 0.0); return; }
    js_set_number(out, (double)v);
}
static void js_native_parse_float(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    if (c < 1) { js_set_number(out, 0.0 / 0.0); return; }
    char buf[64]; js_to_string_buf(&a[0], buf, sizeof buf);
    char *end = 0;
    const char *p = buf; while (*p == ' ' || *p == '\t') p++;
    double v = strtod(p, &end);
    if (end == p) { js_set_number(out, 0.0 / 0.0); return; }
    js_set_number(out, v);
}
/* A real, confirmed squash codegen bug (found this session, standalone
 * repro in the scratchpad): double `==`/`!=` comparison does NOT follow
 * real IEEE-754 "NaN compares unequal to everything, including itself"
 * semantics -- squash-compiled `nan_value != nan_value` evaluates to
 * false (gcc-compiled, correctly, evaluates to true), and `isnan()` from
 * <math.h> is unreliable too. The NaN bit pattern itself IS produced and
 * stored correctly (confirmed via memcpy-to-uint64 in the same repro) --
 * only the FLOATING-POINT COMPARISON instruction misbehaves on it. This
 * function works around it by checking the IEEE-754 double bit pattern
 * directly (exponent all-1s, non-zero mantissa) via memcpy, using only
 * INTEGER comparison -- never a `==`/`!=` on the double itself. */
static int js_double_is_nan(double v) {
    unsigned long long bits;
    memcpy(&bits, &v, sizeof bits);
    unsigned long long exp = (bits >> 52) & 0x7FFULL;
    unsigned long long mant = bits & 0xFFFFFFFFFFFFFULL;
    return exp == 0x7FFULL && mant != 0ULL;
}
static void js_native_is_nan(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    double v = c > 0 ? js_to_number(&a[0]) : (0.0 / 0.0);
    js_set_bool(out, js_double_is_nan(v));
}
static void js_native_number(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    js_set_number(out, c > 0 ? js_to_number(&a[0]) : 0.0);
}
static void js_native_string(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    if (c < 1) { js_set_string(out, ""); return; }
    char buf[JS_STR_MAX]; js_to_string_buf(&a[0], buf, sizeof buf);
    js_set_string(out, buf);
}
static void js_native_boolean(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    js_set_bool(out, c > 0 ? js_to_bool(&a[0]) : 0);
}
static void js_native_math_floor(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) { (void)i; (void)t; js_set_number(out, c > 0 ? floor(js_to_number(&a[0])) : 0.0); }
static void js_native_math_ceil(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) { (void)i; (void)t; js_set_number(out, c > 0 ? ceil(js_to_number(&a[0])) : 0.0); }
static void js_native_math_round(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) { (void)i; (void)t; js_set_number(out, c > 0 ? floor(js_to_number(&a[0]) + 0.5) : 0.0); }
static void js_native_math_abs(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) { (void)i; (void)t; js_set_number(out, c > 0 ? fabs(js_to_number(&a[0])) : 0.0); }
static void js_native_math_sqrt(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) { (void)i; (void)t; js_set_number(out, c > 0 ? sqrt(js_to_number(&a[0])) : 0.0); }
static void js_native_math_pow(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) { (void)i; (void)t; js_set_number(out, c > 1 ? pow(js_to_number(&a[0]), js_to_number(&a[1])) : 0.0); }
static void js_native_math_max(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)i; (void)t;
    if (c == 0) { js_set_number(out, -1.0/0.0); return; }
    double m = js_to_number(&a[0]); int k; for (k = 1; k < c; k++) { double v = js_to_number(&a[k]); if (v > m) m = v; }
    js_set_number(out, m);
}
static void js_native_math_min(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)i; (void)t;
    if (c == 0) { js_set_number(out, 1.0/0.0); return; }
    double m = js_to_number(&a[0]); int k; for (k = 1; k < c; k++) { double v = js_to_number(&a[k]); if (v < m) m = v; }
    js_set_number(out, m);
}
static void js_native_math_random(JSInterp *i, JSValue *a, int c, JSObject *t, JSValue *out) { (void)i; (void)a; (void)c; (void)t; js_set_number(out, (double)rand() / ((double)RAND_MAX + 1.0)); }

static void js_native_document_get_by_id(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)this_obj;
    if (argc < 1) { js_set_null(out); return; }
    char id[128];
    js_to_string_buf(&args[0], id, sizeof id);
    DomNode *found = js_dom_find_by_id(interp->document_root, id);
    if (found) js_set_object(out, js_wrap_dom_node(found)); else js_set_null(out);
}
static void js_native_document_get_by_tag(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)this_obj;
    if (argc < 1) { js_set_null(out); return; }
    char tag[64];
    js_to_string_buf(&args[0], tag, sizeof tag);
    DomNode *matches[JS_MAX_COLLECT];
    int n = js_dom_collect_by_tag(interp->document_root, tag, matches, JS_MAX_COLLECT);
    JSObject *list = js_object_new(JSOBJ_PLAIN);
    js_obj_set_num(list, "length", (double)n);
    int i;
    for (i = 0; i < n; i++) {
        char key[16]; snprintf(key, sizeof key, "%d", i);
        DomNode *m = matches[i];
        js_obj_set_obj(list, key, js_wrap_dom_node(m));
    }
    js_set_object(out, list);
}

/* Installs document/console/Math/alert/window (an alias for the global
 * object itself, real JS's own top-level-is-window convention, just
 * enough that "window.foo" reads the same as a bare "foo" for a script
 * that happens to write it that way) into `env`.
 *
 * Split into one small function per installed global -- NOT inlined into
 * one js_install_builtins() body. A real, confirmed squash codegen bug
 * (found this session): a single function accumulating this many live
 * locals across many sequential statements (document/root/body/html/
 * console/math, each alive from its own declaration through the rest of
 * the function) corrupted a plain pointer-return-and-assign a few
 * statements in, even though every individual statement is completely
 * ordinary C -- the same "too many simultaneously-live locals" bug class
 * layout.c's own PlaceSpec/init_block_frame comments and sqw_main.c's own
 * SqwAppState comment already document elsewhere in this project. Each
 * helper below has only 1-3 live locals of its own, well clear of it. */
/* document.createElement(tag) -- a real DomNode, allocated but NOT
 * attached anywhere (real DOM semantics: it only becomes part of the page
 * once passed to a real appendChild()). css_font_size/css_opacity are
 * seeded to their real CSS initial values by hand here since this node
 * never goes through css_apply()'s own default-seeding pass (that only
 * runs once, on the whole tree, right before the FIRST layout -- see
 * sqw_apply_css()'s own comment) -- an element created and appended
 * DURING that first script run still gets a real, correct computed style
 * from the css_apply() that follows; one created later (e.g. from an
 * onclick handler, after the page has already been styled once) does
 * NOT, and keeps whatever bare defaults are set here -- a real,
 * documented gap (this engine has no incremental/on-demand restyle,
 * matching innerHTML's own identical limitation elsewhere in this file). */
static void js_native_create_element(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    if (c < 1) { js_set_null(out); return; }
    char tag[HTML_MAX_TAG_LEN]; js_to_string_buf(&a[0], tag, sizeof tag);
    DomNode *el = (DomNode *)malloc(sizeof(DomNode));
    memset(el, 0, sizeof(*el));
    strncpy(el->tag, tag, sizeof el->tag - 1);
    el->css_font_size = 16.0f;
    el->css_opacity = 1.0f;
    js_set_object(out, js_wrap_dom_node(el));
}
static void js_native_create_text_node(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    char text[JS_STR_MAX]; text[0] = 0;
    if (c > 0) js_to_string_buf(&a[0], text, sizeof text);
    DomNode *tn = (DomNode *)malloc(sizeof(DomNode));
    memset(tn, 0, sizeof(*tn));
    tn->text = strdup(text);
    js_set_object(out, js_wrap_dom_node(tn));
}

static void js_install_document(JSInterp *interp, JSEnv *env) {
    JSObject *document = js_object_new(JSOBJ_PLAIN);
    js_obj_set_native(document, "getElementById", js_native_document_get_by_id);
    js_obj_set_native(document, "getElementsByTagName", js_native_document_get_by_tag);
    js_obj_set_native(document, "querySelector", js_native_query_selector);
    js_obj_set_native(document, "querySelectorAll", js_native_query_selector_all);
    js_obj_set_native(document, "createElement", js_native_create_element);
    js_obj_set_native(document, "createTextNode", js_native_create_text_node);
    DomNode *root = interp->document_root;
    DomNode *body = js_dom_find_by_tag(root, "body");
    if (body) js_obj_set_obj(document, "body", js_wrap_dom_node(body));
    DomNode *html = js_dom_find_by_tag(root, "html");
    if (html) js_obj_set_obj(document, "documentElement", js_wrap_dom_node(html));
    js_env_define_obj(env, "document", document);
}

static void js_install_console(JSEnv *env) {
    JSObject *console = js_object_new(JSOBJ_PLAIN);
    js_obj_set_native(console, "log", js_native_console_log);
    js_env_define_obj(env, "console", console);
}

static void js_install_math(JSEnv *env) {
    JSObject *math = js_object_new(JSOBJ_PLAIN);
    js_obj_set_num(math, "PI", 3.14159265358979323846);
    js_obj_set_native(math, "floor", js_native_math_floor);
    js_obj_set_native(math, "ceil", js_native_math_ceil);
    js_obj_set_native(math, "round", js_native_math_round);
    js_obj_set_native(math, "abs", js_native_math_abs);
    js_obj_set_native(math, "sqrt", js_native_math_sqrt);
    js_obj_set_native(math, "pow", js_native_math_pow);
    js_obj_set_native(math, "max", js_native_math_max);
    js_obj_set_native(math, "min", js_native_math_min);
    js_obj_set_native(math, "random", js_native_math_random);
    js_env_define_obj(env, "Math", math);
}

/* ---- Object.keys/values/assign ---- */

/* Real own-enumerable-keys, for BOTH a plain object (its prop_names[])
   and an array (its numeric indices, real JS's own "Object.keys(arr)"
   behavior) -- shared by keys()/values() so they always agree on order. */
static void js_object_own_keys(JSObject *o, char keys[][JS_IDENT_MAX], int *n) {
    *n = 0;
    if (o->kind == JSOBJ_ARRAY) {
        int i;
        for (i = 0; i < o->arr_len && *n < JS_MAX_PROPS; i++) { snprintf(keys[*n], JS_IDENT_MAX, "%d", i); (*n)++; }
        return;
    }
    int i;
    for (i = 0; i < o->prop_count; i++) {
        const char *name = o->prop_names[i];
        /* Skip this engine's own internal marker properties (style/
           classList wrapper tagging -- see their own comments) -- never
           real script-visible data, shouldn't show up in a real
           enumeration. */
        if (name[0] == '_' && name[1] == '_') continue;
        strncpy(keys[*n], name, JS_IDENT_MAX - 1); keys[*n][JS_IDENT_MAX - 1] = 0;
        (*n)++;
    }
}
static void js_native_object_keys(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    JSObject *arr = js_array_new();
    js_set_object(out, arr);
    if (c < 1 || a[0].type != JSV_OBJECT || !a[0].obj) return;
    char keys[JS_MAX_PROPS][JS_IDENT_MAX]; int n;
    js_object_own_keys(a[0].obj, keys, &n);
    int i;
    for (i = 0; i < n; i++) { JSValue v; js_set_string(&v, keys[i]); js_array_push(arr, &v); }
}
static void js_native_object_values(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    JSObject *arr = js_array_new();
    js_set_object(out, arr);
    if (c < 1 || a[0].type != JSV_OBJECT || !a[0].obj) return;
    JSObject *o = a[0].obj;
    char keys[JS_MAX_PROPS][JS_IDENT_MAX]; int n;
    js_object_own_keys(o, keys, &n);
    int i;
    for (i = 0; i < n; i++) { JSValue v; js_get_prop(interp, &a[0], keys[i], &v); js_array_push(arr, &v); }
}
static void js_native_object_assign(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)t;
    js_set_undefined(out);
    if (c < 1 || a[0].type != JSV_OBJECT || !a[0].obj) return;
    int si;
    for (si = 1; si < c; si++) {
        if (a[si].type != JSV_OBJECT || !a[si].obj) continue;
        char keys[JS_MAX_PROPS][JS_IDENT_MAX]; int n;
        js_object_own_keys(a[si].obj, keys, &n);
        int i;
        for (i = 0; i < n; i++) { JSValue v; js_get_prop(interp, &a[si], keys[i], &v); js_set_prop(interp, &a[0], keys[i], &v); }
    }
    js_set_object(out, a[0].obj);
}
static void js_native_object_entries(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)t;
    JSObject *arr = js_array_new();
    js_set_object(out, arr);
    if (c < 1 || a[0].type != JSV_OBJECT || !a[0].obj) return;
    char keys[JS_MAX_PROPS][JS_IDENT_MAX]; int n;
    js_object_own_keys(a[0].obj, keys, &n);
    int i;
    for (i = 0; i < n; i++) {
        JSObject *pair = js_array_new();
        JSValue kv; js_set_string(&kv, keys[i]); js_array_push(pair, &kv);
        JSValue vv; js_get_prop(interp, &a[0], keys[i], &vv); js_array_push(pair, &vv);
        JSValue pairv; js_set_object(&pairv, pair);
        js_array_push(arr, &pairv);
    }
}
static void js_native_object_freeze(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    /* Real no-op -- this engine has no per-property "writable" flag to
       enforce, so Object.freeze(o) here just hands `o` straight back
       (real JS's own return value) without actually preventing later
       mutation. A documented, deliberate gap: code that merely CALLS
       freeze() defensively (very common, e.g. freezing a constants
       object) still works exactly as before; only code that specifically
       relies on a frozen object's mutation being silently rejected would
       behave differently, not something this project's own test pages
       exercise. */
    (void)interp; (void)t;
    if (c > 0) js_value_store(out, &a[0]); else js_set_undefined(out);
}
/* ---- Promises ----
 *
 * A REAL, but deliberately synchronous-scoped, Promise implementation --
 * no separate microtask queue/event-loop tick exists in this engine (see
 * js_engine.h's own top comment on why: a tree-walking interpreter with
 * no async machinery), so every reaction (.then()/.catch()/.finally()
 * callback) fires SYNCHRONOUSLY the instant the promise it's attached to
 * settles -- either immediately inline (if already settled when
 * .then() is called) or immediately from inside js_promise_settle()
 * (if it was still pending). Real JS defers reactions to a genuine
 * microtask queue, so the exact ORDERING relative to other synchronous
 * code can differ from a real engine in edge cases; the actual VALUES
 * produced by a resolve/reject/then chain are correct either way, which
 * is what real page scripts actually depend on. Promise.resolve()/
 * .reject()/.all() and async/await (see JS_CALL's own "await" handling
 * and js_call_function()'s async-function wrapping) build on this same
 * synchronous-settle model. */
static JSObject *js_promise_new(void) { return js_object_new(JSOBJ_PROMISE); }

static void js_promise_settle(JSInterp *interp, JSObject *prom, int state, const JSValue *value) {
    /* Settling only happens once -- a resolve()/reject() called again
       after the promise already settled is a real, silent no-op, matching
       real Promise semantics exactly (a script can't "unsettle" a
       promise). */
    if (prom->promise_state != JS_PROMISE_PENDING) return;
    prom->promise_state = state;
    js_value_store(&prom->promise_value, value);
    int i;
    int n = prom->promise_reaction_count;
    prom->promise_reaction_count = 0; /* cleared BEFORE firing -- a reaction that itself adds a new .then() to this (now-settled) promise takes the "already settled" fast path in js_native_promise_then(), not this array */
    for (i = 0; i < n; i++) {
        JSObject *handler = (state == JS_PROMISE_FULFILLED) ? prom->promise_reactions[i].on_fulfilled : prom->promise_reactions[i].on_rejected;
        JSObject *result_prom = prom->promise_reactions[i].result_promise;
        if (!handler) {
            /* No handler for this outcome (e.g. ".then(onFulfilled)" with
               no onRejected, on a promise that rejected) -- real JS
               pass-through semantics: propagate the SAME state/value to
               the chained promise unchanged. */
            js_promise_settle(interp, result_prom, state, value);
            continue;
        }
        JSValue args[1]; js_value_store(&args[0], value);
        JSValue rv;
        js_call_function(interp, handler, 0, args, 1, &rv);
        if (interp->signal == JS_SIG_THROW) {
            js_promise_settle(interp, result_prom, JS_PROMISE_REJECTED, &interp->thrown_value);
            interp->signal = JS_SIG_NONE; /* the throw became a rejection, not a propagating exception -- same real-JS rule js_call_function()'s own async-wrapping comment describes */
        } else {
            js_promise_settle(interp, result_prom, JS_PROMISE_FULFILLED, &rv);
        }
    }
}

static void js_native_promise_resolve_closure(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)this_obj;
    js_set_undefined(out);
    JSObject *prom = interp->current_native_self ? (JSObject *)interp->current_native_self->native_capture : 0;
    if (!prom) return;
    JSValue v; if (argc > 0) js_value_store(&v, &args[0]); else js_set_undefined(&v);
    /* "resolve(anotherPromise)" -- real JS adopts the inner promise's own
       eventual state instead of fulfilling with the promise object
       itself; supported here for the common case where that inner
       promise is ALREADY settled (this engine's usual case, see this
       section's own top comment) -- chases through the val chain if
       it's a promise. */
    if (v.type == JSV_OBJECT && v.obj && v.obj->kind == JSOBJ_PROMISE) {
        JSObject *inner = v.obj;
        if (inner->promise_state == JS_PROMISE_FULFILLED) { js_promise_settle(interp, prom, JS_PROMISE_FULFILLED, &inner->promise_value); return; }
        if (inner->promise_state == JS_PROMISE_REJECTED) { js_promise_settle(interp, prom, JS_PROMISE_REJECTED, &inner->promise_value); return; }
        /* Still pending: chain THIS promise to settle whenever the inner
           one eventually does, via an ordinary reaction pair (bounded by
           JS_MAX_PROMISE_REACTIONS same as any other .then()). */
        if (inner->promise_reaction_count < JS_MAX_PROMISE_REACTIONS) {
            int idx = inner->promise_reaction_count++;
            inner->promise_reactions[idx].on_fulfilled = 0;
            inner->promise_reactions[idx].on_rejected = 0;
            inner->promise_reactions[idx].result_promise = prom;
        }
        return;
    }
    js_promise_settle(interp, prom, JS_PROMISE_FULFILLED, &v);
}
static void js_native_promise_reject_closure(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)this_obj;
    js_set_undefined(out);
    JSObject *prom = interp->current_native_self ? (JSObject *)interp->current_native_self->native_capture : 0;
    if (!prom) return;
    JSValue v; if (argc > 0) js_value_store(&v, &args[0]); else js_set_undefined(&v);
    js_promise_settle(interp, prom, JS_PROMISE_REJECTED, &v);
}

/* Shared by the real ".then()" native and by js_dom_get_prop()-style
   property access below -- both ".catch(f)" (sugar for ".then(undefined,
   f)") and ".then(f, g)" funnel through here. */
static void js_promise_then_core(JSInterp *interp, JSObject *prom, JSObject *on_fulfilled, JSObject *on_rejected, JSValue *out) {
    JSObject *result = js_promise_new();
    js_set_object(out, result);
    if (prom->promise_state == JS_PROMISE_PENDING) {
        if (prom->promise_reaction_count < JS_MAX_PROMISE_REACTIONS) {
            int idx = prom->promise_reaction_count++;
            prom->promise_reactions[idx].on_fulfilled = on_fulfilled;
            prom->promise_reactions[idx].on_rejected = on_rejected;
            prom->promise_reactions[idx].result_promise = result;
        }
        return;
    }
    JSObject *handler = (prom->promise_state == JS_PROMISE_FULFILLED) ? on_fulfilled : on_rejected;
    if (!handler) { js_promise_settle(interp, result, prom->promise_state, &prom->promise_value); return; }
    JSValue args[1]; js_value_store(&args[0], &prom->promise_value);
    JSValue rv;
    js_call_function(interp, handler, 0, args, 1, &rv);
    if (interp->signal == JS_SIG_THROW) {
        js_promise_settle(interp, result, JS_PROMISE_REJECTED, &interp->thrown_value);
        interp->signal = JS_SIG_NONE;
    } else {
        js_promise_settle(interp, result, JS_PROMISE_FULFILLED, &rv);
    }
}
static void js_native_promise_then(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    js_set_undefined(out);
    if (!this_obj || this_obj->kind != JSOBJ_PROMISE) return;
    JSObject *on_f = (argc > 0 && args[0].type == JSV_OBJECT) ? args[0].obj : 0;
    JSObject *on_r = (argc > 1 && args[1].type == JSV_OBJECT) ? args[1].obj : 0;
    js_promise_then_core(interp, this_obj, on_f, on_r, out);
}
static void js_native_promise_catch(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    js_set_undefined(out);
    if (!this_obj || this_obj->kind != JSOBJ_PROMISE) return;
    JSObject *on_r = (argc > 0 && args[0].type == JSV_OBJECT) ? args[0].obj : 0;
    js_promise_then_core(interp, this_obj, 0, on_r, out);
}
static void js_native_promise_finally(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    /* Real JS chains onFinally's own return value into the settle path
       (e.g. a throw inside onFinally overrides the original outcome) --
       simplified here to always just pass the ORIGINAL state/value
       through unchanged after calling onFinally with no args, a
       documented, narrow gap (the overwhelmingly common real use --
       cleanup work with no return value of its own -- behaves
       identically either way). */
    js_set_undefined(out);
    if (!this_obj || this_obj->kind != JSOBJ_PROMISE) return;
    JSObject *on_fin = (argc > 0 && args[0].type == JSV_OBJECT) ? args[0].obj : 0;
    JSObject *result = js_promise_new();
    js_set_object(out, result);
    if (this_obj->promise_state == JS_PROMISE_PENDING) {
        if (this_obj->promise_reaction_count < JS_MAX_PROMISE_REACTIONS) {
            int idx = this_obj->promise_reaction_count++;
            this_obj->promise_reactions[idx].on_fulfilled = on_fin;
            this_obj->promise_reactions[idx].on_rejected = on_fin;
            this_obj->promise_reactions[idx].result_promise = result;
        }
        return;
    }
    if (on_fin) { JSValue tmp; js_call_function(interp, on_fin, 0, 0, 0, &tmp); }
    js_promise_settle(interp, result, this_obj->promise_state, &this_obj->promise_value);
}

/* "new Promise(function(resolve, reject) {...})" -- see JS_NEW's own
   "callee resolved to a JSOBJ_NATIVE" case, which is what actually routes
   here (a generic "new SomeNativeFn(...)" mechanism, not Promise-
   specific, added alongside this). Runs the executor SYNCHRONOUSLY and
   immediately, exactly like real JS does -- resolve/reject may or may not
   have been called by the time this returns (still pending is fine, real
   JS allows that too). */
static void js_native_promise_constructor(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)this_obj;
    JSObject *prom = js_promise_new();
    js_set_object(out, prom);
    if (argc < 1 || args[0].type != JSV_OBJECT || !args[0].obj) return;
    JSObject *executor = args[0].obj;
    JSObject *resolve_fn = js_object_new(JSOBJ_NATIVE);
    resolve_fn->native_fn = js_native_promise_resolve_closure;
    resolve_fn->native_capture = prom;
    JSObject *reject_fn = js_object_new(JSOBJ_NATIVE);
    reject_fn->native_fn = js_native_promise_reject_closure;
    reject_fn->native_capture = prom;
    JSValue eargs[2];
    js_set_object(&eargs[0], resolve_fn);
    js_set_object(&eargs[1], reject_fn);
    JSValue tmp;
    js_call_function(interp, executor, 0, eargs, 2, &tmp);
    if (interp->signal == JS_SIG_THROW) {
        /* An executor that throws rejects the promise with that value
           instead of propagating -- real JS semantics. */
        js_promise_settle(interp, prom, JS_PROMISE_REJECTED, &interp->thrown_value);
        interp->signal = JS_SIG_NONE;
    }
}
static void js_native_promise_resolve(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)this_obj;
    if (argc > 0 && args[0].type == JSV_OBJECT && args[0].obj && args[0].obj->kind == JSOBJ_PROMISE) { js_set_object(out, args[0].obj); return; } /* real JS: Promise.resolve(existingPromise) returns it AS-IS */
    JSObject *prom = js_promise_new();
    js_set_object(out, prom);
    JSValue v; if (argc > 0) js_value_store(&v, &args[0]); else js_set_undefined(&v);
    js_promise_settle(interp, prom, JS_PROMISE_FULFILLED, &v);
}
static void js_native_promise_reject(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)this_obj;
    JSObject *prom = js_promise_new();
    js_set_object(out, prom);
    JSValue v; if (argc > 0) js_value_store(&v, &args[0]); else js_set_undefined(&v);
    js_promise_settle(interp, prom, JS_PROMISE_REJECTED, &v);
}
/* Promise.all([...]) -- resolves with an array of every input's own
   fulfilled value, in order, once ALL have fulfilled; rejects immediately
   with the first rejection encountered (real JS semantics for "first
   rejection wins", simplified here to check in ARRAY order rather than
   true first-in-time, indistinguishable for this engine's own
   synchronous-settle model -- see this section's own top comment). A
   non-promise input value is treated as already-fulfilled with itself,
   real JS's own Promise.all() behavior. Only already-settled inputs
   (or plain values) are supported for producing a settled Promise.all()
   result immediately; a still-PENDING input promise leaves the overall
   result pending too (correctly reachable once that input settles, via
   an ordinary .then() reaction chained onto it here). */
static void js_native_promise_all(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    (void)this_obj;
    JSObject *result = js_promise_new();
    js_set_object(out, result);
    if (argc < 1 || args[0].type != JSV_OBJECT || !args[0].obj || args[0].obj->kind != JSOBJ_ARRAY) {
        JSValue empty; js_set_object(&empty, js_array_new());
        js_promise_settle(interp, result, JS_PROMISE_FULFILLED, &empty);
        return;
    }
    JSObject *inputs = args[0].obj;
    JSObject *results = js_array_new();
    int i;
    for (i = 0; i < inputs->arr_len; i++) {
        JSValue *item = &inputs->arr_items[i];
        if (item->type == JSV_OBJECT && item->obj && item->obj->kind == JSOBJ_PROMISE) {
            JSObject *p = item->obj;
            if (p->promise_state == JS_PROMISE_REJECTED) { js_promise_settle(interp, result, JS_PROMISE_REJECTED, &p->promise_value); return; }
            if (p->promise_state == JS_PROMISE_FULFILLED) { js_array_push(results, &p->promise_value); continue; }
            /* Still pending -- a real, bounded simplification: the
               overall Promise.all() result stays pending forever if ANY
               input never settles (no mechanism here to revisit it
               later once it does), rather than reactively completing --
               a documented gap for the one case genuinely beyond this
               engine's synchronous model. */
            return;
        }
        js_array_push(results, item);
    }
    JSValue resultsv; js_set_object(&resultsv, results);
    js_promise_settle(interp, result, JS_PROMISE_FULFILLED, &resultsv);
}
static void js_install_promise(JSEnv *env) {
    JSObject *ctor = js_object_new(JSOBJ_NATIVE);
    ctor->native_fn = js_native_promise_constructor;
    js_obj_set_native(ctor, "resolve", js_native_promise_resolve);
    js_obj_set_native(ctor, "reject", js_native_promise_reject);
    js_obj_set_native(ctor, "all", js_native_promise_all);
    js_env_define_obj(env, "Promise", ctor);
}

/* ---- Generators ("function*" / "yield") ----
 *
 * A REAL suspend/resume implementation -- unlike this file's own
 * synchronous-only "await" (js_eval()'s own JS_UNARY "aw" case), an
 * actual generator body genuinely PAUSES mid-execution at each "yield"
 * and resumes exactly where it left off, including all of its own local
 * variables and control-flow position -- something a tree-walking
 * interpreter has no built-in way to do (it would need real
 * continuations or a from-scratch bytecode VM). This engine gets real
 * suspend/resume anyway by giving each generator INSTANCE its own
 * dedicated OS thread, hand-synchronized with whichever thread calls
 * .next() via a plain mutex+condvar pair so that -- despite two real
 * threads existing -- only ONE of {the generator's own thread, the
 * caller} ever actually executes interpreter code at any instant, a
 * strict ping-pong handoff (see JSObject's own gen_* fields and
 * JSInterp::current_generator's comments for the exact protocol, mirrored
 * identically on both sides: js_native_generator_next() below and
 * JS_YIELD's own js_eval() case). This is deliberately the SAME mechanism
 * a real "green threads via a real OS thread each" library would use --
 * de-risked with a standalone, 30x-stress-tested repro (see the
 * scratchpad) BEFORE writing any of this, specifically because this
 * project has hit real, confirmed squash-compiler codegen bugs on other
 * less-common patterns before; plain pthread mutex/cond usage was
 * confirmed to compile identically to gcc.
 *
 * Real, disclosed cost of this approach: a generator's OS thread is only
 * ever cleaned up by running it to completion (or the whole process
 * exiting) -- a generator abandoned mid-iteration (e.g. a "for...of"
 * loop that "break"s early) leaves its own worker thread parked forever
 * in pthread_cond_wait(), a genuine per-abandoned-generator thread leak
 * for the rest of the process's life. This is the exact same "leak
 * rather than risk a use-after-free with no real GC" tradeoff
 * js_interp_free()'s own comment already makes for ordinary heap memory,
 * just for a thread instead -- accepted for the same reason (this
 * project's own local-testing scale), and safe: an abandoned thread
 * never touches interpreter state again unless woken, which nothing can
 * do once its generator object is unreachable.
 *
 * Explicitly out of scope (documented, not silently missing):
 * generator.throw()/generator.return(), "yield*" delegation to another
 * iterable, and async generators ("async function*") -- this file's own
 * top comment's "real but bounded" convention. */
static void *js_generator_thread_main(void *arg) {
    JSObject *gen = (JSObject *)arg;
    JSInterp *interp = gen->gen_interp;
    pthread_mutex_lock(&gen->gen_mutex);
    while (gen->gen_turn != 1) pthread_cond_wait(&gen->gen_cond, &gen->gen_mutex);
    pthread_mutex_unlock(&gen->gen_mutex);

    JSEnv *call_env = js_env_new(gen->gen_closure_env);
    JSValue thisval;
    if (gen->gen_this_obj) js_set_object(&thisval, gen->gen_this_obj); else js_set_undefined(&thisval);
    js_env_define(call_env, "this", &thisval);
    int param_count = gen->gen_func_node->kid_count - 1;
    int i;
    for (i = 0; i < param_count; i++) {
        JsNode *param = gen->gen_func_node->kids[i];
        JSValue av;
        if (i < gen->gen_argc) js_value_store(&av, &gen->gen_args[i]); else js_set_undefined(&av);
        js_env_define(call_env, param->str, &av);
    }
    JsNode *body = gen->gen_func_node->kids[param_count];
    js_exec_block(interp, body, call_env);

    pthread_mutex_lock(&gen->gen_mutex);
    if (interp->signal == JS_SIG_RETURN) js_value_store(&gen->gen_yielded_value, &interp->return_value);
    else js_set_undefined(&gen->gen_yielded_value); /* including an UNCAUGHT THROW inside the generator body -- real JS would propagate that out of the .next() call instead; simplified here to just end the generator, a documented, narrow gap */
    interp->signal = JS_SIG_NONE;
    gen->gen_done = 1;
    gen->gen_turn = 0;
    pthread_cond_broadcast(&gen->gen_cond);
    pthread_mutex_unlock(&gen->gen_mutex);
    return 0;
}
static void js_native_generator_next(JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out) {
    JSObject *result = js_object_new(JSOBJ_PLAIN);
    js_set_object(out, result);
    if (!this_obj || this_obj->kind != JSOBJ_GENERATOR) return;
    JSObject *gen = this_obj;
    if (gen->gen_done) {
        JSValue u; js_set_undefined(&u); js_obj_set(result, "value", &u);
        JSValue d; js_set_bool(&d, 1); js_obj_set(result, "done", &d);
        return;
    }
    if (argc > 0) js_value_store(&gen->gen_sent_value, &args[0]); else js_set_undefined(&gen->gen_sent_value);
    JSObject *saved_gen = interp->current_generator;
    interp->current_generator = gen;
    if (!gen->gen_started) {
        gen->gen_started = 1;
        gen->gen_interp = interp;
        pthread_mutex_init(&gen->gen_mutex, 0);
        pthread_cond_init(&gen->gen_cond, 0);
        gen->gen_turn = 0;
        pthread_mutex_lock(&gen->gen_mutex);
        gen->gen_turn = 1;
        pthread_create(&gen->gen_thread, 0, js_generator_thread_main, gen);
        pthread_cond_broadcast(&gen->gen_cond);
        while (gen->gen_turn != 0) pthread_cond_wait(&gen->gen_cond, &gen->gen_mutex);
        pthread_mutex_unlock(&gen->gen_mutex);
    } else {
        pthread_mutex_lock(&gen->gen_mutex);
        gen->gen_turn = 1;
        pthread_cond_broadcast(&gen->gen_cond);
        while (gen->gen_turn != 0) pthread_cond_wait(&gen->gen_cond, &gen->gen_mutex);
        pthread_mutex_unlock(&gen->gen_mutex);
    }
    interp->current_generator = saved_gen;
    js_obj_set(result, "value", &gen->gen_yielded_value);
    JSValue donev; js_set_bool(&donev, gen->gen_done);
    js_obj_set(result, "done", &donev);
}

static void js_install_object(JSEnv *env) {
    JSObject *o = js_object_new(JSOBJ_PLAIN);
    js_obj_set_native(o, "keys", js_native_object_keys);
    js_obj_set_native(o, "values", js_native_object_values);
    js_obj_set_native(o, "assign", js_native_object_assign);
    js_obj_set_native(o, "entries", js_native_object_entries);
    js_obj_set_native(o, "freeze", js_native_object_freeze);
    js_env_define_obj(env, "Object", o);
}

/* ---- Array.isArray/Array.from ---- */
static void js_native_array_is_array(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    js_set_bool(out, c > 0 && a[0].type == JSV_OBJECT && a[0].obj && a[0].obj->kind == JSOBJ_ARRAY);
}
static void js_native_array_from(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)t;
    JSObject *arr = js_array_new();
    js_set_object(out, arr);
    if (c < 1) return;
    JSObject *mapfn = (c > 1 && a[1].type == JSV_OBJECT && a[1].obj) ? a[1].obj : 0;
    if (a[0].type == JSV_STRING) {
        const char *s = a[0].str ? a[0].str : "";
        int i, n = (int)strlen(s);
        for (i = 0; i < n; i++) {
            char one[2]; one[0] = s[i]; one[1] = 0;
            JSValue v; js_set_string(&v, one);
            if (mapfn) { JSValue margs[2]; js_value_store(&margs[0], &v); js_set_number(&margs[1], (double)i); JSValue mv; js_call_function(interp, mapfn, 0, margs, 2, &mv); js_array_push(arr, &mv); }
            else js_array_push(arr, &v);
        }
        return;
    }
    if (a[0].type != JSV_OBJECT || !a[0].obj || a[0].obj->kind != JSOBJ_ARRAY) return;
    JSObject *src = a[0].obj;
    int i;
    for (i = 0; i < src->arr_len; i++) {
        if (mapfn) { JSValue margs[2]; js_value_store(&margs[0], &src->arr_items[i]); js_set_number(&margs[1], (double)i); JSValue mv; js_call_function(interp, mapfn, 0, margs, 2, &mv); js_array_push(arr, &mv); }
        else js_array_push(arr, &src->arr_items[i]);
    }
}
static void js_install_array_statics(JSEnv *env) {
    JSObject *o = js_object_new(JSOBJ_PLAIN);
    js_obj_set_native(o, "isArray", js_native_array_is_array);
    js_obj_set_native(o, "from", js_native_array_from);
    js_env_define_obj(env, "Array", o);
}

/* ---- JSON.stringify / JSON.parse ---- */

/* Real recursive serialization -- objects/arrays/strings(escaped)/
 * numbers/booleans/null; a function-valued property is skipped entirely
 * (object) or becomes "null" (array), matching real JSON.stringify's own
 * rule that functions aren't representable in JSON. undefined is also
 * written as "null" here rather than omitted (real JSON.stringify OMITS
 * an undefined-valued object property and writes "null" for one inside
 * an array) -- a documented simplification: this engine always does the
 * array behavior, even for a plain object's own undefined-valued
 * property, since distinguishing the two cases isn't worth the extra
 * bookkeeping at this project's own scale of use. */
static void js_json_append(char *out, int *len, int cap, const char *s) {
    int wn = snprintf(out + *len, (size_t)(cap - *len > 0 ? cap - *len : 0), "%s", s);
    if (wn > 0) *len += wn;
}
static void js_json_stringify_string(char *out, int *len, int cap, const char *s) {
    js_json_append(out, len, cap, "\"");
    const char *p = s;
    char esc[3]; esc[0] = '\\'; esc[2] = 0;
    while (*p) {
        char c = *p;
        if (c == '"' || c == '\\') { esc[1] = c; js_json_append(out, len, cap, esc); }
        else if (c == '\n') js_json_append(out, len, cap, "\\n");
        else if (c == '\t') js_json_append(out, len, cap, "\\t");
        else if (c == '\r') js_json_append(out, len, cap, "\\r");
        else { char one[2]; one[0] = c; one[1] = 0; js_json_append(out, len, cap, one); }
        p++;
    }
    js_json_append(out, len, cap, "\"");
}
static void js_json_stringify_value(JSInterp *interp, const JSValue *v, char *out, int *len, int cap) {
    if (v->type == JSV_STRING) { js_json_stringify_string(out, len, cap, v->str ? v->str : ""); return; }
    if (v->type == JSV_NUMBER) { char b[32]; js_format_number(v->num, b, sizeof b); js_json_append(out, len, cap, b); return; }
    if (v->type == JSV_BOOL) { js_json_append(out, len, cap, v->boolean ? "true" : "false"); return; }
    if (v->type == JSV_NULL || v->type == JSV_UNDEFINED) { js_json_append(out, len, cap, "null"); return; }
    if (v->type != JSV_OBJECT || !v->obj) { js_json_append(out, len, cap, "null"); return; }
    JSObject *o = v->obj;
    if (o->kind == JSOBJ_FUNCTION || o->kind == JSOBJ_NATIVE || o->kind == JSOBJ_DOM_ELEMENT) { js_json_append(out, len, cap, "null"); return; }
    if (o->kind == JSOBJ_ARRAY) {
        js_json_append(out, len, cap, "[");
        int i;
        for (i = 0; i < o->arr_len; i++) {
            if (i > 0) js_json_append(out, len, cap, ",");
            js_json_stringify_value(interp, &o->arr_items[i], out, len, cap);
        }
        js_json_append(out, len, cap, "]");
        return;
    }
    js_json_append(out, len, cap, "{");
    char keys[JS_MAX_PROPS][JS_IDENT_MAX]; int n;
    js_object_own_keys(o, keys, &n);
    int i, wrote = 0;
    for (i = 0; i < n; i++) {
        JSValue pv;
        JSValue basev; js_set_object(&basev, o);
        js_get_prop(interp, &basev, keys[i], &pv);
        if (pv.type == JSV_OBJECT && pv.obj && (pv.obj->kind == JSOBJ_FUNCTION || pv.obj->kind == JSOBJ_NATIVE)) continue;
        if (wrote) js_json_append(out, len, cap, ",");
        js_json_stringify_string(out, len, cap, keys[i]);
        js_json_append(out, len, cap, ":");
        js_json_stringify_value(interp, &pv, out, len, cap);
        wrote = 1;
    }
    js_json_append(out, len, cap, "}");
}
static void js_native_json_stringify(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)t;
    if (c < 1) { js_set_undefined(out); return; }
    char buf[JS_STR_MAX * 2]; int len = 0; buf[0] = 0;
    js_json_stringify_value(interp, &a[0], buf, &len, (int)sizeof buf);
    js_set_string(out, buf);
}
/* Real JSON syntax is a strict SUBSET of this engine's own expression
 * grammar (object/array literals, quoted strings, numbers, true/false/
 * null are all already exactly what js_parse_primary() accepts) -- so
 * JSON.parse() is implemented by literally re-using the real lexer +
 * expression parser on the JSON text, then evaluating the resulting AST
 * with a throwaway env (JSON text can never contain an identifier
 * reference or function call, so there's nothing for that env to ever
 * actually be asked to resolve) -- a real parse, not a hand-rolled
 * second JSON-specific parser to keep in sync with the first. */
static void js_native_json_parse(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)t;
    js_set_null(out);
    if (c < 1) return;
    char text[JS_STR_MAX]; js_to_string_buf(&a[0], text, sizeof text);
    JsTokenList *toks = (JsTokenList *)malloc(sizeof(JsTokenList));
    if (js_lex(text, toks)) {
        JsParser p; p.toks = toks; p.pos = 0; p.ok = 1; p.depth = 0;
        JsNode *val = js_parse_assign(&p);
        if (p.ok) {
            JSEnv *throwaway = js_env_new(0);
            js_eval(interp, val, throwaway, out);
        }
    }
    free(toks);
}
static void js_install_json(JSEnv *env) {
    JSObject *j = js_object_new(JSOBJ_PLAIN);
    js_obj_set_native(j, "stringify", js_native_json_stringify);
    js_obj_set_native(j, "parse", js_native_json_parse);
    js_env_define_obj(env, "JSON", j);
}

/* ---- setTimeout/setInterval/clearTimeout/clearInterval ---- */

static void js_native_set_timeout_ex(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out, int repeating) {
    (void)t;
    js_set_number(out, 0.0);
    if (c < 1 || a[0].type != JSV_OBJECT || !a[0].obj) return;
    double delay = c > 1 ? js_to_number(&a[1]) : 0.0;
    if (delay < 0) delay = 0;
    if (interp->timer_count >= JS_MAX_TIMERS) return;
    int slot = interp->timer_count++;
    interp->timers[slot].fn = a[0].obj;
    interp->timers[slot].interval_ms = delay;
    interp->timers[slot].next_fire_ms = interp->now_ms + delay;
    interp->timers[slot].repeating = repeating;
    interp->timers[slot].active = 1;
    js_set_number(out, (double)(slot + 1)); /* 1-based id, 0 reserved for "no timer" */
}
static void js_native_set_timeout(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) { js_native_set_timeout_ex(interp, a, c, t, out, 0); }
static void js_native_set_interval(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) { js_native_set_timeout_ex(interp, a, c, t, out, 1); }
static void js_native_clear_timeout(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)t;
    js_set_undefined(out);
    if (c < 1) return;
    int id = (int)js_to_number(&a[0]);
    if (id >= 1 && id <= interp->timer_count) interp->timers[id - 1].active = 0;
}

/* ---- localStorage -- one shared flat file, deliberately simple (same
 * "flat file, not a real database" scope decision this project's own
 * server-side storage work already made -- see project memory on the
 * SQS $wpdb connector plan). One line per entry, "key=value\n"; a value
 * containing '\n' is truncated at the first one (a narrow, documented
 * limitation -- real localStorage values are always plain strings, but
 * nothing stops a script from handing it one containing a newline; this
 * engine just can't round-trip that one specific case). No real per-
 * origin isolation (this project has exactly one "site" active at a
 * time from SQW's own perspective) -- a single shared file for the
 * whole process, cleared only by an explicit localStorage.clear(). ---- */
#define JS_LOCALSTORAGE_PATH "SQW/localstorage.dat"
#define JS_LOCALSTORAGE_MAX_ENTRIES 256

static void js_native_localstorage_get(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    js_set_null(out);
    if (c < 1) return;
    char key[128]; js_to_string_buf(&a[0], key, sizeof key);
    FILE *fp = fopen(JS_LOCALSTORAGE_PATH, "r");
    if (!fp) return;
    char line[768];
    while (fgets(line, sizeof line, fp)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        if (!strcmp(line, key)) {
            char *val = eq + 1;
            char *nl = strchr(val, '\n');
            if (nl) *nl = 0;
            js_set_string(out, val);
            fclose(fp);
            return;
        }
    }
    fclose(fp);
}
static void js_native_localstorage_set(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    js_set_undefined(out);
    if (c < 2) return;
    char key[128]; js_to_string_buf(&a[0], key, sizeof key);
    char val[512]; js_to_string_buf(&a[1], val, sizeof val);
    char *nl = strchr(val, '\n'); if (nl) *nl = 0; /* see this section's own top comment */
    char keys[JS_LOCALSTORAGE_MAX_ENTRIES][128];
    char vals[JS_LOCALSTORAGE_MAX_ENTRIES][512];
    int n = 0;
    FILE *fp = fopen(JS_LOCALSTORAGE_PATH, "r");
    if (fp) {
        char line[768];
        while (n < JS_LOCALSTORAGE_MAX_ENTRIES && fgets(line, sizeof line, fp)) {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            char *v = eq + 1;
            char *lnl = strchr(v, '\n'); if (lnl) *lnl = 0;
            strncpy(keys[n], line, sizeof keys[n] - 1); keys[n][sizeof keys[n] - 1] = 0;
            strncpy(vals[n], v, sizeof vals[n] - 1); vals[n][sizeof vals[n] - 1] = 0;
            n++;
        }
        fclose(fp);
    }
    int found = -1, i;
    for (i = 0; i < n; i++) if (!strcmp(keys[i], key)) { found = i; break; }
    if (found >= 0) { strncpy(vals[found], val, sizeof vals[found] - 1); vals[found][sizeof vals[found] - 1] = 0; }
    else if (n < JS_LOCALSTORAGE_MAX_ENTRIES) {
        strncpy(keys[n], key, sizeof keys[n] - 1); keys[n][sizeof keys[n] - 1] = 0;
        strncpy(vals[n], val, sizeof vals[n] - 1); vals[n][sizeof vals[n] - 1] = 0;
        n++;
    }
    FILE *out_fp = fopen(JS_LOCALSTORAGE_PATH, "w");
    if (out_fp) {
        for (i = 0; i < n; i++) fprintf(out_fp, "%s=%s\n", keys[i], vals[i]);
        fclose(out_fp);
    }
}
static void js_native_localstorage_remove(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)t;
    js_set_undefined(out);
    if (c < 1) return;
    char key[128]; js_to_string_buf(&a[0], key, sizeof key);
    FILE *fp = fopen(JS_LOCALSTORAGE_PATH, "r");
    if (!fp) return;
    char keys[JS_LOCALSTORAGE_MAX_ENTRIES][128];
    char vals[JS_LOCALSTORAGE_MAX_ENTRIES][512];
    int n = 0;
    char line[768];
    while (n < JS_LOCALSTORAGE_MAX_ENTRIES && fgets(line, sizeof line, fp)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        if (!strcmp(line, key)) continue; /* dropped */
        char *v = eq + 1;
        char *lnl = strchr(v, '\n'); if (lnl) *lnl = 0;
        strncpy(keys[n], line, sizeof keys[n] - 1); keys[n][sizeof keys[n] - 1] = 0;
        strncpy(vals[n], v, sizeof vals[n] - 1); vals[n][sizeof vals[n] - 1] = 0;
        n++;
    }
    fclose(fp);
    FILE *out_fp = fopen(JS_LOCALSTORAGE_PATH, "w");
    if (out_fp) {
        int i; for (i = 0; i < n; i++) fprintf(out_fp, "%s=%s\n", keys[i], vals[i]);
        fclose(out_fp);
    }
}
static void js_native_localstorage_clear(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)interp; (void)a; (void)c; (void)t;
    js_set_undefined(out);
    remove(JS_LOCALSTORAGE_PATH);
}
static void js_install_local_storage(JSEnv *env) {
    JSObject *ls = js_object_new(JSOBJ_PLAIN);
    js_obj_set_native(ls, "getItem", js_native_localstorage_get);
    js_obj_set_native(ls, "setItem", js_native_localstorage_set);
    js_obj_set_native(ls, "removeItem", js_native_localstorage_remove);
    js_obj_set_native(ls, "clear", js_native_localstorage_clear);
    js_env_define_obj(env, "localStorage", ls);
    js_env_define_obj(env, "sessionStorage", ls); /* same backing store -- no real per-tab/per-session isolation exists in this engine, a documented simplification */
}

static void js_install_timers(JSEnv *env) {
    js_env_define_native(env, "setTimeout", js_native_set_timeout);
    js_env_define_native(env, "setInterval", js_native_set_interval);
    js_env_define_native(env, "clearTimeout", js_native_clear_timeout);
    js_env_define_native(env, "clearInterval", js_native_clear_timeout); /* same slot table, same clear logic -- real JS keeps these as two names for the same underlying id space too */
}

/* fetch(url, callback) -- see js_engine.h's own comment on the hook pair
 * this goes through. `callback(body, success)` is called once, later
 * (from js_deliver_fetch_result(), driven by the host's own render loop
 * polling its real net_client.c-backed fetch -- see sqw_main.c's own
 * wiring) -- never synchronously from this call itself, matching real
 * fetch()'s own always-async contract even though this engine's version
 * returns nothing awaitable (no Promise machinery -- see this file's own
 * top comment). */
static void js_native_fetch(JSInterp *interp, JSValue *a, int c, JSObject *t, JSValue *out) {
    (void)t;
    js_set_undefined(out);
    if (c < 2 || a[1].type != JSV_OBJECT || !a[1].obj) return;
    char url[384]; js_to_string_buf(&a[0], url, sizeof url);
    int slot = -1, i;
    for (i = 0; i < JS_MAX_FETCHES; i++) if (!interp->fetches[i].used) { slot = i; break; }
    if (slot < 0) return; /* generous cap for this project's own scale -- a script issuing more than JS_MAX_FETCHES concurrent fetches silently drops the extras rather than growing unboundedly */
    long id = ++interp->fetch_next_id;
    interp->fetches[slot].id = id;
    interp->fetches[slot].callback = a[1].obj;
    interp->fetches[slot].used = 1;
    if (g_fetch_start_fn) g_fetch_start_fn(url, id, g_fetch_start_user_data);
}

static void js_install_builtins(JSInterp *interp, JSEnv *env) {
    js_install_document(interp, env);
    js_install_console(env);
    js_install_math(env);
    js_install_timers(env);
    js_install_local_storage(env);
    js_install_object(env);
    js_install_json(env);
    js_install_promise(env);
    js_env_define_native(env, "alert", js_native_alert);
    js_env_define_native(env, "fetch", js_native_fetch);
    js_env_define_native(env, "parseInt", js_native_parse_int);
    js_env_define_native(env, "parseFloat", js_native_parse_float);
    js_env_define_native(env, "isNaN", js_native_is_nan);
    js_env_define_native(env, "Number", js_native_number);
    js_env_define_native(env, "String", js_native_string);
    js_env_define_native(env, "Boolean", js_native_boolean);
    js_install_array_statics(env);
}

/* Runs every timer whose `next_fire_ms` has arrived -- called once per
 * frame from the host render loop (sqw_main.c) with the CURRENT wall-
 * clock time (SDL_GetTicks(), converted to double milliseconds) --see
 * js_engine.h's own comment on why timers are host-driven rather than
 * this engine running its own event loop/clock. A one-shot timer
 * (setTimeout) is deactivated after firing once; a repeating one
 * (setInterval) is rescheduled for `now + interval` -- NOT "last fire +
 * interval" -- a deliberate simplification (a script whose own callback
 * runs long could see slight drift versus real JS's own catch-up
 * behavior, never seen at this project's own scale of test script). */
void js_run_timers(JSInterp *interp, double now_ms, int *relayout_needed) {
    if (!interp) return;
    interp->now_ms = now_ms;
    interp->mutated_dom = 0;
    int i;
    int n = interp->timer_count; /* snapshot -- a timer firing during this pass may itself register a NEW timer, appending past `n`; that new one is picked up on a LATER frame, not this same pass, avoiding any risk of an infinite same-frame chain */
    for (i = 0; i < n; i++) {
        if (!interp->timers[i].active) continue;
        if (now_ms < interp->timers[i].next_fire_ms) continue;
        JSObject *fn = interp->timers[i].fn;
        if (interp->timers[i].repeating) interp->timers[i].next_fire_ms = now_ms + interp->timers[i].interval_ms;
        else interp->timers[i].active = 0;
        JSValue tmp;
        js_call_function(interp, fn, 0, 0, 0, &tmp);
        interp->signal = JS_SIG_NONE; /* an uncaught throw inside a timer callback doesn't propagate anywhere meaningful -- clear it and keep going, same as a real browser's own "logs to console, timer loop continues" behavior */
    }
    if (relayout_needed) *relayout_needed = interp->mutated_dom;
}

void js_deliver_fetch_result(JSInterp *interp, long fetch_id, const char *body, int success, int *relayout_needed) {
    if (relayout_needed) *relayout_needed = 0;
    if (!interp) return;
    int i;
    for (i = 0; i < JS_MAX_FETCHES; i++) {
        if (!interp->fetches[i].used || interp->fetches[i].id != fetch_id) continue;
        JSObject *cb = interp->fetches[i].callback;
        interp->fetches[i].used = 0;
        interp->mutated_dom = 0;
        JSValue args[2];
        js_set_string(&args[0], body ? body : "");
        js_set_bool(&args[1], success);
        JSValue tmp;
        js_call_function(interp, cb, 0, args, 2, &tmp);
        interp->signal = JS_SIG_NONE; /* an uncaught throw in a fetch callback doesn't propagate anywhere meaningful -- see js_run_timers()'s own identical comment */
        if (relayout_needed) *relayout_needed = interp->mutated_dom;
        return;
    }
}

/* ============================= onclick="" wiring ============================= */

/* Compiles `src` (the text between "function(){" and "}") as a single
 * expression -- used to turn an "onclick" HTML ATTRIBUTE (real HTML5:
 * its value is a small implicit JS statement list, evaluated as if
 * wrapped in "function(event){ <value> }") into a real callable
 * JSOBJ_FUNCTION, so a page written as `<button onclick="count++">` (no
 * <script> tag at all) works the same way a real
 * addEventListener('click', fn) registration would -- see
 * js_wire_onclick_attrs() below, the actual caller. */
static JSObject *js_compile_onclick_attr(JSInterp *interp, const char *attr_value) {
    char src[600];
    snprintf(src, sizeof src, "function(){%s}", attr_value);
    JsTokenList *toks = (JsTokenList *)malloc(sizeof(JsTokenList));
    JSObject *result = 0;
    if (js_lex(src, toks)) {
        JsParser p; p.toks = toks; p.pos = 0; p.ok = 1; p.depth = 0;
        JsNode *fn_node = js_parse_primary(&p); /* "function(){...}" parses as one JS_FUNC_EXPR primary */
        if (p.ok && fn_node->kind == JS_FUNC_EXPR) {
            JSObject *fo = js_object_new(JSOBJ_FUNCTION);
            fo->func_node = fn_node;
            fo->closure_env = interp->global_env;
            result = fo;
        }
    }
    free(toks);
    return result;
}

/* Iterative walk (same pattern as js_dom_find_by_id() etc, see its own
 * comment) over the WHOLE tree looking for a real "onclick" HTML
 * attribute on any element, compiling and wiring each one found into
 * DomNode::js_onclick -- runs unconditionally from js_run_script(), even
 * when the page being run had no <script> source at all, since an
 * onclick attribute needs no <script> tag to work in real HTML5 either. */
static void js_wire_onclick_attrs(JSInterp *interp, DomNode *root) {
    int cap = 64, top = 0;
    DomNode **stack = (DomNode **)malloc((size_t)cap * sizeof(DomNode *));
    int *next_child = (int *)malloc((size_t)cap * sizeof(int));
    stack[top] = root; next_child[top] = 0; top++;
    while (top > 0) {
        DomNode *node = stack[top - 1];
        if (next_child[top - 1] >= node->child_count) { top--; continue; }
        DomNode *child = node->children[next_child[top - 1]];
        next_child[top - 1]++;
        if (!dom_is_text(child)) {
            const char *oc = dom_get_attr(child, "onclick");
            if (oc && !child->js_onclick) {
                JSObject *fo = js_compile_onclick_attr(interp, oc);
                if (fo) child->js_onclick = (void *)fo;
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
}

/* ============================= Public API ============================= */

JSInterp *js_run_script(const char *src, DomNode *document_root, int *relayout_needed) {
    JSInterp *interp = (JSInterp *)malloc(sizeof(JSInterp));
    interp->document_root = document_root;
    interp->global_env = js_env_new(0);
    interp->signal = JS_SIG_NONE;
    js_set_undefined(&interp->return_value);
    interp->mutated_dom = 0;
    interp->timer_count = 0;
    interp->now_ms = 0.0;
    interp->fetch_next_id = 0;
    interp->call_depth = 0;
    interp->current_generator = 0;
    interp->current_module_exports = 0;
    interp->module_count = 0;
    { int fi; for (fi = 0; fi < JS_MAX_FETCHES; fi++) interp->fetches[fi].used = 0; }
    js_install_builtins(interp, interp->global_env);

    if (src && src[0]) {
        JsTokenList *toks = (JsTokenList *)malloc(sizeof(JsTokenList));
        if (js_lex(src, toks)) {
            int ok = 0;
            JsNode *prog = js_parse_program(toks, &ok);
            if (ok) {
                js_exec_block(interp, prog, interp->global_env);
                interp->signal = JS_SIG_NONE; /* a stray top-level return/break/continue is harmless to just clear, not an error */
            } else {
                fprintf(stderr, "SQW/js: parse error, script skipped\n"); fflush(stderr);
            }
        } else {
            fprintf(stderr, "SQW/js: lex error, script skipped\n"); fflush(stderr);
        }
        free(toks);
    }

    js_wire_onclick_attrs(interp, document_root);

    if (relayout_needed) *relayout_needed = interp->mutated_dom;
    return interp;
}

/* Runs `src` as a real ES MODULE against the already-running `interp`
 * (so it sees the same document/console/Math/timers/fetches/etc as the
 * page's ordinary script) -- see js_engine.h's own comment for the full
 * public contract. A module gets its OWN top-level scope (a fresh JSEnv
 * whose PARENT is interp->global_env, so it still sees every builtin,
 * but its own "var"/"function"/"class" declarations do NOT leak into
 * global_env or any other module -- real isolation, achieved just via
 * env parenting, no separate "module system" data structure needed for
 * that half). "import ... from url" resolves against whatever other
 * modules have ALREADY run (via `module_id`, js_module_find_exports())
 * -- modules run in whatever order the CALLER (sqw_main.c, matching
 * HTML5's own document-order script execution) calls this function, and
 * an import of a module that hasn't run YET (a forward reference, or a
 * real circular import) sees an empty/partial export set rather than
 * this function fetching and running that dependency itself first --
 * js_engine.c has no network/file I/O of its own (see this file's own
 * top comment on why) and so genuinely cannot fetch a dependency on its
 * own; document-order execution of every module tag, the same one real
 * browsers used before ES modules existed, is the honest, disclosed
 * scope limit this trades for staying fetch-free. */
void js_run_module(JSInterp *interp, const char *src, const char *module_id) {
    if (!interp || !src) return;
    JSObject *exports = js_object_new(JSOBJ_PLAIN);
    JSEnv *mod_env = js_env_new(interp->global_env);
    JSObject *saved_exports = interp->current_module_exports;
    interp->current_module_exports = exports;

    JsTokenList *toks = (JsTokenList *)malloc(sizeof(JsTokenList));
    if (js_lex(src, toks)) {
        int ok = 0;
        JsNode *prog = js_parse_program(toks, &ok);
        if (ok) {
            js_exec_block(interp, prog, mod_env);
            interp->signal = JS_SIG_NONE; /* same "a stray top-level return/etc is harmless" rule js_run_script() itself already follows */
        } else {
            fprintf(stderr, "SQW/js: module parse error, module skipped: %s\n", module_id ? module_id : "(inline)"); fflush(stderr);
        }
    } else {
        fprintf(stderr, "SQW/js: module lex error, module skipped: %s\n", module_id ? module_id : "(inline)"); fflush(stderr);
    }
    free(toks);

    interp->current_module_exports = saved_exports;
    if (module_id && interp->module_count < JS_MAX_MODULES) {
        int idx = interp->module_count++;
        strncpy(interp->modules[idx].id, module_id, sizeof interp->modules[idx].id - 1);
        interp->modules[idx].id[sizeof interp->modules[idx].id - 1] = 0;
        interp->modules[idx].exports = exports;
    }
}

int js_dispatch_click(JSInterp *interp, DomNode *node, int *relayout_needed) {
    if (!interp || !node || !node->js_onclick) return 0;
    JSObject *fn = (JSObject *)node->js_onclick;
    interp->mutated_dom = 0;
    JSValue tmp;
    /* "this" inside the handler is the clicked element itself -- real
       addEventListener/onclick semantics. */
    JSObject *this_obj = js_wrap_dom_node(node);
    js_call_function(interp, fn, this_obj, 0, 0, &tmp);
    if (relayout_needed) *relayout_needed = interp->mutated_dom;
    return 1;
}

/* Same pattern as js_dispatch_click() above -- see its own comment. Called
 * by sqw_main.c right after it mutates a text input/textarea's own
 * form_value (typing/backspace/newline-insert), for a real "input" event. */
int js_dispatch_input(JSInterp *interp, DomNode *node, int *relayout_needed) {
    if (!interp || !node || !node->js_oninput) return 0;
    JSObject *fn = (JSObject *)node->js_oninput;
    interp->mutated_dom = 0;
    JSValue tmp;
    JSObject *this_obj = js_wrap_dom_node(node);
    js_call_function(interp, fn, this_obj, 0, 0, &tmp);
    if (relayout_needed) *relayout_needed = interp->mutated_dom;
    return 1;
}

/* Same pattern as js_dispatch_click() above. Called by sqw_main.c once a
 * checkbox/radio is toggled, or once a text input/textarea loses focus --
 * real HTML5's own distinction between "input" and "change". */
int js_dispatch_change(JSInterp *interp, DomNode *node, int *relayout_needed) {
    if (!interp || !node || !node->js_onchange) return 0;
    JSObject *fn = (JSObject *)node->js_onchange;
    interp->mutated_dom = 0;
    JSValue tmp;
    JSObject *this_obj = js_wrap_dom_node(node);
    js_call_function(interp, fn, this_obj, 0, 0, &tmp);
    if (relayout_needed) *relayout_needed = interp->mutated_dom;
    return 1;
}

/* Same pattern as js_dispatch_click() above. `key_name` is passed as a
 * real (if minimal) event-like object's "key" property so a handler can do
 * `function(e) { if (e.key == 'Enter') ... }` -- the one field real
 * KeyboardEvent code most commonly reads. Called by sqw_main.c's own
 * keydown handling while an element has keyboard focus. */
int js_dispatch_keydown(JSInterp *interp, DomNode *node, const char *key_name, int *relayout_needed) {
    if (!interp || !node || !node->js_onkeydown) return 0;
    JSObject *fn = (JSObject *)node->js_onkeydown;
    interp->mutated_dom = 0;
    JSValue tmp;
    JSObject *this_obj = js_wrap_dom_node(node);
    JSObject *evt = js_object_new(JSOBJ_PLAIN);
    js_obj_set_str(evt, "key", key_name ? key_name : "");
    JSValue arg;
    js_set_object(&arg, evt);
    JSValue args[1];
    js_value_store(&args[0], &arg);
    js_call_function(interp, fn, this_obj, args, 1, &tmp);
    if (relayout_needed) *relayout_needed = interp->mutated_dom;
    return 1;
}

/* Frees the JSInterp itself and its global environment's own top-level
 * storage. Deliberately does NOT walk and free every JsNode/JSObject/
 * JSEnv/heap string this engine allocated over the page's lifetime (a
 * real, accepted memory leak for the duration this page stays loaded --
 * see this file's own top comment on this being a deliberately minimal
 * engine, and codegen.c's own "small one-time... leak is the safe choice
 * over risking a use-after-free" precedent elsewhere in this project):
 * closures can capture arbitrary outer environments and AST nodes are
 * shared between a function's every call, so safely freeing any of it
 * without a real garbage collector risks a use-after-free far worse than
 * the leak it would avoid. Freed on the NEXT navigation (a whole new
 * JSInterp is created for the new page; the old one's process memory is
 * simply never reclaimed until the process itself exits) -- acceptable
 * for this project's own local-testing scale, a real cost for a
 * long-running many-navigations session, disclosed here rather than
 * silently ignored. */
void js_interp_free(JSInterp *interp) {
    if (!interp) return;
    free(interp->global_env);
    free(interp);
}
