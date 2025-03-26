#include "AST.h" 

void tokenlst_init(token_list_t** tlst)
{
    (*tlst) = (token_list_t*)malloc(sizeof(token_list_t));
    (*tlst)->next = NULL;
    (*tlst)->prev = NULL;
    (*tlst)->token = AST_UNDEFINED;
}

void idlst_init(id_list_t* item, int id)
{
    item->next = NULL;
    item->id = id;
}

void idlst_add(id_list_t* parent, int id)
{
    if (parent != NULL)
    {
        id_list_t* child = (id_list_t*)malloc(sizeof(id_list_t));
        if (child != NULL)
        {
            child->next = NULL;
            child->id = id;
            parent->next = child;
        }
    }
}

bool idlst_exists(id_list_t* front, int id)
{
    id_list_t* start = front;
    while (start != NULL)
    {
        if (start->id == id)
        {
            return true;
        }
        start = start->next;
    }
    return false;
}

void ast_init(AST** ast)
{
    (*ast) = (AST*)malloc(sizeof(AST));
    (*ast)->ID = ++((*ast)->prevID);
}

char* reverse(char* source)
{
    int l = strlen(source);
    char* r = (char*)malloc((l + 1) * sizeof(char));
    r[l] = '\0';
    int i;
    for(i = 0; i < l; i++) {
      r[i] = source[l - 1 - i];
    }
    return r; 
}


/// <summary>
/// Finds a variable name given found assignment operator '-' starting index.
/// </summary>
/// <param name="startingIndex"></param>
/// <param name="lexer"></param>
/// <returns></returns>
char* FindVarName(int startingIndex, token_list_t* lexer)
{
    int lst_size = 0;
    char_list_t* lst_next = NULL;
    char_list_t* lst = (char_list_t*)malloc(sizeof(char_list_t));
    char_list_t* lst_first = lst;

    //char* varName = (char*)malloc(sizeof(char) * 128);
    enum AST_ENUM_TOKEN token_type = AST_UNDEFINED;
    token_list_t* next = lexer;

    bool working = true;
    int i = 0;
    //for (int i = startingIndex - 1; (i >= 0) && working; i--)
    while(next != NULL && working)
    {
        if (i >= startingIndex)
        {
            token_type = next->token;

            //token_type = lexer[i];

            if (token_type == AST_INT || token_type == AST_SHORT)
            {
                working = false;
            }
            else if (isAlphaNumeric(token_type))
            {
                //varName += ParseString(token_type);

                char chr = ParseString(token_type);

                lst->chr = chr;
                lst_next = NULL;

                if (next->next != NULL)
                {
                    lst_next = (char_list_t*)malloc(sizeof(char_list_t));
                    lst_next->next = NULL;
                }

                lst->next = lst_next;
                lst = lst_next;

                lst_size++;
            }
            else if (token_type == AST_WHITESPACE)
            {

            }
        }
        i++;
        next = next->next;
    }
    //varName = Reverse(varName);

    char* result = (char*)malloc(sizeof(char) * lst_size + 1);
    result[lst_size] = 0x00;

    //char_list_t* lst_next;
    
    lst = lst_first;
    lst_next = lst->next;
    i = 0;

    while (lst_next != NULL)
    {
        result[i] = lst_next->chr;

        i++;
        lst_next = lst_next->next;
    }

    char* result_reversed = reverse(result);

    return result_reversed;
}

char* FindOperandLeft(int startingIndex, token_list_t* lexer)
{
    int lst_size = 0;
    char_list_t* lst_next = NULL;
    char_list_t* lst = (char_list_t*)malloc(sizeof(char_list_t));

    //char* varName = (char*)malloc(sizeof(char) * 1024);
    enum AST_ENUM_TOKEN token_type = AST_UNDEFINED;
    token_list_t* next = NULL;
    bool working = true;
    //int lst_size = 0;
    char* result = NULL;
    char_list_t* lst_first = lst;
    int i = 0;

    //for (int i = startingIndex - 1; (i >= 0) && working; i--)
    while(next != NULL && working)
    {
        if (i >= startingIndex - 1)
        {
            //token_type = lexer[i];
            token_type = next->token;


            if (token_type == AST_INT || token_type == AST_SHORT)
            {
                working = false;
            }
            else if (isAlphaNumeric(token_type))
            {
                //lst_size++;

                //varName += ParseString(token_type);

                char chr = ParseString(token_type);
                lst_next = (char_list_t*)malloc(sizeof(char_list_t));
                lst_next->next = NULL;
                lst_next->chr = chr;

                lst->next = lst_next;
                lst = lst_next;

                lst_size++;
            }
            else if (token_type == AST_WHITESPACE)
            {

            }
            else if (token_type == AST_MULTIPLY)
            {
                working = false;
            }
            else if (token_type == AST_DIV)
            {
                working = false;
            }
            else if (token_type == AST_PLUS)
            {
                working = false;
            }
            else if (token_type == AST_SUBTRACT)
            {
                working = false;
            }
        }
        i++;
        next = next->next;
    }
    
    if (lst_size > 0)
    {
        //result = (char*)malloc(sizeof(char) * lst_size + 1);
        //result[lst_size] = 0x00;

        result = (char*)malloc(sizeof(char) * lst_size + 1);
        result[lst_size] = 0x00;

        //char_list_t* lst_next;

        lst = lst_first;
        lst_next = lst->next;
        i = 0;

        while (lst_next != NULL)
        {
            result[i] = lst_next->chr;

            i++;
            lst_next = lst_next->next;
        }
    }
    return result;

    //varName = Reverse(varName);
    //return varName;
}

char* FindOperandRight(int index, token_list_t* lexer, int* outIndex)
{
    int lst_size = 0;
    char_list_t* lst_next = NULL;
    char_list_t* lst = (char_list_t*)malloc(sizeof(char_list_t));

    //char* varValue = (char*)malloc(sizeof(char) * 1024);
    enum AST_ENUM_TOKEN token_type = AST_UNDEFINED;
    bool isForLoop = false;
    bool working = true;
    int i;
    token_list_t* next = lexer;
    char_list_t* lst_first = lst;
    i = 0;

    //for (i = index + 1; i < lexerCount && working; i++)
    while (next != NULL && working)
    {
        token_type = next->token;

        //token_type = lexer[i];

        if (token_type == AST_SEMI_COLON)
        {
            if (isForLoop == false)
            {
                working = false;
            }
        }
        else if (token_type == AST_MULTIPLY)
        {
            working = false;
        }
        else if (token_type == AST_DIV)
        {
            working = false;
        }
        else if (token_type == AST_PLUS)
        {
            working = false;
        }
        else if (token_type == AST_SUBTRACT)
        {
            working = false;
        }
        else if (token_type == AST_FOR)
        {
            isForLoop = true;
        }
        else
        {
            //varValue += ParseString(token_type);
            char chr = ParseString(token_type);
            lst_next = (char_list_t*)malloc(sizeof(char_list_t));
            lst_next->next = NULL;
            lst_next->chr = chr;

            lst->next = lst_next;
            lst = lst_next;

            lst_size++;
        }

        next = next->next;
        i++;
    }

    outIndex = i;

    char* result = NULL;
    if (lst_size > 0)
    {
        result = (char*)malloc(sizeof(char) * lst_size + 1);
        result[lst_size] = 0x00;

        //char_list_t* lst_next;

        lst = lst_first;
        lst_next = lst->next;
        i = 0;

        while (lst_next != NULL)
        {
            result[i] = lst_next->chr;

            i++;
            lst_next = lst_next->next;
        }
    }
    return result;
}

enum OperatorType GetNextOperatorType(int index, token_list_t* lexer)
{
    enum AST_ENUM_TOKEN token_type = AST_UNDEFINED;
    token_list_t* next = lexer;

    //for (int i = index + 1; i < lexerCount; i++)
    while(next != NULL)
    {
        token_type = next->token;
        //token_type = lexer[i];
        if (token_type == AST_SEMI_COLON)
        {
            next = next->next;
            continue;
        }
        else if (token_type == AST_MULTIPLY)
        {
            return OT_MULTIPLY;
        }
        else if (token_type == AST_DIV)
        {
            return OT_DIVIDE;
        }
        else if (token_type == AST_PLUS)
        {
            return OT_ADD;
        }
        else if (token_type == AST_SUBTRACT)
        {
            return OT_SUBTRACT;
        }
        else if (token_type == AST_FOR)
        {
            next = next->next;
            continue;
        }
        else
        {
            next = next->next;
            continue;
        }
        next = next->next;
    }

    return OT_UNDEFINED;
}


int GetCurrPrecedence(int defaultPrecedence, int index, enum AST_ENUM_TOKEN token, token_list_t* lexer)
{
    int precedence = defaultPrecedence;
    enum AST_ENUM_TOKEN tok;
    token_list_t* next = lexer;

    //for(int j = index; j< lexerCount;j++)
    while(next != NULL)
    {
        tok = next->token;
        //tok = lexer[j];

        if (tok == AST_MULTIPLY)
        {
            return (int)OP_AST_MULTIPLY;
        }
        else if (tok == AST_DIV)
        {
            return (int)OP_AST_DIVIDE;
        }
        else if (tok == AST_PLUS)
        {
            return (int)OP_AST_ADD;
        }
        else if (tok == AST_SUBTRACT)
        {
            return (int)OP_AST_SUBTRACT;
        }

        next = next->next;
    }

    return (int)OP_AST_UNDEFINED;
}
//TODO: parse tenery operator. parse unary operator.
AST* ParseBinaryOperator(int* index, token_list_t* lexer, AST* parentAST, AST* rootAST)
{
    AST* ast;
    ast_init(&ast);

    enum AST_ENUM_TOKEN tok = lexer->token;
    int i = index + 1;

    ast->operatorType = GetNextOperatorType(index - 1, lexer);
    ast->nextOprType = GetNextOperatorType(index, lexer);
    ast->precedence = GetCurrPrecedence(0, index, tok, lexer);
    ast->operandLeft = FindOperandLeft(index, lexer);
    ast->operandRight = FindOperandRight(index, lexer, &i);
    index = i;

    if (rootAST == NULL)
    {
        rootAST = ast;
    }
    if (parentAST == NULL)
    {
        parentAST = ast;
    }
    else
    {
        if (ast != NULL)
        {
            if (ast->leftChild == NULL)
            {
                ast->leftChild = parentAST;
            }
            else if (ast->rightChild == NULL)
            {
                ast->rightChild = parentAST;
            }


            if ((parentAST)->leftChild == NULL)
            {
                (parentAST)->leftChild = ast;
            }
            else if ((parentAST)->rightChild == NULL)
            {
                (parentAST)->rightChild = ast;
            }

            if (ast->nextOprType != OT_UNDEFINED)
            {
                AST* astPrior = (AST*)malloc(sizeof(AST));

                ast_init(astPrior);

                astPrior->operatorType = ast->nextOprType;
                astPrior->leftChild = parentAST;
                astPrior->rightChild = ast;

                (parentAST) = ast;

                return astPrior;
            }
            else
            {
                (parentAST) = ast;

                return ast;
            }
        }
    }
    
    (parentAST) = ast;

    return ast;
}

token_list_t* FindVarValue(int index, token_list_t* lexer)
{
    token_list_t* sub_lexer = lexer;
    token_list_t* valueTokens = (token_list_t*)malloc(sizeof(token_list_t));
    token_list_t* lst_first = valueTokens;
    token_list_t* lst_main = valueTokens;

    enum AST_ENUM_TOKEN token_type = AST_UNDEFINED;
    bool isForLoop = false;
    bool working = true;

    token_list_t* new_beginning = lexer;
    token_list_t* prev = lexer;
    int i = 0;
    //bool working = true;

    // Search to find beginning marked by specified index. 
    while (new_beginning != NULL)
    {
        if (i == index)
        {
            break;
        }
        i++;
        prev = new_beginning;
        new_beginning = new_beginning->next;
    }
    sub_lexer = prev;

    //for (int i = index + 1; i < lexerCount && working; i++)
    while(sub_lexer != NULL)
    {
        //token_type = lexer[i];
        token_type = sub_lexer->token;

        if (token_type == AST_SEMI_COLON)
        {
            if (isForLoop == false)
            {
                working = false;
            }
        }
        else if (token_type == AST_FOR)
        {
            isForLoop = true;
        }
        else
        {
            //valueTokens.Add(token_type);

            lst_first->token = token_type;
            if (sub_lexer->next != NULL)
            {
                lst_first->next = (token_list_t*)malloc(sizeof(token_list_t));
            }
            else 
            {
                lst_first->next = NULL;
            }
            lst_first = lst_first->next;
        }
        sub_lexer = sub_lexer->next;
    }

    //return valueTokens;

    return lst_main;
}

bool isAlphaNumeric(enum AST_ENUM_TOKEN token)
{
    int tokenInt = (int)token;
    if (tokenInt >= 0x26 && tokenInt <= 0x3F)
    {
        return true;
    }
    if (tokenInt >= 0x40 && tokenInt <= 0x59)
    {
        return true;
    }
    if (tokenInt >= 0x5A && tokenInt <= 0x63)
    {
        return true;
    }
    return false;
}

bool isNumeric(enum AST_ENUM_TOKEN token)
{
    int tokenInt = (int)token;
    if (tokenInt >= 0x5A && tokenInt <= 0x63)
    {
        return true;
    }
    if(token == AST_FULL_STOP)
    {
        return true;
    }
    return false;
}

bool isWhiteSpace(enum AST_ENUM_TOKEN token)
{
    return token == AST_WHITESPACE;
}

AST* Expr(char* expr)
{
    token_list_t* tokens = Lexer(expr);

    AST* computeResult = Parser(tokens);

    return computeResult;
}

char* ParseString(enum AST_ENUM_TOKEN token)
{
    char* result = NULL;

    int tokenInt = (int)token;

    if (tokenInt >= 0x26 && tokenInt <= 0x3F)
    {
        switch (tokenInt)
        {
            case 0x26:
                result = 'A';
                break;
            case 0x27:
                result = 'B';
                break;
            case 0x28:
                result = 'C';
                break;
            case 0x29:
                result = 'D';
                break;
            case 0x2A:
                result = 'E';
                break;
            case 0x2B:
                result = 'F';
                break;
            case 0x2C:
                result = 'G';
                break;
            case 0x2D:
                result = 'H';
                break;
            case 0x2E:
                result = 'I';
                break;
            case 0x2F:
                result = 'J';
                break;
            case 0x30:
                result = 'K';
                break;
            case 0x31:
                result = 'L';
                break;
            case 0x32:
                result = 'M';
                break;
            case 0x33:
                result = 'N';
                break;
            case 0x34:
                result = 'O';
                break;
            case 0x35:
                result = 'P';
                break;
            case 0x36:
                result = 'Q';
                break;
            case 0x37:
                result = 'R';
                break;
            case 0x38:
                result = 'S';
                break;
            case 0x39:
                result = 'T';
                break;
            case 0x3A:
                result = 'U';
                break;
            case 0x3B:
                result = 'V';
                break;
            case 0x3C:
                result = 'W';
                break;
            case 0x3D:
                result = 'X';
                break;
            case 0x3E:
                result = 'Y';
                break;
            case 0x3F:
                result = 'Z';
                break;
        }
    }

    if (tokenInt >= 0x40 && tokenInt <= 0x59)
    {
        switch (tokenInt)
        {
            case 0x40:
                result = 'a';
                break;
            case 0x41:
                result = 'b';
                break;
            case 0x42:
                result = 'c';
                break;
            case 0x43:
                result = 'd';
                break;
            case 0x44:
                result = 'e';
                break;
            case 0x45:
                result = 'f';
                break;
            case 0x46:
                result = 'g';
                break;
            case 0x47:
                result = 'h';
                break;
            case 0x48:
                result = 'i';
                break;
            case 0x49:
                result = 'j';
                break;
            case 0x4A:
                result = 'k';
                break;
            case 0x4B:
                result = 'l';
                break;
            case 0x4C:
                result = 'm';
                break;
            case 0x4D:
                result = 'n';
                break;
            case 0x4E:
                result = 'o';
                break;
            case 0x4F:
                result = 'p';
                break;
            case 0x50:
                result = 'q';
                break;
            case 0x51:
                result = 'r';
                break;
            case 0x52:
                result = 's';
                break;
            case 0x53:
                result = 't';
                break;
            case 0x54:
                result = 'u';
                break;
            case 0x55:
                result = 'v';
                break;
            case 0x56:
                result = 'w';
                break;
            case 0x57:
                result = 'x';
                break;
            case 0x58:
                result = 'y';
                break;
            case 0x59:
                result = 'z';
                break;
        }
    }
    if (tokenInt >= 0x5A && tokenInt <= 0x63)
    {
        switch (tokenInt)
        {
            case 0x5A:
                result = '0';
                break;
            case 0x5B:
                result = '1';
                break;
            case 0x5C:
                result = '2';
                break;
            case 0x5D:
                result = '3';
                break;
            case 0x5E:
                result = '4';
                break;
            case 0x5F:
                result = '5';
                break;
            case 0x60:
                result = '6';
                break;
            case 0x61:
                result = '7';
                break;
            case 0x62:
                result = '8';
                break;
            case 0x63:
                result = '9';
                break;
        }
    }

    return result;
}

char* ParseStringFromToken(token_list_t* lexer, int index)
{
    char* result;
    token_list_t* lst_first = lexer;
    enum AST_ENUM_TOKEN token_type = AST_UNDEFINED;
    char_list_t* lst_char = (char_list_t*)malloc(sizeof(char_list_t));
    char* c;
    int string_size = 0;
    char_list_t* lst_char_first = lst_char;

    //for (int i = 0; i < lexerCount; i++)
    while(lst_first != NULL)
    {
        //token_type = lexer[i];

        //result += ParseString(token_type);
        
        token_type = lst_first->token;

        c = ParseString(token_type);
        string_size++;

        lst_char->chr = c;

        if (lst_first->next != NULL)
        {
            lst_char->next = (char_list_t*)malloc(sizeof(char_list_t));

            lst_char = lst_char->next;
        }

        lst_first = lst_first->next;
    }

    result = (char*)malloc(sizeof(char) * string_size + 1);
    result[string_size] = 0x00;

    char_list_t* list_next = lst_char_first;
    int i = 0;

    while(list_next != NULL)
    {
        result[i] = list_next->chr;


        list_next = list_next->next;
        i++;
    }

    return result;
}

AST* Parser(token_list_t* lexer)
{
    AST* results = NULL;
    enum AST_ENUM_TOKEN token_type = AST_UNDEFINED;
    char* varName = NULL;
    enum AST_ENUM_TYPE variableType = AST_TYPE_UNDEFINED;
    bool functionScope = false;
    bool parenthesisScope = false;
    bool multilineComment = false;
    bool singleComment = false;
    AST* parentAST = NULL;
    AST* rootAST = NULL;
    token_list_t* lexeme_front = lexer;
    token_list_t* subLexerExprPrev = NULL;
    int i = 0;

    //for (int i = 0; i < lexer.Count; i++)
    while(lexeme_front != NULL)
    {
        //token_type = lexer[i];
        token_type = lexeme_front->token;

        if (token_type == AST_MULTILINE_COMMENT_BEGIN)
        {
            multilineComment = true;
        }
        if (token_type == AST_MULTILINE_COMMENT_END)
        {
            multilineComment = false;
        }
        if (token_type == AST_COMMENT_BEGIN)
        {
            singleComment = true;
        }
        if (token_type == AST_COMMENT_END)
        {
            singleComment = false;
        }
        if (token_type == AST_PARENTHESIS_BEGIN)
        {
            parenthesisScope = true;
        }
        if (token_type == AST_PARENTHESIS_END)
        {
            parenthesisScope = false;
        }
        if (token_type == AST_INT) // int
        {
            variableType = AST_INT;
        }
        if (token_type == AST_SHORT) // short
        {
            variableType = AST_SHORT;
        }

        if (token_type == AST_ASSIGN) // =
        {
            if (variableType != AST_TYPE_UNDEFINED)
            {
                varName = FindVarName(i, lexer);

                token_list_t* subLexerExpr = FindVarValue(i, lexer); // Find the tokens after assignment operator and before semi-colon.
                token_list_t* subLexerExpr_main = subLexerExpr;

                //if (subLexerCount > 0)
                {
                    // Has underlying expression to parse.
                    //for (int j = 0; j < subLexerExpr.Count; j++)
                    int j = 0;
                    while(subLexerExpr != NULL)
                    {
                        if (subLexerExpr == 0xcdcdcdcdcdcdcdcd)
                        {
                            break;
                        }
                        enum AST_ENUM_TOKEN sub_token = subLexerExpr->token;
                        //subLexerExpr.RemoveAt(j);

                        if (sub_token != AST_UNDEFINED)
                        {

                        }
                        if (sub_token == AST_PARENTHESIS_BEGIN)
                        {

                        }
                        if (sub_token == AST_PARENTHESIS_END)
                        {

                        }
                        if (sub_token == AST_WHITESPACE)
                        {

                        }
                        if(isNumeric(sub_token))
                        {

                        }
                        // int a = 2 / 8 * (90 + 20) + 3+1 - 4*8 +7*3 + 12;
                        if (sub_token == AST_MULTIPLY)
                        {
                            AST* mulAST;

                            // PARSE BINARY OPERATOR
                            mulAST = ParseBinaryOperator(&j, subLexerExpr, &parentAST, &rootAST);
                        }
                        if (sub_token == AST_DIV)
                        {                                    
                            AST* divAST;

                            // PARSE BINARY OPERATOR
                            divAST = ParseBinaryOperator(&j, subLexerExpr, &parentAST, &rootAST);
                        }
                        if (sub_token == AST_PLUS)
                        {
                            AST* addAST;

                            // PARSE BINARY OPERATOR
                            addAST = ParseBinaryOperator(&j, subLexerExpr, &parentAST, &rootAST);
                        }
                        if (sub_token == AST_SUBTRACT)
                        {
                            AST* subtractAST;

                            // PARSE BINARY OPERATOR
                            subtractAST = ParseBinaryOperator(&j, subLexerExpr, &parentAST, &rootAST);
                        }

                        j++;
                        subLexerExprPrev = subLexerExpr;
                        subLexerExpr = subLexerExpr->next;
                    }
                }

            }
        }
        if (token_type == AST_SEMI_COLON) // ;
        {
            if (variableType != AST_TYPE_UNDEFINED && varName != NULL)
            {

            }
        }

        i++;
        lexeme_front = lexeme_front->next;
    }

    if(rootAST != NULL)
    {
        results = rootAST;
    }

    return results;
}

token_list_t* Lexer(char* code)
{
    int code_len = strlen(code);
    
    token_list_t* lexer; //token_list_t* lexer = (token_list_t*)malloc(sizeof(token_list_t));
    token_list_t* lexer_node = NULL;

    tokenlst_init(&lexer);
    token_list_t* lexer_front = lexer;

    enum AST_ENUM_TOKEN token_type = AST_UNDEFINED;
    enum AST_ENUM_TYPE data_type = AST_TYPE_UNDEFINED;
    char current_char;
    char current_char1;
    char current_char2;
    char current_char3;
    char current_char4;
    bool comment = false;
    bool commentAfter = false;

    for (int i = 0; i < code_len; i++)
    {
        current_char = code[i];

        if (current_char == ' ' || current_char == '\n' || current_char == '\r' || current_char == '\t')
        {
            token_type = AST_WHITESPACE;
        }
        if (current_char == 'A')
        {
            token_type = AST_A;
        }
        if (current_char == 'B')
        {
            token_type = AST_B;
        }
        if (current_char == 'C')
        {
            token_type = AST_C;
        }
        if (current_char == 'D')
        {
            token_type = AST_D;
        }
        if (current_char == 'E')
        {
            token_type = AST_E;
        }
        if (current_char == 'F')
        {
            token_type = AST_F;
        }
        if (current_char == 'G')
        {
            token_type = AST_G;
        }
        if (current_char == 'H')
        {
            token_type = AST_H;
        }
        if (current_char == 'I')
        {
            token_type = AST_I;
        }
        if (current_char == 'J')
        {
            token_type = AST_J;
        }
        if (current_char == 'K')
        {
            token_type = AST_K;
        }
        if (current_char == 'L')
        {
            token_type = AST_L;
        }
        if (current_char == 'M')
        {
            token_type = AST_M;
        }
        if (current_char == 'N')
        {
            token_type = AST_N;
        }
        if (current_char == 'O')
        {
            token_type = AST_O;
        }
        if (current_char == 'P')
        {
            token_type = AST_P;
        }
        if (current_char == 'Q')
        {
            token_type = AST_Q;
        }
        if (current_char == 'R')
        {
            token_type = AST_R;
        }
        if (current_char == 'S')
        {
            token_type = AST_S;
        }
        if (current_char == 'T')
        {
            token_type = AST_T;
        }
        if (current_char == 'U')
        {
            token_type = AST_U;
        }
        if (current_char == 'V')
        {
            token_type = AST_V;
        }
        if (current_char == 'W')
        {
            token_type = AST_W;
        }
        if (current_char == 'X')
        {
            token_type = AST_X;
        }
        if (current_char == 'Y')
        {
            token_type = AST_Y;
        }
        if (current_char == 'Z')
        {
            token_type = AST_Z;
        }

        if (current_char == 'a')
        {
            token_type = AST_a;
        }
        if (current_char == 'b')
        {
            token_type = AST_b;
        }
        if (current_char == 'c')
        {
            token_type = AST_c;
        }
        if (current_char == 'd')
        {
            token_type = AST_d;
        }
        if (current_char == 'e')
        {
            token_type = AST_e;
        }
        if (current_char == 'f')
        {
            token_type = AST_f;
        }
        if (current_char == 'g')
        {
            token_type = AST_g;
        }
        if (current_char == 'h')
        {
            token_type = AST_h;
        }
        if (current_char == 'i')
        {
            token_type = AST_i;
        }
        if (current_char == 'j')
        {
            token_type = AST_j;
        }
        if (current_char == 'k')
        {
            token_type = AST_k;
        }
        if (current_char == 'k')
        {
            token_type = AST_l;
        }
        if (current_char == 'm')
        {
            token_type = AST_m;
        }
        if (current_char == 'n')
        {
            token_type = AST_n;
        }
        if (current_char == 'o')
        {
            token_type = AST_o;
        }
        if (current_char == 'p')
        {
            token_type = AST_p;
        }
        if (current_char == 'q')
        {
            token_type = AST_q;
        }
        if (current_char == 'r')
        {
            token_type = AST_r;
        }
        if (current_char == 's')
        {
            token_type = AST_s;
        }
        if (current_char == 't')
        {
            token_type = AST_t;
        }
        if (current_char == 'u')
        {
            token_type = AST_u;
        }
        if (current_char == 'v')
        {
            token_type = AST_v;
        }
        if (current_char == 'w')
        {
            token_type = AST_w;
        }
        if (current_char == 'y')
        {
            token_type = AST_x;
        }
        if (current_char == 'y')
        {
            token_type = AST_y;
        }
        if (current_char == 'z')
        {
            token_type = AST_z;
        }
        if (current_char == '0')
        {
            token_type = AST_0;
        }
        if (current_char == '1')
        {
            token_type = AST_1;
        }
        if (current_char == '2')
        {
            token_type = AST_2;
        }
        if (current_char == '3')
        {
            token_type = AST_3;
        }
        if (current_char == '4')
        {
            token_type = AST_4;
        }
        if (current_char == '5')
        {
            token_type = AST_5;
        }
        if (current_char == '6')
        {
            token_type = AST_6;
        }
        if (current_char == '7')
        {
            token_type = AST_7;
        }
        if (current_char == '8')
        {
            token_type = AST_8;
        }
        if (current_char == '9')
        {
            token_type = AST_9;
        }
        if (current_char == ';')
        {
            token_type = AST_SEMI_COLON;
        }

        if (current_char == '(')
        {
            token_type = AST_PARENTHESIS_BEGIN;
        }

        if (current_char == ')')
        {
            token_type = AST_PARENTHESIS_END;
        }

        if (current_char == '*')
        {
            token_type = AST_MULTIPLY;
        }

        if (current_char == '/')
        {
            token_type = AST_DIV;
        }

        if (current_char == '+')
        {
            token_type = AST_PLUS;
        }

        if (current_char == '-')
        {
            token_type = AST_SUBTRACT;
        }

        if (current_char == '^')
        {
            token_type = AST_XOR;
        }

        if (current_char == '=')
        {
            token_type = AST_ASSIGN;
        }

        if (current_char == '~')
        {
            token_type = AST_TILDE;
        }

        //if (current_char == '-')
        //{
        //    token_type = AST_NEGATION;
        //}

        if (i + 1 < code_len)
        {
            current_char1 = code[i + 1];

            char a = tolower(current_char);
            char b = tolower(current_char1);

            if (a == "i" && b == "f")
            {
                token_type = AST_IF;
                i++;
            }

            if (current_char == '-' && current_char1 == '-')
            {
                token_type = AST_DECREMENT;
                i++;
            }
            if (current_char == '+' && current_char1 == '+')
            {
                token_type = AST_INCREMENT;
                i++;
            }
            if (current_char == '=' && current_char1 == '=')
            {
                token_type = AST_EQUALITY;
                i++;
            }
            if (current_char == '!' && current_char1 == '=')
            {
                token_type = AST_NOT_EQUAL_TO;
                i++;
            }
            if (current_char == '/' && current_char1 == '/')
            {
                token_type = AST_COMMENT_BEGIN;
                i++;
                comment = true;
            }
            if (current_char == '\n' || current_char == '\r' || current_char1 == '\r' || current_char1 == '\n')
            {
                commentAfter = true;
                comment = false;
            }
            if (current_char == '/' && current_char1 == '*')
            {
                token_type = AST_MULTILINE_COMMENT_BEGIN;
                i++;
            }
            if (current_char == '*' && current_char1 == '/')
            {
                token_type = AST_MULTILINE_COMMENT_END;
                i++;
            }
            if (current_char == 'd' && current_char1 == 'o')
            {
                token_type = AST_DO;
                i++;
            }

            if (i + 2 < code_len)
            {
                current_char2 = code[i + 2];

                if (current_char == 'f' && current_char1 == 'o' && current_char2 == 'r')
                {
                    token_type = AST_FOR;
                    i += 2;
                }

                if (current_char == 'i' && current_char1 == 'n' && current_char2 == 't')
                {
                    data_type = AST_INT;
                    token_type = AST_INT;
                    i += 2;
                }

                if (i + 3 < code_len)
                {
                    current_char3 = code[i + 3];

                    if (current_char == 'b' && current_char1 == 'o' && current_char2 == 'o' && current_char3 == 'l')
                    {
                        data_type = AST_BOOLEAN;
                        token_type = AST_BOOLEAN;
                        i += 3;
                    }

                    if (i + 4 < code_len)
                    {
                        current_char4 = code[i + 4];

                        if (current_char == 'w' && current_char1 == 'h' && current_char2 == 'i' && current_char3 == 'l' && current_char4 == 'e')
                        {
                            token_type = AST_WHILE;
                            i += 4;
                        }

                        if (current_char == 's' && current_char1 == 'h' && current_char2 == 'o' && current_char3 == 'r' && current_char4 == 't')
                        {
                            data_type = AST_SHORT;
                            token_type = AST_SHORT;
                            i += 4;
                        }
                    }
                }
            }
        }

        if (token_type != AST_UNDEFINED)
        {
            lexer_node = NULL;
            
            lexer->next = NULL;
            lexer->token = token_type;

            if (i + 1 < code_len)
            {
                tokenlst_init(&lexer_node);

                //lexer_node = (token_list_t*)malloc(sizeof(token_list_t));
                //lexer_node->next = NULL;

                lexer->next = lexer_node;
                lexer = lexer->next;
            }

            //lexer.Add(token_type);
        }

        if (commentAfter && comment == false)
        {
            if (i + 1 < code_len)
            {
                tokenlst_init(&lexer_node);
                //lexer_node = (token_list_t*)malloc(sizeof(token_list_t));
                //lexer_node->next = NULL;
                lexer_node->token = AST_COMMENT_END;

                lexer->next = lexer_node;
                lexer = lexer->next;
            }

            //lexer.Add(AST_COMMENT_END);
            commentAfter = false;
        }
    }

    return lexer_front;
}

char* OperatorTypeToString(enum OperatorType operatorType)
{
    char* result = NULL;
    if (operatorType == OT_MULTIPLY)
    {
        result = (char*)malloc(sizeof(char) * 12);
        result[0] = 'O';
        result[1] = 'T';
        result[2] = '_';
        result[3] = 'M';
        result[4] = 'U';
        result[5] = 'L';
        result[6] = 'T';
        result[7] = 'I';
        result[8] = 'P';
        result[9] = 'L';
        result[10] = 'Y';
        result[11] = 0x00;
        //return "OT_MULTIPLY";
    }
    else if (operatorType == OT_DIVIDE)
    {
        result = (char*)malloc(sizeof(char) * 10);
        result[0] = 'O';
        result[1] = 'T';
        result[2] = '_';
        result[3] = 'D';
        result[4] = 'I';
        result[5] = 'V';
        result[6] = 'I';
        result[7] = 'D';
        result[8] = 'E';
        result[9] = 0x00;
        //return "OT_DIVIDE";
    }
    else if (operatorType == OT_ADD)
    {
        result = (char*)malloc(sizeof(char) * 7);
        result[0] = 'O';
        result[1] = 'T';
        result[2] = '_';
        result[3] = 'A';
        result[4] = 'D';
        result[5] = 'D';
        result[6] = 0x00;
        //return "OT_ADD";
    }
    else if (operatorType == OT_SUBTRACT)
    {
        result = (char*)malloc(sizeof(char) * 12);
        result[0] = 'O';
        result[1] = 'T';
        result[2] = '_';
        result[3] = 'S';
        result[4] = 'U';
        result[5] = 'B';
        result[6] = 'T';
        result[7] = 'R';
        result[8] = 'A';
        result[9] = 'C';
        result[10] = 'T';
        result[11] = 0x00;
        //return "OT_SUBTRACT";
    }
    else
    {
        result = (char*)malloc(sizeof(char) * 13);
        result[0] = 'O';
        result[1] = 'T';
        result[2] = '_';
        result[3] = 'U';
        result[4] = 'N';
        result[5] = 'D';
        result[6] = 'E';
        result[7] = 'F';
        result[8] = 'I';
        result[9] = 'N';
        result[10] = 'E';
        result[11] = 'D';
        result[12] = 0x00;
        //return "Undefined Operator Type";
    }
    return result;
}



id_list_t* front_of_list = NULL;
id_list_t* lst_front = NULL;
void PrintAST(AST* ast, int currentDepth)
{
    if (ast == 0xcdcdcdcdcdcdcdcd || ast == NULL)
    {
        return;
    }

    if (front_of_list == NULL)
    {
        front_of_list = (id_list_t*)malloc(sizeof(id_list_t));
        idlst_init(front_of_list, ast->ID);

        lst_front = front_of_list;
    }
    else 
    {
        if (!idlst_exists(lst_front, ast->ID))
        {
            id_list_t* child = (id_list_t*)malloc(sizeof(id_list_t));
            idlst_init(child, ast->ID);

            front_of_list->next = child;
            front_of_list = child;
        }
        else 
        {
            return;
        }
    }
    //if (debugIDs.Exists(ID = > ID == ast.ID))
    //{
    //    return;
    //}

    //debugIDs.Add(ast.ID);

    int d = 0;
    char* indentation = (char*)malloc(sizeof(char) * 1);
    indentation[0] = '\t';
    //for (int i = 0;i < 28;i++)
    //{
    //    indentation[i] = 0x00;
    //}
    //for (d = 0; d < currentDepth; d++)
    //{
    //    indentation = strcat(indentation, '\t');
    //}
    //indentation[d] = 0x00;

    char* title = strcat(indentation, "--AST--");
    //printf("%s",title);
    printf("%s", "--AST--");

    printf("%s", "op: ");
    printf("%s", OperatorTypeToString(ast->operatorType));

    printf("%s", "next: ");
    printf("%s", OperatorTypeToString(ast->nextOprType));

    printf("%s", "left: ");
    printf("%s", ast->operandLeft);

    printf("%s", "right: ");
    printf("%s", ast->operandRight);

    printf("%s", "precedence: ");
    printf("%d", ast->precedence);

    if (ast->leftChild != NULL)
    {
        //if (!debugIDs.Exists(ID = > ID == ast->leftChild->ID))
        if(!idlst_exists(lst_front, ast->leftChild->ID))
        {
            printf("%s", "child left:");

            PrintAST(ast->leftChild, currentDepth + 1);
        }
    }
    if (ast->rightChild != NULL)
    {
        //if (!debugIDs.Exists(ID = > ID == ast.rightChild.ID))
        if (!idlst_exists(lst_front, ast->rightChild->ID))
        {
            printf("%s", "child right:");

            PrintAST(ast->rightChild, currentDepth + 1);
        }
    }
}
