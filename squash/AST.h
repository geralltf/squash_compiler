#ifndef AST_SQUASH_H
#define AST_SQUASH_H

#include <stdlib.h>
#include <stdbool.h>

enum OperatorType
{
    OT_MULTIPLY,
    OT_DIVIDE,
    OT_ADD,
    OT_SUBTRACT,
    OT_UNDEFINED
};

enum OperatorPrecedence
{
    OP_AST_MULTIPLY = 50,
    OP_AST_DIVIDE = 50,
    OP_AST_REMAINDER = 50,
    OP_AST_ADD = 45,
    OP_AST_SUBTRACT = 45,
    OP_AST_UNDEFINED = 0
};

enum AST_ENUM_TOKEN
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

enum AST_ENUM_TYPE
{
    AST_TYPE_STRING = 0x00,
    AST_TYPE_BYTE = 0x01,
    AST_TYPE_INT = 0x02,
    AST_TYPE_UINT = 0x03,
    AST_TYPE_SHORT = 0x04,
    AST_TYPE_USHORT = 0x05,
    AST_TYPE_ULONG = 0x06,
    AST_TYPE_LONG = 0x07,
    AST_TYPE_FLOAT = 0x08,
    AST_TYPE_DOUBLE = 0x09,
    AST_TYPE_VAR = 0x0A,
    AST_TYPE_MAT3x3 = 0x0B,
    AST_TYPE_MAT4x4 = 0x0C,
    AST_TYPE_MAT8x8 = 0x0D,
    AST_TYPE_MAT10x10 = 0x0E,
    AST_TYPE_MAT12x12 = 0x0F,
    AST_TYPE_MAT13x13 = 0x10,
    AST_TYPE_MAT16x16 = 0x11,
    AST_TYPE_MAT20x20 = 0x12,
    AST_TYPE_MATNxN = 0x13,
    AST_TYPE_MATNxM = 0x14,
    AST_TYPE_BOOLEAN = 0x15,
    AST_TYPE_MULTI_STRING = 0xFF,
    AST_TYPE_UNDEFINED = 0xFFF
};

typedef struct _AST_s {
    char* varName;
    char* operandLeft;
    char* operandRight;
    enum OperatorType operatorType;
    enum OperatorType nextOprType;
    struct _AST_s* leftChild;
    struct _AST_s* rightChild;
    int precedence;
    int ID;
    int prevID;
} AST;

typedef struct token_list {
    enum AST_ENUM_TOKEN token;
    struct token_list* next;
} token_list_t;

typedef struct char_list {
    char* chr;
    struct char_list* next;
} char_list_t;

typedef struct id_list {
    int id;
    struct id_list* next;
} id_list_t;

void idlst_init(id_list_t* item, int id);
void idlst_add(id_list_t* parent, int id);
bool idlst_exists(id_list_t* front, int id);
void ast_init(AST** ast);
char* reverse(char* source);
char* FindVarName(int startingIndex, token_list_t* lexer);
char* FindOperandLeft(int startingIndex, token_list_t* lexer);
enum OperatorType GetNextOperatorType(int index, token_list_t* lexer);
int GetCurrPrecedence(int defaultPrecedence, int index, enum AST_ENUM_TOKEN token, token_list_t* lexer);
AST* ParseBinaryOperator(int* index, token_list_t* lexer, AST* parentAST, AST* rootAST);
token_list_t* FindVarValue(int index, token_list_t* lexer);
bool isAlphaNumeric(enum AST_ENUM_TOKEN token);
bool isNumeric(enum AST_ENUM_TOKEN token);
bool isWhiteSpace(enum AST_ENUM_TOKEN token);
AST* Expr(char* expr);
char* ParseString(enum AST_ENUM_TOKEN token);
char* ParseStringFromToken(token_list_t* lexer, int index);
AST* Parser(token_list_t* lexer);
token_list_t* Lexer(char* code);
char* OperatorTypeToString(enum OperatorType operatorType);

#endif