#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* =========================================================================
 * Error reporting helper
 * ========================================================================= */
static void lex_error(const Lexer *l, const char *msg) {
    printf("%s:%d:%d: error: %s\n",
            l->filename ? l->filename : "<input>", l->line, l->col, msg);
    exit(1);
}

/* =========================================================================
 * Keyword table
 * ========================================================================= */
/* Keyword match using strncmp directly — avoids global data tables */
#define KW_CMP(word, wlen, tok) if(len==wlen && strncmp(s,word,wlen)==0) return tok
static TokenKind kw_lookup(const char *s, int len) {
    switch (len) {
    case 2: KW_CMP("do",2,TOK_DO); KW_CMP("if",2,TOK_IF); break;
    case 3: KW_CMP("for",3,TOK_FOR); KW_CMP("int",3,TOK_INT); break;
    case 4: KW_CMP("auto",4,TOK_AUTO); KW_CMP("case",4,TOK_CASE); KW_CMP("char",4,TOK_CHAR);
            KW_CMP("else",4,TOK_ELSE); KW_CMP("enum",4,TOK_ENUM); KW_CMP("goto",4,TOK_GOTO);
            KW_CMP("long",4,TOK_LONG); KW_CMP("void",4,TOK_VOID); break;
    case 5: KW_CMP("break",5,TOK_BREAK); KW_CMP("const",5,TOK_CONST); KW_CMP("float",5,TOK_FLOAT_KW);
            KW_CMP("short",5,TOK_SHORT); KW_CMP("union",5,TOK_UNION); KW_CMP("while",5,TOK_WHILE); break;
    case 6: KW_CMP("double",6,TOK_DOUBLE); KW_CMP("extern",6,TOK_EXTERN); KW_CMP("inline",6,TOK_INLINE);
            KW_CMP("return",6,TOK_RETURN); KW_CMP("signed",6,TOK_SIGNED); KW_CMP("sizeof",6,TOK_SIZEOF);
            KW_CMP("static",6,TOK_STATIC); KW_CMP("struct",6,TOK_STRUCT); KW_CMP("switch",6,TOK_SWITCH); break;
    case 7: KW_CMP("default",7,TOK_DEFAULT); KW_CMP("typedef",7,TOK_TYPEDEF); break;
    case 8: KW_CMP("continue",8,TOK_CONTINUE); KW_CMP("register",8,TOK_REGISTER);
            KW_CMP("unsigned",8,TOK_UNSIGNED); KW_CMP("volatile",8,TOK_VOLATILE); break;
    }
    return TOK_IDENT;
}
#undef KW_CMP

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

static char lex_adv(Lexer *l) {
    char c = l->src[l->pos++];
    if (c=='\n') { l->line++; l->col=1; } else l->col++;
    return c;
}

static void skip_ws_comments(Lexer *l) {
    for (;;) {
        while (l->src[l->pos] && isspace((unsigned char)l->src[l->pos])) lex_adv(l);
        if (l->src[l->pos]=='/' && l->src[l->pos+1]=='/') {
            while (l->src[l->pos] && l->src[l->pos]!='\n') lex_adv(l);
            continue;
        }
        if (l->src[l->pos]=='/' && l->src[l->pos+1]=='*') {
            lex_adv(l); lex_adv(l);
            while (l->src[l->pos]) {
                if (l->src[l->pos]=='*' && l->src[l->pos+1]=='/') { lex_adv(l); lex_adv(l); break; }
                lex_adv(l);
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
    char e = lex_adv(l);
    switch (e) {
        case 'n': return '\n'; case 'r': return '\r'; case 't': return '\t';
        case 'b': return '\b'; case 'f': return '\f'; case 'v': return '\v';
        case 'a': return '\a'; case '0': return '\0'; case '\\': return '\\';
        case '\'': return '\''; case '"': return '"';  case '?': return '?';
        case 'x': {
            int v = 0;
            while (isxdigit((unsigned char)l->src[l->pos])) {
                char c2 = lex_adv(l);
                v = v*16 + (isdigit((unsigned char)c2) ? c2-'0' :
                            tolower((unsigned char)c2)-'a'+10);
            }
            return (char)v;
        }
        default:
            if (isdigit((unsigned char)e)) {
                int v = e-'0';
                while (l->src[l->pos]>='0' && l->src[l->pos]<='7')
                    v = v*8 + (lex_adv(l)-'0');
                return (char)v;
            }
            return e;
    }
}

/* =========================================================================
 * lex_parse_number — lex integer or float literal
 * ========================================================================= */
static void lex_parse_number(Lexer *l) {
    l->cur.start=l->src+l->pos; l->cur.line=l->line; l->cur.col=l->col;
    l->cur.ival=0; l->cur.sval=0; l->cur.len=0;

    int is_float = 0;
    /* hex */
    if (l->src[l->pos]=='0' && (l->src[l->pos+1]=='x'||l->src[l->pos+1]=='X')) {
        lex_adv(l); lex_adv(l);
        unsigned long long v=0;
        while (isxdigit((unsigned char)l->src[l->pos])) {
            char c=lex_adv(l);
            v=v*16+(isdigit((unsigned char)c)?c-'0':tolower((unsigned char)c)-'a'+10);
        }
        l->cur.ival=(long long)v;
    } else {
        long long v=0;
        while (isdigit((unsigned char)l->src[l->pos])) v=v*10+(lex_adv(l)-'0');
        if (l->src[l->pos]=='.' || l->src[l->pos]=='e' || l->src[l->pos]=='E') {
            is_float=1;
            /* restart: back up and re-lex float */
            l->pos = (int)(l->cur.start - l->src);
            l->col = l->cur.col;
            char fbuf[64]; int fi=0;
            while (isdigit((unsigned char)l->src[l->pos])||l->src[l->pos]=='.') fbuf[fi++]=lex_adv(l);
            if (l->src[l->pos]=='e'||l->src[l->pos]=='E') {
                fbuf[fi++]=lex_adv(l);
                if (l->src[l->pos]=='+'||l->src[l->pos]=='-') fbuf[fi++]=lex_adv(l);
                while (isdigit((unsigned char)l->src[l->pos])) fbuf[fi++]=lex_adv(l);
            }
            fbuf[fi]=0;
            l->cur.fval=atof(fbuf); l->cur.ival=(long long)l->cur.fval;
        } else {
            l->cur.ival=v;
        }
    }
    /* Consume integer/float suffixes: U, L, LL, UL, F, etc. */
    while (l->src[l->pos]=='u'||l->src[l->pos]=='U'||
           l->src[l->pos]=='l'||l->src[l->pos]=='L'||
           l->src[l->pos]=='f'||l->src[l->pos]=='F') lex_adv(l);
    l->cur.kind  = is_float ? TOK_FLOAT : TOK_NUMBER;
    l->cur.len   = (int)(l->src+l->pos-l->cur.start);
}

/* =========================================================================
 * lex_parse_identifier — lex identifier or keyword
 * ========================================================================= */
static void lex_parse_identifier(Lexer *l) {
    l->cur.start=l->src+l->pos; l->cur.line=l->line; l->cur.col=l->col;
    l->cur.ival=0; l->cur.sval=0;
    while (isalnum((unsigned char)l->src[l->pos])||l->src[l->pos]=='_') lex_adv(l);
    l->cur.len  = (int)(l->src+l->pos-l->cur.start);
    l->cur.kind = kw_lookup(l->cur.start, l->cur.len);
}

/* =========================================================================
 * Lex string literal
 * ========================================================================= */
static void lex_string(Lexer *l) {
    l->cur.kind=TOK_STRING; l->cur.start=l->src+l->pos; l->cur.line=l->line; l->cur.col=l->col;
    l->cur.ival=0;
    lex_adv(l); /* skip " */
    char buf[4096]; int bi=0;
    while (l->src[l->pos] && l->src[l->pos]!='"') {
        char c = (l->src[l->pos]=='\\') ? (lex_adv(l), process_escape(l)) : lex_adv(l);
        if (bi<(int)sizeof(buf)-1) buf[bi++]=c;
    }
    if (l->src[l->pos]=='"') lex_adv(l);
    buf[bi]='\0';
    l->cur.len  = (int)(l->src+l->pos-l->cur.start);
    l->cur.sval = my_strdup(buf);
}

/* =========================================================================
 * Lex char literal
 * ========================================================================= */
static void lex_char(Lexer *l) {
    l->cur.kind=TOK_CHAR_LIT; l->cur.start=l->src+l->pos; l->cur.line=l->line; l->cur.col=l->col;
    l->cur.sval=0;
    lex_adv(l); /* skip ' */
    char c;
    if (l->src[l->pos]=='\\') { lex_adv(l); c=process_escape(l); }
    else c=lex_adv(l);
    if (l->src[l->pos]!='\'') lex_error(l,"unterminated character literal");
    lex_adv(l);
    l->cur.ival = (unsigned char)c;
    l->cur.len  = (int)(l->src+l->pos-l->cur.start);
}

/* =========================================================================
 * lexer_init
 * ========================================================================= */
void lexer_init(Lexer *l, const char *src, const char *filename) {
    l->src=src;
    l->filename=filename;
    l->pos=0; l->line=1; l->col=1;
    memset(&l->cur,0,sizeof l->cur);
    l->cur.kind=TOK_EOF;
    lexer_next(l);
}

/* =========================================================================
 * lexer_next
 * ========================================================================= */
void lexer_next(Lexer *l) {
    skip_ws_comments(l);
    Token t;
    t.kind=0; t.start=0; t.len=0; t.ival=0; t.sval=0; t.line=0; t.col=0;
    t.start=l->src+l->pos;
    t.line=l->line;
    t.col=l->col;

    if (!l->src[l->pos]) {
        l->cur.kind=TOK_EOF; l->cur.start=t.start; l->cur.line=t.line; l->cur.col=t.col;
        l->cur.len=0; l->cur.ival=0; l->cur.sval=0; return;
    }

    char c=l->src[l->pos];
    char c2=pc1(l);
    char c3=pc2(l);

    if (isdigit((unsigned char)c)) { lex_parse_number(l); return; }
    if (c=='.') {
        if (isdigit((unsigned char)c2)) { lex_parse_number(l); return; }
        if (c2=='.'&&c3=='.') { lex_adv(l);lex_adv(l);lex_adv(l); t.kind=TOK_ELLIPSIS; t.len=3; goto done; }
        lex_adv(l); t.kind=TOK_DOT; t.len=1; goto done;
    }
    if (isalpha((unsigned char)c)||c=='_') { lex_parse_identifier(l); return; }
    if (c=='"') { lex_string(l); return; }
    if (c=='\'') { lex_char(l); return; }

    lex_adv(l); t.len=1;

    /* Three-char */
    if (c=='<'&&c2=='<'&&c3=='=') { lex_adv(l);lex_adv(l); t.kind=TOK_LSHIFT_EQ; t.len=3; goto done; }
    if (c=='>'&&c2=='>'&&c3=='=') { lex_adv(l);lex_adv(l); t.kind=TOK_RSHIFT_EQ; t.len=3; goto done; }

    /* Two-char */
    if(c=='<'&&c2=='<'){lex_adv(l);t.kind=TOK_LSHIFT;t.len=2;goto done;}  if(c=='>'&&c2=='>'){lex_adv(l);t.kind=TOK_RSHIFT;t.len=2;goto done;}
    if(c=='&'&&c2=='&'){lex_adv(l);t.kind=TOK_AND;t.len=2;goto done;}     if(c=='|'&&c2=='|'){lex_adv(l);t.kind=TOK_OR;t.len=2;goto done;}
    if(c=='='&&c2=='='){lex_adv(l);t.kind=TOK_EQ;t.len=2;goto done;}      if(c=='!'&&c2=='='){lex_adv(l);t.kind=TOK_NEQ;t.len=2;goto done;}
    if(c=='<'&&c2=='='){lex_adv(l);t.kind=TOK_LE;t.len=2;goto done;}      if(c=='>'&&c2=='='){lex_adv(l);t.kind=TOK_GE;t.len=2;goto done;}
    if(c=='+'&&c2=='+'){lex_adv(l);t.kind=TOK_INC;t.len=2;goto done;}     if(c=='-'&&c2=='-'){lex_adv(l);t.kind=TOK_DEC;t.len=2;goto done;}
    if(c=='+'&&c2=='='){lex_adv(l);t.kind=TOK_PLUS_EQ;t.len=2;goto done;} if(c=='-'&&c2=='='){lex_adv(l);t.kind=TOK_MINUS_EQ;t.len=2;goto done;}
    if(c=='*'&&c2=='='){lex_adv(l);t.kind=TOK_STAR_EQ;t.len=2;goto done;} if(c=='/'&&c2=='='){lex_adv(l);t.kind=TOK_SLASH_EQ;t.len=2;goto done;}
    if(c=='%'&&c2=='='){lex_adv(l);t.kind=TOK_PERCENT_EQ;t.len=2;goto done;} if(c=='&'&&c2=='='){lex_adv(l);t.kind=TOK_AMP_EQ;t.len=2;goto done;}
    if(c=='|'&&c2=='='){lex_adv(l);t.kind=TOK_PIPE_EQ;t.len=2;goto done;} if(c=='^'&&c2=='='){lex_adv(l);t.kind=TOK_CARET_EQ;t.len=2;goto done;}
    if(c=='-'&&c2=='>'){lex_adv(l);t.kind=TOK_ARROW;t.len=2;goto done;}
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
            printf("%s:%d:%d: error: unknown character '%c' (0x%02X)\n",
                    l->filename?l->filename:"?", t.line, t.col, c, (unsigned char)c);
            t.kind=TOK_ERROR; break;
    }
done:
    l->cur.kind  = t.kind;
    l->cur.start = t.start;
    l->cur.line  = t.line;
    l->cur.col   = t.col;
    l->cur.len   = t.len;
    l->cur.ival  = t.ival;
    l->cur.sval  = t.sval;
    return;
}
/* marker - should not be reached */

Token lexer_peek (Lexer *l) { return l->cur; }  /* GCC callers only */
int   lexer_check(Lexer *l, TokenKind k) { return l->cur.kind==k; }

/* lexer_expect_void: void version for squash-compiled callers that can't handle
 * the large-struct return ABI.  lexer_expect (Token-returning) is kept for
 * any remaining GCC-compiled callers. */
void lexer_expect_void(Lexer *l, TokenKind k) {
    if (l->cur.kind!=k) {
        printf("%s:%d:%d: error: expected '%s' but got '%s'\n",
                l->filename?l->filename:"?", l->cur.line, l->cur.col,
                token_kind_name(k), token_kind_name(l->cur.kind));
        exit(1);
    }
    lexer_next(l);
}

Token lexer_expect(Lexer *l, TokenKind k) {
    Token t=l->cur;
    if (t.kind!=k) {
        printf("%s:%d:%d: error: expected '%s' but got '%s'\n",
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

typedef struct {
    char *name;
    char *value;
    char *params[16]; /* NULL-terminated list of param names, NULL if object-like */
    int   nparams;    /* -1 = object-like, >=0 = function-like */
} PPMacro;

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

static int pp_macro_find(PPState *st, const char *name, int len) {
    for (int i = 0; i < st->nmc; i++) {
        if (!st->macros[i].name) continue;
        if ((int)strlen(st->macros[i].name)==len &&
            strncmp(st->macros[i].name, name, len)==0)
            return i;
    }
    return -1;
}

static void pp_macro_define(PPState *st, const char *name, int nlen,
                             const char *value) {
    int idx = pp_macro_find(st, name, nlen);
    if (idx >= 0) {
        free(st->macros[idx].value);
        st->macros[idx].value = my_strdup(value);
        st->macros[idx].nparams = -1;
        return;
    }
    if (st->nmc >= PP_MAX_MACROS) return;
    { char *_snd=malloc(nlen+1); strncpy(_snd,name,nlen); _snd[nlen]='\0'; st->macros[st->nmc].name=_snd; }
    st->macros[st->nmc].value = my_strdup(value);
    st->macros[st->nmc].nparams = -1;
    st->nmc++;
}

static void pp_macro_undef(PPState *st, const char *name) {
    for (int i = 0; i < st->nmc; i++) {
        if (strcmp(st->macros[i].name, name)==0) {
            free(st->macros[i].name);
            free(st->macros[i].value);
            { char **_pi = st->macros[i].params;
              for (int _j=0;_j<16;_j++) { free(_pi[_j]); _pi[_j]=NULL; } }
            /* Replace with last entry — use field-by-field copy to avoid struct assignment
             * (squash codegen does not handle struct-to-struct copies of large structs). */
            --st->nmc;
            if (i < st->nmc) {
                st->macros[i].name   = st->macros[st->nmc].name;
                st->macros[i].value  = st->macros[st->nmc].value;
                { char **_psrc = st->macros[st->nmc].params;
                  char **_pdst = st->macros[i].params;
                  int _j; for (_j=0;_j<16;_j++) _pdst[_j] = _psrc[_j]; }
                st->macros[i].nparams= st->macros[st->nmc].nparams;
                /* Zero vacated slot so its pointers aren't reused if the slot
                 * is later filled by a new object-like macro. */
                st->macros[st->nmc].name = NULL;
                st->macros[st->nmc].value = NULL;
                { char **_pz = st->macros[st->nmc].params;
                  int _k; for (_k=0;_k<16;_k++) _pz[_k] = NULL; }
                st->macros[st->nmc].nparams = -1;
            }
            return;
        }
    }
}

static int pp_macro_defined(PPState *st, const char *name) {
    int len = (int)strlen(name);
    return pp_macro_find(st, name, len) >= 0;
}

/* Expand macros in a single text token (word-boundary check). */
/* Check if position p in string out is inside a string literal or char literal.
 * Used to prevent macro expansion inside string/char literals. */
static int pp_in_string(const char *out, const char *p) {
    int in_str = 0;  /* 0=normal, 1=in double-quoted string, 2=in single-quoted char */
    const char *c = out;
    while (c < p) {
        if (!in_str) {
            if (*c == '"')  { in_str = 1; c++; continue; }
            if (*c == '\'') { in_str = 2; c++; continue; }
        } else if (in_str == 1) {
            if (*c == '\\') { c += 2; continue; }  /* skip escaped char */
            if (*c == '"')  { in_str = 0; c++; continue; }
        } else { /* in_str == 2 */
            if (*c == '\\') { c += 2; continue; }  /* skip escaped char */
            if (*c == '\'') { in_str = 0; c++; continue; }
        }
        c++;
    }
    return in_str != 0;
}

static int pp_expand_dbg = 0; /* set to 1 to enable tracing */
static char *pp_expand(PPState *st, const char *src) {
    char *out = my_strdup(src);
    int changed = 1;
    int max_iters = 100; /* prevent infinite expansion loops */
    if (pp_expand_dbg) { printf("[expand] src=%.80s\n",src); fflush(0); }
    while (changed && max_iters-- > 0) {
        changed = 0;
        for (int m = 0; m < st->nmc; m++) {
            const char *mname = st->macros[m].name;
            if (!mname) { printf("[pp] expand: NULL mname at m=%d nmc=%d\n",m,st->nmc); fflush(0); continue; }
            const char *mval  = st->macros[m].value;
            if (!mval)  { printf("[pp] expand: NULL mval  m=%d name=%s\n",m,mname); fflush(0); continue; }
            int   mlen = (int)strlen(mname);
            if (pp_expand_dbg) { printf("[expand] m=%d name=%s mlen=%d is_fn=%d\n",m,mname,mlen,(st->macros[m].nparams>=0)); fflush(0); }
            int   is_fn = (st->macros[m].nparams >= 0);
            char *p    = out;
            while ((p = strstr(p, mname)) != NULL) {
                int poff = (int)(p - out);
                /* Skip macro expansion inside string/char literals */
                if (pp_in_string(out, p)) { p++; continue; }
                int pre  = (p==out || !(isalnum((unsigned char)p[-1])||p[-1]=='_'));
                int post = is_fn ? (p[mlen]=='(') : !(isalnum((unsigned char)p[mlen])||p[mlen]=='_');
                if (!pre || !post) { p++; continue; }

                if (is_fn) {
                    /* Function-like: parse args from (a, b, c) */
                    char *ap = p + mlen + 1; /* skip '(' */
                    while (*ap==' '||*ap=='\t') ap++;
                    /* Use flat storage: args_buf[na*512 + char_offset] */
                    /* Heap alloc avoids squash codegen stack-frame bugs with large arrays */
                    char *args_buf = (char*)malloc(8192); int na=0;  /* 16 args * 512 bytes each */
                    int depth=1;
                    if (pp_expand_dbg) { printf("[fnarg] m=%d name=%s args_buf=%p ap=%.20s\n",m,mname,(void*)args_buf,ap); fflush(0); }
                    /* Parse comma-separated args, handling nested parens */
                    if (*ap != ')') {  /* non-empty arg list */
                        while (na < 16) {
                            int ai=0;
                            char *cur_arg = args_buf + na*512;
                            while (*ap) {
                                if (*ap=='(') { depth++; if(ai<511) cur_arg[ai++]=*ap++; }
                                else if (*ap==')') {
                                    if (depth==1) break;
                                    depth--;
                                    if(ai<511) cur_arg[ai++]=*ap++;
                                }
                                else if (*ap==',' && depth==1) break;
                                else { if(ai<511) cur_arg[ai++]=*ap++; }
                            }
                            cur_arg[ai]='\0';
                            while (ai>0 && (cur_arg[ai-1]==' '||cur_arg[ai-1]=='\t')) cur_arg[--ai]='\0';
                            char *ta=cur_arg; while(*ta==' '||*ta=='\t') ta++;
                            if(ta!=cur_arg) memmove(cur_arg,ta,strlen(ta)+1);
                            na++;
                            if (*ap==',') { ap++; }
                            else break;
                        }
                    }
                    if (*ap==')') ap++;
                    if (pp_expand_dbg) { printf("[fnarg] parsed na=%d arg0=%.30s\n",na,args_buf); fflush(0); }
                    /* Build expanded body by substituting params */
                    /* Use heap to avoid squash codegen issues with large stack arrays */
                    char *body = (char*)malloc(4096); body[0]='\0'; int bi=0;
                    if (pp_expand_dbg) { printf("[body] allocated body=%p mval=%.40s\n",(void*)body,mval); fflush(0); }
                    if (pp_expand_dbg) { printf("[body] m=%d macros[m].name=%p macros[m].nparams=%d params[0]=%p params[1]=%p\n",m,(void*)st->macros[m].name,st->macros[m].nparams,(void*)st->macros[m].params[0],(void*)st->macros[m].params[1]); fflush(0); }
                    if (pp_expand_dbg) { printf("[body] starting loop bp=%p\n",(void*)mval); fflush(0); }
                    const char *bp=mval;
                    while (*bp && bi<4095) {
                        /* Check if current position matches a param name */
                        int matched=0;
                        /* Use temp pointer to avoid squash nested-array-in-struct codegen bug */
                        char **_mparams = st->macros[m].params;
                        for (int pi=0; pi<st->macros[m].nparams && pi<na; pi++) {
                            const char *pn=_mparams[pi];
                            if (!pn) continue;
                            int pl=(int)strlen(pn);
                            if (strncmp(bp,pn,pl)==0 &&
                                !(isalnum((unsigned char)bp[pl])||bp[pl]=='_') &&
                                !(bp>mval && (isalnum((unsigned char)bp[-1])||bp[-1]=='_'))) {
                                /* substitute */
                                int al=(int)strlen(args_buf+pi*512);
                                if (bi+al<4095) { memcpy(body+bi,args_buf+pi*512,al); bi+=al; }
                                bp+=pl; matched=1; break;
                            }
                        }
                        if (!matched) body[bi++]=*bp++;
                    }
                    body[bi]='\0';
                    /* Replace MACRO(args) in out */
                    int macro_len=(int)(ap-p);
                    int ol=(int)strlen(out);
                    int vl=(int)strlen(body);
                    char *tmp=malloc(ol-macro_len+vl+1);
                    memcpy(tmp,out,poff);
                    memcpy(tmp+poff,body,vl);
                    strcpy(tmp+poff+vl,out+poff+macro_len);
                    free(args_buf);
                    free(body);
                    free(out); out=tmp;
                    p=out+poff+vl;
                    changed=1;
                } else {
                    /* Object-like macro */
                    int poff2=(int)(p-out);
                    int ol=(int)strlen(out);
                    int vl=(int)strlen(mval);
                    char *tmp=malloc(ol-mlen+vl+1);
                    memcpy(tmp,out,poff2);
                    memcpy(tmp+poff2,mval,vl);
                    strcpy(tmp+poff2+vl,p+mlen);
                    free(out); out=tmp;
                    p=out+poff2+vl;
                    changed=1;
                }
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

    /* Helper: append text to output buffer */

    /* Split source into lines, process each */
    char *copy = my_strdup(src);
    char *line;
    char *rest = copy;
    int pp_lineno = 0;
    while ((line = (rest && *rest) ? rest : NULL) != NULL) {
        char *_nl = strchr(rest, '\n');
        if (_nl) { *_nl = '\0'; rest = _nl+1; } else rest = NULL;
        pp_lineno++;
        if (pp_lineno >= 40 && pp_lineno <= 50 && filename && strstr(filename,"symtable.c")) { printf("[pp] %s line=%d: %.80s\n", filename, pp_lineno, line); fflush(0); pp_expand_dbg=1; } else pp_expand_dbg=0;
        const char *p = line;
        while (*p==' '||*p=='\t') p++;

        if (*p != '#') {
            /* Regular source line: macro-expand and emit if active */
            if ((cond_active[cond_depth])) {
                char *expanded = pp_expand(st, line);
                do { size_t _emn=(strlen(expanded)); while(len+_emn+2>cap){cap*=2;out=(char*)realloc(out,cap);} memcpy(out+len,(expanded),_emn); len+=_emn; } while(0);
                do { size_t _emn=(1); while(len+_emn+2>cap){cap*=2;out=(char*)realloc(out,cap);} memcpy(out+len,("\n"),_emn); len+=_emn; } while(0);
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
        if (!(cond_active[cond_depth])) continue;

        /* ── #define ──────────────────────────── */
        if (strncmp(p,"define",6)==0 && !isalnum((unsigned char)p[6]) && p[6]!='_') {
            p += 6;
            while (*p==' '||*p=='\t') p++;
            const char *nm = p;
            while (isalnum((unsigned char)*p)||*p=='_') p++;
            int nlen = (int)(p-nm);
            if (nlen==0) continue;
            /* Handle function-like macros: #define FOO(a,b) body */
            if (*p=='(') {
                p++; /* skip '(' */
                char pnames_buf[1024]; int np=0;  /* 16*64 */
                while (*p && *p!=')' && np<16) {
                    while (*p==' '||*p=='\t') p++;
                    if (*p==')') break;
                    if (strncmp(p,"...",3)==0) { p+=3; break; } /* variadic - skip */
                    int pl=0;
                    while ((isalnum((unsigned char)*p)||*p=='_') && pl<63)
                        pnames_buf[np*64+pl++]=*p++;
                    pnames_buf[np*64+pl]='\0';
                    if (pl>0) np++;
                    while (*p==' '||*p=='\t') p++;
                    if (*p==',') p++;
                }
                while (*p && *p!=')') p++;
                if (*p==')') p++;
                while (*p==' '||*p=='\t') p++;
                /* Get body (handle backslash continuation) */
                char body[4096]=""; int bi=0;
                while (*p && *p!='\n' && *p!='\r' && bi<4095) {
                    if (*p=='\\' && (p[1]=='\n'||p[1]=='\r')) {
                        p+=2; if (*p=='\n') p++;
                        while (*p==' '||*p=='\t') p++;
                        if (bi>0 && body[bi-1]!=' ') body[bi++]=' ';
                    } else { body[bi++]=*p++; }
                }
                body[bi]='\0';
                /* Trim trailing whitespace */
                while (bi>0 && (body[bi-1]==' '||body[bi-1]=='\t')) body[--bi]='\0';
                /* Register function-like macro */
                int idx2=pp_macro_find(st,nm,nlen);
                if (idx2<0) { idx2=st->nmc++; }
                { char *_snd2=malloc(nlen+1); strncpy(_snd2,nm,nlen); _snd2[nlen]='\0'; st->macros[idx2].name=_snd2; }
                st->macros[idx2].value=my_strdup(body);
                st->macros[idx2].nparams=np;
                { /* Use temp pointer to avoid squash nested-array-in-struct codegen bug */
                    char **_pp = st->macros[idx2].params;
                    for (int pi=0;pi<np;pi++) _pp[pi]=my_strdup(pnames_buf+pi*64);
                    for (int pi=np;pi<16;pi++) _pp[pi]=NULL;
                }
                continue;
            }
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

            /* For system includes (<stdio.h> etc.), try our built-in include/ dir first */
            char sys_path[600];
            if (system_inc) {
                /* Strip any leading path: use just the filename */
                const char *base = incname;
                for (const char *cp=incname; *cp; cp++) if(*cp=='/'||*cp=='\\') base=cp+1;
                snprintf(sys_path, sizeof sys_path, "include/%s", base);
                incname[0] = '\0'; /* signal to use sys_path */
                strncat(incname, sys_path, sizeof(sys_path)-1);
            }
            char *inc_src = pp_read_file(st, incname);
            if (!inc_src) {
                printf("preprocessor: cannot find '%s'\n", incname);
                continue;
            }
            char *inc_out = process_file(st, inc_src, incname);
            do { size_t _emn=(strlen(inc_out)); while(len+_emn+2>cap){cap*=2;out=(char*)realloc(out,cap);} memcpy(out+len,(inc_out),_emn); len+=_emn; } while(0);
            free(inc_src); free(inc_out);
            continue;
        }

        /* ── #pragma / unknown ────────────────── */
        /* silently skip */
    }

    free(copy);
    out[len] = '\0';
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
    /* Standard streams: use msvcrt file descriptor values */
    pp_macro_define(st,"stdin", 5,"((void*)0)");
    pp_macro_define(st,"stdout",6,"((void*)1)");
    pp_macro_define(st,"stderr",6,"((void*)2)");

    char *result = process_file(st, src, filename);

    /* free macros */
    for (int i=0;i<st->nmc;i++) {
        free(st->macros[i].name);
        free(st->macros[i].value);
        if (st->macros[i].nparams >= 0) {
            char **_pp = st->macros[i].params;
            for(int j=0;j<16&&_pp[j];j++) free(_pp[j]);
        }
    }
    for (int i=0;i<st->n_included;i++) free(st->included[i]);
    free(st);
    return result;
}

