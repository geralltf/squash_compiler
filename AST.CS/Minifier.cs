using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SquashC.Compiler
{
    /// <summary>
    /// Tokens made at minifier stage comprising a value and debug info like positions within the file.
    /// </summary>
    public class PreToken
    {
        public string Value;
        public long Position;
        public long OffsetX;
        public long OffsetY;
        public PreToken(string val, long position, long offsetX, long offsetY) 
        {
            this.Value = val;
            this.Position = position;
            this.OffsetX = offsetX;
            this.OffsetY = offsetY;
        }
    }
    public class Minifier
    {
        static int currentPos;
        static char currentChar = '\0';
        static char currentChar1 = '\0';
        static char currentChar2 = '\0';
        static char currentChar3 = '\0';
        static char currentChar4 = '\0';
        static char currentChar5 = '\0';
        static char currentChar6 = '\0';
        static char currentChar7 = '\0';
        static char currentChar8 = '\0';
        static char currentChar9 = '\0';

        static string CHARSET = "ABCDEFGHIJabcdefghijklmnopqrstuvwxyz";

        static long id = 10;
        public static string newIdentifier()
        {
            string result = base10ToRadix36(id);

            id++;
            return result;
        }

        /// <summary>
        /// Converts a base 10 number into a base 36 useful for creating short identifiers quickly.
        /// </summary>
        public static string base10ToRadix36(long base10Number)
        {
            string charset = CHARSET;
            int radix = CHARSET.Length;
            const int longBits = 64;

            if (radix < 2 || radix > charset.Length)
            {
                Logger.Log.LogError("The radix must be >= 2 and <= " + charset.Length.ToString());
            }

            if (base10Number == 0)
            {
                return "0";
            }

            int index = longBits - 1;
            long currentNumber = Math.Abs(base10Number);
            char[] charArray = new char[longBits];

            while (currentNumber != 0)
            {
                int remainder = (int)(currentNumber % radix);
                charArray[index--] = charset[remainder];
                currentNumber = currentNumber / radix;
            }

            string result = new String(charArray, index + 1, longBits - index - 1);
            if (base10Number < 0)
            {
                result = "-" + result;
            }

            return result;
        }

        /// <summary>
        /// Cleans up the code ready for the compiler to work with by removing new lines, 
        /// tabs, spaces, and all whitespace.
        /// 
        /// Minifies the input string so to make it easier (and make it more efficient) 
        /// for the compiler to parse and compile.
        /// 
        /// Strips out single line comments and multi line comments.
        /// </summary>
        public static string MinifyCode(string input, ref List<PreToken> tokens)
        {
            string result = string.Empty;
            long offsetX = 0;
            long offsetY = 0;
            long originalSize = input.Length;
            bool isSinglelineComment = false;
            bool isMultilineComment = false;
            Dictionary<string, string> mapIdentifiers = new Dictionary<string, string>();

            for (currentPos = 0; currentPos < input.Length;)
            {
                currentChar = input[currentPos];

                predictiveLookaheads(input);

                // Strip out any single line comments and strip out any multiline comments.
                if (currentChar == '/' && currentChar1 == '/')
                {
                    isSinglelineComment = true;
                    currentPos += 2;
                }
                else if (currentChar == '\n' || currentChar == '\r')
                {
                    //if (currentChar1 == '\n' || currentChar1 == '\r')
                    //{
                    //    currentPos += 2;
                    //}
                    isSinglelineComment = false;
                }
                if (currentChar == '\n')
                {
                    // Since the char is a newline reset x-direction to zero to keep basis within the file construct.
                    offsetX = 0;
                    // Increment the offset in the y-direction since the char is a newline.
                    offsetY++;
                }
                else
                {
                    // Any char other than a newline moves in the x-direction.
                    offsetX++;
                }
                if (currentChar == '/' && currentChar1 == '*')
                {
                    isMultilineComment = true;
                    currentPos += 2;
                }
                else if (isMultilineComment && (currentChar == '*' && currentChar1 == '/'))
                {
                    isMultilineComment = false;
                    currentPos += 2;
                }
                if (!isSinglelineComment && !isMultilineComment)
                {
                    if (char.IsWhiteSpace(currentChar) || currentChar == ' ' || currentChar == '\t' || currentChar == '\n' || currentChar == '\r')
                    {
                        // Strip any whitespace except when it is needed to delimit a keyword.
                        currentPos++;
                    }
                    else if (currentChar == 'v' && currentChar1 == 'a' && currentChar2 == 'r')
                    {
                        PreToken tok = new PreToken(
                            currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString(),
                            currentPos,
                            offsetX,
                            offsetY
                        );
                        tokens.Add(tok);

                        result += currentChar;
                        result += currentChar1;
                        result += currentChar2;
                        currentPos += 3;

                        result += ' ';
                        currentPos++;
                    }
                    else if (currentChar == 'v' && currentChar1 == 'o' && currentChar2 == 'i' && currentChar3 == 'd')
                    {
                        PreToken tok = new PreToken(
                            currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString() + currentChar3.ToString(),
                            currentPos,
                            offsetX,
                            offsetY
                        );
                        tokens.Add(tok);

                        result += currentChar;
                        result += currentChar1;
                        result += currentChar2;
                        result += currentChar3;
                        currentPos += 4;

                        if(currentChar4 == ' ')
                        {
                            result += ' ';
                            currentPos++;
                        }
                    }
                    else if (currentChar == 'i' && currentChar1 == 'n' && currentChar2 == 't')
                    {
                        PreToken tok = new PreToken(
                            currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString(),
                            currentPos,
                            offsetX,
                            offsetY
                        );
                        tokens.Add(tok);

                        result += currentChar;
                        result += currentChar1;
                        result += currentChar2;
                        currentPos += 3;

                        result += ' ';
                        currentPos++;

                    }
                    else if (currentChar == 'd' && currentChar1 == 'o' && currentChar2 == 'u' && currentChar3 == 'b' && currentChar4 == 'l' && currentChar5 == 'e')
                    {
                        PreToken tok = new PreToken(
                            currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString() + currentChar3.ToString() + currentChar4.ToString() + currentChar5.ToString(),
                            currentPos,
                            offsetX,
                            offsetY
                        );
                        tokens.Add(tok);

                        result += currentChar;
                        result += currentChar1;
                        result += currentChar2;
                        result += currentChar3;
                        result += currentChar4;
                        result += currentChar5;
                        currentPos += 6;

                        result += ' ';
                        currentPos++;

                    }
                    else if (currentChar == 's' && currentChar1 == 't' && currentChar2 == 'r' && currentChar3 == 'i' && currentChar4 == 'n' && currentChar5 == 'g')
                    {
                        PreToken tok = new PreToken(
                            currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString() + currentChar3.ToString() + currentChar4.ToString() + currentChar5.ToString(),
                            currentPos,
                            offsetX,
                            offsetY
                        );
                        tokens.Add(tok);

                        result += currentChar;
                        result += currentChar1;
                        result += currentChar2;
                        result += currentChar3;
                        result += currentChar4;
                        result += currentChar5;
                        currentPos += 6;

                        result += ' ';
                        currentPos++;

                    }
                    else if (currentChar == 'r' && currentChar1 == 'e' && currentChar2 == 't' && currentChar3 == 'u' && currentChar4 == 'r' && currentChar5 == 'n')
                    {
                        PreToken tok = new PreToken(
                            currentChar.ToString() + currentChar1.ToString() + currentChar2.ToString() + currentChar3.ToString() + currentChar4.ToString() + currentChar5.ToString(),
                            currentPos,
                            offsetX,
                            offsetY
                        );
                        tokens.Add(tok);

                        result += currentChar;
                        result += currentChar1;
                        result += currentChar2;
                        result += currentChar3;
                        result += currentChar4;
                        result += currentChar5;
                        currentPos += 6;

                        result += ' ';
                        currentPos++;

                    }
                    else if (currentChar == '(' || currentChar == ')')
                    {
                        PreToken tok = new PreToken(
                            currentChar.ToString(),
                            currentPos,
                            offsetX,
                            offsetY
                        );
                        tokens.Add(tok);

                        result += currentChar;
                        currentPos++;
                    }
                    //TODO: Parse identifiers to shorten them without breaking ABIs perhaps don't change function calls.
                    //if (char.IsDigit(currentChar))
                    //{
                    //    string number = ParseNumber(ref input);

                        //    result += number;
                        //}
                        //else if (char.IsLetter(currentChar))
                        //{
                        //    string identifier = ParseIdentifier(ref input);
                        //    string identifierChanged;

                        //    if (!mapIdentifiers.ContainsKey(identifier))
                        //    {
                        //        identifierChanged = newIdentifier();

                        //        mapIdentifiers.Add(identifier, identifierChanged);
                        //    }
                        //    else
                        //    {
                        //        string mappedIdentifier;
                        //        mapIdentifiers.TryGetValue(identifier, out mappedIdentifier);

                        //        identifierChanged = mappedIdentifier;
                        //    }
                        //    result += identifierChanged;
                        //}
                    else
                    {
                        if (!isSinglelineComment || !isMultilineComment)
                        {
                            currentChar = input[currentPos];
                            if (!char.IsWhiteSpace(currentChar)
                                && currentChar != ' '
                                && currentChar != '\t'
                                && currentChar != '\n'
                                && currentChar != '\r')
                            {
                                result += currentChar;

                                // Preserve debug information about each char before any lossy minification.
                                PreToken tok = new PreToken(
                                    currentChar.ToString(),
                                    currentPos,
                                    offsetX,
                                    offsetY
                                );
                                tokens.Add(tok);
                            }
                        }

                        currentPos++;
                    }
                }
                else
                {
                    currentPos++;
                }
            }

            Logger.Log.LogInformation("Size: " + originalSize.ToString() + " Minified Size: " + result.Length.ToString());

            return result;
        }

        public static void Advance(ref string input)
        {
            currentPos++;
            UpdateChar(ref input);
        }

        public static void UpdateChar(ref string input)
        {
            if (currentPos < input.Length)
            {
                currentChar = input[currentPos];
            }
            else
            {
                currentChar = '\0';
            }
        }

        private static string ParseNumber(ref string input)
        {
            StringBuilder number = new StringBuilder();

            UpdateChar(ref input);

            while (currentChar != '\0' && (char.IsDigit(currentChar) || currentChar == '.' || currentChar == 'f'))
            {
                number.Append(currentChar);
                Advance(ref input);
            }

            return number.ToString();
        }

        private static string ParseIdentifier(ref string input)
        {
            StringBuilder identifier = new StringBuilder();

            UpdateChar(ref input);

            while (currentChar != '\0' && (char.IsLetterOrDigit(currentChar) || currentChar == '_'))
            {
                identifier.Append(currentChar);
                Advance(ref input);
            }

            return identifier.ToString();
        }

        private static void predictiveLookaheads(string input)
        {
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
    }
}
