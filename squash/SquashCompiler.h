#ifndef SQUASH_COMPILER_H
#define SQUASH_COMPILER_H

#include "Logger.h"
#include "Lexer.h"
#include "Token.h"
#include "Symbols.h"
#include "AST.h"
#include "Assembler.h"
#include "Optimiser.h"

#include <errno.h>
#include <math.h>
#include <string.h>

struct SquashCompiler
{
    struct Lexer* lexer;
    struct token* currentToken;
    struct SymbolTable* symbolTable;
    //AbstractSyntaxTree* rootAST;
    struct Assembler* asm0;
};

struct SquashCompiler* squash_compiler_new();
void squash_compiler_init(struct SquashCompiler* squash_compiler, char* input, int inputLength);
void CompileExpression(struct SquashCompiler* squash_compiler, char* output_file_name, char* output_binary_file_name, bool enable_tracing);
astnode_t* ParseStatements(struct SquashCompiler* squash_compiler);
astnode_t* ParseVariableDeclaration(struct SquashCompiler* squash_compiler, enum VarType varType);
astnode_t* ParseVariableDefine(struct SquashCompiler* squash_compiler, enum VarType varType);
astnode_t* parseAssignmentOperator(struct SquashCompiler* squash_compiler, enum VarType varType);
void parseEndStatement(struct SquashCompiler* squash_compiler, astnode_t* varDefineNode);
bool tryParseFloat(const char* str, float* result);

astnode_t* returnkeyword_parse(struct SquashCompiler* squash_compiler);
astnode_t* varkeyword_parse(struct SquashCompiler* squash_compiler);
astnode_t* doublekeyword_parse(struct SquashCompiler* squash_compiler, int* retFlag);
astnode_t* identifier_parse(struct SquashCompiler* squash_compiler, bool rememberLocation, int pos, int* retFlag);
astnode_t* number_parse(struct SquashCompiler* squash_compiler);
astnode_t* int_define_parse(struct SquashCompiler* squash_compiler, int pos, int* retFlag);
astnode_t* int_define2_parse(struct SquashCompiler* squash_compiler);
astnode_t* intkeyword_parse(struct SquashCompiler* squash_compiler, int* retFlag);
astnode_t* stringkeyword_parse(struct SquashCompiler* squash_compiler, int* retFlag);
astnode_t* voidkeyword_parse(struct SquashCompiler* squash_compiler, int* retFlag);
astnode_t* numberentry_parse(struct SquashCompiler* squash_compiler, token_t* token);
astnode_t* identifier2_parse(token_t* token, struct SquashCompiler* squash_compiler);
astnode_t* parenthesis_parse(struct SquashCompiler* squash_compiler);
void variable_parse(struct SquashCompiler* squash_compiler);
void whitespace_parse(struct SquashCompiler* squash_compiler);
void semicolon_parse(struct SquashCompiler* squash_compiler);

astnode_t* ParsePrimaryExpression(struct SquashCompiler* squash_compiler);
void ParseEndOfFunction(struct SquashCompiler* squash_compiler);
astnode_t* ParseEntryPoint(struct SquashCompiler* squash_compiler, char* functIdentifierName);
astnode_t* ParseFunctionDefinition(struct SquashCompiler* squash_compiler, enum VarType retVarType, char* functIdentifierName);

void ParseFunctionArg(struct SquashCompiler* squash_compiler, list_t* args, char* argType); // ref List<ASTNode> args
list_t* ParseFunctionArgs(struct SquashCompiler* squash_compiler, enum VarType retVarType); // returns: List<ASTNode>
list_t* ParseFunctionDefArguments(struct SquashCompiler* squash_compiler, enum VarType retVarType); // returns: List<ASTNode>
list_t* ParseFunctionArguments(struct SquashCompiler* squash_compiler); // returns: List<ASTNode>
astnode_t* ParseExpression(struct SquashCompiler* squash_compiler, int precedence);
int GetPrecedence(char* op);
bool IsUnaryOperator(struct SquashCompiler* squash_compiler);
bool IsBinaryOperator(struct SquashCompiler* squash_compiler);

#endif