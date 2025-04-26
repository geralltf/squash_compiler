using System;
using System.Collections.Generic;
using System.Diagnostics.Metrics;
using System.Text;

namespace SquashC.Compiler
{
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

            this.symbolTable.DefineFunction("sin", null);
            //this.symbolTable.DefineVariable(VarType.Int, "a", 0);
        }


        public void CompileExpression()
        {
            ASTNode expression = ParseStatements();

            rootAST.Root = expression;

            if (currentToken != null && currentToken.Type != TokenType.EOF)
            {
                throw new Exception("Unexpected token found. Position:" + lexer.GetPosition().ToString());
            }

            //asm.Is_macOS = true;
            asm.Is_Linux = true;
            asm.GenerateCode(expression);
        }

        private ASTNode ParseStatements()
        {
            ASTNode expression = ParseExpression(0, rootAST);
            if (expression != null)
            {
                if (expression.FunctionBody == null)
                {
                    expression.FunctionBody = new List<ASTNode>();
                }
                //expression.FunctionBody.Add(expression);
            }

            if (currentToken != null && currentToken.Type == TokenType.SemiColon)
            {
                currentToken = lexer.GetNextToken();

                if (currentToken.Type == TokenType.CurleyBrace && currentToken.Value == "}")
                {
                    currentToken = lexer.GetNextToken();
                }
            }
            else if (currentToken != null && currentToken.Type == TokenType.CurleyBrace && currentToken.Value == "}")
            {
                currentToken = lexer.GetNextToken();
            }
            else
            {
                //return null;
            }

            if (currentToken != null)
            {
                ASTNode expressionChild = ParseStatements();

                if (expressionChild != null)
                {
                    expression.FunctionBody.Add(expressionChild);
                    expression.IsFunctionDefinition = true;
                }
            }

            return expression;
        }

        private ASTNode? ParseVariableDeclaration(VarType varType)
        {
            currentToken = lexer.GetNextToken();
            if(currentToken != null && currentToken.Type == TokenType.Whitespace)
            {
                currentToken = lexer.GetNextToken();
                if (currentToken != null && currentToken.Type == TokenType.Identifier)
                {
                    string varIdentifier = currentToken.Value;

                    currentToken = lexer.GetNextToken();

                    //VariableSymbol variableSymbol = symbolTable.DefineVariable(varType, varIdentifier, "");
                    ASTNode? left = null;
                    ASTNode? right = null;
                    ASTNode varDeclNode = new ASTNode(ASTNodeType.VariableDeclaration, varIdentifier, left, right);
                    varDeclNode.VariableType = varType;
                    return varDeclNode;
                }
            }
            return null;
        }

        private ASTNode ParseVariableDefine(VarType varType)
        {
            bool varAssignment = true;

            while (varAssignment)
            {
                if (currentToken == null || currentToken.Type == TokenType.EOF || currentToken.Type == TokenType.Identifier)
                {
                    varAssignment = false;
                }
                else
                {
                    currentToken = lexer.GetNextToken();
                }
            }

            return parseAssignmentOperator(varType);
        }

        private ASTNode parseAssignmentOperator(VarType varType)
        {
            if (currentToken != null && currentToken.Type == TokenType.Identifier)
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
                    //lexer.SetPosition(savedPosition);
                    //lexer.Advance();
                    //currentToken = lexer.GetNextToken();

                    if(!symbolTable.VariableHasKey(identifierName))
                    {
                        symbolTable.DefineVariable(VarType.VarAutomatic, identifierName, 0);
                    }

                    VariableSymbol symbol = symbolTable.LookupVariable(identifierName);

                    ASTNode variableNode = new ASTNode(ASTNodeType.Variable, identifierName, symbol);

                    currentToken = lexer.GetNextToken();

                    if (currentToken != null && currentToken.Type == TokenType.SemiColon && currentToken.Type != TokenType.EOF)
                    {
                        ASTNode rhs = parseAssignmentOperator(varType);

                        variableNode.Right = rhs;
                    }

                    return variableNode;
                }
            }
            return null;
        }

        private void parseEndStatement(ref ASTNode varDefineNode)
        {
            if (currentToken != null && currentToken.Type == TokenType.SemiColon)
            {
                currentToken = lexer.GetNextToken();
                if (currentToken != null)
                {
                    ASTNode rhs = ParseExpression(0, rootAST);
                    varDefineNode.Right = rhs;
                }
            }
        }
        private ASTNode ParsePrimaryExpression()
        {
            Token token = currentToken;

            if(currentToken == null)
            {
                return null;
            }

            if (currentToken.Type == TokenType.ReturnKeyword)
            {
                currentToken = lexer.GetNextToken(); // Skip past return keyword.
                currentToken = lexer.GetNextToken(); // Skip past whitespace.

                ASTNode left = ParseExpression(0, rootAST);

                ASTNode returnNode = new ASTNode(ASTNodeType.FunctionReturn, "", left, null);
                return returnNode;
            }
            else if (currentToken.Type == TokenType.VarKeyword)
            {
                ASTNode varDefineNode = ParseVariableDefine(VarType.VarAutomatic);
                ASTNode left = ParseExpression(0, rootAST);
                varDefineNode.Left = left;
                parseEndStatement(ref varDefineNode);
                return varDefineNode;
            }
            else if (currentToken.Type == TokenType.DoubleKeyword)
            {
                int pos = lexer.GetPosition();
                Token token1 = lexer.GetNextToken();
                if (token1 != null && token1.Type == TokenType.Whitespace)
                {
                    token1 = lexer.GetNextToken();
                    if (token1 != null && token1.Type == TokenType.Identifier)
                    {
                        int pos2 = lexer.GetPosition();
                        string functIdentifierName = token1.Value;

                        ASTNode? functDefNode = ParseFunctionDefinition(VarType.Double, functIdentifierName);

                        if (functDefNode != null)
                        {
                            return functDefNode;
                        }
                    }
                }
                lexer.SetPosition(pos);

                ASTNode varDefineNode = ParseVariableDefine(VarType.Double);
                ASTNode left = ParseExpression(0, rootAST);
                varDefineNode.Left = left;
                parseEndStatement(ref varDefineNode);
                return varDefineNode;
            }
            else if (currentToken.Type == TokenType.IntKeyword)
            {
                bool rememberLocation = false;
                int pos = lexer.GetPosition();
                Token token1 = lexer.GetNextToken();
                if (token1 != null && token1.Type == TokenType.Whitespace)
                {
                    token1 = lexer.GetNextToken();
                    if (token1 != null && token1.Type == TokenType.Identifier)
                    {
                        int pos2 = lexer.GetPosition();
                        string functIdentifierName = token1.Value;
                        if (functIdentifierName == "main")
                        {
                            token1 = lexer.GetNextToken();
                            if (token1.Type == TokenType.Parenthesis && token1.Value == "(")
                            {
                                token1 = lexer.GetNextToken();
                                if (token1.Type == TokenType.Identifier && token1.Value == "void")
                                {
                                    token1 = lexer.GetNextToken();
                                    if (token1.Type == TokenType.Parenthesis && token1.Value == ")")
                                    {
                                        token1 = lexer.GetNextToken();
                                        if (token1.Type == TokenType.CurleyBrace && token1.Value == "{")
                                        {
                                            rememberLocation = true;

                                            token1 = lexer.GetNextToken();
                                            // Is Main entry point function.

                                            if (token1.Type == TokenType.ReturnKeyword)
                                            {
                                                currentToken = token1;
                                            }
                                            else
                                            {
                                                // if double etc.
                                                currentToken = token1;
                                            }
                                            ASTNode left = ParseStatements();

                                            ASTNode entryPointNode = new ASTNode(ASTNodeType.FunctionDefinition, functIdentifierName, left, null);
                                            entryPointNode.IsFunctionDefinition = true;
                                            if(entryPointNode.FunctionBody == null)
                                            {
                                                entryPointNode.FunctionBody = new List<ASTNode>();
                                            }
                                            if(left != null)
                                            {
                                                entryPointNode.FunctionBody.Add(left);
                                            }

                                            if (token1.Type == TokenType.CurleyBrace && token1.Value == "}")
                                            {
                                                //return left;
                                            }
                                            else
                                            {
                                                //return left;
                                                //throw new Exception("Missing matching '}' curley brace for function definition.");
                                            }

                                            return entryPointNode;
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            int pos3 = lexer.GetPosition();
                            token1 = lexer.GetNextToken();

                            if (token1.Type == TokenType.Parenthesis && token1.Value == "(")
                            {
                                currentToken = token1;
                                //lexer.SetPosition(pos2);
                                ASTNode? functDefNode = ParseFunctionDefinition(VarType.Int, functIdentifierName);
                                if (functDefNode != null)
                                {
                                    return functDefNode;
                                }
                            }
                            else
                            {
                                lexer.SetPosition(pos3);
                            }


                            if (!rememberLocation)
                            {
                                //lexer.SetPosition(pos);
                                lexer.SetPosition(pos2);
                                var tok = currentToken;

                                ASTNode varDefineNode = ParseVariableDefine(VarType.Int);
                                if (varDefineNode != null)
                                {
                                    ASTNode left = ParseExpression(0, rootAST);
                                    varDefineNode.Left = left;
                                    parseEndStatement(ref varDefineNode);
                                    return varDefineNode;
                                }
                                //return null;
                            }
                            // functIdentifierName
                        }
                    }
                    else if (token1 != null && token1.Type == TokenType.Number)
                    {
                        ASTNode left = null;
                        ASTNode right = null;
                        ASTNode numASTNode = new ASTNode(ASTNodeType.Number, token1.Value, left, right);

                        currentToken = lexer.GetNextToken();

                        if (currentToken.Type == TokenType.SemiColon)
                        {
                            currentToken = lexer.GetNextToken();

                            if (currentToken.Type == TokenType.CurleyBrace && currentToken.Value == "}")
                            {
                                currentToken = lexer.GetNextToken();
                            }
                        }

                        return numASTNode;
                    }
                    else
                    {
                        if (!rememberLocation)
                        {
                            //lexer.SetPosition(pos);
                            var tok = currentToken;

                            ASTNode varDefineNode = ParseVariableDefine(VarType.Int);
                            ASTNode left = ParseExpression(0, rootAST);
                            varDefineNode.Left = left;
                            parseEndStatement(ref varDefineNode);
                            return varDefineNode;
                        }
                    }
                }
                else
                {

                }
                if (!rememberLocation)
                {
                    //lexer.SetPosition(pos);
                    var tok = currentToken;

                    ASTNode varDefineNode = ParseVariableDefine(VarType.Int);
                    if(varDefineNode != null)
                    {
                        ASTNode left = ParseExpression(0, rootAST);
                        if (left != null)
                        {
                            varDefineNode.Left = left;
                        }

                        parseEndStatement(ref varDefineNode);
                        return varDefineNode;
                    }
                    return null;
                }
            }
            else if (currentToken.Type == TokenType.StringKeyword)
            {
                int pos = lexer.GetPosition();
                Token token1 = lexer.GetNextToken();
                if (token1 != null && token1.Type == TokenType.Whitespace)
                {
                    token1 = lexer.GetNextToken();
                    if (token1 != null && token1.Type == TokenType.Identifier)
                    {
                        int pos2 = lexer.GetPosition();
                        string functIdentifierName = token1.Value;

                        ASTNode? functDefNode = ParseFunctionDefinition(VarType.String, functIdentifierName);

                        if (functDefNode != null)
                        {
                            return functDefNode;
                        }
                    }
                }
                lexer.SetPosition(pos);
                
                ASTNode varDefineNode = ParseVariableDefine(VarType.String);
                ASTNode left = ParseExpression(0, rootAST);
                varDefineNode.Left = left;
                parseEndStatement(ref varDefineNode);
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
                    if (!symbolTable.VariableHasKey(identifierName))
                    {
                        symbolTable.DefineVariable(VarType.VarAutomatic, identifierName, 0);
                    }
                    VariableSymbol variableSymbol = symbolTable.LookupVariable(identifierName);
                    ASTNode varNode = new ASTNode(ASTNodeType.Variable, identifierName, variableSymbol);

                    Token before = currentToken;
                    //currentToken = lexer.GetNextToken(); // Skip past identifier token.
                    if (currentToken.Type == TokenType.Assignment && currentToken.Value == "=")
                    {
                        currentToken = lexer.GetNextToken();

                        ASTNode left = ParseExpression(0, rootAST);
                        ASTNode right = new ASTNode(ASTNodeType.VariableAssignment, before.Value, left, varNode);

                        return right;
                    }
                    else if (currentToken.Type == TokenType.Operator)
                    {
                        return varNode;
                    }
                    else
                    {
                        return varNode;
                    }
                }
            }
            else if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
            {
                currentToken = lexer.GetNextToken(); // Move past "("
                ASTNode node = ParseExpression(0, rootAST);
                if (currentToken != null && currentToken.Value == ")")
                {
                    currentToken = lexer.GetNextToken(); // Move past ")"
                }
                else
                {
                    //throw new Exception("Missing closing parenthesis.");
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
            else if (currentToken.Type == TokenType.SemiColon)
            {
                currentToken = lexer.GetNextToken(); // Move past semicolon character.
            }
            if(currentToken == null)
            {
                return null;
            }
            if (currentToken.Type == TokenType.SemiColon)
            {
                currentToken = lexer.GetNextToken(); // Move past semicolon character.
                return null;
            }
            if (currentToken != null)
            {
                throw new Exception("Invalid primary expression. Position: " + currentToken.Position.ToString());
            }
            else
            {
                throw new Exception("Invalid primary expression.");
                //return null;
            }
        }

        private ASTNode? ParseFunctionDefinition(VarType retVarType, string functIdentifierName)
        {
            Token token1;

            //token1 = lexer.GetNextToken();
            //if (token1.Type == TokenType.Parenthesis && token1.Value == "(")
            //{
            //    token1 = lexer.GetNextToken();
            //    if (token1.Type == TokenType.Identifier && token1.Value == "void")
            //    {
            //        token1 = lexer.GetNextToken();
            //        if (token1.Type == TokenType.Parenthesis && token1.Value == ")")
            //        {

            //        }
            //    }
            //}

            List<ASTNode> args = ParseFunctionDefArguments(retVarType);

            token1 = lexer.GetNextToken();
            if (token1 == null || (token1 != null && token1.Type == TokenType.CurleyBrace && token1.Value == "{"))
            {
                //rememberLocation = true;

                token1 = lexer.GetNextToken();
                // Is function with a definition.

                if (token1 != null && token1.Type == TokenType.ReturnKeyword)
                {
                    currentToken = token1;
                }
                else
                {
                    // if double etc.
                    currentToken = token1;
                }
                ASTNode left = ParseStatements();

                ASTNode entryPointNode = new ASTNode(ASTNodeType.FunctionDefinition, functIdentifierName, left, null);
                entryPointNode.IsFunctionDefinition = true;
                entryPointNode.FunctionArguments = args;
                entryPointNode.FunctionReturnType = retVarType;
                if (entryPointNode.FunctionBody == null)
                {
                    entryPointNode.FunctionBody = new List<ASTNode>();
                }
                if (left != null)
                {
                    entryPointNode.FunctionBody.Add(left);
                }

                if (token1 != null && token1.Type == TokenType.CurleyBrace && token1.Value == "}")
                {
                    //return left;
                }
                else
                {
                    //return left;
                    //throw new Exception("Missing matching '}' curley brace for function definition.");
                }

                return entryPointNode;
            }

            return null;
        }
        
        private List<ASTNode> ParseFunctionDefArguments(VarType retVarType)
        {
            List<ASTNode> arguments = new List<ASTNode>();

            while (currentToken != null && (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")"))
            {
                ASTNode expr = ParseExpression(0, rootAST);
                if (expr != null)
                {
                    arguments.Add(expr);
                }


                if (currentToken != null && ((currentToken.Type == TokenType.SemiColon) 
                    || (currentToken.Type == TokenType.Operator && currentToken.Value == ",")))
                {
                    currentToken = lexer.GetNextToken(); // Move past "," or ";"
                    if (currentToken.Type == TokenType.CurleyBrace && currentToken.Value == "}")
                    {
                        currentToken = lexer.GetNextToken(); // Move past "}"
                    }
                }
                else if (currentToken != null && (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")"))
                {
                    throw new Exception("Invalid function argument list.");
                }
            }

            if(currentToken != null)
            {
                if (currentToken.Value == ")")
                {
                    currentToken = lexer.GetNextToken(); // Move past ")"
                }
                else
                {
                    throw new Exception("Missing closing parenthesis in function call arguments.");
                }
            }

            return arguments;
        }

        private List<ASTNode> ParseFunctionArguments()
        {
            List<ASTNode> arguments = new List<ASTNode>();

            while (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")")
            {
                ASTNode expr = ParseExpression(0, rootAST);
                if(expr != null)
                {
                    arguments.Add(expr);
                }
                

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
                    //currentToken = lexer.GetNextToken(); // Move to the next token
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

            //if (currentToken != null && currentToken.Type == TokenType.SemiColon)
            //{
            //    currentToken = lexer.GetNextToken();
            //}

            //if (currentToken != null && currentToken.Type == TokenType.CurleyBrace && currentToken.Value == "}")
            //{
            //    currentToken = lexer.GetNextToken();
            //}

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
            return currentToken != null && currentToken.Type == TokenType.Operator && (currentToken.Value == "-" || currentToken.Value == "+");
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
        public bool VariableHasKey(string name)
        {
            if (variables.ContainsKey(name))
            {
                return true;
            }
            return false;
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
            if (variables.ContainsKey(name))
            {
                object objValue = variables[name].Value;
                string strValue = objValue.ToString();
                int intValue;
                if (int.TryParse(strValue, out intValue))
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