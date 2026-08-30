#include "cs_lexer.h"
#include "cs_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* squash's own include/stdlib.h shim declares strtol() but not
 * strtoll() -- hand-declared here rather than widening the shim itself
 * (out of scope for this frontend), matching this codebase's own
 * established pattern (e.g. setjmp/longjmp in CSR/csharp_rt.h) of
 * declaring a real libc function by hand when the project's header shims
 * don't cover it yet. */
extern long long strtoll(const char *s, char **endptr, int base);

static const char *KEYWORDS[] = {
    "using", "namespace", "class", "struct", "interface", "enum",
    "public", "private", "protected", "internal", "static", "readonly",
    "const", "virtual", "override", "abstract", "sealed", "partial",
    "void", "var", "new", "return", "if", "else", "for", "foreach", "in",
    "while", "do", "break", "continue", "switch", "case", "default",
    "try", "catch", "finally", "throw", "this", "base", "null", "true",
    "false", "get", "set", "out", "ref", "params", "where", "is", "as",
    "delegate", "extern", "explicit", "implicit", "operator",
    /* primitive-type keywords -- also real C# reserved words, not just
     * ordinary identifiers */
    "int", "long", "short", "byte", "sbyte", "uint", "ulong", "ushort",
    "string", "bool", "double", "float", "decimal", "char", "object",
    0
};

int cs_is_keyword(const char *s) {
    int i;
    for (i = 0; KEYWORDS[i]; i++) if (strcmp(KEYWORDS[i], s) == 0) return 1;
    return 0;
}

void cs_lexer_init(CsLexer *lx, const char *src) {
    lx->src = src;
    lx->pos = 0;
    lx->len = (int)strlen(src);
    lx->line = 1;
}

static int peekc(CsLexer *lx) { return lx->pos < lx->len ? (unsigned char)lx->src[lx->pos] : -1; }
static int peekc2(CsLexer *lx) { return lx->pos + 1 < lx->len ? (unsigned char)lx->src[lx->pos + 1] : -1; }
static int peekc3(CsLexer *lx) { return lx->pos + 2 < lx->len ? (unsigned char)lx->src[lx->pos + 2] : -1; }
static int advc(CsLexer *lx) {
    int c = peekc(lx);
    if (c < 0) return -1;
    lx->pos++;
    if (c == '\n') lx->line++;
    return c;
}

static void skip_ws_and_comments(CsLexer *lx) {
    for (;;) {
        int c = peekc(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advc(lx); continue; }
        if (c == '/' && peekc2(lx) == '/') {
            while (peekc(lx) >= 0 && peekc(lx) != '\n') advc(lx);
            continue;
        }
        if (c == '/' && peekc2(lx) == '*') {
            advc(lx); advc(lx);
            while (peekc(lx) >= 0 && !(peekc(lx) == '*' && peekc2(lx) == '/')) advc(lx);
            if (peekc(lx) >= 0) { advc(lx); advc(lx); }
            continue;
        }
        break;
    }
}

static CsTok *mktok(CsTokKind kind, int line) {
    CsTok *t = (CsTok *)malloc(sizeof(CsTok));
    memset(t, 0, sizeof(CsTok));
    t->kind = kind;
    t->line = line;
    return t;
}

/* Consumes one escaped character (after a backslash already consumed for
 * regular strings/chars) or a doubled-quote (for verbatim strings, handled
 * by the caller instead) into `out`, returning the number of bytes
 * appended. Supports the common set: \n \t \r \\ \" \' \0 \uXXXX
 * (encoded as raw UTF-8, matching the runtime's UTF-8 CsString scope). */
static int append_escape(CsLexer *lx, char *out) {
    int c = advc(lx);
    switch (c) {
    case 'n': out[0] = '\n'; return 1;
    case 't': out[0] = '\t'; return 1;
    case 'r': out[0] = '\r'; return 1;
    case '0': out[0] = '\0'; return 1;
    case '\\': out[0] = '\\'; return 1;
    case '"': out[0] = '"'; return 1;
    case '\'': out[0] = '\''; return 1;
    case 'u': {
        int i, cp = 0;
        for (i = 0; i < 4 && isxdigit(peekc(lx)); i++) {
            int d = advc(lx);
            cp = cp * 16 + (isdigit(d) ? d - '0' : (tolower(d) - 'a' + 10));
        }
        /* encode cp as UTF-8 (BMP range only, matches \uXXXX's own 4-hex-digit range) */
        if (cp < 0x80) { out[0] = (char)cp; return 1; }
        if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    default:
        if (c < 0) return 0;
        out[0] = (char)c;
        return 1;
    }
}

#define CS_STRBUF_MAX 65536

static char *lex_regular_string_body(CsLexer *lx) {
    static char buf[CS_STRBUF_MAX];
    int n = 0;
    while (peekc(lx) >= 0 && peekc(lx) != '"') {
        if (peekc(lx) == '\\') {
            advc(lx);
            if (n + 4 < CS_STRBUF_MAX) n += append_escape(lx, buf + n);
            else advc(lx);
        } else {
            if (n + 1 < CS_STRBUF_MAX) buf[n++] = (char)advc(lx); else advc(lx);
        }
    }
    if (peekc(lx) == '"') advc(lx);
    buf[n] = 0;
    { char *r = (char *)malloc((unsigned int)n + 1); memcpy(r, buf, (unsigned int)n + 1); return r; }
}

static char *lex_verbatim_string_body(CsLexer *lx) {
    static char buf[CS_STRBUF_MAX];
    int n = 0;
    for (;;) {
        if (peekc(lx) < 0) break;
        if (peekc(lx) == '"') {
            if (peekc2(lx) == '"') { advc(lx); advc(lx); if (n + 1 < CS_STRBUF_MAX) buf[n++] = '"'; continue; }
            advc(lx);
            break;
        }
        if (n + 1 < CS_STRBUF_MAX) buf[n++] = (char)advc(lx); else advc(lx);
    }
    buf[n] = 0;
    { char *r = (char *)malloc((unsigned int)n + 1); memcpy(r, buf, (unsigned int)n + 1); return r; }
}

/* Interpolated string body: returns the RAW text between $" and the
 * closing " (inclusive of literal {{ }} escapes left as-is, `{expr}`
 * spans left as-is too) — cs_parser.c re-scans this raw text to split
 * literal segments from embedded expressions (see cs_parser.c's
 * parse_interp_string). Tracks brace/paren/bracket depth and skips over
 * nested "..."/'...' literals inside an interpolation slot so a quote or
 * brace INSIDE `{ Foo("a{b}") }` doesn't prematurely end the scan —
 * documented scope limit: a nested verbatim (@"...") or interpolated
 * ($"...") string INSIDE an interpolation slot is not specially handled
 * (rare in practice; real C# code overwhelmingly nests plain strings, if
 * anything, inside an interpolation). */
static char *lex_interp_string_body(CsLexer *lx) {
    static char buf[CS_STRBUF_MAX];
    int n = 0;
    int depth = 0; /* brace nesting inside a {expr} slot; 0 = in literal text */
    while (peekc(lx) >= 0) {
        int c = peekc(lx);
        if (depth == 0) {
            if (c == '"') { advc(lx); break; }
            if (c == '{' && peekc2(lx) == '{') { advc(lx); advc(lx); if (n + 1 < CS_STRBUF_MAX) buf[n++] = '{'; continue; }
            if (c == '}' && peekc2(lx) == '}') { advc(lx); advc(lx); if (n + 1 < CS_STRBUF_MAX) buf[n++] = '}'; continue; }
            if (c == '{') { depth = 1; if (n + 1 < CS_STRBUF_MAX) buf[n++] = (char)advc(lx); continue; }
            if (c == '\\') {
                advc(lx);
                if (n + 4 < CS_STRBUF_MAX) n += append_escape(lx, buf + n);
                else advc(lx);
                continue;
            }
            if (n + 1 < CS_STRBUF_MAX) buf[n++] = (char)advc(lx); else advc(lx);
        } else {
            if (c == '"') {
                /* nested plain string literal inside an interpolation slot */
                if (n + 1 < CS_STRBUF_MAX) buf[n++] = (char)advc(lx);
                while (peekc(lx) >= 0 && peekc(lx) != '"') {
                    if (peekc(lx) == '\\' && n + 2 < CS_STRBUF_MAX) { buf[n++] = (char)advc(lx); buf[n++] = (char)advc(lx); }
                    else if (n + 1 < CS_STRBUF_MAX) buf[n++] = (char)advc(lx);
                    else advc(lx);
                }
                if (peekc(lx) == '"' && n + 1 < CS_STRBUF_MAX) buf[n++] = (char)advc(lx);
                continue;
            }
            if (c == '{') depth++;
            if (c == '}') depth--;
            if (n + 1 < CS_STRBUF_MAX) buf[n++] = (char)advc(lx); else advc(lx);
        }
    }
    buf[n] = 0;
    { char *r = (char *)malloc((unsigned int)n + 1); memcpy(r, buf, (unsigned int)n + 1); return r; }
}

CsTok *cs_lexer_next(CsLexer *lx) {
    int c, line;
    skip_ws_and_comments(lx);
    line = lx->line;
    c = peekc(lx);

    if (c < 0) return mktok(CS_TOK_EOF, line);

    /* verbatim / interpolated string prefixes */
    if (c == '@' && peekc2(lx) == '"') {
        advc(lx); advc(lx);
        { CsTok *t = mktok(CS_TOK_STRING_LIT, line); t->text = lex_verbatim_string_body(lx); return t; }
    }
    if (c == '$' && peekc2(lx) == '"') {
        advc(lx); advc(lx);
        { CsTok *t = mktok(CS_TOK_INTERP_STRING_LIT, line); t->text = lex_interp_string_body(lx); return t; }
    }
    if (c == '$' && peekc2(lx) == '@' && peekc3(lx) == '"') {
        advc(lx); advc(lx); advc(lx);
        { CsTok *t = mktok(CS_TOK_INTERP_STRING_LIT, line); t->text = lex_verbatim_string_body(lx); return t; }
    }

    if (c == '"') {
        advc(lx);
        { CsTok *t = mktok(CS_TOK_STRING_LIT, line); t->text = lex_regular_string_body(lx); return t; }
    }

    if (c == '\'') {
        long long val;
        advc(lx);
        if (peekc(lx) == '\\') {
            char tmp[4]; advc(lx); append_escape(lx, tmp); val = (unsigned char)tmp[0];
        } else {
            val = advc(lx);
        }
        if (peekc(lx) == '\'') advc(lx);
        { CsTok *t = mktok(CS_TOK_CHAR_LIT, line); t->int_value = val; return t; }
    }

    if (isdigit(c)) {
        char buf[128]; int n = 0;
        int is_double = 0;
        if (c == '0' && (peekc2(lx) == 'x' || peekc2(lx) == 'X')) {
            buf[n++] = (char)advc(lx); buf[n++] = (char)advc(lx);
            while (isxdigit(peekc(lx)) && n < 120) buf[n++] = (char)advc(lx);
            buf[n] = 0;
            { CsTok *t = mktok(CS_TOK_INT_LIT, line); t->int_value = strtoll(buf, 0, 16); return t; }
        }
        while (isdigit(peekc(lx)) && n < 120) buf[n++] = (char)advc(lx);
        if (peekc(lx) == '.' && isdigit(peekc2(lx))) {
            is_double = 1;
            buf[n++] = (char)advc(lx);
            while (isdigit(peekc(lx)) && n < 120) buf[n++] = (char)advc(lx);
        }
        if (peekc(lx) == 'e' || peekc(lx) == 'E') {
            is_double = 1;
            buf[n++] = (char)advc(lx);
            if (peekc(lx) == '+' || peekc(lx) == '-') buf[n++] = (char)advc(lx);
            while (isdigit(peekc(lx)) && n < 120) buf[n++] = (char)advc(lx);
        }
        buf[n] = 0;
        /* numeric suffix (f/F/d/D/m/M/L/l/u/U), possibly combined (UL) -- consumed and, beyond
         * marking float-vs-double-ness, otherwise ignored (this phase doesn't distinguish
         * float from double at the type level, matching the runtime's own single `double`
         * BCL numeric scope noted in csharp_rt.h). */
        while (peekc(lx) == 'f' || peekc(lx) == 'F' || peekc(lx) == 'd' || peekc(lx) == 'D' ||
               peekc(lx) == 'm' || peekc(lx) == 'M' || peekc(lx) == 'L' || peekc(lx) == 'l' ||
               peekc(lx) == 'u' || peekc(lx) == 'U') {
            int sc = advc(lx);
            if (sc == 'f' || sc == 'F' || sc == 'd' || sc == 'D' || sc == 'm' || sc == 'M') is_double = 1;
        }
        if (is_double) { CsTok *t = mktok(CS_TOK_DOUBLE_LIT, line); t->double_value = atof(buf); return t; }
        { CsTok *t = mktok(CS_TOK_INT_LIT, line); t->int_value = strtoll(buf, 0, 10); return t; }
    }

    if (isalpha(c) || c == '_') {
        char buf[256]; int n = 0;
        while ((isalnum(peekc(lx)) || peekc(lx) == '_') && n < 250) buf[n++] = (char)advc(lx);
        buf[n] = 0;
        if (cs_is_keyword(buf)) { CsTok *t = mktok(CS_TOK_KEYWORD, line); t->text = cs_strdup(buf); return t; }
        { CsTok *t = mktok(CS_TOK_IDENT, line); t->text = cs_strdup(buf); return t; }
    }

    /* punctuation / operators, longest-match first */
    {
        static const char *ops3[] = { "?\?=", "<<=", ">>=", 0 };
        static const char *ops2[] = { "=>", "??", "?.", "==", "!=", "<=", ">=", "&&", "||",
                                       "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                                       "<<", ">>", "::", 0 };
        int i;
        for (i = 0; ops3[i]; i++) {
            int l = (int)strlen(ops3[i]);
            if (lx->pos + l <= lx->len && strncmp(lx->src + lx->pos, ops3[i], (unsigned int)l) == 0) {
                CsTok *t = mktok(CS_TOK_PUNCT, line); t->text = cs_strdup(ops3[i]);
                lx->pos += l; return t;
            }
        }
        for (i = 0; ops2[i]; i++) {
            int l = (int)strlen(ops2[i]);
            if (lx->pos + l <= lx->len && strncmp(lx->src + lx->pos, ops2[i], (unsigned int)l) == 0) {
                CsTok *t = mktok(CS_TOK_PUNCT, line); t->text = cs_strdup(ops2[i]);
                lx->pos += l; return t;
            }
        }
    }
    {
        char single[2]; single[0] = (char)advc(lx); single[1] = 0;
        CsTok *t = mktok(CS_TOK_PUNCT, line); t->text = cs_strdup(single);
        return t;
    }
}

void cs_tok_free(CsTok *t) {
    if (!t) return;
    free(t->text);
    free(t);
}
