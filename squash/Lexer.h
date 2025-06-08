#ifndef LEXER_H
#define LEXER_H

#include "List.h"
#include "Token.h"
#include "Minifier.h"

typedef struct Lexer
{
    Minifier_t* minifier;
    char* input;
    int inputLength;

    list_t* preLexer; // List<PreToken>
    int preLexerLength;

    int currentPos;
    PreToken_t* preToken;
    char currentChar;
    char currentChar1;
    char currentChar2;
    char currentChar3;
    char currentChar4;
    char currentChar5;
    char currentChar6;
    char currentChar7;
    char currentChar8;
    char currentChar9;
} lexer_t;


lexer_t* lexer_new(Minifier_t* minifier);
void lexer_init(lexer_t* lexer, char* input, int inputLength, list_t* preTokens); // List<PreToken>
void lexer_advance(lexer_t* lexer);
int lexer_getposition(lexer_t* lexer);
void lexer_setposition(lexer_t* lexer, int newPosition);
token_t* SkipToToken(lexer_t* lexer, enum TokenType tokenType);
void lexer_predictiveLookaheads(lexer_t* lexer);
token_t* GetNextToken(lexer_t* lexer);
char* ParseNumber(lexer_t* lexer);
char* ParseIdentifier(lexer_t* lexer);
char* ParseWhitespace(lexer_t* lexer);

#endif