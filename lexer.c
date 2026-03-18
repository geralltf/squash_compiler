#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
#define strdup _strdup
#endif

#ifdef _WIN32
#define strtok_r strtok_s
#endif

/* =========================================================================
 * Error reporting helper
 * ========================================================================= */
static void lex_error(const Lexer *l, const char *msg) {
    fprintf(stderr, "%s:%d:%d: error: %s\n",
            l->filename ? l->filename : "<input>", l->line, l->col, msg);
    exit(1);
}

/* =========================================================================
 * Keyword table
 * ========================================================================= */
typedef struct { const char *w; TokenKind k; } KW;
static const KW KEYWORDS[] = {
    {"auto",     TOK_AUTO},   {"break",    TOK_BREAK},
    {"case",     TOK_CASE},   {"char",     TOK_CHAR},
    {"const",    TOK_CONST},  {"continue", TOK_CONTINUE},
    {"default",  TOK_DEFAULT},{"do",       TOK_DO},
    {"double",   TOK_DOUBLE}, {"else",     TOK_ELSE},
    {"enum",     TOK_ENUM},   {"extern",   TOK_EXTERN},
    {"float",    TOK_FLOAT_KW},{"for",     TOK_FOR},
    {"goto",     TOK_GOTO},   {"if",       TOK_IF},
    {"int",      TOK_INT},    {"long",     TOK_LONG},
    {"register", TOK_REGISTER},{"return",  TOK_RETURN},
    {"short",    TOK_SHORT},  {"signed",   TOK_SIGNED},
    {"sizeof",   TOK_SIZEOF}, {"static",   TOK_STATIC},
    {"struct",   TOK_STRUCT}, {"switch",   TOK_SWITCH},
    {"typedef",  TOK_TYPEDEF},{"union",    TOK_UNION},
    {"unsigned", TOK_UNSIGNED},{"void",    TOK_VOID},
    {"volatile", TOK_VOLATILE},{"while",   TOK_WHILE},
    {NULL, TOK_EOF}
};

static TokenKind kw_lookup(const char *s, int len) {
    for (int i = 0; KEYWORDS[i].w; i++)
        if ((int)strlen(KEYWORDS[i].w)==len && strncmp(KEYWORDS[i].w,s,len)==0)
            return KEYWORDS[i].k;
    return TOK_IDENT;
}

int tok_is_type(TokenKind k) {
    switch(k) {
    case TOK_INT: case TOK_CHAR: case TOK_VOID: case TOK_DOUBLE:
    case TOK_FLOAT_KW: case TOK_LONG: case TOK_SHORT: case TOK_SIGNED:
    case TOK_UNSIGNED: case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
    case TOK_CONST: case TOK_VOLATILE: case TOK_STATIC: case TOK_EXTERN:
    case TOK_AUTO: case TOK_REGISTER: case TOK_TYPEDEF:
        return 1;
    default: return 0;
    }
}

/* =========================================================================
 * Character helpers
 * ========================================================================= */
static char pc(const Lexer *l)        { return l->src[l->pos]; }
static char pc1(const Lexer *l)       { return l->src[l->pos] ? l->src[l->pos+1] : 0; }
static char pc2(const Lexer *l)       { return (l->src[l->pos]&&l->src[l->pos+1]) ? l->src[l->pos+2] : 0; }

static char adv(Lexer *l) {
    char c = l->src[l->pos++];
    if (c=='\n') { l->line++; l->col=1; } else l->col++;
    return c;
}

static void skip_ws_comments(Lexer *l) {
    for (;;) {
        while (l->src[l->pos] && isspace((unsigned char)l->src[l->pos])) adv(l);
        if (l->src[l->pos]=='/' && l->src[l->pos+1]=='/') {
            while (l->src[l->pos] && l->src[l->pos]!='\n') adv(l);
            continue;
        }
        if (l->src[l->pos]=='/' && l->src[l->pos+1]=='*') {
            adv(l); adv(l);
            while (l->src[l->pos]) {
                if (l->src[l->pos]=='*' && l->src[l->pos+1]=='/') { adv(l); adv(l); break; }
                adv(l);
            }
            continue;
        }
        break;
    }
}

/* =========================================================================
 * Escape sequence processor (shared for strings and char literals)
 * ========================================================================= */
static char process_escape(Lexer *l) {
    char e = adv(l);
    switch (e) {
        case 'n': return '\n'; case 'r': return '\r'; case 't': return '\t';
        case 'b': return '\b'; case 'f': return '\f'; case 'v': return '\v';
        case 'a': return '\a'; case '0': return '\0'; case '\\': return '\\';
        case '\'': return '\''; case '"': return '"';  case '?': return '?';
        case 'x': {
            int v = 0;
            while (isxdigit((unsigned char)l->src[l->pos])) {
                char c2 = adv(l);
                v = v*16 + (isdigit((unsigned char)c2) ? c2-'0' :
                            tolower((unsigned char)c2)-'a'+10);
            }
            return (char)v;
        }
        default:
            if (isdigit((unsigned char)e)) {
                int v = e-'0';
                while (l->src[l->pos]>='0' && l->src[l->pos]<='7')
                    v = v*8 + (adv(l)-'0');
                return (char)v;
            }
            return e;
    }
}

/* =========================================================================
 * ParseNumber — lex integer or float literal
 * ========================================================================= */
static Token ParseNumber(Lexer *l) {
    Token t; memset(&t,0,sizeof t);
    t.start=l->src+l->pos; t.line=l->line; t.col=l->col;

    int is_float = 0;
    /* hex */
    if (l->src[l->pos]=='0' && (l->src[l->pos+1]=='x'||l->src[l->pos+1]=='X')) {
        adv(l); adv(l);
        unsigned long long v=0;
        while (isxdigit((unsigned char)l->src[l->pos])) {
            char c=adv(l);
            v=v*16+(isdigit((unsigned char)c)?c-'0':tolower((unsigned char)c)-'a'+10);
        }
        t.ival=(long long)v;
    } else {
        long long v=0;
        while (isdigit((unsigned char)l->src[l->pos])) v=v*10+(adv(l)-'0');
        if (l->src[l->pos]=='.' || l->src[l->pos]=='e' || l->src[l->pos]=='E') {
            is_float=1;
            /* reparse as double */
            char buf[64]; int bi=0;
            const char *s=t.start;
            while (*s && (isdigit((unsigned char)*s)||*s=='.'||*s=='e'||*s=='E'||
                          *s=='+'||*s=='-') && bi<62) buf[bi++]=*s++;
            while (l->src[l->pos] && l->src[l->pos]!=buf[0]) {} /* consumed above */
            /* restart: back up and re-lex float */
            l->pos = (int)(t.start - l->src);
            l->col = t.col;
            char fbuf[64]; int fi=0;
            while (isdigit((unsigned char)l->src[l->pos])||l->src[l->pos]=='.') fbuf[fi++]=adv(l);
            if (l->src[l->pos]=='e'||l->src[l->pos]=='E') {
                fbuf[fi++]=adv(l);
                if (l->src[l->pos]=='+'||l->src[l->pos]=='-') fbuf[fi++]=adv(l);
                while (isdigit((unsigned char)l->src[l->pos])) fbuf[fi++]=adv(l);
            }
            fbuf[fi]=0;
            t.fval=atof(fbuf); t.ival=(long long)t.fval;
        } else {
            t.ival=v;
        }
        /* suffixes: u, l, ll, ul, etc. */
        while (l->src[l->pos]=='u'||l->src[l->pos]=='U'||
               l->src[l->pos]=='l'||l->src[l->pos]=='L'||
               l->src[l->pos]=='f'||l->src[l->pos]=='F') adv(l);
    }
    t.kind  = is_float ? TOK_FLOAT : TOK_NUMBER;
    t.len   = (int)(l->src+l->pos-t.start);
    return t;
}

/* =========================================================================
 * ParseIdentifier — lex identifier or keyword
 * ========================================================================= */
static Token ParseIdentifier(Lexer *l) {
    Token t; memset(&t,0,sizeof t);
    t.start=l->src+l->pos; t.line=l->line; t.col=l->col;
    while (isalnum((unsigned char)l->src[l->pos])||l->src[l->pos]=='_') adv(l);
    t.len  = (int)(l->src+l->pos-t.start);
    t.kind = kw_lookup(t.start, t.len);
    return t;
}

/* =========================================================================
 * Lex string literal
 * ========================================================================= */
static Token lex_string(Lexer *l) {
    Token t; memset(&t,0,sizeof t);
    t.kind=TOK_STRING; t.start=l->src+l->pos; t.line=l->line; t.col=l->col;
    adv(l); /* skip " */
    char buf[4096]; int bi=0;
    while (l->src[l->pos] && l->src[l->pos]!='"') {
        char c = (l->src[l->pos]=='\\') ? (adv(l), process_escape(l)) : adv(l);
        if (bi<(int)sizeof(buf)-1) buf[bi++]=c;
    }
    if (l->src[l->pos]=='"') adv(l);
    buf[bi]='\0';
    t.len  = (int)(l->src+l->pos-t.start);
    t.sval = strdup(buf);
    return t;
}

/* =========================================================================
 * Lex char literal
 * ========================================================================= */
static Token lex_char(Lexer *l) {
    Token t; memset(&t,0,sizeof t);
    t.kind=TOK_CHAR_LIT; t.start=l->src+l->pos; t.line=l->line; t.col=l->col;
    adv(l); /* skip ' */
    char c;
    if (l->src[l->pos]=='\\') { adv(l); c=process_escape(l); }
    else c=adv(l);
    if (l->src[l->pos]!='\'') lex_error(l,"unterminated character literal");
    adv(l);
    t.ival = (unsigned char)c;
    t.len  = (int)(l->src+l->pos-t.start);
    return t;
}

/* =========================================================================
 * lexer_init
 * ========================================================================= */
void lexer_init(Lexer *l, const char *src, const char *filename) {
    l->src=src; l->filename=filename; l->pos=0; l->line=1; l->col=1;
    memset(&l->cur,0,sizeof l->cur); l->cur.kind=TOK_EOF;
    l->cur=lexer_next(l);
}

/* =========================================================================
 * lexer_next
 * ========================================================================= */
Token lexer_next(Lexer *l) {
    skip_ws_comments(l);
    Token t; memset(&t,0,sizeof t);
    t.start=l->src+l->pos; t.line=l->line; t.col=l->col;

    if (!l->src[l->pos]) { t.kind=TOK_EOF; l->cur=t; return t; }

    char c=l->src[l->pos], c2=pc1(l), c3=pc2(l);

    if (isdigit((unsigned char)c)) { l->cur=ParseNumber(l); return l->cur; }
    if (c=='.') {
        if (isdigit((unsigned char)c2)) { l->cur=ParseNumber(l); return l->cur; }
        if (c2=='.'&&c3=='.') { adv(l);adv(l);adv(l); t.kind=TOK_ELLIPSIS; t.len=3; l->cur=t; return t; }
        adv(l); t.kind=TOK_DOT; t.len=1; l->cur=t; return t;
    }
    if (isalpha((unsigned char)c)||c=='_') { l->cur=ParseIdentifier(l); return l->cur; }
    if (c=='"') { l->cur=lex_string(l); return l->cur; }
    if (c=='\'') { l->cur=lex_char(l); return l->cur; }

    adv(l); t.len=1;

    /* Three-char */
    if (c=='<'&&c2=='<'&&c3=='=') { adv(l);adv(l); t.kind=TOK_LSHIFT_EQ; t.len=3; goto done; }
    if (c=='>'&&c2=='>'&&c3=='=') { adv(l);adv(l); t.kind=TOK_RSHIFT_EQ; t.len=3; goto done; }

    /* Two-char */
#define TW(a,b,k) if(c==(a)&&c2==(b)){adv(l);t.kind=(k);t.len=2;goto done;}
    TW('<','<',TOK_LSHIFT)  TW('>','>',TOK_RSHIFT)
    TW('&','&',TOK_AND)     TW('|','|',TOK_OR)
    TW('=','=',TOK_EQ)      TW('!','=',TOK_NEQ)
    TW('<','=',TOK_LE)      TW('>','=',TOK_GE)
    TW('+','+',TOK_INC)     TW('-','-',TOK_DEC)
    TW('+','=',TOK_PLUS_EQ) TW('-','=',TOK_MINUS_EQ)
    TW('*','=',TOK_STAR_EQ) TW('/','=',TOK_SLASH_EQ)
    TW('%','=',TOK_PERCENT_EQ) TW('&','=',TOK_AMP_EQ)
    TW('|','=',TOK_PIPE_EQ) TW('^','=',TOK_CARET_EQ)
    TW('-','>',TOK_ARROW)
#undef TW

    /* One-char */
    switch(c) {
        case '(':t.kind=TOK_LPAREN;break;   case ')':t.kind=TOK_RPAREN;break;
        case '{':t.kind=TOK_LBRACE;break;   case '}':t.kind=TOK_RBRACE;break;
        case '[':t.kind=TOK_LBRACKET;break; case ']':t.kind=TOK_RBRACKET;break;
        case ';':t.kind=TOK_SEMICOLON;break;case ':':t.kind=TOK_COLON;break;
        case ',':t.kind=TOK_COMMA;break;    case '?':t.kind=TOK_QUESTION;break;
        case '~':t.kind=TOK_TILDE;break;    case '#':t.kind=TOK_HASH;break;
        case '+':t.kind=TOK_PLUS;break;     case '-':t.kind=TOK_MINUS;break;
        case '*':t.kind=TOK_STAR;break;     case '/':t.kind=TOK_SLASH;break;
        case '%':t.kind=TOK_PERCENT;break;  case '&':t.kind=TOK_AMP;break;
        case '|':t.kind=TOK_PIPE;break;     case '^':t.kind=TOK_CARET;break;
        case '!':t.kind=TOK_BANG;break;     case '<':t.kind=TOK_LT;break;
        case '>':t.kind=TOK_GT;break;       case '=':t.kind=TOK_ASSIGN;break;
        default:
            fprintf(stderr,"%s:%d:%d: error: unknown character '%c' (0x%02X)\n",
                    l->filename?l->filename:"?", t.line, t.col, c, (unsigned char)c);
            t.kind=TOK_ERROR; break;
    }
done:
    l->cur=t; return t;
}

Token lexer_peek (Lexer *l) { return l->cur; }
int   lexer_check(Lexer *l, TokenKind k) { return l->cur.kind==k; }

Token lexer_expect(Lexer *l, TokenKind k) {
    Token t=l->cur;
    if (t.kind!=k) {
        fprintf(stderr,"%s:%d:%d: error: expected '%s' but got '%s'\n",
                l->filename?l->filename:"?", t.line, t.col,
                token_kind_name(k), token_kind_name(t.kind));
        exit(1);
    }
    lexer_next(l);
    return t;
}

/* =========================================================================
 * token_kind_name
 * ========================================================================= */
const char *token_kind_name(TokenKind k) {
    switch(k) {
    case TOK_NUMBER:  return "NUMBER";   case TOK_FLOAT:    return "FLOAT";
    case TOK_STRING:  return "STRING";   case TOK_CHAR_LIT: return "CHAR";
    case TOK_IDENT:   return "IDENT";
    case TOK_AUTO:    return "auto";     case TOK_BREAK:    return "break";
    case TOK_CASE:    return "case";     case TOK_CHAR:     return "char";
    case TOK_CONST:   return "const";    case TOK_CONTINUE: return "continue";
    case TOK_DEFAULT: return "default";  case TOK_DO:       return "do";
    case TOK_DOUBLE:  return "double";   case TOK_ELSE:     return "else";
    case TOK_ENUM:    return "enum";     case TOK_EXTERN:   return "extern";
    case TOK_FLOAT_KW:return "float";    case TOK_FOR:      return "for";
    case TOK_GOTO:    return "goto";     case TOK_IF:       return "if";
    case TOK_INT:     return "int";      case TOK_LONG:     return "long";
    case TOK_REGISTER:return "register"; case TOK_RETURN:   return "return";
    case TOK_SHORT:   return "short";    case TOK_SIGNED:   return "signed";
    case TOK_SIZEOF:  return "sizeof";   case TOK_STATIC:   return "static";
    case TOK_STRUCT:  return "struct";   case TOK_SWITCH:   return "switch";
    case TOK_TYPEDEF: return "typedef";  case TOK_UNION:    return "union";
    case TOK_UNSIGNED:return "unsigned"; case TOK_VOID:     return "void";
    case TOK_VOLATILE:return "volatile"; case TOK_WHILE:    return "while";
    case TOK_LPAREN:  return "(";   case TOK_RPAREN:   return ")";
    case TOK_LBRACE:  return "{";   case TOK_RBRACE:   return "}";
    case TOK_LBRACKET:return "[";   case TOK_RBRACKET: return "]";
    case TOK_SEMICOLON:return ";";  case TOK_COLON:    return ":";
    case TOK_COMMA:   return ",";   case TOK_DOT:      return ".";
    case TOK_ARROW:   return "->";  case TOK_ELLIPSIS: return "...";
    case TOK_HASH:    return "#";   case TOK_QUESTION: return "?";
    case TOK_PLUS:    return "+";   case TOK_MINUS:    return "-";
    case TOK_STAR:    return "*";   case TOK_SLASH:    return "/";
    case TOK_PERCENT: return "%";   case TOK_AMP:      return "&";
    case TOK_PIPE:    return "|";   case TOK_CARET:    return "^";
    case TOK_TILDE:   return "~";   case TOK_LSHIFT:   return "<<";
    case TOK_RSHIFT:  return ">>";  case TOK_INC:      return "++";
    case TOK_DEC:     return "--";  case TOK_BANG:     return "!";
    case TOK_AND:     return "&&";  case TOK_OR:       return "||";
    case TOK_EQ:      return "==";  case TOK_NEQ:      return "!=";
    case TOK_LT:      return "<";   case TOK_GT:       return ">";
    case TOK_LE:      return "<=";  case TOK_GE:       return ">=";
    case TOK_ASSIGN:  return "=";
    case TOK_PLUS_EQ: return "+=";  case TOK_MINUS_EQ: return "-=";
    case TOK_STAR_EQ: return "*=";  case TOK_SLASH_EQ: return "/=";
    case TOK_PERCENT_EQ:return "%=";case TOK_AMP_EQ:   return "&=";
    case TOK_PIPE_EQ: return "|=";  case TOK_CARET_EQ: return "^=";
    case TOK_LSHIFT_EQ:return "<<=";case TOK_RSHIFT_EQ:return ">>=";
    case TOK_EOF:     return "EOF"; default: return "?";
    }
}

void token_print(const Token *t) {
    printf("[%s", token_kind_name(t->kind));
    if (t->kind==TOK_NUMBER) printf(" %lld", t->ival);
    else if (t->kind==TOK_IDENT||t->kind==TOK_STRING)
        printf(" '%.*s'", t->len, t->start);
    printf(" L%d:C%d]", t->line, t->col);
}

/* =========================================================================
 * Preprocessor — handles #define and #include
 * ========================================================================= */

/* =========================================================================
 * Preprocessor — full C preprocessor with:
 *   #define / #undef          object-like macros
 *   #include "file"           user headers (with header-guard support)
 *   #include <file>           system headers (skipped)
 *   #ifdef / #ifndef / #if    conditional compilation
 *   #else / #elif             alternative branches
 *   #endif                    close conditional
 *   #pragma                   ignored
 *
 * Macros, include-once tracking, and if-stack are all kept in a single
 * PPState that is shared across recursive #include calls.
 * ========================================================================= */

#define PP_MAX_MACROS   1024
#define PP_MAX_DEPTH    64      /* max nested #if depth            */
#define PP_MAX_INCLUDED 256     /* max distinct files included     */

typedef struct { char *name; char *value; } PPMacro;

typedef struct {
    PPMacro macros[PP_MAX_MACROS];
    int     nmc;

    /* included-file deduplication (header guards via __FILE_ONCE__) */
    char   *included[PP_MAX_INCLUDED];
    int     n_included;

    /* include search paths */
    const char **inc_dirs;
    int          n_dirs;
} PPState;

/* ── helpers ─────────────────────────────────────────────────────────── */

size_t my_strnlen(const char* src, size_t n) {
    size_t len = 0;
    while (len < n && src[len])
        len++;
    return len;
}

char* my_strndup(const char* s, size_t n) {
    size_t len = my_strnlen(s, n);
    char* p = malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}


static int pp_macro_find(PPState *st, const char *name, int len) {
    for (int i = 0; i < st->nmc; i++)
        if ((int)strlen(st->macros[i].name)==len &&
            strncmp(st->macros[i].name, name, len)==0)
            return i;
    return -1;
}

static void pp_macro_define(PPState *st, const char *name, int nlen,
                             const char *value) {
    int idx = pp_macro_find(st, name, nlen);
    if (idx >= 0) {
        free(st->macros[idx].value);
        st->macros[idx].value = strdup(value);
        return;
    }
    if (st->nmc >= PP_MAX_MACROS) return;
    st->macros[st->nmc].name  = my_strndup(name, nlen);
    st->macros[st->nmc].value = strdup(value);
    st->nmc++;
}

static void pp_macro_undef(PPState *st, const char *name) {
    for (int i = 0; i < st->nmc; i++) {
        if (strcmp(st->macros[i].name, name)==0) {
            free(st->macros[i].name);
            free(st->macros[i].value);
            st->macros[i] = st->macros[--st->nmc];
            return;
        }
    }
}

static int pp_macro_defined(PPState *st, const char *name) {
    int len = (int)strlen(name);
    return pp_macro_find(st, name, len) >= 0;
}

/* Expand macros in a single text token (word-boundary check). */
static char *pp_expand(PPState *st, const char *src) {
    char *out = strdup(src);
    /* iterate: keep expanding until stable (handles chained macros) */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int m = 0; m < st->nmc; m++) {
            const char *mname = st->macros[m].name;
            const char *mval  = st->macros[m].value;
            int   mlen = (int)strlen(mname);
            char *p    = out;
            while ((p = strstr(p, mname)) != NULL) {
                int pre  = (p==out || !(isalnum((unsigned char)p[-1])||p[-1]=='_'));
                int post = !(isalnum((unsigned char)p[mlen])||p[mlen]=='_');
                if (!pre || !post) { p++; continue; }
                int ol   = (int)strlen(out);
                int vl   = (int)strlen(mval);
                char *tmp = malloc(ol - mlen + vl + 1);
                memcpy(tmp, out, p - out);
                memcpy(tmp + (p-out), mval, vl);
                strcpy(tmp + (p-out) + vl, p + mlen);
                free(out); out = tmp;
                p = out + (p-out) + vl;
                changed = 1;
            }
        }
    }
    return out;
}

/* Evaluate a simple #if / #elif expression:
 * Supports: defined(X), !defined(X), integers, ==, !=, &&, || */
static int pp_eval_expr(PPState *st, const char *expr) {
    const char *p = expr;
    while (*p==' '||*p=='\t') p++;

    /* defined(NAME) */
    if (strncmp(p,"defined",7)==0 && (p[7]=='('||p[7]==' '||p[7]=='\t')) {
        p += 7;
        while (*p==' '||*p=='\t'||*p=='(') p++;
        const char *ns = p;
        while (isalnum((unsigned char)*p)||*p=='_') p++;
        char name[256]; int nlen=(int)(p-ns);
        if (nlen<=0) return 0;
        snprintf(name,sizeof name,"%.*s",nlen,ns);
        return pp_macro_defined(st, name);
    }
    /* !defined(NAME) */
    if (*p=='!' && strncmp(p+1,"defined",7)==0) {
        p++;
        while (*p==' '||*p=='\t') p++;
        return !pp_eval_expr(st, p);
    }
    /* bare integer */
    if (isdigit((unsigned char)*p)) {
        return atoi(p) != 0;
    }
    /* bare identifier (1 if defined, value if numeric define) */
    if (isalpha((unsigned char)*p)||*p=='_') {
        const char *ns=p;
        while (isalnum((unsigned char)*p)||*p=='_') p++;
        int nlen=(int)(p-ns);
        char name[256]; snprintf(name,sizeof name,"%.*s",nlen,ns);
        int idx=pp_macro_find(st,name,nlen);
        if (idx<0) return 0;
        const char *val=st->macros[idx].value;
        if (!val||!val[0]) return 1;
        if (isdigit((unsigned char)val[0])||val[0]=='-') return atoi(val)!=0;
        return 1;
    }
    return 0;
}

/* Read a file from the include search path; returns heap string or NULL. */
static char *pp_read_file(PPState *st, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        char path[1024];
        for (int i=0; i<st->n_dirs && !fp; i++) {
            snprintf(path,sizeof path,"%s/%s",st->inc_dirs[i],filename);
            fp = fopen(path,"r");
        }
    }
    if (!fp) return NULL;
    fseek(fp,0,SEEK_END); long sz=ftell(fp); rewind(fp);
    char *buf = malloc(sz+2); fread(buf,1,sz,fp); buf[sz]='\0';
    fclose(fp);
    return buf;
}

/* Forward declaration: process_file() is called recursively for #include. */
static char *process_file(PPState *st, const char *src, const char *filename);

/* ── main preprocessor ───────────────────────────────────────────────── */
static char *process_file(PPState *st, const char *src, const char *filename) {
    /* Output buffer */
    size_t cap=65536, len=0;
    char *out = malloc(cap);
    out[0] = '\0';

    /* Conditional stack:
     *   each entry is: active(1/0), seen_true(1/0), seen_else(1/0) */
    int cond_active[PP_MAX_DEPTH];      /* 1 = currently outputting */
    int cond_seen_true[PP_MAX_DEPTH];   /* 1 = already had a true branch */
    int cond_seen_else[PP_MAX_DEPTH];   /* 1 = already had #else       */
    int cond_depth = 0;
    cond_active[0]=1; cond_seen_true[0]=1; cond_seen_else[0]=0;

    /* Helper: are we currently in an active (outputting) branch? */
#define PP_ACTIVE() (cond_active[cond_depth])

    /* Helper: append text to output buffer */
#define PP_EMIT(s,n) do {                          \
    size_t _n=(n);                                  \
    while(len+_n+2>cap){cap*=2;out=realloc(out,cap);}  \
    memcpy(out+len,(s),_n); len+=_n;               \
} while(0)

    /* Split source into lines, process each */
    char *copy = strdup(src);
    char *line;
    char *rest = copy;
    while ((line = strtok_r(rest, "\n", &rest)) != NULL) {
        const char *p = line;
        while (*p==' '||*p=='\t') p++;

        if (*p != '#') {
            /* Regular source line: macro-expand and emit if active */
            if (PP_ACTIVE()) {
                char *expanded = pp_expand(st, line);
                PP_EMIT(expanded, strlen(expanded));
                PP_EMIT("\n", 1);
                free(expanded);
            }
            continue;
        }

        /* Directive line */
        p++;
        while (*p==' '||*p=='\t') p++;

        /* ── #if ─────────────────────────────── */
        if (strncmp(p,"if",2)==0 && !isalnum((unsigned char)p[2]) && p[2]!='_') {
            const char *expr = p+2;
            while (*expr==' '||*expr=='\t') expr++;
            int parent_active = cond_active[cond_depth]; /* save BEFORE increment */
            int val = parent_active ? pp_eval_expr(st, expr) : 0;
            if (++cond_depth >= PP_MAX_DEPTH) cond_depth=PP_MAX_DEPTH-1;
            cond_active[cond_depth]    = val;
            cond_seen_true[cond_depth] = val;
            cond_seen_else[cond_depth] = 0;
            continue;
        }

        /* ── #ifdef ───────────────────────────── */
        if (strncmp(p,"ifdef",5)==0 && !isalnum((unsigned char)p[5]) && p[5]!='_') {
            const char *name = p+5;
            while (*name==' '||*name=='\t') name++;
            char nbuf[256]; int ni=0;
            while (name[ni]&&!isspace((unsigned char)name[ni])&&ni<255) ni++;
            snprintf(nbuf,sizeof nbuf,"%.*s",ni,name);
            int parent_active = cond_active[cond_depth]; /* save BEFORE increment */
            int val = parent_active ? pp_macro_defined(st, nbuf) : 0;
            if (++cond_depth >= PP_MAX_DEPTH) cond_depth=PP_MAX_DEPTH-1;
            cond_active[cond_depth]    = val;
            cond_seen_true[cond_depth] = val;
            cond_seen_else[cond_depth] = 0;
            continue;
        }

        /* ── #ifndef ──────────────────────────── */
        if (strncmp(p,"ifndef",6)==0 && !isalnum((unsigned char)p[6]) && p[6]!='_') {
            const char *name = p+6;
            while (*name==' '||*name=='\t') name++;
            char nbuf[256]; int ni=0;
            while (name[ni]&&!isspace((unsigned char)name[ni])&&ni<255) ni++;
            snprintf(nbuf,sizeof nbuf,"%.*s",ni,name);
            int parent_active = cond_active[cond_depth]; /* save BEFORE increment */
            int val = parent_active ? !pp_macro_defined(st, nbuf) : 0;
            if (++cond_depth >= PP_MAX_DEPTH) cond_depth=PP_MAX_DEPTH-1;
            cond_active[cond_depth]    = val;
            cond_seen_true[cond_depth] = val;
            cond_seen_else[cond_depth] = 0;
            continue;
        }

        /* ── #elif ────────────────────────────── */
        if (strncmp(p,"elif",4)==0 && !isalnum((unsigned char)p[4]) && p[4]!='_') {
            if (cond_depth > 0 && !cond_seen_else[cond_depth]) {
                const char *expr = p+4;
                while (*expr==' '||*expr=='\t') expr++;
                int parent_active = cond_depth>0 ? cond_active[cond_depth-1] : 1;
                if (!cond_seen_true[cond_depth] && parent_active) {
                    int val = pp_eval_expr(st, expr);
                    cond_active[cond_depth]    = val;
                    cond_seen_true[cond_depth] = val;
                } else {
                    cond_active[cond_depth] = 0;
                }
            }
            continue;
        }

        /* ── #else ────────────────────────────── */
        if (strncmp(p,"else",4)==0 && (!p[4]||isspace((unsigned char)p[4]))) {
            if (cond_depth > 0 && !cond_seen_else[cond_depth]) {
                int parent_active = cond_depth>0 ? cond_active[cond_depth-1] : 1;
                cond_active[cond_depth] =
                    (!cond_seen_true[cond_depth]) && parent_active;
                cond_seen_else[cond_depth] = 1;
            }
            continue;
        }

        /* ── #endif ───────────────────────────── */
        if (strncmp(p,"endif",5)==0 && (!p[5]||isspace((unsigned char)p[5]))) {
            if (cond_depth > 0) cond_depth--;
            continue;
        }

        /* Skip directives in inactive branches */
        if (!PP_ACTIVE()) continue;

        /* ── #define ──────────────────────────── */
        if (strncmp(p,"define",6)==0 && !isalnum((unsigned char)p[6]) && p[6]!='_') {
            p += 6;
            while (*p==' '||*p=='\t') p++;
            const char *nm = p;
            while (isalnum((unsigned char)*p)||*p=='_') p++;
            int nlen = (int)(p-nm);
            if (nlen==0) continue;
            /* skip function-like macros */
            if (*p=='(') continue;
            while (*p==' '||*p=='\t') p++;
            /* trim trailing CR/whitespace */
            char val[4096]; strncpy(val,p,sizeof val-1); val[sizeof val-1]='\0';
            int vl=(int)strlen(val);
            while (vl>0&&(val[vl-1]=='\r'||val[vl-1]==' '||val[vl-1]=='\t')) vl--;
            val[vl]='\0';
            pp_macro_define(st, nm, nlen, val);
            continue;
        }

        /* ── #undef ───────────────────────────── */
        if (strncmp(p,"undef",5)==0 && !isalnum((unsigned char)p[5]) && p[5]!='_') {
            p += 5;
            while (*p==' '||*p=='\t') p++;
            char nbuf[256]; int ni=0;
            while (p[ni]&&!isspace((unsigned char)p[ni])&&ni<255) ni++;
            snprintf(nbuf,sizeof nbuf,"%.*s",ni,p);
            pp_macro_undef(st, nbuf);
            continue;
        }

        /* ── #include ─────────────────────────── */
        if (strncmp(p,"include",7)==0 && isspace((unsigned char)p[7])) {
            p += 7;
            while (*p==' '||*p=='\t') p++;
            int system_inc = (*p=='<');
            p++;                       /* skip < or " */
            const char *ns = p;
            while (*p && *p!='>' && *p!='"') p++;
            int nlen2=(int)(p-ns);
            char incname[512]; snprintf(incname,sizeof incname,"%.*s",nlen2,ns);

            if (system_inc) continue;  /* skip <stdio.h> etc. */

            char *inc_src = pp_read_file(st, incname);
            if (!inc_src) {
                fprintf(stderr,"preprocessor: cannot find '%s'\n", incname);
                continue;
            }
            char *inc_out = process_file(st, inc_src, incname);
            PP_EMIT(inc_out, strlen(inc_out));
            free(inc_src); free(inc_out);
            continue;
        }

        /* ── #pragma / unknown ────────────────── */
        /* silently skip */
    }

    free(copy);
    out[len] = '\0';

#undef PP_ACTIVE
#undef PP_EMIT
    return out;
}

/* Public API — initialises state with built-in macros, then processes. */
char *preprocess(const char *src, const char *filename,
                 const char **inc_dirs, int n_dirs) {
    PPState *st = calloc(1, sizeof(PPState));
    st->inc_dirs = inc_dirs;
    st->n_dirs   = n_dirs;

    /* Built-in macros */
    pp_macro_define(st,"NULL",4,"0");
    pp_macro_define(st,"TRUE",4,"1");
    pp_macro_define(st,"FALSE",5,"0");
    pp_macro_define(st,"EXIT_SUCCESS",12,"0");
    pp_macro_define(st,"EXIT_FAILURE",12,"1");

    char *result = process_file(st, src, filename);

    /* free macros */
    for (int i=0;i<st->nmc;i++) { free(st->macros[i].name); free(st->macros[i].value); }
    for (int i=0;i<st->n_included;i++) free(st->included[i]);
    free(st);
    return result;
}

