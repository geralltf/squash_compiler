using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace AST.CS
{
    public class AST
    {
        public string varName;
        public string operandLeft;
        public string operandRight;
        public OperatorType operatorType;
        public OperatorType nextOprType;
        public AST leftChild;
        public AST rightChild;
        public int precedence;

        public enum OperatorType
        {
            MULTIPLY,
            DIVIDE,
            ADD,
            SUBTRACT,
            UNDEFINED
        }
        public enum OperatorPrecedence
        {
            AST_MULTIPLY = 50,
            AST_DIVIDE = 50,
            AST_REMAINDER = 50,
            AST_ADD = 45,
            AST_SUBTRACT = 45,
            AST_UNDEFINED = 0
        }
        public enum AST_ENUM_TOKEN
        {
            AST_TOKEN_BEGIN = 0xF000,
            AST_TOKEN_END = 0xF001,
            AST_WHITESPACE = 0x00,
            AST_COMMENT_BEGIN = 0x01,
            AST_COMMENT_END = 0x02,
            AST_MULTILINE_COMMENT_BEGIN = 0x03,
            AST_MULTILINE_COMMENT_END = 0x04,
            AST_SEMI_COLON = 0x05,
            AST_VAR_TYPE = 0x06,
            AST_VAR_SIG = 0x07,
            AST_ASSIGN = 0x08,
            AST_OP_PVE = 0x09,
            AST_OP_SUB = 0x0A,
            AST_OP_DIV = 0x0B,
            AST_OP_MUL = 0x0C,
            AST_FUN_DEF = 0x0D, // FUNCTION SIGNATURE. void Function(int a, int b) {}
            AST_FUN_NAME = 0x0E,    // FUNCTION NAME
            AST_FUN_TRAIT = 0x0F,   // FUNCTION TRAIT
            AST_FUN_SPEC = 0x10, // FUNCTION SPECIFICATION. void Function(int a, int b);
            AST_FUN_SCOPE_BEGIN = 0x11,
            AST_FUN_SCOPE_END = 0x12,
            AST_FUN_INVOKE = 0x13, // F();
            AST_STRUCT_NAME = 0x14,
            AST_STRUCT_TYPE = 0x15,
            AST_CLASS_NAME = 0x16,
            AST_CLASS_TYPE = 0x17,
            AST_CLASS_SCOPE_BEGIN = 0x18,
            AST_CLASS_SCOPE_END = 0x19,
            AST_TYPEDEF_KEYWORD = 0x1A, // typedef type new_name_t
            AST_TYPEDEF_TYPE = 0x1B,
            AST_TYPEDEF_DEFINITION = 0x1C,

            AST_MULTILINE_WHITESPACE = 0xFF,

            AST_PARENTHESIS_BEGIN = 0x1D,
            AST_PARENTHESIS_END = 0x1E,
            AST_MULTIPLY = 0x1F,
            AST_PLUS = 0x20,
            AST_SUBTRACT = 0x21,
            AST_DIV = 0x22,
            AST_MOD = 0x23,
            AST_XOR = 0x24,
            AST_IF = 0x25,

            AST_A = 0x26,
            AST_B = 0x27,
            AST_C = 0x28,
            AST_D = 0x29,
            AST_E = 0x2A,
            AST_F = 0x2B,
            AST_G = 0x2C,
            AST_H = 0x2D,
            AST_I = 0x2E,
            AST_J = 0x2F,
            AST_K = 0x30,
            AST_L = 0x31,
            AST_M = 0x32,
            AST_N = 0x33,
            AST_O = 0x34,
            AST_P = 0x35,
            AST_Q = 0x36,
            AST_R = 0x37,
            AST_S = 0x38,
            AST_T = 0x39,
            AST_U = 0x3A,
            AST_V = 0x3B,
            AST_W = 0x3C,
            AST_X = 0x3D,
            AST_Y = 0x3E,
            AST_Z = 0x3F,

            AST_a = 0x40,
            AST_b = 0x41,
            AST_c = 0x42,
            AST_d = 0x43,
            AST_e = 0x44,
            AST_f = 0x45,
            AST_g = 0x46,
            AST_h = 0x47,
            AST_i = 0x48,
            AST_j = 0x49,
            AST_k = 0x4A,
            AST_l = 0x4B,
            AST_m = 0x4C,
            AST_n = 0x4D,
            AST_o = 0x4E,
            AST_p = 0x4F,
            AST_q = 0x50,
            AST_r = 0x51,
            AST_s = 0x52,
            AST_t = 0x53,
            AST_u = 0x54,
            AST_v = 0x55,
            AST_w = 0x56,
            AST_x = 0x57,
            AST_y = 0x58,
            AST_z = 0x59,

            AST_0 = 0x5A,
            AST_1 = 0x5B,
            AST_2 = 0x5C,
            AST_3 = 0x5D,
            AST_4 = 0x5E,
            AST_5 = 0x5F,
            AST_6 = 0x60,
            AST_7 = 0x61,
            AST_8 = 0x62,
            AST_9 = 0x63,

            AST_INT = 0x64,
            AST_SHORT = 0x65,
            AST_BOOLEAN = 0x66,

            AST_FOR = 0x67,
            AST_WHILE = 0x68,
            AST_DO = 0x69,
            AST_EQUALITY = 0x6A,        // ==
            AST_NOT_EQUAL_TO = 0x6B,    // !=
            AST_FULL_STOP = 0x6C,

            /// <summary>
            /// Bitwise compliment (~).
            /// </summary>
            AST_TILDE = 0x6D, 

            AST_DECREMENT = 0x6E,
            AST_INCREMENT = 0x6F,
            AST_NEGATION = 0x70,

            //AST_TOKEN_WILDCARD = 0xBAD, // Twaoken wild card is Uno.
            AST_UNDEFINED = 0xFFFF

            //reserved keywords
            //preprocessor directive, macro, inline, class, function, scope, branch, enum, string, float, int, short, long, char/byte, bool, pointer arithmetic
            //sys library stuff; 1D array, 2D array, indicies, vectors, imaginary, integral, factorisation, anim curves, prime, mat, arithmetic, map, list, stack, heap, priority que, scene graph, db without file system, tex compr, temporal ADTs
            //geometry, 
            //h.264x,h.265x,webp/vp9.
            // Const correctness.
            // static function const correctness.
            // binary signature, assembly signature, function signature
        };

        public enum AST_ENUM_TYPE
        {
            AST_STRING = 0x00,
            AST_BYTE = 0x01,
            AST_INT = 0x02,
            AST_UINT = 0x03,
            AST_SHORT = 0x04,
            AST_USHORT = 0x05,
            AST_ULONG = 0x06,
            AST_LONG = 0x07,
            AST_FLOAT = 0x08,
            AST_DOUBLE = 0x09,
            AST_VAR = 0x0A,
            AST_MAT3x3 = 0x0B,
            AST_MAT4x4 = 0x0C,
            AST_MAT8x8 = 0x0D,
            AST_MAT10x10 = 0x0E,
            AST_MAT12x12 = 0x0F,
            AST_MAT13x13 = 0x10,
            AST_MAT16x16 = 0x11,
            AST_MAT20x20 = 0x12,
            AST_MATNxN = 0x13,
            AST_MATNxM = 0x14,
            AST_BOOLEAN = 0x15,
            AST_MULTI_STRING = 0xFF,
            AST_TYPE_UNDEFINED = 0xFFF
        };

        public static string Reverse(string s)
        {
            char[] charArray = s.ToCharArray();
            Array.Reverse(charArray);
            return new string(charArray);
        }

        /// <summary>
        /// Finds a variable name given found assignment operator '-' starting index.
        /// </summary>
        /// <param name="startingIndex"></param>
        /// <param name="lexer"></param>
        /// <returns></returns>
        public static string FindVarName(int startingIndex, List<AST_ENUM_TOKEN> lexer)
        {
            string varName = string.Empty;
            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;
            bool working = true;
            for (int i = startingIndex - 1; (i >= 0) && working; i--)
            {
                token_type = lexer[i];

                if (token_type == AST_ENUM_TOKEN.AST_INT || token_type == AST_ENUM_TOKEN.AST_SHORT)
                {
                    working = false;
                }
                else if (isAlphaNumeric(token_type))
                {
                    varName += ParseString(token_type);
                }
                else if (token_type == AST_ENUM_TOKEN.AST_WHITESPACE)
                {

                }
            }
            varName = Reverse(varName);
            return varName;
        }

        public static string FindOperandLeft(int startingIndex, List<AST_ENUM_TOKEN> lexer)
        {
            string varName = string.Empty;
            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;
            bool working = true;
            for (int i = startingIndex - 1; (i >= 0) && working; i--)
            {
                token_type = lexer[i];

                if (token_type == AST_ENUM_TOKEN.AST_INT || token_type == AST_ENUM_TOKEN.AST_SHORT)
                {
                    working = false;
                }
                else if (isAlphaNumeric(token_type))
                {
                    varName += ParseString(token_type);
                }
                else if (token_type == AST_ENUM_TOKEN.AST_WHITESPACE)
                {

                }
                else if (token_type == AST_ENUM_TOKEN.AST_MULTIPLY)
                {
                    working = false;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_DIV)
                {
                    working = false;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_PLUS)
                {
                    working = false;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_SUBTRACT)
                {
                    working = false;
                }
            }
            varName = Reverse(varName);
            return varName;
        }

        public static OperatorType GetNextOperatorType(int index, List<AST_ENUM_TOKEN> lexer)
        {
            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;

            for (int i = index + 1; i < lexer.Count; i++)
            {
                token_type = lexer[i];
                if (token_type == AST_ENUM_TOKEN.AST_SEMI_COLON)
                {
                    continue;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_MULTIPLY)
                {
                    return OperatorType.MULTIPLY;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_DIV)
                {
                    return OperatorType.DIVIDE;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_PLUS)
                {
                    return OperatorType.ADD;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_SUBTRACT)
                {
                    return OperatorType.SUBTRACT;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_FOR)
                {
                    continue;
                }
                else
                {
                    continue;
                }
            }

            return OperatorType.UNDEFINED;
        }


        public static int GetCurrPrecedence(int defaultPrecedence, int index, AST_ENUM_TOKEN token, List<AST_ENUM_TOKEN> lexer)
        {
            int precedence = defaultPrecedence;
            AST_ENUM_TOKEN tok;
            for(int j = index; j< lexer.Count;j++)
            {
                tok = lexer[j];

                if (tok == AST_ENUM_TOKEN.AST_MULTIPLY)
                {
                    return (int)OperatorPrecedence.AST_MULTIPLY;
                }
                else if (tok == AST_ENUM_TOKEN.AST_DIV)
                {
                    return (int)OperatorPrecedence.AST_DIVIDE;
                }
                else if (tok == AST_ENUM_TOKEN.AST_PLUS)
                {
                    return (int)OperatorPrecedence.AST_ADD;
                }
                else if (tok == AST_ENUM_TOKEN.AST_SUBTRACT)
                {
                    return (int)OperatorPrecedence.AST_SUBTRACT;
                }
            }

            return (int)OperatorPrecedence.AST_UNDEFINED;
        }
        //TODO: parse tenery operator. parse unary operator.
        public static AST? ParseBinaryOperator(ref int index, List<AST_ENUM_TOKEN> lexer, ref AST parentAST, ref AST rootAST)
        {
            AST ast = new AST();
            AST_ENUM_TOKEN tok = lexer[index];
            int i = index + 1;

            ast.operatorType = GetNextOperatorType(index - 1, lexer);
            ast.nextOprType = GetNextOperatorType(index, lexer);
            ast.precedence = GetCurrPrecedence(0, index, tok, lexer);
            ast.operandLeft = FindOperandLeft(index, lexer);
            ast.operandRight = FindOperandRight(index, lexer, ref i);
            index = i;

            if (rootAST == null)
            {
                rootAST = ast;
            }
            if (parentAST == null)
            {
                parentAST = ast;
            }
            else
            {
                if (ast != null)
                {
                    ast.leftChild = parentAST;
                }

                if (parentAST.leftChild == null)
                {
                    parentAST.leftChild = ast;
                }
                else if (parentAST.rightChild == null)
                {
                    parentAST.rightChild = ast;
                }

                if (ast.nextOprType != OperatorType.UNDEFINED)
                {
                    AST astPrior = new AST();

                    astPrior.operatorType = ast.nextOprType;
                    astPrior.leftChild = parentAST;
                    astPrior.rightChild = ast;

                    return astPrior;
                }
                else
                {
                    parentAST = ast;

                    return ast;
                }
            }
            return ast;
        }

        public static string FindOperandRightLookAhead(int index, List<AST_ENUM_TOKEN> lexer)
        {
            string varValue = string.Empty;
            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;
            bool isForLoop = false;
            bool working = true;
            bool first = false;
            //bool second = false;

            for (int i = index + 1; i < lexer.Count && working; i++)
            {
                token_type = lexer[i];
                if (token_type == AST_ENUM_TOKEN.AST_SEMI_COLON)
                {
                    if (isForLoop == false)
                    {
                        working = false;
                    }
                }
                else if (token_type == AST_ENUM_TOKEN.AST_MULTIPLY)
                {
                    //working = false;
                    if(!first)
                    {
                        first = true;
                    }
                    else
                    {
                        working = false;
                    }
                }
                else if (token_type == AST_ENUM_TOKEN.AST_DIV)
                {
                    //working = false;
                    if (!first)
                    {
                        first = true;
                    }
                    else
                    {
                        working = false;
                    }
                }
                else if (token_type == AST_ENUM_TOKEN.AST_PLUS)
                {
                    //working = false;
                    if (!first)
                    {
                        first = true;
                    }
                    else
                    {
                        working = false;
                    }
                }
                else if (token_type == AST_ENUM_TOKEN.AST_SUBTRACT)
                {
                    //working = false;
                    if (!first)
                    {
                        first = true;
                    }
                    else
                    {
                        working = false;
                    }
                }
                else if (token_type == AST_ENUM_TOKEN.AST_FOR)
                {
                    isForLoop = true;
                }
                else
                {
                    if(first)
                    {
                        varValue += ParseString(token_type);
                    }
                }
            }

            return varValue;
        }
        public static string FindOperandRight(int index, List<AST_ENUM_TOKEN> lexer, ref int outIndex)
        {
            string varValue = string.Empty;
            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;
            bool isForLoop = false;
            bool working = true;
            int i;

            for (i = index + 1; i < lexer.Count && working; i++)
            {
                token_type = lexer[i];

                if (token_type == AST_ENUM_TOKEN.AST_SEMI_COLON)
                {
                    if (isForLoop == false)
                    {
                        working = false;
                    }
                }
                else if (token_type == AST_ENUM_TOKEN.AST_MULTIPLY)
                {
                    working = false;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_DIV)
                {
                    working = false;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_PLUS)
                {
                    working = false;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_SUBTRACT)
                {
                    working = false;
                }
                else if (token_type == AST_ENUM_TOKEN.AST_FOR)
                {
                    isForLoop = true;
                }
                else
                {
                    varValue += ParseString(token_type);
                }
            }

            outIndex = i;

            return varValue;
        }

        public static List<AST_ENUM_TOKEN> FindVarValue(int index, List<AST_ENUM_TOKEN> lexer)
        {
            List<AST_ENUM_TOKEN> valueTokens = new List<AST_ENUM_TOKEN>();
            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;
            bool isForLoop = false;
            bool working = true;

            for (int i = index + 1; i < lexer.Count && working; i++)
            {
                token_type = lexer[i];

                if (token_type == AST_ENUM_TOKEN.AST_SEMI_COLON)
                {
                    if (isForLoop == false)
                    {
                        working = false;
                    }
                }
                else if (token_type == AST_ENUM_TOKEN.AST_FOR)
                {
                    isForLoop = true;
                }
                else
                {
                    valueTokens.Add(token_type);
                }
            }

            return valueTokens;
        }

        public static bool isAlphaNumeric(AST_ENUM_TOKEN token)
        {
            int tokenInt = (int)token;
            if (tokenInt >= 0x26 && tokenInt <= 0x3F)
            {
                return true;
            }
            if (tokenInt >= 0x40 && tokenInt <= 0x59)
            {
                return true;
            }
            if (tokenInt >= 0x5A && tokenInt <= 0x63)
            {
                return true;
            }
            return false;
        }

        public static bool isNumeric(AST_ENUM_TOKEN token)
        {
            int tokenInt = (int)token;
            if (tokenInt >= 0x5A && tokenInt <= 0x63)
            {
                return true;
            }
            if(token == AST_ENUM_TOKEN.AST_FULL_STOP)
            {
                return true;
            }
            return false;
        }

        public static bool isWhiteSpace(AST_ENUM_TOKEN token)
        {
            return token == AST_ENUM_TOKEN.AST_WHITESPACE;
        }

        public static List<AST> Expr(string expr)
        {
            var tokens = AST.Lexer(expr);

            List<AST> computeResult = AST.Parser(tokens);

            return computeResult;
        }

        public static string ParseString(AST_ENUM_TOKEN token)
        {
            string result = string.Empty;

            int tokenInt = (int)token;

            if (tokenInt >= 0x26 && tokenInt <= 0x3F)
            {
                switch (tokenInt)
                {
                    case 0x26:
                        result += 'A';
                        break;
                    case 0x27:
                        result += 'B';
                        break;
                    case 0x28:
                        result += 'C';
                        break;
                    case 0x29:
                        result += 'D';
                        break;
                    case 0x2A:
                        result += 'E';
                        break;
                    case 0x2B:
                        result += 'F';
                        break;
                    case 0x2C:
                        result += 'G';
                        break;
                    case 0x2D:
                        result += 'H';
                        break;
                    case 0x2E:
                        result += 'I';
                        break;
                    case 0x2F:
                        result += 'J';
                        break;
                    case 0x30:
                        result += 'K';
                        break;
                    case 0x31:
                        result += 'L';
                        break;
                    case 0x32:
                        result += 'M';
                        break;
                    case 0x33:
                        result += 'N';
                        break;
                    case 0x34:
                        result += 'O';
                        break;
                    case 0x35:
                        result += 'P';
                        break;
                    case 0x36:
                        result += 'Q';
                        break;
                    case 0x37:
                        result += 'R';
                        break;
                    case 0x38:
                        result += 'S';
                        break;
                    case 0x39:
                        result += 'T';
                        break;
                    case 0x3A:
                        result += 'U';
                        break;
                    case 0x3B:
                        result += 'V';
                        break;
                    case 0x3C:
                        result += 'W';
                        break;
                    case 0x3D:
                        result += 'X';
                        break;
                    case 0x3E:
                        result += 'Y';
                        break;
                    case 0x3F:
                        result += 'Z';
                        break;
                }
            }

            if (tokenInt >= 0x40 && tokenInt <= 0x59)
            {
                switch (tokenInt)
                {
                    case 0x40:
                        result += 'a';
                        break;
                    case 0x41:
                        result += 'b';
                        break;
                    case 0x42:
                        result += 'c';
                        break;
                    case 0x43:
                        result += 'd';
                        break;
                    case 0x44:
                        result += 'e';
                        break;
                    case 0x45:
                        result += 'f';
                        break;
                    case 0x46:
                        result += 'g';
                        break;
                    case 0x47:
                        result += 'h';
                        break;
                    case 0x48:
                        result += 'i';
                        break;
                    case 0x49:
                        result += 'j';
                        break;
                    case 0x4A:
                        result += 'k';
                        break;
                    case 0x4B:
                        result += 'l';
                        break;
                    case 0x4C:
                        result += 'm';
                        break;
                    case 0x4D:
                        result += 'n';
                        break;
                    case 0x4E:
                        result += 'o';
                        break;
                    case 0x4F:
                        result += 'p';
                        break;
                    case 0x50:
                        result += 'q';
                        break;
                    case 0x51:
                        result += 'r';
                        break;
                    case 0x52:
                        result += 's';
                        break;
                    case 0x53:
                        result += 't';
                        break;
                    case 0x54:
                        result += 'u';
                        break;
                    case 0x55:
                        result += 'v';
                        break;
                    case 0x56:
                        result += 'w';
                        break;
                    case 0x57:
                        result += 'x';
                        break;
                    case 0x58:
                        result += 'y';
                        break;
                    case 0x59:
                        result += 'z';
                        break;
                }
            }
            if (tokenInt >= 0x5A && tokenInt <= 0x63)
            {
                switch (tokenInt)
                {
                    case 0x5A:
                        result += '0';
                        break;
                    case 0x5B:
                        result += '1';
                        break;
                    case 0x5C:
                        result += '2';
                        break;
                    case 0x5D:
                        result += '3';
                        break;
                    case 0x5E:
                        result += '4';
                        break;
                    case 0x5F:
                        result += '5';
                        break;
                    case 0x60:
                        result += '6';
                        break;
                    case 0x61:
                        result += '7';
                        break;
                    case 0x62:
                        result += '8';
                        break;
                    case 0x63:
                        result += '9';
                        break;
                }
            }

            return result;
        }

        public static string ParseString(List<AST_ENUM_TOKEN> lexer, int index)
        {
            string result = string.Empty;

            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;

            for (int i = 0; i < lexer.Count; i++)
            {
                token_type = lexer[i];

                result += ParseString(token_type);
            }

            return result;
        }

        public static List<AST> Parser(List<AST_ENUM_TOKEN> lexer)
        {
            List<AST> results = new List<AST>();
            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;
            string? varName = null;
            AST_ENUM_TYPE variableType = AST_ENUM_TYPE.AST_TYPE_UNDEFINED;
            bool functionScope = false;
            bool parenthesisScope = false;
            bool multilineComment = false;
            bool singleComment = false;
            AST? parentAST = null;
            AST rootAST = null;

            for (int i = 0; i < lexer.Count; i++)
            {
                token_type = lexer[i];

                if (token_type == AST_ENUM_TOKEN.AST_MULTILINE_COMMENT_BEGIN)
                {
                    multilineComment = true;
                }
                if (token_type == AST_ENUM_TOKEN.AST_MULTILINE_COMMENT_END)
                {
                    multilineComment = false;
                }
                if (token_type == AST_ENUM_TOKEN.AST_COMMENT_BEGIN)
                {
                    singleComment = true;
                }
                if (token_type == AST_ENUM_TOKEN.AST_COMMENT_END)
                {
                    singleComment = false;
                }
                if (token_type == AST_ENUM_TOKEN.AST_PARENTHESIS_BEGIN)
                {
                    parenthesisScope = true;
                }
                if (token_type == AST_ENUM_TOKEN.AST_PARENTHESIS_END)
                {
                    parenthesisScope = false;
                }
                if (token_type == AST_ENUM_TOKEN.AST_INT) // int
                {
                    variableType = AST_ENUM_TYPE.AST_INT;
                }
                if (token_type == AST_ENUM_TOKEN.AST_SHORT) // short
                {
                    variableType = AST_ENUM_TYPE.AST_SHORT;
                }

                if (token_type == AST_ENUM_TOKEN.AST_ASSIGN) // =
                {
                    if (variableType != AST_ENUM_TYPE.AST_TYPE_UNDEFINED)
                    {
                        varName = FindVarName(i, lexer);

                        List<AST_ENUM_TOKEN> subLexerExpr = FindVarValue(i, lexer); // Find the tokens after assignment operator and before semi-colon.

                        if (subLexerExpr.Count > 0)
                        {
                            // Has underlying expression to parse.
                            for (int j = 0; j < subLexerExpr.Count; j++)
                            {
                                AST_ENUM_TOKEN sub_token = subLexerExpr[j];
                                //subLexerExpr.RemoveAt(j);

                                if (sub_token != AST_ENUM_TOKEN.AST_UNDEFINED)
                                {

                                }
                                if (sub_token == AST_ENUM_TOKEN.AST_PARENTHESIS_BEGIN)
                                {

                                }
                                if (sub_token == AST_ENUM_TOKEN.AST_PARENTHESIS_END)
                                {

                                }
                                if (sub_token == AST_ENUM_TOKEN.AST_WHITESPACE)
                                {

                                }
                                if(isNumeric(sub_token))
                                {

                                }
                                if (sub_token == AST_ENUM_TOKEN.AST_MULTIPLY)
                                {
                                    AST mulAST = new AST();

                                    // PARSE BINARY OPERATOR
                                    mulAST = ParseBinaryOperator(ref j, subLexerExpr, ref parentAST, ref rootAST);
                                }
                                if (sub_token == AST_ENUM_TOKEN.AST_DIV)
                                {                                    
                                    AST divAST;

                                    // PARSE BINARY OPERATOR
                                    divAST = ParseBinaryOperator(ref j, subLexerExpr, ref parentAST, ref rootAST);
                                }
                                if (sub_token == AST_ENUM_TOKEN.AST_PLUS)
                                {
                                    AST addAST;

                                    // PARSE BINARY OPERATOR
                                    addAST = ParseBinaryOperator(ref j, subLexerExpr, ref parentAST, ref rootAST);
                                }
                                if (sub_token == AST_ENUM_TOKEN.AST_SUBTRACT)
                                {
                                    AST subtractAST;

                                    // PARSE BINARY OPERATOR
                                    subtractAST = ParseBinaryOperator(ref j, subLexerExpr, ref parentAST, ref rootAST);
                                }
                            }
                        }

                    }
                }
                if (token_type == AST_ENUM_TOKEN.AST_SEMI_COLON) // ;
                {
                    if (variableType != AST_ENUM_TYPE.AST_TYPE_UNDEFINED && varName != null)
                    {

                    }
                }
            }

            if(rootAST != null)
            {
                results.Add(rootAST);
            }

            return results;
        }

        public static List<AST_ENUM_TOKEN> Lexer(string code)
        {
            List<AST_ENUM_TOKEN> lexer = new List<AST_ENUM_TOKEN>();

            AST_ENUM_TOKEN token_type = AST_ENUM_TOKEN.AST_UNDEFINED;
            AST_ENUM_TYPE data_type = AST_ENUM_TYPE.AST_TYPE_UNDEFINED;
            char current_char;
            char current_char1;
            char current_char2;
            char current_char3;
            char current_char4;
            bool comment = false;
            bool commentAfter = false;

            for (int i = 0; i < code.Length; i++)
            {
                current_char = code[i];

                if (current_char == ' ' || current_char == '\n' || current_char == '\r' || current_char == '\t')
                {
                    token_type = AST_ENUM_TOKEN.AST_WHITESPACE;
                }
                if (current_char == 'A')
                {
                    token_type = AST_ENUM_TOKEN.AST_A;
                }
                if (current_char == 'B')
                {
                    token_type = AST_ENUM_TOKEN.AST_B;
                }
                if (current_char == 'C')
                {
                    token_type = AST_ENUM_TOKEN.AST_C;
                }
                if (current_char == 'D')
                {
                    token_type = AST_ENUM_TOKEN.AST_D;
                }
                if (current_char == 'E')
                {
                    token_type = AST_ENUM_TOKEN.AST_E;
                }
                if (current_char == 'F')
                {
                    token_type = AST_ENUM_TOKEN.AST_F;
                }
                if (current_char == 'G')
                {
                    token_type = AST_ENUM_TOKEN.AST_G;
                }
                if (current_char == 'H')
                {
                    token_type = AST_ENUM_TOKEN.AST_H;
                }
                if (current_char == 'I')
                {
                    token_type = AST_ENUM_TOKEN.AST_I;
                }
                if (current_char == 'J')
                {
                    token_type = AST_ENUM_TOKEN.AST_J;
                }
                if (current_char == 'K')
                {
                    token_type = AST_ENUM_TOKEN.AST_K;
                }
                if (current_char == 'L')
                {
                    token_type = AST_ENUM_TOKEN.AST_L;
                }
                if (current_char == 'M')
                {
                    token_type = AST_ENUM_TOKEN.AST_M;
                }
                if (current_char == 'N')
                {
                    token_type = AST_ENUM_TOKEN.AST_N;
                }
                if (current_char == 'O')
                {
                    token_type = AST_ENUM_TOKEN.AST_O;
                }
                if (current_char == 'P')
                {
                    token_type = AST_ENUM_TOKEN.AST_P;
                }
                if (current_char == 'Q')
                {
                    token_type = AST_ENUM_TOKEN.AST_Q;
                }
                if (current_char == 'R')
                {
                    token_type = AST_ENUM_TOKEN.AST_R;
                }
                if (current_char == 'S')
                {
                    token_type = AST_ENUM_TOKEN.AST_S;
                }
                if (current_char == 'T')
                {
                    token_type = AST_ENUM_TOKEN.AST_T;
                }
                if (current_char == 'U')
                {
                    token_type = AST_ENUM_TOKEN.AST_U;
                }
                if (current_char == 'V')
                {
                    token_type = AST_ENUM_TOKEN.AST_V;
                }
                if (current_char == 'W')
                {
                    token_type = AST_ENUM_TOKEN.AST_W;
                }
                if (current_char == 'X')
                {
                    token_type = AST_ENUM_TOKEN.AST_X;
                }
                if (current_char == 'Y')
                {
                    token_type = AST_ENUM_TOKEN.AST_Y;
                }
                if (current_char == 'Z')
                {
                    token_type = AST_ENUM_TOKEN.AST_Z;
                }

                if (current_char == 'a')
                {
                    token_type = AST_ENUM_TOKEN.AST_a;
                }
                if (current_char == 'b')
                {
                    token_type = AST_ENUM_TOKEN.AST_b;
                }
                if (current_char == 'c')
                {
                    token_type = AST_ENUM_TOKEN.AST_c;
                }
                if (current_char == 'd')
                {
                    token_type = AST_ENUM_TOKEN.AST_d;
                }
                if (current_char == 'e')
                {
                    token_type = AST_ENUM_TOKEN.AST_e;
                }
                if (current_char == 'f')
                {
                    token_type = AST_ENUM_TOKEN.AST_f;
                }
                if (current_char == 'g')
                {
                    token_type = AST_ENUM_TOKEN.AST_g;
                }
                if (current_char == 'h')
                {
                    token_type = AST_ENUM_TOKEN.AST_h;
                }
                if (current_char == 'i')
                {
                    token_type = AST_ENUM_TOKEN.AST_i;
                }
                if (current_char == 'j')
                {
                    token_type = AST_ENUM_TOKEN.AST_j;
                }
                if (current_char == 'k')
                {
                    token_type = AST_ENUM_TOKEN.AST_k;
                }
                if (current_char == 'k')
                {
                    token_type = AST_ENUM_TOKEN.AST_l;
                }
                if (current_char == 'm')
                {
                    token_type = AST_ENUM_TOKEN.AST_m;
                }
                if (current_char == 'n')
                {
                    token_type = AST_ENUM_TOKEN.AST_n;
                }
                if (current_char == 'o')
                {
                    token_type = AST_ENUM_TOKEN.AST_o;
                }
                if (current_char == 'p')
                {
                    token_type = AST_ENUM_TOKEN.AST_p;
                }
                if (current_char == 'q')
                {
                    token_type = AST_ENUM_TOKEN.AST_q;
                }
                if (current_char == 'r')
                {
                    token_type = AST_ENUM_TOKEN.AST_r;
                }
                if (current_char == 's')
                {
                    token_type = AST_ENUM_TOKEN.AST_s;
                }
                if (current_char == 't')
                {
                    token_type = AST_ENUM_TOKEN.AST_t;
                }
                if (current_char == 'u')
                {
                    token_type = AST_ENUM_TOKEN.AST_u;
                }
                if (current_char == 'v')
                {
                    token_type = AST_ENUM_TOKEN.AST_v;
                }
                if (current_char == 'w')
                {
                    token_type = AST_ENUM_TOKEN.AST_w;
                }
                if (current_char == 'y')
                {
                    token_type = AST_ENUM_TOKEN.AST_x;
                }
                if (current_char == 'y')
                {
                    token_type = AST_ENUM_TOKEN.AST_y;
                }
                if (current_char == 'z')
                {
                    token_type = AST_ENUM_TOKEN.AST_z;
                }
                if (current_char == '0')
                {
                    token_type = AST_ENUM_TOKEN.AST_0;
                }
                if (current_char == '1')
                {
                    token_type = AST_ENUM_TOKEN.AST_1;
                }
                if (current_char == '2')
                {
                    token_type = AST_ENUM_TOKEN.AST_2;
                }
                if (current_char == '3')
                {
                    token_type = AST_ENUM_TOKEN.AST_3;
                }
                if (current_char == '4')
                {
                    token_type = AST_ENUM_TOKEN.AST_4;
                }
                if (current_char == '5')
                {
                    token_type = AST_ENUM_TOKEN.AST_5;
                }
                if (current_char == '6')
                {
                    token_type = AST_ENUM_TOKEN.AST_6;
                }
                if (current_char == '7')
                {
                    token_type = AST_ENUM_TOKEN.AST_7;
                }
                if (current_char == '8')
                {
                    token_type = AST_ENUM_TOKEN.AST_8;
                }
                if (current_char == '9')
                {
                    token_type = AST_ENUM_TOKEN.AST_9;
                }
                if (current_char == ';')
                {
                    token_type = AST_ENUM_TOKEN.AST_SEMI_COLON;
                }

                if (current_char == '(')
                {
                    token_type = AST_ENUM_TOKEN.AST_PARENTHESIS_BEGIN;
                }

                if (current_char == ')')
                {
                    token_type = AST_ENUM_TOKEN.AST_PARENTHESIS_END;
                }

                if (current_char == '*')
                {
                    token_type = AST_ENUM_TOKEN.AST_MULTIPLY;
                }

                if (current_char == '/')
                {
                    token_type = AST_ENUM_TOKEN.AST_DIV;
                }

                if (current_char == '+')
                {
                    token_type = AST_ENUM_TOKEN.AST_PLUS;
                }

                if (current_char == '-')
                {
                    token_type = AST_ENUM_TOKEN.AST_SUBTRACT;
                }

                if (current_char == '^')
                {
                    token_type = AST_ENUM_TOKEN.AST_XOR;
                }

                if (current_char == '=')
                {
                    token_type = AST_ENUM_TOKEN.AST_ASSIGN;
                }

                if (current_char == '~')
                {
                    token_type = AST_ENUM_TOKEN.AST_TILDE;
                }

                if (current_char == '-')
                {
                    token_type = AST_ENUM_TOKEN.AST_NEGATION;
                }

                if (i + 1 < code.Length)
                {
                    current_char1 = code[i + 1];

                    if (current_char.ToString().ToLower() == "i" && current_char1.ToString().ToLower() == "f")
                    {
                        token_type = AST_ENUM_TOKEN.AST_IF;
                        i++;
                    }

                    if (current_char == '-' && current_char1 == '-')
                    {
                        token_type = AST_ENUM_TOKEN.AST_DECREMENT;
                        i++;
                    }
                    if (current_char == '+' && current_char1 == '+')
                    {
                        token_type = AST_ENUM_TOKEN.AST_INCREMENT;
                        i++;
                    }
                    if (current_char == '=' && current_char1 == '=')
                    {
                        token_type = AST_ENUM_TOKEN.AST_EQUALITY;
                        i++;
                    }
                    if (current_char == '!' && current_char1 == '=')
                    {
                        token_type = AST_ENUM_TOKEN.AST_NOT_EQUAL_TO;
                        i++;
                    }
                    if (current_char == '/' && current_char1 == '/')
                    {
                        token_type = AST_ENUM_TOKEN.AST_COMMENT_BEGIN;
                        i++;
                        comment = true;
                    }
                    if (current_char == '\n' || current_char == '\r' || current_char1 == '\r' || current_char1 == '\n')
                    {
                        commentAfter = true;
                        comment = false;
                    }
                    if (current_char == '/' && current_char1 == '*')
                    {
                        token_type = AST_ENUM_TOKEN.AST_MULTILINE_COMMENT_BEGIN;
                        i++;
                    }
                    if (current_char == '*' && current_char1 == '/')
                    {
                        token_type = AST_ENUM_TOKEN.AST_MULTILINE_COMMENT_END;
                        i++;
                    }
                    if (current_char == 'd' && current_char1 == 'o')
                    {
                        token_type = AST_ENUM_TOKEN.AST_DO;
                        i++;
                    }

                    if (i + 2 < code.Length)
                    {
                        current_char2 = code[i + 2];

                        if (current_char == 'f' && current_char1 == 'o' && current_char2 == 'r')
                        {
                            token_type = AST_ENUM_TOKEN.AST_FOR;
                            i += 2;
                        }

                        if (current_char == 'i' && current_char1 == 'n' && current_char2 == 't')
                        {
                            data_type = AST_ENUM_TYPE.AST_INT;
                            token_type = AST_ENUM_TOKEN.AST_INT;
                            i += 2;
                        }

                        if (i + 3 < code.Length)
                        {
                            current_char3 = code[i + 3];

                            if (current_char == 'b' && current_char1 == 'o' && current_char2 == 'o' && current_char3 == 'l')
                            {
                                data_type = AST_ENUM_TYPE.AST_BOOLEAN;
                                token_type = AST_ENUM_TOKEN.AST_BOOLEAN;
                                i += 3;
                            }

                            if (i + 4 < code.Length)
                            {
                                current_char4 = code[i + 4];

                                if (current_char == 'w' && current_char1 == 'h' && current_char2 == 'i' && current_char3 == 'l' && current_char4 == 'e')
                                {
                                    token_type = AST_ENUM_TOKEN.AST_WHILE;
                                    i += 4;
                                }

                                if (current_char == 's' && current_char1 == 'h' && current_char2 == 'o' && current_char3 == 'r' && current_char4 == 't')
                                {
                                    data_type = AST_ENUM_TYPE.AST_SHORT;
                                    token_type = AST_ENUM_TOKEN.AST_SHORT;
                                    i += 4;
                                }
                            }
                        }
                    }
                }

                if (token_type != AST_ENUM_TOKEN.AST_UNDEFINED)
                {
                    lexer.Add(token_type);
                }

                if (commentAfter && comment == false)
                {
                    lexer.Add(AST_ENUM_TOKEN.AST_COMMENT_END);
                    commentAfter = false;
                }
            }

            return lexer;
        }
    }
}
