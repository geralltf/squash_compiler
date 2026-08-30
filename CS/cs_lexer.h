#ifndef CS_LEXER_H
#define CS_LEXER_H

/* C# tokenizer — modeled on squash's own lexer.c/lexer.h stylistically,
 * but a completely separate token set (C# has its own keyword list,
 * operators like `??`/`?.`/`=>`, verbatim/interpolated string forms,
 * etc. — nothing here is shared with or affects squash's own C lexer). */

typedef enum {
    CS_TOK_EOF,
    CS_TOK_IDENT,
    CS_TOK_INT_LIT,
    CS_TOK_DOUBLE_LIT,
    CS_TOK_STRING_LIT,
    CS_TOK_INTERP_STRING_LIT,  /* raw text between $" and "; cs_parser.c re-lexes {expr} spans itself */
    CS_TOK_CHAR_LIT,
    CS_TOK_KEYWORD,
    CS_TOK_PUNCT,              /* operators/punctuation; exact text in `text` */
} CsTokKind;

typedef struct {
    CsTokKind kind;
    int       line;
    char     *text;      /* identifier name, keyword text, punctuator text, or raw string body */
    long long int_value;
    double    double_value;
} CsTok;

typedef struct {
    const char *src;
    int         pos;
    int         len;
    int         line;
} CsLexer;

void  cs_lexer_init(CsLexer *lx, const char *src);
/* Returns a freshly-allocated token (caller frees via cs_tok_free); never
 * returns NULL — CS_TOK_EOF is returned at end of input, repeatedly if
 * called again. */
CsTok *cs_lexer_next(CsLexer *lx);
void   cs_tok_free(CsTok *t);

/* True if `s` is one of C#'s reserved keywords covered by this frontend's
 * grammar (cs_parser.c's own header comment lists the full grammar scope;
 * this list is exactly the keyword SPELLINGS the lexer needs to recognize
 * as CS_TOK_KEYWORD rather than CS_TOK_IDENT — a keyword outside this
 * grammar's scope, e.g. "async"/"yield"/"unsafe", is deliberately left as
 * a plain identifier so a script using them fails with a normal parse
 * error at the point of use rather than a mysterious lexer rejection). */
int cs_is_keyword(const char *s);

#endif /* CS_LEXER_H */
