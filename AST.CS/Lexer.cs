using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SquashC.Compiler
{
    public class Lexer
    {
        private string input;
        private List<PreToken> preLexer;

        private int currentPos;
        private PreToken preToken;
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

        public Lexer(string input, ref List<PreToken> preTokens)
        {
            this.input = input;
            this.preLexer = preTokens;
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
                preToken = preLexer[currentPos%preLexer.Count];
            }
            else
            {
                currentChar = '\0';
                preToken = null;
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
            else
            {
                currentChar1 = '\0';
            }
            if (currentPos + 2 < input.Length)
            {
                currentChar2 = input[currentPos + 2];
            }
            else
            {
                currentChar2 = '\0';
            }
            if (currentPos + 3 < input.Length)
            {
                currentChar3 = input[currentPos + 3];
            }
            else
            {
                currentChar3 = '\0';
            }
            if (currentPos + 4 < input.Length)
            {
                currentChar4 = input[currentPos + 4];
            }
            else
            {
                currentChar4 = '\0';
            }
            if (currentPos + 5 < input.Length)
            {
                currentChar5 = input[currentPos + 5];
            }
            else
            {
                currentChar5 = '\0';
            }
            if (currentPos + 6 < input.Length)
            {
                currentChar6 = input[currentPos + 6];
            }
            else
            {
                currentChar6 = '\0';
            }
            if (currentPos + 7 < input.Length)
            {
                currentChar7 = input[currentPos + 7];
            }
            else
            {
                currentChar7 = '\0';
            }
            if (currentPos + 8 < input.Length)
            {
                currentChar8 = input[currentPos + 8];
            }
            else
            {
                currentChar8 = '\0';
            }
            if (currentPos + 9 < input.Length)
            {
                currentChar9 = input[currentPos + 9];
            }
            else
            {
                currentChar9 = '\0';
            }
        }
        public Token GetNextToken() // Lexer.
        {
            if (currentPos < input.Length)
            {
                currentChar = input[currentPos];
                preToken = preLexer[currentPos%preLexer.Count];
            }
            else
            {
                currentChar = '\0';
                preToken = null;
            }

            while (currentChar != '\0')
            {
                predictiveLookaheads();

                if (currentPos >= input.Length)
                {
                    Token token = new Token(TokenType.EOF, string.Empty, currentPos, preToken);

                    return token;
                }
                if (currentChar == 'v' && currentChar1 == 'a' && currentChar2 == 'r')
                {
                    Token token = new Token(TokenType.VarKeyword,
                        currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString(), 
                        currentPos, preToken
                    );
                    Advance();
                    Advance();
                    Advance();
                    return token;
                }
                if (currentChar == 'v' && currentChar1 == 'o' && currentChar2 == 'i' && currentChar3 == 'd')
                {
                    Token token = new Token(TokenType.VoidKeyword,
                        currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString() 
                        + currentChar3.ToString(), 
                        currentPos, 
                        preToken
                    );
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    return token;
                }
                if (currentChar == 'i' && currentChar1 == 'n' && currentChar2 == 't')
                {
                    Token token = new Token(TokenType.IntKeyword,
                        currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString(), 
                        currentPos, 
                        preToken
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
                        + currentChar3.ToString() + currentChar4.ToString() + currentChar5.ToString(), 
                        currentPos, 
                        preToken
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
                        + currentChar3.ToString() + currentChar4.ToString() + currentChar5.ToString(), 
                        currentPos, 
                        preToken
                    );
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    Advance();
                    return token;
                }
                if (currentChar == 'r' && currentChar1 == 'e' && currentChar2 == 't' && currentChar3 == 'u' && currentChar4 == 'r' && currentChar5 == 'n')
                {
                    Token token = new Token(TokenType.ReturnKeyword,
                        currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString()
                        + currentChar3.ToString() + currentChar4.ToString() + currentChar5.ToString(), 
                        currentPos, 
                        preToken
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
                    Token token = new Token(TokenType.Number, ParseNumber(), currentPos, preToken);
                    return token;
                }
                else if (char.IsLetter(currentChar))
                {
                    Token token = new Token(TokenType.Identifier, ParseIdentifier(), currentPos, preToken);
                    return token;
                }
                else if (currentChar == '+' || currentChar == '-' || currentChar == '*' || currentChar == '/' || currentChar == ',')
                {
                    Token token = new Token(TokenType.Operator, currentChar.ToString(), currentPos, preToken);
                    Advance();
                    return token;
                }
                else if (currentChar == '(' || currentChar == ')') // Handle parentheses
                {
                    Token token = new Token(TokenType.Parenthesis, currentChar.ToString(), currentPos, preToken);
                    Advance();
                    return token;
                }
                else if (currentChar == '=')
                {
                    Token token = new Token(TokenType.Assignment, currentChar.ToString(), currentPos, preToken);
                    Advance();
                    return token;
                }
                else if (currentChar == '.')
                {
                    Token token = new Token(TokenType.Peroid, currentChar.ToString(), currentPos, preToken);
                    Advance();
                    return token;
                }
                else if (currentChar == ';')
                {
                    Token token = new Token(TokenType.SemiColon, currentChar.ToString(), currentPos, preToken);
                    Advance();
                    return token;
                }
                else if (currentChar == '{')
                {
                    Token token = new Token(TokenType.CurleyBrace, currentChar.ToString(), currentPos, preToken);
                    Advance();
                    return token;
                }
                else if (currentChar == '}')
                {
                    Token token = new Token(TokenType.CurleyBrace, currentChar.ToString(), currentPos, preToken);
                    Advance();
                    return token;
                }
                else if (char.IsWhiteSpace(currentChar) || currentChar == ' ' || currentChar == '\t' || currentChar == '\n' || currentChar == '\r')
                {
                    Token token = new Token(TokenType.Whitespace, ParseWhitespace(), currentPos, preToken);
                    return token;
                }
                else
                {
                    Logger.Log.LogError("Invalid character found in input. Current position: " + GetPosition().ToString() 
                        + " offsetX: " + ((preToken != null) ? preToken.OffsetX.ToString() : "is null") 
                        + " offsetY: " + ((preToken != null) ? preToken.OffsetY.ToString() : "is null")
                    );

                    throw new Exception("Invalid character found in input. Current position: " + GetPosition().ToString()
                        + " offsetX: " + ((preToken != null) ? preToken.OffsetX.ToString() : "is null")
                        + " offsetY: " + ((preToken != null) ? preToken.OffsetY.ToString() : "is null")
                    );
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

        private string ParseWhitespace()
        {
            StringBuilder number = new StringBuilder();

            while (currentChar != '\0' && (char.IsWhiteSpace(currentChar) || currentChar == ' ' || currentChar == '\t' || currentChar == '\n' || currentChar == '\r'))
            {
                number.Append(currentChar);
                Advance();
            }

            return number.ToString();
        }
    }
}
