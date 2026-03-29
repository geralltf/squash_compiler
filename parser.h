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
