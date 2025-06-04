#ifndef TOKEN_H
#define TOKEN_H

#include "Minifier.h"

enum TokenType
{
    AST_Number,
    AST_Variable,
    AST_Function,
    AST_Operator,
    AST_Parenthesis,
    AST_Identifier,
    AST_Assignment,
    AST_Comma,
    AST_Peroid,
    AST_ReturnKeyword,
    AST_WhileKeyword,
    AST_VoidKeyword,
    AST_VarKeyword,
    AST_IntKeyword,
    AST_DoubleKeyword,
    AST_StringKeyword,
    AST_StringLiteral,
    AST_IntLiteral,
    AST_CurleyBrace,
    AST_SemiColon,
    AST_Whitespace,
    AST_EOF
};

enum VarType //TODO: Array types and object types
{
    AST_VarAutomatic,
    AST_Void,
    AST_NullLiteral,
    AST_Object,
    AST_FunctionPointer,
    AST_VoidPointer,
    AST_Int,
    AST_Short,
    AST_Long,
    AST_Int32,
    AST_Int64,
    AST_UInt32,
    AST_UInt64,
    AST_Double,
    AST_Float,
    AST_String,
    AST_Char,
    AST_Matrix,
    AST_Matrix4,
    AST_Matrix3,
    AST_Vector,
    AST_Vector2,
    AST_Vector3,
    AST_Vector4
};

typedef struct token
{
    enum TokenType Type;
    char* Value;
    int Position;
    PreToken_t* PreToken;
} token_t;

token_t* token_new()
{
    token_t* token = (token_t*)malloc(sizeof(token_t));
    return token;
}

void token_init(token_t** token, enum TokenType type, char* value, int position, PreToken_t* preToken)
{
    token_t* t = *token;
    //token_t* t = token_new();
    t->Type = type;
    t->Value = value;
    t->Position = position + 1;
    t->PreToken = preToken;
    (*token) = t;
}

#endif