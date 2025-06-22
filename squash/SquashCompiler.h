#ifndef SQUASH_COMPILER_H
#define SQUASH_COMPILER_H

#include "Logger.h"
#include "Lexer.h"
#include "Token.h"
#include "Symbols.h"
#include "AST.h"
#include "Assembler.h"

#include <errno.h>
#include <math.h>
#include <string.h>

typedef struct SquashCompiler
{
    lexer_t* lexer;
    token_t* currentToken;
    SymbolTable_t* symbolTable;
    //AbstractSyntaxTree* rootAST;
    assembler_t* asm0;
} squash_compiler_t;

squash_compiler_t* squash_compiler_new();
void squash_compiler_init(squash_compiler_t* squash_compiler, char* input, int inputLength);
void CompileExpression(squash_compiler_t* squash_compiler);
astnode_t* ParseStatements(squash_compiler_t* squash_compiler);
astnode_t* ParseVariableDeclaration(squash_compiler_t* squash_compiler, enum VarType varType);
astnode_t* ParseVariableDefine(squash_compiler_t* squash_compiler, enum VarType varType);
astnode_t* parseAssignmentOperator(squash_compiler_t* squash_compiler, enum VarType varType);
void parseEndStatement(squash_compiler_t* squash_compiler, astnode_t* varDefineNode);
bool tryParseFloat(const char* str, float* result);

astnode_t* ParsePrimaryExpression(squash_compiler_t* squash_compiler);
void ParseEndOfFunction(squash_compiler_t* squash_compiler);
astnode_t* ParseEntryPoint(squash_compiler_t* squash_compiler, char* functIdentifierName);
astnode_t* ParseFunctionDefinition(squash_compiler_t* squash_compiler, enum VarType retVarType, char* functIdentifierName);

void ParseFunctionArg(squash_compiler_t* squash_compiler, list_t* args, char* argType); // ref List<ASTNode> args
list_t* ParseFunctionArgs(squash_compiler_t* squash_compiler, enum VarType retVarType); // returns: List<ASTNode>
list_t* ParseFunctionDefArguments(squash_compiler_t* squash_compiler, enum VarType retVarType); // returns: List<ASTNode>
list_t* ParseFunctionArguments(squash_compiler_t* squash_compiler); // returns: List<ASTNode>
astnode_t* ParseExpression(squash_compiler_t* squash_compiler, int precedence);
int GetPrecedence(char* op);
bool IsUnaryOperator(squash_compiler_t* squash_compiler);
bool IsBinaryOperator(squash_compiler_t* squash_compiler);

#endif