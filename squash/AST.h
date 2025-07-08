#ifndef AST_SQUASH_H
#define AST_SQUASH_H

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "List.h"
#include "Symbols.h"
#include "sb.h"

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

enum ASTNodeValueType
{
    AST_VALUE_INT = 4,
    AST_VALUE_DOUBLE = 3,
    AST_VALUE_FLOAT = 2,
    AST_VALUE_STRING = 1,
    AST_VALUE_UNDEFINED = 0
};

typedef struct ASTNode
{
    enum ASTNodeType Type;

    char* Value;
    enum ASTNodeValueType ValueType;
    int ValueInt;
    float ValueFloat;
    double ValueDouble;

    struct ASTNode* Left;
    struct ASTNode* Right;
    int Precedence;

    bool IsFunctionCall;
    struct FunctionSymbol* FunctSymbol;
    list_t* FunctionArguments; // List<ASTNode> FunctionArguments

    bool IsVariable;
    enum VarType VariableType;
    struct VariableSymbol* VarSymbol;

    bool IsFunctionDefinition;
    char* FunctionName;
    list_t* FunctionBody; //List<ASTNode> FunctionBody;
    enum VarType FunctionReturnType;
    char* ArgumentType;
} astnode_t;

char* astnodetype_tostring(enum ASTNodeType type);
char* bool_tostring(bool input);
char* ast_tostring(struct ASTNode* node);

struct ASTNode* ast_node_new();

void ast_node_init_bt(struct ASTNode** node, enum ASTNodeType type, char* value, enum ASTNodeValueType value_type, struct ASTNode* left, struct ASTNode* right);
void ast_node_init_bt2(struct ASTNode** node, enum ASTNodeType type, char* argumentType, char* value, enum ASTNodeValueType value_type, struct ASTNode* left, struct ASTNode* right);
void ast_node_init_funct(struct ASTNode** node, enum ASTNodeType type, char* value, struct FunctionSymbol* functionSymbol, list_t* arguments); // List<ASTNode> arguments
void ast_node_init_var(struct ASTNode** node, enum ASTNodeType type, char* value, enum ASTNodeValueType value_type, struct VariableSymbol* variableSymbol);

#endif