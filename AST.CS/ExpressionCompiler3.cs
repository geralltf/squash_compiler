//public Token GetNextToken()
//{
//    if (currentPos >= input.Length)
//    {
//        return null;
//    }

//    while (char.IsWhiteSpace(input[currentPos]))
//    {
//        currentPos++;
//    }

//    if (char.IsDigit(input[currentPos]))
//    {
//        StringBuilder sb = new StringBuilder();
//        while (currentPos < input.Length && (char.IsDigit(input[currentPos]) || input[currentPos] == '.'))
//        {
//            sb.Append(input[currentPos]);
//            currentPos++;
//        }
//        return new Token(TokenType.Number, sb.ToString());
//    }

//    if (char.IsLetter(input[currentPos]))
//    {
//        StringBuilder sb = new StringBuilder();
//        while (currentPos < input.Length && (char.IsLetterOrDigit(input[currentPos]) || input[currentPos] == '_'))
//        {
//            sb.Append(input[currentPos]);
//            currentPos++;
//        }

//        return keywordDict.TryGetValue(sb.ToString(), out TokenType type) ?
//            new Token(type, sb.ToString()) : new Token(TokenType.Variable, sb.ToString());
//    }

//    if (input[currentPos] == '(' || input[currentPos] == ')') // Handle parentheses
//    {
//        Token token = new Token(TokenType.Parenthesis, input[currentPos].ToString());
//        //Advance();
//        currentPos++;
//        return token;
//    }


//    switch (input[currentPos])
//    {
//        case '(':
//            currentPos++;
//            return new Token(TokenType.LParen, "(");
//        case ')':
//            currentPos++;
//            return new Token(TokenType.RParen, ")");
//        case ',':
//            currentPos++;
//            return new Token(TokenType.Comma, ",");
//        default:
//            currentPos++;
//            return new Token(TokenType.Operator, input[currentPos - 1].ToString());
//    }
//}

//public void CompileExpression()
//{
//    while (currentToken != null)
//    {
//        if (currentToken.Type == TokenType.Variable)
//        {
//            // Handle variable assignment
//            Console.WriteLine($"Variable assignment found: {currentToken.Value}");
//            // Code to handle variable assignment goes here
//            currentToken = lexer.GetNextToken();
//        }
//        else if (currentToken.Type == TokenType.Function)
//        {
//            // Handle function call
//            Console.WriteLine($"Function call found: {currentToken.Value}");
//            // Code to handle function call goes here
//            currentToken = lexer.GetNextToken();
//        }
//        else
//        {
//            // Handle arithmetic expression
//            ASTNode expression = ParseExpression(0, rootAST);

//            GenerateCode(expression);
//        }
//    }
//}

//private void ParseExpression(int precedence)
//{
//    // Handle expression parsing using precedence climbing
//    // Code for parsing expressions goes here
//}

//private void ParseExpression(int precedence)
//{
//    // Handle expression parsing using precedence climbing
//    // Code for parsing expressions goes here

//    // Base case: get the next token
//    Token left = currentToken;
//    currentToken = lexer.GetNextToken();

//    // Loop to handle binary operators and precedence
//    while (currentToken != null && currentToken.Type == TokenType.Operator && (GetPrecedence(currentToken.Value) >= precedence))
//    {
//        Token op = currentToken;
//        currentToken = lexer.GetNextToken();

//        Token right = currentToken;
//        currentToken = lexer.GetNextToken();

//        // Recursively parse the right side of the expression
//        int nextPrecedence = GetPrecedence(op.Value);
//        if (op.Value == "-" && IsUnaryOperator())
//        {
//            ParseExpression(6);
//        }
//        else
//        {
//            ParseExpression(nextPrecedence);
//        }

//        // Code to generate code for the op goes here
//        Console.WriteLine("OP: " + op.Value + ", OP Type: " + op.Type.ToString() + ", ");
//    }
//}

//private ASTNode ParsePrimaryExpression()
//{
//    Token token = currentToken;

//    if (currentToken.Type == TokenType.Number)
//    {
//        currentToken = lexer.GetNextToken();
//        return new ASTNode(token.Value, null, null);
//    }
//    else if (currentToken.Type == TokenType.Identifier)
//    {
//        currentToken = lexer.GetNextToken();
//        return new ASTNode(token.Value, null, null);
//    }
//    else if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
//    {
//        // Handle parentheses
//        currentToken = lexer.GetNextToken(); // Move past "("
//        ASTNode node = ParseExpression(0, rootAST); // Parse the expression within the parentheses
//        if (currentToken.Value == ")")
//        {
//            currentToken = lexer.GetNextToken(); // Move past ")"
//        }
//        else
//        {
//            throw new Exception("Missing closing parenthesis.");
//        }
//        return node;
//    }

//    throw new Exception("Invalid primary expression.");
//}

//public class SymbolTable
//{
//    private Dictionary<string, int> variables;

//    public SymbolTable()
//    {
//        variables = new Dictionary<string, int>();
//    }

//    public void AddVariable(string name, int value)
//    {
//        variables[name] = value;
//    }

//    public int GetVariableValue(string name)
//    {
//        return variables.ContainsKey(name) ? variables[name] : 0;
//    }
//}

