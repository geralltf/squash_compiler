#include "Lexer.h"

struct Lexer* lexer_new(Minifier_t* minifier)
{
    struct Lexer* lex = (struct Lexer*)malloc(sizeof(struct Lexer));
    lex->minifier = minifier;
    lex->currentChar = '\0';
    lex->currentChar1 = '\0';
    lex->currentChar2 = '\0';
    lex->currentChar3 = '\0';
    lex->currentChar4 = '\0';
    lex->currentChar5 = '\0';
    lex->currentChar6 = '\0';
    lex->currentChar7 = '\0';
    lex->currentChar8 = '\0';
    lex->currentChar9 = '\0';
    return lex;
}

void lexer_init(struct Lexer* lexer, char* input, int inputLength, list_t* preTokens) // List<PreToken> preTokens
{
    lexer->input = input;
    lexer->inputLength = inputLength;
    lexer->preLexer = preTokens;
    lexer->currentPos = 0;
    if (inputLength > 0)
    {
        lexer->currentChar = input[lexer->currentPos];
    }
}

void lexer_lexer_advance(struct Lexer* lexer)
{
    lexer->currentPos++;
    if (lexer->currentPos < lexer->inputLength)
    {
        lexer->currentChar = lexer->input[lexer->currentPos];
        //lexer->preToken = lexer->preLexer[lexer->currentPos % lexer->preLexerLength];
    }
    else
    {
        lexer->currentChar = '\0';
        lexer->preToken = NULL;
    }
}
int lexer_getposition(struct Lexer* lexer)
{
    return lexer->currentPos;
}

void lexer_setposition(struct Lexer* lexer, int newPosition)
{
    lexer->currentPos = newPosition;
}

struct token* SkipToToken(struct Lexer* lexer, enum TokenType tokenType)
{
    token_t* currentToken = NULL;
    currentToken = GetNextToken(lexer);
    while (currentToken != NULL)
    {
        if (currentToken->Type == tokenType)
        {
            break;
        }
        else if (currentToken->Type == AST_EOF)
        {
            currentToken = NULL;
            break;
        }
        //lexer_advance();
        currentToken = GetNextToken(lexer);
    }
    return currentToken;
}

void lexer_predictiveLookaheads(struct Lexer* lexer)
{
    // Lookaheads for predictive parsing.
    if (lexer->currentPos + 1 < lexer->inputLength)
    {
        lexer->currentChar1 = lexer->input[lexer->currentPos + 1];
    }
    else
    {
        lexer->currentChar1 = '\0';
    }
    if (lexer->currentPos + 2 < lexer->inputLength)
    {
        lexer->currentChar2 = lexer->input[lexer->currentPos + 2];
    }
    else
    {
        lexer->currentChar2 = '\0';
    }
    if (lexer->currentPos + 3 < lexer->inputLength)
    {
        lexer->currentChar3 = lexer->input[lexer->currentPos + 3];
    }
    else
    {
        lexer->currentChar3 = '\0';
    }
    if (lexer->currentPos + 4 < lexer->inputLength)
    {
        lexer->currentChar4 = lexer->input[lexer->currentPos + 4];
    }
    else
    {
        lexer->currentChar4 = '\0';
    }
    if (lexer->currentPos + 5 < lexer->inputLength)
    {
        lexer->currentChar5 = lexer->input[lexer->currentPos + 5];
    }
    else
    {
        lexer->currentChar5 = '\0';
    }
    if (lexer->currentPos + 6 < lexer->inputLength)
    {
        lexer->currentChar6 = lexer->input[lexer->currentPos + 6];
    }
    else
    {
        lexer->currentChar6 = '\0';
    }
    if (lexer->currentPos + 7 < lexer->inputLength)
    {
        lexer->currentChar7 = lexer->input[lexer->currentPos + 7];
    }
    else
    {
        lexer->currentChar7 = '\0';
    }
    if (lexer->currentPos + 8 < lexer->inputLength)
    {
        lexer->currentChar8 = lexer->input[lexer->currentPos + 8];
    }
    else
    {
        lexer->currentChar8 = '\0';
    }
    if (lexer->currentPos + 9 < lexer->inputLength)
    {
        lexer->currentChar9 = lexer->input[lexer->currentPos + 9];
    }
    else
    {
        lexer->currentChar9 = '\0';
    }
}
struct token* GetNextToken(struct Lexer* lexer) // Lexer.
{
    if (lexer->currentPos < lexer->inputLength)
    {
        lexer->currentChar = lexer->input[lexer->currentPos];
        //lexer->preToken = lexer->preLexer[lexer->currentPos % preLexer.Count];
    }
    else
    {
        lexer->currentChar = '\0';
        lexer->preToken = NULL;
    }

    while (lexer->currentChar != '\0')
    {
        lexer_predictiveLookaheads(lexer);

        if (lexer->currentPos >= lexer->inputLength)
        {
            char* tok_value = (char*)malloc(sizeof(char) * 1);
            tok_value[0] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_EOF, tok_value, lexer->currentPos, NULL); // preToken

            return token;
        }
        if (lexer->currentChar == 'v' && lexer->currentChar1 == 'a' && lexer->currentChar2 == 'r')
        {
            char* tok_value = (char*)malloc(sizeof(char) * 4);
            tok_value[0] = lexer->currentChar;
            tok_value[1] = lexer->currentChar1;
            tok_value[2] = lexer->currentChar2;
            tok_value[3] = '\0';
            token_t* token = token_new();
            token_init(&token, AST_VarKeyword, tok_value, lexer->currentPos, NULL);

            lexer_advance(lexer);
            lexer_advance(lexer);
            lexer_advance(lexer);
            return token;
        }
        if (lexer->currentChar == 'v' && lexer->currentChar1 == 'o' && lexer->currentChar2 == 'i' && lexer->currentChar3 == 'd')
        {
            char* tok_value = (char*)malloc(sizeof(char) * 5);
            tok_value[0] = lexer->currentChar;
            tok_value[1] = lexer->currentChar1;
            tok_value[2] = lexer->currentChar2;
            tok_value[3] = lexer->currentChar3;
            tok_value[4] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_VoidKeyword, tok_value, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            return token;
        }
        if (lexer->currentChar == 'i' && lexer->currentChar1 == 'n' && lexer->currentChar2 == 't')
        {
            char* tok_value = (char*)malloc(sizeof(char) * 4);
            tok_value[0] = lexer->currentChar;
            tok_value[1] = lexer->currentChar1;
            tok_value[2] = lexer->currentChar2;
            tok_value[3] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_IntKeyword, tok_value, lexer->currentPos, NULL);

            //Token token = new Token(TokenType.IntKeyword,
            //    currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString(),
            //    currentPos,
            //    preToken
            //);
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            return token;
        }
        if (lexer->currentChar == 's' && lexer->currentChar1 == 't' && lexer->currentChar2 == 'r' && lexer->currentChar3 == 'i' && lexer->currentChar4 == 'n' && lexer->currentChar5 == 'g')
        {
            char* tok_value = (char*)malloc(sizeof(char) * 7);
            tok_value[0] = lexer->currentChar;
            tok_value[1] = lexer->currentChar1;
            tok_value[2] = lexer->currentChar2;
            tok_value[3] = lexer->currentChar3;
            tok_value[4] = lexer->currentChar4;
            tok_value[5] = lexer->currentChar5;
            tok_value[6] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_StringKeyword, tok_value, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            return token;
        }
        if (lexer->currentChar == 'd' && lexer->currentChar1 == 'o' && lexer->currentChar2 == 'u' && lexer->currentChar3 == 'b' && lexer->currentChar4 == 'l' && lexer->currentChar5 == 'e')
        {
            char* tok_value = (char*)malloc(sizeof(char) * 7);
            tok_value[0] = lexer->currentChar;
            tok_value[1] = lexer->currentChar1;
            tok_value[2] = lexer->currentChar2;
            tok_value[3] = lexer->currentChar3;
            tok_value[4] = lexer->currentChar4;
            tok_value[5] = lexer->currentChar5;
            tok_value[6] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_DoubleKeyword, tok_value, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            return token;
        }
        if (lexer->currentChar == 'w' && lexer->currentChar1 == 'h' && lexer->currentChar2 == 'i' && lexer->currentChar3 == 'l' && lexer->currentChar4 == 'e')
        {
            char* tok_value = (char*)malloc(sizeof(char) * 6);
            tok_value[0] = lexer->currentChar;
            tok_value[1] = lexer->currentChar1;
            tok_value[2] = lexer->currentChar2;
            tok_value[3] = lexer->currentChar3;
            tok_value[4] = lexer->currentChar4;
            tok_value[5] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_WhileKeyword, tok_value, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            return token;
        }
        if (lexer->currentChar == 'r' && lexer->currentChar1 == 'e' && lexer->currentChar2 == 't' && lexer->currentChar3 == 'u' && lexer->currentChar4 == 'r' && lexer->currentChar5 == 'n')
        {
            char* tok_value = (char*)malloc(sizeof(char) * 7);
            tok_value[0] = lexer->currentChar;
            tok_value[1] = lexer->currentChar1;
            tok_value[2] = lexer->currentChar2;
            tok_value[3] = lexer->currentChar3;
            tok_value[4] = lexer->currentChar4;
            tok_value[5] = lexer->currentChar5;
            tok_value[6] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_ReturnKeyword, tok_value, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            lexer_advance(lexer);;
            return token;
        }
        else if (isdigit(lexer->currentChar))
        {
            token_t* token = token_new();
            token_init(&token, AST_NumberEntry, ParseNumber(lexer), lexer->currentPos, NULL);

            return token;
        }
        else if (isalpha(lexer->currentChar))
        {
            token_t* token = token_new();
            token_init(&token, AST_Identifier, ParseIdentifier(lexer), lexer->currentPos, NULL);

            return token;
        }
        else if (lexer->currentChar == '+' || lexer->currentChar == '-' || lexer->currentChar == '*' || lexer->currentChar == '/' || lexer->currentChar == ',')
        {
            char* tok_value = (char*)malloc(sizeof(char) * 2);
            tok_value[0] = lexer->currentChar;
            tok_value[1] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_Operator, tok_value, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            return token;
        }
        else if (lexer->currentChar == '>' || lexer->currentChar == '<' || lexer->currentChar1 == '=')
        {
            char* operators = NULL;
            if ((lexer->currentChar == '>' || lexer->currentChar == '<') && lexer->currentChar1 != '=')
            {
                operators = (char*)malloc(sizeof(char));
                operators[0] = lexer->currentChar;

                lexer_advance(lexer);;
            }
            else if (lexer->currentChar == '>' && lexer->currentChar1 == '=')
            {
                operators = (char*)malloc(sizeof(char) * 2);
                operators[0] = lexer->currentChar;
                operators[1] = lexer->currentChar1;

                lexer_advance(lexer);;
                lexer_advance(lexer);;
            }
            else if (lexer->currentChar == '<' && lexer->currentChar1 == '=')
            {
                operators = (char*)malloc(sizeof(char) * 2);
                operators[0] = lexer->currentChar;
                operators[1] = lexer->currentChar1;

                lexer_advance(lexer);;
                lexer_advance(lexer);;
            }
            else
            {
                operators = (char*)malloc(sizeof(char));
                operators[0] = lexer->currentChar;

                lexer_advance(lexer);;
            }

            token_t* token = token_new();
            token_init(&token, AST_Operator, operators, lexer->currentPos, NULL);

            return token;
        }
        else if (lexer->currentChar == '(' || lexer->currentChar == ')') // Handle parentheses
        {
            char* parenthesis = (char*)malloc(sizeof(char) * 2);
            parenthesis[0] = lexer->currentChar;
            parenthesis[1] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_Parenthesis, parenthesis, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            return token;
        }
        else if (lexer->currentChar == '=')
        {
            char* assignmentOperator = (char*)malloc(sizeof(char) * 2);
            assignmentOperator[0] = lexer->currentChar;
            assignmentOperator[1] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_Assignment, assignmentOperator, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            return token;
        }
        else if (lexer->currentChar == '.')
        {
            char* peroidOperator = (char*)malloc(sizeof(char) * 2);
            peroidOperator[0] = lexer->currentChar;
            peroidOperator[1] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_Peroid, peroidOperator, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            return token;
        }
        else if (lexer->currentChar == ';')
        {
            char* semiColonOperator = (char*)malloc(sizeof(char) * 2);
            semiColonOperator[0] = lexer->currentChar;
            semiColonOperator[1] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_SemiColon, semiColonOperator, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            return token;
        }
        else if (lexer->currentChar == '{')
        {
            char* curleyBraceOperator = (char*)malloc(sizeof(char) * 2);
            curleyBraceOperator[0] = lexer->currentChar;
            curleyBraceOperator[1] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_CurleyBrace, curleyBraceOperator, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            return token;
        }
        else if (lexer->currentChar == '}')
        {
            char* curleyBraceOperator = (char*)malloc(sizeof(char) * 2);
            curleyBraceOperator[0] = lexer->currentChar;
            curleyBraceOperator[1] = '\0';

            token_t* token = token_new();
            token_init(&token, AST_CurleyBrace, curleyBraceOperator, lexer->currentPos, NULL);

            lexer_advance(lexer);;
            return token;
        }
        else if (isspace(lexer->currentChar) || lexer->currentChar == ' ' || lexer->currentChar == '\t' || lexer->currentChar == '\n' || lexer->currentChar == '\r')
        {
            token_t* token = token_new();
            token_init(&token, AST_CurleyBrace, ParseWhitespace(lexer), lexer->currentPos, NULL);

            return token;
        }
        else
        {
            //Logger.Log.LogError("Invalid character found in input '" + currentChar.ToString() + "' Current position: " + GetPosition().ToString()
            //    + " offsetX: " + ((preToken != null) ? preToken.OffsetX.ToString() : "is null")
            //    + " offsetY: " + ((preToken != null) ? preToken.OffsetY.ToString() : "is null")
            //);

            //throw new Exception("Invalid character found in input. '" + currentChar.ToString() + "' Current position: " + GetPosition().ToString()
            //    + " offsetX: " + ((preToken != null) ? preToken.OffsetX.ToString() : "is null")
            //    + " offsetY: " + ((preToken != null) ? preToken.OffsetY.ToString() : "is null")
            //);
        }
    }

    return NULL;
}

char* ParseNumber(struct Lexer* lexer)
{
    list_t* lst_builder = list_new();
    list_t* n = NULL;
    int count = 0;
    char* buffer = NULL;
    int index = 0;

    while (lexer->currentChar != '\0' && (isdigit(lexer->currentChar) || lexer->currentChar == '.' || lexer->currentChar == 'f'))
    {
        char* c = &lexer->currentChar;
        list_enqueue(lst_builder, (void*)c);

        lexer_advance(lexer);;
    }

    n = lst_builder;
    count = 0;

    while (n != NULL)
    {
        count++;

        n = n->next;
    }

    n = lst_builder;
    buffer = (char*)malloc(sizeof(char) * (count + 1));
    index = 0;
    buffer[count] = '\0';
    char* pC;

    while (n != NULL)
    {
        pC = (char*)n->data;
        buffer[index] = *pC;
        index++;

        n = n->next;
    }

    return buffer;
}

char* ParseIdentifier(struct Lexer* lexer)
{
    list_t* lst_builder = list_new();
    list_t* n = NULL;
    int count = 0;
    char* buffer = NULL;
    int index = 0;

    while (lexer->currentChar != '\0' && ((isdigit(lexer->currentChar) || isalpha(lexer->currentChar)) || lexer->currentChar == '_'))
    {
        char* c = &lexer->currentChar;
        list_enqueue(lst_builder, (void*)c);

        lexer_advance(lexer);;
    }

    n = lst_builder;
    count = 0;

    while (n != NULL)
    {
        count++;

        n = n->next;
    }

    n = lst_builder;
    buffer = (char*)malloc(sizeof(char) * (count + 1));
    index = 0;
    buffer[count] = '\0';
    char* pC;

    while (n != NULL)
    {
        pC = (char*)n->data;
        buffer[index] = *pC;
        index++;

        n = n->next;
    }

    return buffer;
}

char* ParseWhitespace(struct Lexer* lexer)
{
    list_t* lst_builder = list_new();
    list_t* n = NULL;
    int count = 0;
    char* buffer = NULL;
    int index = 0;

    while (lexer->currentChar != '\0' && (isspace(lexer->currentChar) || lexer->currentChar == ' ' || lexer->currentChar == '\t' || lexer->currentChar == '\n' || lexer->currentChar == '\r'))
    {
        char* c = &lexer->currentChar;
        list_enqueue(lst_builder, (void*)c);

        lexer_advance(lexer);;
    }

    n = lst_builder;
    count = 0;

    while (n != NULL)
    {
        count++;

        n = n->next;
    }

    n = lst_builder;
    buffer = (char*)malloc(sizeof(char) * (count + 1));
    index = 0;
    buffer[count] = '\0';
    char* pC;

    while (n != NULL)
    {
        pC = (char*)n->data;
        buffer[index] = *pC;
        index++;

        n = n->next;
    }

    return buffer;
}
