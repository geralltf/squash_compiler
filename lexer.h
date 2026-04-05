#ifndef LEXER_H
#define LEXER_H
#include <stddef.h>

typedef enum {
    TOK_NUMBER, TOK_FLOAT, TOK_STRING, TOK_CHAR_LIT, TOK_IDENT,
    /* keywords */
    TOK_AUTO, TOK_BREAK, TOK_CASE, TOK_CHAR, TOK_CONST,
    TOK_CONTINUE, TOK_DEFAULT, TOK_DO, TOK_DOUBLE, TOK_ELSE,
    TOK_ENUM, TOK_EXTERN, TOK_FLOAT_KW, TOK_FOR, TOK_GOTO,
    TOK_IF, TOK_INT, TOK_LONG, TOK_REGISTER, TOK_RETURN,
    TOK_SHORT, TOK_SIGNED, TOK_SIZEOF, TOK_INLINE, TOK_STATIC, TOK_STRUCT,
    TOK_SWITCH, TOK_TYPEDEF, TOK_UNION, TOK_UNSIGNED, TOK_VOID,
    TOK_VOLATILE, TOK_WHILE,
    /* punctuation */
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMICOLON, TOK_COLON, TOK_COMMA, TOK_DOT,
    TOK_ARROW, TOK_ELLIPSIS, TOK_HASH, TOK_QUESTION,
    /* operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
    TOK_LSHIFT, TOK_RSHIFT,
    /* compound assignment */
    TOK_PLUS_EQ, TOK_MINUS_EQ, TOK_STAR_EQ, TOK_SLASH_EQ,
    TOK_PERCENT_EQ, TOK_AMP_EQ, TOK_PIPE_EQ, TOK_CARET_EQ,
    TOK_LSHIFT_EQ, TOK_RSHIFT_EQ,
    /* inc/dec */
    TOK_INC, TOK_DEC,
    /* logical */
    TOK_BANG, TOK_AND, TOK_OR,
    /* comparison */
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    /* assignment */
    TOK_ASSIGN,
    TOK_EOF, TOK_ERROR
} TokenKind;

typedef struct {
    TokenKind   kind;
    const char *start;
    int         len;
    long long   ival;
    double      fval;
    char       *sval;    /* processed string/char value, heap-allocated */
    int         line;
    int         col;
} Token;

typedef struct {
    const char *src;
    const char *filename;
    int         pos;
    int         line;
    int         col;
    Token       cur;
} Lexer;

void  lexer_init  (Lexer *l, const char *src, const char *filename);
void  lexer_next        (Lexer *l);
Token lexer_peek        (Lexer *l);
int   lexer_check       (Lexer *l, TokenKind k);
Token lexer_expect      (Lexer *l, TokenKind k);
void  lexer_expect_void (Lexer *l, TokenKind k);

const char *token_kind_name(TokenKind k);
void        token_print    (const Token *t);

/* Preprocessor: expand #define / #include, returns new heap string */
char *preprocess(const char *src, const char *filename,
                 const char **include_dirs, int n_dirs);

/* is this token a type-specifier keyword? */
int tok_is_type(TokenKind k);

char* my_strdup(const char* src);

#endif
