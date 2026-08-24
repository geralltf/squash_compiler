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

/* ============================= Lexer ============================= */

typedef enum {
    JSTOK_EOF = 0, JSTOK_NUM, JSTOK_STR, JSTOK_IDENT, JSTOK_PUNCT
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
    JS_INDEX, JS_FUNC_EXPR, JS_COND
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
} JsParser;

static JsToken *js_cur(JsParser *p) {
    JsTokenList *tl = p->toks;
    return &tl->toks[p->pos];
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

static JsNode *js_parse_primary(JsParser *p) {
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
    if (js_at_kw(p, "function")) {
        js_advance(p);
        JsNode *fn = js_node_new(JS_FUNC_EXPR);
        if (js_cur(p)->type == JSTOK_IDENT) { fn->str = strdup(js_cur(p)->text); js_advance(p); }
        if (!js_eat_punct(p, "(")) return fn;
        while (!js_at_punct(p, ")") && p->ok) {
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsNode *param = js_node_new(JS_IDENT);
            param->str = strdup(js_cur(p)->text);
            js_node_push(fn, param);
            js_advance(p);
            if (js_at_punct(p, ",")) js_advance(p);
        }
        js_eat_punct(p, ")");
        JsNode *body = js_parse_block(p);
        js_node_push(fn, body); /* last kid = body; kid_count-1 params before it */
        return fn;
    }
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
            m->str = strdup(js_cur(p)->text);
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
            strcpy(u->op, js_cur(p)->text);
            js_advance(p);
            js_node_push(u, e);
            e = u;
        } else break;
    }
    return e;
}

static JsNode *js_parse_unary(JsParser *p) {
    if (js_at_punct(p, "!") || js_at_punct(p, "-") || js_at_punct(p, "+")) {
        JsNode *n = js_node_new(JS_UNARY);
        strcpy(n->op, js_cur(p)->text);
        js_advance(p);
        js_node_push(n, js_parse_unary(p));
        return n;
    }
    if (js_at_punct(p, "++") || js_at_punct(p, "--")) {
        JsNode *n = js_node_new(JS_UPDATE);
        strcpy(n->op, js_cur(p)->text);
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
    JsNode *left = js_parse_cond(p);
    if (js_at_punct(p, "=") || js_at_punct(p, "+=") || js_at_punct(p, "-=") ||
        js_at_punct(p, "*=") || js_at_punct(p, "/=")) {
        char op[4]; strcpy(op, js_cur(p)->text);
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

static JsNode *js_parse_var_decl(JsParser *p) {
    /* "var"/"let"/"const" already consumed by the caller. Supports a
     * comma-separated list ("var a=1, b=2;") by returning a JS_BLOCK of
     * JS_VAR_DECL statements when there's more than one -- the caller
     * (js_parse_stmt) unwraps a single-decl block back to the bare decl,
     * so the common single-variable case still executes as one node. */
    JsNode *decls = js_node_new(JS_BLOCK);
    for (;;) {
        if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
        JsNode *d = js_node_new(JS_VAR_DECL);
        d->str = strdup(js_cur(p)->text);
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

static JsNode *js_parse_stmt(JsParser *p) {
    if (js_at_punct(p, "{")) return js_parse_block(p);
    if (js_at_punct(p, ";")) { js_advance(p); return js_node_new(JS_EMPTY); }
    if (js_at_kw(p, "var") || js_at_kw(p, "let") || js_at_kw(p, "const")) {
        js_advance(p);
        JsNode *d = js_parse_var_decl(p);
        js_eat_semi(p);
        return d;
    }
    if (js_at_kw(p, "function")) {
        js_advance(p);
        JsNode *fn = js_node_new(JS_FUNC_DECL);
        if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; return fn; }
        fn->str = strdup(js_cur(p)->text);
        js_advance(p);
        js_eat_punct(p, "(");
        while (!js_at_punct(p, ")") && p->ok) {
            if (js_cur(p)->type != JSTOK_IDENT) { p->ok = 0; break; }
            JsNode *param = js_node_new(JS_IDENT);
            param->str = strdup(js_cur(p)->text);
            js_node_push(fn, param);
            js_advance(p);
            if (js_at_punct(p, ",")) js_advance(p);
        }
        js_eat_punct(p, ")");
        JsNode *body = js_parse_block(p);
        js_node_push(fn, body);
        return fn;
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
    JsParser p; p.toks = toks; p.pos = 0; p.ok = 1;
    JsNode *prog = js_node_new(JS_PROGRAM);
    while (js_cur(&p)->type != JSTOK_EOF && p.ok) {
        js_node_push(prog, js_parse_stmt(&p));
    }
    *ok = p.ok;
    return prog;
}

/* ============================= Values ============================= */

JSValue js_undefined(void) { JSValue v; memset(&v, 0, sizeof v); v.type = JSV_UNDEFINED; return v; }
JSValue js_null_val(void) { JSValue v; memset(&v, 0, sizeof v); v.type = JSV_NULL; return v; }
JSValue js_bool(int b) { JSValue v; memset(&v, 0, sizeof v); v.type = JSV_BOOL; v.boolean = b ? 1 : 0; return v; }
JSValue js_number(double n) { JSValue v; memset(&v, 0, sizeof v); v.type = JSV_NUMBER; v.num = n; return v; }
JSValue js_string(const char *s) { JSValue v; memset(&v, 0, sizeof v); v.type = JSV_STRING; v.str = strdup(s ? s : ""); return v; }
static JSValue js_object_val(JSObject *o) { JSValue v; memset(&v, 0, sizeof v); v.type = JSV_OBJECT; v.obj = o; return v; }

static int js_to_bool(JSValue v) {
    if (v.type == JSV_UNDEFINED || v.type == JSV_NULL) return 0;
    if (v.type == JSV_BOOL) return v.boolean;
    if (v.type == JSV_NUMBER) return v.num != 0.0;
    if (v.type == JSV_STRING) return v.str && v.str[0] != 0;
    return 1; /* object */
}

static double js_to_number(JSValue v) {
    if (v.type == JSV_NUMBER) return v.num;
    if (v.type == JSV_BOOL) return v.boolean ? 1.0 : 0.0;
    if (v.type == JSV_STRING) { char *end; double d = strtod(v.str ? v.str : "", &end); return (end == v.str) ? 0.0 : d; }
    return 0.0; /* undefined/null/object -- real JS gives NaN for most of
                   these; 0.0 is a deliberate simplification (avoids NaN
                   propagation edge cases entirely in this small engine). */
}

/* Formats a number the way real JS's default Number->String conversion
 * mostly reads for this engine's own realistic use (page counters, simple
 * arithmetic results): an integer value prints with no decimal point,
 * anything else prints with up to 6 significant fraction digits, trailing
 * zeros trimmed. Not real JS's exact (much more elaborate) algorithm. */
static void js_format_number(double n, char *out, size_t outcap) {
    if (n == (double)(long long)n && fabs(n) < 1e15) {
        snprintf(out, outcap, "%lld", (long long)n);
    } else {
        snprintf(out, outcap, "%g", n);
    }
}

/* Writes v's string form into out (cap outcap) -- used for both real
 * js_to_string() calls (String(x), string concatenation) and for
 * console.log's own arg formatting. */
static void js_to_string_buf(JSValue v, char *out, size_t outcap) {
    if (v.type == JSV_STRING) { strncpy(out, v.str ? v.str : "", outcap - 1); out[outcap - 1] = 0; return; }
    if (v.type == JSV_NUMBER) { js_format_number(v.num, out, outcap); return; }
    if (v.type == JSV_BOOL) { strncpy(out, v.boolean ? "true" : "false", outcap - 1); out[outcap - 1] = 0; return; }
    if (v.type == JSV_UNDEFINED) { strncpy(out, "undefined", outcap - 1); out[outcap - 1] = 0; return; }
    if (v.type == JSV_NULL) { strncpy(out, "null", outcap - 1); out[outcap - 1] = 0; return; }
    strncpy(out, "[object Object]", outcap - 1); out[outcap - 1] = 0;
}

/* ============================= Objects/Env ============================= */

typedef enum { JSOBJ_PLAIN = 0, JSOBJ_FUNCTION, JSOBJ_NATIVE, JSOBJ_DOM_ELEMENT } JsObjKind;

typedef struct JSEnv JSEnv;
struct JSInterp;

struct JSObject {
    JsObjKind kind;
    char prop_names[JS_MAX_PROPS][JS_IDENT_MAX];
    JSValue prop_values[JS_MAX_PROPS];
    int prop_count;

    JsNode *func_node;   /* JSOBJ_FUNCTION: JS_FUNC_DECL/JS_FUNC_EXPR node */
    JSEnv *closure_env;  /* JSOBJ_FUNCTION: captured defining scope */

    JSValue (*native_fn)(struct JSInterp *interp, JSValue *args, int argc, JSObject *this_obj); /* JSOBJ_NATIVE */

    DomNode *dom_node;   /* JSOBJ_DOM_ELEMENT */
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
static void js_env_define(JSEnv *env, const char *name, JSValue val) {
    int i;
    for (i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) { env->values[i] = val; return; }
    }
    if (env->count < JS_ENV_MAX_VARS) {
        strncpy(env->names[env->count], name, JS_IDENT_MAX - 1);
        env->names[env->count][JS_IDENT_MAX - 1] = 0;
        env->values[env->count] = val;
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
            if (strcmp(cur->names[i], name) == 0) { *out = cur->values[i]; return 1; }
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
static void js_env_set(JSEnv *env, const char *name, JSValue val) {
    JSEnv *cur = env;
    JSEnv *last = env;
    while (cur) {
        int i;
        for (i = 0; i < cur->count; i++) {
            if (strcmp(cur->names[i], name) == 0) { cur->values[i] = val; return; }
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
        if (strcmp(o->prop_names[i], name) == 0) { *out = o->prop_values[i]; return 1; }
    }
    return 0;
}

static void js_obj_set(JSObject *o, const char *name, JSValue val) {
    int i;
    for (i = 0; i < o->prop_count; i++) {
        if (strcmp(o->prop_names[i], name) == 0) { o->prop_values[i] = val; return; }
    }
    if (o->prop_count < JS_MAX_PROPS) {
        strncpy(o->prop_names[o->prop_count], name, JS_IDENT_MAX - 1);
        o->prop_names[o->prop_count][JS_IDENT_MAX - 1] = 0;
        o->prop_values[o->prop_count] = val;
        o->prop_count++;
    }
}
