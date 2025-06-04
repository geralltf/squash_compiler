#ifndef AST_SQUASH_H
#define AST_SQUASH_H

#include <stdlib.h>
#include <stdbool.h>

#include "List.h"
#include "Symbols.h"

enum ASTNodeType
{
    AST_BIN_OP,
    AST_UNARY_OP,
    AST_Number,
    AST_VariableDefine,
    AST_VariableAssignment,
    AST_VariableDeclaration,
    AST_Variable,
    AST_FunctionCall,
    AST_FunctionDefinition,
    AST_FunctionReturn,
    AST_FunctionArg
};

//typedef struct _AST_s {
//    char* varName;
//    char* operandLeft;
//    char* operandRight;
//    enum OperatorType operatorType;
//    enum OperatorType nextOprType;
//    struct _AST_s* leftChild;
//    struct _AST_s* rightChild;
//    int precedence;
//    int ID;
//} AST;


typedef struct ASTNode
{
    enum ASTNodeType Type;
    char* Value;
    struct ASTNode* Left;
    struct ASTNode* Right;
    int Precedence;

    bool IsFunctionCall;
    FunctionSymbol_t* FunctSymbol;
    list_t* FunctionArguments; // List<ASTNode> FunctionArguments

    bool IsVariable;
    enum VarType VariableType;
    VariableSymbol_t* VarSymbol;

    bool IsFunctionDefinition;
    char* FunctionName;
    list_t* FunctionBody; //List<ASTNode> FunctionBody;
    enum VarType FunctionReturnType;
    char* ArgumentType;
} astnode_t;

char* astnodetype_tostring(enum ASTNodeType type);
char* bool_tostring(bool input);
char* ast_tostring(astnode_t* node);

astnode_t* ast_node_new();

void ast_node_init_bt(astnode_t** node, enum ASTNodeType type, char* value, astnode_t* left, astnode_t* right);
void ast_node_init_bt2(astnode_t** node, enum ASTNodeType type, char* argumentType, char* value, astnode_t* left, astnode_t* right);
void ast_node_init_funct(astnode_t** node, enum ASTNodeType type, char* value, FunctionSymbol_t* functionSymbol, list_t* arguments); // List<ASTNode> arguments
void ast_node_init_var(astnode_t** node, enum ASTNodeType type, char* value, VariableSymbol_t* variableSymbol);

#endif