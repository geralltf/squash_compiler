#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * Keyword table
 * ========================================================================= */
typedef struct { const char *word; TokenKind kind; } Keyword;
static const Keyword KEYWORDS[] = {
    {"if",     TOK_IF},
    {"else",   TOK_ELSE},
    {"while",  TOK_WHILE},
    {"return", TOK_RETURN},
    {"int",    TOK_INT},
    {"void",   TOK_VOID},
    {"char",   TOK_CHAR},
    {NULL,     TOK_EOF}
};

static TokenKind keyword_lookup(const char *s, int len) {
    for (int i = 0; KEYWORDS[i].word; i++) {
        if ((int)strlen(KEYWORDS[i].word) == len &&
            strncmp(KEYWORDS[i].word, s, len) == 0)
            return KEYWORDS[i].kind;
    }
    return TOK_IDENT;
}

/* =========================================================================
 * Character helpers
 * ========================================================================= */
static char peek_char(Lexer *l) {
    return l->src[l->pos];
}
static char peek_char2(Lexer *l) {
    return l->src[l->pos] ? l->src[l->pos+1] : 0;
}
static char advance_char(Lexer *l) {
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else l->col++;
    return c;
}
static void skip_whitespace_comments(Lexer *l) {
    for (;;) {
        /* whitespace */
        while (l->src[l->pos] && isspace((unsigned char)l->src[l->pos]))
            advance_char(l);
        /* single-line comment */
        if (l->src[l->pos]=='/' && l->src[l->pos+1]=='/') {
            while (l->src[l->pos] && l->src[l->pos]!='\n')
                advance_char(l);
            continue;
        }
        /* multi-line comment */
        if (l->src[l->pos]=='/' && l->src[l->pos+1]=='*') {
            advance_char(l); advance_char(l);
            while (l->src[l->pos]) {
                if (l->src[l->pos]=='*' && l->src[l->pos+1]=='/') {
                    advance_char(l); advance_char(l); break;
                }
                advance_char(l);
            }
            continue;
        }
        break;
    }
}

/* =========================================================================
 * ParseNumber() – lex a numeric literal
 * Supports decimal integers and hex (0x...).
 * ========================================================================= */
static Token ParseNumber(Lexer *l) {
    Token t;
    memset(&t, 0, sizeof t);
    t.kind  = TOK_NUMBER;
    t.start = l->src + l->pos;
    t.line  = l->line;
    t.col   = l->col;

    if (l->src[l->pos]=='0' &&
        (l->src[l->pos+1]=='x' || l->src[l->pos+1]=='X')) {
        advance_char(l); advance_char(l); /* skip 0x */
        long long v = 0;
        while (isxdigit((unsigned char)l->src[l->pos])) {
            char c = advance_char(l);
            int d = (c>='0'&&c<='9') ? c-'0' :
                    (c>='a'&&c<='f') ? c-'a'+10 : c-'A'+10;
            v = v*16 + d;
        }
        t.ival = v;
    } else {
        long long v = 0;
        while (isdigit((unsigned char)l->src[l->pos])) {
            v = v*10 + (advance_char(l)-'0');
        }
        t.ival = v;
    }
    t.len = (int)(l->src + l->pos - t.start);
    return t;
}

/* =========================================================================
 * ParseIdentifier() – lex an identifier or keyword
 * ========================================================================= */
static Token ParseIdentifier(Lexer *l) {
    Token t;
    memset(&t, 0, sizeof t);
    t.start = l->src + l->pos;
    t.line  = l->line;
    t.col   = l->col;

    while (isalnum((unsigned char)l->src[l->pos]) || l->src[l->pos]=='_')
        advance_char(l);

    t.len  = (int)(l->src + l->pos - t.start);
    t.kind = keyword_lookup(t.start, t.len);
    return t;
}

/* =========================================================================
 * Lex a string literal "..."
 * ========================================================================= */
static Token lex_string(Lexer *l) {
    Token t;
    memset(&t, 0, sizeof t);
    t.kind  = TOK_STRING;
    t.start = l->src + l->pos;
    t.line  = l->line;
    t.col   = l->col;
    advance_char(l); /* skip opening quote */

    /* build unescaped string value */
    char buf[4096]; int bi = 0;
    while (l->src[l->pos] && l->src[l->pos]!='"') {
        char c = advance_char(l);
        if (c=='\\') {
            char e = advance_char(l);
            switch (e) {
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case '0':  c = '\0'; break;
                case '\\': c = '\\'; break;
                case '"':  c = '"';  break;
                default:   c = e;    break;
            }
        }
        if (bi < (int)sizeof(buf)-1) buf[bi++] = c;
    }
    if (l->src[l->pos]=='"') advance_char(l);
    buf[bi] = '\0';
    t.len  = (int)(l->src + l->pos - t.start);
    t.sval = strdup(buf);
    return t;
}

/* =========================================================================
 * lexer_init
 * ========================================================================= */
void lexer_init(Lexer *l, const char *src) {
    l->src  = src;
    l->pos  = 0;
    l->line = 1;
    l->col  = 1;
    memset(&l->cur, 0, sizeof l->cur);
    l->cur.kind = TOK_EOF;
    /* Prime the first token */
    l->cur = lexer_next(l);
}

/* =========================================================================
 * lexer_next – advance and return the next token
 * ========================================================================= */
Token lexer_next(Lexer *l) {
    skip_whitespace_comments(l);

    Token t;
    memset(&t, 0, sizeof t);
    t.start = l->src + l->pos;
    t.line  = l->line;
    t.col   = l->col;

    if (!l->src[l->pos]) {
        t.kind = TOK_EOF; t.len = 0;
        l->cur = t; return t;
    }

    char c = l->src[l->pos];
    char c2 = peek_char2(l);

    /* Numbers */
    if (isdigit((unsigned char)c)) {
        l->cur = ParseNumber(l);
        return l->cur;
    }
    /* Identifiers / keywords */
    if (isalpha((unsigned char)c) || c=='_') {
        l->cur = ParseIdentifier(l);
        return l->cur;
    }
    /* String literals */
    if (c=='"') {
        l->cur = lex_string(l);
        return l->cur;
    }

    /* Two-character operators */
    advance_char(l);
    t.len = 1;

    switch (c) {
        case '(': t.kind = TOK_LPAREN;    break;
        case ')': t.kind = TOK_RPAREN;    break;
        case '{': t.kind = TOK_LBRACE;    break;
        case '}': t.kind = TOK_RBRACE;    break;
        case ';': t.kind = TOK_SEMICOLON; break;
        case ',': t.kind = TOK_COMMA;     break;
        case '~': t.kind = TOK_TILDE;     break;
        case '%': t.kind = TOK_PERCENT;   break;
        case '^': t.kind = TOK_CARET;     break;
        case '+': t.kind = TOK_PLUS;      break;
        case '*': t.kind = TOK_STAR;      break;
        case '/': t.kind = TOK_SLASH;     break;
        case '-': t.kind = TOK_MINUS;     break;
        case '&':
            if (c2=='&') { advance_char(l); t.kind=TOK_AND; t.len=2; }
            else t.kind=TOK_AMP; break;
        case '|':
            if (c2=='|') { advance_char(l); t.kind=TOK_OR;  t.len=2; }
            else t.kind=TOK_PIPE; break;
        case '!':
            if (c2=='=') { advance_char(l); t.kind=TOK_NEQ; t.len=2; }
            else t.kind=TOK_BANG; break;
        case '=':
            if (c2=='=') { advance_char(l); t.kind=TOK_EQ;  t.len=2; }
            else t.kind=TOK_ASSIGN; break;
        case '<':
            if (c2=='=') { advance_char(l); t.kind=TOK_LE;  t.len=2; }
            else if (c2=='<') { advance_char(l); t.kind=TOK_LSHIFT; t.len=2; }
            else t.kind=TOK_LT; break;
        case '>':
            if (c2=='=') { advance_char(l); t.kind=TOK_GE;  t.len=2; }
            else if (c2=='>') { advance_char(l); t.kind=TOK_RSHIFT; t.len=2; }
            else t.kind=TOK_GT; break;
        default:
            fprintf(stderr, "Lexer error: unknown char '%c' at line %d col %d\n",
                    c, t.line, t.col);
            t.kind = TOK_ERROR; break;
    }
    l->cur = t;
    return t;
}

/* =========================================================================
 * lexer_peek – return current token without advancing
 * ========================================================================= */
Token lexer_peek(Lexer *l) { return l->cur; }

/* =========================================================================
 * lexer_check – return 1 if current token matches kind
 * ========================================================================= */
int lexer_check(Lexer *l, TokenKind k) { return l->cur.kind == k; }

/* =========================================================================
 * lexer_expect – consume current token if it matches, else error
 * ========================================================================= */
Token lexer_expect(Lexer *l, TokenKind k) {
    Token t = l->cur;
    if (t.kind != k) {
        fprintf(stderr, "Parse error: expected %s but got %s at line %d col %d\n",
                token_kind_name(k), token_kind_name(t.kind), t.line, t.col);
        exit(1);
    }
    lexer_next(l);
    return t;
}

/* =========================================================================
 * Helpers
 * ========================================================================= */
const char *token_kind_name(TokenKind k) {
    switch (k) {
        case TOK_NUMBER:    return "NUMBER";
        case TOK_STRING:    return "STRING";
        case TOK_IDENT:     return "IDENT";
        case TOK_IF:        return "if";
        case TOK_ELSE:      return "else";
        case TOK_WHILE:     return "while";
        case TOK_RETURN:    return "return";
        case TOK_INT:       return "int";
        case TOK_VOID:      return "void";
        case TOK_CHAR:      return "char";
        case TOK_LPAREN:    return "(";
        case TOK_RPAREN:    return ")";
        case TOK_LBRACE:    return "{";
        case TOK_RBRACE:    return "}";
        case TOK_SEMICOLON: return ";";
        case TOK_COMMA:     return ",";
        case TOK_PLUS:      return "+";
        case TOK_MINUS:     return "-";
        case TOK_STAR:      return "*";
        case TOK_SLASH:     return "/";
        case TOK_PERCENT:   return "%";
        case TOK_AMP:       return "&";
        case TOK_PIPE:      return "|";
        case TOK_CARET:     return "^";
        case TOK_TILDE:     return "~";
        case TOK_LSHIFT:    return "<<";
        case TOK_RSHIFT:    return ">>";
        case TOK_BANG:      return "!";
        case TOK_AND:       return "&&";
        case TOK_OR:        return "||";
        case TOK_EQ:        return "==";
        case TOK_NEQ:       return "!=";
        case TOK_LT:        return "<";
        case TOK_GT:        return ">";
        case TOK_LE:        return "<=";
        case TOK_GE:        return ">=";
        case TOK_ASSIGN:    return "=";
        case TOK_EOF:       return "EOF";
        default:            return "?";
    }
}

void token_print(const Token *t) {
    printf("[%s", token_kind_name(t->kind));
    if (t->kind==TOK_NUMBER) printf(" %lld", t->ival);
    else if (t->kind==TOK_IDENT||t->kind==TOK_STRING)
        printf(" '%.*s'", t->len, t->start);
    printf(" L%d:C%d]", t->line, t->col);
}
