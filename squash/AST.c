#include "AST.h"

char* astnodetype_tostring(enum ASTNodeType type)
{
    char* s = NULL;
    switch (type) {
    case AST_BIN_OP:
        s = (char*)malloc(sizeof(char) * 7);
        s[0] = 'B';
        s[1] = 'I';
        s[2] = 'N';
        s[3] = '_';
        s[4] = 'O';
        s[5] = 'P';
        s[6] = '\0';
        break;
    case AST_FunctionArg:
        s = (char*)malloc(sizeof(char) * 12);
        s[0] = 'F';
        s[1] = 'u';
        s[2] = 'n';
        s[3] = 'c';
        s[4] = 't';
        s[5] = 'i';
        s[6] = 'o';
        s[7] = 'n';
        s[8] = 'A';
        s[9] = 'r';
        s[10] = 'g';
        s[11] = '\0';
        break;
    case AST_FunctionCall:
        s = (char*)malloc(sizeof(char) * 13);
        s[0] = 'F';
        s[1] = 'u';
        s[2] = 'n';
        s[3] = 'c';
        s[4] = 't';
        s[5] = 'i';
        s[6] = 'o';
        s[7] = 'n';
        s[8] = 'C';
        s[9] = 'a';
        s[10] = 'l';
        s[11] = 'l';
        s[12] = '\0';
        break;
    case AST_FunctionDefinition:
        s = (char*)malloc(sizeof(char) * 19);
        s[0] = 'F';
        s[1] = 'u';
        s[2] = 'n';
        s[3] = 'c';
        s[4] = 't';
        s[5] = 'i';
        s[6] = 'o';
        s[7] = 'n';
        s[8] = 'D';
        s[9] = 'e';
        s[10] = 'f';
        s[11] = 'i';
        s[12] = 'n';
        s[13] = 'i';
        s[14] = 't';
        s[15] = 'i';
        s[16] = 'o';
        s[17] = 'n';
        s[18] = '\0';
        break;
    case AST_FunctionReturn:
        s = (char*)malloc(sizeof(char) * 15);
        s[0] = 'F';
        s[1] = 'u';
        s[2] = 'n';
        s[3] = 'c';
        s[4] = 't';
        s[5] = 'i';
        s[6] = 'o';
        s[7] = 'n';
        s[8] = 'R';
        s[9] = 'e';
        s[10] = 't';
        s[11] = 'u';
        s[12] = 'r';
        s[13] = 'n';
        s[14] = '\0';
        break;
    case AST_Number:
        s = (char*)malloc(sizeof(char) * 7);
        s[0] = 'N';
        s[1] = 'u';
        s[2] = 'm';
        s[3] = 'b';
        s[4] = 'e';
        s[5] = 'r';
        s[6] = '\0';
        break;
    case AST_UNARY_OP:
        s = (char*)malloc(sizeof(char) * 9);
        s[0] = 'U';
        s[1] = 'N';
        s[2] = 'A';
        s[3] = 'R';
        s[4] = 'Y';
        s[5] = '_';
        s[6] = 'O';
        s[7] = 'P';
        s[8] = '\0';
        break;
    case AST_Variable:
        s = (char*)malloc(sizeof(char) * 9);
        s[0] = 'V';
        s[1] = 'a';
        s[2] = 'r';
        s[3] = 'i';
        s[4] = 'a';
        s[5] = 'b';
        s[6] = 'l';
        s[7] = 'e';
        s[8] = '\0';
        break;
    case AST_VariableAssignment:
        s = (char*)malloc(sizeof(char) * 19);
        s[0] = 'V';
        s[1] = 'a';
        s[2] = 'r';
        s[3] = 'i';
        s[4] = 'a';
        s[5] = 'b';
        s[6] = 'l';
        s[7] = 'e';
        s[8] = 'A';
        s[9] = 's';
        s[10] = 's';
        s[11] = 'i';
        s[12] = 'g';
        s[13] = 'n';
        s[14] = 'm';
        s[15] = 'e';
        s[16] = 'n';
        s[17] = 't';
        s[18] = '\0';
        break;
    case AST_VariableDeclaration:
        s = (char*)malloc(sizeof(char) * 20);
        s[0] = 'V';
        s[1] = 'a';
        s[2] = 'r';
        s[3] = 'i';
        s[4] = 'a';
        s[5] = 'b';
        s[6] = 'l';
        s[7] = 'e';
        s[8] = 'D';
        s[9] = 'e';
        s[10] = 'c';
        s[11] = 'l';
        s[12] = 'a';
        s[13] = 'r';
        s[14] = 'a';
        s[15] = 't';
        s[16] = 'i';
        s[17] = 'o';
        s[18] = 'n';
        s[19] = '\0';
        break;
    }
    return s;
}
char* bool_tostring(bool input)
{
    char* s = NULL;
    if (input)
    {
        s = (char*)malloc(sizeof(char) * 5);
        s[0] = 'f';
        s[1] = 'a';
        s[2] = 'l';
        s[3] = 's';
        s[4] = 'e';
    }
    else
    {
        s = (char*)malloc(sizeof(char) * 4);
        s[0] = 't';
        s[1] = 'r';
        s[2] = 'u';
        s[3] = 'e';
    }
    return s;
}
char* ast_tostring(struct ASTNode* node)
{
    StringBuilder* sb = sb_create();

    char* astType = astnodetype_tostring(node->Type);

    sb_append(sb, "Type: ");
    sb_append(sb, astType);

    sb_append(sb, ", Value: '");
    sb_append(sb, node->Value);

    char* varState = NULL;
    char* functBodyStatus;

    if (node->FunctionArguments != NULL)
    {
        functBodyStatus = " Has them.";
    }
    else
    {
        functBodyStatus = "Doesn't have them.";
    }

    sb_append(sb, "' Function Args: '");
    sb_append(sb, functBodyStatus);

    sb_append(sb, "', IsVariable: ");
    sb_append(sb, (node->IsVariable ? "true" : "false"));

    sb_append(sb, " IsFunctionCall: ");
    sb_append(sb, (node->IsFunctionCall ? "true" : "false"));

    sb_append(sb, " IsFunctionDefinition: ");
    sb_append(sb, (node->IsFunctionDefinition ? "true" : "false"));

    char* statusMsg = sb_concat(sb);

    return statusMsg;
}

struct ASTNode* ast_node_new()
{
    struct ASTNode* n;

    n = (struct ASTNode*)malloc(sizeof(struct ASTNode));

    // Default initilisiers.
    n->FunctionBody = NULL;
    n->FunctionArguments = NULL;
    n->FunctionName = NULL;
    n->FunctSymbol = NULL;
    n->Left = NULL;
    n->Right = NULL;
    n->VarSymbol = NULL;
    n->ArgumentType = NULL;
    n->FunctionReturnType = AST_VT_UNDEFINED;
    n->IsFunctionCall = false;
    n->IsFunctionDefinition = false;
    n->IsVariable = false;
    n->Precedence = 0;
    n->Type = AST_UNDEFINED;
    n->Value = NULL;
    n->VariableType = AST_VT_UNDEFINED;
    n->ValueType = AST_VALUE_UNDEFINED;
    return n;
}

void ast_node_init_bt(struct ASTNode** node, enum ASTNodeType type, char* value, enum ASTNodeValueType value_type, struct ASTNode* left, struct ASTNode* right)
{
    //struct ASTNode* n = ast_node_new();
    struct ASTNode* n = *node;

    n->Type = type;
    n->Value = value;
    n->Left = left;
    n->Right = right;
    n->FunctSymbol = NULL;
    n->FunctionArguments = NULL;
    n->VarSymbol = NULL;
    n->IsFunctionCall = false;
    n->IsVariable = false;
    n->IsFunctionDefinition = false;
    n->ValueType = value_type;

    *node = n;
}

void ast_node_init_bt2(struct ASTNode** node, enum ASTNodeType type, char* argumentType, char* value, enum ASTNodeValueType value_type, struct ASTNode* left, struct ASTNode* right)
{
    //struct ASTNode* n = ast_node_new();
    struct ASTNode* n = *node;

    n->Type = type;
    n->ArgumentType = argumentType;
    n->Value = value;
    n->Left = left;
    n->Right = right;
    n->FunctSymbol = NULL;
    n->FunctionArguments = NULL;
    n->VarSymbol = NULL;
    n->IsFunctionCall = false;
    n->IsVariable = false;
    n->IsFunctionDefinition = false;
    n->ValueType = value_type;

    *node = n;
}

void ast_node_init_funct(struct ASTNode** node, enum ASTNodeType type, char* value, struct FunctionSymbol* functionSymbol, list_t* arguments) // List<ASTNode> arguments
{
    //struct ASTNode* n = ast_node_new();
    struct ASTNode* n = *node;

    n->Type = type;
    n->Value = value;
    n->Left = NULL;
    n->Right = NULL;
    n->FunctSymbol = functionSymbol;
    n->FunctionArguments = arguments;
    n->VarSymbol = NULL;
    n->IsFunctionCall = true;
    n->IsVariable = false;
    n->IsFunctionDefinition = false;

    *node = n;
}

void ast_node_init_var(struct ASTNode** node, enum ASTNodeType type, char* value, enum ASTNodeValueType value_type, struct VariableSymbol* variableSymbol)
{
    //struct ASTNode* n = ast_node_new();
    struct ASTNode* n = *node;

    n->Type = type;
    n->Value = value;
    n->Left = NULL;
    n->Right = NULL;
    n->FunctSymbol = NULL;
    n->FunctionArguments = NULL;
    n->VarSymbol = variableSymbol;
    n->IsVariable = true;
    n->IsFunctionDefinition = false;
    n->ValueType = value_type;

    *node = n;
}