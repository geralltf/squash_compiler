#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"
#include "symtable.h"

/* =========================================================================
 * Parser state
 * ========================================================================= */
typedef struct {
    Lexer    *lex;
    SymTable *sym;
} Parser;

/* =========================================================================
 * API
 * ========================================================================= */
void     parser_init(Parser *p, Lexer *l, SymTable *sym);
ASTNode *parse_program(Parser *p);

/* Public sub-parsers (called by parse_program and recursively) */
ASTNode *ParseFunction  (Parser *p);          /* function definition        */
ASTNode *ParseVariable  (Parser *p);          /* var declaration statement  */
ASTNode *ParseStatement (Parser *p);          /* any statement              */
ASTNode *ParseExpression(Parser *p);          /* full expression (with =)   */

/* Expression sub-parsers implementing precedence climbing */
ASTNode *ParseAssignment(Parser *p);
ASTNode *ParseLogicalOr (Parser *p);
ASTNode *ParseLogicalAnd(Parser *p);
ASTNode *ParseBitOr     (Parser *p);
ASTNode *ParseBitXor    (Parser *p);
ASTNode *ParseBitAnd    (Parser *p);
ASTNode *ParseEquality  (Parser *p);
ASTNode *ParseRelational(Parser *p);
ASTNode *ParseShift     (Parser *p);
ASTNode *ParseAddSub    (Parser *p);
ASTNode *ParseMulDiv    (Parser *p);
ASTNode *ParseUnary     (Parser *p);
ASTNode *ParsePostfix   (Parser *p);
ASTNode *ParsePrimary   (Parser *p);
ASTNode *ParseNumber    (Parser *p);
ASTNode *ParseIdentifier(Parser *p);

#endif /* PARSER_H */
