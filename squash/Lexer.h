#ifndef LEXER_H
#define LEXER_H

#include "List.h"
#include "Token.h"
#include "Minifier.h"

typedef struct Lexer
{
    struct Minifier* minifier;
    char* input;
    int inputLength;

    list_t* preLexer; // List<PreToken>
    int preLexerLength;

    int currentPos;
    struct PreToken* preToken;
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


struct Lexer* lexer_new(struct Minifier* minifier);
void lexer_init(struct Lexer* lexer, char* input, int inputLength, list_t* preTokens); // List<PreToken>
void lexer_advance(struct Lexer* lexer);
int lexer_getposition(struct Lexer* lexer);
void lexer_setposition(struct Lexer* lexer, int newPosition);
struct token* SkipToToken(struct Lexer* lexer, enum TokenType tokenType);
void lexer_predictiveLookaheads(struct Lexer* lexer);
struct token* GetNextToken(struct Lexer* lexer);
char* ParseNumber(struct Lexer* lexer);
char* ParseIdentifier(struct Lexer* lexer);
char* ParseWhitespace(struct Lexer* lexer);

#endif