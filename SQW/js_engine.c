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

static JsNode *js_parse_unary(JsParser *p) {
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
static void js_to_string_buf(const JSValue *v, char *out, size_t outcap) {
    if (v->type == JSV_STRING) { strncpy(out, v->str ? v->str : "", outcap - 1); out[outcap - 1] = 0; return; }
    if (v->type == JSV_NUMBER) { js_format_number(v->num, out, outcap); return; }
    if (v->type == JSV_BOOL) { strncpy(out, v->boolean ? "true" : "false", outcap - 1); out[outcap - 1] = 0; return; }
    if (v->type == JSV_UNDEFINED) { strncpy(out, "undefined", outcap - 1); out[outcap - 1] = 0; return; }
    if (v->type == JSV_NULL) { strncpy(out, "null", outcap - 1); out[outcap - 1] = 0; return; }
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

    void (*native_fn)(struct JSInterp *interp, JSValue *args, int argc, JSObject *this_obj, JSValue *out); /* JSOBJ_NATIVE */

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
    /* Tree-walking control flow: no real exceptions, so return/break/
       continue are signaled by setting these and having every statement-
       executing loop check them after each statement, same convention
       as an ordinary interpreter-without-setjmp. */
    int signal; /* 0=none, 1=return, 2=break, 3=continue */
    JSValue return_value;
    int mutated_dom; /* set by any DOM-mutating builtin -- see js_engine.h's own comment on relayout_needed */
};
#define JS_SIG_NONE 0
#define JS_SIG_RETURN 1
#define JS_SIG_BREAK 2
#define JS_SIG_CONTINUE 3

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
static void js_call_function(JSInterp *interp, JSObject *fn, JSValue *args, int argc, JSValue *out) {
    if (fn->kind == JSOBJ_NATIVE) {
        void (*nf)(JSInterp *, JSValue *, int, JSObject *, JSValue *) = fn->native_fn;
        nf(interp, args, argc, fn, out);
        return;
    }
    if (fn->kind != JSOBJ_FUNCTION) { js_set_undefined(out); return; }

    JsNode *func_node = fn->func_node;
    JSEnv *closure = fn->closure_env;
    JSEnv *call_env = js_env_new(closure);
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
    interp->signal = JS_SIG_NONE;
}

/* ---- property get/set: plain objects, DOM elements, DOM style ---- */

static void js_dom_get_prop(JSInterp *interp, JSObject *obj, const char *name, JSValue *out);
static void js_dom_set_prop(JSInterp *interp, JSObject *obj, const char *name, const JSValue *val);
static void js_style_get_prop(JSObject *obj, const char *name, JSValue *out);
static void js_style_set_prop(JSObject *obj, const char *name, const JSValue *val);

static void js_get_prop(JSInterp *interp, const JSValue *base, const char *name, JSValue *out) {
    if (base->type != JSV_OBJECT || !base->obj) { js_set_undefined(out); return; }
    JSObject *o = base->obj;
    if (o->kind == JSOBJ_DOM_ELEMENT) { js_dom_get_prop(interp, o, name, out); return; }
    /* JSOBJ_PLAIN's own dedicated marker property "__style_of__" flags a
       style-wrapper object (see js_dom_get_prop()'s own "style" case) --
       checked before the generic property lookup below so a style
       property name never collides with a real object property. */
    JSValue marker;
    if (js_obj_get(o, "__style_of__", &marker) && marker.type == JSV_OBJECT) {
        js_style_get_prop(o, name, out);
        return;
    }
    if (js_obj_get(o, name, out)) return;
    js_set_undefined(out);
}

static void js_set_prop(JSInterp *interp, const JSValue *base, const char *name, const JSValue *val) {
    if (base->type != JSV_OBJECT || !base->obj) return;
    JSObject *o = base->obj;
    if (o->kind == JSOBJ_DOM_ELEMENT) { js_dom_set_prop(interp, o, name, val); return; }
    JSValue marker;
    if (js_obj_get(o, "__style_of__", &marker) && marker.type == JSV_OBJECT) {
        js_style_set_prop(o, name, val);
        interp->mutated_dom = 1;
        return;
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
        c->parent = el;
        if (el->child_count >= el->child_cap) {
            el->child_cap = el->child_cap ? el->child_cap * 2 : 4;
            el->children = (DomNode **)realloc(el->children, (size_t)el->child_cap * sizeof(DomNode *));
        }
        el->children[el->child_count++] = c;
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
    char evname[32];
    js_to_string_buf(&args[0], evname, sizeof evname);
    if (strcmp(evname, "click") != 0) return; /* only "click" is wired to anything -- see js_dispatch_click()'s own comment */
    if (args[1].type != JSV_OBJECT || !args[1].obj) return;
    DomNode *n = this_obj->dom_node;
    n->js_onclick = (void *)args[1].obj;
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
    if (!strcmp(name, "getAttribute")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_get_attribute; js_set_object(out, fo); return; }
    if (!strcmp(name, "setAttribute")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_set_attribute; js_set_object(out, fo); return; }
    if (!strcmp(name, "addEventListener")) { JSObject *fo = js_object_new(JSOBJ_NATIVE); fo->native_fn = js_native_add_event_listener; js_set_object(out, fo); return; }
    if (!strcmp(name, "parentNode") || !strcmp(name, "parentElement")) {
        DomNode *p = n->parent;
        if (p) js_set_object(out, js_wrap_dom_node(p)); else js_set_null(out);
        return;
    }
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
        js_set_prop(interp, &base, target->str, val);
        return;
    }
    if (target->kind == JS_INDEX) {
        JsNode *obj_expr = target->kids[0];
        JsNode *idx_expr = target->kids[1];
        JSValue base, idx;
        js_eval(interp, obj_expr, env, &base);
        js_eval(interp, idx_expr, env, &idx);
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
    case JS_MEMBER: {
        JsNode *obj_expr = n->kids[0];
        JSValue base;
        js_eval(interp, obj_expr, env, &base);
        js_get_prop(interp, &base, n->str, out);
        return;
    }
    case JS_INDEX: {
        JsNode *obj_expr = n->kids[0];
        JsNode *idx_expr = n->kids[1];
        JSValue base, idx;
        js_eval(interp, obj_expr, env, &base);
        js_eval(interp, idx_expr, env, &idx);
        char name[64];
        js_to_string_buf(&idx, name, sizeof name);
        js_get_prop(interp, &base, name, out);
        return;
    }
    case JS_CALL: {
        JsNode *callee = n->kids[0];
        JSValue this_val; js_set_undefined(&this_val);
        JSValue fn_val;
        if (callee->kind == JS_MEMBER) {
            JsNode *obj_expr = callee->kids[0];
            js_eval(interp, obj_expr, env, &this_val);
            js_get_prop(interp, &this_val, callee->str, &fn_val);
        } else {
            js_eval(interp, callee, env, &fn_val);
        }
        if (fn_val.type != JSV_OBJECT || !fn_val.obj) { js_set_undefined(out); return; }
        JSValue args[JS_MAX_ARGS];
        int argc = n->kid_count - 1;
        if (argc > JS_MAX_ARGS) argc = JS_MAX_ARGS;
        int i;
        for (i = 0; i < argc; i++) {
            JsNode *argnode = n->kids[i + 1];
            js_eval(interp, argnode, env, &args[i]);
        }
        JSObject *fn_obj = fn_val.obj;
        JSObject *this_obj = (this_val.type == JSV_OBJECT) ? this_val.obj : 0;
        if (fn_obj->kind == JSOBJ_NATIVE) {
            void (*nf)(JSInterp *, JSValue *, int, JSObject *, JSValue *) = fn_obj->native_fn;
            nf(interp, args, argc, this_obj, out);
            return;
        }
        js_call_function(interp, fn_obj, args, argc, out);
        return;
    }
    case JS_UNARY: {
        JsNode *operand = n->kids[0];
        if (!strcmp(n->op, "ty")) {
            JSValue v;
            js_eval(interp, operand, env, &v);
            if (v.type == JSV_UNDEFINED) { js_set_string(out, "undefined"); return; }
            if (v.type == JSV_NUMBER) { js_set_string(out, "number"); return; }
            if (v.type == JSV_STRING) { js_set_string(out, "string"); return; }
            if (v.type == JSV_BOOL) { js_set_string(out, "boolean"); return; }
            if (v.type == JSV_OBJECT && v.obj && (v.obj->kind == JSOBJ_FUNCTION || v.obj->kind == JSOBJ_NATIVE)) { js_set_string(out, "function"); return; }
            js_set_string(out, "object");
            return;
        }
        JSValue v;
        js_eval(interp, operand, env, &v);
        if (!strcmp(n->op, "!")) { js_set_bool(out, !js_to_bool(&v)); return; }
        if (!strcmp(n->op, "-")) { js_set_number(out, -js_to_number(&v)); return; }
        js_set_number(out, js_to_number(&v));
        return;
    }
    case JS_UPDATE: {
        JsNode *target = n->kids[0];
        JSValue cur;
        js_eval(interp, target, env, &cur);
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
        if (!strcmp(n->op, "&&")) { if (js_to_bool(&l)) js_eval(interp, rnode, env, out); else js_value_store(out, &l); return; }
        if (js_to_bool(&l)) js_value_store(out, &l); else js_eval(interp, rnode, env, out);
        return;
    }
    case JS_BINARY: {
        JsNode *lnode = n->kids[0];
        JsNode *rnode = n->kids[1];
        JSValue l, r;
        js_eval(interp, lnode, env, &l);
        js_eval(interp, rnode, env, &r);
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
        if (js_to_bool(&c)) js_eval(interp, then_e, env, out); else js_eval(interp, else_e, env, out);
        return;
    }
    case JS_ASSIGN: {
        JsNode *target = n->kids[0];
        JsNode *rhs = n->kids[1];
        JSValue rv;
        js_eval(interp, rhs, env, &rv);
        if (strcmp(n->op, "=") != 0) {
            JSValue cur;
            js_eval(interp, target, env, &cur);
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
    case JS_BLOCK: {
        JSEnv *inner = js_env_new(env);
        js_exec_block(interp, n, inner);
        return;
    }
    case JS_FUNC_DECL: return; /* already hoisted -- see js_hoist_functions() */
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
               interpreter with no such prompt available. */
            if (++guard > 2000000) { fprintf(stderr, "SQW/js: while loop exceeded iteration cap, aborting script\n"); return; }
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
            if (++guard > 2000000) { fprintf(stderr, "SQW/js: for loop exceeded iteration cap, aborting script\n"); return; }
        }
        return;
    }
    case JS_RETURN: {
        if (n->kid_count > 0) js_eval(interp, n->kids[0], env, &interp->return_value);
        else js_set_undefined(&interp->return_value);
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
static void js_install_document(JSInterp *interp, JSEnv *env) {
    JSObject *document = js_object_new(JSOBJ_PLAIN);
    js_obj_set_native(document, "getElementById", js_native_document_get_by_id);
    js_obj_set_native(document, "getElementsByTagName", js_native_document_get_by_tag);
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

static void js_install_builtins(JSInterp *interp, JSEnv *env) {
    js_install_document(interp, env);
    js_install_console(env);
    js_install_math(env);
    js_env_define_native(env, "alert", js_native_alert);
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
        JsParser p; p.toks = toks; p.pos = 0; p.ok = 1;
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

int js_dispatch_click(JSInterp *interp, DomNode *node, int *relayout_needed) {
    if (!interp || !node || !node->js_onclick) return 0;
    JSObject *fn = (JSObject *)node->js_onclick;
    interp->mutated_dom = 0;
    JSValue tmp;
    js_call_function(interp, fn, 0, 0, &tmp);
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
