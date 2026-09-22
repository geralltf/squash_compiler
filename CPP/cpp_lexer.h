#ifndef CPP_LEXER_H
#define CPP_LEXER_H

/* C++ tokenizer — modeled on CS/cs_lexer.h stylistically. A completely
 * separate token set from squash's own C lexer.c/lexer.h and from
 * CS/cs_lexer.c — nothing here is shared with or affects either. */

typedef enum {
    CPP_TOK_EOF,
    CPP_TOK_IDENT,
    CPP_TOK_INT_LIT,
    CPP_TOK_DOUBLE_LIT,
    CPP_TOK_STRING_LIT,
    CPP_TOK_CHAR_LIT,
    CPP_TOK_KEYWORD,
    CPP_TOK_PUNCT,          /* operators/punctuation, incl. "::", "->", "<<", ">>", "&&", "||", etc; exact text in `text` */
    CPP_TOK_PREPROC_LINE,   /* one raw preprocessor line ("#include <...>" etc), text after '#' verbatim */
} CppTokKind;

typedef struct {
    CppTokKind kind;
    int        line;
    char      *text;
    long long  int_value;
    double     double_value;
} CppTok;

typedef struct {
    const char *src;
    int         pos;
    int         len;
    int         line;
} CppLexer;

void    cpp_lexer_init(CppLexer *lx, const char *src);
CppTok *cpp_lexer_next(CppLexer *lx);
void    cpp_tok_free(CppTok *t);
int     cpp_is_keyword(const char *s);

#endif /* CPP_LEXER_H */
