#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cpp_lexer.h"

static const char *KEYWORDS[] = {
    "class", "struct", "public", "private", "protected", "virtual", "override",
    "namespace", "using", "template", "typename", "new", "delete", "this",
    "if", "else", "for", "while", "do", "break", "continue", "return",
    "switch", "case", "default", "true", "false", "nullptr", "NULL",
    "void", "bool", "char", "int", "short", "long", "float", "double",
    "unsigned", "signed", "const", "static", "sizeof", "operator",
    "explicit", "friend", "inline", "auto", "typedef", "enum", "union",
    NULL
};

int cpp_is_keyword(const char *s) {
    for (int i = 0; KEYWORDS[i]; i++) if (strcmp(KEYWORDS[i], s) == 0) return 1;
    return 0;
}

void cpp_lexer_init(CppLexer *lx, const char *src) {
    lx->src = src; lx->pos = 0; lx->len = (int)strlen(src); lx->line = 1;
}

static int peekc(CppLexer *lx, int off) {
    int p = lx->pos + off;
    return p < lx->len ? (unsigned char)lx->src[p] : 0;
}
static int curc(CppLexer *lx) { return peekc(lx, 0); }
static void adv(CppLexer *lx) { if (lx->pos < lx->len) { if (lx->src[lx->pos] == '\n') lx->line++; lx->pos++; } }

static void skip_ws_and_comments(CppLexer *lx) {
    for (;;) {
        int c = curc(lx);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { adv(lx); continue; }
        if (c == '/' && peekc(lx, 1) == '/') { while (curc(lx) && curc(lx) != '\n') adv(lx); continue; }
        if (c == '/' && peekc(lx, 1) == '*') {
            adv(lx); adv(lx);
            while (curc(lx) && !(curc(lx) == '*' && peekc(lx, 1) == '/')) adv(lx);
            if (curc(lx)) { adv(lx); adv(lx); }
            continue;
        }
        break;
    }
}

static CppTok *mktok(CppTokKind kind, int line) {
    CppTok *t = calloc(1, sizeof(CppTok));
    t->kind = kind; t->line = line;
    return t;
}

static char *dupn(const char *s, int n) {
    char *r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static char *cpp_strdup_local(const char *s) {
    return dupn(s, (int)strlen(s));
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int lex_escape(CppLexer *lx) {
    int c = curc(lx); adv(lx);
    switch (c) {
        case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
        case '0': return '\0'; case '\\': return '\\'; case '\'': return '\'';
        case '"': return '"'; case 'a': return '\a'; case 'b': return '\b';
        case 'f': return '\f'; case 'v': return '\v';
        case 'x': {
            int v = 0, n = 0;
            while (n < 2 && hexval(curc(lx)) >= 0) { v = v * 16 + hexval(curc(lx)); adv(lx); n++; }
            return v;
        }
        default: return c;
    }
}

CppTok *cpp_lexer_next(CppLexer *lx) {
    skip_ws_and_comments(lx);
    int line = lx->line;
    if (lx->pos >= lx->len) return mktok(CPP_TOK_EOF, line);

    int c = curc(lx);

    /* Preprocessor line — only meaningful at (effectively) line start;
     * since we always call this right after skipping whitespace/comments,
     * any '#' here begins a directive line. Captured verbatim so
     * cpp_lower.c can decide what to do with #include (re-emit for
     * system/native headers; special-case the handful of "logical"
     * standard headers like <iostream>/<string>/<vector> it actually
     * understands) — see cpp_lower.h's own header comment. */
    if (c == '#') {
        adv(lx);
        int start = lx->pos;
        while (curc(lx) && curc(lx) != '\n') {
            if (curc(lx) == '\\' && peekc(lx, 1) == '\n') { adv(lx); adv(lx); continue; }
            adv(lx);
        }
        char *text = dupn(lx->src + start, lx->pos - start);
        CppTok *t = mktok(CPP_TOK_PREPROC_LINE, line);
        t->text = text;
        return t;
    }

    if (isalpha(c) || c == '_') {
        int start = lx->pos;
        while (isalnum(curc(lx)) || curc(lx) == '_') adv(lx);
        char *text = dupn(lx->src + start, lx->pos - start);
        CppTok *t = mktok(cpp_is_keyword(text) ? CPP_TOK_KEYWORD : CPP_TOK_IDENT, line);
        t->text = text;
        return t;
    }

    if (isdigit(c) || (c == '.' && isdigit(peekc(lx, 1)))) {
        int start = lx->pos;
        int is_double = 0;
        if (c == '0' && (peekc(lx, 1) == 'x' || peekc(lx, 1) == 'X')) {
            adv(lx); adv(lx);
            while (isxdigit(curc(lx))) adv(lx);
            char *text = dupn(lx->src + start, lx->pos - start);
            CppTok *t = mktok(CPP_TOK_INT_LIT, line);
            t->int_value = strtoll(text, NULL, 16);
            t->text = text;
            return t;
        }
        while (isdigit(curc(lx))) adv(lx);
        if (curc(lx) == '.' && isdigit(peekc(lx, 1))) { is_double = 1; adv(lx); while (isdigit(curc(lx))) adv(lx); }
        if (curc(lx) == 'e' || curc(lx) == 'E') {
            is_double = 1; adv(lx);
            if (curc(lx) == '+' || curc(lx) == '-') adv(lx);
            while (isdigit(curc(lx))) adv(lx);
        }
        if (curc(lx) == 'f' || curc(lx) == 'F') { is_double = 1; adv(lx); }
        while (curc(lx) == 'u' || curc(lx) == 'U' || curc(lx) == 'l' || curc(lx) == 'L') adv(lx);
        char *text = dupn(lx->src + start, lx->pos - start);
        CppTok *t = mktok(is_double ? CPP_TOK_DOUBLE_LIT : CPP_TOK_INT_LIT, line);
        if (is_double) t->double_value = strtod(text, NULL);
        else t->int_value = strtoll(text, NULL, 10);
        t->text = text;
        return t;
    }

    if (c == '"') {
        adv(lx);
        char buf[4096]; int n = 0;
        while (curc(lx) && curc(lx) != '"' && n < (int)sizeof(buf) - 1) {
            if (curc(lx) == '\\') { adv(lx); buf[n++] = (char)lex_escape(lx); }
            else { buf[n++] = (char)curc(lx); adv(lx); }
        }
        if (curc(lx) == '"') adv(lx);
        buf[n] = '\0';
        CppTok *t = mktok(CPP_TOK_STRING_LIT, line);
        t->text = dupn(buf, n);
        return t;
    }

    if (c == '\'') {
        adv(lx);
        long long v = 0;
        if (curc(lx) == '\\') { adv(lx); v = lex_escape(lx); }
        else { v = curc(lx); adv(lx); }
        if (curc(lx) == '\'') adv(lx);
        CppTok *t = mktok(CPP_TOK_CHAR_LIT, line);
        t->int_value = v;
        return t;
    }

    /* Punctuation, longest match first. */
    static const char *ops3[] = { "<<=", ">>=", "...", NULL };
    static const char *ops2[] = { "::", "->", "<<", ">>", "<=", ">=", "==", "!=",
                                    "&&", "||", "++", "--", "+=", "-=", "*=", "/=",
                                    "%=", "&=", "|=", "^=", NULL };
    for (int i = 0; ops3[i]; i++) {
        int l = 3;
        if (peekc(lx, 0) == ops3[i][0] && peekc(lx, 1) == ops3[i][1] && peekc(lx, 2) == ops3[i][2]) {
            for (int k = 0; k < l; k++) adv(lx);
            CppTok *t = mktok(CPP_TOK_PUNCT, line); t->text = cpp_strdup_local(ops3[i]); return t;
        }
    }
    for (int i = 0; ops2[i]; i++) {
        if (peekc(lx, 0) == ops2[i][0] && peekc(lx, 1) == ops2[i][1]) {
            adv(lx); adv(lx);
            CppTok *t = mktok(CPP_TOK_PUNCT, line); t->text = cpp_strdup_local(ops2[i]); return t;
        }
    }
    {
        char one[2] = { (char)c, '\0' };
        adv(lx);
        CppTok *t = mktok(CPP_TOK_PUNCT, line);
        t->text = cpp_strdup_local(one);
        return t;
    }
}

void cpp_tok_free(CppTok *t) {
    if (!t) return;
    free(t->text);
    free(t);
}
