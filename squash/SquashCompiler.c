#include "SquashCompiler.h"

struct SquashCompiler* squash_compiler_new()
{
    struct SquashCompiler* squash_compiler = (struct SquashCompiler*)malloc(sizeof(struct SquashCompiler));
    return squash_compiler;
}

void squash_compiler_init(struct SquashCompiler* squash_compiler, char* input, int inputLength)
{
    LogInformation("SquashCompiler(): ctor");

    stats_begin();

    list_t* tokens = list_new();

    Minifier_t* minifier = Minifier_new();
    Minifier_init(minifier);

    int newInputLength = 0;

    input = MinifyCode(minifier, input, inputLength, tokens, &newInputLength); // ref preTokens

    LogInformation("SOURCE:\n%s", input);
    //LogError("SquashCompiler(): error\n");
    //LogCritical("SquashCompiler(): critical\n");
    //LogWarning("SquashCompiler(): warning\n");

    squash_compiler->lexer = lexer_new(minifier); // new Lexer(input, ref preTokens);
    lexer_init(squash_compiler->lexer, input, inputLength, tokens);

    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

    squash_compiler->symbolTable = SymbolTable_new();
    SymbolTable_init(&squash_compiler->symbolTable);

    //squash_compiler->rootAST = new AbstractSyntaxTree();
    squash_compiler->asm0 = assembler_new(); // new Assembler(rootAST)
}

void CompileExpression(struct SquashCompiler* squash_compiler, char* output_file_name, char* output_binary_file_name, bool enable_tracing)
{
    astnode_t* expression = ParseStatements(squash_compiler);

    //rootAST.Root = expression;

    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type != AST_EOF)
    {
        LogCritical("Unexpected token found. Position: %d", lexer_getposition(squash_compiler->lexer));
        return;
    }

    //OptimiseNode(&expression, false);

    //squash_compiler->asm->Is_macOS = true;
    //squash_compiler->asm->Is_Linux = true;
    squash_compiler->asm0->Is_Windows = true;

    GenerateCode(squash_compiler->asm0, expression, output_file_name, output_binary_file_name, enable_tracing);
}

astnode_t* ParseStatements(struct SquashCompiler* squash_compiler)
{
    LogInformation("ParseStatements(): Parsing individual statements that may be outside a function definition or inside a function definition.");

    astnode_t* expression = ParseExpression(squash_compiler, 0);
    if (expression != NULL)
    {
        if (expression->FunctionBody == NULL)
        {
            expression->FunctionBody = list_new(); // new List<ASTNode>()
        }
        //expression.FunctionBody.Add(expression);
    }

    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_SemiColon)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

        LogInformation("ParseStatements(): end of statement semi colon found.");

        if (squash_compiler->currentToken->Type == AST_CurleyBrace && strcmp(squash_compiler->currentToken->Value, "}") == 0)
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

            LogInformation("ParseStatements(): end of function curley brace found.");
        }
    }
    else if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_CurleyBrace
        && strcmp(squash_compiler->currentToken->Value, "}") == 0)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

        LogInformation("ParseStatements(): end of function curley brace found.");
    }
    else
    {
        //return null;
    }

    if (squash_compiler->currentToken != NULL)
    {
        astnode_t* expressionChild = ParseStatements(squash_compiler);

        if (expressionChild != NULL)
        {
            if (expression != NULL)
            {
                list_enqueue(expression->FunctionBody, (void*)expressionChild);

                expression->IsFunctionDefinition = true;

                LogInformation("ParseStatements(): added statement to function body which is function definition.");
            }
            else 
            {
                return expressionChild;
            }
        }
    }

    return expression;
}

astnode_t* ParseVariableDeclaration(struct SquashCompiler* squash_compiler, enum VarType varType)
{
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Whitespace)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Identifier)
        {
            char* varIdentifier = squash_compiler->currentToken->Value;

            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

            //VariableSymbol variableSymbol = symbolTable.DefineVariable(varType, varIdentifier, "");
            astnode_t* left = NULL;
            astnode_t* right = NULL;
            astnode_t* varDeclNode = ast_node_new();

            ast_node_init_bt(&varDeclNode, AST_VariableDeclaration, varIdentifier, AST_VALUE_STRING, left, right);
            //astnode_t* varDeclNode = new ASTNode(ASTNodeType.VariableDeclaration, varIdentifier, left, right);
            varDeclNode->VariableType = varType;
            return varDeclNode;
        }
    }
    return NULL;
}

astnode_t* ParseVariableDefine(struct SquashCompiler* squash_compiler, enum VarType varType)
{
    bool varAssignment = true;

    while (varAssignment)
    {
        if (squash_compiler->currentToken == NULL || squash_compiler->currentToken->Type == AST_EOF
            || squash_compiler->currentToken->Type == AST_Identifier)
        {
            varAssignment = false;
        }
        else
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        }
    }

    return parseAssignmentOperator(squash_compiler, varType);
}

astnode_t* parseAssignmentOperator(struct SquashCompiler* squash_compiler, enum VarType varType)
{
    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Identifier)
    {
        char* identifierName = squash_compiler->currentToken->Value;
        double varValue = 0;

        int savedPosition = lexer_getposition(squash_compiler->lexer);
        token_t* assignmentToken = SkipToToken(squash_compiler->lexer, AST_Assignment);

        if (assignmentToken != NULL && assignmentToken->Type == AST_Assignment)
        {
            VariableSymbol_t* varDefine;

            LogInformation("parseAssignmentOperator(): parsing assignment");

            if (varType != AST_VarAutomatic)
            {
                switch (varType)
                {
                case AST_Int:
                case AST_Int32:
                    squash_compiler->currentToken = SkipToToken(squash_compiler->lexer, AST_NumberEntry);
                    int varValueI = atoi(squash_compiler->currentToken->Value);
                    //int varValueI = int.Parse(squash_compiler->currentToken->Value);
                    varDefine = SymbolTable_DefineVariableI(squash_compiler->symbolTable, AST_Int, identifierName, varValueI);
                    break;
                    //case AST_Long:
                    //case AST_Int64:

                    //    break;
                    //case AST_Float:

                    //    break;
                case AST_Double:
                    squash_compiler->currentToken = SkipToToken(squash_compiler->lexer, AST_NumberEntry);
                    double varValueD = atof(squash_compiler->currentToken->Value);
                    varDefine = SymbolTable_DefineVariableD(squash_compiler->symbolTable, AST_Double, identifierName, varValueD);
                    break;
                case AST_String:
                    squash_compiler->currentToken = SkipToToken(squash_compiler->lexer, AST_StringLiteral);
                    varDefine = SymbolTable_DefineVariableC(squash_compiler->symbolTable, AST_StringLiteral, identifierName, squash_compiler->currentToken->Value);
                    break;
                case AST_Void:
                    varDefine = NULL;
                    break;
                default:
                    varDefine = NULL;
                    //Logger.Log.LogError("Unhandled variable type which is not implemented type specified: '" + varType.ToString() + "'");
                    //throw new Exception("Unhandled variable type which is not implemented type specified: '" + varType.ToString() + "'");
                }
            }
            else
            {   // Automatic type inference required.
                int savedPosition2 = lexer_getposition(squash_compiler->lexer);

                squash_compiler->currentToken = SkipToToken(squash_compiler->lexer, AST_NumberEntry);

                if (squash_compiler->currentToken == NULL)
                {
                    lexer_setposition(squash_compiler->lexer, savedPosition2);

                    squash_compiler->currentToken = SkipToToken(squash_compiler->lexer, AST_StringLiteral);

                    if (squash_compiler->currentToken == NULL)
                    {
                        lexer_setposition(squash_compiler->lexer, savedPosition2);

                        squash_compiler->currentToken = SkipToToken(squash_compiler->lexer, AST_IntLiteral);

                        if (squash_compiler->currentToken == NULL)
                        {
                            lexer_setposition(squash_compiler->lexer, savedPosition2);
                            //Logger.Log.LogError("Unhandled token type. Can not parse null token");
                            varDefine = NULL;
                        }
                        else
                        {
                            int varValueI = atoi(squash_compiler->currentToken->Value);

                            varDefine = SymbolTable_DefineVariableI(squash_compiler->symbolTable, AST_Int, identifierName, varValueI);
                        }
                    }
                    else
                    {
                        char* varValueS = squash_compiler->currentToken->Value;

                        varDefine = SymbolTable_DefineVariableC(squash_compiler->symbolTable, AST_String, identifierName, varValueS);
                    }
                }
                else
                {
                    varValue = atof(squash_compiler->currentToken->Value);

                    varDefine = SymbolTable_DefineVariableD(squash_compiler->symbolTable, AST_Double, identifierName, varValue);
                }
            }

            astnode_t* varDefineNode = ast_node_new();
            ast_node_init_var(&varDefineNode, AST_VariableDefine, identifierName, AST_VALUE_STRING, varDefine);
            //ASTNode varDefineNode = new ASTNode(ASTNodeType.VariableDefine, identifierName, varDefine);

            return varDefineNode;
        }
        else
        {
            if (!SymbolTable_VariableHasKey(squash_compiler->symbolTable, identifierName))
            {
                SymbolTable_DefineVariableI(squash_compiler->symbolTable, AST_VarAutomatic, identifierName, 0);
            }

            VariableSymbol_t* symbol = SymbolTable_LookupVariable(squash_compiler->symbolTable, identifierName);

            astnode_t* variableNode = ast_node_new();
            ast_node_init_var(&variableNode, AST_Variable, identifierName, AST_VALUE_STRING, symbol);
            //ASTNode variableNode = new ASTNode(ASTNodeType.Variable, identifierName, symbol);

            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

            if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_SemiColon && squash_compiler->currentToken->Type != AST_EOF)
            {
                astnode_t* rhs = parseAssignmentOperator(squash_compiler, varType);

                variableNode->Right = rhs;
            }

            return variableNode;
        }
    }
    return NULL;
}

void parseEndStatement(struct SquashCompiler* squash_compiler, astnode_t* varDefineNode)
{
    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_SemiColon)
    {
        LogInformation("parseEndStatement(): parsing end statement");
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        if (squash_compiler->currentToken != NULL)
        {
            astnode_t* rhs = ParseExpression(squash_compiler, 0);
            varDefineNode->Right = rhs;

            char* ast_str = ast_tostring(rhs);

            LogInformation("parseEndStatement(): rhs: %s", ast_str);
        }
    }
}

bool tryParseFloat(const char* str, float* result)
{
    char* endptr;
    errno = 0; // Reset errno before the call

    // Use strtof to convert the string to a float
    *result = strtof(str, &endptr);

    // Check for errors
    if (errno == ERANGE) {
        // Conversion resulted in overflow or underflow
        return false; // Indicate failure
    }
    if (endptr == str) {
        // No conversion was performed
        return false; // Indicate failure
    }
    if (*endptr != '\0') {
        // String contains non-numeric characters
        return false; // Indicate failure
    }

    return true; // Indicate success
}

astnode_t* returnkeyword_parse(struct SquashCompiler* squash_compiler)
{
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Skip past return keyword.

    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_SemiColon)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Skip past whitespace.
        //astnode_t* left = ParseExpression(0, rootAST);
        astnode_t* left = NULL;

        LogInformation("ParsePrimaryExpression(): made a ASTNodeType.FunctionReturn AST with left=null");

        //ASTNode? left = null;
        astnode_t* returnNode = ast_node_new();
        ast_node_init_bt(&returnNode, AST_FunctionReturn, "", AST_VALUE_UNDEFINED, left, NULL);
        //ASTNode returnNode = new ASTNode(ASTNodeType.FunctionReturn, "", left, null);

        //TODO: Check for curley brace to find out the end of a function definition
        return returnNode;
    }
    else
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Skip past whitespace.
        astnode_t* left = ParseExpression(squash_compiler, 0);

        if (left == NULL)
        {
            LogInformation("ParsePrimaryExpression(): made a ASTNodeType.FunctionReturn AST with lhs=null");
        }
        else
        {
            LogInformation("ParsePrimaryExpression(): made a ASTNodeType.FunctionReturn AST");
        }

        astnode_t* returnNode = ast_node_new();
        ast_node_init_bt(&returnNode, AST_FunctionReturn, "", AST_VALUE_UNDEFINED, left, NULL);
        //ASTNode returnNode = new ASTNode(ASTNodeType.FunctionReturn, "", left, null);

        //TODO: Check for curley brace to find out the end of a function definition
        return returnNode;
    }
}

astnode_t* varkeyword_parse(struct SquashCompiler* squash_compiler)
{
    LogInformation("ParsePrimaryExpression(): made a var variable of some inferred type AST");
    astnode_t* varDefineNode = ParseVariableDefine(squash_compiler, AST_VarAutomatic);
    astnode_t* left = ParseExpression(squash_compiler, 0);
    varDefineNode->Left = left;
    parseEndStatement(squash_compiler, varDefineNode);
    return varDefineNode;
}

astnode_t* doublekeyword_parse(struct SquashCompiler* squash_compiler, int* retFlag)
{
    (*retFlag) = 1;
    LogInformation("ParsePrimaryExpression(): double keyword");
    int pos = lexer_getposition(squash_compiler->lexer);
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Whitespace)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Identifier)
        {
            int pos2 = lexer_getposition(squash_compiler->lexer);
            char* functIdentifierName = squash_compiler->currentToken->Value;

            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
            if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Parenthesis
                && strcmp(squash_compiler->currentToken->Value, "(") == 0)
            {
                LogInformation("ParsePrimaryExpression(): double keyword parse function definition '%s'", functIdentifierName);

                astnode_t* functDefNode = ParseFunctionDefinition(squash_compiler, AST_Double, functIdentifierName);

                if (functDefNode != NULL)
                {
                    return functDefNode;
                }
            }
        }
    }
    lexer_setposition(squash_compiler->lexer, pos);

    astnode_t* varDefineNode = ParseVariableDefine(squash_compiler, AST_Double);
    if (varDefineNode != NULL)
    {
        astnode_t* left = ParseExpression(squash_compiler, 0);
        char* ast_str = ast_tostring(varDefineNode);
        char* ast_strB = ast_tostring(left);
        LogInformation("ParsePrimaryExpression(): var define node: %s, left expr: %s", ast_str, ast_strB);
        varDefineNode->Left = left;
        parseEndStatement(squash_compiler, varDefineNode);
        return varDefineNode;
    }
    else
    {
        lexer_setposition(squash_compiler->lexer, pos);
        astnode_t* varDecla = ParseVariableDeclaration(squash_compiler, AST_Double);

        if (varDecla != NULL)
        {
            return varDecla;
        }
    }
    (*retFlag) = 0;
    return NULL;
}

astnode_t* identifier_parse(struct SquashCompiler* squash_compiler, bool rememberLocation, int pos, int* retFlag)
{
    (*retFlag) = 1;
    int pos2 = lexer_getposition(squash_compiler->lexer);
    char* functIdentifierName = squash_compiler->currentToken->Value;
    if (strcmp(functIdentifierName, "main") == 0)
    {
        // Parse entry point main() function.
        astnode_t* mainFunct = ParseEntryPoint(squash_compiler, functIdentifierName);

        if (mainFunct != NULL)
        {
            return mainFunct;
        }
    }
    else
    {
        int pos3 = lexer_getposition(squash_compiler->lexer);
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

        if (squash_compiler->currentToken->Type == AST_Parenthesis && strcmp(squash_compiler->currentToken->Value, "(") == 0)
        {
            //currentToken = token1;

            LogInformation("ParsePrimaryExpression(): ParseFunctionDefinition int keyword function is: %s", functIdentifierName);

            //lexer.SetPosition(pos2);
            astnode_t* functDefNode = ParseFunctionDefinition(squash_compiler, AST_Int, functIdentifierName);
            if (functDefNode != NULL)
            {
                return functDefNode;
            }
        }
        else
        {
            lexer_setposition(squash_compiler->lexer, pos3);
        }

        if (!rememberLocation)
        {
            //lexer.SetPosition(pos);
            lexer_setposition(squash_compiler->lexer, pos2);
            token_t* tok = squash_compiler->currentToken;

            astnode_t* varDefineNode = ParseVariableDefine(squash_compiler, AST_Int);
            if (varDefineNode != NULL)
            {
                astnode_t* left = ParseExpression(squash_compiler, 0);
                varDefineNode->Left = left;
                parseEndStatement(squash_compiler, varDefineNode);
                return varDefineNode;
            }
            else
            {
                lexer_setposition(squash_compiler->lexer, pos);
                astnode_t* varDecla = ParseVariableDeclaration(squash_compiler, AST_Int);

                if (varDecla != NULL)
                {
                    return varDecla;
                }
            }
            //return null;
        }
        // functIdentifierName
    }
    (*retFlag) = 0;
    return NULL;
}

astnode_t* number_parse(struct SquashCompiler* squash_compiler)
{
    astnode_t* left = NULL;
    astnode_t* right = NULL;
    astnode_t* numASTNode = ast_node_new();

    char* input_str = squash_compiler->currentToken->Value;
    int num;
    double double_num;
    float floating_num;
    enum ASTNodeValueType value_type = AST_VALUE_UNDEFINED;

    // Attempt to parse as an integer.
    if (sscanf(input_str, "%d", &num) == 1)
    {
        value_type = AST_VALUE_INT;
    }
    else
    {
        // Attempt to parse as a double.
        if (sscanf(input_str, "%lf", &double_num) == 1)
        {
            value_type = AST_VALUE_DOUBLE;
        }
        else
        {
            if (tryParseFloat(input_str, &floating_num))
            {
                value_type = AST_VALUE_FLOAT;
            }
        }
    }

    //TODO: detect primative type for current token value and specify it directly so AST_Number ASTs can correctly parse type as needed.
    ast_node_init_bt(&numASTNode, AST_Number, squash_compiler->currentToken->Value, value_type, left, right);
    //numASTNode = new ASTNode(ASTNodeType.Number, currentToken.Value, left, right);

    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
    ParseEndOfFunction(squash_compiler);

    return numASTNode;
}

astnode_t* int_define_parse(struct SquashCompiler* squash_compiler, int pos, int* retFlag)
{
    (*retFlag) = 1;
    //lexer.SetPosition(pos);
    token_t* tok = squash_compiler->currentToken;

    astnode_t* varDefineNode = ParseVariableDefine(squash_compiler, AST_Int);
    if (varDefineNode != NULL)
    {
        astnode_t* left = ParseExpression(squash_compiler, 0);
        varDefineNode->Left = left;
        parseEndStatement(squash_compiler, varDefineNode);
        return varDefineNode;
    }
    else
    {
        lexer_setposition(squash_compiler->lexer, pos);
        astnode_t* varDecla = ParseVariableDeclaration(squash_compiler, AST_Int);

        if (varDecla != NULL)
        {
            return varDecla;
        }
    }
    (*retFlag) = 0;
    return NULL;
}

astnode_t* int_define2_parse(struct SquashCompiler* squash_compiler)
{
    //lexer.SetPosition(pos);
    token_t* tok = squash_compiler->currentToken;

    astnode_t* varDefineNode = ParseVariableDefine(squash_compiler, AST_Int);
    if (varDefineNode != NULL)
    {
        astnode_t* left = ParseExpression(squash_compiler, 0);
        if (left != NULL)
        {
            varDefineNode->Left = left;
        }

        parseEndStatement(squash_compiler, varDefineNode);
        return varDefineNode;
    }
    else
    {
        //lexer.SetPosition(pos);

        astnode_t* varDecla = ParseVariableDeclaration(squash_compiler, AST_Int);

        if (varDecla != NULL)
        {
            return varDecla;
        }
    }
    return NULL;
}

astnode_t* intkeyword_parse(struct SquashCompiler* squash_compiler, int* retFlag2)
{
    (*retFlag2) = 1;
    bool rememberLocation = false;
    int pos = lexer_getposition(squash_compiler->lexer);
    //Token token1 = lexer.GetNextToken();
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Whitespace)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Identifier)
        {
            int retFlag;
            astnode_t* retVal = identifier_parse(squash_compiler, rememberLocation, pos, &retFlag);
            if (retFlag == 1) return retVal;
        }
        else if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_NumberEntry)
        {
            return number_parse(squash_compiler);
        }
        else
        {
            if (!rememberLocation)
            {
                int retFlag;
                astnode_t* retVal = int_define_parse(squash_compiler, pos, &retFlag);
                if (retFlag == 1) return retVal;
            }
        }
    }
    else
    {

    }
    if (!rememberLocation)
    {
        return int_define2_parse(squash_compiler);
    }
    (*retFlag2) = 0;
    return NULL;
}

astnode_t* stringkeyword_parse(struct SquashCompiler* squash_compiler, int* retFlag)
{
    (*retFlag) = 1;
    int pos = lexer_getposition(squash_compiler->lexer);
    token_t* token1 = GetNextToken(squash_compiler->lexer);
    if (token1 != NULL && token1->Type == AST_Whitespace)
    {
        token1 = GetNextToken(squash_compiler->lexer);
        if (token1 != NULL && token1->Type == AST_Identifier)
        {
            int pos2 = lexer_getposition(squash_compiler->lexer);
            char* functIdentifierName = token1->Value;

            astnode_t* functDefNode = ParseFunctionDefinition(squash_compiler, AST_String, functIdentifierName);

            if (functDefNode != NULL)
            {
                return functDefNode;
            }
        }
    }
    lexer_setposition(squash_compiler->lexer, pos);

    astnode_t* varDefineNode = ParseVariableDefine(squash_compiler, AST_String);
    if (varDefineNode != NULL)
    {
        astnode_t* left = ParseExpression(squash_compiler, 0);
        varDefineNode->Left = left;
        parseEndStatement(squash_compiler, varDefineNode);
        return varDefineNode;
    }
    else
    {
        lexer_setposition(squash_compiler->lexer, pos);

        astnode_t* varDecla = ParseVariableDeclaration(squash_compiler, AST_String);

        if (varDecla != NULL)
        {
            return varDecla;
        }
    }
    (*retFlag) = 0;
    return NULL;
}

astnode_t* voidkeyword_parse(struct SquashCompiler* squash_compiler, int* retFlag)
{
    (*retFlag) = 1;
    int pos = lexer_getposition(squash_compiler->lexer);
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Whitespace)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Identifier)
        {
            int pos2 = lexer_getposition(squash_compiler->lexer);
            char* functIdentifierName = squash_compiler->currentToken->Value;

            astnode_t* functDefNode = ParseFunctionDefinition(squash_compiler, AST_Void, functIdentifierName);

            if (functDefNode != NULL)
            {
                return functDefNode;
            }
        }
    }
    lexer_setposition(squash_compiler->lexer, pos);

    astnode_t* varDefineNode = ParseVariableDefine(squash_compiler, AST_Void);
    if (varDefineNode != NULL)
    {
        astnode_t* left = ParseExpression(squash_compiler, 0);
        varDefineNode->Left = left;
        parseEndStatement(squash_compiler, varDefineNode);
        return varDefineNode;
    }
    else
    {
        lexer_setposition(squash_compiler->lexer, pos);

        astnode_t* varDecla = ParseVariableDeclaration(squash_compiler, AST_Void);

        if (varDecla != NULL)
        {
            return varDecla;
        }
    }
    (*retFlag) = 0;
    return NULL;
}

astnode_t* numberentry_parse(struct SquashCompiler* squash_compiler, token_t* token)
{
    astnode_t* left = NULL;
    astnode_t* right = NULL;

    char* input_str = squash_compiler->currentToken->Value;
    int num;
    double double_num;
    float floating_num;
    enum ASTNodeValueType value_type = AST_VALUE_UNDEFINED;

    // Attempt to parse as an integer.
    if (sscanf(input_str, "%d", &num) == 1)
    {
        value_type = AST_VALUE_INT;
    }
    else
    {
        // Attempt to parse as a double.
        if (sscanf(input_str, "%lf", &double_num) == 1)
        {
            value_type = AST_VALUE_DOUBLE;
        }
        else
        {
            if (tryParseFloat(input_str, &floating_num))
            {
                value_type = AST_VALUE_FLOAT;
            }
        }
    }

    astnode_t* numNode = ast_node_new();
    ast_node_init_bt(&numNode, AST_Number, token->Value, value_type, left, right);

    //ASTNode numNode = new ASTNode(ASTNodeType.Number, token.Value, left, right);
    char* ast_str_num = ast_tostring(numNode);

    LogInformation("ParsePrimaryExpression(): AST Number node created. %s", ast_str_num);

    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
    return numNode;
}

astnode_t* identifier2_parse(token_t* token, struct SquashCompiler* squash_compiler)
{
    char* identifierName = token->Value;

    LogInformation("ParsePrimaryExpression(): Identifier found '%s'", identifierName);

    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

    if (squash_compiler->currentToken->Type == AST_Parenthesis && strcmp(squash_compiler->currentToken->Value, "(") == 0)
    {
        LogInformation("ParsePrimaryExpression(): Parsing function call for identifier '%s'", identifierName);

        // Handle function call
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past "("
        list_t* arguments = ParseFunctionArguments(squash_compiler); // List<ASTNode>

        //foreach(ASTNode arg in arguments)
        //{
        //    Logger.Log.LogInformation("ParsePrimaryExpression(): function argument for function call: " + arg.ToString());
        //}

        // Lookup function in symbol table and generate corresponding ASTNode
        if (!SymbolTable_FunctionHasKey(squash_compiler->symbolTable, identifierName))
        {
            SymbolTable_DefineFunction(squash_compiler->symbolTable, identifierName, NULL);
        }
        FunctionSymbol_t* functionSymbol = SymbolTable_LookupFunction(squash_compiler->symbolTable, identifierName);
        astnode_t* functNode = ast_node_new();

        ast_node_init_funct(&functNode, AST_FunctionCall, identifierName, functionSymbol, arguments);

        //return new ASTNode(ASTNodeType.FunctionCall, identifierName, functionSymbol, arguments);

        return functNode;
    }
    else
    {
        LogInformation("ParsePrimaryExpression(): Parsing variable given identifier: '%s'", identifierName);

        // Handle variable
        if (!SymbolTable_VariableHasKey(squash_compiler->symbolTable, identifierName))
        {
            SymbolTable_DefineVariableC(squash_compiler->symbolTable, AST_VarAutomatic, identifierName, NULL);
        }
        VariableSymbol_t* variableSymbol = SymbolTable_LookupVariable(squash_compiler->symbolTable, identifierName);

        astnode_t* varNode = ast_node_new();

        // void ast_node_init_var(astnode_t** node, enum ASTNodeType type, char* value, enum ASTNodeValueType value_type, VariableSymbol_t* variableSymbol);
        ast_node_init_var(&varNode, AST_Variable, identifierName, AST_VALUE_STRING, variableSymbol);
        //ASTNode varNode = new ASTNode(ASTNodeType.Variable, identifierName, variableSymbol);

        token_t* before = squash_compiler->currentToken;
        //currentToken = lexer.GetNextToken(); // Skip past identifier token.
        if (squash_compiler->currentToken->Type == AST_Assignment && strcmp(squash_compiler->currentToken->Value, "=") == 0)
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

            astnode_t* left = ParseExpression(squash_compiler, 0);

            astnode_t* right = ast_node_new();

            ast_node_init_bt(&right, AST_VariableAssignment, before->Value, AST_VALUE_STRING, left, varNode);

            //astnode_t* right = new ASTNode(ASTNodeType.VariableAssignment, before.Value, left, varNode);

            char* ast_str_left = ast_tostring(left);
            char* ast_str_right = ast_tostring(right);

            LogInformation("ParsePrimaryExpression(): variable assignment lhs: '%s' rhs: '%s'", ast_str_left, ast_str_right);

            return right;
        }
        else if (varNode != NULL && squash_compiler->currentToken->Type == AST_Operator)
        {
            LogInformation("ParsePrimaryExpression(): variable usage in expression or statement #1.");
            return varNode;
        }
        else if (varNode != NULL)
        {
            LogInformation("ParsePrimaryExpression(): variable usage in expression or statement #2.");
            return varNode;
        }
        else
        {
            return NULL;
        }
    }
    return NULL;
}

astnode_t* parenthesis_parse(struct SquashCompiler* squash_compiler)
{
    LogInformation("ParsePrimaryExpression(): Handling parenthesis");
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past "("
    astnode_t* node = ParseExpression(squash_compiler, 0);
    if (squash_compiler->currentToken != NULL && strcmp(squash_compiler->currentToken->Value, ")") == 0)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past ")"
    }
    else
    {
        //throw new Exception("Missing closing parenthesis.");
    }
    return node;
}

void variable_parse(struct SquashCompiler* squash_compiler)
{
    char* identifierName = squash_compiler->currentToken->Value;
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past variable.

    // TODO: Check if there is a variable assignment unary operator, 
    //       otherwise skip over past potential variable assignment or skip past token.
}

void whitespace_parse(struct SquashCompiler* squash_compiler)
{
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past whitespace character.
}

void semicolon_parse(struct SquashCompiler* squash_compiler)
{
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past semicolon character.
}

astnode_t* artifacts_parse(struct SquashCompiler* squash_compiler, token_t* token, int* retFlag)
{
    (*retFlag) = 1;
    if (squash_compiler->currentToken->Type == AST_ReturnKeyword)
    {
        return returnkeyword_parse(squash_compiler);
    }
    else if (squash_compiler->currentToken->Type == AST_VarKeyword)
    {
        return varkeyword_parse(squash_compiler);
    }
    else if (squash_compiler->currentToken->Type == AST_DoubleKeyword)
    {
        int retFlag;
        astnode_t* retVal = doublekeyword_parse(squash_compiler, &retFlag);
        if (retFlag == 1) return retVal;
    }
    else if (squash_compiler->currentToken->Type == AST_IntKeyword)
    {
        int retFlag;
        astnode_t* retVal = intkeyword_parse(squash_compiler, &retFlag);
        if (retFlag == 1) return retVal;
    }
    else if (squash_compiler->currentToken->Type == AST_StringKeyword)
    {
        int retFlag;
        astnode_t* retVal = stringkeyword_parse(squash_compiler, &retFlag);
        if (retFlag == 1) return retVal;
    }
    else if (squash_compiler->currentToken->Type == AST_VoidKeyword)
    {
        int retFlag;
        astnode_t* retVal = voidkeyword_parse(squash_compiler, &retFlag);
        if (retFlag == 1) return retVal;
    }
    else if (squash_compiler->currentToken->Type == AST_NumberEntry)
    {
        return numberentry_parse(squash_compiler, token);
    }
    else if (squash_compiler->currentToken->Type == AST_Identifier)
    {
        return identifier2_parse(token, squash_compiler);
    }
    else if (squash_compiler->currentToken->Type == AST_Parenthesis && strcmp(squash_compiler->currentToken->Value, "(") == 0)
    {
        return parenthesis_parse(squash_compiler);
    }
    else if (squash_compiler->currentToken->Type == AST_Variable)
    {
        variable_parse(squash_compiler);
    }
    else if (squash_compiler->currentToken->Type == AST_Whitespace)
    {
        whitespace_parse(squash_compiler);
    }
    else if (squash_compiler->currentToken->Type == AST_SemiColon)
    {
        semicolon_parse(squash_compiler);
    }
    (*retFlag) = 0;
    return NULL;
}

astnode_t* ParsePrimaryExpression(struct SquashCompiler* squash_compiler)
{
    token_t* token = squash_compiler->currentToken;

    if (squash_compiler->currentToken == NULL)
    {
        //LogError("ParsePrimaryExpression(): currentToken is null");
        return NULL;
    }

    LogInformation("ParsePrimaryExpression(): handle token types prior to handling");

    int retFlag;
    astnode_t* retVal = artifacts_parse(squash_compiler, token, &retFlag);
    if (retFlag == 1) return retVal;

    if (squash_compiler->currentToken == NULL)
    {
        return NULL;
    }
    if (squash_compiler->currentToken->Type == AST_SemiColon)
    {
        semicolon_parse(squash_compiler);

        return NULL;
    }

    if (squash_compiler->currentToken != NULL && strcmp(squash_compiler->currentToken->Value, ")") == 0)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past ")"
    }
    if (squash_compiler->currentToken != NULL && strcmp(squash_compiler->currentToken->Value, "{") == 0)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past "{"
    }
    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_ReturnKeyword)
    {
        return ParseExpression(squash_compiler, 0);
    }
    if (squash_compiler->currentToken != NULL && strcmp(squash_compiler->currentToken->Value, "}") == 0)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past "}"
    }
    if (squash_compiler->currentToken != NULL &&
        (squash_compiler->currentToken->Type == AST_IntKeyword
            || squash_compiler->currentToken->Type == AST_StringKeyword
            || squash_compiler->currentToken->Type == AST_VarKeyword
            || squash_compiler->currentToken->Type == AST_VoidKeyword
            || squash_compiler->currentToken->Type == AST_DoubleKeyword))
    {
        return ParseExpression(squash_compiler, 0);
    }

    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_SemiColon)
    {
        semicolon_parse(squash_compiler);
    }

    if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_ReturnKeyword)
    {
        return ParseExpression(squash_compiler, 0);
    }

    if (squash_compiler->currentToken != NULL)
    {
        LogError("Invalid primary expression. Position: %d", squash_compiler->currentToken->Position);
        //throw new Exception("Invalid primary expression. Position: " + currentToken.Position.ToString());
    }
    else
    {
        //Logger.Log.LogError("Invalid primary expression.");
        //throw new Exception("Invalid primary expression.");
        //return null;
    }
    return NULL;
}

void ParseEndOfFunction(struct SquashCompiler* squash_compiler)
{
    if (squash_compiler->currentToken->Type == AST_SemiColon)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

        if (squash_compiler->currentToken->Type == AST_CurleyBrace && strcmp(squash_compiler->currentToken->Value, "}") == 0)
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        }
    }
}

astnode_t* ParseEntryPoint(struct SquashCompiler* squash_compiler, char* functIdentifierName)
{
    //Token token1;
    bool rememberLocation = false;

    LogInformation("ParsePrimaryExpression(): parsing entry point main() function");

    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
    if (squash_compiler->currentToken->Type == AST_Parenthesis && strcmp(squash_compiler->currentToken->Value, "(") == 0)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        if (squash_compiler->currentToken->Type == AST_VoidKeyword && strcmp(squash_compiler->currentToken->Value, "void") == 0)
        {
            //currentToken = lexer.GetNextToken();

            //if(currentToken.Type == TokenType.Whitespace)
            {
                squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
                if (squash_compiler->currentToken->Type == AST_Parenthesis && strcmp(squash_compiler->currentToken->Value, ")") == 0)
                {
                    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
                    if (squash_compiler->currentToken->Type == AST_CurleyBrace && strcmp(squash_compiler->currentToken->Value, "{") == 0)
                    {
                        rememberLocation = true;

                        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
                        // Is Main entry point function.

                        if (squash_compiler->currentToken->Type == AST_ReturnKeyword)
                        {
                            //urrentToken = token1;
                        }
                        else
                        {
                            // if double etc.
                            //currentToken = token1;
                        }
                        astnode_t* left = ParseStatements(squash_compiler);
                        astnode_t* entryPointNode = ast_node_new();
                        ast_node_init_bt(&entryPointNode, AST_FunctionDefinition, functIdentifierName, AST_VALUE_STRING, left, NULL);
                        //astnode_t* entryPointNode = new ASTNode(ASTNodeType.FunctionDefinition, functIdentifierName, left, null);
                        
                        entryPointNode->IsFunctionDefinition = true;
                        if (entryPointNode->FunctionBody == NULL)
                        {
                            entryPointNode->FunctionBody = list_new(); // new List<ASTNode>()
                        }
                        if (left != NULL)
                        {
                            list_enqueue(entryPointNode->FunctionBody, (void*)left);
                        }

                        char* ast_str = ast_tostring(entryPointNode);
                        LogInformation("ParseEntryPoint(): entry point node parsed '%s'", ast_str);

                        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_CurleyBrace && strcmp(squash_compiler->currentToken->Value, "}") == 0)
                        {
                            LogInformation("ParseEntryPoint(): entry point end closing curley brace found");

                            //return left;
                        }
                        else
                        {
                            //Logger.Log.LogWarning("ParseEntryPoint(): Missing matching '}' curley brace for function definition. Might have already parsed it.");
                            //return left;
                            //throw new Exception("Missing matching '}' curley brace for function definition.");
                        }

                        return entryPointNode;
                    }
                }
            }
        }
    }
    return NULL;
}

astnode_t* ParseFunctionDefinition(struct SquashCompiler* squash_compiler, enum VarType retVarType, char* functIdentifierName)
{
    token_t* token1;

    list_t* args = ParseFunctionArgs(squash_compiler, retVarType); //List<ASTNode> args = ParseFunctionDefArguments(retVarType);

    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
    if (squash_compiler->currentToken == NULL || (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_CurleyBrace && strcmp(squash_compiler->currentToken->Value, "{") == 0))
    {
        //rememberLocation = true;

        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        // Is function with a definition.

        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_ReturnKeyword)
        {
            //squash_compiler->currentToken = token1;
        }
        else
        {
            // if double etc.
            //squash_compiler->currentToken = token1;
        }
        astnode_t* left = ParseStatements(squash_compiler);
        astnode_t* entryPointNode = ast_node_new();

        ast_node_init_bt(&entryPointNode, AST_FunctionDefinition, functIdentifierName, AST_VALUE_STRING, left, NULL);
        //ASTNode entryPointNode = new ASTNode(ASTNodeType.FunctionDefinition, functIdentifierName, left, null);

        entryPointNode->IsFunctionDefinition = true;
        entryPointNode->FunctionArguments = args;
        entryPointNode->FunctionReturnType = retVarType;
        if (entryPointNode->FunctionBody == NULL)
        {
            entryPointNode->FunctionBody = list_new(); //new List<ASTNode>();
        }
        if (left != NULL)
        {
            list_enqueue(entryPointNode->FunctionBody, (void*)left);
        }

        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_CurleyBrace && strcmp(squash_compiler->currentToken->Value, "}") == 0)
        {
            //return left;
        }
        else
        {
            //return left;
            //throw new Exception("Missing matching '}' curley brace for function definition.");
        }

        if (!SymbolTable_FunctionHasKey(squash_compiler->symbolTable, functIdentifierName))
        {
            SymbolTable_DefineFunction(squash_compiler->symbolTable, functIdentifierName, NULL);
        }

        return entryPointNode;
    }

    return NULL;
}

void ParseFunctionArg(struct SquashCompiler* squash_compiler, list_t* args, char* argType) // ref List<ASTNode> args
{
    squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
    if (squash_compiler->currentToken->Type == AST_Whitespace)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
        if (squash_compiler->currentToken->Type == AST_Identifier)
        {
            char* identifierName = squash_compiler->currentToken->Value;
            astnode_t* left = NULL;
            astnode_t* right = NULL;
            astnode_t* functionArg = ast_node_new();

            ast_node_init_bt2(&functionArg, AST_FunctionArg, argType, identifierName, AST_VALUE_STRING, left, right);
            //ASTNode functionArg = new ASTNode(ASTNodeType.FunctionArg, argType, identifierName, left, right);

            list_enqueue(args, (void*)functionArg); //args.Add(functionArg);

            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);
            if (squash_compiler->currentToken->Type == AST_Operator && strcmp(squash_compiler->currentToken->Value, ",") == 0)
            {

            }
        }
    }
}
list_t* ParseFunctionArgs(struct SquashCompiler* squash_compiler, enum VarType retVarType) // returns: List<ASTNode>
{
    list_t* args = list_new(); //List<ASTNode> args = new List<ASTNode>();
    while (squash_compiler->currentToken != NULL && (squash_compiler->currentToken->Type != AST_Parenthesis || strcmp(squash_compiler->currentToken->Value, ")") != 0))
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

        if (squash_compiler->currentToken->Type == AST_IntKeyword)
        {
            ParseFunctionArg(squash_compiler, args, "int");
        }
        else if (squash_compiler->currentToken->Type == AST_DoubleKeyword)
        {
            ParseFunctionArg(squash_compiler, args, "double");
        }
        else if (squash_compiler->currentToken->Type == AST_StringKeyword)
        {
            ParseFunctionArg(squash_compiler, args, "string");
        }
        if (squash_compiler->currentToken != NULL && (squash_compiler->currentToken->Type != AST_Parenthesis || strcmp(squash_compiler->currentToken->Value, ")") != 0))
        {
            //Logger.Log.LogWarning("ParseFunctionArgs(): Invalid function argument list.");
        }
    }
    return args;
}

list_t* ParseFunctionDefArguments(struct SquashCompiler* squash_compiler, enum VarType retVarType) // returns: List<ASTNode>
{
    list_t* arguments = list_new(); //List<ASTNode> arguments = new List<ASTNode>();



    while (squash_compiler->currentToken != NULL && (squash_compiler->currentToken->Type != AST_Parenthesis || strcmp(squash_compiler->currentToken->Value, ")") != 0))
    {
        astnode_t* expr = ParseExpression(squash_compiler, 0);
        if (expr != NULL)
        {
            list_enqueue(arguments, (void*)expr);
        }


        if (squash_compiler->currentToken != NULL && ((squash_compiler->currentToken->Type == AST_SemiColon)
            || (squash_compiler->currentToken->Type == AST_Operator && strcmp(squash_compiler->currentToken->Value, ",") == 0)))
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past "," or ";"
            if (squash_compiler->currentToken->Type == AST_CurleyBrace && strcmp(squash_compiler->currentToken->Value, "}") == 0)
            {
                squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past "}"
            }
        }
        else if (squash_compiler->currentToken != NULL && (squash_compiler->currentToken->Type != AST_Parenthesis || strcmp(squash_compiler->currentToken->Value, ")") != 0))
        {
            //Logger.Log.LogError("Invalid function argument list.");
            //throw new Exception("Invalid function argument list.");
        }
    }

    if (squash_compiler->currentToken != NULL)
    {
        if (strcmp(squash_compiler->currentToken->Value, ")") == 0)
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past ")"
        }
        else
        {
            //Logger.Log.LogError("Missing closing parenthesis in function call arguments.");
            //throw new Exception("Missing closing parenthesis in function call arguments.");
        }
    }

    return arguments;
}

list_t* ParseFunctionArguments(struct SquashCompiler* squash_compiler) // returns: List<ASTNode>
{
    list_t* arguments = list_new(); //List<ASTNode> arguments = new List<ASTNode>();

    while (squash_compiler->currentToken != NULL && (squash_compiler->currentToken->Type != AST_Parenthesis || strcmp(squash_compiler->currentToken->Value, ")") != 0))
    {
        astnode_t* expr = ParseExpression(squash_compiler, 0);
        if (expr != NULL)
        {
            list_enqueue(arguments, (void*)expr);
        }

        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Operator && strcmp(squash_compiler->currentToken->Value, ",") == 0)
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past ","
        }
        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_SemiColon)
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past ";"
        }
        if (squash_compiler->currentToken != NULL && (squash_compiler->currentToken->Type == AST_CurleyBrace && strcmp(squash_compiler->currentToken->Value, "}") == 0))
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past "}"
        }
        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Identifier)
        {
            return arguments;
        }
        if (squash_compiler->currentToken != NULL && (squash_compiler->currentToken->Type != AST_Parenthesis || strcmp(squash_compiler->currentToken->Value, ")") == 0))
        {
            //throw new Exception("Invalid function argument list.");
        }
    }

    if (squash_compiler->currentToken != NULL && strcmp(squash_compiler->currentToken->Value, ")") == 0)
    {
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move past ")"
    }
    else
    {
        //throw new Exception("Missing closing parenthesis in function call arguments.");
    }

    return arguments;
}

astnode_t* ParseExpression(struct SquashCompiler* squash_compiler, int precedence)
{
    LogInformation("ParseExpression(): precedence: '%d'", precedence);

    astnode_t* leftNode = ParsePrimaryExpression(squash_compiler); // Parse the left operand
    
    if (leftNode != NULL)
    {
        leftNode->Precedence = precedence;
        char* ast_str = ast_tostring(leftNode);

        LogInformation("ParseExpression(): precedence: %d, leftNode: %s", precedence, ast_str);
    }
    else
    {
        LogInformation("ParseExpression(): precedence: %d, leftNode: == null", precedence);
    }

    while (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Operator && (GetPrecedence(squash_compiler->currentToken->Value) >= precedence))
    {
        token_t* op = squash_compiler->currentToken;
        squash_compiler->currentToken = GetNextToken(squash_compiler->lexer);

        int nextPrecedence = GetPrecedence(squash_compiler->currentToken->Value);
        int precd;
        char* ast_str_left;
        char* ast_str_right;

        astnode_t* rightNode = ParseExpression(squash_compiler, nextPrecedence); // Parse the right operand with correct precedence

        // Handle associativity for right-associative operators
        while (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Operator && GetPrecedence(squash_compiler->currentToken->Value) == nextPrecedence)
        {
            precd = GetPrecedence(squash_compiler->currentToken->Value);

            LogInformation("ParseExpression(): precedence: %d, token: %s", precd, squash_compiler->currentToken->Value);
            rightNode = ParseExpression(squash_compiler, nextPrecedence);

            ast_str_right = ast_tostring(rightNode);
            LogInformation("ParseExpression(): precedence climbing to next rhs: %s", ast_str_right);
        }

        // Handle parentheses
        if (squash_compiler->currentToken != NULL && squash_compiler->currentToken->Type == AST_Parenthesis && strcmp(squash_compiler->currentToken->Value, ")") == 0)
        {
            squash_compiler->currentToken = GetNextToken(squash_compiler->lexer); // Move to the next token
        }

        if ((strcmp(op->Value, "--") == 0 || strcmp(op->Value, "++") == 0) && IsUnaryOperator(squash_compiler))
        {
            leftNode = ast_node_new();
            ast_node_init_bt(&leftNode, AST_UNARY_OP, op->Value, AST_VALUE_STRING, NULL, rightNode);

            //ast.AddUnaryOperator(op.Value, rightNode);
            //leftNode = new ASTNode(ASTNodeType.UNARY_OP, op.Value, null, rightNode);

            ast_str_left = ast_tostring(leftNode);

            LogInformation("ParseExpression(): precedence: '%d', Is Unary Operator, %s", precedence, ast_str_left);
        }
        else
        {
            astnode_t* p = leftNode;
            leftNode = ast_node_new();
            ast_node_init_bt(&leftNode, AST_BIN_OP, op->Value, AST_VALUE_STRING, p, rightNode);
            //leftNode = new ASTNode(ASTNodeType.BIN_OP, op.Value, leftNode, rightNode);

            ast_str_left = ast_tostring(leftNode);

            LogInformation("ParseExpression(): precedence: '%d', Is Binary Operator, %s", precedence, ast_str_left);
        }
    }

    return leftNode;
}


int GetPrecedence(char* op)
{
    if (strcmp(op, "+") == 0) 
    {
        return 1;
    }
    else if (strcmp(op, "-") == 0)
    {
        return 1;
    }
    else if (strcmp(op, "*") == 0)
    {
        return 2;
    }
    else if (strcmp(op, "/") == 0)
    {
        return 2;
    }
    else 
    {
        return 0;
    }
    //switch (*op)
    //{
    //case '+':
    //case '-':
    //    return 1;
    //case '*':
    //case '/':
    //    return 2;
    //default:
    //    return 0;
    //}
}

bool IsUnaryOperator(struct SquashCompiler* squash_compiler)
{
    return squash_compiler->currentToken != NULL
        && squash_compiler->currentToken->Type == AST_Operator
        && (strcmp(squash_compiler->currentToken->Value, "--") == 0 || strcmp(squash_compiler->currentToken->Value, "++") == 0);
}

bool IsBinaryOperator(struct SquashCompiler* squash_compiler)
{
    return squash_compiler->currentToken != NULL
        && squash_compiler->currentToken->Type == AST_Operator
        && (strcmp(squash_compiler->currentToken->Value, "-") == 0
            || strcmp(squash_compiler->currentToken->Value, "+") == 0
            || strcmp(squash_compiler->currentToken->Value, "*") == 0
            || strcmp(squash_compiler->currentToken->Value, "/") == 0);
}