using System;
using System.Collections.Generic;
using System.Diagnostics.Metrics;
using System.Text;
using Microsoft.Extensions.Logging;

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
            Logger.Log.LogInformation("ExpressionCompiler(): ctor");

            input = Minifier.MinifyCode(input);
            this.lexer = new Lexer(input);
            this.currentToken = lexer.GetNextToken();
            this.symbolTable = new SymbolTable();
            this.rootAST = new AbstractSyntaxTree();
            this.asm = new Assembler(rootAST);
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
            Logger.Log.LogInformation("ParseStatements(): Parsing individual statements that may be outside a function definition or inside a function definition.");

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

                Logger.Log.LogInformation("ParseStatements(): end of statement semi colon found.");

                if (currentToken.Type == TokenType.CurleyBrace && currentToken.Value == "}")
                {
                    currentToken = lexer.GetNextToken();

                    Logger.Log.LogInformation("ParseStatements(): end of function curley brace found.");
                }
            }
            else if (currentToken != null && currentToken.Type == TokenType.CurleyBrace && currentToken.Value == "}")
            {
                currentToken = lexer.GetNextToken();

                Logger.Log.LogInformation("ParseStatements(): end of function curley brace found.");
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

                    Logger.Log.LogInformation("ParseStatements(): added statement to function body which is function definition.");
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

                    Logger.Log.LogInformation("parseAssignmentOperator(): parsing assignment");

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
                                    Logger.Log.LogError("Unhandled token type. Can not parse null token");
                                    varDefine = null;
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
                Logger.Log.LogInformation("parseEndStatement(): parsing end statement");
                currentToken = lexer.GetNextToken();
                if (currentToken != null)
                {
                    ASTNode rhs = ParseExpression(0, rootAST);
                    varDefineNode.Right = rhs;
                    Logger.Log.LogInformation("parseEndStatement(): rhs: " + rhs.ToString());
                }
            }
        }
        private ASTNode ParsePrimaryExpression()
        {           
            Token token = currentToken;

            if(currentToken == null)
            {
                Logger.Log.LogError("ParsePrimaryExpression(): currentToken is null");
                return null;
            }

            Logger.Log.LogInformation("ParsePrimaryExpression(): handle token types prior to handling");

            if (currentToken.Type == TokenType.ReturnKeyword)
            {
                currentToken = lexer.GetNextToken(); // Skip past return keyword.
                currentToken = lexer.GetNextToken(); // Skip past whitespace.

                ASTNode left = ParseExpression(0, rootAST);

                Logger.Log.LogInformation("ParsePrimaryExpression(): made a ASTNodeType.FunctionReturn AST");

                ASTNode returnNode = new ASTNode(ASTNodeType.FunctionReturn, "", left, null);

                //TODO: Check for curley brace to find out the end of a function definition
                return returnNode;
            }
            else if (currentToken.Type == TokenType.VarKeyword)
            {
                Logger.Log.LogInformation("ParsePrimaryExpression(): made a var variable of some inferred type AST");
                ASTNode varDefineNode = ParseVariableDefine(VarType.VarAutomatic);
                ASTNode left = ParseExpression(0, rootAST);
                varDefineNode.Left = left;
                parseEndStatement(ref varDefineNode);
                return varDefineNode;
            }
            else if (currentToken.Type == TokenType.DoubleKeyword)
            {
                Logger.Log.LogInformation("ParsePrimaryExpression(): double keyword");
                int pos = lexer.GetPosition();
                Token token1 = lexer.GetNextToken();
                if (token1 != null && token1.Type == TokenType.Whitespace)
                {
                    token1 = lexer.GetNextToken();
                    if (token1 != null && token1.Type == TokenType.Identifier)
                    {
                        int pos2 = lexer.GetPosition();
                        string functIdentifierName = token1.Value;

                        Logger.Log.LogInformation("ParsePrimaryExpression(): double keyword parse function definition");

                        ASTNode? functDefNode = ParseFunctionDefinition(VarType.Double, functIdentifierName);

                        if (functDefNode != null)
                        {
                            return functDefNode;
                        }
                    }
                }
                lexer.SetPosition(pos);

                Logger.Log.LogInformation("");

                ASTNode varDefineNode = ParseVariableDefine(VarType.Double);
                ASTNode left = ParseExpression(0, rootAST);
                Logger.Log.LogInformation("ParsePrimaryExpression(): var define node: " + varDefineNode.ToString() + "left expr: " + left.ToString());
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
                            // Parse entry point main() function.
                            ASTNode mainFunct = ParseEntryPoint(functIdentifierName);

                            if (mainFunct != null)
                            {
                                return mainFunct;
                            }
                        }
                        else
                        {
                            int pos3 = lexer.GetPosition();
                            token1 = lexer.GetNextToken();

                            if (token1.Type == TokenType.Parenthesis && token1.Value == "(")
                            {
                                currentToken = token1;

                                Logger.Log.LogInformation("ParsePrimaryExpression(): ParseFunctionDefinition int keyword function is: " + functIdentifierName);

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

                Logger.Log.LogInformation("ParsePrimaryExpression(): AST Number node created. " + numNode.ToString());

                currentToken = lexer.GetNextToken();
                return numNode;
            }
            else if (currentToken.Type == TokenType.Identifier)
            {
                string identifierName = token.Value;

                Logger.Log.LogInformation("ParsePrimaryExpression(): Identifier found '" + identifierName + "'");

                currentToken = lexer.GetNextToken();

                if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
                {
                    Logger.Log.LogInformation("ParsePrimaryExpression(): Parsing function call for identifier '" + identifierName + "'");

                    // Handle function call
                    currentToken = lexer.GetNextToken(); // Move past "("
                    List<ASTNode> arguments = ParseFunctionArguments();

                    foreach (ASTNode arg in arguments)
                    {
                        Logger.Log.LogInformation("ParsePrimaryExpression(): function argument for function call: " + arg.ToString());
                    }

                    // Lookup function in symbol table and generate corresponding ASTNode
                    if(!symbolTable.FunctionHasKey(identifierName))
                    {
                        symbolTable.DefineFunction(identifierName, null);
                    }
                    FunctionSymbol functionSymbol = symbolTable.LookupFunction(identifierName);
                    return new ASTNode(ASTNodeType.FunctionCall, identifierName, functionSymbol, arguments);
                }
                else
                {
                    Logger.Log.LogInformation("ParsePrimaryExpression(): Parsing variable given identifier: '" + identifierName + "'");
                    
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

                        Logger.Log.LogInformation("ParsePrimaryExpression(): variable assignment lhs: '"+ left.ToString() + "' rhs: '"+right.ToString()+"'");

                        return right;
                    }
                    else if (currentToken.Type == TokenType.Operator)
                    {
                        Logger.Log.LogInformation("ParsePrimaryExpression(): variable usage in expression or statement #1.");
                        return varNode;
                    }
                    else
                    {
                        Logger.Log.LogInformation("ParsePrimaryExpression(): variable usage in expression or statement #2.");
                        return varNode;
                    }
                }
            }
            else if (currentToken.Type == TokenType.Parenthesis && currentToken.Value == "(")
            {
                Logger.Log.LogInformation("ParsePrimaryExpression(): Handling parenthesis");
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
                Logger.Log.LogError("Invalid primary expression. Position: " + currentToken.Position.ToString());
                throw new Exception("Invalid primary expression. Position: " + currentToken.Position.ToString());
            }
            else
            {
                Logger.Log.LogError("Invalid primary expression.");
                throw new Exception("Invalid primary expression.");
                //return null;
            }
        }

        private ASTNode ParseEntryPoint(string functIdentifierName)
        {
            Token token1;
            bool rememberLocation = false;

            Logger.Log.LogInformation("ParsePrimaryExpression(): parsing entry point main() function");

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
                            if (entryPointNode.FunctionBody == null)
                            {
                                entryPointNode.FunctionBody = new List<ASTNode>();
                            }
                            if (left != null)
                            {
                                entryPointNode.FunctionBody.Add(left);
                            }

                            Logger.Log.LogInformation("ParseEntryPoint(): entry point node parsed '" + entryPointNode.ToString() + "'");

                            if (token1.Type == TokenType.CurleyBrace && token1.Value == "}")
                            {
                                Logger.Log.LogInformation("ParseEntryPoint(): entry point end closing curley brace found");

                                //return left;
                            }
                            else
                            {
                                Logger.Log.LogWarning("ParseEntryPoint(): Missing matching '}' curley brace for function definition. Might have already parsed it.");
                                //return left;
                                //throw new Exception("Missing matching '}' curley brace for function definition.");
                            }

                            return entryPointNode;
                        }
                    }
                }
            }
            return null;
        }

        private ASTNode? ParseFunctionDefinition(VarType retVarType, string functIdentifierName)
        {
            Token token1;

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

                if(!symbolTable.FunctionHasKey(functIdentifierName))
                {
                    symbolTable.DefineFunction(functIdentifierName, null);
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

            while (currentToken != null && (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")"))
            {
                ASTNode expr = ParseExpression(0, rootAST);
                if(expr != null)
                {
                    arguments.Add(expr);
                }
                

                if (currentToken != null && currentToken.Type == TokenType.Operator && currentToken.Value == ",")
                {
                    currentToken = lexer.GetNextToken(); // Move past ","
                }
                if (currentToken != null && currentToken.Type == TokenType.SemiColon)
                {
                    currentToken = lexer.GetNextToken(); // Move past ";"
                }
                if (currentToken != null && (currentToken.Type == TokenType.CurleyBrace && currentToken.Value == "}"))
                {
                    currentToken = lexer.GetNextToken(); // Move past "}"
                }
                if (currentToken != null && (currentToken.Type != TokenType.Parenthesis || currentToken.Value != ")"))
                {
                    throw new Exception("Invalid function argument list.");
                }
            }

            if (currentToken != null && currentToken.Value == ")")
            {
                currentToken = lexer.GetNextToken(); // Move past ")"
            }
            else
            {
                //throw new Exception("Missing closing parenthesis in function call arguments.");
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

                int nextPrecedence = GetPrecedence(currentToken.Value);

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

                if (op.Value == "--" && IsUnaryOperator())
                {
                    //ast.AddUnaryOperator(op.Value, rightNode);
                    leftNode = new ASTNode(ASTNodeType.UNARY_OP, op.Value, null, rightNode);
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
            return currentToken != null && currentToken.Type == TokenType.Operator && (currentToken.Value == "-" || currentToken.Value == "+");
        }

    }
}