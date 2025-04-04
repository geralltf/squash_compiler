using System;
using System.Collections.Generic;

public class Token
{
    public string Type { get; set; }
    public object Value { get; set; }
}

public class Lexer
{
    private readonly string text;
    private int position;

    public Lexer(string text)
    {
        this.text = text;
        position = 0;
    }

    public Token GetNextToken()
    {
        if (position >= text.Length)
        {
            return new Token { Type = "EOF" };
        }

        char currentChar = text[position];

        if (char.IsDigit(currentChar))
        {
            position++;
            return new Token { Type = "INTEGER", Value = int.Parse(currentChar.ToString()) };
        }
        else if (char.IsLetter(currentChar))
        {
            position++;
            return new Token { Type = "IDENTIFIER", Value = currentChar.ToString() };
        }
        else if (new char[] { '+', '-', '*', '/', '(', ')' }.Contains(currentChar))
        {
            position++;
            return new Token { Type = "OPERATOR", Value = currentChar };
        }
        else if (char.IsWhiteSpace(currentChar))
        {
            position++;
            return new Token { Type = "SPACE", Value = currentChar };
        }
        else
        {
            throw new Exception("Invalid character");
        }
    }
}

public class ASTNode
{
    public string Type { get; set; }
    public object Value { get; set; }
    public List<ASTNode> Children { get; set; }

    public override string ToString()
    {
        string children = "";
        if (Children != null && Children.Count > 0)
        {
            foreach (ASTNode child in Children)
            {
                children += "\t" + child.ToString();
            }
        }
        return base.ToString() + " type: " + Type + ", value: " + Value.ToString() + "\n" + children;
    }
}

public class Parser
{
    private readonly Lexer lexer;
    private Token currentToken;

    public Parser(Lexer lexer)
    {
        this.lexer = lexer;
        currentToken = this.lexer.GetNextToken();
    }

    public void Eat(string tokenType)
    {
        if (currentToken.Type == tokenType)
        {
            currentToken = lexer.GetNextToken();
        }
        else
        {
            throw new Exception("Unexpected token");
        }
    }

    public ASTNode Expr()
    {
        var node = Factor();

        while (currentToken.Type == "OPERATOR")
        {
            var token = currentToken;
            Eat("OPERATOR");
            var binOpNode = new ASTNode { Type = "BIN_OP", Value = token.Value };
            binOpNode.Children = new List<ASTNode> { node, Factor() };
            node = binOpNode;
        }

        return node;
    }

    public ASTNode Factor()
    {
        var token = currentToken;

        if (token.Type == "INTEGER")
        {
            Eat("INTEGER");
            return new ASTNode { Type = "INTEGER", Value = token.Value };
        }
        else if (token.Type == "IDENTIFIER")
        {
            Eat("IDENTIFIER");
            return new ASTNode { Type = "IDENTIFIER", Value = token.Value };
        }
        else if (token.Type == "OPERATOR" && (char)token.Value == '(')
        {
            Eat("OPERATOR");
            var node = Expr();
            Eat("OPERATOR");
            return node;
        }
        else
        {
            throw new Exception("Invalid factor");
        }
    }
}

public class Compiler
{
    private readonly string text;

    public Compiler(string text)
    {
        this.text = text;
    }

    public ASTNode Compile()
    {
        Lexer lexer = new Lexer(text);
        Parser parser = new Parser(lexer);

        return parser.Expr();
    }
}


//using System;
//using System.Collections.Generic;
//using System.Text;

//public enum TokenType
//{
//    Number,
//    Variable,
//    Function,
//    Operator,
//    Parenthesis,
//    Identifier,
//    //LParen,
//    //RParen,
//    Comma
//}

//public class Token
//{
//    public TokenType Type { get; set; }
//    public string Value { get; set; }

//    public Token(TokenType type, string value)
//    {
//        Type = type;
//        Value = value;
//    }
//}

//public class Lexer
//{
//    private string input;
//    private int currentPos;
//    private char currentChar;

//    private Dictionary<string, TokenType> keywordDict;

//    public List<Token> tokens;
//    public int tokenIndex = 0;

//    public Lexer(string input)
//    {
//        this.input = input;
//        this.tokens = Lex(input);
//        this.currentPos = 0;
//        this.tokenIndex = 0;
//        this.keywordDict = new Dictionary<string, TokenType>
//        {
//            { "sin", TokenType.Function },
//            { "cos", TokenType.Function },
//            { "tan", TokenType.Function }
//        };
//    }

//    public void Advance()
//    {
//        //tokenIndex++;
//        currentPos++;
//        if (currentPos < input.Length)
//        {
//            currentChar = input[currentPos];
//        }
//        else
//        {
//            currentChar = '\0';
//        }
//    }

//    public List<Token> Lex(string input)
//    {
//        this.input = input;
//        List<Token> tokens = new List<Token>();

//        while (currentPos < input.Length)
//        {
//            char currentChar = input[currentPos];

//            if (char.IsDigit(currentChar))
//            {
//                string number = currentChar.ToString();
//                currentPos++;

//                while (currentPos < input.Length && char.IsDigit(input[currentPos]))
//                {
//                    number += input[currentPos];
//                    currentPos++;
//                }

//                tokens.Add(new Token(TokenType.Number, number));
//            }
//            else if (currentChar == '+')
//            {
//                tokens.Add(new Token(TokenType.Operator, "+"));
//                currentPos++;
//            }
//            else if (currentChar == '-')
//            {
//                tokens.Add(new Token(TokenType.Operator, "-"));
//                currentPos++;
//            }
//            else if (char.IsLetter(currentChar))
//            {
//                string variable = currentChar.ToString();
//                currentPos++;

//                while (currentPos < input.Length && (char.IsLetterOrDigit(input[currentPos]) || input[currentPos] == '_'))
//                {
//                    variable += input[currentPos];
//                    currentPos++;
//                }

//                tokens.Add(new Token(TokenType.Variable, variable));
//            }
//            else
//            {
//                currentPos++; // Skip whitespace or invalid characters
//            }
//        }

//        return tokens;
//    }


//    public Token GetNextToken()
//    {
//        while (currentChar != '\0')
//        {
//            if (char.IsDigit(currentChar))
//            {
//                return new Token(TokenType.Number, ParseNumber());
//            }
//            else if (char.IsLetter(currentChar))
//            {
//                return new Token(TokenType.Identifier, ParseIdentifier());
//            }
//            else if (currentChar == '+' || currentChar == '-' || currentChar == '*' || currentChar == '/')
//            {
//                Token token = new Token(TokenType.Operator, currentChar.ToString());
//                Advance();
//                return token;
//            }
//            else if (currentChar == '(' || currentChar == ')') // Handle parentheses
//            {
//                Token token = new Token(TokenType.Parenthesis, currentChar.ToString());
//                Advance();
//                return token;
//            }
//            else
//            {
//                throw new Exception("Invalid character found in input.");
//            }
//        }

//        return null;
//    }

//    private string ParseNumber()
//    {
//        StringBuilder number = new StringBuilder();

//        while (currentChar != '\0' && char.IsDigit(currentChar))
//        {
//            number.Append(currentChar);
//            Advance();
//        }

//        return number.ToString();
//    }

//    private string ParseIdentifier()
//    {
//        StringBuilder identifier = new StringBuilder();

//        while (currentChar != '\0' && (char.IsLetterOrDigit(currentChar) || currentChar == '_'))
//        {
//            identifier.Append(currentChar);
//            Advance();
//        }

//        return identifier.ToString();
//    }



//    //public Token GetNextToken()
//    //{
//    //    if (currentPos >= input.Length)
//    //    {
//    //        return null;
//    //    }

//    //    while (char.IsWhiteSpace(input[currentPos]))
//    //    {
//    //        currentPos++;
//    //    }

//    //    if (char.IsDigit(input[currentPos]))
//    //    {
//    //        StringBuilder sb = new StringBuilder();
//    //        while (currentPos < input.Length && (char.IsDigit(input[currentPos]) || input[currentPos] == '.'))
//    //        {
//    //            sb.Append(input[currentPos]);
//    //            currentPos++;
//    //        }
//    //        return new Token(TokenType.Number, sb.ToString());
//    //    }

//    //    if (char.IsLetter(input[currentPos]))
//    //    {
//    //        StringBuilder sb = new StringBuilder();
//    //        while (currentPos < input.Length && (char.IsLetterOrDigit(input[currentPos]) || input[currentPos] == '_'))
//    //        {
//    //            sb.Append(input[currentPos]);
//    //            currentPos++;
//    //        }

//    //        return keywordDict.TryGetValue(sb.ToString(), out TokenType type) ?
//    //            new Token(type, sb.ToString()) : new Token(TokenType.Variable, sb.ToString());
//    //    }

//    //    if (input[currentPos] == '(' || input[currentPos] == ')') // Handle parentheses
//    //    {
//    //        Token token = new Token(TokenType.Parenthesis, input[currentPos].ToString());
//    //        //Advance();
//    //        currentPos++;
//    //        return token;
//    //    }


//    //    switch (input[currentPos])
//    //    {
//    //        case '(':
//    //            currentPos++;
//    //            return new Token(TokenType.LParen, "(");
//    //        case ')':
//    //            currentPos++;
//    //            return new Token(TokenType.RParen, ")");
//    //        case ',':
//    //            currentPos++;
//    //            return new Token(TokenType.Comma, ",");
//    //        default:
//    //            currentPos++;
//    //            return new Token(TokenType.Operator, input[currentPos - 1].ToString());
//    //    }
//    //}
//}

//public class FunctionSymbol
//{
//    public string Name { get; private set; }
//    public List<string> Parameters { get; private set; }
//    // Additional properties or methods as needed

//    public FunctionSymbol(string name, List<string> parameters)
//    {
//        Name = name;
//        Parameters = parameters;
//    }
//}

//public class VariableSymbol
//{
//    public string Name { get; private set; }
//    public int Value { get; private set; }
//    // Additional properties or methods as needed

//    public VariableSymbol(string name, int value)
//    {
//        Name = name;
//        Value = value;
//    }
//}


//public class ASTNode
//{
//    public string Value { get; set; }
//    public ASTNode? Left { get; set; }
//    public ASTNode? Right { get; set; }

//    public bool IsFunctionCall { get;set; }
//    public FunctionSymbol? FunctSymbol { get; set; }
//    public List<ASTNode>? FunctionArguments {  get; set; }

//    public bool IsVariable { get; set; }
//    public VariableSymbol? VarSymbol { get; set; }


//    public ASTNode(string value, ASTNode? left, ASTNode? right)
//    {
//        Value = value;
//        Left = left;
//        Right = right;
//        FunctSymbol = null;
//        FunctionArguments = null;
//        VarSymbol = null;
//        IsFunctionCall = false;
//        IsVariable = false;
//    }

//    public ASTNode(string value, FunctionSymbol functionSymbol, List<ASTNode> arguments)
//    {
//        Value = value;
//        Left = null;
//        Right = null;
//        FunctSymbol = functionSymbol;
//        FunctionArguments = arguments;
//        VarSymbol = null;
//        IsFunctionCall = true;
//        IsVariable = false;
//    }

//    public ASTNode(string value, VariableSymbol variableSymbol)
//    {
//        Value = value;
//        Left = null;
//        Right = null;
//        FunctSymbol = null;
//        FunctionArguments = null;
//        VarSymbol = variableSymbol;
//        IsVariable = true;
//    }
//}

//public class AbstractSyntaxTree
//{
//    public ASTNode? Root { get; set; }

//    public AbstractSyntaxTree()
//    {
//        Root = null;
//    }

//    public void AddBinaryOperator(string op, ASTNode? left, ASTNode? right)
//    {
//        ASTNode node = new ASTNode(op, left, right); // Create a new node for the binary operator
//        Root = node; // Assign the new node as the root of the tree
//    }

//    public void AddUnaryOperator(string op, ASTNode? operand)
//    {
//        ASTNode node = new ASTNode(op, operand, null); // Create a new node for the unary operator
//        Root = node; // Assign the new node as the root of the tree
//    }
//}


//public class ExpressionCompiler
//{
//    private Lexer lexer;
//    private Token currentToken;
//    private SymbolTable symbolTable;
//    private AbstractSyntaxTree rootAST;

//    public ExpressionCompiler(string input)
//    {
//        this.lexer = new Lexer(input);
//        this.currentToken = lexer.GetNextToken();
//        this.symbolTable = new SymbolTable();
//        this.rootAST = new AbstractSyntaxTree();
//    }

//    //public void CompileExpression()
//    //{
//    //    while (currentToken != null)
//    //    {
//    //        if (currentToken.Type == TokenType.Variable)
//    //        {
//    //            // Handle variable assignment
//    //            Console.WriteLine($"Variable assignment found: {currentToken.Value}");
//    //            // Code to handle variable assignment goes here
//    //            currentToken = lexer.GetNextToken();
//    //        }
//    //        else if (currentToken.Type == TokenType.Function)
//    //        {
//    //            // Handle function call
//    //            Console.WriteLine($"Function call found: {currentToken.Value}");
//    //            // Code to handle function call goes here
//    //            currentToken = lexer.GetNextToken();
//    //        }
//    //        else
//    //        {
//    //            // Handle arithmetic expression
//    //            ASTNode expression = ParseExpression(0, rootAST);

//    //            GenerateCode(expression);
//    //        }
//    //    }
//    //}

//    public void CompileExpression()
//    {
//        ASTNode expression = ParseExpression(0, rootAST);

//        //if (currentToken.Type != TokenType.EOF)
//        //{
//        //    throw new Exception("Unexpected token found.");
//        //}

//        GenerateCode(expression);
//    }

//    private ASTNode ParsePrimaryExpression()
//    {
//        currentToken = lexer.tokens[lexer.tokenIndex];
//        ++lexer.tokenIndex;
//        Token token = currentToken;

//        if (currentToken.Type == TokenType.Number)
//        {
//            currentToken = lexer.GetNextToken();
//            ASTNode? left = null;
//            ASTNode? right = null;
//            ASTNode numNode = new ASTNode("number", left, right);
//            return numNode;
//        }
//        else if (currentToken.Type == TokenType.Identifier)
//        {
//            string identifierName = token.Value;
//            currentToken = lexer.GetNextToken();

//            if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
//            {
//                // Handle function call
//                currentToken = lexer.GetNextToken(); // Move past "("
//                List<ASTNode> arguments = ParseFunctionArguments();

//                // Lookup function in symbol table and generate corresponding ASTNode
//                FunctionSymbol functionSymbol = symbolTable.LookupFunction(identifierName);
//                return new ASTNode("function_call", functionSymbol, arguments);
//            }
//            else
//            {
//                // Handle variable
//                VariableSymbol variableSymbol = symbolTable.LookupVariable(identifierName);
//                return new ASTNode("variable", variableSymbol);
//            }
//        }
//        else if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
//        {
//            currentToken = lexer.GetNextToken(); // Move past "("
//            ASTNode node = ParseExpression(0,rootAST);
//            if (currentToken.Value == ")")
//            {
//                currentToken = lexer.GetNextToken(); // Move past ")"
//            }
//            else
//            {
//                throw new Exception("Missing closing parenthesis.");
//            }
//            return node;
//        }

//        throw new Exception("Invalid primary expression.");
//    }

//    private List<ASTNode> ParseFunctionArguments()
//    {
//        List<ASTNode> arguments = new List<ASTNode>();

//        while (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")")
//        {
//            arguments.Add(ParseExpression(0, rootAST));

//            if (currentToken.Type == TokenType.Operator && currentToken.Value == ",")
//            {
//                currentToken = lexer.GetNextToken(); // Move past ","
//            }
//            else if (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")")
//            {
//                throw new Exception("Invalid function argument list.");
//            }
//        }

//        if (currentToken.Value == ")")
//        {
//            currentToken = lexer.GetNextToken(); // Move past ")"
//        }
//        else
//        {
//            throw new Exception("Missing closing parenthesis in function call arguments.");
//        }

//        return arguments;
//    }


//    private void GenerateCode(ASTNode node)
//    {
//        // Generate code for the abstract syntax tree (AST)
//    }


//    //private void ParseExpression(int precedence)
//    //{
//    //    // Handle expression parsing using precedence climbing
//    //    // Code for parsing expressions goes here
//    //}

//    //private void ParseExpression(int precedence)
//    //{
//    //    // Handle expression parsing using precedence climbing
//    //    // Code for parsing expressions goes here

//    //    // Base case: get the next token
//    //    Token left = currentToken;
//    //    currentToken = lexer.GetNextToken();

//    //    // Loop to handle binary operators and precedence
//    //    while (currentToken != null && currentToken.Type == TokenType.Operator && (GetPrecedence(currentToken.Value) >= precedence))
//    //    {
//    //        Token op = currentToken;
//    //        currentToken = lexer.GetNextToken();

//    //        Token right = currentToken;
//    //        currentToken = lexer.GetNextToken();

//    //        // Recursively parse the right side of the expression
//    //        int nextPrecedence = GetPrecedence(op.Value);
//    //        if (op.Value == "-" && IsUnaryOperator())
//    //        {
//    //            ParseExpression(6);
//    //        }
//    //        else
//    //        {
//    //            ParseExpression(nextPrecedence);
//    //        }

//    //        // Code to generate code for the op goes here
//    //        Console.WriteLine("OP: " + op.Value + ", OP Type: " + op.Type.ToString() + ", ");
//    //    }
//    //}

//    //private ASTNode ParsePrimaryExpression()
//    //{
//    //    Token token = currentToken;

//    //    if (currentToken.Type == TokenType.Number)
//    //    {
//    //        currentToken = lexer.GetNextToken();
//    //        return new ASTNode(token.Value, null, null);
//    //    }
//    //    else if (currentToken.Type == TokenType.Identifier)
//    //    {
//    //        currentToken = lexer.GetNextToken();
//    //        return new ASTNode(token.Value, null, null);
//    //    }
//    //    else if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
//    //    {
//    //        // Handle parentheses
//    //        currentToken = lexer.GetNextToken(); // Move past "("
//    //        ASTNode node = ParseExpression(0, rootAST); // Parse the expression within the parentheses
//    //        if (currentToken.Value == ")")
//    //        {
//    //            currentToken = lexer.GetNextToken(); // Move past ")"
//    //        }
//    //        else
//    //        {
//    //            throw new Exception("Missing closing parenthesis.");
//    //        }
//    //        return node;
//    //    }

//    //    throw new Exception("Invalid primary expression.");
//    //}


//    private ASTNode ParseExpression(int precedence, AbstractSyntaxTree ast)
//    {
//        ASTNode leftNode = ParsePrimaryExpression(); // Parse the left operand

//        while (currentToken != null && currentToken.Type == TokenType.Operator && (GetPrecedence(currentToken.Value) >= precedence))
//        {
//            Token op = currentToken;
//            currentToken = lexer.GetNextToken();

//            int nextPrecedence = GetPrecedence(op.Value);

//            ASTNode rightNode = ParseExpression(nextPrecedence, ast); // Parse the right operand with correct precedence

//            // Handle associativity for right-associative operators
//            while (currentToken != null && currentToken.Type == TokenType.Operator && GetPrecedence(currentToken.Value) == nextPrecedence)
//            {
//                rightNode = ParseExpression(nextPrecedence, ast);
//            }

//            // Handle parentheses
//            if (currentToken != null && currentToken.Type == TokenType.Parenthesis && currentToken.Value == ")")
//            {
//                currentToken = lexer.GetNextToken(); // Move to the next token
//            }

//            if (op.Value == "-" && IsUnaryOperator())
//            {
//                ast.AddUnaryOperator(op.Value, rightNode);
//            }
//            else
//            {
//                leftNode = new ASTNode(op.Value, leftNode, rightNode);
//            }
//        }

//        return leftNode;
//    }


//    private int GetPrecedence(string op)
//    {
//        switch (op)
//        {
//            case "+":
//            case "-":
//                return 1;
//            case "*":
//            case "/":
//                return 2;
//            default:
//                return 0;
//        }
//    }

//    private bool IsUnaryOperator()
//    {
//        return currentToken.Type == TokenType.Operator && (currentToken.Value == "-" || currentToken.Value == "+");
//    }

//}

////public class SymbolTable
////{
////    private Dictionary<string, int> variables;

////    public SymbolTable()
////    {
////        variables = new Dictionary<string, int>();
////    }

////    public void AddVariable(string name, int value)
////    {
////        variables[name] = value;
////    }

////    public int GetVariableValue(string name)
////    {
////        return variables.ContainsKey(name) ? variables[name] : 0;
////    }
////}

//public class SymbolTable
//{
//    private Dictionary<string, VariableSymbol> variables;
//    private Dictionary<string, FunctionSymbol> functions;

//    public SymbolTable()
//    {
//        variables = new Dictionary<string, VariableSymbol>();
//        functions = new Dictionary<string, FunctionSymbol>();
//    }

//    public void DefineVariable(string name, int value)
//    {
//        VariableSymbol variable = new VariableSymbol(name, value);
//        variables[name] = variable;
//    }

//    public void DefineFunction(string name, List<string> parameters)
//    {
//        FunctionSymbol function = new FunctionSymbol(name, parameters);
//        functions[name] = function;
//    }

//    public VariableSymbol LookupVariable(string name)
//    {
//        if (variables.ContainsKey(name))
//        {
//            return variables[name];
//        }
//        throw new Exception($"Variable '{name}' not found in symbol table.");
//    }

//    public FunctionSymbol LookupFunction(string name)
//    {
//        if (functions.ContainsKey(name))
//        {
//            return functions[name];
//        }
//        throw new Exception($"Function '{name}' not found in symbol table.");
//    }

//    public int GetVariableValue(string name)
//    {
//        return variables.ContainsKey(name) ? variables[name].Value : 0;
//    }
//}
