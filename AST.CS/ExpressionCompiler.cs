using System;
using System.Collections.Generic;
using System.Diagnostics.Metrics;
using System.Text;

namespace Squash.Compiler
{
    public enum TokenType
    {
        Number,
        Variable,
        Function,
        Operator,
        Parenthesis,
        Identifier,
        Assignment,
        Comma,
        Peroid,
        VarKeyword,
        IntKeyword,
        DoubleKeyword,
        StringKeyword,
        StringLiteral,
        IntLiteral,
        SemiColon,
        Whitespace,
        EOF
    }

    public enum VarType //TODO: Array types and object types
    {
        VarAutomatic,
        NullLiteral,
        Object,
        FunctionPointer,
        VoidPointer,
        Int,
        Short,
        Long,
        Int32,
        Int64,
        UInt32,
        UInt64,
        Double,
        Float,
        String,
        Char,
        Matrix,
        Matrix4,
        Matrix3,
        Vector,
        Vector2,
        Vector3,
        Vector4
    }

    public class Token
    {
        public TokenType Type { get; set; }
        public string Value { get; set; }

        public Token(TokenType type, string value)
        {
            Type = type;
            Value = value;
        }
    }

    public class Lexer
    {
        private string input;
        private int currentPos;
        private char currentChar = '\0';
        private char currentChar1 = '\0';
        private char currentChar2 = '\0';
        private char currentChar3 = '\0';
        private char currentChar4 = '\0';
        private char currentChar5 = '\0';
        private char currentChar6 = '\0';
        private char currentChar7 = '\0';
        private char currentChar8 = '\0';
        private char currentChar9 = '\0';

        public Lexer(string input)
        {
            this.input = input;
            this.currentPos = 0;
            if (input.Length > 0)
            {
                this.currentChar = this.input[currentPos];
            }
        }

        public void Advance()
        {
            currentPos++;
            if (currentPos < input.Length)
            {
                currentChar = input[currentPos];
            }
            else
            {
                currentChar = '\0';
            }
        }
        public int GetPosition()
        {
            return currentPos;
        }

        public void SetPosition(int newPosition)
        {
            currentPos = newPosition;
        }

        public Token SkipToToken(TokenType tokenType)
        {
            Token currentToken = null;
            currentToken = GetNextToken();
            while (currentToken != null)
            {
                if (currentToken.Type == tokenType)
                {
                    break;
                }
                else if (currentToken.Type == TokenType.EOF)
                {
                    currentToken = null;
                    break;
                }
                //Advance();
                currentToken = GetNextToken();
            }
            return currentToken;
        }

        private void predictiveLookaheads()
        {
            // Lookaheads for predictive parsing.
            if (currentPos + 1 < input.Length)
            {
                currentChar1 = input[currentPos + 1];
            }
            if (currentPos + 2 < input.Length)
            {
                currentChar2 = input[currentPos + 2];
            }
            if (currentPos + 3 < input.Length)
            {
                currentChar3 = input[currentPos + 3];
            }
            if (currentPos + 4 < input.Length)
            {
                currentChar4 = input[currentPos + 4];
            }
            if (currentPos + 5 < input.Length)
            {
                currentChar5 = input[currentPos + 5];
            }
            if (currentPos + 6 < input.Length)
            {
                currentChar6 = input[currentPos + 6];
            }
            if (currentPos + 7 < input.Length)
            {
                currentChar7 = input[currentPos + 7];
            }
            if (currentPos + 8 < input.Length)
            {
                currentChar8 = input[currentPos + 8];
            }
            if (currentPos + 9 < input.Length)
            {
                currentChar9 = input[currentPos + 9];
            }
        }
        public Token GetNextToken() // Lexer.
        {
            while (currentChar != '\0')
            {
                predictiveLookaheads();

                if (currentPos >= input.Length)
                {
                    Token token = new Token(TokenType.EOF, string.Empty);

                    return token;
                }
                if (currentChar == 'v' && currentChar1 == 'a' && currentChar2 == 'r')
                {
                    Token token = new Token(TokenType.VarKeyword,
                        currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString()
                    );
                    Advance();
                    Advance();
                    Advance();
                    return token;
                }
                if (currentChar == 'i' && currentChar1 == 'n' && currentChar2 == 't')
                {
                    Token token = new Token(TokenType.IntKeyword,
                        currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString()
                    );
                    Advance();
                    Advance();
                    Advance();
                    return token;
                }
                if (currentChar == 's' && currentChar1 == 't' && currentChar2 == 'r' && currentChar3 == 'i' && currentChar4 == 'n' && currentChar5 == 'g')
                {
                    Token token = new Token(TokenType.StringKeyword,
                        currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString()
                        + currentChar3.ToString() + currentChar4.ToString() + currentChar5.ToString()
                    );
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    return token;
                }
                if (currentChar == 'd' && currentChar1 == 'o' && currentChar2 == 'u' && currentChar3 == 'b' && currentChar4 == 'l' && currentChar5 == 'e')
                {
                    Token token = new Token(TokenType.DoubleKeyword,
                        currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString()
                        + currentChar3.ToString() + currentChar4.ToString() + currentChar5.ToString()
                    );
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    return token;
                }
                else if (char.IsDigit(currentChar))
                {
                    Token token = new Token(TokenType.Number, ParseNumber());
                    Advance();
                    return token;
                }
                else if (char.IsLetter(currentChar))
                {
                    Token token = new Token(TokenType.Identifier, ParseIdentifier());
                    Advance();
                    return token;
                }
                else if (currentChar == '+' || currentChar == '-' || currentChar == '*' || currentChar == '/')
                {
                    Token token = new Token(TokenType.Operator, currentChar.ToString());
                    Advance();
                    return token;
                }
                else if (currentChar == '(' || currentChar == ')') // Handle parentheses
                {
                    Token token = new Token(TokenType.Parenthesis, currentChar.ToString());
                    Advance();
                    return token;
                }
                else if (currentChar == '=')
                {
                    Token token = new Token(TokenType.Assignment, currentChar.ToString());
                    Advance();
                    return token;
                }
                else if (currentChar == '.')
                {
                    Token token = new Token(TokenType.Peroid, currentChar.ToString());
                    Advance();
                    return token;
                }
                else if (currentChar == ';')
                {
                    Token token = new Token(TokenType.SemiColon, currentChar.ToString());
                    Advance();
                    return token;
                }
                else if (char.IsWhiteSpace(currentChar))
                {
                    Token token = new Token(TokenType.Whitespace, currentChar.ToString());
                    Advance();
                    return token;
                }
                else
                {
                    throw new Exception("Invalid character found in input. Current position: " + GetPosition().ToString());
                }
            }

            return null;
        }

        private string ParseNumber()
        {
            StringBuilder number = new StringBuilder();

            while (currentChar != '\0' && (char.IsDigit(currentChar) || currentChar == '.' || currentChar == 'f'))
            {
                number.Append(currentChar);
                Advance();
            }

            return number.ToString();
        }

        private string ParseIdentifier()
        {
            StringBuilder identifier = new StringBuilder();

            while (currentChar != '\0' && (char.IsLetterOrDigit(currentChar) || currentChar == '_'))
            {
                identifier.Append(currentChar);
                Advance();
            }

            return identifier.ToString();
        }
    }

    public class FunctionSymbol
    {
        public string Name { get; private set; }
        public List<string> Parameters { get; private set; }
        // Additional properties or methods as needed

        public FunctionSymbol(string name, List<string> parameters)
        {
            Name = name;
            Parameters = parameters;
        }
    }

    public class VariableSymbol
    {
        public VarType VariableType { get; set; }
        public string Name { get; private set; }
        public object Value { get; private set; }
        // Additional properties or methods as needed

        public VariableSymbol(VarType type, string name, int value)
        {
            VariableType = type; 
            Name = name;
            Value = value;
        }
        public VariableSymbol(VarType type, string name, double value)
        {
            VariableType = type;
            Name = name;
            Value = value;
        }
        public VariableSymbol(VarType type, string name, string value)
        {
            VariableType = type;
            Name = name;
            Value = value;
        }
        public int ParseInt()
        {
            if(Value != null && (VariableType == VarType.Int || VariableType == VarType.Int32))
            {
                string val = Value.ToString();
                if(!string.IsNullOrEmpty(val))
                {
                    return int.Parse(val);
                }
            }
            return 0;
        }

        public string ParseString()
        {
            if (Value != null && (VariableType == VarType.String))
            {
                return Value.ToString();
            }
            return string.Empty;
        }
    }

    public enum ASTNodeType
    {
        BIN_OP,
        UNARY_OP,
        Number,
        VariableDefine,
        Variable,
        FunctionCall
    }

    public class ASTNode
    {
        public ASTNodeType Type { get; set; }
        public string Value { get; set; }
        public ASTNode? Left { get; set; }
        public ASTNode? Right { get; set; }

        public bool IsFunctionCall { get; set; }
        public FunctionSymbol? FunctSymbol { get; set; }
        public List<ASTNode>? FunctionArguments { get; set; }

        public bool IsVariable { get; set; }
        public VariableSymbol? VarSymbol { get; set; }


        public ASTNode(ASTNodeType type, string value, ASTNode? left, ASTNode? right)
        {
            Type = type;
            Value = value;
            Left = left;
            Right = right;
            FunctSymbol = null;
            FunctionArguments = null;
            VarSymbol = null;
            IsFunctionCall = false;
            IsVariable = false;
        }

        public ASTNode(ASTNodeType type, string value, FunctionSymbol functionSymbol, List<ASTNode> arguments)
        {
            Type = type;
            Value = value;
            Left = null;
            Right = null;
            FunctSymbol = functionSymbol;
            FunctionArguments = arguments;
            VarSymbol = null;
            IsFunctionCall = true;
            IsVariable = false;
        }

        public ASTNode(ASTNodeType type, string value, VariableSymbol variableSymbol)
        {
            Type = type;
            Value = value;
            Left = null;
            Right = null;
            FunctSymbol = null;
            FunctionArguments = null;
            VarSymbol = variableSymbol;
            IsVariable = true;
        }
    }

    public class AbstractSyntaxTree
    {
        public ASTNode? Root { get; set; }

        public AbstractSyntaxTree()
        {
            Root = null;
        }

        public void AddBinaryOperator(string op, ASTNode? left, ASTNode? right)
        {
            ASTNode node = new ASTNode(ASTNodeType.BIN_OP, op, left, right); // Create a new node for the binary operator
            Root = node; // Assign the new node as the root of the tree
        }

        public void AddUnaryOperator(string op, ASTNode? operand)
        {
            ASTNode node = new ASTNode(ASTNodeType.UNARY_OP, op, operand, null); // Create a new node for the unary operator
            Root = node; // Assign the new node as the root of the tree
        }
    }


    public class ExpressionCompiler
    {
        private Lexer lexer;
        private Token currentToken;
        private SymbolTable symbolTable;
        private AbstractSyntaxTree rootAST;
        private Assembler asm;


        public ExpressionCompiler(string input)
        {
            this.lexer = new Lexer(input);
            this.currentToken = lexer.GetNextToken();
            this.symbolTable = new SymbolTable();
            this.rootAST = new AbstractSyntaxTree();
            this.asm = new Assembler(rootAST);
        }


        public void CompileExpression()
        {
            ASTNode expression = ParseExpression(0, rootAST);
            rootAST.Root = expression;

            if (currentToken != null && currentToken.Type != TokenType.EOF)
            {
                throw new Exception("Unexpected token found. Position:" + lexer.GetPosition().ToString());
            }

            asm.GenerateCode();
        }

        private ASTNode ParseVariableDefine(VarType varType)
        {
            bool varAssignment = true;
            while (varAssignment)
            {
                if (currentToken.Type == TokenType.EOF || currentToken.Type == TokenType.Identifier)
                {
                    varAssignment = false;
                }
                else
                {
                    //lexer.Advance();
                    currentToken = lexer.GetNextToken();
                }
            }

            return parseAssignmentOperator(varType);
        }

        private ASTNode parseAssignmentOperator(VarType varType)
        {
            if (currentToken.Type == TokenType.Identifier)
            {
                string identifierName = currentToken.Value;
                double varValue = 0;

                int savedPosition = lexer.GetPosition();
                Token assignmentToken = lexer.SkipToToken(TokenType.Assignment);

                if (assignmentToken != null && assignmentToken.Type == TokenType.Assignment)
                {
                    VariableSymbol varDefine;

                    if (varType != VarType.VarAutomatic)
                    {
                        switch (varType)
                        {
                            case VarType.Int:
                            case VarType.Int32:
                                currentToken = lexer.SkipToToken(TokenType.Number);
                                int varValueI = int.Parse(currentToken.Value);
                                varDefine = symbolTable.DefineVariable(VarType.Int, identifierName, varValueI);
                                break;
                            //case VarType.Long:
                            //case VarType.Int64:

                            //    break;
                            //case VarType.Float:

                            //    break;
                            case VarType.Double:
                                currentToken = lexer.SkipToToken(TokenType.Number);
                                double varValueD = double.Parse(currentToken.Value);
                                varDefine = symbolTable.DefineVariable(VarType.Double, identifierName, varValueD);
                                break;
                            case VarType.String:
                                currentToken = lexer.SkipToToken(TokenType.StringLiteral);
                                varDefine = symbolTable.DefineVariable(VarType.Double, identifierName, currentToken.Value);
                                break;
                            default:
                                varDefine = null;
                                throw new Exception("Unhandled variable type which is not implemented type specified: '" + varType.ToString() + "'");
                        }
                    }
                    else
                    {   // Automatic type inference required.
                        int savedPosition2 = lexer.GetPosition();

                        currentToken = lexer.SkipToToken(TokenType.Number);

                        if (currentToken == null)
                        {
                            lexer.SetPosition(savedPosition2);

                            currentToken = lexer.SkipToToken(TokenType.StringLiteral);

                            if (currentToken == null)
                            {
                                lexer.SetPosition(savedPosition2);

                                currentToken = lexer.SkipToToken(TokenType.IntLiteral);

                                if (currentToken == null)
                                {
                                    lexer.SetPosition(savedPosition2);
                                    throw new Exception("Unhandled token type. Can not parse '" + lexer.GetNextToken().Value + "'");
                                }
                                else
                                {
                                    int varValueI = int.Parse(currentToken.Value);

                                    varDefine = symbolTable.DefineVariable(VarType.Int, identifierName, varValueI);
                                }
                            }
                            else
                            {
                                string varValueS = currentToken.Value;

                                varDefine = symbolTable.DefineVariable(VarType.String, identifierName, varValueS);
                            }
                        }
                        else
                        {
                            varValue = double.Parse(currentToken.Value);

                            varDefine = symbolTable.DefineVariable(VarType.Double, identifierName, varValue);
                        }
                    }

                    ASTNode varDefineNode = new ASTNode(ASTNodeType.VariableDefine, identifierName, varDefine);

                    return varDefineNode;
                }
                else
                {
                    lexer.SetPosition(savedPosition);
                    //lexer.Advance();
                    currentToken = lexer.GetNextToken();

                    if(currentToken.Type == TokenType.SemiColon && currentToken.Type != TokenType.EOF)
                    {
                        return parseAssignmentOperator(varType);
                    }
                }
            }
            return null;
        }

        private ASTNode ParsePrimaryExpression()
        {
            Token token = currentToken;

            if (currentToken.Type == TokenType.VarKeyword)
            {
                ASTNode varDefineNode = ParseVariableDefine(VarType.VarAutomatic);
                return varDefineNode;
            }
            else if (currentToken.Type == TokenType.DoubleKeyword)
            {
                ASTNode varDefineNode = ParseVariableDefine(VarType.Double);
                return varDefineNode;
            }
            else if (currentToken.Type == TokenType.IntKeyword)
            {
                ASTNode varDefineNode = ParseVariableDefine(VarType.Int);
                return varDefineNode;
            }
            else if (currentToken.Type == TokenType.StringKeyword)
            {
                ASTNode varDefineNode = ParseVariableDefine(VarType.String);
                return varDefineNode;
            }
            else if (currentToken.Type == TokenType.Number)
            {
                ASTNode? left = null;
                ASTNode? right = null;
                ASTNode numNode = new ASTNode(ASTNodeType.Number, token.Value, left, right);

                currentToken = lexer.GetNextToken();
                return numNode;
            }
            else if (currentToken.Type == TokenType.Identifier)
            {
                string identifierName = token.Value;
                currentToken = lexer.GetNextToken();

                if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
                {
                    // Handle function call
                    currentToken = lexer.GetNextToken(); // Move past "("
                    List<ASTNode> arguments = ParseFunctionArguments();

                    // Lookup function in symbol table and generate corresponding ASTNode
                    FunctionSymbol functionSymbol = symbolTable.LookupFunction(identifierName);
                    return new ASTNode(ASTNodeType.FunctionCall, identifierName, functionSymbol, arguments);
                }
                else
                {
                    // Handle variable
                    VariableSymbol variableSymbol = symbolTable.LookupVariable(identifierName);
                    return new ASTNode(ASTNodeType.Variable, identifierName, variableSymbol);
                }
            }
            else if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
            {
                currentToken = lexer.GetNextToken(); // Move past "("
                ASTNode node = ParseExpression(0, rootAST);
                if (currentToken.Value == ")")
                {
                    currentToken = lexer.GetNextToken(); // Move past ")"
                }
                else
                {
                    throw new Exception("Missing closing parenthesis.");
                }
                return node;
            }
            else if (currentToken.Type == TokenType.Variable)
            {
                string identifierName = currentToken.Value;
                currentToken = lexer.GetNextToken(); // Move past variable.

                // TODO: Check if there is a variable assignment unary operator, 
                //       otherwise skip over past potential variable assignment or skip past token.
            }
            else if (currentToken.Type == TokenType.Whitespace)
            {
                currentToken = lexer.GetNextToken(); // Move past whitespace character.
            }
            throw new Exception("Invalid primary expression.");
        }

        private List<ASTNode> ParseFunctionArguments()
        {
            List<ASTNode> arguments = new List<ASTNode>();

            while (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")")
            {
                arguments.Add(ParseExpression(0, rootAST));

                if (currentToken.Type == TokenType.Operator && currentToken.Value == ",")
                {
                    currentToken = lexer.GetNextToken(); // Move past ","
                }
                else if (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")")
                {
                    throw new Exception("Invalid function argument list.");
                }
            }

            if (currentToken.Value == ")")
            {
                currentToken = lexer.GetNextToken(); // Move past ")"
            }
            else
            {
                throw new Exception("Missing closing parenthesis in function call arguments.");
            }

            return arguments;
        }

        private ASTNode ParseExpression(int precedence, AbstractSyntaxTree ast)
        {
            ASTNode leftNode = ParsePrimaryExpression(); // Parse the left operand

            while (currentToken != null && currentToken.Type == TokenType.Operator && (GetPrecedence(currentToken.Value) >= precedence))
            {
                Token op = currentToken;
                currentToken = lexer.GetNextToken();

                int nextPrecedence = GetPrecedence(op.Value);

                ASTNode rightNode = ParseExpression(nextPrecedence, ast); // Parse the right operand with correct precedence

                // Handle associativity for right-associative operators
                while (currentToken != null && currentToken.Type == TokenType.Operator && GetPrecedence(currentToken.Value) == nextPrecedence)
                {
                    rightNode = ParseExpression(nextPrecedence, ast);
                }

                // Handle parentheses
                if (currentToken != null && currentToken.Type == TokenType.Parenthesis && currentToken.Value == ")")
                {
                    currentToken = lexer.GetNextToken(); // Move to the next token
                }

                if (op.Value == "-" && IsUnaryOperator())
                {
                    ast.AddUnaryOperator(op.Value, rightNode);
                }
                else
                {
                    leftNode = new ASTNode(ASTNodeType.BIN_OP, op.Value, leftNode, rightNode);
                }
            }

            return leftNode;
        }


        private int GetPrecedence(string op)
        {
            switch (op)
            {
                case "+":
                case "-":
                    return 1;
                case "*":
                case "/":
                    return 2;
                default:
                    return 0;
            }
        }

        private bool IsUnaryOperator()
        {
            return currentToken.Type == TokenType.Operator && (currentToken.Value == "-" || currentToken.Value == "+");
        }

    }

    public class SymbolTable
    {
        private Dictionary<string, TokenType> keywordDict;
        private Dictionary<string, VariableSymbol> variables;
        private Dictionary<string, FunctionSymbol> functions;

        public SymbolTable()
        {
            this.keywordDict = new Dictionary<string, TokenType>
        {
            { "sin", TokenType.Function },
            { "cos", TokenType.Function },
            { "tan", TokenType.Function }
        };
            variables = new Dictionary<string, VariableSymbol>();
            functions = new Dictionary<string, FunctionSymbol>();
        }

        public VariableSymbol DefineVariable(VarType type, string name, int value)
        {
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public VariableSymbol DefineVariable(VarType type, string name, double value)
        {
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public VariableSymbol DefineVariable(VarType type, string name, string value)
        {
            VariableSymbol variable = new VariableSymbol(type, name, value);
            variables[name] = variable;
            return variable;
        }
        public void DefineFunction(string name, List<string> parameters)
        {
            FunctionSymbol function = new FunctionSymbol(name, parameters);
            functions[name] = function;
        }

        public VariableSymbol LookupVariable(string name)
        {
            if (variables.ContainsKey(name))
            {
                return variables[name];
            }
            throw new Exception($"Variable '{name}' not found in symbol table.");
        }

        public FunctionSymbol LookupFunction(string name)
        {
            if (functions.ContainsKey(name))
            {
                return functions[name];
            }
            throw new Exception($"Function '{name}' not found in symbol table.");
        }

        public int GetInt(string name)
        {
            if(variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                int intValue;
                if(int.TryParse(strValue, out intValue))
                {
                    return intValue;
                }
                else
                {
                    throw new Exception("int can not be parsed from '" + objValue + "' value.");
                }
            }
            return 0;
        }

        public long GetLong(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                long longValue;
                if (long.TryParse(strValue, out longValue))
                {
                    return longValue;
                }
                else
                {
                    throw new Exception("long can not be parsed from '" + strValue + "' value.");
                }
            }
            return 0;
        }

        public string GetString(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                return strValue;
            }
            return string.Empty;
        }

        public float GetFloat(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                float floatValue;
                if (float.TryParse(strValue, out floatValue))
                {
                    return floatValue;
                }
                else
                {
                    throw new Exception("float can not be parsed from '" + strValue + "' value.");
                }
            }
            return 0.0f;
        }

        public double GetDouble(string name)
        {
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                double doubleValue;
                if (double.TryParse(strValue, out doubleValue))
                {
                    return doubleValue;
                }
                else
                {
                    throw new Exception("double can not be parsed from '" + strValue + "' value.");
                }
            }
            return 0.0f;
        }
    }
}