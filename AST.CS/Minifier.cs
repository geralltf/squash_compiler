using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SquashC.Compiler
{
    public class Minifier
    {
        /// <summary>
        /// Cleans up the code ready for the compiler to work with by removing new lines, 
        /// tabs, spaces, and all whitespace.
        /// 
        /// Minifies the input string so to make it easier (and make it more efficient) 
        /// for the compiler to parse and compile.
        /// 
        /// Strips out single line comments and multi line comments.
        /// </summary>
        public static string MinifyCode(string input)
        {
            string result = string.Empty;
            int currentPos;
            char currentChar = '\0';
            char currentChar1 = '\0';
            char currentChar2 = '\0';
            char currentChar3 = '\0';
            char currentChar4 = '\0';
            char currentChar5 = '\0';
            char currentChar6 = '\0';
            char currentChar7 = '\0';
            char currentChar8 = '\0';
            char currentChar9 = '\0';
            bool isSinglelineComment = false;
            bool isMultilineComment = false;

            for (currentPos = 0; currentPos < input.Length;)
            {
                currentChar = input[currentPos];
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
                if (!isSinglelineComment && !isMultilineComment)// || !isMultilineComment)
                {
                    if (char.IsWhiteSpace(currentChar) || currentChar == ' ' || currentChar == '\t' || currentChar == '\n' || currentChar == '\r')
                    {
                        // Strip any whitespace except when it is needed to delimit a keyword.
                        currentPos++;
                    }
                    else if (currentChar == 'v' && currentChar1 == 'a' && currentChar2 == 'r')
                    {
                        result += currentChar;
                        result += currentChar1;
                        result += currentChar2;
                        currentPos += 3;

                        result += ' ';
                        currentPos++;

                    }
                    else if (currentChar == 'i' && currentChar1 == 'n' && currentChar2 == 't')
                    {
                        result += currentChar;
                        result += currentChar1;
                        result += currentChar2;
                        currentPos += 3;

                        result += ' ';
                        currentPos++;

                    }
                    else if (currentChar == 'd' && currentChar1 == 'o' && currentChar2 == 'u' && currentChar3 == 'b' && currentChar4 == 'l' && currentChar5 == 'e')
                    {
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

            return result;
        }
    }
}
