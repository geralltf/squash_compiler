//using System;
//using System.Collections.Generic;

//public class Token
//{
//    public string Type { get; set; }
//    public object Value { get; set; }
//}

//public class Lexer
//{
//    private readonly string text;
//    private int position;

//    public Lexer(string text)
//    {
//        this.text = text;
//        position = 0;
//    }

//public List<Token> Lex(string input)
//{
//    this.input = input;
//    List<Token> tokens = new List<Token>();

//    while (currentPos < input.Length)
//    {
//        char currentChar = input[currentPos];

//        if (char.IsDigit(currentChar))
//        {
//            string number = currentChar.ToString();
//            currentPos++;

//            while (currentPos < input.Length && char.IsDigit(input[currentPos]))
//            {
//                number += input[currentPos];
//                currentPos++;
//            }

//            tokens.Add(new Token(TokenType.Number, number));
//        }
//        else if (currentChar == '+')
//        {
//            tokens.Add(new Token(TokenType.Operator, "+"));
//            currentPos++;
//        }
//        else if (currentChar == '-')
//        {
//            tokens.Add(new Token(TokenType.Operator, "-"));
//            currentPos++;
//        }
//        else if (currentChar == '*')
//        {
//            tokens.Add(new Token(TokenType.Operator, "*"));
//            currentPos++;
//        }
//        else if (currentChar == '/')
//        {
//            tokens.Add(new Token(TokenType.Operator, "/"));
//            currentPos++;
//        }
//        else if (char.IsLetter(currentChar))
//        {
//            string variable = currentChar.ToString();
//            currentPos++;

//            while (currentPos < input.Length && (char.IsLetterOrDigit(input[currentPos]) || input[currentPos] == '_'))
//            {
//                variable += input[currentPos];
//                currentPos++;
//            }

//            tokens.Add(new Token(TokenType.Variable, variable));
//        }
//        else
//        {
//            currentPos++; // Skip whitespace or invalid characters
//        }
//    }

//    return tokens;
//}

//    public Token GetNextToken()
//    {
//        if (position >= text.Length)
//        {
//            return new Token { Type = "EOF" };
//        }

//        char currentChar = text[position];

//        if (char.IsDigit(currentChar))
//        {
//            position++;
//            return new Token { Type = "INTEGER", Value = int.Parse(currentChar.ToString()) };
//        }
//        else if (char.IsLetter(currentChar))
//        {
//            position++;
//            return new Token { Type = "IDENTIFIER", Value = currentChar.ToString() };
//        }
//        else if (new char[] { '+', '-', '*', '/', '(', ')' }.Contains(currentChar))
//        {
//            position++;
//            return new Token { Type = "OPERATOR", Value = currentChar };
//        }
//        else if (char.IsWhiteSpace(currentChar))
//        {
//            position++;
//            return new Token { Type = "SPACE", Value = currentChar };
//        }
//        else
//        {
//            throw new Exception("Invalid character");
//        }
//    }
//}

//public class ASTNode
//{
//    public string Type { get; set; }
//    public object Value { get; set; }
//    public List<ASTNode> Children { get; set; }

//    public override string ToString()
//    {
//        string children = "";
//        if (Children != null && Children.Count > 0)
//        {
//            foreach (ASTNode child in Children)
//            {
//                children += "\t" + child.ToString();
//            }
//        }
//        return base.ToString() + " type: " + Type + ", value: " + Value.ToString() + "\n" + children;
//    }
//}

//public class Parser
//{
//    private readonly Lexer lexer;
//    private Token currentToken;

//    public Parser(Lexer lexer)
//    {
//        this.lexer = lexer;
//        currentToken = this.lexer.GetNextToken();
//    }

//    public void Eat(string tokenType)
//    {
//        if (currentToken.Type == tokenType)
//        {
//            currentToken = lexer.GetNextToken();
//        }
//        else
//        {
//            throw new Exception("Unexpected token");
//        }
//    }

//    public ASTNode Expr()
//    {
//        var node = Factor();

//        while (currentToken.Type == "OPERATOR")
//        {
//            var token = currentToken;
//            Eat("OPERATOR");
//            var binOpNode = new ASTNode { Type = "BIN_OP", Value = token.Value };
//            binOpNode.Children = new List<ASTNode> { node, Factor() };
//            node = binOpNode;
//        }

//        return node;
//    }

//    public ASTNode Factor()
//    {
//        var token = currentToken;

//        if (token.Type == "INTEGER")
//        {
//            Eat("INTEGER");
//            return new ASTNode { Type = "INTEGER", Value = token.Value };
//        }
//        else if (token.Type == "IDENTIFIER")
//        {
//            Eat("IDENTIFIER");
//            return new ASTNode { Type = "IDENTIFIER", Value = token.Value };
//        }
//        else if (token.Type == "OPERATOR" && (char)token.Value == '(')
//        {
//            Eat("OPERATOR");
//            var node = Expr();
//            Eat("OPERATOR");
//            return node;
//        }
//        else
//        {
//            throw new Exception("Invalid factor");
//        }
//    }
//}

//public class Compiler
//{
//    private readonly string text;

//    public Compiler(string text)
//    {
//        this.text = text;
//    }

//    public ASTNode Compile()
//    {
//        Lexer lexer = new Lexer(text);
//        Parser parser = new Parser(lexer);

//        return parser.Expr();
//    }
//}