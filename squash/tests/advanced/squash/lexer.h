#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

/* =========================================================================
 * Token kinds
 * ========================================================================= */
typedef enum {
    /* Literals */
    TOK_NUMBER,      /* integer literal                 */
    TOK_STRING,      /* "..."                           */
    /* Names */
    TOK_IDENT,       /* identifier                      */
    /* Keywords */
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_RETURN,
    TOK_INT,
    TOK_VOID,
    TOK_CHAR,
    /* Punctuation */
    TOK_LPAREN,      /* (  */
    TOK_RPAREN,      /* )  */
    TOK_LBRACE,      /* {  */
    TOK_RBRACE,      /* }  */
    TOK_SEMICOLON,   /* ;  */
    TOK_COMMA,       /* ,  */
    /* Arithmetic / bitwise operators */
    TOK_PLUS,        /* +  */
    TOK_MINUS,       /* -  */
    TOK_STAR,        /* *  */
    TOK_SLASH,       /* /  */
    TOK_PERCENT,     /* %  */
    TOK_AMP,         /* &  */
    TOK_PIPE,        /* |  */
    TOK_CARET,       /* ^  */
    TOK_TILDE,       /* ~  */
    TOK_LSHIFT,      /* << */
    TOK_RSHIFT,      /* >> */
    /* Logical operators */
    TOK_BANG,        /* !  */
    TOK_AND,         /* && */
    TOK_OR,          /* || */
    /* Comparison operators */
    TOK_EQ,          /* == */
    TOK_NEQ,         /* != */
    TOK_LT,          /* <  */
    TOK_GT,          /* >  */
    TOK_LE,          /* <= */
    TOK_GE,          /* >= */
    /* Assignment */
    TOK_ASSIGN,      /* =  */
    /* Unary prefix operators handled via TOK_MINUS / TOK_BANG / TOK_TILDE */
    TOK_EOF,
    TOK_ERROR
} TokenKind;

/* =========================================================================
 * Token
 * ========================================================================= */
typedef struct {
    TokenKind   kind;
    const char *start;   /* pointer into source string            */
    int         len;     /* byte length of token text             */
    long long   ival;    /* numeric value (TOK_NUMBER)            */
    char       *sval;    /* heap-allocated string (TOK_STRING)    */
    int         line;    /* source line (1-based)                 */
    int         col;     /* source column (1-based)               */
} Token;

/* =========================================================================
 * Lexer state
 * ========================================================================= */
typedef struct {
    const char *src;   /* full source text                        */
    int         pos;   /* current position in src                 */
    int         line;
    int         col;
    Token       cur;   /* current (most recently lexed) token     */
} Lexer;

/* =========================================================================
 * API
 * ========================================================================= */
void  lexer_init  (Lexer *l, const char *src);
Token lexer_next  (Lexer *l);          /* advance and return next token     */
Token lexer_peek  (Lexer *l);          /* return current token (no advance) */
int   lexer_check (Lexer *l, TokenKind k); /* 1 if cur.kind==k             */
Token lexer_expect(Lexer *l, TokenKind k); /* consume or error              */

const char *token_kind_name(TokenKind k);
void        token_print    (const Token *t);

#endif /* LEXER_H */
