#ifndef MINIFIER_H
#define MINIFIER_H

#include <stdlib.h>
#include <ctype.h>

#include "List.h"
#include "Lexer.h"

//const char* CHARSET = "ABCDEFGHIJabcdefghijklmnopqrstuvwxyz";
//const int charsetLength = 36;

/// <summary>
/// Tokens made at minifier stage comprising a value and debug info like positions within the file.
/// </summary>
typedef struct PreToken
{
	char* Value;
	long Position;
	long OffsetX;
	long OffsetY;
} PreToken_t;

typedef struct Minifier
{
	int currentPos;
	char currentChar;
	char currentChar1;
	char currentChar2;
	char currentChar3;
	char currentChar4;
	char currentChar5;
	char currentChar6;
	char currentChar7;
	char currentChar8;
	char currentChar9;
	long id;
} Minifier_t;

PreToken_t* PreToken_new(char* val, long position, long offsetX, long offsetY);
Minifier_t* Minifier_new();
void Minifier_init(Minifier_t* minifier);

//char* newIdentifier(Minifier_t* minifier)
//{
//    char* result = base10ToRadix36(minifier->id);
//
//    minifier->id++;
//
//    return result;
//}
//
///// <summary>
///// Converts a base 10 number into a base 36 useful for creating short identifiers quickly.
///// </summary>
//char* base10ToRadix36(long base10Number, Minifier_t* minifier)
//{
//    char* charset = CHARSET;
//    int radix = charsetLength;
//    const int longBits = 64;
//
//    if (radix < 2 || radix > charsetLength)
//    {
//        //Logger.Log.LogError("base10ToRadix36(): The radix must be >= 2 and <= " + charset.Length.ToString());
//    }
//
//    if (base10Number == 0)
//    {
//        return "0";
//    }
//
//    int index = longBits - 1;
//    long currentNumber = Math.Abs(base10Number);
//    char[] charArray = new char[longBits];
//
//    while (currentNumber != 0)
//    {
//        int remainder = (int)(currentNumber % radix);
//        charArray[index--] = charset[remainder];
//        currentNumber = currentNumber / radix;
//    }
//
//    string result = new String(charArray, index + 1, longBits - index - 1);
//    if (base10Number < 0)
//    {
//        result = "-" + result;
//    }
//
//    return result;
//}

/// <summary>
/// Cleans up the code ready for the compiler to work with by removing new lines, 
/// tabs, spaces, and all whitespace.
/// 
/// Minifies the input string so to make it easier (and make it more efficient) 
/// for the compiler to parse and compile.
/// 
/// Strips out single line comments and multi line comments.
/// </summary>
char* MinifyCode(Minifier_t* minifier, char* input, int inputLength, list_t* tokens, int* newInputLength); // List<PreToken>
void Advance(struct Lexer* lexer, Minifier_t* minifier);
void UpdateChar(struct Lexer* lexer, Minifier_t* minifier);

//private static string ParseNumber(ref string input)
//{
//    StringBuilder number = new StringBuilder();
//
//    UpdateChar(ref input);
//
//    while (currentChar != '\0' && (char.IsDigit(currentChar) || currentChar == '.' || currentChar == 'f'))
//    {
//        number.Append(currentChar);
//        Advance(ref input);
//    }
//
//    return number.ToString();
//}
//
//private static string ParseIdentifier(ref string input)
//{
//    StringBuilder identifier = new StringBuilder();
//
//    UpdateChar(ref input);
//
//    while (currentChar != '\0' && (char.IsLetterOrDigit(currentChar) || currentChar == '_'))
//    {
//        identifier.Append(currentChar);
//        Advance(ref input);
//    }
//
//    return identifier.ToString();
//}

void predictiveLookaheads(Minifier_t* minifier, char* input, int inputLength);


#endif