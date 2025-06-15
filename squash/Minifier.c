#include "Minifier.h"

PreToken_t* PreToken_new(char* val, long position, long offsetX, long offsetY)
{
	PreToken_t* preToken = (PreToken_t*)malloc(sizeof(PreToken_t));
	if (preToken != NULL)
	{
		preToken->Value = val;
		preToken->Position = position;
		preToken->OffsetX = offsetX;
		preToken->OffsetY = offsetY;
		return preToken;
	}
	return NULL;
}


Minifier_t* Minifier_new()
{
	Minifier_t* result = (Minifier_t*)malloc(sizeof(Minifier_t));

	return result;
}

void Minifier_init(Minifier_t* minifier)
{
	minifier->currentChar = '\0';
	minifier->currentChar1 = '\0';
	minifier->currentChar2 = '\0';
	minifier->currentChar3 = '\0';
	minifier->currentChar4 = '\0';
	minifier->currentChar5 = '\0';
	minifier->currentChar6 = '\0';
	minifier->currentChar7 = '\0';
	minifier->currentChar8 = '\0';
	minifier->currentChar9 = '\0';

	minifier->id = 10;
}

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
char* MinifyCode(Minifier_t* minifier, char* input, int inputLength, list_t* tokens, int* newInputLength) // List<PreToken>
{
	list_t* tokens_result = list_new();
	char* result = '\0';
	long offsetX = 0;
	long offsetY = 0;
	long originalSize = inputLength;
	bool isSinglelineComment = false;
	bool isMultilineComment = false;
	//Dictionary<string, string> mapIdentifiers = new Dictionary<string, string>();

	for (minifier->currentPos = 0; minifier->currentPos < inputLength;) // input.Length
	{
		minifier->currentChar = input[minifier->currentPos];

		predictiveLookaheads(minifier, input, inputLength);

		// Strip out any single line comments and strip out any multiline comments.
		if (minifier->currentChar == '/' && minifier->currentChar1 == '/')
		{
			isSinglelineComment = true;
			minifier->currentPos += 2;
		}
		else if (minifier->currentChar == '\n' || minifier->currentChar == '\r')
		{
			//if (currentChar1 == '\n' || currentChar1 == '\r')
			//{
			//    currentPos += 2;
			//}
			isSinglelineComment = false;
		}
		if (minifier->currentChar == '\n')
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
		if (minifier->currentChar == '/' && minifier->currentChar1 == '*')
		{
			isMultilineComment = true;
			minifier->currentPos += 2;
		}
		else if (isMultilineComment && (minifier->currentChar == '*' && minifier->currentChar1 == '/'))
		{
			isMultilineComment = false;
			minifier->currentPos += 2;
		}
		if (!isSinglelineComment && !isMultilineComment)
		{
			if (isspace(minifier->currentChar) || minifier->currentChar == ' ' || minifier->currentChar == '\t' || minifier->currentChar == '\n' || minifier->currentChar == '\r')
			{
				// Strip any whitespace except when it is needed to delimit a keyword.
				minifier->currentPos++;
			}
			else if (minifier->currentChar == 'v' && minifier->currentChar1 == 'a' && minifier->currentChar2 == 'r')
			{
				char* currArray = (char*)malloc(sizeof(char) * 3);
				currArray[0] = minifier->currentChar;
				currArray[1] = minifier->currentChar1;
				currArray[2] = minifier->currentChar2;

				PreToken_t* tok = PreToken_new(currArray, minifier->currentPos, offsetX, offsetY);

				list_enqueue(tokens, (void*)tok);

				char* ch = &minifier->currentChar;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar1;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar2;
				list_enqueue(tokens_result, (void*)ch);

				//result += minifier->currentChar;
				//result += minifier->currentChar1;
				//result += minifier->currentChar2;
				minifier->currentPos += 3;

				char space = ' ';
				ch = &space;
				list_enqueue(tokens_result, (void*)ch);

				//result += ' ';
				minifier->currentPos++;
			}
			else if (minifier->currentChar == 'v' && minifier->currentChar1 == 'o' && minifier->currentChar2 == 'i' && minifier->currentChar3 == 'd')
			{
				char* currArray = (char*)malloc(sizeof(char) * 4);
				currArray[0] = minifier->currentChar;
				currArray[1] = minifier->currentChar1;
				currArray[2] = minifier->currentChar2;
				currArray[3] = minifier->currentChar3;

				PreToken_t* tok = PreToken_new(currArray, minifier->currentPos, offsetX, offsetY);

				list_enqueue(tokens, (void*)tok);

				char* ch = &minifier->currentChar;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar1;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar2;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar3;
				list_enqueue(tokens_result, (void*)ch);

				//result += currentChar;
				//result += currentChar1;
				//result += currentChar2;
				//result += currentChar3;
				minifier->currentPos += 4;

				if (minifier->currentChar4 == ' ')
				{
					char space = ' ';
					ch = &space;
					list_enqueue(tokens_result, (void*)ch);
					//result += ' ';

					minifier->currentPos++;
				}
			}
			else if (minifier->currentChar == 'i' && minifier->currentChar1 == 'n' && minifier->currentChar2 == 't')
			{
				char* currArray = (char*)malloc(sizeof(char) * 3);
				currArray[0] = minifier->currentChar;
				currArray[1] = minifier->currentChar1;
				currArray[2] = minifier->currentChar2;

				PreToken_t* tok = PreToken_new(currArray, minifier->currentPos, offsetX, offsetY);

				list_enqueue(tokens, (void*)tok);

				char* ch = &minifier->currentChar;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar1;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar2;
				list_enqueue(tokens_result, (void*)ch);

				//result += currentChar;
				//result += currentChar1;
				//result += currentChar2;
				minifier->currentPos += 3;

				char space = ' ';
				ch = &space;
				list_enqueue(tokens_result, (void*)ch);
				//result += ' ';
				minifier->currentPos++;

			}
			else if (minifier->currentChar == 'd' && minifier->currentChar1 == 'o' && minifier->currentChar2 == 'u' && minifier->currentChar3 == 'b' && minifier->currentChar4 == 'l' && minifier->currentChar5 == 'e')
			{
				char* currArray = (char*)malloc(sizeof(char) * 6);
				currArray[0] = minifier->currentChar;
				currArray[1] = minifier->currentChar1;
				currArray[2] = minifier->currentChar2;
				currArray[3] = minifier->currentChar3;
				currArray[4] = minifier->currentChar4;
				currArray[5] = minifier->currentChar5;

				PreToken_t* tok = PreToken_new(currArray, minifier->currentPos, offsetX, offsetY);

				list_enqueue(tokens, (void*)tok);

				char* ch = &minifier->currentChar;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar1;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar2;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar3;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar4;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar5;
				list_enqueue(tokens_result, (void*)ch);

				//result += currentChar;
				//result += currentChar1;
				//result += currentChar2;
				//result += currentChar3;
				//result += currentChar4;
				//result += currentChar5;

				minifier->currentPos += 6;

				char space = ' ';
				ch = &space;
				list_enqueue(tokens_result, (void*)ch);
				//result += ' ';
				minifier->currentPos++;

			}
			else if (minifier->currentChar == 's' && minifier->currentChar1 == 't' && minifier->currentChar2 == 'r' && minifier->currentChar3 == 'i' && minifier->currentChar4 == 'n' && minifier->currentChar5 == 'g')
			{
				char* currArray = (char*)malloc(sizeof(char) * 6);
				currArray[0] = minifier->currentChar;
				currArray[1] = minifier->currentChar1;
				currArray[2] = minifier->currentChar2;
				currArray[3] = minifier->currentChar3;
				currArray[4] = minifier->currentChar4;
				currArray[5] = minifier->currentChar5;

				PreToken_t* tok = PreToken_new(currArray, minifier->currentPos, offsetX, offsetY);

				list_enqueue(tokens, (void*)tok);

				char* ch = &minifier->currentChar;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar1;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar2;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar3;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar4;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar5;
				list_enqueue(tokens_result, (void*)ch);

				//result += currentChar;
				//result += currentChar1;
				//result += currentChar2;
				//result += currentChar3;
				//result += currentChar4;
				//result += currentChar5;

				minifier->currentPos += 6;

				char space = ' ';
				ch = &space;
				list_enqueue(tokens_result, (void*)ch);
				//result += ' ';
				minifier->currentPos++;

			}
			else if (minifier->currentChar == 'r' && minifier->currentChar1 == 'e' && minifier->currentChar2 == 't' && minifier->currentChar3 == 'u' && minifier->currentChar4 == 'r' && minifier->currentChar5 == 'n')
			{
				char* currArray = (char*)malloc(sizeof(char) * 6);
				currArray[0] = minifier->currentChar;
				currArray[1] = minifier->currentChar1;
				currArray[2] = minifier->currentChar2;
				currArray[3] = minifier->currentChar3;
				currArray[4] = minifier->currentChar4;
				currArray[5] = minifier->currentChar5;

				PreToken_t* tok = PreToken_new(currArray, minifier->currentPos, offsetX, offsetY);

				list_enqueue(tokens, (void*)tok);

				char* ch = &minifier->currentChar;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar1;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar2;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar3;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar4;
				list_enqueue(tokens_result, (void*)ch);
				ch = &minifier->currentChar5;
				list_enqueue(tokens_result, (void*)ch);

				//result += currentChar;
				//result += currentChar1;
				//result += currentChar2;
				//result += currentChar3;
				//result += currentChar4;
				//result += currentChar5;
				minifier->currentPos += 6;

				if (minifier->currentChar6 == ' ')
				{
					char space = ' ';
					ch = &space;
					list_enqueue(tokens_result, (void*)ch);
					//result += ' ';
					minifier->currentPos++;
				}
			}
			else if (minifier->currentChar == '(' || minifier->currentChar == ')')
			{
				char* currArray = (char*)malloc(sizeof(char) * 1);
				currArray[0] = minifier->currentChar;

				PreToken_t* tok = PreToken_new(currArray, minifier->currentPos, offsetX, offsetY);

				list_enqueue(tokens, (void*)tok);

				char* ch = &minifier->currentChar;
				list_enqueue(tokens_result, (void*)ch);

				//result += currentChar;
				minifier->currentPos++;
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
					minifier->currentChar = input[minifier->currentPos];
					if (!isspace(minifier->currentChar)
						&& minifier->currentChar != ' '
						&& minifier->currentChar != '\t'
						&& minifier->currentChar != '\n'
						&& minifier->currentChar != '\r')
					{

						char* ch = &minifier->currentChar;
						list_enqueue(tokens_result, (void*)ch);
						//result += currentChar;

						char* currArray = (char*)malloc(sizeof(char) * 1);
						currArray[0] = minifier->currentChar;

						// Preserve debug information about each char before any lossy minification.
						PreToken_t* tok = PreToken_new(currArray, minifier->currentPos, offsetX, offsetY);

						list_enqueue(tokens, (void*)tok);

						//PreToken tok = new PreToken(
						//    currentChar.ToString(),
						//    currentPos,
						//    offsetX,
						//    offsetY
						//);
						//tokens.Add(tok);
					}
				}

				minifier->currentPos++;
			}
		}
		else
		{
			minifier->currentPos++;
		}
	}

	list_t* n = tokens_result;
	list_t* p = NULL;
	int count = 0;

	while (n != NULL)
	{
		count++;

		p = n;
		n = n->next;
	}

	result = (char*)malloc(sizeof(char) * count);
	int index = 0;

	n = tokens_result;
	p = NULL;

	while (n != NULL)
	{
		char d = *(char*)n->data;
		result[index] = d;

		index++;
		p = n;
		n = n->next;
	}

	*newInputLength = count;

	//Logger.Log.LogInformation("Size: " + originalSize.ToString() + " Minified Size: " + result.Length.ToString());

	return result;
}

void Advance(struct Lexer* lexer, Minifier_t* minifier)
{
	minifier->currentPos++;
	UpdateChar(lexer, minifier);
}

void UpdateChar(struct Lexer* lexer, Minifier_t* minifier)
{
	if (minifier->currentPos < lexer->inputLength)
	{
		minifier->currentChar = lexer->input[minifier->currentPos];
	}
	else
	{
		minifier->currentChar = '\0';
	}
}

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

void predictiveLookaheads(Minifier_t* minifier, char* input, int inputLength)
{
	if (minifier->currentPos + 1 < inputLength)
	{
		minifier->currentChar1 = input[minifier->currentPos + 1];
	}
	else
	{
		minifier->currentChar1 = '\0';
	}
	if (minifier->currentPos + 2 < inputLength)
	{
		minifier->currentChar2 = input[minifier->currentPos + 2];
	}
	else
	{
		minifier->currentChar2 = '\0';
	}
	if (minifier->currentPos + 3 < inputLength)
	{
		minifier->currentChar3 = input[minifier->currentPos + 3];
	}
	else
	{
		minifier->currentChar3 = '\0';
	}
	if (minifier->currentPos + 4 < inputLength)
	{
		minifier->currentChar4 = input[minifier->currentPos + 4];
	}
	else
	{
		minifier->currentChar4 = '\0';
	}
	if (minifier->currentPos + 5 < inputLength)
	{
		minifier->currentChar5 = input[minifier->currentPos + 5];
	}
	else
	{
		minifier->currentChar5 = '\0';
	}
	if (minifier->currentPos + 6 < inputLength)
	{
		minifier->currentChar6 = input[minifier->currentPos + 6];
	}
	else
	{
		minifier->currentChar6 = '\0';
	}
	if (minifier->currentPos + 7 < inputLength)
	{
		minifier->currentChar7 = input[minifier->currentPos + 7];
	}
	else
	{
		minifier->currentChar7 = '\0';
	}
	if (minifier->currentPos + 8 < inputLength)
	{
		minifier->currentChar8 = input[minifier->currentPos + 8];
	}
	else
	{
		minifier->currentChar8 = '\0';
	}
	if (minifier->currentPos + 9 < inputLength)
	{
		minifier->currentChar9 = input[minifier->currentPos + 9];
	}
	else
	{
		minifier->currentChar9 = '\0';
	}
}