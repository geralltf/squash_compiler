#ifndef PARSER_H
#define PARSER_H
#include "lexer.h"
#include "ast.h"
#include "symtable.h"

typedef struct {
    Lexer    *lex;
    SymTable *sym;
    const char *filename;
    int       error_count;
    int       anon_counter;  /* unique counter for anonymous struct/union names */
    /* Compound-literal support: "(Type){ ... }" is parsed by hoisting a
     * synthetic "Type __cl_N = { ... };" local declaration out to the
     * enclosing statement (see ParseStatement's wrapper in parser_new4.c)
     * and replacing the compound-literal expression itself with a
     * reference to that synthetic variable. Compound literals are parsed
     * deep inside expression parsing, with no direct access to "the
     * statement list currently being built" — this side-channel queue is
     * how the hoisted declaration gets back out to statement level. */
    ASTNode **pending_stmts;
    int       n_pending;
    int       cap_pending;
    int       compound_lit_counter;
    /* Nesting depth for the two recursive-descent recursion points that
     * can blow the real C call stack on hostile/pathological input --
     * "(((((...)))))" (ParsePrimary recursing into ParseExpression on
     * each '(') and "{{{{{...}}}}}" (ParseBlock/ParseStatement recursing
     * on each '{') both crash with a real stack-overflow segfault at
     * ~tens of thousands of levels, found via fuzzing this compiler's
     * own robustness against hostile input. Checked/incremented at just
     * those two recursion points (see parser_new4.c) rather than
     * threaded through the whole precedence-climbing call chain. */
    int       expr_depth;
    int       block_depth;
} Parser;

void     parser_init   (Parser *p, Lexer *l, SymTable *sym, const char *filename);
ASTNode *parse_program (Parser *p);

/* Type parsing */
TypeInfo *ParseTypeSpecifier (Parser *p);  /* parse base type + qualifiers */
TypeInfo *ParseFullType      (Parser *p);  /* type + pointer stars         */

/* Declaration parsers */
ASTNode *ParseFunction  (Parser *p, const char *storage, TypeInfo *ret, const char *name);
ASTNode *ParseVariable  (Parser *p);
ASTNode *ParseStructDecl(Parser *p);
ASTNode *ParseEnumDecl  (Parser *p);
ASTNode *ParseTypedef   (Parser *p);

/* Statement parsers */
ASTNode *ParseStatement (Parser *p);
ASTNode *ParseBlock     (Parser *p);

/* Expression parsers (recursive descent, precedence climbing) */
ASTNode *ParseExpression  (Parser *p);
ASTNode *ParseAssignment  (Parser *p);
ASTNode *ParseTernary     (Parser *p);
ASTNode *ParseLogicalOr   (Parser *p);
ASTNode *ParseLogicalAnd  (Parser *p);
ASTNode *ParseBitOr       (Parser *p);
ASTNode *ParseBitXor      (Parser *p);
ASTNode *ParseBitAnd      (Parser *p);
ASTNode *ParseEquality    (Parser *p);
ASTNode *ParseRelational  (Parser *p);
ASTNode *ParseShift       (Parser *p);
ASTNode *ParseAddSub      (Parser *p);
ASTNode *ParseMulDiv      (Parser *p);
ASTNode *ParseUnary       (Parser *p);
ASTNode *ParsePostfix     (Parser *p);
ASTNode *ParsePrimary     (Parser *p);
ASTNode *ParseNumber      (Parser *p);
ASTNode *ParseIdentifier  (Parser *p);
ASTNode *ParseFunction    (Parser *p, const char *storage, TypeInfo *ret, const char *name);
ASTNode *ParseVariable    (Parser *p);

/* Error helpers */
void parse_error  (Parser *p, const char *msg);
void parse_warn   (Parser *p, const char *msg);

#endif
