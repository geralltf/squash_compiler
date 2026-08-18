#include "lexer.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* portable strdup replacement */
char* my_strdup(const char* src);

/* Set from compiler.c's -macos flag (defined in ast.c). Declared here
 * rather than #including ast.h so the lexer keeps its existing, deliberate
 * independence from the AST layer. */
extern int g_squash_macos_target;
extern int g_squash_openbsd_target;

/* =========================================================================
 * Error reporting helper
 * ========================================================================= */
static void lex_error(const Lexer *l, const char *msg) {
    /* l->filename is only ever the top-level file the lexer was handed —
     * meaningless once #include has flattened real headers ahead of it.
     * diag_emit resolves l->line (a position in that flattened buffer)
     * back to the real originating file via the preprocessor's line map. */
    diag_emit(DIAG_ERROR, l->line, NULL, NULL, "%s", msg);
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
    case 3: KW_CMP("for",3,TOK_FOR); KW_CMP("int",3,TOK_INT); KW_CMP("asm",3,TOK_ASM); break;
    case 4: KW_CMP("auto",4,TOK_AUTO); KW_CMP("case",4,TOK_CASE); KW_CMP("char",4,TOK_CHAR);
            KW_CMP("else",4,TOK_ELSE); KW_CMP("enum",4,TOK_ENUM); KW_CMP("goto",4,TOK_GOTO);
            KW_CMP("long",4,TOK_LONG); KW_CMP("void",4,TOK_VOID); break;
    case 5: KW_CMP("_Bool",5,TOK_BOOL); KW_CMP("__asm",5,TOK_ASM);
            KW_CMP("break",5,TOK_BREAK); KW_CMP("const",5,TOK_CONST); KW_CMP("float",5,TOK_FLOAT_KW);
            KW_CMP("short",5,TOK_SHORT); KW_CMP("union",5,TOK_UNION); KW_CMP("while",5,TOK_WHILE); break;
    case 6: KW_CMP("typeof",6,TOK_TYPEOF);
            KW_CMP("double",6,TOK_DOUBLE); KW_CMP("extern",6,TOK_EXTERN); KW_CMP("inline",6,TOK_INLINE);
            KW_CMP("return",6,TOK_RETURN); KW_CMP("signed",6,TOK_SIGNED); KW_CMP("sizeof",6,TOK_SIZEOF);
            KW_CMP("static",6,TOK_STATIC); KW_CMP("struct",6,TOK_STRUCT); KW_CMP("switch",6,TOK_SWITCH); break;
    case 7: KW_CMP("nullptr",7,TOK_NULLPTR); KW_CMP("__asm__",7,TOK_ASM);
            KW_CMP("alignas",7,TOK_ALIGNAS); KW_CMP("alignof",7,TOK_ALIGNOF);
            KW_CMP("default",7,TOK_DEFAULT); KW_CMP("typedef",7,TOK_TYPEDEF); break;
    case 8: KW_CMP("_Alignas",8,TOK_ALIGNAS); KW_CMP("_Alignof",8,TOK_ALIGNOF);
            KW_CMP("__typeof",8,TOK_TYPEOF); KW_CMP("noreturn",8,TOK_NORETURN);
            KW_CMP("continue",8,TOK_CONTINUE); KW_CMP("register",8,TOK_REGISTER);
            KW_CMP("unsigned",8,TOK_UNSIGNED); KW_CMP("volatile",8,TOK_VOLATILE); break;
    case 9: KW_CMP("_Noreturn",9,TOK_NORETURN); KW_CMP("constexpr",9,TOK_CONSTEXPR);
            KW_CMP("__alignof",9,TOK_ALIGNOF); break;
    case 10: KW_CMP("__typeof__",10,TOK_TYPEOF); KW_CMP("__volatile",10,TOK_VOLATILE); break;
    case 11: KW_CMP("thread_local",12,TOK_THREAD_LOCAL); break; /* checked at case 11 won't match — see case 12 */
    case 12: KW_CMP("thread_local",12,TOK_THREAD_LOCAL); KW_CMP("__volatile__",12,TOK_VOLATILE); break;
    case 13: KW_CMP("_Thread_local",13,TOK_THREAD_LOCAL);
             KW_CMP("static_assert",13,TOK_STATIC_ASSERT); break;
    case 14: KW_CMP("_Static_assert",14,TOK_STATIC_ASSERT); break;
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
    case TOK_BOOL: case TOK_CONSTEXPR: case TOK_NORETURN:
    case TOK_ALIGNAS: case TOK_THREAD_LOCAL: case TOK_INLINE:
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
    l->cur.ival=0; l->cur.sval=0; l->cur.len=0; l->cur.is_single=0;

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
        /* C99 hex float: 0x<hexdigits>[.<hexdigits>]p[+-]<decdigits>[f|F|l|L]
         * — the 'p'/'P' binary exponent is mandatory for a hex constant to
         * be a float (unlike decimal, where '.' alone is enough). Without
         * this, "0x1p-24f" (real SDL3 source, stdlib/SDL_random.c's
         * SDL_randf_r()) lexed as hex integer "0x1" only, leaving "p-24f"
         * behind as a bogus identifier token — breaking every build that
         * includes that file. */
        if (l->src[l->pos]=='.' || l->src[l->pos]=='p' || l->src[l->pos]=='P') {
            double mantissa = (double)v;
            if (l->src[l->pos]=='.') {
                lex_adv(l);
                double frac_scale = 1.0/16.0;
                while (isxdigit((unsigned char)l->src[l->pos])) {
                    char c=lex_adv(l);
                    int digit = isdigit((unsigned char)c)?c-'0':tolower((unsigned char)c)-'a'+10;
                    mantissa += digit*frac_scale;
                    frac_scale /= 16.0;
                }
            }
            int exp_val = 0, exp_neg = 0;
            if (l->src[l->pos]=='p' || l->src[l->pos]=='P') {
                lex_adv(l);
                if (l->src[l->pos]=='+'||l->src[l->pos]=='-') { exp_neg = (l->src[l->pos]=='-'); lex_adv(l); }
                while (isdigit((unsigned char)l->src[l->pos])) exp_val = exp_val*10 + (lex_adv(l)-'0');
            }
            double result = mantissa;
            int e = exp_neg ? -exp_val : exp_val;
            /* ldexp-equivalent via repeated doubling/halving (avoids pulling in math.h here) */
            while (e > 0) { result *= 2.0; e--; }
            while (e < 0) { result /= 2.0; e++; }
            l->cur.fval = result;
            l->cur.ival = (long long)result;
            is_float = 1;
        }
    /* binary literal: 0b... or 0B... (C23 / GCC extension) */
    } else if (l->src[l->pos]=='0' && (l->src[l->pos+1]=='b'||l->src[l->pos+1]=='B')) {
        lex_adv(l); lex_adv(l);
        unsigned long long v=0;
        while (l->src[l->pos]=='0'||l->src[l->pos]=='1') {
            v=v*2+(lex_adv(l)-'0');
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
            /* Octal literal: a leading '0' followed by more digits (and not
             * a float, ruled out above) means base 8 in C, not base 10 —
             * e.g. macho_builder.c's own "chmod(path, 0755);" evaluated to
             * decimal 755 instead of octal 493 (0x1ED), silently writing
             * the wrong file-permission bits on every squash-compiled
             * program's own output binary (confirmed self-hosting: genA
             * writing genB came out world/owner-write-only with no read
             * bit, "--wxrwx-wt" instead of "-rwxr-xr-x"). Only re-parses
             * as octal when every digit in the run is a valid octal digit
             * (0-7); "08"/"09" have no valid interpretation as an octal
             * literal in real C either (a constraint violation there), so
             * this just leaves those as the decimal value already parsed,
             * matching how they'd otherwise be silently misread anyway. */
            int len = (int)(l->src + l->pos - l->cur.start);
            if (len > 1 && l->cur.start[0]=='0') {
                int all_octal = 1;
                for (int i=1;i<len;i++) if (l->cur.start[i] < '0' || l->cur.start[i] > '7') { all_octal = 0; break; }
                if (all_octal) {
                    long long ov = 0;
                    for (int i=1;i<len;i++) ov = ov*8 + (l->cur.start[i]-'0');
                    v = ov;
                }
            }
            l->cur.ival=v;
        }
    }
    /* MSVC fixed-width integer literal suffixes: i8/i16/i32/i64, optionally
     * u/U-prefixed (ui8/ui16/ui32/ui64) — e.g. real wincodec.h's "0xffui8".
     * Consumed and discarded like every other suffix here: this constant
     * evaluator/codegen doesn't track literal width precisely enough for
     * it to matter which of these was written, only U/L's signedness
     * distinction does (handled below), so just recognize and skip the
     * whole "[uU]?i(8|16|32|64)" run before the ordinary suffix loop. */
    {
        int p = l->pos;
        int up = (l->src[p]=='u'||l->src[p]=='U') ? 1 : 0;
        if (l->src[p+up]=='i' &&
            ((l->src[p+up+1]=='8' && !isdigit((unsigned char)l->src[p+up+2])) ||
             (l->src[p+up+1]=='1'&&l->src[p+up+2]=='6') ||
             (l->src[p+up+1]=='3'&&l->src[p+up+2]=='2') ||
             (l->src[p+up+1]=='6'&&l->src[p+up+2]=='4'))) {
            while (l->pos < p+up+1) lex_adv(l); /* consume [uU]?i */
            while (isdigit((unsigned char)l->src[l->pos])) lex_adv(l); /* consume width digits */
        }
    }
    /* Consume integer/float suffixes: U, L, LL, UL, F, etc. */
    while (l->src[l->pos]=='u'||l->src[l->pos]=='U'||
           l->src[l->pos]=='l'||l->src[l->pos]=='L'||
           l->src[l->pos]=='f'||l->src[l->pos]=='F') {
        if (l->src[l->pos]=='f'||l->src[l->pos]=='F') l->cur.is_single=1;
        lex_adv(l);
    }
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
    /* Wide/unicode string/char prefixes: L"...", u8"...", u"...", U"...", L'...', u'...', U'...'
     * L"..." specifically is a WIDE (UTF-16 on Windows) string literal —
     * squash previously discarded the L prefix entirely and lexed it
     * identically to a plain narrow "...", so codegen had no way to know
     * to emit 2-bytes-per-character data. Stash "was wide" in the
     * token's otherwise-unused (for strings) ival field so codegen can
     * later encode it as real UTF-16LE instead of silently truncating
     * every other byte's worth of width — see AST_STRING's is_wide. */
    if ((c=='L'||c=='U') && (c2=='"'||c2=='\'')) {
        lex_adv(l); /* skip prefix char */
        if (l->src[l->pos]=='"') { lex_string(l); if (c=='L') l->cur.ival=1; return; }
        else { lex_char(l); return; }
    }
    if (c=='u' && c2=='8' && (pc2(l)=='"'||pc2(l)=='\'')) {
        lex_adv(l); lex_adv(l); /* skip 'u' and '8' */
        if (l->src[l->pos]=='"') { lex_string(l); return; }
        else { lex_char(l); return; }
    }
    if (c=='u' && (c2=='"'||c2=='\'')) {
        lex_adv(l); /* skip 'u' */
        if (l->src[l->pos]=='"') { lex_string(l); return; }
        else { lex_char(l); return; }
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
        default: {
            char onech[2] = { c, '\0' };
            diag_emit(DIAG_ERROR, t.line, NULL, onech,
                      "unknown character '%c' (0x%02X)", c, (unsigned char)c);
            t.kind=TOK_ERROR; break;
        }
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
        char gotbuf[256];
        int gl = l->cur.len; if (gl > (int)sizeof(gotbuf)-1) gl = (int)sizeof(gotbuf)-1;
        if (gl > 0) snprintf(gotbuf, sizeof gotbuf, "%.*s", gl, l->cur.start); else gotbuf[0]='\0';
        diag_emit(DIAG_ERROR, l->cur.line, NULL, gotbuf[0] ? gotbuf : NULL,
                  "expected '%s' but got '%s'", token_kind_name(k), token_kind_name(l->cur.kind));
        exit(1);
    }
    lexer_next(l);
}

Token lexer_expect(Lexer *l, TokenKind k) {
    Token t=l->cur;
    if (t.kind!=k) {
        char gotbuf[256];
        int gl = t.len; if (gl > (int)sizeof(gotbuf)-1) gl = (int)sizeof(gotbuf)-1;
        if (gl > 0) snprintf(gotbuf, sizeof gotbuf, "%.*s", gl, t.start); else gotbuf[0]='\0';
        diag_emit(DIAG_ERROR, t.line, NULL, gotbuf[0] ? gotbuf : NULL,
                  "expected '%s' but got '%s'", token_kind_name(k), token_kind_name(t.kind));
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
    case TOK_EOF:     return "EOF";
    case TOK_CONSTEXPR:    return "constexpr";
    case TOK_NORETURN:     return "_Noreturn";
    case TOK_TYPEOF:       return "typeof";
    case TOK_ALIGNAS:      return "_Alignas";
    case TOK_ALIGNOF:      return "_Alignof";
    case TOK_BOOL:         return "_Bool";
    case TOK_THREAD_LOCAL: return "_Thread_local";
    case TOK_NULLPTR:      return "nullptr";
    case TOK_STATIC_ASSERT:return "_Static_assert";
    case TOK_ASM:          return "asm";
    default: return "?";
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

#define PP_MAX_MACROS   65536   /* real Windows SDK headers (winnt.h etc, when
                                  * imported) define far more than SDL3's own
                                  * ~1024-2000 macros combined */
#define PP_MAX_DEPTH    64      /* max nested #if depth            */
#define PP_MAX_INCLUDED 256     /* max distinct files included     */

typedef struct {
    char *name;
    char *value;
    char *params[16]; /* NULL-terminated list of param names, NULL if object-like */
    int   nparams;    /* -1 = object-like, >=0 = function-like */
} PPMacro;

/* Macro name -> macros[] index hash table (open addressing, linear probing).
 * Replaces an O(n) linear scan (pp_macro_find) and an O(n) per-line,
 * O(n*text_len) per-pass scan (pp_expand, which used to loop over every
 * defined macro and strstr() the whole line for each one) with O(1)-average
 * lookups. Both became a real bottleneck once macro counts grow into the
 * thousands (e.g. importing real Windows SDK headers, whose winnt.h alone
 * defines several thousand macros) -- SDL3's own ~1-2k combined macros
 * stayed fast enough for this to go unnoticed until then. Size is a power
 * of 2, ~4x PP_MAX_MACROS for a low load factor even at full capacity. */
#define PP_HASH_SIZE    262144
#define PP_HASH_EMPTY   (-1)
#define PP_HASH_DELETED (-2)

typedef struct {
    PPMacro macros[PP_MAX_MACROS];
    int     nmc;
    int     hash_slots[PP_HASH_SIZE]; /* value = index into macros[], or EMPTY/DELETED */

    /* included-file deduplication (header guards via __FILE_ONCE__) */
    char   *included[PP_MAX_INCLUDED];
    int     n_included;

    /* include search paths */
    const char **inc_dirs;
    int          n_dirs;

    /* Carries a still-open /\* block comment *\/ across the line-splitting
     * loop in process_file() — preprocessing works one line at a time, so
     * without this a multi-line Doxygen comment forgets it's still inside
     * a comment on every line after the first, and identifiers mentioned
     * in the comment text (e.g. "\sa SDL_MAX_SINT64") get macro-expanded
     * as if they were real code. 0 = not in a block comment, 1 = in one. */
    int in_block_comment;
    /* True iff the still-open comment above STARTED on a directive line
     * (#define/#if/etc — which are control constructs, never echoed to the
     * preprocessed output themselves). A comment that starts on an ordinary
     * source line has its own opening "/\*" marker already sitting in the
     * output, so later continuation lines can be left alone (a real,
     * single-pass C lexer re-scanning the FULL preprocessed text will
     * correctly treat the whole span as one comment, newlines and all —
     * this preprocessor doesn't need to strip comments from regular
     * output itself). But a comment that starts on a DIRECTIVE line's own
     * value/expression (e.g. real qos.h's "#define SERVICETYPE_NOTRAFFIC
     * 0  /\* No data in this\n * direction *\/") never gets its opening
     * marker echoed at all (the #define line itself is never emitted) —
     * so its continuation line(s) must be suppressed from the output
     * until the comment closes, or a lone orphaned "*\/" with no matching
     * opener ends up corrupting the output. */
    int in_block_comment_from_directive;
} PPState;

/* ── helpers ─────────────────────────────────────────────────────────── */

static unsigned pp_hash_name(const char *name, int len) {
    /* FNV-1a — fast, simple, good-enough distribution for identifier text. */
    unsigned h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= (unsigned char)name[i];
        h *= 16777619u;
    }
    return h;
}

/* Returns the hash_slots[] slot currently holding `name`, or -1 if absent. */
static int pp_hash_find_slot(PPState *st, const char *name, int len) {
    unsigned h = pp_hash_name(name, len) & (PP_HASH_SIZE - 1);
    for (unsigned probe = 0; probe < PP_HASH_SIZE; probe++) {
        int slot = (int)((h + probe) & (PP_HASH_SIZE - 1));
        int idx = st->hash_slots[slot];
        if (idx == PP_HASH_EMPTY) return -1;
        if (idx != PP_HASH_DELETED &&
            (int)strlen(st->macros[idx].name) == len &&
            strncmp(st->macros[idx].name, name, len) == 0) {
            return slot;
        }
    }
    return -1;
}

/* Returns an empty-or-tombstone slot suitable for a NEW entry named `name`.
 * Caller must already know no live entry for this name exists. */
static int pp_hash_find_insert_slot(PPState *st, const char *name, int len) {
    unsigned h = pp_hash_name(name, len) & (PP_HASH_SIZE - 1);
    for (unsigned probe = 0; probe < PP_HASH_SIZE; probe++) {
        int slot = (int)((h + probe) & (PP_HASH_SIZE - 1));
        if (st->hash_slots[slot] == PP_HASH_EMPTY || st->hash_slots[slot] == PP_HASH_DELETED) {
            return slot;
        }
    }
    return -1; /* table full — shouldn't happen given PP_HASH_SIZE's margin */
}

static int pp_macro_find(PPState *st, const char *name, int len) {
    int slot = pp_hash_find_slot(st, name, len);
    return slot < 0 ? -1 : st->hash_slots[slot];
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
    { int slot = pp_hash_find_insert_slot(st, st->macros[st->nmc].name, nlen);
      if (slot >= 0) st->hash_slots[slot] = st->nmc; }
    st->nmc++;
}

static void pp_macro_undef(PPState *st, const char *name) {
    int nlen = (int)strlen(name);
    int slot = pp_hash_find_slot(st, name, nlen);
    if (slot < 0) return;
    int i = st->hash_slots[slot];
    st->hash_slots[slot] = PP_HASH_DELETED;

    free(st->macros[i].name);
    free(st->macros[i].value);
    { char **_pi = st->macros[i].params;
      for (int _j=0;_j<16;_j++) { free(_pi[_j]); _pi[_j]=NULL; } }
    /* Replace with last entry — use field-by-field copy to avoid struct assignment
     * (squash codegen does not handle struct-to-struct copies of large structs). */
    --st->nmc;
    if (i < st->nmc) {
        /* The macro currently at st->macros[st->nmc] is about to move to
         * index i — find the (already-correct) hash slot pointing at its
         * OLD index and repoint it at the new one, instead of treating
         * this as a fresh insertion. Must happen before the copy below
         * only in the sense that it reads the still-intact source name;
         * the pointer itself isn't cleared until after the copy. */
        int moved_slot = pp_hash_find_slot(st, st->macros[st->nmc].name,
                                            (int)strlen(st->macros[st->nmc].name));

        st->macros[i].name   = st->macros[st->nmc].name;
        st->macros[i].value  = st->macros[st->nmc].value;
        { char **_psrc = st->macros[st->nmc].params;
          char **_pdst = st->macros[i].params;
          int _j; for (_j=0;_j<16;_j++) _pdst[_j] = _psrc[_j]; }
        st->macros[i].nparams= st->macros[st->nmc].nparams;

        if (moved_slot >= 0) st->hash_slots[moved_slot] = i;

        /* Zero vacated slot so its pointers aren't reused if the slot
         * is later filled by a new object-like macro. */
        st->macros[st->nmc].name = NULL;
        st->macros[st->nmc].value = NULL;
        { char **_pz = st->macros[st->nmc].params;
          int _k; for (_k=0;_k<16;_k++) _pz[_k] = NULL; }
        st->macros[st->nmc].nparams = -1;
    }
}

static int pp_macro_defined(PPState *st, const char *name) {
    int len = (int)strlen(name);
    return pp_macro_find(st, name, len) >= 0;
}

/* Expand macros in a single text token (word-boundary check). */
/* Check if position p in string out is inside a string literal, char
 * literal, or comment (// line or /\* block *\/). Used to prevent macro
 * expansion from firing on plain-English identifiers that merely appear in
 * a Doxygen comment (e.g. "\sa SDL_MAX_SINT64") — expanding those inline
 * can splice a literal "/ *  ... * /" sequence into the comment body and
 * prematurely close it, turning the rest of the comment into bogus code. */
static int pp_in_string(const char *out, const char *p, int start_state) {
    int in_str = start_state;  /* 0=normal, 1=dquote, 2=squote, 3=block comment, 4=line comment */
    const char *c = out;
    while (c < p) {
        if (!in_str) {
            if (c[0]=='/' && c[1]=='*') { in_str = 3; c += 2; continue; }
            if (c[0]=='/' && c[1]=='/') { in_str = 4; c += 2; continue; }
            if (*c == '"')  { in_str = 1; c++; continue; }
            if (*c == '\'') { in_str = 2; c++; continue; }
        } else if (in_str == 1) {
            if (*c == '\\') { c += 2; continue; }  /* skip escaped char */
            if (*c == '"')  { in_str = 0; c++; continue; }
        } else if (in_str == 2) {
            if (*c == '\\') { c += 2; continue; }  /* skip escaped char */
            if (*c == '\'') { in_str = 0; c++; continue; }
        } else if (in_str == 3) {
            if (c[0]=='*' && c[1]=='/') { in_str = 0; c += 2; continue; }
        } else { /* in_str == 4: line comment ends at newline */
            if (*c == '\n') { in_str = 0; c++; continue; }
        }
        c++;
    }
    return in_str != 0;
}

/* Strips // and /\* *\/ comments from a single line of text in place,
 * respecting string/char literals. #define bodies are captured as raw
 * "rest of the line" text — without this, a trailing "// comment" on a
 * #define line (extremely common, e.g. SDL_sysaudio.h's
 * "#define SDL_MAX_CHANNELMAP_CHANNELS 8  // FIXME: ...") becomes part of
 * the macro's stored VALUE, and gets spliced into every expansion site
 * verbatim — turning "arr[SDL_MAX_CHANNELMAP_CHANNELS]" into
 * "arr[8  // FIXME: ...]", where the // then eats the rest of THAT line,
 * including the closing ']' and ';'. */
static void pp_strip_line_comments_from(char *s, int start_state) {
    int state = start_state; /* 0=normal, 1=dquote, 2=squote, 3=block comment */
    char *r = s, *w = s;
    while (*r) {
        if (state==0) {
            if (r[0]=='/' && r[1]=='/') break; /* rest of line is a comment */
            if (r[0]=='/' && r[1]=='*') { state=3; r+=2; continue; }
            if (*r=='"')  { state=1; *w++=*r++; continue; }
            if (*r=='\'') { state=2; *w++=*r++; continue; }
            *w++=*r++;
        } else if (state==1) {
            if (*r=='\\') { *w++=*r++; if(*r){*w++=*r++;} continue; }
            if (*r=='"') state=0;
            *w++=*r++;
        } else if (state==2) {
            if (*r=='\\') { *w++=*r++; if(*r){*w++=*r++;} continue; }
            if (*r=='\'') state=0;
            *w++=*r++;
        } else { /* state==3: block comment, drop chars until closed */
            if (r[0]=='*' && r[1]=='/') { state=0; r+=2; continue; }
            r++;
        }
    }
    *w='\0';
    int len=(int)(w-s);
    while (len>0 && (s[len-1]==' '||s[len-1]=='\t')) len--;
    s[len]='\0';
}

/* Scans a whole line to find the block-comment state at end-of-line, so it
 * can be carried into the next line via PPState::in_block_comment. A line
 * comment or an (invalid, unterminated) quote never legitimately persists
 * across a line boundary, so only block-comment state (3) is carried. */
static int pp_line_end_comment_state(const char *line, int start_state) {
    int state = start_state;
    const char *c = line;
    while (*c) {
        if (!state) {
            if (c[0]=='/' && c[1]=='*') { state = 3; c += 2; continue; }
            if (c[0]=='/' && c[1]=='/') { state = 4; c += 2; continue; }
            if (*c == '"')  { state = 1; c++; continue; }
            if (*c == '\'') { state = 2; c++; continue; }
        } else if (state == 1) {
            if (*c == '\\') { c += 2; continue; }
            if (*c == '"')  { state = 0; c++; continue; }
        } else if (state == 2) {
            if (*c == '\\') { c += 2; continue; }
            if (*c == '\'') { state = 0; c++; continue; }
        } else if (state == 3) {
            if (c[0]=='*' && c[1]=='/') { state = 0; c += 2; continue; }
        } else { /* state == 4: line comment always ends at this line's end */
            state = 0; break;
        }
        c++;
    }
    return (state == 3) ? 1 : 0;
}

/* Counts net unmatched '(' (opens minus closes) in a line, skipping over
 * strings/char-literals/comments, and reports the raw end-of-line
 * comment/string state (0..4, see pp_in_string) via *state_out. Used to
 * detect a function-like macro call whose argument list spans multiple
 * physical lines — legal, ordinary C (newlines are insignificant inside
 * parens), but this preprocessor otherwise expands macros one physical
 * line at a time, so such a call would be split mid-argument-list and
 * mangled. */
static int pp_line_paren_delta(const char *line, int start_state, int *state_out) {
    int state = start_state;
    int depth = 0;
    const char *c = line;
    while (*c) {
        if (!state) {
            if (c[0]=='/' && c[1]=='*') { state = 3; c += 2; continue; }
            if (c[0]=='/' && c[1]=='/') { state = 4; c += 2; continue; }
            if (*c == '"')  { state = 1; c++; continue; }
            if (*c == '\'') { state = 2; c++; continue; }
            if (*c == '(')  { depth++; c++; continue; }
            if (*c == ')')  { depth--; c++; continue; }
        } else if (state == 1) {
            if (*c == '\\') { c += 2; continue; }
            if (*c == '"')  { state = 0; c++; continue; }
        } else if (state == 2) {
            if (*c == '\\') { c += 2; continue; }
            if (*c == '\'') { state = 0; c++; continue; }
        } else if (state == 3) {
            if (c[0]=='*' && c[1]=='/') { state = 0; c += 2; continue; }
        } else { /* state == 4: line comment ends at this line's end */
            state = 0; break;
        }
        c++;
    }
    if (state_out) *state_out = state;
    return depth;
}

static char *pp_expand(PPState *st, const char *src) {
    char *out = my_strdup(src);
    int changed = 1;
    int max_iters = 100; /* prevent infinite expansion loops */
    int start_state = st->in_block_comment ? 3 : 0;
    while (changed && max_iters-- > 0) {
        changed = 0;
        /* Single pass over the text, tokenizing identifiers once and doing
         * an O(1)-average hash lookup per token, instead of the old
         * "for every one of the (up to tens of thousands of) defined
         * macros, strstr() the whole line looking for it" loop — that was
         * O(macro_count * line_length) per line and became the dominant
         * cost once macro counts grow into the thousands (e.g. importing
         * real Windows SDK headers). Scanning out maximal identifier runs
         * (isalnum/'_' chars) before doing the lookup also subsumes the old
         * per-match "pre"/"post" word-boundary checks for free: a token
         * found this way is never a prefix/suffix fragment of a longer
         * identifier, since the scan always consumes the FULL run. */
        char *p = out;
        while (*p) {
            if (*p == '\x01') {
                /* Blue-paint marker (see the self-reference substitution
                 * below): skip the marker AND the whole identifier that
                 * follows it WITHOUT looking it up as a macro, on every
                 * pass, forever — this occurrence must never be
                 * (re-)expanded as an invocation of the macro it came from. */
                p++;
                while (isalnum((unsigned char)*p) || *p=='_') p++;
                continue;
            }
            if (!(isalpha((unsigned char)*p) || *p=='_')) { p++; continue; }
            char *tok_start = p;
            while (isalnum((unsigned char)*p) || *p=='_') p++;
            int mlen = (int)(p - tok_start);
            int m = pp_macro_find(st, tok_start, mlen);
            if (m < 0) continue; /* not a macro name; p already past the token */

            const char *mname = st->macros[m].name;
            const char *mval  = st->macros[m].value;
            if (!mname || !mval) continue;
            int   is_fn = (st->macros[m].nparams >= 0);

            {
                char *tp = tok_start; /* matches the old loop's `p` at match time */
                int poff = (int)(tp - out);
                /* Skip macro expansion inside string/char literals/comments */
                if (pp_in_string(out, tp, start_state)) continue;
                /* A function-like macro invocation may have whitespace
                 * between the name and '(' (e.g. "CHECK_PARAM (1)" — valid,
                 * ordinary C). Peek past spaces/tabs to find the paren
                 * instead of requiring it immediately adjacent, otherwise
                 * such calls are silently never recognized as invocations
                 * at all (left as bare, unexpanded text). */
                const char *paren_pos = p; /* p already sits right after the token */
                if (is_fn) { while (*paren_pos==' '||*paren_pos=='\t') paren_pos++; }
                int post = is_fn ? (*paren_pos=='(') : 1; /* non-fn: token-boundary already guaranteed by the scan above */
                if (!post) continue;
                p = tp; /* rewind so the expansion code below sees the same `p` it always has */

                if (is_fn) {
                    /* Function-like: parse args from (a, b, c) */
                    char *ap = (char*)paren_pos + 1; /* skip '(' */
                    while (*ap==' '||*ap=='\t') ap++;
                    /* Use flat storage: args_buf[na*512 + char_offset] */
                    /* Heap alloc avoids squash codegen stack-frame bugs with large arrays */
                    char *args_buf = (char*)malloc(8192); int na=0;  /* 16 args * 512 bytes each */
                    int depth=1;
                    /* Parse comma-separated args, handling nested parens */
                    if (*ap != ')') {  /* non-empty arg list */
                        while (na < 16) {
                            int ai=0;
                            char *cur_arg = args_buf + na*512;
                            /* Track whether we're inside a "..."/'...' literal
                             * (0 = not in one, else '"' or '\'' = which kind
                             * closes it) so a comma or paren INSIDE a quoted
                             * string argument (e.g. real stb_image.h's
                             * "Image not of any known type, or corrupt") is
                             * never mistaken for an argument separator or
                             * depth change — without this, that single
                             * string argument silently split into multiple
                             * arguments at the embedded comma, one of them a
                             * string literal missing its closing quote,
                             * corrupting everything lexed afterward (a real
                             * repro: this fed a stray unterminated string
                             * into the token stream, surfacing much later as
                             * a confusing, seemingly unrelated "expected ')'
                             * but got IDENT" parse error). */
                            int in_str = 0;
                            while (*ap) {
                                if (in_str) {
                                    if (*ap=='\\' && ap[1]) {
                                        if (ai<510) { cur_arg[ai++]=*ap++; cur_arg[ai++]=*ap++; } else ap+=2;
                                        continue;
                                    }
                                    if (*ap==in_str) in_str=0;
                                    if (ai<511) cur_arg[ai++]=*ap++;
                                    continue;
                                }
                                if (*ap=='"' || *ap=='\'') { in_str=(int)*ap; if(ai<511) cur_arg[ai++]=*ap++; continue; }
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
                    /* Build expanded body by substituting params */
                    /* Use heap to avoid squash codegen issues with large stack arrays */
                    char *body = (char*)malloc(4096); body[0]='\0'; int bi=0;
                    const char *bp=mval;
                    while (*bp && bi<4095) {
                        /* Token-paste operator: A ## B -> AB (concatenated,
                         * no space). We don't retokenize, so just drop the
                         * "##" and surrounding whitespace; normal param
                         * substitution below then handles whatever follows. */
                        if (*bp=='#' && bp[1]=='#') {
                            bp+=2;
                            while (*bp==' '||*bp=='\t') bp++;
                            while (bi>0 && (body[bi-1]==' '||body[bi-1]=='\t')) bi--;
                            continue;
                        }
                        /* Stringification operator: #param -> "argument text". */
                        if (*bp=='#') {
                            const char *sp=bp+1;
                            while (*sp==' '||*sp=='\t') sp++;
                            int smatched=0;
                            char **_sparams = st->macros[m].params;
                            for (int pi=0; pi<st->macros[m].nparams && pi<na; pi++) {
                                const char *pn=_sparams[pi];
                                if (!pn) continue;
                                int pl=(int)strlen(pn);
                                if (strncmp(sp,pn,pl)==0 &&
                                    !(isalnum((unsigned char)sp[pl])||sp[pl]=='_')) {
                                    const char *argtxt=args_buf+pi*512;
                                    if (bi<4095) body[bi++]='"';
                                    for (const char *ac=argtxt; *ac && bi<4093; ac++) {
                                        if (*ac=='"'||*ac=='\\') body[bi++]='\\';
                                        body[bi++]=*ac;
                                    }
                                    if (bi<4095) body[bi++]='"';
                                    bp=sp+pl;
                                    smatched=1;
                                    break;
                                }
                            }
                            if (smatched) continue;
                        }
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
                        if (!matched) {
                            /* Self-reference check: does the macro body
                             * invoke its OWN name here (e.g. stb_image.h's
                             * real "#define stbi__err(x,y) stbi__err(y)" —
                             * intentionally forwarding only ONE of its two
                             * parameters to a same-named call, used to pick
                             * between differently-shaped implementations)?
                             * Per the C standard's "blue paint" rule this
                             * exact occurrence must never be re-expanded as
                             * another invocation of the SAME macro. Without
                             * this, the outer rescan-until-stable loop
                             * treats the substituted "stbi__err(...)" text
                             * as a brand-new invocation on its NEXT pass and
                             * tries to expand it again using the ORIGINAL
                             * (x,y) definition — but by then only whichever
                             * arg THIS body actually forwarded is available,
                             * so the other parameter name is left as a bare,
                             * unsubstituted literal in the final output
                             * (confirmed via a real repro reproducing
                             * stb_image.h's exact pattern: a literal "y"
                             * survived in place of the real message
                             * argument, four call frames removed from
                             * anything resembling a real error). Marked
                             * with a single SOH (0x01) sentinel byte
                             * immediately before the name — the outer scan
                             * loop above skips any marked identifier
                             * without ever looking it up again — then
                             * stripped in one final pass before returning. */
                            int nlen = (int)strlen(mname);
                            if (strncmp(bp,mname,nlen)==0 &&
                                !(isalnum((unsigned char)bp[nlen])||bp[nlen]=='_') &&
                                !(bp>mval && (isalnum((unsigned char)bp[-1])||bp[-1]=='_'))) {
                                if (bi<4095) body[bi++] = '\x01';
                                for (int ci=0; ci<nlen && bi<4095; ci++) body[bi++]=mname[ci];
                                bp += nlen;
                                matched = 1;
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
    /* Strip every blue-paint marker byte (0x01) now that expansion has
     * fully settled — they only ever existed to stop the rescan loop above
     * from re-expanding a self-referential macro's own name a second time;
     * the plain identifier text underneath was always correct output. */
    { char *rp=out, *wp=out; while (*rp) { if (*rp!='\x01') *wp++=*rp; rp++; } *wp='\0'; }
    return out;
}

/* Evaluate a #if / #elif constant expression.
 *
 * The previous implementation here only ever looked at the FIRST token of
 * the expression (a bare "defined(X)", a bare integer, or a bare
 * identifier) and returned immediately — despite its own comment claiming
 * to support "==, !=, &&, ||", none of those were actually implemented at
 * all. Any real expression combining more than one term (which is nearly
 * every non-trivial #if in real-world headers — e.g.
 * "!defined(__cplusplus) && ((defined(__GNUC__) && __GNUC__ >= 16) ||
 * SDL_HAS_EXTENSION(c_countof)) && (defined(__STDC_VERSION__) &&
 * __STDC_VERSION__ >= 202500L)" from SDL3's own SDL_stdinc.h) silently
 * evaluated to whatever the first term happened to be, picking the WRONG
 * branch with no error at all — e.g. that exact condition should be false
 * (squash defines __STDC_VERSION__ as 201710L, well under 202500L) but the
 * old code evaluated just "!defined(__cplusplus)" (true) and stopped,
 * wrongly selecting a branch that calls the nonexistent "_Countof"
 * function, producing a binary that fails to even *load*
 * (STATUS_DLL_NOT_FOUND) since that symbol was silently generated as
 * bogus import.
 *
 * This is a proper recursive-descent evaluator over the full C constant-
 * expression grammar needed here: || && | ^ & == != < > <= >= << >>
 * + - * / % ! ~ unary -/+, defined()/!defined(), parens, integer literals
 * (any base/suffix via strtoll), and macro-name lookup (expanded first via
 * the existing pp_expand(), after "defined(X)" is resolved to a literal
 * 1/0 — which must happen BEFORE macro expansion, since defined() asks
 * whether X itself is a macro name, not what X expands to). */
static long long pp_eval_or_expr(PPState *st, const char **pp);

static void pp_eval_skip_ws(const char **pp) { while (**pp==' '||**pp=='\t') (*pp)++; }

static long long pp_eval_primary(PPState *st, const char **pp) {
    pp_eval_skip_ws(pp);
    const char *p = *pp;
    if (*p=='(') { (*pp)++; long long v=pp_eval_or_expr(st,pp); pp_eval_skip_ws(pp); if(**pp==')') (*pp)++; return v; }
    if (*p=='!') { (*pp)++; return !pp_eval_primary(st,pp); }
    if (*p=='~') { (*pp)++; return ~pp_eval_primary(st,pp); }
    if (*p=='-') { (*pp)++; return -pp_eval_primary(st,pp); }
    if (*p=='+') { (*pp)++; return pp_eval_primary(st,pp); }
    if (isdigit((unsigned char)*p)) {
        char *end=NULL;
        long long v = strtoll(p, &end, 0);
        *pp = end ? end : p+1;
        while (**pp=='u'||**pp=='U'||**pp=='l'||**pp=='L') (*pp)++; /* integer suffix */
        return v;
    }
    if (isalpha((unsigned char)*p)||*p=='_') {
        /* Any bare identifier reaching here survived pp_resolve_defined()
         * and macro expansion (both done by the caller before parsing),
         * so per the C standard it's simply undefined: evaluates to 0. */
        while (isalnum((unsigned char)**pp)||**pp=='_') (*pp)++;
        return 0;
    }
    if (*p=='\0') return 0; /* malformed/truncated expression */
    (*pp)++; /* unrecognized character: skip it so callers still make progress */
    return 0;
}
static long long pp_eval_mul(PPState *st, const char **pp) {
    long long v = pp_eval_primary(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if (**pp=='*') { (*pp)++; v *= pp_eval_primary(st,pp); }
        else if (**pp=='/') { (*pp)++; long long r=pp_eval_primary(st,pp); v = r? v/r : 0; }
        else if (**pp=='%') { (*pp)++; long long r=pp_eval_primary(st,pp); v = r? v%r : 0; }
        else break;
    }
    return v;
}
static long long pp_eval_add(PPState *st, const char **pp) {
    long long v = pp_eval_mul(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if (**pp=='+') { (*pp)++; v += pp_eval_mul(st,pp); }
        else if (**pp=='-') { (*pp)++; v -= pp_eval_mul(st,pp); }
        else break;
    }
    return v;
}
static long long pp_eval_shift(PPState *st, const char **pp) {
    long long v = pp_eval_add(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if ((*pp)[0]=='<' && (*pp)[1]=='<') { (*pp)+=2; v <<= pp_eval_add(st,pp); }
        else if ((*pp)[0]=='>' && (*pp)[1]=='>') { (*pp)+=2; v >>= pp_eval_add(st,pp); }
        else break;
    }
    return v;
}
static long long pp_eval_rel(PPState *st, const char **pp) {
    long long v = pp_eval_shift(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if ((*pp)[0]=='<' && (*pp)[1]=='=') { (*pp)+=2; v = (v <= pp_eval_shift(st,pp)); }
        else if ((*pp)[0]=='>' && (*pp)[1]=='=') { (*pp)+=2; v = (v >= pp_eval_shift(st,pp)); }
        else if ((*pp)[0]=='<') { (*pp)+=1; v = (v < pp_eval_shift(st,pp)); }
        else if ((*pp)[0]=='>') { (*pp)+=1; v = (v > pp_eval_shift(st,pp)); }
        else break;
    }
    return v;
}
static long long pp_eval_eq(PPState *st, const char **pp) {
    long long v = pp_eval_rel(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if ((*pp)[0]=='=' && (*pp)[1]=='=') { (*pp)+=2; v = (v == pp_eval_rel(st,pp)); }
        else if ((*pp)[0]=='!' && (*pp)[1]=='=') { (*pp)+=2; v = (v != pp_eval_rel(st,pp)); }
        else break;
    }
    return v;
}
static long long pp_eval_band(PPState *st, const char **pp) {
    long long v = pp_eval_eq(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if ((*pp)[0]=='&' && (*pp)[1]!='&') { (*pp)++; v &= pp_eval_eq(st,pp); }
        else break;
    }
    return v;
}
static long long pp_eval_bxor(PPState *st, const char **pp) {
    long long v = pp_eval_band(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if ((*pp)[0]=='^') { (*pp)++; v ^= pp_eval_band(st,pp); }
        else break;
    }
    return v;
}
static long long pp_eval_bor(PPState *st, const char **pp) {
    long long v = pp_eval_bxor(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if ((*pp)[0]=='|' && (*pp)[1]!='|') { (*pp)++; v |= pp_eval_bxor(st,pp); }
        else break;
    }
    return v;
}
static long long pp_eval_and_expr(PPState *st, const char **pp) {
    long long v = pp_eval_bor(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if ((*pp)[0]=='&' && (*pp)[1]=='&') { (*pp)+=2; long long r=pp_eval_bor(st,pp); v = (v && r); }
        else break;
    }
    return v;
}
static long long pp_eval_or_expr(PPState *st, const char **pp) {
    long long v = pp_eval_and_expr(st,pp);
    for (;;) {
        pp_eval_skip_ws(pp);
        if ((*pp)[0]=='|' && (*pp)[1]=='|') { (*pp)+=2; long long r=pp_eval_and_expr(st,pp); v = (v || r); }
        else break;
    }
    return v;
}

/* Rewrites "defined(NAME)" / "defined NAME" to a literal "1"/"0" — must
 * run BEFORE macro expansion, since defined() tests whether NAME itself
 * is a macro, not what it expands to. */
static char *pp_resolve_defined(PPState *st, const char *expr) {
    size_t cap = strlen(expr) + 64, len = 0;
    char *out = (char*)malloc(cap);
    const char *p = expr;
    while (*p) {
        if (strncmp(p,"defined",7)==0 && !(isalnum((unsigned char)p[7])||p[7]=='_')) {
            p += 7;
            while (*p==' '||*p=='\t') p++;
            int has_paren = (*p=='(');
            if (has_paren) { p++; while (*p==' '||*p=='\t') p++; }
            const char *ns = p;
            while (isalnum((unsigned char)*p)||*p=='_') p++;
            int nlen = (int)(p-ns);
            if (has_paren) { while (*p==' '||*p=='\t') p++; if (*p==')') p++; }
            char name[256]; snprintf(name,sizeof name,"%.*s",nlen,ns);
            const char *rep = (nlen>0 && pp_macro_defined(st,name)) ? "1" : "0";
            size_t rl = strlen(rep);
            while (len+rl+1>cap) { cap*=2; out=(char*)realloc(out,cap); }
            memcpy(out+len,rep,rl); len+=rl;
            continue;
        }
        while (len+2>cap) { cap*=2; out=(char*)realloc(out,cap); }
        out[len++] = *p++;
    }
    out[len]='\0';
    return out;
}

static int pp_eval_expr(PPState *st, const char *expr) {
    /* #if/#elif condition lines routinely carry a trailing "// ..." or
     * "/* ... *\/" comment (e.g. SDL3's own libm/math_private.h:
     * "#if !defined(SDL_PLATFORM_HAIKU) && ... /* already defined in a
     * system header. *\/"). Left unstripped, the comment's own tokens
     * (like the '/' in "system") get fed into the expression evaluator
     * and corrupt the result — silently picking the wrong #if/#else
     * branch, exactly the same class of bug as the macro-body case this
     * session already fixed elsewhere in this file. */
    char *stripped = my_strdup(expr);
    pp_strip_line_comments_from(stripped, 0);
    char *resolved = pp_resolve_defined(st, stripped);
    free(stripped);
    char *expanded = pp_expand(st, resolved);
    free(resolved);
    const char *cursor = expanded;
    long long result = pp_eval_or_expr(st, &cursor);
    free(expanded);
    return result != 0;
}

/* Read a file from the include search path; returns heap string or NULL. */
/* resolved_out (if non-NULL) receives the path that actually opened —
 * either `filename` unchanged, or one of the "-I dir/filename" combos.
 * Without this, a file found only via an -I fallback kept the *unqualified*
 * name as its own "filename" for recursion purposes, so any relative
 * quoted #include *inside* it (e.g. "../SDL_internal.h") resolved against
 * the wrong (too-shallow) directory and failed — this bit SDL3's own
 * src/ tree hard, since most of its .c files are found via -Isrc but sit
 * several subdirectories deep and reach shared headers via "../..". */
static char *pp_read_file(PPState *st, const char *filename, char *resolved_out, size_t resolved_cap) {
    FILE *fp = fopen(filename, "rb");
    if (fp) {
        if (resolved_out) snprintf(resolved_out, resolved_cap, "%s", filename);
    } else {
        char path[1024];
        for (int i=0; i<st->n_dirs && !fp; i++) {
            snprintf(path,sizeof path,"%s/%s",st->inc_dirs[i],filename);
            fp = fopen(path,"rb");
            if (fp && resolved_out) snprintf(resolved_out, resolved_cap, "%s", path);
        }
    }
    if (!fp) return NULL;
    fseek(fp,0,SEEK_END); long sz=ftell(fp); rewind(fp);
    char *buf = malloc(sz+2); fread(buf,1,sz,fp); buf[sz]='\0';
    fclose(fp);
    /* normalize line endings: strip \r so CRLF->LF and bare CR->nothing */
    { int _ri=0, _wi=0; while (buf[_ri]) { if (buf[_ri]!='\r') buf[_wi++]=buf[_ri]; _ri++; } buf[_wi]='\0'; }
    return buf;
}

/* Forward declaration: process_file() is called recursively for #include. */
static char *process_file(PPState *st, const char *src, const char *filename);

/* =========================================================================
 * Line map: resolves a "merged" line number (a position in the single,
 * fully-preprocessed/#include-flattened buffer the lexer/parser/codegen all
 * actually operate on) back to the real originating file + line, for
 * diagnostics. Without this, every error/warning after preprocessing could
 * only report a line number counted across the ENTIRE flattened text —
 * meaningless to a user staring at their own real source file, and
 * especially useless once real headers (SDL3, Windows SDK, ...) are
 * #include-spliced in ahead of it.
 *
 * Built as a sparse list of "runs" (file, local_line) tagged with the
 * merged-line index where each run starts, appended in strictly increasing
 * merged-line order: process_file() emits exactly one output line per
 * source line (see the call site below), depth-first, in the exact order
 * lines end up in the final concatenated buffer (a #include recurses fully
 * before its caller's next line is processed) — so a single monotonically
 * increasing counter shared across every recursive call, incremented once
 * per emitted line, already equals that line's final position with no
 * extra bookkeeping. A new run is only pushed when the mapping becomes
 * discontinuous (a #include boundary, or a run of #if/#ifdef-excluded or
 * multi-line-macro-joined lines), so the map stays small even for huge
 * builds (hundreds of thousands of merged lines) rather than needing one
 * entry per line. */
typedef struct {
    int   merged_line;  /* 0-based index (into the final buffer) where this run starts */
    char *file;         /* real originating file (owned copy) */
    int   local_line;   /* that file's own 1-based line number at merged_line */
} LineMapRun;

static LineMapRun *g_linemap_runs = NULL;
static int g_linemap_n = 0, g_linemap_cap = 0;
static int g_linemap_out_lines = 0; /* shared cumulative output-line counter */

static void linemap_reset(void) {
    for (int i=0;i<g_linemap_n;i++) free(g_linemap_runs[i].file);
    free(g_linemap_runs);
    g_linemap_runs = NULL;
    g_linemap_n = 0; g_linemap_cap = 0; g_linemap_out_lines = 0;
}

static void linemap_push(int merged_line, const char *file, int local_line) {
    if (g_linemap_n > 0) {
        LineMapRun *last = &g_linemap_runs[g_linemap_n-1];
        int expected_local = last->local_line + (merged_line - last->merged_line);
        if (expected_local == local_line && strcmp(last->file, file)==0) return; /* still contiguous */
    }
    if (g_linemap_n >= g_linemap_cap) {
        g_linemap_cap = g_linemap_cap ? g_linemap_cap*2 : 256;
        g_linemap_runs = realloc(g_linemap_runs, g_linemap_cap*sizeof(LineMapRun));
    }
    g_linemap_runs[g_linemap_n].merged_line = merged_line;
    g_linemap_runs[g_linemap_n].file = my_strdup(file);
    g_linemap_runs[g_linemap_n].local_line = local_line;
    g_linemap_n++;
}

/* Public: see lexer.h. Binary search since runs are sorted by merged_line
 * (strictly increasing, by construction above). Returns NULL (and passes
 * merged_line straight through as out_local_line) if the map is empty —
 * e.g. if called before any preprocessing has happened. */
const char *linemap_resolve(int merged_line, int *out_local_line) {
    if (g_linemap_n == 0 || merged_line < 0) {
        if (out_local_line) *out_local_line = merged_line;
        return NULL;
    }
    int lo = 0, hi = g_linemap_n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (g_linemap_runs[mid].merged_line <= merged_line) lo = mid; else hi = mid - 1;
    }
    LineMapRun *r = &g_linemap_runs[lo];
    if (out_local_line) *out_local_line = r->local_line + (merged_line - r->merged_line);
    return r->file;
}

/* ── main preprocessor ───────────────────────────────────────────────── */
static char *process_file(PPState *st, const char *src, const char *filename) {
    /* Output buffer */
    size_t cap=65536, len=0;
    char *out = malloc(cap);
    out[0] = '\0';

    /* __FILE__: redefine on entry to each file (including recursive
     * #include calls) so it reflects whichever real source file is
     * actually being processed, not just the top-level translation unit. */
    {
        char *fq = malloc(strlen(filename) + 3);
        sprintf(fq, "\"%s\"", filename);
        pp_macro_define(st, "__FILE__", 8, fq);
        free(fq);
    }
    int pp_lineno = 0;

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

    /* Join backslash-continued lines (e.g. multi-line function-like macro
     * bodies) BEFORE splitting into lines below. Without this, the line
     * splitter below cuts the continuation off into what looks like a
     * separate, unrelated line — it's never recognized as part of the
     * #define, and leaks into the preprocessed output as bare top-level
     * code instead of becoming part of the macro body. Shrinks in place
     * (only ever removes characters), so writing through the same buffer
     * via a read/write pointer pair is safe. */
    {
        char *rp = copy, *wp = copy;
        while (*rp) {
            if (rp[0]=='\\' && rp[1]=='\r' && rp[2]=='\n') { rp += 3; *wp++ = ' '; }
            else if (rp[0]=='\\' && (rp[1]=='\n' || rp[1]=='\r')) { rp += 2; *wp++ = ' '; }
            else { *wp++ = *rp++; }
        }
        *wp = '\0';
    }

    char *line;
    char *rest = copy;
    while ((line = (rest && *rest) ? rest : NULL) != NULL) {
        char *_nl = strchr(rest, '\n');
        if (_nl) { *_nl = '\0'; rest = _nl+1; } else rest = NULL;
        /* __LINE__: redefine every physical line to the current line
         * number as a plain decimal literal. Not perfectly precise after a
         * backslash-continued multi-line macro (those were already joined
         * into one line earlier, so the count can drift a little past
         * one) — good enough for __LINE__'s real-world use (assert/debug
         * messages), not required for correctness elsewhere. */
        {
            pp_lineno++;
            char lnbuf[16];
            snprintf(lnbuf, sizeof lnbuf, "%d", pp_lineno);
            pp_macro_define(st, "__LINE__", 8, lnbuf);
        }
        const char *p = line;
        while (*p==' '||*p=='\t') p++;

        /* Track a /\* block comment *\/ across lines so a line that's
         * entirely (or partly) inside one isn't mistaken for a directive —
         * SDL's doc comments routinely show example code like
         * "\code #include <SDL3/SDL.h> \endcode" or "* #define FOO 1" and
         * those must never be treated as real preprocessor directives. */
        int line_starts_in_comment = st->in_block_comment;
        int line_comment_from_directive = st->in_block_comment_from_directive;

        if (*p != '#' || line_starts_in_comment) {
            /* Regular source line. A function-like macro call's argument
             * list may legally span multiple physical lines (newlines are
             * just whitespace inside parens in real C) — e.g. SDL_endian.h
             * has "SDL_static_cast(Uint32, ((x << 24) | ...\n... (x >> 24)))"
             * split across two lines with no trailing backslash. This
             * preprocessor otherwise expands macros one physical line at a
             * time, so without joining first, the argument list gets cut
             * mid-expression and mangled. Detect unbalanced '(' and pull in
             * following physical lines (consuming them from `rest`, so the
             * outer loop won't see them again) until parens balance. */
            char *joined = NULL;
            const char *to_expand = line;
            /* Only attempt the join when this line will actually be
             * emitted (cond_active). Inactive #if/#ifndef branches routinely
             * contain disabled, platform-specific example code that isn't
             * even meant to be well-formed C for THIS platform — a stray
             * unbalanced '(' in there must not make the join loop swallow
             * subsequent lines (which can include a real, load-bearing
             * #endif/#define from the surrounding active structure) since
             * those lines never get macro-expanded/emitted anyway. */
            if (!line_starts_in_comment && cond_active[cond_depth]) {
                int cstate;
                int depth = pp_line_paren_delta(line, 0, &cstate);
                if (depth > 0 && cstate == 0) {
                    /* Strip this first line's own trailing "// ..."/"/* *\/"
                     * comment before it becomes the seed of the joined
                     * buffer — see the identical strip on each `nextline`
                     * segment below for why this matters. */
                    char *line0 = my_strdup(line);
                    pp_strip_line_comments_from(line0, 0);
                    size_t jlen = strlen(line0);
                    size_t jcap = jlen + 256;
                    joined = (char*)malloc(jcap);
                    memcpy(joined, line0, jlen + 1);
                    free(line0);
                    int guard = 0;
                    /* A conditional-compilation directive (#ifdef/#ifndef/
                     * #if/#elif/#else/#endif) can legally appear INSIDE an
                     * unbalanced-paren span being joined here — e.g. real
                     * wincrypt.h's "CryptSignCertificate(#ifdef ... X #else
                     * ... Y #endif , DWORD dwKeySpec, ...)" (a conditional
                     * parameter). Without recognizing these, they fall
                     * through to the generic "append as text" path below and
                     * survive into the joined output as literal '#ifdef'
                     * tokens, which the parser then can't make sense of.
                     * Track a small LOCAL conditional stack (independent of
                     * the file-level cond_active/cond_depth, which this
                     * whole join is already known to be active under — see
                     * the `cond_active[cond_depth]` check that gated
                     * entering this branch) so only the chosen side's lines
                     * get appended. */
                    int jcond_active[16], jcond_seen_true[16], jcond_seen_else[16];
                    int jcond_depth = 0;
                    jcond_active[0] = 1;
                    while (depth > 0 && rest && *rest && guard++ < 200) {
                        char *nl2 = strchr(rest, '\n');
                        const char *nextline = rest;
                        size_t nextlen;
                        if (nl2) { nextlen = (size_t)(nl2 - rest); rest = nl2 + 1; }
                        else     { nextlen = strlen(rest);        rest = NULL;   }
                        /* Compute the paren delta on the RAW segment first
                         * (pp_line_paren_delta already understands comments
                         * well enough to skip over them for depth-counting
                         * purposes) — then strip the segment's own trailing
                         * comment before it's actually appended into
                         * `joined`. Without this, a "// explains this
                         * clause" comment on any but the LAST joined line
                         * survives into the single flattened output line
                         * this whole block produces; since real newlines
                         * were replaced with plain spaces during joining,
                         * the lexer's ordinary (and otherwise completely
                         * correct) "// runs to end of line" rule then treats
                         * EVERY later joined line's real code — up to and
                         * including the closing ')' that ended the join —
                         * as part of that one comment, silently deleting it.
                         * This is a genuinely common real-world shape: a
                         * multi-line boolean expression with one comment per
                         * clause (e.g. SDL3's own
                         * audio/SDL_audio.c:AudioDeviceCanUseSimpleCopy). */
                        char *nlcopy = (char*)malloc(nextlen + 1);
                        memcpy(nlcopy, nextline, nextlen);
                        nlcopy[nextlen] = '\0';
                        {
                            const char *dp = nlcopy;
                            while (*dp==' '||*dp=='\t') dp++;
                            if (*dp == '#') {
                                dp++;
                                while (*dp==' '||*dp=='\t') dp++;
                                int parent_active = jcond_active[jcond_depth];
                                if (!strncmp(dp,"ifdef",5) && !isalnum((unsigned char)dp[5]) && dp[5]!='_') {
                                    const char *nm=dp+5; while(*nm==' '||*nm=='\t') nm++;
                                    char nbuf[256]; int ni=0;
                                    while (nm[ni]&&!isspace((unsigned char)nm[ni])&&ni<255) ni++;
                                    snprintf(nbuf,sizeof nbuf,"%.*s",ni,nm);
                                    int val = parent_active ? pp_macro_defined(st, nbuf) : 0;
                                    if (++jcond_depth >= 16) jcond_depth = 15;
                                    jcond_active[jcond_depth]=val; jcond_seen_true[jcond_depth]=val; jcond_seen_else[jcond_depth]=0;
                                } else if (!strncmp(dp,"ifndef",6) && !isalnum((unsigned char)dp[6]) && dp[6]!='_') {
                                    const char *nm=dp+6; while(*nm==' '||*nm=='\t') nm++;
                                    char nbuf[256]; int ni=0;
                                    while (nm[ni]&&!isspace((unsigned char)nm[ni])&&ni<255) ni++;
                                    snprintf(nbuf,sizeof nbuf,"%.*s",ni,nm);
                                    int val = parent_active ? !pp_macro_defined(st, nbuf) : 0;
                                    if (++jcond_depth >= 16) jcond_depth = 15;
                                    jcond_active[jcond_depth]=val; jcond_seen_true[jcond_depth]=val; jcond_seen_else[jcond_depth]=0;
                                } else if (!strncmp(dp,"if",2) && !isalnum((unsigned char)dp[2]) && dp[2]!='_') {
                                    const char *expr=dp+2; while(*expr==' '||*expr=='\t') expr++;
                                    int val = parent_active ? pp_eval_expr(st, expr) : 0;
                                    if (++jcond_depth >= 16) jcond_depth = 15;
                                    jcond_active[jcond_depth]=val; jcond_seen_true[jcond_depth]=val; jcond_seen_else[jcond_depth]=0;
                                } else if (!strncmp(dp,"elif",4) && !isalnum((unsigned char)dp[4]) && dp[4]!='_') {
                                    if (jcond_depth > 0 && !jcond_seen_else[jcond_depth]) {
                                        const char *expr=dp+4; while(*expr==' '||*expr=='\t') expr++;
                                        int gp_active = jcond_depth>0 ? jcond_active[jcond_depth-1] : 1;
                                        if (!jcond_seen_true[jcond_depth] && gp_active) {
                                            int val = pp_eval_expr(st, expr);
                                            jcond_active[jcond_depth]=val; jcond_seen_true[jcond_depth]=val;
                                        } else jcond_active[jcond_depth]=0;
                                    }
                                } else if (!strncmp(dp,"else",4) && (!dp[4]||isspace((unsigned char)dp[4]))) {
                                    if (jcond_depth > 0 && !jcond_seen_else[jcond_depth]) {
                                        int gp_active = jcond_depth>0 ? jcond_active[jcond_depth-1] : 1;
                                        jcond_active[jcond_depth] = (!jcond_seen_true[jcond_depth]) && gp_active;
                                        jcond_seen_else[jcond_depth] = 1;
                                    }
                                } else if (!strncmp(dp,"endif",5) && (!dp[5]||isspace((unsigned char)dp[5]))) {
                                    if (jcond_depth > 0) jcond_depth--;
                                }
                                /* Directive line consumed: never appended to
                                 * `joined`, and its own (typically paren-free)
                                 * text doesn't participate in depth counting. */
                                free(nlcopy);
                                continue;
                            }
                        }
                        if (!jcond_active[jcond_depth]) { free(nlcopy); continue; }
                        /* Bound the delta scan to this line's own NUL-
                         * terminated copy — `nextline` itself points into
                         * the middle of the whole remaining source buffer
                         * with no terminator of its own, so scanning it
                         * directly would silently run past this line into
                         * subsequent ones and miscount the depth. */
                        int cstate2;
                        depth += pp_line_paren_delta(nlcopy, cstate, &cstate2);
                        cstate = cstate2;
                        pp_strip_line_comments_from(nlcopy, 0);
                        size_t striplen = strlen(nlcopy);
                        while (jlen + striplen + 2 > jcap) { jcap *= 2; joined = (char*)realloc(joined, jcap); }
                        joined[jlen++] = ' ';
                        memcpy(joined + jlen, nlcopy, striplen);
                        jlen += striplen;
                        joined[jlen] = '\0';
                        free(nlcopy);
                    }
                    to_expand = joined;
                }
            }
            /* macro-expand and emit if active */
            if ((cond_active[cond_depth])) {
                /* If this line started already inside an unclosed block
                 * comment (carried over from a #define, or any other
                 * line, whose own trailing comment spans multiple
                 * physical lines — e.g. real qos.h's "#define
                 * SERVICETYPE_NOTRAFFIC 0  ...comment-open-here" followed
                 * by a continuation line "...comment-close-here more"),
                 * this line is either entirely comment (never closes) or
                 * a comment tail followed by real code once it closes.
                 * Strip the still-open comment portion (starting from
                 * state 3) before expanding — without this,
                 * line_starts_in_comment only prevented misreading the
                 * line as a directive, but the raw (uncommented-looking)
                 * text still got macro-expanded and emitted verbatim as
                 * if it were real source, corrupting the output with
                 * stray comment-tail text and any code sharing that
                 * physical line with the comment's closing marker. */
                char *cstripped = NULL;
                const char *to_emit = to_expand;
                if (line_starts_in_comment && line_comment_from_directive) {
                    cstripped = my_strdup(to_expand);
                    pp_strip_line_comments_from(cstripped, 3);
                    to_emit = cstripped;
                }
                char *expanded = pp_expand(st, to_emit);
                if (cstripped) free(cstripped);
                /* Record where THIS output line really came from. The
                 * counter is 1-indexed (incremented BEFORE the push, not
                 * after) to match the lexer's own 1-indexed l->line
                 * numbering (Lexer starts at line=1 and increments on each
                 * consumed '\n') — every ASTNode/Token ->line value this
                 * gets looked up with is 1-indexed, so the map's own keys
                 * must be too, or every single lookup resolves one line
                 * short. See the line-map comment above process_file's
                 * forward declaration. */
                g_linemap_out_lines++;
                linemap_push(g_linemap_out_lines, filename, pp_lineno);
                do { size_t _emn=(strlen(expanded)); while(len+_emn+2>cap){cap*=2;out=(char*)realloc(out,cap);} memcpy(out+len,(expanded),_emn); len+=_emn; } while(0);
                do { size_t _emn=(1); while(len+_emn+2>cap){cap*=2;out=(char*)realloc(out,cap);} memcpy(out+len,("\n"),_emn); len+=_emn; } while(0);
                free(expanded);
            }
            /* Re-scan the (possibly joined) text from scratch to find the
             * authoritative end-of-line comment state — simpler and less
             * bug-prone than threading partial states through the join loop
             * above, which only tracked them well enough to know when to
             * stop pulling more lines in. */
            {
                int new_cstate = pp_line_end_comment_state(to_expand, line_starts_in_comment ? 3 : 0);
                if (!line_starts_in_comment && new_cstate) st->in_block_comment_from_directive = 0; /* freshly opened on a regular line */
                if (!new_cstate) st->in_block_comment_from_directive = 0; /* closed */
                st->in_block_comment = new_cstate;
            }
            if (joined) free(joined);
            continue;
        }
        {
            int new_cstate = pp_line_end_comment_state(line, line_starts_in_comment ? 3 : 0);
            if (!line_starts_in_comment && new_cstate) st->in_block_comment_from_directive = 1; /* freshly opened on a directive line */
            if (!new_cstate) st->in_block_comment_from_directive = 0; /* closed */
            st->in_block_comment = new_cstate;
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
                pp_strip_line_comments_from(body, 0);
                /* Register function-like macro. Unlike pp_macro_define()
                 * (object-like macros), this path had no PP_MAX_MACROS
                 * bounds check — once nmc reached the fixed-size macros[]
                 * array's capacity, "idx2=st->nmc++" walked straight past
                 * the end of it and wrote out of bounds, corrupting
                 * whatever followed in PPState. Combining many large
                 * headers (SDL3's 55 headers between them define more than
                 * 1024 distinct function-like macros) reached this reliably
                 * and crashed deep in unrelated, later processing. */
                int idx2=pp_macro_find(st,nm,nlen);
                int idx2_is_new = (idx2<0);
                if (idx2<0) {
                    if (st->nmc >= PP_MAX_MACROS) continue;
                    idx2=st->nmc++;
                }
                { char *_snd2=malloc(nlen+1); strncpy(_snd2,nm,nlen); _snd2[nlen]='\0'; st->macros[idx2].name=_snd2; }
                if (idx2_is_new) {
                    int slot = pp_hash_find_insert_slot(st, st->macros[idx2].name, nlen);
                    if (slot >= 0) st->hash_slots[slot] = idx2;
                }
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
            pp_strip_line_comments_from(val, 0);
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
            /* Preserve exactly what the user wrote — `incname` itself gets
             * progressively rewritten by the fallback attempts below (flattened
             * to "include/<basename>", then possibly overwritten again with
             * whatever -I path resolved), so by the time every fallback has
             * failed it no longer reflects the real #include text. Needed for
             * a clear final error message (see the "cannot find" case below). */
            char requested_incname[512]; snprintf(requested_incname,sizeof requested_incname,"%s",incname);

            /* For system includes (<stdio.h> etc.), try our built-in include/ dir first */
            char sys_path[600];
            char orig_incname[512];
            int try_orig_after_flatten = 0;
            if (system_inc) {
                /* Strip any leading path: use just the filename */
                const char *base = incname;
                for (const char *cp=incname; *cp; cp++) if(*cp=='/'||*cp=='\\') base=cp+1;
                snprintf(sys_path, sizeof sys_path, "include/%s", base);
                /* Keep the original (possibly with subdirectories, e.g.
                 * <SDL3/SDL_stdinc.h>) so that if the flattened
                 * "include/<basename>" lookup below fails, we can still
                 * resolve it against -I search dirs *preserving* the
                 * subdirectory — otherwise any third-party tree that
                 * angle-includes its own headers by subpath (SDL3, etc.)
                 * could never be found, since the flatten discards the
                 * subdirectory entirely and pp_read_file never tries the
                 * un-flattened path on its own. */
                snprintf(orig_incname, sizeof orig_incname, "%s", incname);
                try_orig_after_flatten = (base != incname); /* had a subdir */
                incname[0] = '\0'; /* signal to use sys_path */
                strncat(incname, sys_path, sizeof(sys_path)-1);
            } else if (incname[0] != '/') {
                /* Quote-form #include "...": per the C standard, resolve
                 * relative to the INCLUDING file's own directory before
                 * falling back to the CWD/-I search chain that pp_read_file
                 * does below. Without this, a header that includes a sibling
                 * header via a relative path (e.g. include/GL/gl_funcs.h
                 * doing #include "gl.h", expecting include/GL/gl.h) only
                 * resolved if the *original* source file's own directory or
                 * an explicit -I happened to also contain that sibling —
                 * otherwise every such nested include silently failed. */
                const char *slash = NULL;
                for (const char *cp=filename; *cp; cp++) if (*cp=='/'||*cp=='\\') slash=cp;
                if (slash) {
                    char rel_path[600];
                    int dirlen = (int)(slash - filename) + 1; /* include the slash */
                    snprintf(rel_path, sizeof rel_path, "%.*s%s", dirlen, filename, incname);
                    FILE *probe = fopen(rel_path, "rb");
                    if (probe) {
                        fclose(probe);
                        snprintf(incname, sizeof incname, "%s", rel_path);
                    }
                }
            }
            char resolved[600];
            char *inc_src = pp_read_file(st, incname, resolved, sizeof resolved);
            if (inc_src) snprintf(incname, sizeof incname, "%s", resolved);
            if (!inc_src && try_orig_after_flatten) {
                inc_src = pp_read_file(st, orig_incname, resolved, sizeof resolved);
                if (inc_src) snprintf(incname, sizeof incname, "%s", resolved);
            }
            /* Quote-form #include of a standard-library-named header (e.g.
             * a project including "math.h" instead of <math.h>, which real
             * C source in the wild does) never got the "fall back to our
             * built-in include/ shim" treatment that angle-form system
             * includes get above — only relative-to-including-file and the
             * -I search chain were tried. Add the same last-resort fallback
             * here so both spellings resolve identically. */
            if (!inc_src && !system_inc) {
                const char *base = incname;
                for (const char *cp=incname; *cp; cp++) if (*cp=='/'||*cp=='\\') base=cp+1;
                char fallback_path[600];
                snprintf(fallback_path, sizeof fallback_path, "include/%s", base);
                inc_src = pp_read_file(st, fallback_path, resolved, sizeof resolved);
                if (inc_src) snprintf(incname, sizeof incname, "%s", resolved);
            }
            if (!inc_src) {
                /* A failed #include used to just print a bare message and
                 * silently `continue` past it — invisible to the final
                 * "compilation failed: N errors" tally (diag_error_count()
                 * only counts diag_emit(DIAG_ERROR, ...) calls), so a
                 * missing header could let a build report success (or a
                 * misleadingly small error count from whatever real
                 * "undefined identifier" errors it happened to cause
                 * downstream) despite this real, primary cause never being
                 * counted at all. This has no merged-line to resolve yet
                 * (preprocessing is still building that buffer right now),
                 * so the real file+line is embedded directly rather than
                 * going through diag_emit's usual linemap_resolve path. */
                diag_emit(DIAG_ERROR, -1, NULL, NULL,
                          "%s:%d: %s%s%s: no such file or directory",
                          filename, pp_lineno,
                          system_inc ? "<" : "\"", requested_incname, system_inc ? ">" : "\"");
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
                 const char **inc_dirs, int n_dirs, int is_linux) {
    /* Reset (not free-and-leave-null!) — the line map itself must survive
     * long after this function returns: parser/codegen diagnostics resolve
     * against it for the entire rest of compilation. Only guards against a
     * hypothetical second preprocess() call in the same process. */
    linemap_reset();
    PPState *st = calloc(1, sizeof(PPState));
    st->inc_dirs = inc_dirs;
    st->n_dirs   = n_dirs;
    /* calloc() zero-fills, but 0 is a valid macros[] index — every slot
     * must start as PP_HASH_EMPTY (-1), not 0, or slot 0 looks permanently
     * "occupied" and every other slot looks "empty" (breaking probing) from
     * the very first insert. */
    for (int _hi = 0; _hi < PP_HASH_SIZE; _hi++) st->hash_slots[_hi] = PP_HASH_EMPTY;

    /* Target platform macro — lets headers branch on _WIN32 / __linux__ the
     * same way they would with a real compiler (e.g. gl_funcs.h picks WGL
     * vs GLX this way). Without this, a -windows build still took the
     * #else (GLX/Linux) branch since _WIN32 was simply never defined. */
    if (is_linux && g_squash_macos_target) {
        /* -macos comes through here with is_linux=1 (macOS is a SysV Unix
         * as far as every ABI decision goes — see compiler.c's is_macos
         * comment), but it must NOT define __linux__: real third-party
         * headers use that to pick genuinely Linux-only APIs. __unix__ is
         * the marker squash's own bundled headers key off for the
         * SysV-vs-Win32 split (see include/stdarg.h, include/time.h). */
        pp_macro_define(st,"__APPLE__",9,"1");
        pp_macro_define(st,"__MACH__",8,"1");
        pp_macro_define(st,"__unix__",8,"1");
    } else if (is_linux && g_squash_openbsd_target) {
        /* -openbsd, same is_linux=1 rider as -macos above — OpenBSD is also
         * a SysV/LP64 Unix for every ABI purpose, it just must not define
         * __linux__ (headers use that for genuinely Linux-only APIs) or
         * __APPLE__/__MACH__. */
        pp_macro_define(st,"__OpenBSD__",11,"1");
        pp_macro_define(st,"__unix__",8,"1");
    } else if (is_linux) {
        pp_macro_define(st,"__linux__",9,"1");
        pp_macro_define(st,"__unix__",8,"1");
    } else {
        pp_macro_define(st,"_WIN32",6,"1");
        pp_macro_define(st,"_WIN64",6,"1");
        /* Real MSVC x64 architecture macro — real Windows SDK headers guard
         * CPU-specific struct layouts (e.g. winnt.h's CONTEXT, the x86/x64/
         * ARM register-dump struct used throughout exception handling) on
         * this, not _WIN64 (which only means "64-bit pointers", not "which
         * CPU"). Without it, real winnt.h's "#if defined(_M_AMD64)" picks
         * none of its architecture branches at all and CONTEXT/PCONTEXT
         * are never typedef'd, breaking everything downstream that
         * references them. Same imprecision as _WIN64 just above (defined
         * unconditionally for any non-Linux build, not gated on actual
         * target bitness/arch) — this project only ever targets Windows
         * x64 in practice. */
        pp_macro_define(st,"_M_AMD64",8,"100");
        pp_macro_define(st,"_M_X64",6,"100");
    }

    /* squash always bundles its own libc shims (include/stdio.h,
     * include/stdlib.h, etc. — see the file-lookup comment near the top of
     * this file) regardless of target platform, so these "does the
     * platform have this header" feature macros are unconditionally true
     * for squash the way a real hosted libc's autoconf/CMake probe would
     * find them. Without this, build configs that gate a header behind an
     * explicit HAVE_*_H check (e.g. SDL's SDL_build_config_minimal.h,
     * which deliberately leaves HAVE_STDIO_H undefined so freestanding
     * targets don't get libc dragged in) never see the transitive
     * "#include <stdio.h>" that real desktop code — like SDL_cpuinfo.c's
     * Linux cacheline-size fopen() — relies on, so FILE/fopen are still
     * undefined by the time that code is reached. */
    pp_macro_define(st,"HAVE_STDIO_H",12,"1");
    pp_macro_define(st,"HAVE_STDLIB_H",13,"1");
    pp_macro_define(st,"HAVE_STDDEF_H",13,"1");
    pp_macro_define(st,"HAVE_STDARG_H",13,"1");
    pp_macro_define(st,"HAVE_STRING_H",13,"1");

    /* Built-in macros */
    pp_macro_define(st,"NULL",4,"0");
    pp_macro_define(st,"nullptr",7,"((void*)0)");
    pp_macro_define(st,"TRUE",4,"1");
    pp_macro_define(st,"FALSE",5,"0");
    pp_macro_define(st,"true",4,"1");
    pp_macro_define(st,"false",5,"0");
    pp_macro_define(st,"EXIT_SUCCESS",12,"0");
    pp_macro_define(st,"EXIT_FAILURE",12,"1");
    pp_macro_define(st,"stdin", 5,"((void*)0)");
    pp_macro_define(st,"stdout",6,"((void*)1)");
    pp_macro_define(st,"stderr",6,"((void*)2)");
    /* C standard version macros */
    pp_macro_define(st,"__STDC__",8,"1");
    pp_macro_define(st,"__STDC_VERSION__",16,"201710L");
    pp_macro_define(st,"__STDC_HOSTED__",15,"1");
    /* MSVC fixed-width integer type keywords (__int8/16/32/64) — used
     * pervasively throughout real Windows SDK headers (winnt.h alone
     * defines dozens of typedefs like "typedef signed __int64 INT64,
     * *PINT64;"). Rather than teaching the lexer/parser a whole new class
     * of type-specifier keyword (a much bigger change touching the token
     * enum, keyword table, and ParseTypeSpecifier), predefine them as
     * plain object-like macros expanding to the equivalent standard
     * integer type — "signed __int64"/"unsigned __int64" then textually
     * become "signed long long"/"unsigned long long" before parsing ever
     * sees them, which squash already supports. Not defined as real
     * keywords means a program that redefines them via #undef/#define
     * would behave differently than real MSVC, but no real-world header
     * does that. */
    pp_macro_define(st,"__int8",6,"char");
    pp_macro_define(st,"__int16",7,"short");
    pp_macro_define(st,"__int32",7,"int");
    pp_macro_define(st,"__int64",7,"long long");
    /* MSVC inline-hint keywords — same "predefine as a macro to an already-
     * supported real keyword" trick as __int8/16/32/64 above, rather than
     * adding a whole new recognized keyword to the lexer/parser for each. */
    pp_macro_define(st,"__inline",8,"inline");
    pp_macro_define(st,"__forceinline",13,"inline");
    /* __unaligned -- MSVC pointer qualifier (legacy IA64-era "this pointer
     * may not be naturally aligned, generate byte-wise access"), a no-op
     * on x64 either way since squash already only ever generates ordinary
     * (potentially-unaligned-tolerant) load/store instructions. Same
     * "predefine as empty" treatment as the other MSVC-only qualifier
     * keywords above. */
    pp_macro_define(st,"__unaligned",11,"");
    /* __pragma(...) is MSVC's expression-level equivalent of a "#pragma"
     * directive (usable inline within a macro body, unlike "#pragma"
     * itself, which must start a line) -- e.g. every real MSVC CRT
     * header's "__pragma(pack(push, 8))" prologue. squash's preprocessor
     * already silently no-ops real "#pragma" directives (no packing/
     * warning-control support), so treat __pragma(...) the same way for
     * consistency, rather than teaching the parser a whole new expression
     * form it would just discard the effect of anyway. Must be registered
     * as FUNCTION-like (pp_macro_define() only ever creates object-like
     * macros) so the whole "(pack(push, 8))" argument list is consumed and
     * discarded too, not just the bare "__pragma" name — an object-like
     * expansion would leave a stray, unparseable "(pack(push, 8))" behind. */
    pp_macro_define(st,"__pragma",8,"");
    { int _pi = pp_macro_find(st,"__pragma",8);
      if (_pi >= 0) {
          st->macros[_pi].nparams = 1;
          st->macros[_pi].params[0] = my_strdup("x");
          for (int _pj=1; _pj<16; _pj++) st->macros[_pi].params[_pj] = NULL;
      } }
    /* __declspec(...) (dllimport/dllexport/noreturn/align(n)/novtable/...)
     * -- an MSVC attribute qualifier prefixing a declaration. squash
     * already resolves DLL imports/exports its own way (name-based
     * relocation guessing, not attribute-driven) and has no concept of the
     * others, so it's a no-op here too, same "function-like macro
     * expanding to nothing" treatment as __pragma just above. */
    pp_macro_define(st,"__declspec",10,"");
    { int _pi = pp_macro_find(st,"__declspec",10);
      if (_pi >= 0) {
          st->macros[_pi].nparams = 1;
          st->macros[_pi].params[0] = my_strdup("x");
          for (int _pj=1; _pj<16; _pj++) st->macros[_pi].params[_pj] = NULL;
      } }
    /* __func__ is really a C99 implicit local (the enclosing function's
     * name), synthesized by the compiler at each function's start, not a
     * textual preprocessor macro — squash's preprocessor has no notion of
     * "current function" to give it a real per-function value. Defining it
     * as a fixed placeholder string at least makes it a valid expression
     * (previously an undefined identifier, tripping "codegen: undefined") so
     * pervasive real-world macros that reference it (e.g. SDL3's own
     * CHECK_PARAM/SDL_assert) compile and run without crashing; the exact
     * function name in the resulting message just won't be accurate. */
    pp_macro_define(st,"__func__",8,"\"?\"");

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

