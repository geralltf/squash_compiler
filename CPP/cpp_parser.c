#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpp_parser.h"

/* ---- small growable vectors ------------------------------------------- */
typedef struct { CppNode **items; int n, cap; } NodeVec;
static void nv_push(NodeVec *v, CppNode *n) {
    if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 4; v->items = realloc(v->items, sizeof(CppNode*) * v->cap); }
    v->items[v->n++] = n;
}
typedef struct { char **items; int n, cap; } StrVec;
static void sv_push(StrVec *v, const char *s) {
    if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 8; v->items = realloc(v->items, sizeof(char*) * v->cap); }
    v->items[v->n++] = cpp_strdup(s);
}
static int sv_has(StrVec *v, const char *s) {
    for (int i = 0; i < v->n; i++) if (strcmp(v->items[i], s) == 0) return 1;
    return 0;
}

/* ---- parser state ------------------------------------------------------
 * `known_types` accumulates every class/struct name (and template base
 * name) declared so far, seeded with the handful of std:: names
 * cpp_lower.c understands — used to disambiguate "Type ident ..." local
 * declarations from ordinary expression statements without a real
 * symbol table (see try_parse_var_decl's own comment on the backtracking
 * this enables). Tokens are deliberately never freed as the parser
 * advances past them (a one-shot CLI process; the alternative is either a
 * real arena or making every ParserSnapshot deep-copy its lookahead,
 * neither of which is worth it here) — that is what makes snapshot/
 * restore below safe and cheap. */
struct CppParserExtra { StrVec known_types; };
static struct CppParserExtra *extra(CppParser *p) {
    static struct CppParserExtra *singleton = NULL; /* one parser instance per process in practice */
    (void)p;
    if (!singleton) {
        singleton = calloc(1, sizeof(*singleton));
        sv_push(&singleton->known_types, "std::string");
        sv_push(&singleton->known_types, "std::vector");
        sv_push(&singleton->known_types, "string");
    }
    return singleton;
}

typedef struct { CppLexer lx; CppTok *cur; CppTok *ahead; int error_count; } ParserSnapshot;
static ParserSnapshot snap(CppParser *p) { ParserSnapshot s; s.lx = p->lx; s.cur = p->cur; s.ahead = p->ahead; s.error_count = p->error_count; return s; }
static void restore(CppParser *p, ParserSnapshot s) { p->lx = s.lx; p->cur = s.cur; p->ahead = s.ahead; p->error_count = s.error_count; }

void cpp_parser_init(CppParser *p, const char *src) {
    cpp_lexer_init(&p->lx, src);
    p->cur = cpp_lexer_next(&p->lx);
    p->ahead = NULL;
    p->error_count = 0;
    p->suppress_diag = 0;
}
void cpp_parser_free(CppParser *p) { (void)p; }

static void perror_at(CppParser *p, const char *msg) {
    if (p->suppress_diag) return;
    fprintf(stderr, "cpp: line %d: %s (near '%s')\n", p->cur->line, msg, p->cur->text ? p->cur->text : "<eof>");
    p->error_count++;
}

static void advance(CppParser *p) {
    if (p->ahead) { p->cur = p->ahead; p->ahead = NULL; }
    else p->cur = cpp_lexer_next(&p->lx);
}
static CppTok *peek_ahead(CppParser *p) {
    if (!p->ahead) p->ahead = cpp_lexer_next(&p->lx);
    return p->ahead;
}
static int cur_is_kw(CppParser *p, const char *s) { return p->cur->kind == CPP_TOK_KEYWORD && strcmp(p->cur->text, s) == 0; }
static int cur_is_punct(CppParser *p, const char *s) { return p->cur->kind == CPP_TOK_PUNCT && strcmp(p->cur->text, s) == 0; }
static int cur_is_ident(CppParser *p) { return p->cur->kind == CPP_TOK_IDENT; }

static void expect_punct(CppParser *p, const char *s) {
    if (cur_is_punct(p, s)) { advance(p); return; }
    char buf[64]; snprintf(buf, sizeof buf, "expected '%s'", s);
    perror_at(p, buf);
}
static void expect_kw(CppParser *p, const char *s) {
    if (cur_is_kw(p, s)) { advance(p); return; }
    char buf[64]; snprintf(buf, sizeof buf, "expected '%s'", s);
    perror_at(p, buf);
}
static char *expect_ident(CppParser *p) {
    if (!cur_is_ident(p)) { perror_at(p, "expected identifier"); return cpp_strdup("<error>"); }
    char *name = cpp_strdup(p->cur->text);
    advance(p);
    return name;
}

/* Closes one level of a template argument list, splitting a ">>" token
 * into two logical '>' the way real C++ parsers do for "vector<vector<int>>" —
 * see cpp_parser.h's own comment on this being a deliberate, minimal fix
 * for the classic >> ambiguity rather than a full maximal-munch redesign. */
static void expect_template_close(CppParser *p) {
    if (cur_is_punct(p, ">")) { advance(p); return; }
    if (cur_is_punct(p, ">>")) { free(p->cur->text); p->cur->text = cpp_strdup(">"); return; }
    perror_at(p, "expected '>'");
}

static int is_type_kw_word(const char *s) {
    static const char *w[] = { "void", "bool", "char", "int", "short", "long", "float", "double", "unsigned", "signed", "auto", NULL };
    for (int i = 0; w[i]; i++) if (strcmp(w[i], s) == 0) return 1;
    return 0;
}

static CppNode *parse_expr(CppParser *p);
static CppNode *parse_statement(CppParser *p);
static CppNode *parse_block(CppParser *p);
static CppNode *parse_unary(CppParser *p);
static CppType *parse_type_q(CppParser *p, int *ok);

/* Non-fatal type parse — never advances error_count; caller decides what
 * NULL/`*ok==0` means (a real syntax error, or "this wasn't a type after
 * all, try something else" during declaration-vs-expression-statement
 * disambiguation). */
static CppType *parse_type_q(CppParser *p, int *ok) {
    *ok = 1;
    int is_const = 0;
    while (cur_is_kw(p, "const")) { is_const = 1; advance(p); }
    char namebuf[256] = "";
    if (p->cur->kind == CPP_TOK_KEYWORD && is_type_kw_word(p->cur->text)) {
        strcpy(namebuf, p->cur->text); advance(p);
        while (p->cur->kind == CPP_TOK_KEYWORD && is_type_kw_word(p->cur->text)) {
            strcat(namebuf, " "); strcat(namebuf, p->cur->text); advance(p);
        }
    } else if (cur_is_ident(p)) {
        strcpy(namebuf, p->cur->text); advance(p);
        while (cur_is_punct(p, "::")) {
            advance(p);
            if (!cur_is_ident(p) && p->cur->kind != CPP_TOK_KEYWORD) { *ok = 0; return NULL; }
            strcat(namebuf, "::"); strcat(namebuf, p->cur->text); advance(p);
        }
    } else { *ok = 0; return NULL; }

    CppType *t = cpptype_new(namebuf);
    t->is_const = is_const;
    if (cur_is_punct(p, "<")) {
        advance(p);
        NodeVec dummy; (void)dummy;
        CppType **args = NULL; int nargs = 0, cap = 0;
        int sub_ok;
        CppType *first = parse_type_q(p, &sub_ok);
        if (!sub_ok) { *ok = 0; cpptype_free(t); return NULL; }
        if (nargs == cap) { cap = cap ? cap * 2 : 2; args = realloc(args, sizeof(CppType*) * cap); }
        args[nargs++] = first;
        while (cur_is_punct(p, ",")) {
            advance(p);
            CppType *nx = parse_type_q(p, &sub_ok);
            if (!sub_ok) { *ok = 0; cpptype_free(t); return NULL; }
            if (nargs == cap) { cap = cap ? cap * 2 : 2; args = realloc(args, sizeof(CppType*) * cap); }
            args[nargs++] = nx;
        }
        expect_template_close(p);
        t->type_args = args; t->n_type_args = nargs;
    }
    while (cur_is_punct(p, "*")) { t->ptr_depth++; advance(p); }
    if (cur_is_punct(p, "&")) { t->is_ref = 1; advance(p); }
    return t;
}

static CppType *parse_type(CppParser *p) {
    int ok;
    CppType *t = parse_type_q(p, &ok);
    if (!ok) { perror_at(p, "expected type"); return cpptype_new("int"); }
    return t;
}

static int is_known_type_start(CppParser *p) {
    if (cur_is_kw(p, "const")) return 1;
    if (p->cur->kind == CPP_TOK_KEYWORD && is_type_kw_word(p->cur->text)) return 1;
    if (cur_is_ident(p)) {
        if (sv_has(&extra(p)->known_types, p->cur->text)) return 1;
        /* "Name::x" — a qualified name is plausibly a type ("std::vector<T>
         * items;") just as plausibly as an expression ("std::cout << x;");
         * optimistically say yes and let try_parse_var_decl's own
         * snapshot/restore (with diagnostics suppressed — see
         * CppParser.suppress_diag's own comment) sort out which one this
         * actually is, instead of returning 0 here and never even
         * attempting the type-parse path at all (a real, confirmed bug:
         * every "std::vector<T> name;"/"std::string name = ...;" local
         * declaration failed to parse as a declaration at all). */
        if (peek_ahead(p)->kind == CPP_TOK_PUNCT && strcmp(peek_ahead(p)->text, "::") == 0) return 1;
    }
    return 0;
}

/* ============================= EXPRESSIONS ============================= */

static CppNode **parse_arg_list(CppParser *p, int *argc, const char *close) {
    NodeVec v = {0};
    if (!cur_is_punct(p, close)) {
        nv_push(&v, parse_expr(p));
        while (cur_is_punct(p, ",")) { advance(p); nv_push(&v, parse_expr(p)); }
    }
    expect_punct(p, close);
    *argc = v.n;
    return v.items;
}

static CppNode *parse_primary(CppParser *p) {
    int line = p->cur->line;
    if (p->cur->kind == CPP_TOK_INT_LIT) { long long v = p->cur->int_value; advance(p); return cppnode_lit_int(v, line); }
    if (p->cur->kind == CPP_TOK_DOUBLE_LIT) { double v = p->cur->double_value; advance(p); return cppnode_lit_double(v, line); }
    if (p->cur->kind == CPP_TOK_STRING_LIT) { char *v = p->cur->text; advance(p); return cppnode_lit_string(v, line); }
    if (p->cur->kind == CPP_TOK_CHAR_LIT) { long long v = p->cur->int_value; advance(p); return cppnode_lit_char(v, line); }
    if (cur_is_kw(p, "true")) { advance(p); return cppnode_lit_bool(1, line); }
    if (cur_is_kw(p, "false")) { advance(p); return cppnode_lit_bool(0, line); }
    if (cur_is_kw(p, "nullptr") || cur_is_kw(p, "NULL")) { advance(p); return cppnode_lit_nullptr(line); }
    if (cur_is_kw(p, "this")) { advance(p); return cppnode_this(line); }
    if (cur_is_kw(p, "sizeof")) {
        advance(p);
        expect_punct(p, "(");
        int ok; ParserSnapshot s = snap(p);
        CppType *t = parse_type_q(p, &ok);
        if (ok && cur_is_punct(p, ")")) { advance(p); return cppnode_sizeof_type(t, line); }
        restore(p, s);
        if (ok) cpptype_free(t);
        CppNode *e = parse_expr(p);
        expect_punct(p, ")");
        return cppnode_sizeof_expr(e, line);
    }
    if (cur_is_punct(p, "(")) {
        /* Possible C-style cast "(Type)expr" vs grouped "(expr)". */
        ParserSnapshot s = snap(p);
        advance(p);
        int ok;
        CppType *t = parse_type_q(p, &ok);
        if (ok && cur_is_punct(p, ")")) {
            advance(p);
            int next_can_start_unary =
                p->cur->kind == CPP_TOK_INT_LIT || p->cur->kind == CPP_TOK_DOUBLE_LIT ||
                p->cur->kind == CPP_TOK_STRING_LIT || p->cur->kind == CPP_TOK_CHAR_LIT ||
                cur_is_ident(p) || cur_is_punct(p, "(") || cur_is_punct(p, "-") ||
                cur_is_punct(p, "*") || cur_is_punct(p, "&") || cur_is_punct(p, "!") ||
                cur_is_kw(p, "this") || cur_is_kw(p, "true") || cur_is_kw(p, "false");
            if (next_can_start_unary) {
                CppNode *operand = parse_unary(p);
                return cppnode_cast(t, operand, line);
            }
        }
        if (ok) cpptype_free(t);
        restore(p, s);
        advance(p);
        CppNode *e = parse_expr(p);
        expect_punct(p, ")");
        return e;
    }
    if (cur_is_ident(p)) {
        char *name = cpp_strdup(p->cur->text); advance(p);
        if (cur_is_punct(p, "::")) {
            advance(p);
            char *member = expect_ident(p);
            CppNode *n = cppnode_scope(name, member, line);
            free(name);
            return n;
        }
        CppNode *n = cppnode_ident(name, line);
        free(name);
        return n;
    }
    perror_at(p, "expected expression");
    advance(p);
    return cppnode_lit_int(0, line);
}

static CppNode *parse_postfix(CppParser *p) {
    CppNode *n = parse_primary(p);
    for (;;) {
        int line = p->cur->line;
        if (cur_is_punct(p, ".")) { advance(p); char *name = expect_ident(p); n = cppnode_member(n, name, line); free(name); }
        else if (cur_is_punct(p, "->")) { advance(p); char *name = expect_ident(p); n = cppnode_arrow(n, name, line); free(name); }
        else if (cur_is_punct(p, "[")) { advance(p); CppNode *idx = parse_expr(p); expect_punct(p, "]"); n = cppnode_index(n, idx, line); }
        else if (cur_is_punct(p, "(")) { advance(p); int argc; CppNode **args = parse_arg_list(p, &argc, ")"); n = cppnode_call(n, args, argc, line); }
        else if (cur_is_punct(p, "++")) { advance(p); n = cppnode_unary("++", n, 1, line); }
        else if (cur_is_punct(p, "--")) { advance(p); n = cppnode_unary("--", n, 1, line); }
        else break;
    }
    return n;
}

static CppNode *parse_unary(CppParser *p) {
    int line = p->cur->line;
    if (cur_is_punct(p, "!") || cur_is_punct(p, "-") || cur_is_punct(p, "+") ||
        cur_is_punct(p, "*") || cur_is_punct(p, "&") || cur_is_punct(p, "~")) {
        char *op = cpp_strdup(p->cur->text); advance(p);
        CppNode *n = cppnode_unary(op, parse_unary(p), 0, line);
        free(op);
        return n;
    }
    if (cur_is_punct(p, "++") || cur_is_punct(p, "--")) {
        char *op = cpp_strdup(p->cur->text); advance(p);
        CppNode *n = cppnode_unary(op, parse_unary(p), 0, line);
        free(op);
        return n;
    }
    if (cur_is_kw(p, "new")) {
        advance(p);
        int ok; CppType *t = parse_type_q(p, &ok);
        if (!ok) { perror_at(p, "expected type after 'new'"); t = cpptype_new("int"); }
        if (cur_is_punct(p, "[")) {
            advance(p);
            CppNode *sz = parse_expr(p);
            expect_punct(p, "]");
            return cppnode_new_array(t, sz, line);
        }
        if (cur_is_punct(p, "(")) {
            advance(p);
            int argc; CppNode **args = parse_arg_list(p, &argc, ")");
            return cppnode_new(t, args, argc, line);
        }
        return cppnode_new(t, NULL, 0, line);
    }
    if (cur_is_kw(p, "delete")) {
        advance(p);
        if (cur_is_punct(p, "[")) {
            advance(p); expect_punct(p, "]");
            return cppnode_delete_array(parse_unary(p), line);
        }
        return cppnode_delete(parse_unary(p), line);
    }
    return parse_postfix(p);
}

static CppNode *parse_bin(CppParser *p, int min_prec);

static int binop_prec(const char *op) {
    if (!strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%")) return 10;
    if (!strcmp(op, "+") || !strcmp(op, "-")) return 9;
    if (!strcmp(op, "<<") || !strcmp(op, ">>")) return 8;
    if (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) return 7;
    if (!strcmp(op, "==") || !strcmp(op, "!=")) return 6;
    if (!strcmp(op, "&")) return 5;
    if (!strcmp(op, "^")) return 4;
    if (!strcmp(op, "|")) return 3;
    if (!strcmp(op, "&&")) return 2;
    if (!strcmp(op, "||")) return 1;
    return -1;
}

static CppNode *parse_bin(CppParser *p, int min_prec) {
    CppNode *left = parse_unary(p);
    for (;;) {
        if (p->cur->kind != CPP_TOK_PUNCT) break;
        int prec = binop_prec(p->cur->text);
        if (prec < min_prec || prec < 0) break;
        char *op = cpp_strdup(p->cur->text);
        int line = p->cur->line;
        advance(p);
        CppNode *right = parse_bin(p, prec + 1);
        left = cppnode_binary(op, left, right, line);
        free(op);
    }
    return left;
}

static CppNode *parse_ternary(CppParser *p) {
    CppNode *c = parse_bin(p, 0);
    if (cur_is_punct(p, "?")) {
        int line = p->cur->line;
        advance(p);
        CppNode *t = parse_expr(p);
        expect_punct(p, ":");
        CppNode *e = parse_ternary(p);
        return cppnode_ternary(c, t, e, line);
    }
    return c;
}

static int is_assign_op(const char *s) {
    static const char *ops[] = { "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^=", NULL };
    for (int i = 0; ops[i]; i++) if (!strcmp(ops[i], s)) return 1;
    return 0;
}

static CppNode *parse_expr(CppParser *p) {
    CppNode *left = parse_ternary(p);
    if (p->cur->kind == CPP_TOK_PUNCT && is_assign_op(p->cur->text)) {
        char *op = cpp_strdup(p->cur->text);
        int line = p->cur->line;
        advance(p);
        CppNode *right = parse_expr(p);
        CppNode *n = cppnode_assign(op, left, right, line);
        free(op);
        return n;
    }
    return left;
}

/* ============================= STATEMENTS ============================== */

static CppNode *try_parse_var_decl(CppParser *p) {
    ParserSnapshot s = snap(p);
    if (!is_known_type_start(p)) return NULL;
    int ok;
    int line = p->cur->line;
    p->suppress_diag++;
    CppType *t = parse_type_q(p, &ok);
    if (!ok || !cur_is_ident(p)) { p->suppress_diag--; if (ok) cpptype_free(t); restore(p, s); return NULL; }
    char *name = cpp_strdup(p->cur->text);
    advance(p);
    CppNode *init = NULL;
    if (cur_is_punct(p, "=")) { advance(p); init = parse_expr(p); }
    else if (cur_is_punct(p, "(")) {
        advance(p);
        int argc; CppNode **args = parse_arg_list(p, &argc, ")");
        init = cppnode_call(NULL, args, argc, line); /* callee==NULL marks a ctor-style init; lower fills in the type */
    } else if (!cur_is_punct(p, ";")) {
        p->suppress_diag--; cpptype_free(t); free(name); restore(p, s); return NULL;
    }
    if (!cur_is_punct(p, ";")) { p->suppress_diag--; cpptype_free(t); free(name); restore(p, s); return NULL; }
    p->suppress_diag--;
    advance(p);
    CppNode *n = cppnode_var_decl(t, name, init, line);
    free(name);
    return n;
}

static CppNode *parse_statement(CppParser *p) {
    int line = p->cur->line;
    if (cur_is_punct(p, "{")) return parse_block(p);
    if (cur_is_kw(p, "if")) {
        advance(p); expect_punct(p, "(");
        CppNode *cond = parse_expr(p);
        expect_punct(p, ")");
        CppNode *then_ = parse_statement(p);
        CppNode *else_ = NULL;
        if (cur_is_kw(p, "else")) { advance(p); else_ = parse_statement(p); }
        return cppnode_if(cond, then_, else_, line);
    }
    if (cur_is_kw(p, "for")) {
        advance(p); expect_punct(p, "(");
        CppNode *init = NULL;
        if (!cur_is_punct(p, ";")) {
            init = try_parse_var_decl(p);
            if (!init) { CppNode *e = parse_expr(p); expect_punct(p, ";"); init = cppnode_expr_stmt(e, line); }
        } else advance(p);
        CppNode *cond = cur_is_punct(p, ";") ? NULL : parse_expr(p);
        expect_punct(p, ";");
        CppNode *step = cur_is_punct(p, ")") ? NULL : parse_expr(p);
        expect_punct(p, ")");
        CppNode *body = parse_statement(p);
        return cppnode_for(init, cond, step, body, line);
    }
    if (cur_is_kw(p, "while")) {
        advance(p); expect_punct(p, "(");
        CppNode *cond = parse_expr(p);
        expect_punct(p, ")");
        CppNode *body = parse_statement(p);
        return cppnode_while(cond, body, line);
    }
    if (cur_is_kw(p, "do")) {
        advance(p);
        CppNode *body = parse_statement(p);
        expect_kw(p, "while"); expect_punct(p, "(");
        CppNode *cond = parse_expr(p);
        expect_punct(p, ")"); expect_punct(p, ";");
        return cppnode_do_while(body, cond, line);
    }
    if (cur_is_kw(p, "switch")) {
        advance(p); expect_punct(p, "(");
        CppNode *expr = parse_expr(p);
        expect_punct(p, ")"); expect_punct(p, "{");
        NodeVec cases = {0};
        while (!cur_is_punct(p, "}") && p->cur->kind != CPP_TOK_EOF) {
            int cline = p->cur->line;
            CppNode *value = NULL;
            if (cur_is_kw(p, "case")) { advance(p); value = parse_expr(p); expect_punct(p, ":"); }
            else { expect_kw(p, "default"); expect_punct(p, ":"); }
            NodeVec stmts = {0};
            while (!cur_is_kw(p, "case") && !cur_is_kw(p, "default") && !cur_is_punct(p, "}") && p->cur->kind != CPP_TOK_EOF)
                nv_push(&stmts, parse_statement(p));
            nv_push(&cases, cppnode_switch_case(value, stmts.items, stmts.n, cline));
        }
        expect_punct(p, "}");
        return cppnode_switch(expr, cases.items, cases.n, line);
    }
    if (cur_is_kw(p, "break")) { advance(p); expect_punct(p, ";"); return cppnode_break(line); }
    if (cur_is_kw(p, "continue")) { advance(p); expect_punct(p, ";"); return cppnode_continue(line); }
    if (cur_is_kw(p, "return")) {
        advance(p);
        CppNode *e = cur_is_punct(p, ";") ? NULL : parse_expr(p);
        expect_punct(p, ";");
        return cppnode_return(e, line);
    }
    CppNode *decl = try_parse_var_decl(p);
    if (decl) return decl;
    CppNode *e = parse_expr(p);
    expect_punct(p, ";");
    return cppnode_expr_stmt(e, line);
}

static CppNode *parse_block(CppParser *p) {
    int line = p->cur->line;
    expect_punct(p, "{");
    NodeVec v = {0};
    while (!cur_is_punct(p, "}") && p->cur->kind != CPP_TOK_EOF) nv_push(&v, parse_statement(p));
    expect_punct(p, "}");
    return cppnode_block(v.items, v.n, line);
}

/* ============================ DECLARATIONS =============================
 * "params" grammar shared by free functions, methods, and constructors:
 * "(" [ Type name [ "=" expr ] ("," Type name [ "=" expr ])* ] ")" */
static CppNode **parse_params(CppParser *p, int *n_params) {
    NodeVec v = {0};
    expect_punct(p, "(");
    if (!cur_is_punct(p, ")")) {
        for (;;) {
            int line = p->cur->line;
            CppType *t = parse_type(p);
            char *name = cur_is_ident(p) ? expect_ident(p) : cpp_strdup("");
            CppNode *def = NULL;
            if (cur_is_punct(p, "=")) { advance(p); def = parse_expr(p); }
            nv_push(&v, cppnode_param(t, name, def, line));
            free(name);
            if (cur_is_punct(p, ",")) { advance(p); continue; }
            break;
        }
    }
    expect_punct(p, ")");
    *n_params = v.n;
    return v.items;
}

static const char *parse_operator_name(CppParser *p) {
    /* cursor is just past the 'operator' keyword */
    static const char *two[] = { "==", "!=", "<=", ">=", "<<", ">>", "&&", "||", "+=", "-=", NULL };
    for (int i = 0; two[i]; i++) if (cur_is_punct(p, two[i])) { const char *s = two[i]; advance(p); return s; }
    static const char *one[] = { "+", "-", "*", "/", "%", "<", ">", "=", "[", "!", NULL };
    for (int i = 0; one[i]; i++) {
        if (cur_is_punct(p, one[i])) {
            const char *s = one[i]; advance(p);
            if (s[0] == '[') { expect_punct(p, "]"); return "[]"; }
            return s;
        }
    }
    perror_at(p, "unsupported operator overload");
    return "?";
}

/* Parses one class/struct member after the access-specifier state
 * machine (see parse_class_decl) has already picked `access`. Returns
 * NULL having consumed nothing further when `cur` is an access-specifier
 * label itself ("public:" etc) — caller handles that case first. */
static CppNode *parse_member(CppParser *p, const char *class_name, CppAccess access) {
    int line = p->cur->line;
    int is_static = 0, is_virtual = 0;
    while (cur_is_kw(p, "static") || cur_is_kw(p, "virtual") || cur_is_kw(p, "inline") || cur_is_kw(p, "explicit") || cur_is_kw(p, "friend")) {
        if (cur_is_kw(p, "static")) is_static = 1;
        if (cur_is_kw(p, "virtual")) is_virtual = 1;
        advance(p);
    }
    if (cur_is_punct(p, "~")) {
        advance(p);
        char *name = expect_ident(p);
        int n_params; CppNode **params = parse_params(p, &n_params);
        (void)params;
        CppNode *body = NULL;
        if (cur_is_punct(p, "{")) body = parse_block(p);
        else expect_punct(p, ";");
        CppNode *n = cppnode_dtor_decl(name, body, access, is_virtual, line);
        free(name);
        return n;
    }
    if (cur_is_ident(p) && strcmp(p->cur->text, class_name) == 0 && (peek_ahead(p)->kind == CPP_TOK_PUNCT && strcmp(peek_ahead(p)->text, "(") == 0)) {
        char *name = expect_ident(p);
        int n_params; CppNode **params = parse_params(p, &n_params);
        NodeVec init_args = {0}; char *init_target = NULL;
        if (cur_is_punct(p, ":")) {
            advance(p);
            init_target = expect_ident(p);
            expect_punct(p, "(");
            int argc; CppNode **args = parse_arg_list(p, &argc, ")");
            for (int i = 0; i < argc; i++) nv_push(&init_args, args[i]);
            free(args);
        }
        CppNode *body = NULL;
        if (cur_is_punct(p, "{")) body = parse_block(p);
        else expect_punct(p, ";");
        CppNode *n = cppnode_ctor_decl(name, params, n_params, body, init_args.items, init_args.n, init_target, access, line);
        free(name); free(init_target);
        return n;
    }
    if (cur_is_kw(p, "operator")) {
        CppType *ret = cpptype_new("int"); /* placeholder; real return type was already consumed below when present */
        advance(p);
        const char *opname = parse_operator_name(p);
        int n_params; CppNode **params = parse_params(p, &n_params);
        int is_const = 0;
        if (cur_is_kw(p, "const")) { is_const = 1; advance(p); }
        CppNode *body = NULL;
        if (cur_is_punct(p, "{")) body = parse_block(p);
        else expect_punct(p, ";");
        return cppnode_method_decl(ret, "operator", params, n_params, body, access, is_static, is_virtual, is_const, opname, line);
    }
    CppType *type = parse_type(p);
    if (cur_is_kw(p, "operator")) {
        advance(p);
        const char *opname = parse_operator_name(p);
        int n_params; CppNode **params = parse_params(p, &n_params);
        int is_const = 0;
        if (cur_is_kw(p, "const")) { is_const = 1; advance(p); }
        CppNode *body = NULL;
        if (cur_is_punct(p, "{")) body = parse_block(p);
        else expect_punct(p, ";");
        return cppnode_method_decl(type, "operator", params, n_params, body, access, is_static, is_virtual, is_const, opname, line);
    }
    char *name = expect_ident(p);
    if (cur_is_punct(p, "(")) {
        int n_params; CppNode **params = parse_params(p, &n_params);
        int is_const = 0;
        if (cur_is_kw(p, "const")) { is_const = 1; advance(p); }
        if (cur_is_kw(p, "override")) advance(p);
        CppNode *body = NULL;
        if (cur_is_punct(p, "{")) body = parse_block(p);
        else expect_punct(p, ";");
        CppNode *n = cppnode_method_decl(type, name, params, n_params, body, access, is_static, is_virtual, is_const, NULL, line);
        free(name);
        return n;
    }
    CppNode *init = NULL;
    if (cur_is_punct(p, "=")) { advance(p); init = parse_expr(p); }
    expect_punct(p, ";");
    CppNode *n = cppnode_field_decl(type, name, init, access, is_static, line);
    free(name);
    return n;
}

static CppNode *parse_class_decl(CppParser *p, CppNode **template_params, int n_template_params) {
    int line = p->cur->line;
    int is_struct = cur_is_kw(p, "struct");
    advance(p); /* 'class' or 'struct' */
    char *name = expect_ident(p);
    sv_push(&extra(p)->known_types, name);
    char *base = NULL;
    if (cur_is_punct(p, ":")) {
        advance(p);
        if (cur_is_kw(p, "public") || cur_is_kw(p, "private") || cur_is_kw(p, "protected")) advance(p);
        base = expect_ident(p);
    }
    expect_punct(p, "{");
    NodeVec members = {0};
    CppAccess access = is_struct ? CPP_ACC_PUBLIC : CPP_ACC_PRIVATE;
    while (!cur_is_punct(p, "}") && p->cur->kind != CPP_TOK_EOF) {
        if (cur_is_kw(p, "public") && peek_ahead(p)->kind == CPP_TOK_PUNCT && strcmp(peek_ahead(p)->text, ":") == 0) { advance(p); advance(p); access = CPP_ACC_PUBLIC; continue; }
        if (cur_is_kw(p, "private") && peek_ahead(p)->kind == CPP_TOK_PUNCT && strcmp(peek_ahead(p)->text, ":") == 0) { advance(p); advance(p); access = CPP_ACC_PRIVATE; continue; }
        if (cur_is_kw(p, "protected") && peek_ahead(p)->kind == CPP_TOK_PUNCT && strcmp(peek_ahead(p)->text, ":") == 0) { advance(p); advance(p); access = CPP_ACC_PROTECTED; continue; }
        nv_push(&members, parse_member(p, name, access));
    }
    expect_punct(p, "}");
    expect_punct(p, ";");
    CppNode *n = cppnode_class_decl(name, is_struct, base, template_params, n_template_params, members.items, members.n, line);
    free(name); free(base);
    return n;
}

static CppNode **parse_template_params(CppParser *p, int *n) {
    expect_punct(p, "<");
    NodeVec v = {0};
    for (;;) {
        if (cur_is_kw(p, "typename") || cur_is_kw(p, "class")) advance(p);
        char *name = expect_ident(p);
        nv_push(&v, cppnode_template_param(name, p->cur->line));
        /* Register the template parameter itself as a known type name
         * (e.g. "T") — without this, a local declaration using the bare
         * parameter type inside the template's own body ("T v = ...;")
         * was never recognized as a declaration at all by
         * is_known_type_start, and got misparsed as an expression
         * statement instead (a real, confirmed bug). */
        sv_push(&extra(p)->known_types, name);
        free(name);
        if (cur_is_punct(p, ",")) { advance(p); continue; }
        break;
    }
    expect_template_close(p);
    *n = v.n;
    return v.items;
}

static CppNode *parse_toplevel_decl(CppParser *p) {
    int line = p->cur->line;
    if (p->cur->kind == CPP_TOK_PREPROC_LINE) {
        char *text = cpp_strdup(p->cur->text);
        advance(p);
        CppNode *n = cppnode_include_raw(text, line);
        free(text);
        return n;
    }
    if (cur_is_kw(p, "using")) {
        advance(p);
        if (cur_is_kw(p, "namespace")) {
            advance(p);
            char *name = expect_ident(p);
            expect_punct(p, ";");
            CppNode *n = cppnode_using_namespace(name, line);
            free(name);
            return n;
        }
        char namebuf[256] = "";
        strcpy(namebuf, p->cur->text); advance(p);
        while (cur_is_punct(p, "::")) { advance(p); strcat(namebuf, "::"); strcat(namebuf, p->cur->text); advance(p); }
        expect_punct(p, ";");
        return cppnode_using_decl(namebuf, line);
    }
    if (cur_is_kw(p, "namespace")) {
        advance(p);
        char *name = expect_ident(p);
        expect_punct(p, "{");
        NodeVec decls = {0};
        while (!cur_is_punct(p, "}") && p->cur->kind != CPP_TOK_EOF) nv_push(&decls, parse_toplevel_decl(p));
        expect_punct(p, "}");
        CppNode *n = cppnode_namespace(name, decls.items, decls.n, line);
        free(name);
        return n;
    }
    if (cur_is_kw(p, "template")) {
        advance(p);
        int n_tp; CppNode **tp = parse_template_params(p, &n_tp);
        if (cur_is_kw(p, "class") || cur_is_kw(p, "struct")) return parse_class_decl(p, tp, n_tp);
        CppType *ret = parse_type(p);
        char *name = expect_ident(p);
        int n_params; CppNode **params = parse_params(p, &n_params);
        CppNode *body = NULL;
        if (cur_is_punct(p, "{")) body = parse_block(p);
        else expect_punct(p, ";");
        CppNode *n = cppnode_func_decl(ret, name, tp, n_tp, params, n_params, body, line);
        free(name);
        return n;
    }
    if (cur_is_kw(p, "class") || cur_is_kw(p, "struct")) return parse_class_decl(p, NULL, 0);

    /* Otherwise: a free function or a global variable declaration. */
    CppNode *decl = try_parse_var_decl(p);
    if (decl) return decl;
    CppType *ret = parse_type(p);
    char *name = expect_ident(p);
    int n_params; CppNode **params = parse_params(p, &n_params);
    CppNode *body = NULL;
    if (cur_is_punct(p, "{")) body = parse_block(p);
    else expect_punct(p, ";");
    CppNode *n = cppnode_func_decl(ret, name, NULL, 0, params, n_params, body, line);
    free(name);
    return n;
}

CppNode *cpp_parse_unit(CppParser *p) {
    int line = p->cur->line;
    NodeVec decls = {0};
    while (p->cur->kind != CPP_TOK_EOF) nv_push(&decls, parse_toplevel_decl(p));
    return cppnode_unit(decls.items, decls.n, line);
}
