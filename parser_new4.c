#include "parser.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* =========================================================================
 * Helpers
 * ========================================================================= */
/* cur: macro for direct access — avoids large struct return ABI issue */
#define cur(p) ((p)->lex->cur)
static void adv(Parser *p) { lexer_next(p->lex); }
static int  adv_kind(Parser *p) { adv(p); return (int)p->lex->cur.kind; }
static int    chk(Parser *p, TokenKind k) { return lexer_check(p->lex, k); }
static void eat(Parser *p, TokenKind k) { lexer_expect_void(p->lex, k); }

/* p->filename is only ever the TOP-level file squash was invoked on — real
 * parse errors routinely come from deep inside a #include-flattened header
 * (SDL3, the Windows SDK, ...), so that alone is often just wrong. diag_emit
 * resolves the token's own ->line (a position in the fully preprocessed,
 * #include-merged buffer) back to the real originating file via the line
 * map built during preprocessing — see diag.c / lexer.c's linemap_resolve.
 * The offending token's own text is passed as the caret target, giving a
 * real GCC-style "here" pointer under the exact spot, not just a line. */
void parse_error(Parser *p, const char *msg) {
    Token t = cur(p);
    char tokbuf[256];
    int tl = t.len; if (tl > (int)sizeof(tokbuf)-1) tl = (int)sizeof(tokbuf)-1;
    if (tl > 0) snprintf(tokbuf, sizeof tokbuf, "%.*s", tl, t.start); else tokbuf[0] = '\0';
    diag_emit(DIAG_ERROR, t.line, NULL, tokbuf[0] ? tokbuf : NULL, "%s", msg);
    p->error_count++;
    if (p->error_count > 20) { diag_emit(DIAG_ERROR, -1, NULL, NULL, "too many errors, aborting"); exit(1); }
}

void parse_warn(Parser *p, const char *msg) {
    Token t = cur(p);
    char tokbuf[256];
    int tl = t.len; if (tl > (int)sizeof(tokbuf)-1) tl = (int)sizeof(tokbuf)-1;
    if (tl > 0) snprintf(tokbuf, sizeof tokbuf, "%.*s", tl, t.start); else tokbuf[0] = '\0';
    diag_emit(DIAG_WARNING, t.line, NULL, tokbuf[0] ? tokbuf : NULL, "%s", msg);
}

static char *tok_ident(Token *t) {
    static char buf[256];
    snprintf(buf,sizeof buf,"%.*s",t->len,t->start);
    return buf;
}

/* Skip a calling-convention keyword (__stdcall, __cdecl, __fastcall,
 * __thiscall, __vectorcall) between a return type and a declarator name,
 * if present. These only matter for 32-bit x86 (x64 has one uniform ABI),
 * so squash just discards them — but the token still needs to be consumed
 * or the declarator parser misreads it as the actual name. WINAPI/CALLBACK
 * (used throughout Windows headers like wgl.h) are macros that expand to
 * one of these or nothing depending on _WIN32, so by the time the parser
 * sees this it's already a plain identifier token, indistinguishable from
 * a real declarator name except by spelling. */
static void skip_calling_convention(Parser *p) {
    if (chk(p,TOK_IDENT)) {
        const char *id = tok_ident(&cur(p));
        if (!strcmp(id,"__stdcall")||!strcmp(id,"__cdecl")||!strcmp(id,"__fastcall")||
            !strcmp(id,"__thiscall")||!strcmp(id,"__vectorcall")) {
            adv(p);
        }
    }
}

/* Constant integer array-dimension expressions, e.g. `data[4 * 4 * 4]`.
 * Squash has no general constant-folding pass, so a declarator's array
 * size — which must be a single int by the time it reaches ast_var_decl —
 * needs the arithmetic evaluated right here at parse time. Handles NUMBER,
 * unary '-', parens, and * / % over + -; anything fancier (sizeof, enum
 * constants, etc.) isn't needed by any array dimension seen so far. */
static int parse_const_dim_expr(Parser *p);
static int parse_const_or_expr(Parser *p);
static int parse_const_expr(Parser *p);
static int parse_const_dim_factor(Parser *p);
static int is_type_start(Parser *p); /* forward decl: defined further down this file */
static void parse_flatten_brace_elem(Parser *p, ASTNode ***elems, int *ne, int *ecap); /* forward decl: defined further down this file */
/* Constant "offsetof"-style address expressions, e.g. real Windows SDK
 * headers' compile-time layout assert idiom:
 *   typedef char __C_ASSERT__[(((LONG)(LONG_PTR)&(((T*)0)->Field)) == 0)?1:-1];
 * This constant evaluator has no general lvalue/address-of machinery (it
 * only ever folds plain integers), so this implements just enough of real
 * offsetof semantics to cover it: walk a chain of "->field", ".field", and
 * "[const-index]" postfix accesses rooted at a "(TYPE *)<expr>" cast,
 * tracking a running byte offset via the same field-layout logic codegen
 * uses for real member access (symtable_field_offset, shared with
 * symtable_compute_struct_size so the two can never disagree). */
typedef struct {
    int accum;      /* running byte offset */
    int valid;      /* 0 once the chain hits something it can't resolve */
    TypeInfo *cur;  /* type of the expression at this point, for further chaining */
    int elemsz;     /* per-element size of 'cur', for a following "[idx]" */
} ConstOffsetVal;

static ConstOffsetVal parse_const_offset_chain(Parser *p);

static ConstOffsetVal parse_const_offset_primary(Parser *p) {
    ConstOffsetVal v; v.accum=0; v.valid=0; v.cur=NULL; v.elemsz=0;
    if (chk(p,TOK_LPAREN)) {
        adv(p);
        if (is_type_start(p)) {
            /* "(TYPE *)<expr>" — the offsetof idiom's base pointer. The
             * operand (almost always the literal 0) is discarded: only the
             * resulting pointer's type matters for computing the offset. */
            TypeInfo *ti = ParseTypeSpecifier(p);
            while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
            eat(p,TOK_RPAREN);
            (void)parse_const_dim_factor(p);
            v.cur = ti;
            v.valid = (ti->pointer_depth > 0);
            return v;
        }
        v = parse_const_offset_chain(p);
        eat(p,TOK_RPAREN);
        return v;
    }
    parse_error(p,"expected constant address expression");
    return v;
}

static ConstOffsetVal parse_const_offset_chain(Parser *p) {
    ConstOffsetVal v = parse_const_offset_primary(p);
    for (;;) {
        if (chk(p,TOK_ARROW) || chk(p,TOK_DOT)) {
            int arrow = chk(p,TOK_ARROW);
            adv(p);
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected field name in constant offset expression"); v.valid=0; break; }
            char fname[256]; snprintf(fname,sizeof fname,"%s",tok_ident(&cur(p)));
            adv(p);
            if (!v.valid || !v.cur || !v.cur->base) { v.valid = 0; continue; }
            int have_ptr = v.cur->pointer_depth > 0;
            if ((arrow && !have_ptr) || (!arrow && have_ptr)) { v.valid = 0; continue; }
            ASTNode *sn = symtable_resolve_struct_node(p->sym, v.cur->base);
            if (!sn) { v.valid = 0; continue; }
            TypeInfo *ftype = NULL; int elemsz = 0;
            int foff = symtable_field_offset(p->sym, sn, fname, &ftype, &elemsz);
            if (foff < 0) { v.valid = 0; continue; }
            v.accum += foff;
            v.cur = ftype;
            v.elemsz = elemsz;
        } else if (chk(p,TOK_LBRACKET)) {
            adv(p);
            int idx = parse_const_expr(p);
            eat(p,TOK_RBRACKET);
            if (!v.valid || !v.cur) continue;
            int elemsz = v.elemsz;
            if (elemsz <= 0) {
                elemsz = (v.cur->pointer_depth > 0) ? 1 : typeinfo_size(v.cur, p->sym->is_64bit);
                if (v.cur->pointer_depth == 0 && v.cur->base) {
                    ASTNode *sn = symtable_resolve_struct_node(p->sym, v.cur->base);
                    if (sn) elemsz = symtable_sizeof_struct(p->sym, sn);
                }
            }
            v.accum += idx * elemsz;
        } else break;
    }
    return v;
}

static int parse_const_dim_factor(Parser *p) {
    if (chk(p,TOK_MINUS)) { adv(p); return -parse_const_dim_factor(p); }
    if (chk(p,TOK_PLUS))  { adv(p); return parse_const_dim_factor(p); }
    if (chk(p,TOK_TILDE)) { adv(p); return ~parse_const_dim_factor(p); }
    if (chk(p,TOK_BANG))  { adv(p); return !parse_const_dim_factor(p); }
    if (chk(p,TOK_AMP)) {
        adv(p);
        ConstOffsetVal v = parse_const_offset_chain(p);
        if (!v.valid) { parse_error(p,"expected constant in array size"); return 1; }
        return v.accum;
    }
    if (chk(p,TOK_SIZEOF)) {
        adv(p);
        int has_paren = chk(p,TOK_LPAREN);
        if (has_paren) adv(p);
        if (has_paren && is_type_start(p)) {
            TypeInfo *ti = ParseTypeSpecifier(p);
            while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
            eat(p,TOK_RPAREN);
            int sz = typeinfo_size(ti, p->sym->is_64bit);
            typeinfo_free(ti);
            return sz;
        }
        /* sizeof(expr) or sizeof expr — this simple constant evaluator
         * doesn't do expression type-inference, so just skip past the
         * operand and fall back to a plausible size. Array dimensions,
         * enum values, and case labels essentially never use sizeof of a
         * non-type expression, so this is a rare, low-stakes fallback. */
        if (has_paren) {
            int depth=1;
            while (!chk(p,TOK_EOF) && depth>0) {
                if (chk(p,TOK_LPAREN)) depth++;
                else if (chk(p,TOK_RPAREN)) depth--;
                adv(p);
            }
        } else {
            (void)parse_const_dim_factor(p);
        }
        return (int)sizeof(int);
    }
    if (chk(p,TOK_ALIGNOF)) {
        /* __alignof(TYPE)/_Alignof(TYPE)/alignof(TYPE) inside a constant
         * array-size/static-assert expression — e.g. real NDIS headers'
         * "typedef char __C_ASSERT__[(sizeof(X) % __alignof(Y) == 0)?1:
         * -1];". Unlike the runtime expression parser's simplified
         * "same as sizeof" treatment (see lexer.c's __alignof keyword
         * fix), this constant evaluator already has real struct-alignment
         * machinery on hand (symtable_compute_struct_alignment, built for
         * the offsetof support above) — use it for a struct/union operand
         * instead of just reusing sizeof, so e.g. a struct whose true
         * alignment differs from its size (FILETIME-shaped: two 4-byte
         * members, size 8, alignment 4) computes correctly here even
         * though the runtime-expression path doesn't yet. */
        adv(p);
        eat(p,TOK_LPAREN);
        TypeInfo *ti = ParseTypeSpecifier(p);
        while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
        eat(p,TOK_RPAREN);
        int align = 4;
        if (ti->pointer_depth > 0) {
            align = p->sym->is_64bit ? 8 : 4;
        } else if (ti->base) {
            ASTNode *sn = symtable_resolve_struct_node(p->sym, ti->base);
            if (sn) align = symtable_compute_struct_alignment(p->sym, sn);
            else align = typeinfo_size(ti, p->sym->is_64bit);
        }
        typeinfo_free(ti);
        if (align < 1) align = 1;
        return align;
    }
    if (chk(p,TOK_LPAREN)) {
        adv(p);
        if (is_type_start(p)) {
            /* Cast expression, e.g. "(unsigned)(x)" inside a FourCC-style
             * packed-integer macro (STBI__PNG_TYPE('I','H','D','R') and
             * friends, used as PNG/JPEG chunk-type case labels) — this
             * constant evaluator has no real type/width model, so the
             * cast's target type is parsed and discarded; only the
             * operand's value matters for every real-world use of a cast
             * inside a case label / array dim / enum value seen so far. */
            TypeInfo *ti = ParseTypeSpecifier(p);
            while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
            typeinfo_free(ti);
            eat(p,TOK_RPAREN);
            return parse_const_dim_factor(p);
        }
        int v=parse_const_expr(p); eat(p,TOK_RPAREN); return v;
    }
    if (chk(p,TOK_NUMBER)) { int v=(int)cur(p).ival; adv(p); return v; }
    if (chk(p,TOK_CHAR_LIT)) { int v=(int)cur(p).ival; adv(p); return v; }
    if (chk(p,TOK_IDENT)) {
        /* Enum-constant reference (e.g. inside another enum's initializer,
         * or a bitmask expression like SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL). */
        Symbol *s = symtable_lookup(p->sym, tok_ident(&cur(p)));
        int v = (s && s->kind==SYM_ENUM_VAL) ? (int)s->enum_value : 0;
        adv(p);
        return v;
    }
    parse_error(p,"expected constant in array size");
    return 1;
}
static int parse_const_dim_term(Parser *p) {
    int v = parse_const_dim_factor(p);
    while (chk(p,TOK_STAR)||chk(p,TOK_SLASH)||chk(p,TOK_PERCENT)) {
        TokenKind k=cur(p).kind; adv(p);
        int r = parse_const_dim_factor(p);
        if (k==TOK_STAR) v*=r;
        else if (k==TOK_SLASH) v = r ? v/r : v;
        else v = r ? v%r : v;
    }
    return v;
}
static int parse_const_dim_expr(Parser *p) {
    int v = parse_const_dim_term(p);
    while (chk(p,TOK_PLUS)||chk(p,TOK_MINUS)) {
        TokenKind k=cur(p).kind; adv(p);
        int r = parse_const_dim_term(p);
        if (k==TOK_PLUS) v+=r; else v-=r;
    }
    return v;
}
/* Bitwise/shift levels, looser-binding than +/-, matching C precedence.
 * Needed for enum initializers like "A | B" or "1 << 4" that plain array
 * dimensions never used, but which are routine in SDL's flag-style enums.
 * parse_const_dim_expr (above) stays the array-dimension entry point;
 * this widens the grammar available via parse_const_dim_factor's parens. */
static int parse_const_shift_expr(Parser *p) {
    int v = parse_const_dim_expr(p);
    while (chk(p,TOK_LSHIFT)||chk(p,TOK_RSHIFT)) {
        TokenKind k=cur(p).kind; adv(p);
        int r = parse_const_dim_expr(p);
        if (k==TOK_LSHIFT) v<<=r; else v>>=r;
    }
    return v;
}
static int parse_const_rel_expr(Parser *p) {
    int v = parse_const_shift_expr(p);
    while (chk(p,TOK_LT)||chk(p,TOK_GT)||chk(p,TOK_LE)||chk(p,TOK_GE)) {
        TokenKind k=cur(p).kind; adv(p);
        int r = parse_const_shift_expr(p);
        if (k==TOK_LT) v = v<r; else if (k==TOK_GT) v = v>r;
        else if (k==TOK_LE) v = v<=r; else v = v>=r;
    }
    return v;
}
static int parse_const_eq_expr(Parser *p) {
    int v = parse_const_rel_expr(p);
    while (chk(p,TOK_EQ)||chk(p,TOK_NEQ)) {
        TokenKind k=cur(p).kind; adv(p);
        int r = parse_const_rel_expr(p);
        v = (k==TOK_EQ) ? (v==r) : (v!=r);
    }
    return v;
}
static int parse_const_and_expr(Parser *p) {
    int v = parse_const_eq_expr(p);
    while (chk(p,TOK_AMP)) { adv(p); v &= parse_const_eq_expr(p); }
    return v;
}
static int parse_const_xor_expr(Parser *p) {
    int v = parse_const_and_expr(p);
    while (chk(p,TOK_CARET)) { adv(p); v ^= parse_const_and_expr(p); }
    return v;
}
static int parse_const_or_expr(Parser *p) {
    int v = parse_const_xor_expr(p);
    while (chk(p,TOK_PIPE)) { adv(p); v |= parse_const_xor_expr(p); }
    return v;
}
/* Logical &&/|| and the ternary conditional sit above bitwise-or in real C
 * precedence, and top off the constant-expression grammar: real-world
 * headers routinely use the "typedef char check[COND ? 1 : -1];"
 * compile-time-assert idiom (e.g. stb_image.h's
 * "validate_uint32[sizeof(x)==4 ? 1 : -1]"), which needs sizeof/==/?: all
 * at the top-level array-dimension entry point, not just inside parens. */
static int parse_const_logand_expr(Parser *p) {
    int v = parse_const_or_expr(p);
    while (chk(p,TOK_AND)) { adv(p); int r = parse_const_or_expr(p); v = v && r; }
    return v;
}
static int parse_const_logor_expr(Parser *p) {
    int v = parse_const_logand_expr(p);
    while (chk(p,TOK_OR)) { adv(p); int r = parse_const_logand_expr(p); v = v || r; }
    return v;
}
static int parse_const_expr(Parser *p) {
    int v = parse_const_logor_expr(p);
    if (chk(p,TOK_QUESTION)) {
        adv(p);
        int a = parse_const_expr(p);
        eat(p,TOK_COLON);
        int b = parse_const_expr(p);
        v = v ? a : b;
    }
    return v;
}

/* Array-typedef declarator: "typedef TYPE name[EXPR];" (e.g. stb_image.h's
 * compile-time-assert idiom "typedef unsigned char check[sizeof(x)==4 ? 1 : -1];").
 * Neither of this file's two typedef-parsing sites checked for a trailing
 * '[' at all before this fix — the bracket was left unconsumed, breaking the
 * parse at the following token. Mirrors the existing 2D array_size/array_size2
 * cap used by regular variable declarators; any dimensions beyond 2D are
 * consumed (so the parse still succeeds) but not separately modeled. */
static void parse_typedef_array_dims(Parser *p, TypeInfo *ti) {
    if (!chk(p,TOK_LBRACKET)) return;
    adv(p);
    int dim = 0;
    if (!chk(p,TOK_RBRACKET)) dim = parse_const_expr(p);
    eat(p,TOK_RBRACKET);
    ti->array_size = dim;
    if (chk(p,TOK_LBRACKET)) {
        adv(p);
        int dim2 = 0;
        if (!chk(p,TOK_RBRACKET)) dim2 = parse_const_expr(p);
        eat(p,TOK_RBRACKET);
        ti->array_size2 = dim2;
    }
    while (chk(p,TOK_LBRACKET)) {
        adv(p);
        while (!chk(p,TOK_RBRACKET) && !chk(p,TOK_EOF)) adv(p);
        eat(p,TOK_RBRACKET);
    }
}

/* =========================================================================
 * parser_init
 * ========================================================================= */
void parser_init(Parser *p, Lexer *l, SymTable *sym, const char *filename) {
    p->lex=l; p->sym=sym; p->filename=filename; p->error_count=0; p->anon_counter=0;
    p->cap_pending=16; p->n_pending=0; p->compound_lit_counter=0;
    p->expr_depth=0; p->block_depth=0;
    p->pending_stmts=(ASTNode**)malloc((size_t)p->cap_pending*sizeof(ASTNode*));

    /* wchar_t is a real compiler-builtin type in MSVC (not typedef'd via
     * any header at all, in either C or C++ mode) — real Windows SDK
     * headers rely on it already being available by the time winnt.h's own
     * "typedef wchar_t WCHAR;" is reached, without ever #include-ing
     * whatever header would normally provide it. squash has no such
     * language-level builtin, so seed it here as an ordinary typedef
     * (matching squash's own include/stddef.h and include/wchar.h, both
     * "typedef unsigned short wchar_t;" — real Windows wchar_t is 16-bit,
     * matching WCHAR/UTF-16, not the 32-bit wchar_t glibc/Linux headers
     * use) so it's already resolvable no matter what a given translation
     * unit does or doesn't #include. squash's own stddef.h/wchar.h are
     * still included normally elsewhere and simply redefine the same name
     * to the same type — harmless. */
    { TypeInfo *wt = typeinfo_new("short"); wt->is_unsigned = 1;
      symtable_define_typedef(sym, "wchar_t", wt); }
}

/* Queue a statement (a synthetic compound-literal backing declaration) to
 * be spliced in immediately before whatever statement is currently being
 * parsed — see the Parser struct's pending_stmts comment in parser.h. */
static void parser_push_pending(Parser *p, ASTNode *stmt) {
    if (p->n_pending >= p->cap_pending) {
        p->cap_pending *= 2;
        p->pending_stmts = (ASTNode**)realloc(p->pending_stmts, (size_t)p->cap_pending*sizeof(ASTNode*));
    }
    p->pending_stmts[p->n_pending++] = stmt;
}

/* =========================================================================
 * Type parsing
 * ========================================================================= */

/* is current token a type keyword or typedef name? */
static int is_type_start(Parser *p) {
    TokenKind k = cur(p).kind;
    if (tok_is_type(k)) return 1;
    /* C11+ qualifier-only tokens that appear before a type */
    if (k==TOK_NORETURN || k==TOK_TYPEOF || k==TOK_ALIGNAS ||
        k==TOK_THREAD_LOCAL || k==TOK_CONSTEXPR) return 1;
    /* check if it's a typedef'd name */
    if (k==TOK_IDENT) {
        Symbol *s = symtable_lookup(p->sym, tok_ident(&cur(p)));
        if (s && s->kind==SYM_TYPEDEF) return 1;
    }
    return 0;
}

TypeInfo *ParseTypeSpecifier(Parser *p) {
    TypeInfo *ti = calloc(1, sizeof(TypeInfo));
    ti->array_size = -1;

    /* storage-class qualifiers and C11+ attributes: eaten, not stored in TypeInfo */
    for (;;) {
        TokenKind _k = cur(p).kind;
        if (_k==TOK_CONST||_k==TOK_VOLATILE||_k==TOK_SIGNED) { adv(p); continue; }
        if (_k==TOK_UNSIGNED) { ti->is_unsigned=1; adv(p); continue; }
        /* C11+ ignored qualifiers */
        if (_k==TOK_CONSTEXPR||_k==TOK_NORETURN||_k==TOK_THREAD_LOCAL||_k==TOK_ALIGNAS) {
            adv(p);
            /* _Alignas/_alignas: consume (expr) argument if present */
            if (_k==TOK_ALIGNAS && chk(p,TOK_LPAREN)) {
                adv(p); int d=1;
                while (!chk(p,TOK_EOF)&&d>0) {
                    if (chk(p,TOK_LPAREN)) d++;
                    else if (chk(p,TOK_RPAREN)) d--;
                    adv(p);
                }
            }
            continue;
        }
        break;
    }

    TokenKind k = cur(p).kind;

    /* typeof(expr) / typeof(type) — deduce type from expression */
    if (k==TOK_TYPEOF) {
        adv(p);
        eat(p,TOK_LPAREN);
        TypeInfo *inner = NULL;
        if (is_type_start(p)) {
            inner = ParseFullType(p);
        } else {
            /* typeof(expr): use int as a simplified fallback for now */
            int depth=1;
            while (!chk(p,TOK_EOF)&&depth>0) {
                if (chk(p,TOK_LPAREN)) depth++;
                else if (chk(p,TOK_RPAREN)) depth--;
                if (depth>0) adv(p);
            }
            inner = typeinfo_new("int");
        }
        eat(p,TOK_RPAREN);
        if (!inner) { inner = typeinfo_new("int"); inner->array_size = -1; }
        free(ti);
        return inner;
    }

    /* _Bool: a real 1-byte type in C — must NOT be mapped to "int" (4
     * bytes) here. This hardcoded "int" mapping was the actual root cause
     * of every "bool" struct field being 4x too large (typeinfo_size()'s
     * own "bool"/"_Bool" cases, see ast.c, never even got a chance to run:
     * stdbool.h's "typedef _Bool bool;" parses the right-hand "_Bool" via
     * this exact TOK_BOOL branch, so the "bool" typedef's underlying type
     * was silently "int" all along, regardless of any fix to the
     * string-based size checks elsewhere). */
    if (k==TOK_BOOL) { adv(p); ti->base=my_strdup("_Bool"); return ti; }

    if (k==TOK_STRUCT || k==TOK_UNION) {
        int is_union = (k==TOK_UNION);
        adv(p);
        char sname[256]=""; int has_name=0;
        if (chk(p,TOK_IDENT)) {
            strncpy(sname, tok_ident(&cur(p)), sizeof sname-1);
            has_name=1; adv(p);
        }
        if (chk(p,TOK_LBRACE)) {
            /* struct definition inline */
            adv(p);
            /* Dynamically-grown field list: real-world structs (e.g. SDL3's
             * own "struct SDL_Window", 68 fields with several packed onto
             * one line as "int x, y;") routinely exceed a small fixed cap —
             * a fixed-size stack array silently overflowing past its bound
             * doesn't just corrupt this struct's field list, it corrupts
             * this function's OTHER stack locals (including nf itself),
             * making the parser appear to "run away" and swallow every
             * following top-level declaration as more of the same struct's
             * fields, eventually dereferencing garbage and crashing deep in
             * codegen — confirmed via a debug build with array-index
             * assertions. */
            int fields_cap = 64;
            ASTNode **fields = (ASTNode**)malloc(fields_cap*sizeof(ASTNode*));
            int nf=0;
            while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                TypeInfo *ft = ParseFullType(p);
                if (!ft) { parse_error(p,"expected field type"); break; }
                /* ParseFullType() already consumed the FIRST declarator's own
                 * leading pointer stars directly into ft->pointer_depth (it
                 * has no way to know it's about to be reused across a
                 * comma-separated declarator list) — this must be reflected
                 * as the "declarator 1" case, and every SUBSEQUENT declarator
                 * needs its OWN star count rebuilt from a clean, zero-pointer
                 * copy of the base type, exactly like ParseVariable's own
                 * multi-declarator fix does for local variables (see its
                 * "base_ptr_depth" comment). Without this, declarator 2+
                 * inherits declarator 1's already-applied pointer_depth ON
                 * TOP of its own stars — one level too deep. Confirmed via
                 * miniz.h's real "mz_uint8 *m_pLZ_code_buf, *m_pLZ_flags,
                 * *m_pOutput_buf, *m_pOutput_buf_end;" (tdefl_compressor):
                 * every field after the first came out pointer_depth==2
                 * instead of 1, corrupting pointer-subtraction/pointee-size
                 * scaling for any expression using one of them. */
                int first_declarator = 1;
                do {
                    /* field might have pointer stars, possibly with a
                     * const/volatile qualifier on an inner pointer level
                     * (e.g. "Type * const *field;" — pointer to const
                     * pointer to Type). */
                    TypeInfo *ft2;
                    if (first_declarator) {
                        ft2 = ft;
                        first_declarator = 0;
                    } else {
                        ft2 = (TypeInfo*)calloc(1, sizeof(TypeInfo));
                        *ft2 = *ft;
                        if (ft->base) ft2->base = my_strdup(ft->base);
                        ft2->pointer_depth = 0;
                    }
                    while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) {
                        if (chk(p,TOK_STAR)) ft2=typeinfo_ptr(ft2);
                        adv(p);
                    }
                    char fn[64];
                    if (chk(p,TOK_LPAREN)) {
                        /* Function-pointer field: Type ( *name)(params);
                         * (e.g. SDL_IOStreamInterface's "Sint64 (*size)(void *userdata);").
                         * Same shape as the function-pointer variable declaration
                         * handled in ParseDeclaration — treated as a plain pointer
                         * field of the return type, matching how codegen already
                         * treats function-pointer variables/calls elsewhere. */
                        adv(p); /* consume '(' */
                        skip_calling_convention(p); /* e.g. "Sint64 (__cdecl *size)(void *userdata);" */
                        while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) ft2=typeinfo_ptr(ft2); adv(p); }
                        if (chk(p,TOK_IDENT)) {
                            strncpy(fn, tok_ident(&cur(p)), sizeof fn-1);
                            fn[sizeof fn-1]='\0';
                            adv(p);
                        } else {
                            snprintf(fn, sizeof fn, "__anon_%d", p->anon_counter++);
                        }
                        eat(p,TOK_RPAREN);
                        /* skip the parameter list: (...) */
                        if (chk(p,TOK_LPAREN)) {
                            adv(p);
                            int depth=1;
                            while (!chk(p,TOK_EOF) && depth>0) {
                                if (chk(p,TOK_LPAREN)) depth++;
                                else if (chk(p,TOK_RPAREN)) depth--;
                                adv(p);
                            }
                        }
                    } else if (!chk(p,TOK_IDENT)) {
                        /* Anonymous struct/union member (C11): no field name.
                         * Give it a hidden name "__anon_N" so struct is valid.
                         * Also covers an anonymous BITFIELD ("int :7;" — pure
                         * padding, extremely common in real Windows headers
                         * like winnt.h's IMAGE_ARCHITECTURE_HEADER): a bare
                         * ": width" with no name, on an ordinary (non-struct/
                         * union) base type, is only valid C when a bitfield
                         * follows — give it the same hidden name so the
                         * existing bitfield-width parsing below still fires. */
                        /* Real MSVC also allows embedding via a plain
                         * TYPEDEF NAME with no declarator at all — a
                         * non-standard extension real headers use, e.g.
                         * iphlpapi.h's "struct _MIB_IPDESTROW {
                         * MIB_IPFORWARDROW; ... };" (MIB_IPFORWARDROW is an
                         * ordinary "typedef struct _MIB_IPFORWARDROW {...}
                         * MIB_IPFORWARDROW;" elsewhere, not a struct/union
                         * keyword here) meaning "embed all of
                         * MIB_IPFORWARDROW's fields here". Recognize this
                         * by resolving the typedef and checking whether it
                         * ultimately names a struct/union. */
                        int is_typedef_struct_embed = ft && ft->pointer_depth==0 && ft->base &&
                            strncmp(ft->base,"struct ",7)!=0 && strncmp(ft->base,"union ",6)!=0 &&
                            symtable_resolve_struct_node(p->sym, ft->base) != NULL;
                        if ((ft && ft->base &&
                             (strncmp(ft->base,"struct ",7)==0 ||
                              strncmp(ft->base,"union " ,6)==0)) ||
                            is_typedef_struct_embed ||
                            chk(p,TOK_COLON)) {
                            snprintf(fn, sizeof fn, "__anon_%d", p->anon_counter++);
                        } else {
                            parse_error(p,"expected field name"); break;
                        }
                    } else {
                        strncpy(fn, tok_ident(&cur(p)), sizeof fn-1);
                        fn[sizeof fn-1]='\0';
                        adv(p);
                    }
                    int arr=-1; int arr2=0;
                    if (chk(p,TOK_LBRACKET)) {
                        adv(p);
                        if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { arr=parse_const_expr(p); }
                        else arr=0;
                        eat(p,TOK_RBRACKET);
                        /* Second dimension: T name[N][M] */
                        if (chk(p,TOK_LBRACKET)) {
                            adv(p);
                            if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { arr2=parse_const_expr(p); }
                            eat(p,TOK_RBRACKET);
                        }
                    }
                    /* Bitfield: "Type name : width;". squash has no packed
                     * sub-byte-field codegen, so the width is parsed and
                     * discarded — the field keeps its full declared type's
                     * size instead of being bit-packed. This changes struct
                     * layout/size vs. a real C compiler (e.g. two adjacent
                     * 1-bit fields take two full words here, not one), but
                     * since all access to the struct goes through squash's
                     * own codegen consistently, ordinary field reads/writes
                     * (not raw memory reinterpretation) still behave
                     * correctly — just parsing "Uint32 x : 31;" as
                     * "Uint32 x;" lets structs like SDL_HashItem (which
                     * pack probe_len/live into one word) compile at all. */
                    if (chk(p,TOK_COLON)) {
                        adv(p);
                        (void)parse_const_or_expr(p);
                    }
                    ASTNode *_fld = ast_field(ft2,fn,arr,cur(p).line);
                    _fld->field.array_size2 = arr2;
                    if (nf>=fields_cap) { fields_cap*=2; fields=(ASTNode**)realloc(fields,fields_cap*sizeof(ASTNode*)); }
                    fields[nf++]=_fld;
                } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
                eat(p,TOK_SEMICOLON);
            }
            eat(p,TOK_RBRACE);
            char key[280]; snprintf(key,sizeof key,"%s %s",is_union?"union":"struct",sname);
            ASTNode *sd=ast_struct_decl(sname,is_union,fields,nf,cur(p).line);
            /* compute size with field alignment and tail padding */
            int sz=0;
            int max_align=1;
            for (int i=0;i<nf;i++) {
                ASTNode *fi = fields[i];
                int fs=typeinfo_size(fi->field.type,p->sym->is_64bit);
                /* Real alignment of a struct/union-typed field — its own
                 * max member alignment, which is NOT the same thing as its
                 * total SIZE (0 = "not struct-typed, use the size-based
                 * alignment below"). See symtable_compute_struct_alignment()'s
                 * comment for the same bug class in symtable.c (the FILETIME
                 * case). Here it bit through a different door: this pre-pass
                 * size is handed to symtable_define_struct(), which keeps
                 * MAX(this, its own correct recomputation) — so an
                 * over-estimate here silently WINS over the correct value and
                 * becomes the cached struct_size that sizeof() reports, while
                 * every actual field offset keeps using the correct (smaller)
                 * recomputed layout. Concretely: a struct whose only members
                 * are int/char (true alignment 4) but whose size is >= 8 —
                 * e.g. SQW/css.h's CssSimpleSel — was treated as needing
                 * 8-byte alignment purely because its size was big, so a
                 * struct holding an ARRAY of it (CssCompound) had its size
                 * rounded up to a multiple of 8 it never needed, and the
                 * error compounded through each further nesting level. Code
                 * then malloc'd/memset'd with the too-large sizeof() while
                 * indexing with the smaller real stride, so every element
                 * after the first zeroed part of its NEXT neighbour —
                 * confirmed as the real corruption behind CSS rules losing
                 * their declarations mid-array in SQW's layout engine. */
                int fstruct_align=0;
                /* typeinfo_size returns 4 for typedefs/structs — resolve via symtable */
                if (fi->field.type && fi->field.type->pointer_depth==0 && fi->field.type->base) {
                    const char *fb=fi->field.type->base;
                    const char *bare=fb;
                    if (strncmp(bare,"struct ",7)==0) bare+=7;
                    else if (strncmp(bare,"union ",6)==0) bare+=6;
                    if (bare!=fb) {
                        char sk[280]; snprintf(sk,sizeof sk,"struct %s",bare);
                        Symbol *ss2=symtable_lookup(p->sym,sk);
                        if (ss2 && ss2->struct_size>0) fs=ss2->struct_size;
                        else if (ss2 && ss2->struct_node) fs=symtable_sizeof_struct(p->sym,ss2->struct_node);
                        if (ss2 && ss2->struct_node) fstruct_align=symtable_compute_struct_alignment(p->sym,ss2->struct_node);
                    } else {
                        Symbol *td=symtable_lookup(p->sym,fb);
                        if (td && td->kind==SYM_TYPEDEF && td->type && td->type->pointer_depth==0) {
                            const char *tb=td->type->base; const char *tbare=tb;
                            if (strncmp(tbare,"struct ",7)==0) tbare+=7;
                            else if (strncmp(tbare,"union ",6)==0) tbare+=6;
                            if (tbare!=tb) {
                                char tk[280]; snprintf(tk,sizeof tk,"struct %s",tbare);
                                Symbol *tss=symtable_lookup(p->sym,tk);
                                if (tss && tss->struct_size>0) fs=tss->struct_size;
                                else if (tss && tss->struct_node) fs=symtable_sizeof_struct(p->sym,tss->struct_node);
                                if (tss && tss->struct_node) fstruct_align=symtable_compute_struct_alignment(p->sym,tss->struct_node);
                            } else {
                                /* Trust the resolved typedef size outright, not just when
                                 * it's larger: `fs` up above started from typeinfo_size()
                                 * called on the UNRESOLVED typedef name (e.g. "WORD"),
                                 * which falls through every known-type check and defaults
                                 * to 4 — bigger than WORD's real 2-byte size, so a ">"
                                 * comparison here would keep the wrong 4 forever. */
                                int ts=typeinfo_size(td->type,p->sym->is_64bit);
                                fs=ts;
                            }
                        }
                    }
                }
                /* struct/union-typed fields use their OWN real alignment;
                 * everything else uses element size capped at 8 (correct for
                 * primitives, where size always equals natural alignment). */
                int fa = fstruct_align>0 ? fstruct_align : (fs < 8 ? fs : 8);
                if (fa < 1) fa = 1;
                if (fa > max_align) max_align = fa;
                if (fi->field.array_size>0) fs*=fi->field.array_size;
                if (is_union) { sz = fs>sz?fs:sz; }
                else {
                    if (fa > 1) sz = (sz + fa - 1) & ~(fa - 1);
                    sz += fs;
                }
            }
            /* tail padding: round up to struct alignment */
            if (max_align > 1) sz = (sz + max_align - 1) & ~(max_align - 1);
            free(fields);
            if (!has_name) {
                /* Generate unique name for anonymous struct/union so it can be found
                 * by field_byte_offset after typedef resolution. */
                snprintf(sname, sizeof sname, "$anon%d", p->anon_counter++);
                has_name = 1;
                snprintf(key, sizeof key, "%s %s", is_union?"union":"struct", sname);
            }
            if (has_name) symtable_define_struct(p->sym, sname, sd, sz);
            ti->base = my_strdup(key);
        } else {
            char key[280]; snprintf(key,sizeof key,"%s %s",is_union?"union":"struct",sname);
            ti->base = my_strdup(key);
        }
        return ti;
    }

    if (k==TOK_ENUM) {
        adv(p);
        char ename[256]="";
        if (chk(p,TOK_IDENT)) { strncpy(ename,tok_ident(&cur(p)),sizeof ename-1); adv(p); }
        /* Enum underlying type: enum E : int { ... }  (C23 / C++ extension) — consume and ignore */
        if (chk(p,TOK_COLON)) {
            adv(p); /* skip ':' */
            /* Skip optional type specifier (int, unsigned, etc.) */
            while (tok_is_type(cur(p).kind) && !chk(p,TOK_LBRACE) && !chk(p,TOK_SEMICOLON)) adv(p);
        }
        if (chk(p,TOK_LBRACE)) {
            adv(p);
            long long next_val=0;
            ASTNode *vals[256]; int nv=0;
            while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                if (!chk(p,TOK_IDENT)) { parse_error(p,"expected enum name"); break; }
                char vn[256]; strncpy(vn,tok_ident(&cur(p)),sizeof vn-1); adv(p);
                long long val=next_val;
                if (chk(p,TOK_ASSIGN)) {
                    adv(p); /* skip '=' */
                    /* Full constant-expression evaluator: numbers, char
                     * literals, other enum constants, unary minus/plus/not,
                     * and mul/div/mod/add/sub/shift/and/xor/or — SDL's
                     * flag-style enums routinely initialise values like
                     * "(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)", not just
                     * plain literals or bare enum-constant references. */
                    val = parse_const_or_expr(p);
                }
                next_val=val+1;
                symtable_define_enum_val(p->sym, vn, val);
                /* vals[] itself is write-only below this point (never read
                 * back — only symtable_define_enum_val's side effect above
                 * matters), but real-world enums can still exceed a small
                 * fixed cap (SDL3's own SDL_Scancode has ~280+ values) —
                 * stop writing once full rather than overflow the array and
                 * corrupt this function's other stack locals. */
                if (nv<256) vals[nv]=ast_enum_val(vn,val,1,cur(p).line);
                nv++;
                if (!chk(p,TOK_COMMA)) break; adv(p);
            }
            eat(p,TOK_RBRACE);
        }
        ti->base = my_strdup(ename[0] ? ename : "int");
        return ti;
    }

    /* Check for typedef name */
    if (k==TOK_IDENT) {
        Symbol *s = symtable_lookup(p->sym, tok_ident(&cur(p)));
        if (s && s->kind==SYM_TYPEDEF) {
            ti->base = my_strdup(tok_ident(&cur(p))); adv(p);
            return ti;
        }
    }

    /* Primitive types — accumulate qualifiers.
     * Use a const char* pointer (not a char array) to avoid local-array
     * zero-init issues in squash-compiled binaries. */
    const char *base_str = NULL; /* NULL = no explicit type token yet */
    int has_long=0, has_short=0;
    while (1) {
        k=cur(p).kind;
        if (k==TOK_UNSIGNED) { ti->is_unsigned=1; adv(p); continue; }
        if (k==TOK_SIGNED)   { adv(p); continue; }
        if (k==TOK_LONG)     { has_long++; adv(p); continue; }
        if (k==TOK_SHORT)    { has_short=1; adv(p); continue; }
        if (k==TOK_INT)      { base_str="int";    adv(p); break; }
        if (k==TOK_CHAR)     { base_str="char";   adv(p); break; }
        if (k==TOK_VOID)     { base_str="void";   adv(p); break; }
        if (k==TOK_DOUBLE)   { base_str="double"; adv(p); break; }
        if (k==TOK_FLOAT_KW) { base_str="float";  adv(p); break; }
        break;
    }
    /* Apply long/short modifiers: only when base is NULL (no explicit token)
     * or when base is "int" (the default integer type). */
    int apply_mod = 0;
    if (!base_str) apply_mod = 1;
    else if (strcmp(base_str,"int")==0) apply_mod = 1;
    if (apply_mod) {
        if (has_long == 2) base_str = "long long";
        else if (has_long >= 1) base_str = "long";
        else if (has_short) base_str = "short";
        else base_str = "int";
    }
    if (!base_str) base_str = "int"; /* safety fallback */
    ti->base = my_strdup(base_str);
    return ti;
}

TypeInfo *ParseFullType(Parser *p) {
    if (!is_type_start(p)) return NULL;
    /* eat storage class and C11 qualifiers separately */
    for (;;) {
        if (chk(p,TOK_STATIC)||chk(p,TOK_EXTERN)||chk(p,TOK_AUTO)||chk(p,TOK_REGISTER)) { adv(p); continue; }
        if (chk(p,TOK_CONSTEXPR)||chk(p,TOK_NORETURN)||chk(p,TOK_THREAD_LOCAL)) { adv(p); continue; }
        if (chk(p,TOK_ALIGNAS)) {
            adv(p);
            if (chk(p,TOK_LPAREN)) { adv(p); int d=1; while (!chk(p,TOK_EOF)&&d>0) { if(chk(p,TOK_LPAREN))d++; else if(chk(p,TOK_RPAREN))d--; adv(p); } }
            continue;
        }
        break;
    }
    TypeInfo *ti = ParseTypeSpecifier(p);
    if (!ti) return NULL;
    while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) ti->pointer_depth++; adv(p); }
    return ti;
}

/* =========================================================================
 * ParseNumber
 * ========================================================================= */
ASTNode *ParseNumber(Parser *p) {
    if (cur(p).kind==TOK_FLOAT) { double fv=cur(p).fval; int is_single=cur(p).is_single; int ln=cur(p).line; adv(p); return ast_float_typed(fv, is_single, ln); }
    long long iv=cur(p).ival; int ln=cur(p).line;
    eat(p,TOK_NUMBER);
    return ast_number(iv, ln);
}

/* =========================================================================
 * ParseIdentifier
 * ========================================================================= */
ASTNode *ParseIdentifier(Parser *p) {
    char name[256]; snprintf(name,sizeof name,"%.*s",cur(p).len,cur(p).start);
    int call_line = cur(p).line;
    eat(p, TOK_IDENT);

    /* function call? */
    if (chk(p,TOK_LPAREN)) {
        adv(p);
        ASTNode *args[128]; int argc=0;
        if (!chk(p,TOK_RPAREN)) {
            do {
                if (argc<128) args[argc++]=ParseAssignment(p);
                else { parse_error(p,"too many arguments"); ParseAssignment(p); }
            } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
        }
        eat(p,TOK_RPAREN);
        /* Track Windows API imports */
        Symbol *sym = symtable_lookup(p->sym, name);
        if (sym && sym->kind==SYM_IMPORT && sym->dll) {
            char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,name);
            symtable_add_import(p->sym, key);
        }
        return ast_call(name, args, argc, call_line);
    }
    ASTNode *vn = ast_var(name, call_line);
    return vn;
}

/* =========================================================================
 * ParsePrimary
 * ========================================================================= */
ASTNode *ParsePrimary(Parser *p) {
    int line = cur(p).line;

    if (chk(p,TOK_NUMBER)||chk(p,TOK_FLOAT)) return ParseNumber(p);

    if (chk(p,TOK_CHAR_LIT)) {
        long long cv=cur(p).ival; int cl=cur(p).line; adv(p);
        return ast_char_lit(cv, cl);
    }

    if (chk(p,TOK_STRING)) {
        /* Adjacent string concatenation. Track wide-ness (L"..." — the
         * lexer stows a 1/0 wide flag in the token's otherwise-unused ival
         * field for TOK_STRING) so codegen can later emit L"..." literals
         * as real UTF-16LE data instead of silently treating them as plain
         * narrow/ASCII bytes — see AST_STRING's is_wide field. Per C's own
         * adjacent-string-concatenation rule, the result is wide if ANY
         * piece was wide (e.g. L"a" "b" is a wide string). */
        char buf[4096]; snprintf(buf,sizeof buf,"%s",cur(p).sval?cur(p).sval:"");
        int is_wide = (int)cur(p).ival;
        adv(p);
        while (chk(p,TOK_STRING)) {
            const char *sv=cur(p).sval;
            if (cur(p).ival) is_wide = 1;
            adv(p);
            if (sv) strncat(buf,sv,sizeof buf-strlen(buf)-1);
        }
        ASTNode *snode = ast_string(buf, line);
        snode->str.is_wide = is_wide;
        return snode;
    }

    if (chk(p,TOK_LPAREN)) {
        adv(p);
        /* Check for cast: (type)expr */
        if (is_type_start(p)) {
            TypeInfo *ti = ParseFullType(p);
            /* Abstract function-pointer cast target: "(int (*)(const void*,
             * const void*)) userdata" — the type has an unnamed "(*)(params)"
             * suffix. This only applies here (not inside ParseFullType
             * itself) because a bare '(' after a type is ambiguous: in a
             * struct-field/param/var-decl context it starts a *named*
             * "(*name)(params)" declarator that the caller must still see,
             * but here we already know we're parsing a standalone cast
             * target, so it's safe to consume it eagerly. */
            if (ti && chk(p,TOK_LPAREN)) {
                adv(p);
                skip_calling_convention(p);
                while (chk(p,TOK_STAR)) { adv(p); ti->pointer_depth++; }
                eat(p,TOK_RPAREN);
                if (chk(p,TOK_LPAREN)) {
                    adv(p);
                    int depth=1;
                    while (!chk(p,TOK_EOF) && depth>0) {
                        if (chk(p,TOK_LPAREN)) depth++;
                        else if (chk(p,TOK_RPAREN)) depth--;
                        adv(p);
                    }
                } else if (ti && chk(p,TOK_LBRACKET)) {
                    /* Pointer-to-array abstract declarator: "(T (*)[N])expr"
                     * (e.g. SDL3 demo code casting a flat "float edges[][6]"
                     * array parameter back to "const float (*)[6]" for 2D
                     * indexing) — a different abstract-declarator shape from
                     * the "(*)(params)" function-pointer one just above.
                     * Record the first dimension the same way a real
                     * "T (*p)[N];" declaration would, so codegen's
                     * cast-aware pointer arithmetic can compute the correct
                     * per-index stride for "((T(*)[N])p)[i][j]". Deeper
                     * ranks are consumed (so the parse still succeeds) but
                     * not separately modeled, matching this codebase's
                     * existing 2-dimension array_size/array_size2 cap. */
                    adv(p);
                    int dim = 0;
                    if (!chk(p,TOK_RBRACKET)) dim = parse_const_dim_expr(p);
                    eat(p,TOK_RBRACKET);
                    ti->array_size = dim;
                    while (chk(p,TOK_LBRACKET)) {
                        adv(p);
                        while (!chk(p,TOK_RBRACKET) && !chk(p,TOK_EOF)) adv(p);
                        eat(p,TOK_RBRACKET);
                    }
                }
            }
            if (ti && chk(p,TOK_RPAREN)) {
                adv(p);
                /* C99 compound literal: "(Type){ init-list }" — NOT a cast
                 * (a cast is always followed by a unary-expression operand,
                 * never a brace). This is an lvalue: squash has no runtime
                 * constructor mechanism for an anonymous temporary, so it's
                 * lowered here into a synthetic "Type __cl_N = { ... };"
                 * local declaration (reusing the exact init-list shape/
                 * codegen path that a normal local struct/array declaration
                 * with a brace initializer already uses), hoisted out to
                 * statement level via the pending-statement queue (see
                 * ParseStatement's wrapper), with the compound literal
                 * expression itself replaced by a reference to that
                 * variable. */
                if (chk(p,TOK_LBRACE)) {
                    adv(p);
                    int ecap=64; ASTNode **elems=(ASTNode**)malloc((size_t)ecap*sizeof(ASTNode*)); int ne=0;
                    while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                        parse_flatten_brace_elem(p, &elems, &ne, &ecap);
                        if (!chk(p,TOK_COMMA)) break;
                        adv(p);
                    }
                    eat(p,TOK_RBRACE);
                    ASTNode *initlist = ast_block(elems, ne, line);
                    free(elems);
                    char synth_name[64];
                    snprintf(synth_name, sizeof synth_name, "__cl_lit_%d", p->compound_lit_counter++);
                    symtable_define_var(p->sym, synth_name, ti);
                    ASTNode *decl = ast_var_decl(NULL, ti, synth_name, initlist, -1, line);
                    parser_push_pending(p, decl);
                    return ast_var(synth_name, line);
                }
                ASTNode *expr = ParseUnary(p);
                return ast_cast(ti, expr, line);
            }
            /* Not a cast — parenthesised expression, put type back somehow */
            /* This is a parse ambiguity we resolve by just continuing as expr */
            typeinfo_free(ti);
        }
        if (p->expr_depth >= 40) {
            parse_error(p, "expression too deeply nested (parenthesis nesting > 40)");
            /* Don't recurse further -- consume tokens up to a matching
             * close-paren-ish point and return a placeholder, same
             * error-recovery shape as the "unexpected token" path below,
             * rather than actually calling ParseExpression and recursing
             * into the stack overflow this check exists to prevent. */
            adv(p);
            return ast_number(0, line);
        }
        p->expr_depth++;
        ASTNode *e = ParseExpression(p);
        p->expr_depth--;
        eat(p,TOK_RPAREN);
        return e;
    }

    if (chk(p,TOK_SIZEOF)) {
        adv(p);
        /* sizeof supports both sizeof(x) and sizeof x */
        if (chk(p, TOK_LPAREN)) {
            adv(p); /* consume '(' */
            if (is_type_start(p)) {
                TypeInfo *ti = ParseFullType(p);
                eat(p,TOK_RPAREN);
                return ast_sizeof_type(ti, line);
            }
            ASTNode *e = ParseUnary(p);
            eat(p,TOK_RPAREN);
            return ast_sizeof_expr(e, line);
        } else {
            /* sizeof without parens: sizeof varname */
            ASTNode *eu = ParseUnary(p);
            return ast_sizeof_expr(eu, line);
        }
    }

    /* _Alignof(type) — returns alignment of type (simplified: same as sizeof) */
    if (chk(p,TOK_ALIGNOF)) {
        adv(p);
        eat(p,TOK_LPAREN);
        if (is_type_start(p)) {
            TypeInfo *ti = ParseFullType(p);
            eat(p,TOK_RPAREN);
            return ast_sizeof_type(ti, line); /* reuse sizeof semantics */
        }
        ASTNode *e = ParseUnary(p);
        eat(p,TOK_RPAREN);
        return ast_sizeof_expr(e, line);
    }

    /* nullptr literal — same as NULL (0) */
    if (chk(p,TOK_NULLPTR)) {
        adv(p);
        return ast_number(0, line);
    }

    if (chk(p,TOK_IDENT)) return ParseIdentifier(p);

    { char _em[128]; snprintf(_em,sizeof _em,"unexpected token '%s'",token_kind_name(cur(p).kind)); parse_error(p,_em); }
    adv(p);
    return ast_number(0, line); /* error recovery */
}

/* =========================================================================
 * ParsePostfix — a[i], a.b, a->b, f(args), a++, a--
 * ========================================================================= */
ASTNode *ParsePostfix(Parser *p) {
    ASTNode *n = ParsePrimary(p);
    for (;;) {
        int line = cur(p).line;
        if (chk(p,TOK_LBRACKET)) {
            adv(p);
            ASTNode *idx = ParseExpression(p);
            eat(p,TOK_RBRACKET);
            n = ast_index(n, idx, line);
        } else if (chk(p,TOK_DOT)) {
            adv(p);
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected field name"); break; }
            char fn[256]; strncpy(fn,tok_ident(&cur(p)),sizeof fn-1); adv(p);
            n = ast_member(n, fn, 0, line);
        } else if (chk(p,TOK_ARROW)) {
            adv(p);
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected field name"); break; }
            char fn[256]; strncpy(fn,tok_ident(&cur(p)),sizeof fn-1); adv(p);
            n = ast_member(n, fn, 1, line);
        } else if (chk(p,TOK_INC)) {
            adv(p); n = ast_unary("++", n, 1, line);
        } else if (chk(p,TOK_DEC)) {
            adv(p); n = ast_unary("--", n, 1, line);
        } else if (chk(p,TOK_LPAREN)) {
            /* Function-pointer call: expr(args) */
            adv(p);
            ASTNode *args[64]; int argc=0;
            if (!chk(p,TOK_RPAREN)) {
                do {
                    if (argc<64) args[argc++]=ParseAssignment(p);
                    else { parse_error(p,"too many args"); ParseAssignment(p); }
                } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
            }
            eat(p,TOK_RPAREN);
            n = ast_fp_call(n, args, argc, line);
        } else break;
    }
    return n;
}

/* =========================================================================
 * ParseUnary — prefix: !, ~, -, +, &, *, ++, --, (cast)
 * ========================================================================= */
ASTNode *ParseUnary(Parser *p) {
    int line = cur(p).line;
    if (chk(p,TOK_BANG))  { adv(p); return ast_unary("!", ParseUnary(p), 0, line); }
    if (chk(p,TOK_TILDE)) { adv(p); return ast_unary("~", ParseUnary(p), 0, line); }
    if (chk(p,TOK_MINUS)) { adv(p); return ast_unary("-", ParseUnary(p), 0, line); }
    if (chk(p,TOK_PLUS))  { adv(p); return ParseUnary(p); }
    if (chk(p,TOK_INC))   { adv(p); return ast_unary("++",ParseUnary(p), 0, line); }
    if (chk(p,TOK_DEC))   { adv(p); return ast_unary("--",ParseUnary(p), 0, line); }
    if (chk(p,TOK_AMP))   { adv(p); return ast_addr(ParseUnary(p), line); }
    if (chk(p,TOK_STAR))  { adv(p); return ast_deref(ParseUnary(p), line); }
    return ParsePostfix(p);
}

/* =========================================================================
 * Binary expression parsers — full precedence table
 * ========================================================================= */
ASTNode *ParseMulDiv(Parser *p) {
    ASTNode *lhs = ParseUnary(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if (chk(p,TOK_STAR))    op="*";
        else if (chk(p,TOK_SLASH))   op="/";
        else if (chk(p,TOK_PERCENT)) op="%";
        else break;
        adv(p);
        ASTNode *rhs = ParseUnary(p);
        lhs = ast_binary(op, lhs, rhs, line);
    }
    return lhs;
}
ASTNode *ParseAddSub(Parser *p) {
    ASTNode *lhs = ParseMulDiv(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if (chk(p,TOK_PLUS))  op="+";
        else if (chk(p,TOK_MINUS)) op="-";
        else break;
        adv(p); lhs = ast_binary(op, lhs, ParseMulDiv(p), line);
    }
    return lhs;
}
ASTNode *ParseShift(Parser *p) {
    ASTNode *lhs = ParseAddSub(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if (chk(p,TOK_LSHIFT)) op="<<";
        else if (chk(p,TOK_RSHIFT)) op=">>";
        else break;
        adv(p); lhs = ast_binary(op, lhs, ParseAddSub(p), line);
    }
    return lhs;
}
ASTNode *ParseRelational(Parser *p) {
    ASTNode *lhs = ParseShift(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if      (chk(p,TOK_LT)) op="<";
        else if (chk(p,TOK_GT)) op=">";
        else if (chk(p,TOK_LE)) op="<=";
        else if (chk(p,TOK_GE)) op=">=";
        else break;
        adv(p); lhs = ast_binary(op, lhs, ParseShift(p), line);
    }
    return lhs;
}
ASTNode *ParseEquality(Parser *p) {
    ASTNode *lhs = ParseRelational(p);
    for (;;) {
        int line=cur(p).line; const char *op=NULL;
        if (chk(p,TOK_EQ)) op="=="; else if (chk(p,TOK_NEQ)) op="!="; else break;
        adv(p); lhs = ast_binary(op, lhs, ParseRelational(p), line);
    }
    return lhs;
}
ASTNode *ParseBitAnd(Parser *p) {
    ASTNode *lhs = ParseEquality(p);
    while (chk(p,TOK_AMP)) { int l=cur(p).line; adv(p); lhs=ast_binary("&",lhs,ParseEquality(p),l); }
    return lhs;
}
ASTNode *ParseBitXor(Parser *p) {
    ASTNode *lhs = ParseBitAnd(p);
    while (chk(p,TOK_CARET)) { int l=cur(p).line; adv(p); lhs=ast_binary("^",lhs,ParseBitAnd(p),l); }
    return lhs;
}
ASTNode *ParseBitOr(Parser *p) {
    ASTNode *lhs = ParseBitXor(p);
    while (chk(p,TOK_PIPE)) { int l=cur(p).line; adv(p); lhs=ast_binary("|",lhs,ParseBitXor(p),l); }
    return lhs;
}
ASTNode *ParseLogicalAnd(Parser *p) {
    ASTNode *lhs = ParseBitOr(p);
    while (chk(p,TOK_AND)) { int l=cur(p).line; adv(p); lhs=ast_binary("&&",lhs,ParseBitOr(p),l); }
    return lhs;
}
ASTNode *ParseLogicalOr(Parser *p) {
    ASTNode *lhs = ParseLogicalAnd(p);
    while (chk(p,TOK_OR)) { int l=cur(p).line; adv(p); lhs=ast_binary("||",lhs,ParseLogicalAnd(p),l); }
    return lhs;
}
ASTNode *ParseTernary(Parser *p) {
    ASTNode *cond = ParseLogicalOr(p);
    if (chk(p,TOK_QUESTION)) {
        int line=cur(p).line; adv(p);
        ASTNode *then_ = ParseExpression(p);
        eat(p,TOK_COLON);
        ASTNode *else_ = ParseTernary(p);
        return ast_ternary(cond, then_, else_, line);
    }
    return cond;
}

ASTNode *ParseAssignment(Parser *p) {
    ASTNode *lhs = ParseTernary(p);
    int line = cur(p).line;
    const char *op = NULL;
    switch (cur(p).kind) {
        case TOK_ASSIGN:     op="=";   break;
        case TOK_PLUS_EQ:    op="+=";  break;
        case TOK_MINUS_EQ:   op="-=";  break;
        case TOK_STAR_EQ:    op="*=";  break;
        case TOK_SLASH_EQ:   op="/=";  break;
        case TOK_PERCENT_EQ: op="%=";  break;
        case TOK_AMP_EQ:     op="&=";  break;
        case TOK_PIPE_EQ:    op="|=";  break;
        case TOK_CARET_EQ:   op="^=";  break;
        case TOK_LSHIFT_EQ:  op="<<="; break;
        case TOK_RSHIFT_EQ:  op=">>="; break;
        default: return lhs;
    }
    adv(p);
    ASTNode *rhs = ParseAssignment(p); /* right-associative */
    return ast_assign(op, lhs, rhs, line);
}

/* Recursively flatten a brace-initializer element into `*elems`, growing the
 * array as needed. A plain element is one ParseAssignment() value; a nested
 * "{ ... }" recurses into itself so arbitrarily deep nesting flattens
 * correctly — e.g. an array-of-structs where a struct field is ITSELF an
 * array needs "{ { 1, {10,20,30,40} }, ... }" (two levels of nesting deep,
 * not one). The three brace-initializer call sites in this file (global
 * single-declarator, global multi-declarator, and local variable/struct-
 * field) previously each inlined their own COPY of this loop supporting only
 * ONE level of "{...}" nesting — calling ParseAssignment() directly on
 * whatever a second-level nested brace's own contents were, which fails
 * outright ("unexpected token '{'") since a bare '{' is never a valid
 * expression start. Confirmed via SDL3's own real-world
 * audio/SDL_wave.c "extensible_guids[]" (a WaveExtensibleGUID array; each
 * element is "{ encoding, { 16 GUID bytes } }") — a genuinely common shape
 * for any array-of-structs whose element type has its own array field. */
static void parse_flatten_brace_elem(Parser *p, ASTNode ***elems, int *ne, int *ecap) {
    if (chk(p,TOK_LBRACE)) {
        adv(p);
        while (!chk(p,TOK_RBRACE) && !chk(p,TOK_EOF)) {
            parse_flatten_brace_elem(p, elems, ne, ecap);
            if (!chk(p,TOK_COMMA)) break;
            adv(p);
        }
        eat(p,TOK_RBRACE);
    } else {
        if (*ne >= *ecap) { *ecap *= 2; *elems = (ASTNode**)realloc(*elems, (size_t)(*ecap) * sizeof(ASTNode*)); }
        (*elems)[(*ne)++] = ParseAssignment(p);
    }
}

ASTNode *ParseExpression(Parser *p) {
    ASTNode *e = ParseAssignment(p);
    /* comma operator */
    while (chk(p,TOK_COMMA)) {
        int line=cur(p).line; adv(p);
        ASTNode *r = ParseAssignment(p);
        e = ast_binary(",", e, r, line);
    }
    return e;
}

/* =========================================================================
 * ParseStatement
 * ========================================================================= */
ASTNode *ParseBlock(Parser *p) {
    int line=cur(p).line;
    eat(p,TOK_LBRACE);
    /* "{{{{{...}}}}}" -- a block nested tens of thousands deep -- recurses
     * through ParseBlock -> ParseStatement -> ParseBlock once per '{' with
     * no other bound, blowing the real call stack (segfault, not a clean
     * error) well before any of this function's own per-call work matters.
     * Found via fuzzing this compiler's own robustness against hostile
     * input; same fix shape as ParsePrimary's own expr_depth guard for
     * "(((((...)))))" just above. */
    if (p->block_depth >= 40) {
        parse_error(p, "block nested too deeply (compound-statement nesting > 40)");
        symtable_push_scope(p->sym);
        ASTNode *empty = ast_block(NULL, 0, line);
        while (!chk(p,TOK_RBRACE) && !chk(p,TOK_EOF)) adv(p);
        if (chk(p,TOK_RBRACE)) adv(p);
        symtable_pop_scope(p->sym);
        return empty;
    }
    p->block_depth++;
    ASTNode *stmts[1024]; int count=0;
    symtable_push_scope(p->sym);
    while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF))
        if (count<1024) stmts[count++]=ParseStatement(p);
        else { parse_error(p,"block too large"); break; }
    symtable_pop_scope(p->sym);
    eat(p,TOK_RBRACE);
    p->block_depth--;
    return ast_block(stmts, count, line);
}

static ASTNode *ParseStatement_impl(Parser *p) {
    int line = cur(p).line;
    /* Null statement: just a semicolon */
    if (chk(p,TOK_SEMICOLON)) { adv(p); return ast_number(0,line); }

    /* _Static_assert / static_assert */
    if (chk(p,TOK_STATIC_ASSERT)) {
        adv(p);
        eat(p,TOK_LPAREN);
        /* Evaluate as expression but generate no code (compile-time check) */
        ASTNode *cond = ParseExpression(p);
        if (chk(p,TOK_COMMA)) {
            adv(p);
            /* skip message string */
            if (chk(p,TOK_STRING)) adv(p);
        }
        eat(p,TOK_RPAREN);
        eat(p,TOK_SEMICOLON);
        /* Emit the condition as a no-op expression (future: constant-fold + error) */
        return ast_expr_stmt(cond, line);
    }

    /* GCC extended inline assembly:
     *   asm [volatile] ( "template" [ : outputs [ : inputs [ : clobbers ]]] ) ;
     * where each output/input operand is [ "[name]" ] "constraint" ( c-expr ).
     * "asm"/"__asm__"/"__asm" and "volatile"/"__volatile__"/"__volatile" are
     * all recognized as the same token by the lexer (see lexer.c). */
    if (chk(p,TOK_ASM)) {
        adv(p);
        int is_volatile = 0;
        if (chk(p,TOK_VOLATILE)) { is_volatile=1; adv(p); }
        if (chk(p,TOK_CONST)) adv(p); /* "asm const(...)" — rare MSVC/GCC extension, no-op here */
        eat(p,TOK_LPAREN);

        char template_buf[4096]="";
        if (chk(p,TOK_STRING)) {
            snprintf(template_buf,sizeof template_buf,"%s",cur(p).sval?cur(p).sval:""); adv(p);
            while (chk(p,TOK_STRING)) {
                const char *sv=cur(p).sval; adv(p);
                if (sv) strncat(template_buf,sv,sizeof template_buf-strlen(template_buf)-1);
            }
        }

        AsmOperandNode outputs[16]; char out_constraints[16][32]; int n_outputs=0;
        AsmOperandNode inputs[16];  char in_constraints[16][32];  int n_inputs=0;
        char clobber_bufs[16][16]; char *clobbers[16]; int n_clobbers=0;

        if (chk(p,TOK_COLON)) {
            adv(p);
            if (!chk(p,TOK_COLON) && !chk(p,TOK_RPAREN)) {
                do {
                    if (chk(p,TOK_LBRACKET)) { adv(p); while(!chk(p,TOK_RBRACKET)&&!chk(p,TOK_EOF)) adv(p); if(chk(p,TOK_RBRACKET)) adv(p); }
                    out_constraints[n_outputs][0]='\0';
                    if (chk(p,TOK_STRING)) { snprintf(out_constraints[n_outputs],32,"%s",cur(p).sval?cur(p).sval:""); adv(p); }
                    eat(p,TOK_LPAREN);
                    ASTNode *oexpr = ParseExpression(p);
                    eat(p,TOK_RPAREN);
                    if (n_outputs<16) { outputs[n_outputs].constraint=out_constraints[n_outputs]; outputs[n_outputs].expr=oexpr; n_outputs++; }
                } while (chk(p,TOK_COMMA) && (adv(p),1));
            }
        }
        if (chk(p,TOK_COLON)) {
            adv(p);
            if (!chk(p,TOK_COLON) && !chk(p,TOK_RPAREN)) {
                do {
                    if (chk(p,TOK_LBRACKET)) { adv(p); while(!chk(p,TOK_RBRACKET)&&!chk(p,TOK_EOF)) adv(p); if(chk(p,TOK_RBRACKET)) adv(p); }
                    in_constraints[n_inputs][0]='\0';
                    if (chk(p,TOK_STRING)) { snprintf(in_constraints[n_inputs],32,"%s",cur(p).sval?cur(p).sval:""); adv(p); }
                    eat(p,TOK_LPAREN);
                    ASTNode *iexpr = ParseExpression(p);
                    eat(p,TOK_RPAREN);
                    if (n_inputs<16) { inputs[n_inputs].constraint=in_constraints[n_inputs]; inputs[n_inputs].expr=iexpr; n_inputs++; }
                } while (chk(p,TOK_COMMA) && (adv(p),1));
            }
        }
        if (chk(p,TOK_COLON)) {
            adv(p);
            if (!chk(p,TOK_RPAREN)) {
                do {
                    if (chk(p,TOK_STRING)) {
                        if (n_clobbers<16) { snprintf(clobber_bufs[n_clobbers],16,"%s",cur(p).sval?cur(p).sval:""); clobbers[n_clobbers]=clobber_bufs[n_clobbers]; n_clobbers++; }
                        adv(p);
                    }
                } while (chk(p,TOK_COMMA) && (adv(p),1));
            }
        }

        eat(p,TOK_RPAREN);
        eat(p,TOK_SEMICOLON);
        return ast_asm_stmt(is_volatile, template_buf, outputs, n_outputs, inputs, n_inputs, clobbers, n_clobbers, line);
    }

    /* Local typedef inside a function body */
    if (chk(p,TOK_TYPEDEF)) {
        adv(p);
        TypeInfo *ti = ParseTypeSpecifier(p);
        while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) ti->pointer_depth++; adv(p); }
        /* Function pointer typedef: typedef RET (*NAME)(PARAMS); — see the
         * matching top-level typedef case in parse_program for rationale;
         * treated as an opaque pointer alias since it's always cast through
         * before use in every header we've found using this pattern. */
        if (chk(p,TOK_LPAREN)) {
            adv(p); /* ( */
            skip_calling_convention(p); /* typedef RET (WINAPI *NAME)(PARAMS); */
            while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) adv(p);
            if (chk(p,TOK_IDENT)) {
                char alias[256]; strncpy(alias,tok_ident(&cur(p)),sizeof alias-1); adv(p);
                eat(p,TOK_RPAREN); /* ) closing (*NAME) */
                eat(p,TOK_LPAREN); /* ( opening the parameter list */
                int depth = 1;
                while (depth > 0 && !chk(p,TOK_EOF)) {
                    if (chk(p,TOK_LPAREN)) depth++;
                    else if (chk(p,TOK_RPAREN)) depth--;
                    adv(p);
                }
                eat(p,TOK_SEMICOLON);
                ti->pointer_depth++;
                symtable_define_typedef(p->sym, alias, ti);
                return ast_typedef_decl(ti, alias, line);
            }
            eat(p,TOK_SEMICOLON);
            return ast_number(0,line);
        }
        if (chk(p,TOK_IDENT) || chk(p,TOK_BOOL)) {
            char alias[256]; strncpy(alias,tok_ident(&cur(p)),sizeof alias-1); adv(p);
            parse_typedef_array_dims(p, ti);
            eat(p,TOK_SEMICOLON);
            symtable_define_typedef(p->sym, alias, ti);
            return ast_typedef_decl(ti, alias, line);
        }
        eat(p,TOK_SEMICOLON);
        return ast_number(0,line);
    }

    if (chk(p,TOK_LBRACE)) return ParseBlock(p);

    if (chk(p,TOK_IF)) {
        adv(p); eat(p,TOK_LPAREN);
        ASTNode *cond = ParseExpression(p); eat(p,TOK_RPAREN);
        ASTNode *then_ = ParseStatement(p);
        ASTNode *else_ = NULL;
        if (chk(p,TOK_ELSE)) { adv(p); else_=ParseStatement(p); }
        return ast_if(cond, then_, else_, line);
    }

    if (chk(p,TOK_WHILE)) {
        adv(p); eat(p,TOK_LPAREN);
        ASTNode *cond=ParseExpression(p); eat(p,TOK_RPAREN);
        ASTNode *body=ParseStatement(p);
        return ast_while(cond, body, line);
    }

    if (chk(p,TOK_DO)) {
        adv(p);
        ASTNode *body=ParseStatement(p);
        eat(p,TOK_WHILE); eat(p,TOK_LPAREN);
        ASTNode *cond=ParseExpression(p); eat(p,TOK_RPAREN); eat(p,TOK_SEMICOLON);
        return ast_do_while(body, cond, line);
    }

    if (chk(p,TOK_FOR)) {
        adv(p); eat(p,TOK_LPAREN);
        ASTNode *init=NULL, *cond=NULL, *step=NULL;
        symtable_push_scope(p->sym);
        if (!chk(p,TOK_SEMICOLON)) {
            if (is_type_start(p)) init = ParseVariable(p); /* var decl — eats semicolon */
            else { init=ast_expr_stmt(ParseExpression(p),line); eat(p,TOK_SEMICOLON); }
        } else eat(p,TOK_SEMICOLON);
        if (!chk(p,TOK_SEMICOLON)) cond=ParseExpression(p);
        eat(p,TOK_SEMICOLON);
        if (!chk(p,TOK_RPAREN)) step=ParseExpression(p);
        eat(p,TOK_RPAREN);
        ASTNode *body=ParseStatement(p);
        symtable_pop_scope(p->sym);
        return ast_for(init, cond, step, body, line);
    }

    if (chk(p,TOK_SWITCH)) {
        adv(p); eat(p,TOK_LPAREN);
        ASTNode *expr=ParseExpression(p); eat(p,TOK_RPAREN);
        eat(p,TOK_LBRACE);
        ASTNode *cases[256]; int nc=0;
        while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
            if (chk(p,TOK_CASE)) {
                adv(p);
                int val=0;
                /* Cast prefix: case (size_t)-2: — the type only affects
                 * width/signedness we don't model here; skip it and
                 * evaluate the operand plainly. Needs a tentative consume
                 * of '(' since the lexer has no multi-token lookahead: if
                 * what follows isn't actually a type, treat it as a normal
                 * parenthesised constant expression instead. */
                if (chk(p,TOK_LPAREN)) {
                    adv(p);
                    if (is_type_start(p)) {
                        TypeInfo *cast_ti = ParseTypeSpecifier(p);
                        while (chk(p,TOK_STAR)) adv(p); /* pointer cast: ignore */
                        typeinfo_free(cast_ti);
                        eat(p,TOK_RPAREN);
                        val = parse_const_dim_factor(p);
                    } else {
                        val = parse_const_or_expr(p);
                        eat(p,TOK_RPAREN);
                    }
                }
                /* Bare case constant — a full constant-EXPRESSION, not just
                 * a lone literal/identifier: real-world switches routinely
                 * combine flags/format tags with a binary operator right in
                 * the label itself (e.g. SDL3's own audio/SDL_audiotypecvt.c
                 * "case SDL_AUDIO_S16 ^ SDL_AUDIO_MASK_BIG_ENDIAN:"). The
                 * previous single-token-only handling (bare NUMBER/IDENT/
                 * CHAR_LIT, or a unary prefix via parse_const_dim_factor)
                 * consumed only the first operand and then unconditionally
                 * expected ':', producing "expected ':' but got '^'" (or
                 * '|', '+', etc.) for any such label. parse_const_or_expr
                 * already implements the full bitwise-or/xor/and precedence
                 * chain (used elsewhere for enum-value initializers like
                 * "SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL") and its base
                 * case already resolves bare numbers/char literals/enum
                 * identifiers/unary prefixes correctly, so it's a strict
                 * superset of everything this branch used to handle
                 * one-token-at-a-time. */
                else { val = parse_const_or_expr(p); }
                eat(p,TOK_COLON);
                ASTNode *body[256]; int nb=0;
                while (!chk(p,TOK_CASE)&&!chk(p,TOK_DEFAULT)&&!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF))
                    body[nb++]=ParseStatement(p);
                if (nc<256) cases[nc++]=ast_case(val,body,nb,line);
            } else if (chk(p,TOK_DEFAULT)) {
                adv(p); eat(p,TOK_COLON);
                ASTNode *body[256]; int nb=0;
                while (!chk(p,TOK_CASE)&&!chk(p,TOK_DEFAULT)&&!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF))
                    body[nb++]=ParseStatement(p);
                if (nc<256) cases[nc++]=ast_default(body,nb,line);
            } else {
                parse_error(p,"expected 'case' or 'default' in switch");
                adv(p);
            }
        }
        eat(p,TOK_RBRACE);
        return ast_switch(expr, cases, nc, line);
    }

    if (chk(p,TOK_RETURN)) {
        adv(p);
        ASTNode *expr=NULL;
        if (!chk(p,TOK_SEMICOLON)) expr=ParseExpression(p);
        eat(p,TOK_SEMICOLON);
        return ast_return(expr, line);
    }

    if (chk(p,TOK_BREAK))    { adv(p); eat(p,TOK_SEMICOLON); return ast_break(line); }
    if (chk(p,TOK_CONTINUE)) { adv(p); eat(p,TOK_SEMICOLON); return ast_continue(line); }

    if (chk(p,TOK_GOTO)) {
        adv(p);
        if (!chk(p,TOK_IDENT)) { parse_error(p,"expected label after goto"); }
        char lbl[256]; strncpy(lbl,tok_ident(&cur(p)),sizeof lbl-1); adv(p);
        eat(p,TOK_SEMICOLON);
        return ast_goto(lbl, line);
    }

    /* Label: ident ':' statement  OR  typedef-name variable declaration */
    if (chk(p,TOK_IDENT)) {
        /* If this ident is a typedef name, treat as variable declaration */
        { Symbol *ts = symtable_lookup(p->sym, tok_ident(&cur(p)));
          if (ts && ts->kind==SYM_TYPEDEF) return ParseVariable(p); }
        /* peek one ahead to see if it's a label — access fields directly, no struct copy */
        char nm[256]; snprintf(nm,sizeof nm,"%.*s",cur(p).len,cur(p).start);
        adv(p);
        if (chk(p,TOK_COLON)) {
            adv(p);
            ASTNode *stmt = ParseStatement(p);
            return ast_label(nm, stmt, line);
        }
        /* Not a label — reconstruct expression starting with the identifier */
        /* We consumed the ident, need to rebuild. Use ast_var and continue parsing */
        ASTNode *base = ast_var(nm, line);
        /* Finish any postfix */
        ASTNode *e = base;
        /* re-enter postfix with already-parsed base */
        for (;;) {
            int l2=cur(p).line;
            if (chk(p,TOK_LBRACKET)) { adv(p); ASTNode *idx=ParseExpression(p); eat(p,TOK_RBRACKET); e=ast_index(e,idx,l2); }
            else if (chk(p,TOK_DOT)) { adv(p); char fn[64]; strncpy(fn,tok_ident(&cur(p)),sizeof fn-1); adv(p); e=ast_member(e,fn,0,l2); }
            else if (chk(p,TOK_ARROW)) { adv(p); char fn[64]; strncpy(fn,tok_ident(&cur(p)),sizeof fn-1); adv(p); e=ast_member(e,fn,1,l2); }
            else if (chk(p,TOK_INC)) { adv(p); e=ast_unary("++",e,1,l2); }
            else if (chk(p,TOK_DEC)) { adv(p); e=ast_unary("--",e,1,l2); }
            else if (chk(p,TOK_LPAREN)) {
                adv(p); ASTNode *args[64]; int argc=0;
                if (!chk(p,TOK_RPAREN)) {
                    do {
                        if (argc<64) args[argc++]=ParseAssignment(p);
                    } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
                }
                eat(p,TOK_RPAREN);
                if (e->kind==AST_VAR) {
                    Symbol *sym=symtable_lookup(p->sym,e->var.name);
                    if (sym&&sym->kind==SYM_IMPORT&&sym->dll) {
                        char key[512]; snprintf(key,sizeof key,"%s:%s",sym->dll,e->var.name);
                        symtable_add_import(p->sym,key);
                    }
                    e=ast_call(e->var.name,args,argc,l2);
                } else e=ast_fp_call(e,args,argc,l2);
            }
            else break;
        }
        /* Continue with binary/assign operators */
        /* Temporarily create a fake parser state by injecting 'e' as the lhs */
        /* We do this by calling a helper that takes a pre-parsed LHS */
        /* Actually: re-enter the assignment chain */
        int l2=cur(p).line;
        const char *aop=NULL;
        switch(cur(p).kind){
            case TOK_ASSIGN: aop="="; break; case TOK_PLUS_EQ: aop="+="; break;
            case TOK_MINUS_EQ: aop="-="; break; case TOK_STAR_EQ: aop="*="; break;
            case TOK_SLASH_EQ: aop="/="; break; case TOK_PERCENT_EQ: aop="%="; break;
            case TOK_AMP_EQ: aop="&="; break; case TOK_PIPE_EQ: aop="|="; break;
            case TOK_CARET_EQ: aop="^="; break;
            case TOK_LSHIFT_EQ: aop="<<="; break; case TOK_RSHIFT_EQ: aop=">>="; break;
            default: break;
        }
        if (aop) { adv(p); ASTNode *rhs=ParseAssignment(p); e=ast_assign(aop,e,rhs,l2); }
        /* Comma operator: "s1 += ptr[0], s2 += s1;" (miniz.h's mz_adler32
         * and friends use this constantly) — this fast path reconstructs an
         * identifier-led statement by hand instead of going through the
         * generic ParseExpression (which already handles ','), so it must
         * loop for trailing comma-joined assignment-expressions itself too,
         * matching ParseExpression's own comma-operator loop exactly. */
        while (chk(p,TOK_COMMA)) {
            int l3=cur(p).line; adv(p);
            ASTNode *r = ParseAssignment(p);
            e = ast_binary(",", e, r, l3);
        }
        eat(p,TOK_SEMICOLON);
        return ast_expr_stmt(e, line);
    }

    /* Variable declaration */
    if (is_type_start(p)) return ParseVariable(p);

    /* Expression statement */
    ASTNode *expr = ParseExpression(p);
    eat(p,TOK_SEMICOLON);
    return ast_expr_stmt(expr, line);
}

/* Public ParseStatement: thin wrapper around ParseStatement_impl that
 * splices in any synthetic compound-literal backing declarations queued
 * during the parse of this one statement (see the Parser struct's
 * pending_stmts comment in parser.h). Every recursive "parse one statement"
 * call site in ParseStatement_impl (if/while/for/switch-case bodies, ...)
 * calls this wrapper by name (not the _impl directly), so a compound
 * literal nested at any statement-body depth gets correctly hoisted to
 * immediately before its own smallest enclosing statement, not leaked past
 * it. */
ASTNode *ParseStatement(Parser *p) {
    int line = cur(p).line;
    int pending_before = p->n_pending;
    ASTNode *stmt = ParseStatement_impl(p);
    if (p->n_pending > pending_before) {
        int n_new = p->n_pending - pending_before;
        ASTNode **wrapped = (ASTNode**)malloc((size_t)(n_new+1)*sizeof(ASTNode*));
        for (int i=0;i<n_new;i++) wrapped[i]=p->pending_stmts[pending_before+i];
        wrapped[n_new]=stmt;
        p->n_pending = pending_before;
        ASTNode *blk = ast_block(wrapped, n_new+1, stmt?stmt->line:line);
        free(wrapped);
        return blk;
    }
    return stmt;
}

/* =========================================================================
 * ParseVariable — [storage] type name [[N]] [= init] ;
 * ========================================================================= */
ASTNode *ParseVariable(Parser *p) {
    int line = cur(p).line;
    char storage[32]="";
    if (chk(p,TOK_STATIC))   { strncpy(storage,"static",sizeof storage-1);   adv(p); }
    else if (chk(p,TOK_EXTERN))  { strncpy(storage,"extern",sizeof storage-1);   adv(p); }
    else if (chk(p,TOK_AUTO))    { strncpy(storage,"auto",sizeof storage-1);     adv(p); }
    else if (chk(p,TOK_REGISTER)){ strncpy(storage,"register",sizeof storage-1); adv(p); }
    else if (chk(p,TOK_CONST))   { strncpy(storage,"const",sizeof storage-1);    adv(p); }
    else if (chk(p,TOK_CONSTEXPR)){ strncpy(storage,"const",sizeof storage-1);   adv(p); }
    /* consume inline/volatile/C11 qualifiers silently */
    for (;;) {
        if (chk(p,TOK_INLINE)||chk(p,TOK_VOLATILE)) { adv(p); continue; }
        if (chk(p,TOK_NORETURN)||chk(p,TOK_THREAD_LOCAL)) { adv(p); continue; }
        if (chk(p,TOK_ALIGNAS)) {
            adv(p);
            if (chk(p,TOK_LPAREN)) { adv(p); int d=1; while (!chk(p,TOK_EOF)&&d>0) { if(chk(p,TOK_LPAREN))d++; else if(chk(p,TOK_RPAREN))d--; adv(p); } }
            continue;
        }
        break;
    }
    if (chk(p,TOK_STATIC)&&!storage[0]){strncpy(storage,"static",sizeof storage-1);adv(p);}
    for (;;) {
        if (chk(p,TOK_INLINE)||chk(p,TOK_VOLATILE)) { adv(p); continue; }
        if (chk(p,TOK_NORETURN)||chk(p,TOK_THREAD_LOCAL)||chk(p,TOK_CONSTEXPR)) { adv(p); continue; }
        break;
    }

    TypeInfo *type = ParseTypeSpecifier(p);
    if (!type) { parse_error(p,"expected type specifier"); return ast_number(0,line); }
    int base_ptr_depth = type->pointer_depth; /* save before first declarator's stars */

    /* Type-only local declaration — no declarator at all, e.g. a local
     * anonymous or tagged "enum { A = 5, B = 3 };" (an extremely common C
     * idiom for scoped named constants — SDL3's own audio/SDL_audioresample.c
     * uses exactly this inside GenerateResamplerFilter()) or a bare local
     * "struct Foo { ... };" type declaration with no variable. By this point
     * ParseTypeSpecifier's own enum/struct/union branch has already fully
     * parsed the body and registered every enum constant / the struct's
     * field list via its own side effects (symtable_define_enum_val,
     * symtable_define_struct) — a trailing ';' with no identifier just means
     * "that's the whole statement," not an error. Previously fell through
     * to the same "expected variable name after type" error the very next
     * declarator-parsing branch below raises, which then only recovered by
     * eating one semicolon — leaving this function's own still-open brace
     * depth mismatched with the caller's expectations and cascading into
     * unrelated later parse failures (confirmed via SDL_audioresample.c). */
    if (chk(p,TOK_SEMICOLON)) { adv(p); return ast_number(0,line); }

    /* Pointer stars directly after type */
    while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) type->pointer_depth++; adv(p); }

    /* Function pointer declaration: type (*name)(...) or type (*name[N])(...)
     * After the base type we see '(' '*' name ')' '(' params ')'
     * We parse this as a plain pointer variable of the return type.        */
    if (chk(p,TOK_LPAREN)) {
        adv(p); /* consume '(' */
        skip_calling_convention(p); /* e.g. "int (__cdecl *compare)(...)" */
        while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) type->pointer_depth++; adv(p); }
        /* optional name */
        char name[256]; name[0]='\0';
        if (chk(p,TOK_IDENT)) {
            strncpy(name, tok_ident(&cur(p)), sizeof name-1);
            adv(p);
        }
        /* optional array dimension: (*ops[3]) */
        int array_size = -1;
        if (chk(p,TOK_LBRACKET)) {
            adv(p);
            if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { array_size=parse_const_expr(p); }
            else array_size = 0;
            eat(p,TOK_RBRACKET);
        }
        eat(p,TOK_RPAREN);
        /* skip parameter list: (...) */
        if (chk(p,TOK_LPAREN)) {
            adv(p);
            int depth=1;
            while (!chk(p,TOK_EOF) && depth>0) {
                if (chk(p,TOK_LPAREN)) depth++;
                else if (chk(p,TOK_RPAREN)) depth--;
                adv(p);
            }
        }
        /* optional initialiser */
        ASTNode *init=NULL;
        if (chk(p,TOK_ASSIGN)) { adv(p); init=ParseAssignment(p); }
        eat(p,TOK_SEMICOLON);
        if (name[0]) {
            if (array_size >= 0) type->array_size = array_size;
            symtable_define_var(p->sym, name, type);
            return ast_var_decl(storage[0]?storage:NULL, type, name,
                                init, array_size, line);
        }
        return ast_number(0, line); /* anonymous — skip */
    }

    if (!chk(p,TOK_IDENT)) {
        parse_error(p,"expected variable name after type");
        eat(p,TOK_SEMICOLON);
        return ast_number(0,line);
    }
    char name[256]; strncpy(name,tok_ident(&cur(p)),sizeof name-1); adv(p);

    /* Array dimension — may be a constant (fixed array) or expression (VLA) */
    int array_size = -1;
    int array_size2 = 0; /* second dimension: T name[N][M] (scoped 2D support) */
    ASTNode *vla_expr = NULL;
    if (chk(p,TOK_LBRACKET)) {
        adv(p);
        if (chk(p,TOK_RBRACKET)) { array_size=0; }
        else if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { array_size=parse_const_expr(p); }
        else if (chk(p,TOK_STAR) && p->lex->cur.kind==TOK_STAR) {
            /* VLA with unspecified size [*] in prototype */
            adv(p); array_size=-2; vla_expr=ast_number(0,line);
        } else {
            /* VLA: runtime expression */
            vla_expr = ParseExpression(p);
            array_size = -2; /* VLA marker */
        }
        eat(p,TOK_RBRACKET);
        if (array_size >= 0 && chk(p,TOK_LBRACKET)) {
            adv(p);
            if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { array_size2=parse_const_expr(p); }
            eat(p,TOK_RBRACKET);
        }
    }

    ASTNode *init = NULL;
    if (chk(p,TOK_ASSIGN)) {
        adv(p);
        if (chk(p,TOK_LBRACE)) {
            /* Array initialiser {a, b, c} or {{a,b},{c,d}} (array-of-struct/
             * array-of-array) — nested braces are flattened into one
             * element list, same as the global-declaration initialiser
             * path just below in this file, since squash's codegen lays
             * initialiser lists out sequentially either way. */
            adv(p);
            int ecap=256; ASTNode **elems=(ASTNode**)malloc(ecap*sizeof(ASTNode*)); int ne=0;
            while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                parse_flatten_brace_elem(p, &elems, &ne, &ecap);
                if (!chk(p,TOK_COMMA)) break; adv(p);
            }
            eat(p,TOK_RBRACE);
            init = ast_block(elems, ne, line); /* reuse AST_BLOCK as initialiser list */
            free(elems);
            /* If array size was unspecified (char x[] = {...}), infer from
             * element count. array_size==0 means empty brackets were seen
             * ("T x[] = {...}"); array_size==-1 means no brackets at all
             * (a plain, non-array declaration, e.g. a struct's own brace
             * initializer like "static struct {...} s = {a,b,c};") — must
             * NOT be reinterpreted as an array just because <= 0, or a
             * plain struct/union global silently becomes a bogus N-element
             * array of its own type, and its brace initializer is then
             * skipped entirely by codegen's array/struct handling. */
            if (array_size == 0) array_size = ne;
        } else {
            init = ParseAssignment(p);
            /* "char x[] = \"literal\";" (empty brackets, non-brace
             * initializer) needs the SAME array_size inference the
             * brace-list case just above already gets ("if (array_size
             * == 0) array_size = ne;") -- without it, array_size stayed
             * 0 for ANY non-brace initializer of an empty-bracket array,
             * string literal included, so codegen allocated/treated the
             * variable as a plain scalar (one pointer/int-sized slot)
             * instead of a real N-byte buffer. Confirmed as a real,
             * reproducible bug this session: a minimal "const char
             * x[] = \"abc\";" local segfaulted (the initializer-copy
             * code wrote "abc\0" into a slot sized for one scalar,
             * corrupting the stack), and the identical shape with
             * "static" silently read back as a permanently-zeroed
             * pointer instead of crashing (see codegen.c's own
             * "is_static" comment for that half of the bug -- a
             * DIFFERENT root cause, fixed separately there). +1 for the
             * NUL terminator, matching how a real C string-literal
             * array initializer sizes itself ("char x[] = \"ab\";" is a
             * 3-element array, not 2). */
            if (array_size == 0 && init && init->kind == AST_STRING && init->str.value) {
                array_size = (int)strlen(init->str.value) + 1;
            }
        }
    }
    /* Multi-declarator: "int a=1, *b=NULL, c[4];" — collect all into a block */
    if (chk(p, TOK_COMMA)) {
        /* Build a list of VarDecl nodes, one per declarator */
        ASTNode *decls[64]; int nd=0;
        if (array_size >= 0) { type->array_size = array_size; type->array_size2 = array_size2; }
        symtable_define_var(p->sym, name, type);
        { ASTNode *_d0 = ast_var_decl(storage[0]?storage:NULL, type, name, init, array_size, line);
          _d0->var_decl.array_size2 = array_size2; decls[nd++] = _d0; }
        while (chk(p, TOK_COMMA) && nd<63) {
            adv(p); /* consume ',' */
            /* Each additional declarator gets a fresh copy of the BASE type */
            if (!type) { parse_error(p,"internal: type is null before copy"); break; }
            TypeInfo *t2 = (TypeInfo*)calloc(1, sizeof(TypeInfo));
            if (!t2) { parse_error(p,"internal: calloc returned null"); break; }
            *t2 = *type;
            if (type->base) t2->base = my_strdup(type->base);
            t2->pointer_depth = base_ptr_depth;  /* reset to base, not first declarator's depth */
            while (chk(p, TOK_STAR)) { adv(p); t2->pointer_depth++; }
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected declarator name"); break; }
            char n2[256]; strncpy(n2,tok_ident(&cur(p)),sizeof n2-1); n2[sizeof n2-1]='\0'; adv(p);
            int arr2=-1; int arr2b=0;
            if (chk(p,TOK_LBRACKET)) {
                adv(p);
                if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { arr2=parse_const_expr(p); } else arr2=0;
                eat(p,TOK_RBRACKET);
                if (arr2 >= 0 && chk(p,TOK_LBRACKET)) {
                    adv(p);
                    if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { arr2b=parse_const_expr(p); }
                    eat(p,TOK_RBRACKET);
                }
            }
            ASTNode *init2=NULL;
            if (chk(p,TOK_ASSIGN)) {
                adv(p);
                if (chk(p,TOK_LBRACE)) {
                    /* Brace initialiser on a NON-first declarator, e.g. the
                     * "tc[3]={0}" in "stbi_uc has_trans=0, tc[3]={0};"
                     * (stb_image.h's real PNG-parser locals) — mirrors the
                     * first declarator's own brace-list handling above;
                     * ParseAssignment alone can't parse a bare "{...}". */
                    adv(p);
                    int ecap2=256; ASTNode **elems2=(ASTNode**)malloc(ecap2*sizeof(ASTNode*)); int ne2=0;
                    while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                        parse_flatten_brace_elem(p, &elems2, &ne2, &ecap2);
                        if (!chk(p,TOK_COMMA)) break; adv(p);
                    }
                    eat(p,TOK_RBRACE);
                    init2 = ast_block(elems2, ne2, line);
                    free(elems2);
                    if (arr2 == 0) arr2 = ne2;
                } else {
                    init2=ParseAssignment(p);
                }
            }
            /* Always reset array_size/array_size2 from THIS declarator's own
             * brackets (arr2/arr2b, -1 if none) rather than only overwriting
             * when arr2>=0. `t2` was shallow-copied from `*type`, which the
             * FIRST declarator in the list may have already mutated in place
             * (line ~1387: "type->array_size = array_size;") — e.g. "int
             * arr[4], *p = arr;" left `type->array_size==4` baked in, and
             * since `p` has no brackets of its own (arr2 stays -1), the old
             * "if (arr2 >= 0)" guard never reset it, so `p` silently
             * inherited arr's array_size==4. symtable_define_var() then
             * sized `p` as if it were "array of 4 pointers" (4 * 8 bytes)
             * instead of a single 8-byte pointer, corrupting the stack
             * layout of every variable declared after it. */
            t2->array_size = arr2; t2->array_size2 = arr2b;
            symtable_define_var(p->sym, n2, t2);
            { ASTNode *_d = ast_var_decl(storage[0]?storage:NULL, t2, n2, init2, arr2, line);
              _d->var_decl.array_size2 = arr2b; decls[nd++] = _d; }
        }
        eat(p,TOK_SEMICOLON);
        if (nd==1) return decls[0];
        return ast_block(decls, nd, line);
    }

    eat(p,TOK_SEMICOLON);

    if (array_size >= 0) { type->array_size = array_size; type->array_size2 = array_size2; }
    if (array_size == -2) type->array_size = -1; /* VLA: treat as pointer-like in symtable */
    symtable_define_var(p->sym, name, type);
    { ASTNode *_n=ast_var_decl(storage[0]?storage:NULL,type,name,init,array_size,line);
      _n->var_decl.vla_expr=vla_expr; _n->var_decl.array_size2=array_size2; return _n; }
}

/* =========================================================================
 * ParseFunction — type name ( params ) { body } or ;
 * ========================================================================= */
ASTNode *ParseFunction(Parser *p, const char *storage, TypeInfo *ret, const char *name) {
    int line=cur(p).line;
    eat(p,TOK_LPAREN);
    int params_cap=32; ASTNode **params=(ASTNode**)malloc(params_cap*sizeof(ASTNode*)); int paramc=0; int variadic=0;
    symtable_push_scope(p->sym);
    symtable_reset_locals(p->sym);
    if (!chk(p,TOK_RPAREN)) {
        int param_byte_off=0; /* cumulative 32-bit param stack offset */
        do {
            if (chk(p,TOK_ELLIPSIS)) { variadic=1; adv(p); break; }
            if (!is_type_start(p)) { parse_error(p,"expected parameter type"); break; }
            TypeInfo *pt = ParseTypeSpecifier(p);
            /* "void f(void)" — single void param with no name = zero params in C */
            if (pt->base && strcmp(pt->base,"void")==0 && pt->pointer_depth==0
                && !chk(p,TOK_IDENT) && !chk(p,TOK_STAR) && chk(p,TOK_RPAREN)) {
                break; /* treat as empty param list */
            }
            /* check for function pointer param: type (*name)(params) */
            while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) {
                if (chk(p,TOK_STAR)) pt->pointer_depth++;
                adv(p);
            }
            char pname[256]="";
            if (chk(p,TOK_LPAREN)) {
                /* Function-pointer parameter: Type (*name)(params) — e.g.
                 * SDL_FindPhysicalAudioDeviceByCallback's callback arg.
                 * Same shape as the function-pointer struct-field/var-decl
                 * cases handled elsewhere: treat as a plain pointer
                 * parameter of the return type. */
                adv(p); /* consume '(' */
                skip_calling_convention(p);
                while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) pt->pointer_depth++; adv(p); }
                if (chk(p,TOK_IDENT)) { strncpy(pname,tok_ident(&cur(p)),sizeof pname-1); adv(p); }
                eat(p,TOK_RPAREN);
                /* skip the parameter list: (...) */
                if (chk(p,TOK_LPAREN)) {
                    adv(p);
                    int depth=1;
                    while (!chk(p,TOK_EOF) && depth>0) {
                        if (chk(p,TOK_LPAREN)) depth++;
                        else if (chk(p,TOK_RPAREN)) depth--;
                        adv(p);
                    }
                }
            } else if (chk(p,TOK_IDENT)) { strncpy(pname,tok_ident(&cur(p)),sizeof pname-1); adv(p); }
            /* array param: name[] or name[N] (and possibly further
             * dimensions, e.g. "int prev[3]" or "int grid[3][4]") — a
             * function parameter's FIRST array dimension is decorative in C
             * (the parameter itself decays to a pointer, so "int prev[3]"
             * and "int prev[99]" and "int *prev" are all identical
             * parameter types) and was previously just skipped, along with
             * every dimension after it.
             *
             * That's wrong for a SECOND (or later) dimension: "char
             * args[][MAX]" decays to "char (*args)[MAX]", a pointer whose
             * POINTEE is an array of MAX chars — MAX is the row stride
             * needed to compute &args[i], not decorative at all. Silently
             * dropping it left every such 2D-array parameter behaving like
             * a plain "char *args" (stride 1) instead of stride MAX, so
             * indexing args[i] inside the callee computed the wrong
             * address (off by a factor of MAX) and, since AST_INDEX/
             * elem_size_of() also had no idea this was a 2D access, args[i]
             * evaluated as a scalar BYTE LOAD instead of decaying to a
             * char* row pointer — confirmed via a minimal repro
             * ("void show(char args[][1024], int n) {..args[i]..}" passed
             * to printf("%s", args[i])) segfaulting because args[i] handed
             * printf a garbage byte value instead of a real pointer.
             *
             * Fix: mirror parse_typedef_array_dims()'s existing 2D
             * array_size/array_size2 convention (used for local/global
             * declarators and typedefs) — capture the SECOND bracket's
             * constant expression into pt->array_size2 so downstream
             * codegen's existing array_size2>0 machinery (already correct
             * for real T x[N][M] locals/globals) recognizes this parameter
             * as a 2D row-pointer and uses array_size2 as the row stride.
             * Dimensions beyond the second are still just consumed (not
             * separately modeled), same as parse_typedef_array_dims(). */
            if (chk(p,TOK_LBRACKET)) {
                pt->pointer_depth++;
                adv(p); /* consume first '[' */
                while (!chk(p,TOK_RBRACKET) && !chk(p,TOK_EOF)) adv(p);
                if (chk(p,TOK_RBRACKET)) adv(p);
                if (chk(p,TOK_LBRACKET)) {
                    adv(p);
                    int dim2 = 0;
                    if (!chk(p,TOK_RBRACKET)) dim2 = parse_const_expr(p);
                    eat(p,TOK_RBRACKET);
                    pt->array_size2 = dim2;
                }
                while (chk(p,TOK_LBRACKET)) {
                    adv(p);
                    while (!chk(p,TOK_RBRACKET) && !chk(p,TOK_EOF)) adv(p);
                    if (chk(p,TOK_RBRACKET)) adv(p);
                }
            }
            symtable_define_param(p->sym, pname, pt, paramc, param_byte_off);
            /* Advance cumulative offset by actual param size (doubles=8, rest=4) */
            { int psz = typeinfo_size(pt, p->sym->is_64bit); param_byte_off += (psz > 4 ? psz : 4); }
            if (paramc>=params_cap) { params_cap*=2; params=(ASTNode**)realloc(params,params_cap*sizeof(ASTNode*)); }
            params[paramc++]=ast_param(pt, pname[0]?pname:NULL, 0, cur(p).line);
        } while (chk(p,TOK_COMMA)&&adv_kind(p)!=TOK_EOF);
    }
    eat(p,TOK_RPAREN);

    /* Register function in symbol table.
     * If already defined (e.g. forward declaration), don't re-add.
     * For a full definition, update func_node to point to the body node. */
    Symbol *existing_func = symtable_lookup(p->sym, name);
    if (!existing_func || existing_func->kind != SYM_FUNC) {
        symtable_define_func(p->sym, name, ret, paramc, NULL);
    }

    ASTNode *body=NULL;
    if (chk(p,TOK_LBRACE)) {
        body = ParseBlock(p);
    } else {
        eat(p,TOK_SEMICOLON); /* forward declaration */
        /* extern/forward declaration: register as import so calls use IAT.
         * BUT: if a real, body-bearing definition of this name already
         * exists (an ordinary same-translation-unit function that some
         * OTHER file also merely re-declares "extern" before use — a very
         * common, completely unremarkable C pattern), do NOT shadow it with
         * a bogus import entry. symtable's global scope is a head-inserted
         * linked list (newest registration wins lookups), so blindly
         * calling symtable_define_import here would silently turn a
         * perfectly ordinary local function call into an unresolved
         * "external" one for every call site processed afterward — this is
         * exactly the class of bug the func_node-clobber guard just above
         * already protects against, but for the symbol's `kind` rather than
         * its func_node. */
        if (storage && strcmp(storage,"extern")==0) {
            Symbol *existing = symtable_lookup(p->sym, name);
            int already_defined_locally =
                existing && existing->kind == SYM_FUNC &&
                existing->func_node && existing->func_node->func.body;
            /* Only register if not already a known Win API import, AND not
             * shadowing a real local definition. */
            if (!already_defined_locally && (!existing || existing->kind != SYM_IMPORT)) {
                /* Pass the real declared return type through (see
                 * symtable_define_import_typed()'s comment) instead of
                 * letting it default to "int" — this "extern" placeholder
                 * dll marker means "defined in a sibling .sqo object", not
                 * "genuine Win32 API import", so unlike a real DLL import
                 * its return type is whatever the source actually said. */
                symtable_define_import_typed(p->sym, name, "extern", ret);
            }
        }
    }
    symtable_pop_scope(p->sym);
    ASTNode *fdecl = ast_func_decl(storage, ret, name, params, paramc, variadic, body, line);
    free(params);
    /* Always store func_decl so callers can inspect parameter types (e.g. float vs double).
     * Use func_node->func.body != NULL to distinguish defined vs. forward-declared.
     * BUT: don't clobber an existing body-bearing func_node with a later bodyless
     * re-declaration (e.g. the same helper forward-declared in several combined
     * files after its real definition) — codegen treats a bodyless func_node as
     * "external, resolve via the GOT", so losing the body reference turns a
     * perfectly ordinary local function call into an unresolved dynamic import. */
    {
        Symbol *fs = symtable_lookup(p->sym, name);
        /* Also cover SYM_IMPORT: a bodyless "extern RET name(params);"
         * prototype (e.g. every SDL3 public API declaration, all of which
         * start with "extern SDL_DECLSPEC ... SDLCALL") gets registered as
         * a SYM_IMPORT a few lines above this block (see the
         * symtable_define_import call for the "extern" storage-class
         * case), which SHADOWS the plain SYM_FUNC symtable_define_func()
         * registered earlier in this same function — real, on-purpose
         * behavior for genuine external DLL symbols. But that import
         * symbol never got a func_node before this fix, and
         * param_is_single_float()/param_type_sz() (codegen.c) both
         * unconditionally return "not a float" when func_node is NULL —
         * so EVERY call to a function declared this way (which is to say,
         * every SDL3 API call made from example/user code, since the
         * example is compiled as a separate object linked against
         * sdl_common.sqo and never sees a body for these functions) lost
         * the double-to-float narrowing conversion for any float argument
         * not in the first position. Concretely: SDL_SetRenderDrawColorFloat's
         * "r"/"g"/"b"/"a" args arrived at the callee as raw unnarrowed
         * doubles instead of floats — and for round values like 0.25/0.5/
         * 0.75/1.0, a double's bit pattern happens to have zeros in its low
         * 32 bits (where the callee's movss reads its 4-byte float from),
         * so every channel silently read back as 0.0 — the actual cause of
         * every squash-built SDL3 window rendering solid black/white
         * instead of the requested clear color. Giving the import symbol a
         * real func_node (with the accurately-parsed parameter list) fixes
         * this without touching the SYM_IMPORT-vs-SYM_FUNC call-dispatch
         * logic itself (asm_call_import vs asm_call_direct), which is keyed
         * on `sym->kind` elsewhere in codegen.c, not on func_node. */
        if (fs && (fs->kind == SYM_FUNC || fs->kind == SYM_IMPORT) &&
            !(fs->func_node && fs->func_node->func.body && !body))
            fs->func_node = fdecl;
    }
    return fdecl;
}

/* =========================================================================
 * parse_program — top-level declarations
 * ========================================================================= */
ASTNode *parse_program(Parser *p) {
    /* Grows as needed — a fixed 1024-entry array silently overflowed (and
     * corrupted the stack) once squash's own combined source (14 files,
     * ~1200 top-level declarations) was fed through self-hosting. */
    int decls_cap = 1024;
    ASTNode **decls = (ASTNode**)malloc(decls_cap * sizeof(ASTNode*));
    int count=0;
    while (!chk(p,TOK_EOF)) {
        int line=cur(p).line;

        /* Stray top-level ';' (empty declaration) — real MSVC/GCC headers
         * occasionally contain one (e.g. after a macro-generated typedef),
         * and every mainstream compiler silently accepts it. Skip rather
         * than erroring. */
        if (chk(p,TOK_SEMICOLON)) { adv(p); continue; }

        /* struct / union definition at top level */
        if (chk(p,TOK_STRUCT)||chk(p,TOK_UNION)) {
            TypeInfo *ti = ParseTypeSpecifier(p);
            if (chk(p,TOK_SEMICOLON)) { adv(p); typeinfo_free(ti); continue; }
            /* struct Foo var; or struct Foo fn(...) */
            while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) ti->pointer_depth++; adv(p); }
            if (!chk(p,TOK_IDENT)) { parse_error(p,"expected identifier"); continue; }
            char name[256]; strncpy(name,tok_ident(&cur(p)),sizeof name-1); adv(p);
            if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
            if (chk(p,TOK_LPAREN)) decls[count++]=ParseFunction(p,NULL,ti,name);
            else {
                /* global var */
                int arr=-1;
                if (chk(p,TOK_LBRACKET)) { adv(p); if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)){ arr=parse_const_expr(p); } eat(p,TOK_RBRACKET); }
                ASTNode *init=NULL;
                if (chk(p,TOK_ASSIGN)){
                    adv(p);
                    if (chk(p,TOK_LBRACE)) {
                        /* Brace-enclosed initializer: { val, val, ... } or { {v,v}, ... }.
                         * Grows as needed — large tables like SDL's own
                         * controller_list.h array (613 entries * 3 fields
                         * flattened = ~1839 elements) blow past any small
                         * fixed cap. */
                        adv(p); /* consume { */
                        int ecap=1024; ASTNode **elems=(ASTNode**)malloc(ecap*sizeof(ASTNode*)); int ne=0;
                        while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                            parse_flatten_brace_elem(p, &elems, &ne, &ecap);
                            if (!chk(p,TOK_COMMA)) break; adv(p);
                        }
                        eat(p,TOK_RBRACE);
                        init = ast_block(elems, ne, line);
                        free(elems);
                    } else {
                        init=ParseAssignment(p);
                    }
                }
                eat(p,TOK_SEMICOLON);
                symtable_define_global(p->sym,name,ti,arr);
                if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
                decls[count++]=ast_var_decl(NULL,ti,name,init,arr,line);
            }
            continue;
        }

        /* enum */
        if (chk(p,TOK_ENUM)) {
            TypeInfo *ti = ParseTypeSpecifier(p); /* handles enum body */
            if (chk(p,TOK_SEMICOLON)) { adv(p); typeinfo_free(ti); continue; }
            /* enum Foo var; */
            while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) ti->pointer_depth++; adv(p); }
            if (chk(p,TOK_IDENT)) {
                char name[256]; strncpy(name,tok_ident(&cur(p)),sizeof name-1); adv(p);
                ASTNode *init=NULL;
                if (chk(p,TOK_ASSIGN)){adv(p); init=ParseAssignment(p);}
                eat(p,TOK_SEMICOLON);
                symtable_define_global(p->sym,name,ti,-1);
                if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
                decls[count++]=ast_var_decl(NULL,ti,name,init,-1,line);
            } else { eat(p,TOK_SEMICOLON); typeinfo_free(ti); }
            continue;
        }

        /* typedef */
        if (chk(p,TOK_TYPEDEF)) {
            adv(p);
            TypeInfo *ti = ParseTypeSpecifier(p);
            int td_base_ptr_depth = ti->pointer_depth; /* save before first declarator's stars */
            while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) ti->pointer_depth++; adv(p); }
            /* Function pointer typedef: typedef RET (*NAME)(PARAMS);
             * Squash's TypeInfo has no representation for a function
             * pointer's own parameter/return types, so — matching how every
             * such typedef we've found in real headers is actually used
             * (the value is immediately cast to a concrete function-pointer
             * type before being stored or called, e.g. GLX's
             * "gf->sym = (T)glXGetProcAddressARB(...)") — treat NAME as an
             * opaque pointer alias for the base type and skip the parameter
             * list, rather than failing to parse the declaration at all. */
            if (chk(p,TOK_LPAREN)) {
                adv(p); /* ( */
                skip_calling_convention(p); /* typedef RET (WINAPI *NAME)(PARAMS); */
                while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) adv(p);
                if (!chk(p,TOK_IDENT)) { parse_error(p,"expected typedef name"); eat(p,TOK_SEMICOLON); continue; }
                char alias[256]; strncpy(alias,tok_ident(&cur(p)),sizeof alias-1); adv(p);
                eat(p,TOK_RPAREN); /* ) closing (*NAME) */
                /* The parameter list is usually right here — "typedef RET
                 * (*NAME)(PARAMS);" — but not always: real winnt.h also has
                 * "typedef ENCLAVE_TARGET_FUNCTION
                 * (*PENCLAVE_TARGET_FUNCTION);", a plain pointer-to-already-
                 * a-function-type typedef with the redundant parens but NO
                 * trailing "(PARAMS)" at all (the signature already lives
                 * in ENCLAVE_TARGET_FUNCTION itself, a prior bare function-
                 * TYPE typedef). Only consume/skip a parameter list if one
                 * is actually there. */
                if (chk(p,TOK_LPAREN)) {
                    adv(p); /* ( opening the parameter list */
                    int depth = 1;
                    while (depth > 0 && !chk(p,TOK_EOF)) {
                        if (chk(p,TOK_LPAREN)) depth++;
                        else if (chk(p,TOK_RPAREN)) depth--;
                        adv(p);
                    }
                }
                eat(p,TOK_SEMICOLON);
                ti->pointer_depth++;
                symtable_define_typedef(p->sym, alias, ti);
                if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
                decls[count++]=ast_typedef_decl(ti, alias, line);
                continue;
            }
            /* A calling-convention keyword may appear here too, between the
             * return type and the declarator name, for the same bare
             * function-TYPE typedef case handled below — e.g. real
             * winnt.h's "typedef EXCEPTION_DISPOSITION NTAPI
             * EXCEPTION_ROUTINE(...);" (NTAPI expands to __stdcall). */
            skip_calling_convention(p);
            /* Accept TOK_BOOL too: shim headers commonly do
             * "typedef int _Bool;" to provide C99/C11 bool support, but the
             * lexer already tokenizes _Bool as a keyword, not a plain
             * identifier. */
            if (!chk(p,TOK_IDENT) && !chk(p,TOK_BOOL)) { parse_error(p,"expected typedef name"); eat(p,TOK_SEMICOLON); continue; }
            char alias[256]; strncpy(alias,tok_ident(&cur(p)),sizeof alias-1); adv(p);
            /* Bare function-TYPE typedef (no '*'): "typedef RET NAME(PARAMS);"
             * — e.g. real winnt.h's "typedef EXCEPTION_DISPOSITION
             * EXCEPTION_ROUTINE(struct _EXCEPTION_RECORD *, ...);", almost
             * always used right afterward only as "typedef NAME *PNAME;"
             * (a real function pointer, handled by the ordinary star-typedef
             * path above since NAME is now a known type). squash has no
             * function-type representation any more than it does for the
             * "typedef RET (*NAME)(PARAMS);" function-POINTER case just
             * above — treated the same way, as an opaque alias of the
             * return type, skipping the parameter list, but WITHOUT
             * bumping pointer_depth (unlike the function-pointer case):
             * NAME itself denotes a function, not a pointer to one. */
            if (chk(p,TOK_LPAREN)) {
                adv(p);
                int fdepth = 1;
                while (fdepth > 0 && !chk(p,TOK_EOF)) {
                    if (chk(p,TOK_LPAREN)) fdepth++;
                    else if (chk(p,TOK_RPAREN)) fdepth--;
                    adv(p);
                }
                symtable_define_typedef(p->sym, alias, ti);
                if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
                decls[count++]=ast_typedef_decl(ti, alias, line);
                eat(p,TOK_SEMICOLON);
                continue;
            }
            parse_typedef_array_dims(p, ti);
            symtable_define_typedef(p->sym, alias, ti);
            if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
            decls[count++]=ast_typedef_decl(ti, alias, line);

            /* Additional comma-separated declarators sharing the same base
             * type -- e.g. real winnt.h's "typedef signed char INT8,
             * *PINT8;" (dozens of these: INT8/INT16/.../UINT64, LONG32,
             * etc). Each declarator gets its own pointer-depth, reset from
             * the base type — same pattern as the analogous multi-
             * declarator global-variable list above. */
            while (chk(p,TOK_COMMA)) {
                adv(p);
                TypeInfo *ti2 = (TypeInfo*)calloc(1, sizeof(TypeInfo));
                *ti2 = *ti;
                if (ti->base) ti2->base = my_strdup(ti->base);
                ti2->pointer_depth = td_base_ptr_depth;
                while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) ti2->pointer_depth++; adv(p); }
                if (!chk(p,TOK_IDENT) && !chk(p,TOK_BOOL)) { parse_error(p,"expected typedef name"); break; }
                char alias2[256]; strncpy(alias2,tok_ident(&cur(p)),sizeof alias2-1); alias2[sizeof alias2-1]='\0'; adv(p);
                parse_typedef_array_dims(p, ti2);
                symtable_define_typedef(p->sym, alias2, ti2);
                if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
                decls[count++]=ast_typedef_decl(ti2, alias2, line);
            }
            eat(p,TOK_SEMICOLON);
            continue;
        }

        /* _Static_assert at top level */
        if (chk(p,TOK_STATIC_ASSERT)) {
            adv(p); eat(p,TOK_LPAREN);
            ParseExpression(p); /* evaluate but ignore */
            if (chk(p,TOK_COMMA)) { adv(p); if (chk(p,TOK_STRING)) adv(p); }
            eat(p,TOK_RPAREN); eat(p,TOK_SEMICOLON);
            continue;
        }

        /* storage class */
        char storage[32]="";
        if (chk(p,TOK_STATIC))  { strncpy(storage,"static",sizeof storage-1);  adv(p); }
        else if (chk(p,TOK_EXTERN)) { strncpy(storage,"extern",sizeof storage-1); adv(p); }
        /* consume inline/const/volatile/C11 qualifiers silently */
        for (;;) {
            if (chk(p,TOK_INLINE)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { adv(p); continue; }
            if (chk(p,TOK_CONSTEXPR)||chk(p,TOK_NORETURN)||chk(p,TOK_THREAD_LOCAL)) { adv(p); continue; }
            if (chk(p,TOK_ALIGNAS)) {
                adv(p);
                if (chk(p,TOK_LPAREN)) { adv(p); int d=1; while (!chk(p,TOK_EOF)&&d>0) { if(chk(p,TOK_LPAREN))d++; else if(chk(p,TOK_RPAREN))d--; adv(p); } }
                continue;
            }
            break;
        }
        /* also handle 'static inline' or 'inline static' */
        if (chk(p,TOK_STATIC)&&!storage[0]) { strncpy(storage,"static",sizeof storage-1); adv(p); }
        for (;;) {
            if (chk(p,TOK_INLINE)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { adv(p); continue; }
            if (chk(p,TOK_CONSTEXPR)||chk(p,TOK_NORETURN)||chk(p,TOK_THREAD_LOCAL)) { adv(p); continue; }
            break;
        }

        if (!is_type_start(p)) {
            { char _em[128]; snprintf(_em,sizeof _em,"expected declaration at top level, got '%s'",token_kind_name(cur(p).kind)); parse_error(p,_em); }
            adv(p); continue;
        }

        TypeInfo *ret = ParseTypeSpecifier(p);
        int base_ptr_depth = ret->pointer_depth; /* save before first declarator's stars */
        /* C's declaration-specifiers are an unordered set — storage-class
         * and function-specifier keywords may legally appear AFTER the type
         * too, not just before it (e.g. real <stringapiset.h>'s "inline
         * LPUWSTR static ua_CharUpperW(...)"). storage[] may already have
         * been set by the pre-type check further up; only fill it in here
         * if it's still empty, so whichever position the keyword actually
         * appeared in wins exactly once. */
        while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)||
               chk(p,TOK_STATIC)||chk(p,TOK_EXTERN)||chk(p,TOK_INLINE)) {
            if (chk(p,TOK_STAR)) ret->pointer_depth++;
            else if (chk(p,TOK_STATIC) && !storage[0]) strncpy(storage,"static",sizeof storage-1);
            else if (chk(p,TOK_EXTERN) && !storage[0]) strncpy(storage,"extern",sizeof storage-1);
            adv(p);
        }
        skip_calling_convention(p); /* RET __stdcall name(...); e.g. WGL's "int WINAPI ChoosePixelFormat(...)" */

        /* Global function-pointer VARIABLE: "RetType (*name)(params) [=
         * init];" — e.g. SDL3's own audio/SDL_audiotypecvt.c dispatch-table
         * globals ("static void (*SDL_Convert_S8_to_F32)(float *dst, const
         * Sint8 *src, int num_samples) = NULL;"). The '(' comes directly
         * after the return type, before any identifier — the plain
         * "int foo(...)" function-DEFINITION shape below only checks for
         * TOK_LPAREN *after* first consuming an identifier, so this case
         * fell straight into the bare "expected identifier" error one
         * declarator name later. ParseVariable (this same function's local-
         * scope counterpart, used inside function bodies) already handles
         * this exact shape; this mirrors it for file scope. */
        if (chk(p,TOK_LPAREN)) {
            adv(p);
            skip_calling_convention(p);
            while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) ret->pointer_depth++; adv(p); }
            char fpname[256]; fpname[0]='\0';
            if (chk(p,TOK_IDENT)) { strncpy(fpname,tok_ident(&cur(p)),sizeof fpname-1); adv(p); }
            eat(p,TOK_RPAREN);
            /* skip the parameter list */
            if (chk(p,TOK_LPAREN)) {
                adv(p); int pdepth=1;
                while (!chk(p,TOK_EOF) && pdepth>0) {
                    if (chk(p,TOK_LPAREN)) pdepth++;
                    else if (chk(p,TOK_RPAREN)) pdepth--;
                    adv(p);
                }
            }
            ASTNode *fpinit=NULL;
            if (chk(p,TOK_ASSIGN)) { adv(p); fpinit=ParseAssignment(p); }
            eat(p,TOK_SEMICOLON);
            if (fpname[0]) {
                symtable_define_global(p->sym, fpname, ret, -1);
                if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
                decls[count++] = ast_var_decl(storage[0]?storage:NULL, ret, fpname, fpinit, -1, line);
            }
            continue;
        }

        if (!chk(p,TOK_IDENT)) {
            parse_error(p,"expected identifier");
            eat(p,TOK_SEMICOLON); continue;
        }
        char name[256]; strncpy(name,tok_ident(&cur(p)),sizeof name-1); adv(p);

        if (chk(p,TOK_LPAREN)) {
            if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
            decls[count++] = ParseFunction(p, storage[0]?storage:NULL, ret, name);
        } else {
            /* global variable(s) — supports comma-separated multi-declarator
             * lists: TYPE a = x, *b = y, c[4]; each declarator after the
             * first gets its own TypeInfo copy so that per-declarator
             * pointer/array attributes don't clobber the shared base type. */
            int arr=-1; int arr2=0;
            if (chk(p,TOK_LBRACKET)) {
                adv(p);
                if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { arr=parse_const_expr(p); } else arr=0;
                eat(p,TOK_RBRACKET);
                /* Second dimension: TYPE name[N][M]; — this top-level
                 * declarator path only ever parsed one bracket pair, so a
                 * genuinely plain "static const int format_list[8][9] = ...;"
                 * (SDL3's own SDL_audio.c, a lookup table with no macros or
                 * expressions involved at all) failed with "expected ';' but
                 * got '['" the instant the second "[" was reached — global
                 * 2D arrays were entirely unparseable, not merely an
                 * expression-in-brackets edge case. Local variables and
                 * struct fields already support this same shape (see
                 * ParseVariable's own array_size2/arr2b handling and struct
                 * field parsing above); this brings the top-level declarator
                 * path in line with both. */
                if (chk(p,TOK_LBRACKET)) {
                    adv(p);
                    if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { arr2=parse_const_expr(p); }
                    eat(p,TOK_RBRACKET);
                }
            }
            ret->array_size2 = arr2;
            ASTNode *init=NULL;
            if (chk(p,TOK_ASSIGN)) {
                adv(p);
                if (chk(p,TOK_LBRACE)) {
                    /* Braced global initializer: arr = {a,b} or = {{a,b},{c,d}}.
                     * Grows as needed (see the sibling copy of this loop
                     * above for why a fixed cap isn't enough). */
                    adv(p);
                    int ecap=1024; ASTNode **elems=(ASTNode**)malloc(ecap*sizeof(ASTNode*)); int ne=0;
                    while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)) {
                        parse_flatten_brace_elem(p, &elems, &ne, &ecap);
                        if (!chk(p,TOK_COMMA)) break; adv(p);
                    }
                    eat(p,TOK_RBRACE);
                    init = ast_block(elems, ne, line);
                    free(elems);
                    /* Infer array size from initializer if unspecified.
                     * See the sibling copy of this comment above for why
                     * this must be ==0 (empty brackets), not <=0 — arr==-1
                     * means no brackets at all (not an array). */
                    if (arr == 0) arr = ne;
                } else {
                    init=ParseAssignment(p);
                }
            }
            if (arr > 0) ret->array_size = arr;
            /* Don't re-register an already-defined global under a new,
             * separate Symbol object — symtable's global scope is a
             * head-inserted linked list (newest registration is found
             * FIRST by symtable_lookup), so re-registering "the same"
             * global (e.g. a plain "extern VideoBootStrap PRIVATE_bootstrap;"
             * forward declaration appearing, in some OTHER combined file,
             * after the real "VideoBootStrap PRIVATE_bootstrap = {...};"
             * definition already ran) would silently create a SECOND,
             * disconnected Symbol with its own separately-computed wdata
             * slot — any code that reads the global AFTER that point
             * resolves to the wrong (uninitialized/differently-allocated)
             * slot instead of the one Pass 0.5 actually initialized. Same
             * class of bug as the analogous extern-function-shadowing fix
             * above ParseFunction; this is the global-variable counterpart. */
            {
                Symbol *existing_global = symtable_lookup(p->sym, name);
                if (!existing_global || existing_global->kind != SYM_GLOBAL) {
                    symtable_define_global(p->sym, name, ret, arr);
                }
            }
            if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
            decls[count++]=ast_var_decl(storage[0]?storage:NULL, ret, name, init, arr, line);

            while (chk(p,TOK_COMMA)) {
                adv(p); /* consume ',' */
                TypeInfo *t2 = (TypeInfo*)calloc(1, sizeof(TypeInfo));
                *t2 = *ret;
                if (ret->base) t2->base = my_strdup(ret->base);
                t2->pointer_depth = base_ptr_depth; /* reset to base, not previous declarator's depth */
                while (chk(p,TOK_STAR)||chk(p,TOK_CONST)||chk(p,TOK_VOLATILE)) { if (chk(p,TOK_STAR)) t2->pointer_depth++; adv(p); }
                if (!chk(p,TOK_IDENT)) { parse_error(p,"expected declarator name"); break; }
                char n2[256]; strncpy(n2,tok_ident(&cur(p)),sizeof n2-1); n2[sizeof n2-1]='\0'; adv(p);
                int arr2=-1;
                if (chk(p,TOK_LBRACKET)) {
                    adv(p);
                    if (chk(p,TOK_NUMBER)||chk(p,TOK_LPAREN)||chk(p,TOK_MINUS)||chk(p,TOK_PLUS)||chk(p,TOK_TILDE)||chk(p,TOK_BANG)||chk(p,TOK_SIZEOF)||chk(p,TOK_IDENT)) { arr2=parse_const_expr(p); } else arr2=0;
                    eat(p,TOK_RBRACKET);
                }
                ASTNode *init2=NULL;
                if (chk(p,TOK_ASSIGN)) {
                    adv(p);
                    if (chk(p,TOK_LBRACE)) {
                        adv(p);
                        ASTNode *elems2[1024]; int ne2=0;
                        while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)&&ne2<1023) {
                            if (chk(p,TOK_LBRACE)) {
                                adv(p);
                                while (!chk(p,TOK_RBRACE)&&!chk(p,TOK_EOF)&&ne2<1023) {
                                    elems2[ne2++]=ParseAssignment(p);
                                    if (!chk(p,TOK_COMMA)) break; adv(p);
                                }
                                eat(p,TOK_RBRACE);
                            } else {
                                elems2[ne2++]=ParseAssignment(p);
                            }
                            if (!chk(p,TOK_COMMA)) break; adv(p);
                        }
                        eat(p,TOK_RBRACE);
                        init2 = ast_block(elems2, ne2, line);
                        if (arr2 == 0) arr2 = ne2;
                    } else {
                        init2=ParseAssignment(p);
                    }
                }
                /* Always reset from arr2 (this declarator's own brackets, -1/0
                 * if none), not just when arr2>0 — see the sibling fix above
                 * (local multi-declarator path) for why: t2 was shallow-copied
                 * from *ret, which the first declarator in this comma-list may
                 * have already mutated in place (line ~1780). */
                t2->array_size = arr2;
                symtable_define_global(p->sym, n2, t2, arr2);
                if (count >= decls_cap) { decls_cap *= 2; decls = (ASTNode**)realloc(decls, decls_cap * sizeof(ASTNode*)); }
                decls[count++]=ast_var_decl(storage[0]?storage:NULL, t2, n2, init2, arr2, line);
            }
            eat(p,TOK_SEMICOLON);
        }
    }
    { ASTNode *_prog = ast_program(decls, count, 1); /* ast_program copies decls, doesn't own it */
      free(decls);
      return _prog; }
}
