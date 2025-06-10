#include "Token.h"

token_t* token_new()
{
    token_t* token = (token_t*)malloc(sizeof(token_t));
    token->Position = 0;
    token->PreToken = NULL;
    token->Type = AST_UNDEFINED;
    token->Value = NULL;
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