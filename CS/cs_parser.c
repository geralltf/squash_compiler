#include "cs_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- token plumbing ---- */

static CsTok *cs_tok_copy(const CsTok *t) {
    CsTok *c;
    if (!t) return 0;
    c = (CsTok *)malloc(sizeof(CsTok));
    *c = *t;
    c->text = t->text ? cs_strdup(t->text) : 0;
    return c;
}

void cs_parser_init(CsParser *p, const char *src) {
    cs_lexer_init(&p->lx, src);
    p->cur = cs_lexer_next(&p->lx);
    p->peeked = 0;
    p->error_count = 0;
}

void cs_parser_free(CsParser *p) {
    cs_tok_free(p->cur);
    if (p->peeked) cs_tok_free(p->peeked);
}

static void advance(CsParser *p) {
    cs_tok_free(p->cur);
    if (p->peeked) { p->cur = p->peeked; p->peeked = 0; }
    else p->cur = cs_lexer_next(&p->lx);
}

static CsTok *peek_next(CsParser *p) {
    if (!p->peeked) p->peeked = cs_lexer_next(&p->lx);
    return p->peeked;
}

typedef struct { CsLexer lx; CsTok *cur; CsTok *peeked; } CsCkpt;

/* NOTE: out-parameter style, NOT "CsCkpt ckpt_save(CsParser *p)" returning
 * by value -- a real squash codegen bug was found this session: returning
 * a struct LARGER THAN 16 BYTES by value produces garbage (confirmed
 * minimal repro: a 4-field/2-pointer ~40-byte struct returned by value
 * came back with correct-looking pointer fields but garbage ints,
 * consistent with the SysV x86-64 "large aggregate returned via hidden
 * pointer" ABI convention not being implemented correctly, or falling
 * back to a register-pair path only valid for <=16-byte structs) even
 * though gcc-compiled code with the identical struct/function shape
 * returns correct values. `CsCkpt` (a CsLexer + 2 pointers) is exactly
 * such an oversized struct. Worked around here rather than fixed in
 * codegen.c (out of scope for this parser — flagged to the user as a
 * THIRD discovered, deferred squash bug this session, and likely the
 * most consequential of the three since "return a struct by value" is a
 * completely ordinary C idiom, not an edge case). */
static void ckpt_save(CsParser *p, CsCkpt *out) {
    out->lx = p->lx;
    out->cur = cs_tok_copy(p->cur);
    out->peeked = p->peeked ? cs_tok_copy(p->peeked) : 0;
}
static void ckpt_restore(CsParser *p, CsCkpt *c) {
    cs_tok_free(p->cur);
    if (p->peeked) cs_tok_free(p->peeked);
    p->lx = c->lx;
    p->cur = c->cur;
    p->peeked = c->peeked;
}
static void ckpt_discard(CsCkpt *c) {
    cs_tok_free(c->cur);
    if (c->peeked) cs_tok_free(c->peeked);
}

static int is_kw(CsParser *p, const char *s)    { return p->cur->kind == CS_TOK_KEYWORD && strcmp(p->cur->text, s) == 0; }
static int is_punct(CsParser *p, const char *s) { return p->cur->kind == CS_TOK_PUNCT && strcmp(p->cur->text, s) == 0; }
static int is_ident_tok(CsParser *p)            { return p->cur->kind == CS_TOK_IDENT; }
static int is_eof(CsParser *p)                  { return p->cur->kind == CS_TOK_EOF; }

static void perror_at(CsParser *p, const char *fmt, ...) {
    va_list ap;
    p->error_count++;
    fprintf(stderr, "cs_parser: line %d: ", p->cur->line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void expect_punct(CsParser *p, const char *s) {
    if (is_punct(p, s)) { advance(p); return; }
    perror_at(p, "expected '%s', got '%s'", s, p->cur->text ? p->cur->text : "<eof>");
}

static void expect_kw(CsParser *p, const char *s) {
    if (is_kw(p, s)) { advance(p); return; }
    perror_at(p, "expected keyword '%s', got '%s'", s, p->cur->text ? p->cur->text : "<eof>");
}

static char *expect_ident(CsParser *p) {
    char *name;
    if (!is_ident_tok(p)) {
        perror_at(p, "expected identifier, got '%s'", p->cur->text ? p->cur->text : "<eof>");
        return cs_strdup("<error>");
    }
    name = cs_strdup(p->cur->text);
    advance(p);
    return name;
}

/* recovery: skip forward to the next ';' or matching '}' at the current
 * brace depth, so one malformed member/statement doesn't abort the whole
 * file -- mirrors squash's own parser_new4.c "report everything" style. */
static void recover_to_stmt_end(CsParser *p) {
    int depth = 0;
    while (!is_eof(p)) {
        if (is_punct(p, "{")) { depth++; advance(p); continue; }
        if (is_punct(p, "}")) { if (depth == 0) return; depth--; advance(p); if (depth == 0) return; continue; }
        if (is_punct(p, ";") && depth == 0) { advance(p); return; }
        advance(p);
    }
}

/* consumes one closing '>' from a possibly-merged ">>"/">>=" token (see
 * cs_parser.h's header comment on the generics/shift-operator ambiguity);
 * returns 1 on success, 0 if the current token isn't angle-bracket-shaped
 * at all. */
static int consume_closing_angle(CsParser *p) {
    if (is_punct(p, ">")) { advance(p); return 1; }
    if (p->cur->kind == CS_TOK_PUNCT && p->cur->text && p->cur->text[0] == '>' && p->cur->text[1] == '>' && p->cur->text[2] == 0) {
        p->cur->text[0] = '>'; p->cur->text[1] = 0; /* shrink ">>" -> ">", leave current for the next close */
        return 1;
    }
    if (p->cur->kind == CS_TOK_PUNCT && strcmp(p->cur->text, ">=") == 0) {
        /* ">>=" already lexed as ">>" + "=" won't happen (lexer has no ">>=" op);
         * a real ">>=" is lexed as a single 3-char punct only via ops3, which we
         * don't include for ">>=" itself (only "?\?=","<<=",">>="... wait ">>="
         * IS in ops3) -- handle that merged form too. */
        return 0;
    }
    if (p->cur->kind == CS_TOK_PUNCT && strcmp(p->cur->text, ">>=") == 0) {
        free(p->cur->text); p->cur->text = cs_strdup(">=");
        return 1;
    }
    return 0;
}

/* forward decls */
static CsType *parse_type(CsParser *p);
static CsNode *parse_expr(CsParser *p);
static CsNode *parse_assignment(CsParser *p);
static CsNode *parse_statement(CsParser *p);
static CsNode *parse_block(CsParser *p);
static void parse_param_list(CsParser *p, CsNode ***out_params, int *out_n);

/* ---- types ---- */

static int looks_like_type_start(CsParser *p) {
    return is_ident_tok(p) || is_kw(p, "int") || is_kw(p, "long") || is_kw(p, "short") ||
           is_kw(p, "byte") || is_kw(p, "sbyte") || is_kw(p, "uint") || is_kw(p, "ulong") ||
           is_kw(p, "ushort") || is_kw(p, "string") || is_kw(p, "bool") || is_kw(p, "double") ||
           is_kw(p, "float") || is_kw(p, "decimal") || is_kw(p, "char") || is_kw(p, "object") ||
           is_kw(p, "void") || is_kw(p, "var");
}

static void parse_type_arg_list(CsParser *p, CsType ***out_args, int *out_n) {
    CsType **args = 0; int n = 0, cap = 0;
    expect_punct(p, "<");
    while (!is_punct(p, ">") && !(p->cur->kind == CS_TOK_PUNCT && p->cur->text[0] == '>') && !is_eof(p)) {
        CsType *t = parse_type(p);
        if (n >= cap) { cap = cap ? cap * 2 : 4; args = (CsType **)realloc(args, sizeof(CsType *) * (unsigned int)cap); }
        args[n++] = t;
        if (is_punct(p, ",")) { advance(p); continue; }
        break;
    }
    consume_closing_angle(p);
    *out_args = args; *out_n = n;
}

static CsType *parse_type(CsParser *p) {
    CsType *t;
    char *name;
    if (!looks_like_type_start(p)) {
        perror_at(p, "expected a type, got '%s'", p->cur->text ? p->cur->text : "<eof>");
        return cstype_new("<error>");
    }
    name = cs_strdup(p->cur->text);
    advance(p);
    /* qualified name "System.Collections.Generic.List" -- flatten to the last segment,
     * matching the plan's "namespaces parsed and flattened" scope. */
    while (is_punct(p, ".") && (peek_next(p)->kind == CS_TOK_IDENT)) {
        advance(p);
        free(name);
        name = cs_strdup(p->cur->text);
        advance(p);
    }
    t = cstype_new(name);
    free(name);
    if (is_punct(p, "<")) {
        CsCkpt ck; ckpt_save(p, &ck);
        CsType **args; int n;
        parse_type_arg_list(p, &args, &n);
        if (p->error_count > 0) {
            /* backtrack: this "<" wasn't actually a generic-arg list (e.g. we're
             * really looking at "SomeIdent < otherExpr" in a context where a type
             * was merely being SPECULATIVELY tried) -- but parse_type() itself is
             * only ever called where a type is syntactically required, so a
             * failure here is a real error, not an ambiguity; still, avoid
             * doubly-counting errors already reported for this trial. */
            ckpt_discard(&ck);
        } else {
            ckpt_discard(&ck);
            t->type_args = args; t->n_type_args = n;
        }
    }
    /* NOTE: `pk` is deliberately hoisted into a local instead of calling
     * peek_next(p) twice inline in the condition below -- a real squash
     * codegen bug (found this session, the fourth): calling the SAME
     * function TWICE within one short-circuited "&&" expression corrupts
     * something (confirmed minimal repro: identical two-call condition
     * segfaults, the same condition split across separate statements or
     * called only once does not) even though gcc compiles the identical
     * expression correctly. Worked around everywhere in this file rather
     * than fixed in codegen.c (out of scope here). */
    for (;;) {
        CsTok *pk;
        if (!is_punct(p, "[")) break;
        pk = peek_next(p);
        if (pk->kind != CS_TOK_PUNCT || strcmp(pk->text, "]") != 0) break;
        advance(p); advance(p);
        t->array_rank++;
    }
    if (is_punct(p, "?")) { advance(p); t->is_nullable = 1; }
    return t;
}

/* ---- expressions ---- */

static CsNode **parse_arg_list(CsParser *p, int *out_argc) {
    CsNode **args = 0; int n = 0, cap = 0;
    expect_punct(p, "(");
    while (!is_punct(p, ")") && !is_eof(p)) {
        CsNode *a;
        if (is_kw(p, "out") || is_kw(p, "ref")) advance(p); /* ref/out modifiers: parsed, not tracked -- see plan scope */
        a = parse_assignment(p);
        if (n >= cap) { cap = cap ? cap * 2 : 4; args = (CsNode **)realloc(args, sizeof(CsNode *) * (unsigned int)cap); }
        args[n++] = a;
        if (is_punct(p, ",")) { advance(p); continue; }
        break;
    }
    expect_punct(p, ")");
    *out_argc = n;
    return args;
}

/* Skips a balanced {...} block without building any AST for it -- used for
 * object-initializer syntax ("new Foo { X = 1 }"), which this phase parses
 * and discards rather than lowering (documented gap, see cs_ast.h). */
static void skip_balanced_braces(CsParser *p) {
    int depth = 0;
    if (!is_punct(p, "{")) return;
    do {
        if (is_punct(p, "{")) depth++;
        else if (is_punct(p, "}")) depth--;
        advance(p);
    } while (depth > 0 && !is_eof(p));
}

static CsNode *parse_new_expr(CsParser *p) {
    int line = p->cur->line;
    char *name;
    CsType *elem_type;
    advance(p); /* "new" */
    if (!is_ident_tok(p) && !looks_like_type_start(p)) {
        perror_at(p, "expected a type after 'new'");
        return csnode_lit_null(line);
    }
    name = cs_strdup(p->cur->text);
    advance(p);
    while (is_punct(p, ".") && peek_next(p)->kind == CS_TOK_IDENT) { advance(p); free(name); name = cs_strdup(p->cur->text); advance(p); }
    elem_type = cstype_new(name);
    free(name);
    if (is_punct(p, "<")) {
        CsType **args; int n;
        parse_type_arg_list(p, &args, &n);
        elem_type->type_args = args; elem_type->n_type_args = n;
    }
    if (is_punct(p, "[")) {
        advance(p);
        if (is_punct(p, "]")) {
            advance(p);
            /* new T[] { elems } */
            CsNode **elems = 0; int n = 0, cap = 0;
            if (is_punct(p, "{")) {
                advance(p);
                while (!is_punct(p, "}") && !is_eof(p)) {
                    CsNode *e = parse_assignment(p);
                    if (n >= cap) { cap = cap ? cap * 2 : 4; elems = (CsNode **)realloc(elems, sizeof(CsNode *) * (unsigned int)cap); }
                    elems[n++] = e;
                    if (is_punct(p, ",")) { advance(p); continue; }
                    break;
                }
                expect_punct(p, "}");
            }
            return csnode_new_array(elem_type, elems, n, 0, line);
        }
        {
            CsNode *size_expr = parse_expr(p);
            expect_punct(p, "]");
            return csnode_new_array(elem_type, 0, 0, size_expr, line);
        }
    }
    {
        CsNode **args = 0; int argc = 0;
        if (is_punct(p, "(")) args = parse_arg_list(p, &argc);
        if (is_punct(p, "{")) skip_balanced_braces(p); /* object-initializer: discarded, see skip_balanced_braces */
        return csnode_new_object(elem_type, args, argc, line);
    }
}

/* (params) => body  OR  ident => body -- tries the parenthesized form via
 * a checkpoint since "(" also starts a parenthesized/cast expression;
 * returns NULL (after restoring the checkpoint) if this isn't a lambda. */
static CsNode *try_parse_lambda(CsParser *p) {
    int line = p->cur->line;
    CsTok *pk1 = 0;
    if (is_ident_tok(p)) pk1 = peek_next(p); /* see the array_rank loop's comment above on why this is hoisted, not called twice inline */
    if (pk1 && pk1->kind == CS_TOK_PUNCT && strcmp(pk1->text, "=>") == 0) {
        char **names = (char **)malloc(sizeof(char *));
        CsNode *body;
        names[0] = cs_strdup(p->cur->text);
        advance(p); advance(p); /* ident, "=>" */
        if (is_punct(p, "{")) { body = parse_block(p); return csnode_lambda(names, 1, body, 0, line); }
        body = parse_assignment(p);
        return csnode_lambda(names, 1, body, 1, line);
    }
    if (is_punct(p, "(")) {
        CsCkpt ck; ckpt_save(p, &ck);
        char **names = 0; int n = 0, cap = 0;
        int ok = 1;
        advance(p);
        while (!is_punct(p, ")") && !is_eof(p)) {
            if (looks_like_type_start(p) && (peek_next(p)->kind == CS_TOK_IDENT)) advance(p); /* optional explicit param type, discarded */
            if (!is_ident_tok(p)) { ok = 0; break; }
            if (n >= cap) { cap = cap ? cap * 2 : 4; names = (char **)realloc(names, sizeof(char *) * (unsigned int)cap); }
            names[n++] = cs_strdup(p->cur->text);
            advance(p);
            if (is_punct(p, ",")) { advance(p); continue; }
            break;
        }
        if (ok && is_punct(p, ")")) {
            advance(p);
            if (is_punct(p, "=>")) {
                CsNode *body;
                advance(p);
                ckpt_discard(&ck);
                if (is_punct(p, "{")) { body = parse_block(p); return csnode_lambda(names, n, body, 0, line); }
                body = parse_assignment(p);
                return csnode_lambda(names, n, body, 1, line);
            }
        }
        { int i; for (i = 0; i < n; i++) free(names[i]); free(names); }
        ckpt_restore(p, &ck);
        return 0;
    }
    return 0;
}

/* "(" Type ")" unary-expr -- a cast. Ambiguous with a parenthesized
 * expression; tries via checkpoint, restoring on failure. Distinguishing
 * heuristic: after "(" TYPE ")" , a cast must be followed by something
 * that can start a unary expression (ident, literal, "(", "!", "-", etc.)
 * -- NOT by a token that could only continue a binary/postfix expression
 * (like an operator or ")" or ";"), which is how "(x)" (grouping, x a
 * plain identifier that also happens to look like a type name) is told
 * apart from "(int)x" (a real cast). */
static CsNode *parse_unary(CsParser *p);

static CsNode *try_parse_cast(CsParser *p) {
    CsCkpt ck;
    CsType *t;
    int errs_before;
    int line = p->cur->line;
    if (!is_punct(p, "(")) return 0;
    ckpt_save(p, &ck);
    errs_before = p->error_count;
    advance(p);
    if (!looks_like_type_start(p)) { ckpt_restore(p, &ck); return 0; }
    t = parse_type(p);
    if (p->error_count != errs_before || !is_punct(p, ")")) { cstype_free(t); ckpt_restore(p, &ck); p->error_count = errs_before; return 0; }
    advance(p);
    /* must be followed by a unary-expression starter */
    if (is_ident_tok(p) || p->cur->kind == CS_TOK_INT_LIT || p->cur->kind == CS_TOK_DOUBLE_LIT ||
        p->cur->kind == CS_TOK_STRING_LIT || p->cur->kind == CS_TOK_INTERP_STRING_LIT ||
        p->cur->kind == CS_TOK_CHAR_LIT || is_punct(p, "(") || is_punct(p, "!") ||
        is_kw(p, "true") || is_kw(p, "false") || is_kw(p, "null") || is_kw(p, "this") || is_kw(p, "new")) {
        CsNode *operand;
        ckpt_discard(&ck);
        operand = parse_unary(p);
        return csnode_cast(t, operand, line);
    }
    cstype_free(t);
    ckpt_restore(p, &ck);
    return 0;
}

/* ---- interpolated strings ---- */

static CsNode *parse_expr_from_substring(const char *sub, CsParser *outer) {
    CsParser sp;
    CsNode *e;
    cs_parser_init(&sp, sub);
    e = parse_expr(&sp);
    outer->error_count += sp.error_count;
    cs_parser_free(&sp);
    return e;
}

/* Splits an interp-string's raw body (as captured by cs_lexer.c's
 * lex_interp_string_body -- literal {{ }} already collapsed to single
 * braces, real {expr} spans still intact) into alternating text/expr
 * parts. Mirrors the lexer's own depth-tracking so a nested string
 * literal's brace doesn't miscount. */
static void split_interp_parts(CsParser *p, const char *raw, CsInterpPart **out_parts, int *out_n) {
    CsInterpPart *parts = 0; int n = 0, cap = 0;
    int i = 0, len = (int)strlen(raw);
    char textbuf[4096]; int tn = 0;

    #define FLUSH_TEXT() do { \
        if (tn > 0) { \
            textbuf[tn] = 0; \
            if (n >= cap) { cap = cap ? cap*2 : 4; parts = (CsInterpPart*)realloc(parts, sizeof(CsInterpPart)*(unsigned int)cap); } \
            parts[n].is_expr = 0; parts[n].text = cs_strdup(textbuf); parts[n].expr = 0; n++; \
            tn = 0; \
        } \
    } while (0)

    while (i < len) {
        if (raw[i] == '{') {
            int depth = 1, start;
            FLUSH_TEXT();
            i++; start = i;
            while (i < len && depth > 0) {
                if (raw[i] == '"') {
                    i++;
                    while (i < len && raw[i] != '"') { if (raw[i] == '\\' && i + 1 < len) i++; i++; }
                    if (i < len) i++;
                    continue;
                }
                if (raw[i] == '{') depth++;
                else if (raw[i] == '}') { depth--; if (depth == 0) break; }
                i++;
            }
            {
                int elen = i - start;
                char *sub = (char *)malloc((unsigned int)elen + 1);
                CsNode *e;
                memcpy(sub, raw + start, (unsigned int)elen);
                sub[elen] = 0;
                e = parse_expr_from_substring(sub, p);
                free(sub);
                if (n >= cap) { cap = cap ? cap*2 : 4; parts = (CsInterpPart*)realloc(parts, sizeof(CsInterpPart)*(unsigned int)cap); }
                parts[n].is_expr = 1; parts[n].text = 0; parts[n].expr = e; n++;
            }
            if (i < len && raw[i] == '}') i++;
            continue;
        }
        if (tn < (int)sizeof(textbuf) - 1) textbuf[tn++] = raw[i];
        i++;
    }
    FLUSH_TEXT();
    #undef FLUSH_TEXT
    *out_parts = parts; *out_n = n;
}

/* ---- primary / postfix / unary ---- */

static CsNode *parse_primary(CsParser *p) {
    int line = p->cur->line;
    CsNode *lam = try_parse_lambda(p);
    if (lam) return lam;

    if (p->cur->kind == CS_TOK_INT_LIT) { long long v = p->cur->int_value; advance(p); return csnode_lit_int(v, line); }
    if (p->cur->kind == CS_TOK_DOUBLE_LIT) { double v = p->cur->double_value; advance(p); return csnode_lit_double(v, line); }
    if (p->cur->kind == CS_TOK_STRING_LIT) { CsNode *n = csnode_lit_string(p->cur->text, line); advance(p); return n; }
    if (p->cur->kind == CS_TOK_CHAR_LIT) { long long v = p->cur->int_value; advance(p); return csnode_lit_char(v, line); }
    if (p->cur->kind == CS_TOK_INTERP_STRING_LIT) {
        CsInterpPart *parts; int n;
        char *raw = cs_strdup(p->cur->text);
        advance(p);
        split_interp_parts(p, raw, &parts, &n);
        free(raw);
        return csnode_lit_interp(parts, n, line);
    }
    if (is_kw(p, "true")) { advance(p); return csnode_lit_bool(1, line); }
    if (is_kw(p, "false")) { advance(p); return csnode_lit_bool(0, line); }
    if (is_kw(p, "null")) { advance(p); return csnode_lit_null(line); }
    if (is_kw(p, "this")) { advance(p); return csnode_this(line); }
    if (is_kw(p, "base")) { advance(p); return csnode_base(line); }
    if (is_kw(p, "new")) return parse_new_expr(p);

    if (is_punct(p, "(")) {
        CsNode *cst = try_parse_cast(p);
        CsNode *inner;
        if (cst) return cst;
        advance(p);
        inner = parse_expr(p);
        expect_punct(p, ")");
        return inner;
    }

    if (is_ident_tok(p)) {
        char *name = cs_strdup(p->cur->text);
        CsNode *n = csnode_ident(name, line);
        free(name);
        advance(p);
        return n;
    }

    perror_at(p, "unexpected token '%s' in expression", p->cur->text ? p->cur->text : "<eof>");
    advance(p);
    return csnode_lit_null(line);
}

/* Tries a generic method-call type-arg list ("Foo<int>(...)") via
 * checkpoint -- only committed if it's immediately followed by "(", since
 * "a < b > (c)" (two comparisons) is also syntactically possible and far
 * more common than a generic call in ambiguous contexts. */
static int try_consume_generic_call_args(CsParser *p) {
    CsCkpt ck;
    CsType **args; int n;
    int errs_before;
    if (!is_punct(p, "<")) return 0;
    ckpt_save(p, &ck);
    errs_before = p->error_count;
    parse_type_arg_list(p, &args, &n);
    { int i; for (i = 0; i < n; i++) cstype_free(args[i]); free(args); }
    if (p->error_count != errs_before || !is_punct(p, "(")) {
        ckpt_restore(p, &ck);
        p->error_count = errs_before;
        return 0;
    }
    ckpt_discard(&ck);
    return 1;
}

static CsNode *parse_postfix(CsParser *p) {
    CsNode *n = parse_primary(p);
    for (;;) {
        int line = p->cur->line;
        if (is_punct(p, ".")) {
            char *name;
            advance(p);
            name = expect_ident(p);
            n = csnode_member(n, name, line);
            free(name);
            try_consume_generic_call_args(p); /* Foo.Bar<int>(...) -- type args parsed & discarded (erased at this layer) */
            continue;
        }
        if (is_punct(p, "(")) {
            int argc; CsNode **args = parse_arg_list(p, &argc);
            n = csnode_call(n, args, argc, line);
            continue;
        }
        if (is_punct(p, "[")) {
            CsNode *idx;
            advance(p);
            idx = parse_expr(p);
            expect_punct(p, "]");
            n = csnode_index(n, idx, line);
            continue;
        }
        if (is_punct(p, "++") || is_punct(p, "--")) {
            char *op = cs_strdup(p->cur->text);
            advance(p);
            n = csnode_unary(op, n, 1, line);
            free(op);
            continue;
        }
        if (is_punct(p, "?.")) {
            /* null-conditional member access -- lowered the same as plain
             * '.' for this phase (documented simplification: real C#
             * short-circuits the whole chain on null, this parser doesn't
             * distinguish it from '.', so a null receiver still throws
             * rather than yielding null). */
            char *name;
            advance(p);
            name = expect_ident(p);
            n = csnode_member(n, name, line);
            free(name);
            continue;
        }
        break;
    }
    return n;
}

static CsNode *parse_unary(CsParser *p) {
    int line = p->cur->line;
    if (is_punct(p, "!") || is_punct(p, "-") || is_punct(p, "+") || is_punct(p, "~")) {
        char *op = cs_strdup(p->cur->text);
        CsNode *operand;
        advance(p);
        operand = parse_unary(p);
        { CsNode *n = csnode_unary(op, operand, 0, line); free(op); return n; }
    }
    if (is_punct(p, "++") || is_punct(p, "--")) {
        char *op = cs_strdup(p->cur->text);
        CsNode *operand;
        advance(p);
        operand = parse_unary(p);
        { CsNode *n = csnode_unary(op, operand, 0, line); free(op); return n; }
    }
    return parse_postfix(p);
}


static CsNode *parse_multiplicative(CsParser *p) {
    CsNode *left = parse_unary(p);
    while (is_punct(p, "*") || is_punct(p, "/") || is_punct(p, "%")) {
        int line = p->cur->line; char *op = cs_strdup(p->cur->text); CsNode *right;
        advance(p); right = parse_unary(p);
        left = csnode_binary(op, left, right, line); free(op);
    }
    return left;
}

static CsNode *parse_additive(CsParser *p) {
    CsNode *left = parse_multiplicative(p);
    while (is_punct(p, "+") || is_punct(p, "-")) {
        int line = p->cur->line; char *op = cs_strdup(p->cur->text); CsNode *right;
        advance(p); right = parse_multiplicative(p);
        left = csnode_binary(op, left, right, line); free(op);
    }
    return left;
}

static CsNode *parse_shift(CsParser *p) {
    CsNode *left = parse_additive(p);
    while (is_punct(p, "<<") || is_punct(p, ">>")) {
        int line = p->cur->line; char *op = cs_strdup(p->cur->text); CsNode *right;
        advance(p); right = parse_additive(p);
        left = csnode_binary(op, left, right, line); free(op);
    }
    return left;
}

/* relational, plus "is"/"as" (RHS is a TYPE, encoded as a CS_IDENT node
 * holding the type's rendered name -- see cs_parser.h's header comment on
 * why generic-arg fidelity is dropped for is/as targets in this phase). */
static CsNode *parse_relational(CsParser *p) {
    CsNode *left = parse_shift(p);
    for (;;) {
        if (is_punct(p, "<") || is_punct(p, ">") || is_punct(p, "<=") || is_punct(p, ">=")) {
            int line = p->cur->line; char *op = cs_strdup(p->cur->text); CsNode *right;
            advance(p); right = parse_shift(p);
            left = csnode_binary(op, left, right, line); free(op);
            continue;
        }
        if (is_kw(p, "is") || is_kw(p, "as")) {
            int line = p->cur->line;
            char *op = cs_strdup(p->cur->text);
            CsType *t; char *tname; CsNode *right;
            advance(p);
            t = parse_type(p);
            tname = cstype_str(t);
            cstype_free(t);
            right = csnode_ident(tname, line);
            free(tname);
            left = csnode_binary(op, left, right, line);
            free(op);
            continue;
        }
        break;
    }
    return left;
}

static CsNode *parse_equality(CsParser *p) {
    CsNode *left = parse_relational(p);
    while (is_punct(p, "==") || is_punct(p, "!=")) {
        int line = p->cur->line; char *op = cs_strdup(p->cur->text); CsNode *right;
        advance(p); right = parse_relational(p);
        left = csnode_binary(op, left, right, line); free(op);
    }
    return left;
}

static CsNode *parse_bitand(CsParser *p) {
    CsNode *left = parse_equality(p);
    while (is_punct(p, "&")) {
        int line = p->cur->line; CsNode *right;
        advance(p); right = parse_equality(p);
        left = csnode_binary("&", left, right, line);
    }
    return left;
}

static CsNode *parse_bitxor(CsParser *p) {
    CsNode *left = parse_bitand(p);
    while (is_punct(p, "^")) {
        int line = p->cur->line; CsNode *right;
        advance(p); right = parse_bitand(p);
        left = csnode_binary("^", left, right, line);
    }
    return left;
}

static CsNode *parse_bitor(CsParser *p) {
    CsNode *left = parse_bitxor(p);
    while (is_punct(p, "|")) {
        int line = p->cur->line; CsNode *right;
        advance(p); right = parse_bitxor(p);
        left = csnode_binary("|", left, right, line);
    }
    return left;
}

static CsNode *parse_and(CsParser *p) {
    CsNode *left = parse_bitor(p);
    while (is_punct(p, "&&")) {
        int line = p->cur->line; CsNode *right;
        advance(p); right = parse_bitor(p);
        left = csnode_binary("&&", left, right, line);
    }
    return left;
}

static CsNode *parse_or(CsParser *p) {
    CsNode *left = parse_and(p);
    while (is_punct(p, "||")) {
        int line = p->cur->line; CsNode *right;
        advance(p); right = parse_and(p);
        left = csnode_binary("||", left, right, line);
    }
    return left;
}

static CsNode *parse_null_coalesce(CsParser *p) {
    CsNode *left = parse_or(p);
    if (is_punct(p, "??")) {
        int line = p->cur->line; CsNode *right;
        advance(p); right = parse_null_coalesce(p); /* right-assoc */
        left = csnode_binary("??", left, right, line);
    }
    return left;
}

static CsNode *parse_ternary(CsParser *p) {
    CsNode *cond = parse_null_coalesce(p);
    if (is_punct(p, "?")) {
        int line = p->cur->line;
        CsNode *then_, *else_;
        advance(p);
        then_ = parse_assignment(p);
        expect_punct(p, ":");
        else_ = parse_assignment(p);
        return csnode_ternary(cond, then_, else_, line);
    }
    return cond;
}

static int is_assign_op(CsParser *p) {
    return is_punct(p, "=") || is_punct(p, "+=") || is_punct(p, "-=") || is_punct(p, "*=") ||
           is_punct(p, "/=") || is_punct(p, "%=") || is_punct(p, "&=") || is_punct(p, "|=") ||
           is_punct(p, "^=") || is_punct(p, "<<=") || is_punct(p, ">>=") || is_punct(p, "?\?=");
}

static CsNode *parse_assignment(CsParser *p) {
    CsNode *lam = try_parse_lambda(p);
    CsNode *left;
    if (lam) return lam;
    left = parse_ternary(p);
    if (is_assign_op(p)) {
        int line = p->cur->line;
        char *op = cs_strdup(p->cur->text);
        CsNode *right;
        advance(p);
        right = parse_assignment(p); /* right-assoc */
        { CsNode *n = csnode_assign(op, left, right, line); free(op); return n; }
    }
    return left;
}

static CsNode *parse_expr(CsParser *p) { return parse_assignment(p); }

/* ---- statements ---- */

static void parse_param_list(CsParser *p, CsNode ***out_params, int *out_n) {
    CsNode **params = 0; int n = 0, cap = 0;
    expect_punct(p, "(");
    while (!is_punct(p, ")") && !is_eof(p)) {
        int line = p->cur->line;
        CsType *t; char *name; CsNode *def = 0;
        int is_out_ref = 0;
        /* "params" (variadic) has no by-reference meaning -- only "out"/
         * "ref" set is_out_ref (see cs_ast.h's own comment on why this is
         * tracked now, unlike before: a [DllImport] extern parameter
         * needs it for correct native marshaling). */
        if (is_kw(p, "out") || is_kw(p, "ref")) { is_out_ref = 1; advance(p); }
        else if (is_kw(p, "params")) advance(p); /* modifier parsed, not tracked */
        t = parse_type(p);
        name = expect_ident(p);
        if (is_punct(p, "=")) { advance(p); def = parse_assignment(p); }
        if (n >= cap) { cap = cap ? cap * 2 : 4; params = (CsNode **)realloc(params, sizeof(CsNode *) * (unsigned int)cap); }
        params[n++] = csnode_param(t, name, def, line);
        params[n-1]->param.is_out_ref = is_out_ref;
        free(name);
        if (is_punct(p, ",")) { advance(p); continue; }
        break;
    }
    expect_punct(p, ")");
    *out_params = params; *out_n = n;
}

static CsNode *parse_local_var_or_expr_stmt(CsParser *p) {
    int line = p->cur->line;
    if (is_kw(p, "var")) {
        char *name; CsNode *init;
        advance(p);
        name = expect_ident(p);
        expect_punct(p, "=");
        init = parse_expr(p);
        expect_punct(p, ";");
        { CsNode *n = csnode_local_var_decl(0, name, init, 1, line); free(name); return n; }
    }
    if (looks_like_type_start(p) && !is_ident_tok(p)) {
        CsType *t = parse_type(p);
        char *name = expect_ident(p);
        CsNode *init = 0;
        if (is_punct(p, "=")) { advance(p); init = parse_expr(p); }
        expect_punct(p, ";");
        { CsNode *n = csnode_local_var_decl(t, name, init, 0, line); free(name); return n; }
    }
    if (is_ident_tok(p)) {
        CsCkpt ck; ckpt_save(p, &ck);
        int errs_before = p->error_count;
        CsType *t = parse_type(p);
        if (p->error_count == errs_before && is_ident_tok(p)) {
            char *name = expect_ident(p);
            CsNode *init = 0;
            ckpt_discard(&ck);
            if (is_punct(p, "=")) { advance(p); init = parse_expr(p); }
            expect_punct(p, ";");
            { CsNode *n = csnode_local_var_decl(t, name, init, 0, line); free(name); return n; }
        }
        cstype_free(t);
        p->error_count = errs_before;
        ckpt_restore(p, &ck);
    }
    {
        CsNode *e = parse_expr(p);
        expect_punct(p, ";");
        return csnode_expr_stmt(e, line);
    }
}

static CsNode *parse_block(CsParser *p) {
    int line = p->cur->line;
    CsNode **stmts = 0; int n = 0, cap = 0;
    expect_punct(p, "{");
    while (!is_punct(p, "}") && !is_eof(p)) {
        CsNode *s = parse_statement(p);
        if (n >= cap) { cap = cap ? cap * 2 : 4; stmts = (CsNode **)realloc(stmts, sizeof(CsNode *) * (unsigned int)cap); }
        stmts[n++] = s;
    }
    expect_punct(p, "}");
    return csnode_block(stmts, n, line);
}

static CsNode *parse_if(CsParser *p) {
    int line = p->cur->line;
    CsNode *cond, *then_, *else_ = 0;
    advance(p); /* if */
    expect_punct(p, "(");
    cond = parse_expr(p);
    expect_punct(p, ")");
    then_ = parse_statement(p);
    if (is_kw(p, "else")) { advance(p); else_ = parse_statement(p); }
    return csnode_if(cond, then_, else_, line);
}

static CsNode *parse_for(CsParser *p) {
    int line = p->cur->line;
    CsNode *init = 0, *cond = 0, *step = 0, *body;
    advance(p); /* for */
    expect_punct(p, "(");
    if (!is_punct(p, ";")) {
        if (is_kw(p, "var") || (looks_like_type_start(p) && !is_ident_tok(p))) {
            int dline = p->cur->line;
            CsType *t = is_kw(p, "var") ? (advance(p), (CsType *)0) : parse_type(p);
            char *name = expect_ident(p);
            CsNode *dinit = 0;
            if (is_punct(p, "=")) { advance(p); dinit = parse_expr(p); }
            init = csnode_local_var_decl(t, name, dinit, t == 0, dline);
            free(name);
        } else if (is_ident_tok(p)) {
            CsCkpt ck; ckpt_save(p, &ck);
            int errs_before = p->error_count;
            int dline = p->cur->line;
            CsType *t = parse_type(p);
            if (p->error_count == errs_before && is_ident_tok(p)) {
                char *name = expect_ident(p);
                CsNode *dinit = 0;
                ckpt_discard(&ck);
                if (is_punct(p, "=")) { advance(p); dinit = parse_expr(p); }
                init = csnode_local_var_decl(t, name, dinit, 0, dline);
                free(name);
            } else {
                cstype_free(t);
                p->error_count = errs_before;
                ckpt_restore(p, &ck);
                init = csnode_expr_stmt(parse_expr(p), dline);
            }
        } else {
            init = csnode_expr_stmt(parse_expr(p), p->cur->line);
        }
    }
    expect_punct(p, ";");
    if (!is_punct(p, ";")) cond = parse_expr(p);
    expect_punct(p, ";");
    if (!is_punct(p, ")")) step = parse_expr(p);
    expect_punct(p, ")");
    body = parse_statement(p);
    return csnode_for(init, cond, step, body, line);
}

static CsNode *parse_foreach(CsParser *p) {
    int line = p->cur->line;
    CsType *t; char *name; CsNode *coll, *body;
    advance(p); /* foreach */
    expect_punct(p, "(");
    t = is_kw(p, "var") ? (advance(p), (CsType *)0) : parse_type(p);
    name = expect_ident(p);
    expect_kw(p, "in");
    coll = parse_expr(p);
    expect_punct(p, ")");
    body = parse_statement(p);
    { CsNode *n = csnode_foreach(t, name, coll, body, line); free(name); return n; }
}

static CsNode *parse_while(CsParser *p) {
    int line = p->cur->line;
    CsNode *cond, *body;
    advance(p);
    expect_punct(p, "(");
    cond = parse_expr(p);
    expect_punct(p, ")");
    body = parse_statement(p);
    return csnode_while(cond, body, line);
}

static CsNode *parse_do_while(CsParser *p) {
    int line = p->cur->line;
    CsNode *body, *cond;
    advance(p); /* do */
    body = parse_statement(p);
    expect_kw(p, "while");
    expect_punct(p, "(");
    cond = parse_expr(p);
    expect_punct(p, ")");
    expect_punct(p, ";");
    return csnode_do_while(body, cond, line);
}

static CsNode *parse_switch(CsParser *p) {
    int line = p->cur->line;
    CsNode *expr;
    CsNode **cases = 0; int n = 0, cap = 0;
    advance(p); /* switch */
    expect_punct(p, "(");
    expr = parse_expr(p);
    expect_punct(p, ")");
    expect_punct(p, "{");
    while (!is_punct(p, "}") && !is_eof(p)) {
        int cline = p->cur->line;
        CsNode **values = 0; int nv = 0, vcap = 0;
        int is_default = 0;
        for (;;) {
            if (is_kw(p, "case")) {
                advance(p);
                { CsNode *v = parse_expr(p);
                  if (nv >= vcap) { vcap = vcap ? vcap * 2 : 4; values = (CsNode **)realloc(values, sizeof(CsNode *) * (unsigned int)vcap); }
                  values[nv++] = v; }
                expect_punct(p, ":");
                continue;
            }
            if (is_kw(p, "default")) { advance(p); expect_punct(p, ":"); is_default = 1; continue; }
            break;
        }
        {
            CsNode **stmts = 0; int ns = 0, scap = 0;
            while (!is_kw(p, "case") && !is_kw(p, "default") && !is_punct(p, "}") && !is_eof(p)) {
                CsNode *s = parse_statement(p);
                if (ns >= scap) { scap = scap ? scap * 2 : 4; stmts = (CsNode **)realloc(stmts, sizeof(CsNode *) * (unsigned int)scap); }
                stmts[ns++] = s;
            }
            if (n >= cap) { cap = cap ? cap * 2 : 4; cases = (CsNode **)realloc(cases, sizeof(CsNode *) * (unsigned int)cap); }
            cases[n++] = csnode_switch_case(values, nv, is_default, stmts, ns, cline);
        }
    }
    expect_punct(p, "}");
    return csnode_switch(expr, cases, n, line);
}

static CsNode *parse_try(CsParser *p) {
    int line = p->cur->line;
    CsNode *try_block;
    CsNode **catches = 0; int n = 0, cap = 0;
    CsNode *finally_block = 0;
    advance(p); /* try */
    try_block = parse_block(p);
    while (is_kw(p, "catch")) {
        int cline = p->cur->line;
        CsType *ex_type = 0; char *var_name = 0; CsNode *body;
        advance(p);
        if (is_punct(p, "(")) {
            advance(p);
            ex_type = parse_type(p);
            if (is_ident_tok(p)) var_name = expect_ident(p);
            expect_punct(p, ")");
        }
        body = parse_block(p);
        if (n >= cap) { cap = cap ? cap * 2 : 4; catches = (CsNode **)realloc(catches, sizeof(CsNode *) * (unsigned int)cap); }
        catches[n++] = csnode_catch_clause(ex_type, var_name, body, cline);
        free(var_name);
    }
    if (is_kw(p, "finally")) { advance(p); finally_block = parse_block(p); }
    return csnode_try(try_block, catches, n, finally_block, line);
}

static CsNode *parse_statement(CsParser *p) {
    int line = p->cur->line;
    if (is_punct(p, "{")) return parse_block(p);
    if (is_kw(p, "if")) return parse_if(p);
    if (is_kw(p, "for")) return parse_for(p);
    if (is_kw(p, "foreach")) return parse_foreach(p);
    if (is_kw(p, "while")) return parse_while(p);
    if (is_kw(p, "do")) return parse_do_while(p);
    if (is_kw(p, "switch")) return parse_switch(p);
    if (is_kw(p, "try")) return parse_try(p);
    if (is_kw(p, "break")) { advance(p); expect_punct(p, ";"); return csnode_break(line); }
    if (is_kw(p, "continue")) { advance(p); expect_punct(p, ";"); return csnode_continue(line); }
    if (is_kw(p, "return")) {
        CsNode *e = 0;
        advance(p);
        if (!is_punct(p, ";")) e = parse_expr(p);
        expect_punct(p, ";");
        return csnode_return(e, line);
    }
    if (is_kw(p, "throw")) {
        CsNode *e = 0;
        advance(p);
        if (!is_punct(p, ";")) e = parse_expr(p);
        expect_punct(p, ";");
        return csnode_throw(e, line);
    }
    if (is_punct(p, ";")) { advance(p); return csnode_block(0, 0, line); } /* empty statement */
    return parse_local_var_or_expr_stmt(p);
}

/* ---- generics: type parameters + where-clauses ---- */

static CsConstraintKind parse_one_constraint(CsParser *p, char **out_base_name) {
    *out_base_name = 0;
    if (is_kw(p, "class")) { advance(p); return CS_CONSTRAINT_CLASS; }
    if (is_kw(p, "struct")) { advance(p); return CS_CONSTRAINT_STRUCT; }
    if (is_kw(p, "new")) { advance(p); expect_punct(p, "("); expect_punct(p, ")"); return CS_CONSTRAINT_NEW; }
    if (is_ident_tok(p)) { *out_base_name = expect_ident(p); return CS_CONSTRAINT_BASE_TYPE; }
    perror_at(p, "expected a generic constraint, got '%s'", p->cur->text ? p->cur->text : "<eof>");
    advance(p);
    return CS_CONSTRAINT_NONE;
}

/* Parses zero or more "where T : c1, c2, ..." clauses that follow a
 * method/class signature, attaching each clause's constraints to the
 * matching CS_TYPE_PARAM node already collected in `type_params`. */
static void parse_where_clauses(CsParser *p, CsNode **type_params, int n_type_params) {
    while (is_kw(p, "where")) {
        char *tp_name; CsNode *target = 0; int i;
        CsConstraint *constraints = 0; int nc = 0, cap = 0;
        advance(p);
        tp_name = expect_ident(p);
        for (i = 0; i < n_type_params; i++) if (strcmp(type_params[i]->type_param.name, tp_name) == 0) { target = type_params[i]; break; }
        expect_punct(p, ":");
        for (;;) {
            char *base_name;
            CsConstraintKind k = parse_one_constraint(p, &base_name);
            if (nc >= cap) { cap = cap ? cap * 2 : 4; constraints = (CsConstraint *)realloc(constraints, sizeof(CsConstraint) * (unsigned int)cap); }
            constraints[nc].kind = k; constraints[nc].base_type_name = base_name; nc++;
            if (is_punct(p, ",")) { advance(p); continue; }
            break;
        }
        if (target) { target->type_param.constraints = constraints; target->type_param.n_constraints = nc; }
        else { int j; for (j = 0; j < nc; j++) free(constraints[j].base_type_name); free(constraints); }
        free(tp_name);
    }
}

static void parse_type_param_list(CsParser *p, CsNode ***out_params, int *out_n) {
    CsNode **tps = 0; int n = 0, cap = 0;
    expect_punct(p, "<");
    while (!is_punct(p, ">") && !is_eof(p)) {
        int line = p->cur->line;
        char *name = expect_ident(p);
        if (n >= cap) { cap = cap ? cap * 2 : 4; tps = (CsNode **)realloc(tps, sizeof(CsNode *) * (unsigned int)cap); }
        tps[n++] = csnode_type_param(name, 0, 0, line);
        free(name);
        if (is_punct(p, ",")) { advance(p); continue; }
        break;
    }
    consume_closing_angle(p);
    *out_params = tps; *out_n = n;
}

/* ---- modifiers ---- */

static int parse_modifiers(CsParser *p) {
    int is_static = 0;
    for (;;) {
        if (is_kw(p, "public") || is_kw(p, "private") || is_kw(p, "protected") || is_kw(p, "internal") ||
            is_kw(p, "readonly") || is_kw(p, "const") || is_kw(p, "virtual") || is_kw(p, "override") ||
            is_kw(p, "abstract") || is_kw(p, "sealed") || is_kw(p, "partial") || is_kw(p, "extern")) {
            advance(p); continue; /* recognized, not tracked -- documented scope simplification */
        }
        if (is_kw(p, "static")) { is_static = 1; advance(p); continue; }
        break;
    }
    return is_static;
}

/* Skips a leading "[Attr(...)]" / "[Attr]" attribute list, if present --
 * parsed and discarded (see cs_ast.h's header comment: real attribute
 * semantics are out of scope for this phase). */
static void skip_attributes(CsParser *p) {
    while (is_punct(p, "[")) {
        int depth = 0;
        do {
            if (is_punct(p, "[")) depth++;
            else if (is_punct(p, "]")) depth--;
            advance(p);
        } while (depth > 0 && !is_eof(p));
    }
}

/* Same as skip_attributes(), but specifically recognizes a single
 * "[DllImport("libname")]" attribute (Phase 6c: P/Invoke-style native
 * declarations, matching .NET's own DllImport spelling for familiarity)
 * among the attribute list and returns its captured library-name string
 * argument (malloc'd, caller frees), or NULL if no DllImport attribute
 * was present. Every other attribute (DllImport or not) is still parsed
 * and discarded exactly like skip_attributes() -- only the one string
 * argument of a bare "DllImport(...)" attribute is captured, not a full
 * attribute-argument grammar (named arguments like "EntryPoint=..." are
 * accepted syntactically -- parse_arg_list handles them like ordinary
 * call arguments -- but not interpreted; see cs_ast.h's own comment on
 * dllimport_name for why the string isn't currently used to pick a
 * specific library). */
static char *skip_attributes_capture_dllimport(CsParser *p) {
    char *lib = 0;
    while (is_punct(p, "[")) {
        int depth = 0;
        int first = 1;
        do {
            if (is_punct(p, "[")) { depth++; advance(p); first = 0; continue; }
            if (is_punct(p, "]")) { depth--; advance(p); continue; }
            if (first == 0 && depth == 1 && !lib && is_ident_tok(p) && strcmp(p->cur->text, "DllImport") == 0) {
                advance(p);
                if (is_punct(p, "(")) {
                    int argc; CsNode **args = parse_arg_list(p, &argc);
                    if (argc > 0 && args[0] && args[0]->kind == CS_LIT_STRING)
                        lib = cs_strdup(args[0]->lit_string.value);
                    { int i; for (i = 0; i < argc; i++) csast_free(args[i]); free(args); }
                }
                continue;
            }
            advance(p);
        } while (depth > 0 && !is_eof(p));
    }
    return lib;
}

/* ---- class/struct/interface members ---- */

static CsNode *parse_class_member(CsParser *p, const char *class_name) {
    int line;
    int is_static;
    CsType *type;
    char *name;
    CsTok *pk = 0;
    int is_ctor;
    char *dllimport_lib = skip_attributes_capture_dllimport(p);
    line = p->cur->line;
    is_static = parse_modifiers(p);

    is_ctor = is_ident_tok(p) && strcmp(p->cur->text, class_name) == 0;
    if (is_ctor) pk = peek_next(p); /* see the array_rank loop's comment on why this is hoisted, not called twice inline */
    is_ctor = is_ctor && pk && pk->kind == CS_TOK_PUNCT && strcmp(pk->text, "(") == 0;
    if (is_ctor) {
        CsNode **params; int n_params; CsNode *body;
        CsNode **base_args = 0; int n_base_args = 0;
        char *cname = expect_ident(p);
        parse_param_list(p, &params, &n_params);
        if (is_punct(p, ":")) {
            /* ": base(args)" -- captured for real now (cs_lower.c runs
             * the base class's own field initializers/ctor body via a
             * generated "<Base>__init(this, args...)" helper, see its
             * own comment) so a derived exception/class ctor's base(...)
             * call actually does something instead of being silently
             * discarded. ": this(args)" (same-class constructor
             * overload chaining) is a separate feature -- still just
             * parsed and discarded, since this pass only supports one
             * constructor per class at all (see lower_class_methods'
             * "constructor overload resolution is not supported yet"
             * check), so there is no second same-class ctor to chain
             * into in the first place. */
            int is_base;
            advance(p);
            is_base = is_kw(p, "base");
            if (is_kw(p, "base") || is_kw(p, "this")) advance(p);
            if (is_punct(p, "(")) {
                int argc; CsNode **args = parse_arg_list(p, &argc);
                if (is_base) { base_args = args; n_base_args = argc; }
                else { int i; for (i=0;i<argc;i++) csast_free(args[i]); free(args); }
            }
        }
        body = parse_block(p);
        free(dllimport_lib); /* DllImport on a constructor isn't a supported shape -- discarded, same as any other unrecognized attribute */
        { CsNode *n = csnode_ctor_decl(cname, params, n_params, body, line); free(cname);
          n->ctor_decl.base_args = base_args; n->ctor_decl.n_base_args = n_base_args;
          return n; }
    }

    if (!looks_like_type_start(p)) {
        perror_at(p, "expected a member declaration, got '%s'", p->cur->text ? p->cur->text : "<eof>");
        recover_to_stmt_end(p);
        free(dllimport_lib);
        return csnode_block(0, 0, line);
    }
    type = parse_type(p);
    name = expect_ident(p);

    if (is_punct(p, "<")) {
        CsNode **tps; int n_tps; CsNode **params; int n_params; CsNode *body = 0;
        parse_type_param_list(p, &tps, &n_tps);
        parse_param_list(p, &params, &n_params);
        parse_where_clauses(p, tps, n_tps);
        if (is_punct(p, "{")) body = parse_block(p);
        else expect_punct(p, ";");
        free(dllimport_lib); /* a generic method as a DllImport target isn't a supported shape -- discarded */
        { CsNode *n = csnode_method_decl(type, name, tps, n_tps, params, n_params, body, is_static, line); free(name); return n; }
    }
    if (is_punct(p, "(")) {
        CsNode **params; int n_params; CsNode *body = 0;
        parse_param_list(p, &params, &n_params);
        if (is_punct(p, "{")) body = parse_block(p);
        else expect_punct(p, ";");
        { CsNode *n = csnode_method_decl(type, name, 0, 0, params, n_params, body, is_static, line);
          /* Phase 6c: "[DllImport("lib")] static extern <ret> Name(...);"
           * -- the method's own NAME is the real native symbol (matching
           * .NET's own default EntryPoint-defaults-to-method-name rule);
           * dllimport_name just needs to be non-NULL for cs_lower.c to
           * route calls straight to it instead of "Class__Name". Stash
           * the captured library string there too (informational -- see
           * cs_ast.h's own comment on why it isn't used to pick a
           * specific .so) so it survives past this function; if no
           * DllImport attribute was present this is simply NULL and the
           * method lowers exactly as before. */
          n->method_decl.dllimport_name = dllimport_lib;
          free(name); return n; }
    }
    if (is_punct(p, "{")) {
        int has_setter = 0;
        free(dllimport_lib); /* a property isn't a supported DllImport target -- discarded */
        advance(p);
        if (is_kw(p, "get")) { advance(p); if (is_punct(p, "=>")) { advance(p); csast_free(parse_expr(p)); } expect_punct(p, ";"); }
        if (is_kw(p, "set")) { has_setter = 1; advance(p); if (is_punct(p, "=>")) { advance(p); csast_free(parse_expr(p)); } expect_punct(p, ";"); }
        expect_punct(p, "}");
        { CsNode *n = csnode_property_decl(type, name, is_static, has_setter, line); free(name); return n; }
    }
    if (is_punct(p, "=>")) {
        /* expression-bodied property getter: "T Name => expr;" */
        CsNode *e; advance(p);
        e = parse_expr(p);
        expect_punct(p, ";");
        csast_free(e);
        free(dllimport_lib); /* a property isn't a supported DllImport target -- discarded */
        { CsNode *n = csnode_property_decl(type, name, is_static, 0, line); free(name); return n; }
    }
    {
        CsNode *init = 0;
        if (is_punct(p, "=")) { advance(p); init = parse_assignment(p); }
        expect_punct(p, ";");
        free(dllimport_lib); /* a field isn't a supported DllImport target -- discarded */
        { CsNode *n = csnode_field_decl(type, name, init, is_static, line); free(name); return n; }
    }
}

static CsNode *parse_type_decl(CsParser *p) {
    int line;
    CsAstKind kind;
    char *name;
    char *base_class_name = 0;
    char **interfaces = 0; int n_interfaces = 0;
    CsNode **type_params = 0; int n_type_params = 0;
    CsNode **members = 0; int n_members = 0, mcap = 0;
    int is_static;

    skip_attributes(p);
    line = p->cur->line;
    is_static = parse_modifiers(p);

    if (is_kw(p, "class")) kind = CS_CLASS_DECL;
    else if (is_kw(p, "struct")) kind = CS_STRUCT_DECL;
    else if (is_kw(p, "interface")) kind = CS_INTERFACE_DECL;
    else {
        perror_at(p, "expected 'class', 'struct', or 'interface', got '%s'", p->cur->text ? p->cur->text : "<eof>");
        recover_to_stmt_end(p);
        return 0;
    }
    advance(p);
    name = expect_ident(p);
    if (is_punct(p, "<")) parse_type_param_list(p, &type_params, &n_type_params);
    if (is_punct(p, ":")) {
        int cap = 0;
        advance(p);
        for (;;) {
            char *iname = expect_ident(p);
            if (is_punct(p, "<")) { CsType **a; int an; parse_type_arg_list(p, &a, &an); { int i; for (i=0;i<an;i++) cstype_free(a[i]); free(a); } }
            if (!base_class_name && n_interfaces == 0) base_class_name = iname;
            else {
                if (n_interfaces >= cap) { cap = cap ? cap * 2 : 4; interfaces = (char **)realloc(interfaces, sizeof(char *) * (unsigned int)cap); }
                interfaces[n_interfaces++] = iname;
            }
            if (is_punct(p, ",")) { advance(p); continue; }
            break;
        }
    }
    parse_where_clauses(p, type_params, n_type_params);
    expect_punct(p, "{");
    while (!is_punct(p, "}") && !is_eof(p)) {
        CsNode *m = parse_class_member(p, name);
        if (mcap <= n_members) { mcap = mcap ? mcap * 2 : 8; members = (CsNode **)realloc(members, sizeof(CsNode *) * (unsigned int)mcap); }
        members[n_members++] = m;
    }
    expect_punct(p, "}");
    { CsNode *n = csnode_class_decl(kind, name, base_class_name, interfaces, n_interfaces, type_params, n_type_params, members, n_members, is_static, line);
      free(name); free(base_class_name); return n; }
}

/* ---- top level ---- */

static int looks_like_type_decl_start(CsParser *p) {
    CsCkpt ck; ckpt_save(p, &ck);
    int found;
    parse_modifiers(p);
    found = is_kw(p, "class") || is_kw(p, "struct") || is_kw(p, "interface");
    ckpt_restore(p, &ck);
    return found;
}

static CsNode *parse_namespace(CsParser *p) {
    int line = p->cur->line;
    char *name;
    CsNode **decls; int n;
    advance(p); /* namespace */
    name = expect_ident(p);
    while (is_punct(p, ".")) { advance(p); { char *seg = expect_ident(p); char *joined; joined = (char*)malloc(strlen(name)+strlen(seg)+2); sprintf(joined, "%s.%s", name, seg); free(name); free(seg); name = joined; } }
    expect_punct(p, "{");
    { int cap = 0; n = 0; decls = 0;
      while (!is_punct(p, "}") && !is_eof(p)) {
          skip_attributes(p);
          if (looks_like_type_decl_start(p)) {
              CsNode *d = parse_type_decl(p);
              if (d) { if (n >= cap) { cap = cap?cap*2:8; decls = (CsNode**)realloc(decls, sizeof(CsNode*)*(unsigned int)cap); } decls[n++] = d; }
          } else {
              perror_at(p, "expected a type declaration inside namespace, got '%s'", p->cur->text ? p->cur->text : "<eof>");
              recover_to_stmt_end(p);
          }
      }
    }
    expect_punct(p, "}");
    { CsNode *ns = csnode_namespace(name, decls, n, line); free(name); return ns; }
}

CsNode *cs_parse_unit(CsParser *p) {
    int line = p->cur->line;
    CsNode **usings = 0; int n_usings = 0, ucap = 0;
    CsNode **decls = 0; int n_decls = 0, dcap = 0;

    while (is_kw(p, "using")) {
        int uline = p->cur->line;
        char *name;
        advance(p);
        name = expect_ident(p);
        while (is_punct(p, ".")) { advance(p); { char *seg = expect_ident(p); char *joined = (char*)malloc(strlen(name)+strlen(seg)+2); sprintf(joined, "%s.%s", name, seg); free(name); free(seg); name = joined; } }
        expect_punct(p, ";");
        if (n_usings >= ucap) { ucap = ucap ? ucap * 2 : 8; usings = (CsNode **)realloc(usings, sizeof(CsNode *) * (unsigned int)ucap); }
        usings[n_usings++] = csnode_using(name, uline);
        free(name);
    }

    while (!is_eof(p)) {
        skip_attributes(p);
        if (is_kw(p, "namespace")) {
            CsNode *ns = parse_namespace(p);
            if (dcap <= n_decls) { dcap = dcap ? dcap * 2 : 8; decls = (CsNode **)realloc(decls, sizeof(CsNode *) * (unsigned int)dcap); }
            decls[n_decls++] = ns;
            continue;
        }
        if (looks_like_type_decl_start(p)) {
            CsNode *d = parse_type_decl(p);
            if (d) {
                if (dcap <= n_decls) { dcap = dcap ? dcap * 2 : 8; decls = (CsNode **)realloc(decls, sizeof(CsNode *) * (unsigned int)dcap); }
                decls[n_decls++] = d;
            }
            continue;
        }
        perror_at(p, "expected 'namespace', 'class', 'struct', or 'interface' at top level, got '%s'", p->cur->text ? p->cur->text : "<eof>");
        recover_to_stmt_end(p);
    }

    return csnode_unit(usings, n_usings, decls, n_decls, line);
}
