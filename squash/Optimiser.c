#include "Optimiser.h"

void OptimiseNode(astnode_t** node, bool optimiseConstVariables)
{
    if (node == NULL)
    {
        return;
    }

    // Collapse the child nodes into one immediate number value node type
    // then for all other node types try and run the optimiser on each node.

    // TODO: Run optimisers on const correct variables as well as constants
    // in compiler time to optimise all code paths.

    OptimiseFunctionBody(node, optimiseConstVariables);

    OptimiseBinaryOperator(node, optimiseConstVariables);
}

void OptimiseFunctionBody(astnode_t** nde, bool optimiseConstVariables)
{
    astnode_t* node = *nde;
    list_t* n;
    if (node != NULL && node->FunctionBody != NULL)
    {
        n = node->FunctionBody;
        while (n != NULL)
        {
            astnode_t* statement = (astnode_t*)n->data;
            OptimiseNode(&statement, optimiseConstVariables);

            n = n->next;
        }
    }
}

void OptimiseBinaryOperator(astnode_t** nde, bool optimiseConstVariables)
{
    astnode_t* node = *nde;

    if (node == NULL)
    {
        return;
    }

    astnode_t* left = node->Left;
    astnode_t* right = node->Right;

    char* result = NULL;

    if (node->Type == AST_BIN_OP || left != NULL || right != NULL)
    {
        char* operandLeft = NULL;
        char* operandRight = NULL;
        enum ASTNodeValueType leftType = AST_VALUE_UNDEFINED;
        enum ASTNodeValueType rightType = AST_VALUE_UNDEFINED;

        if (left != NULL)
        {
            leftType = left->ValueType;

            OptimiseNode(&left, optimiseConstVariables);

            if (left->Type == AST_Number)
            {
                Optimiser_ParseNumber(left);
            }
            if (left->Type == AST_Variable && optimiseConstVariables)
            {
                char* identifierName = left->Value;
                if (left->VarSymbol != NULL)
                {
                    VariableSymbol_t* variableSymbol = left->VarSymbol;

                    if (variableSymbol->Value != NULL)// && (int)variableSymbol.Value != 0)
                    {
                        left->Value = (char*)variableSymbol->Value;
                    }
                }
            }
        }

        if (right != NULL)
        {
            rightType = right->ValueType;

            OptimiseNode(&right, optimiseConstVariables);

            if (right->Type == AST_Number)
            {
                Optimiser_ParseNumber(right);
            }
            if (right->Type == AST_Variable && optimiseConstVariables)
            {
                char* identifierName = right->Value;
                if (right->VarSymbol != NULL)
                {
                    VariableSymbol_t* variableSymbol = right->VarSymbol;

                    if (variableSymbol->Value != NULL)// (int)variableSymbol.Value != 0)
                    {
                        right->Value = (char*)variableSymbol->Value;
                    }
                }
            }
        }

        if (left != NULL && right != NULL && left->Type == AST_Number && right->Type == AST_Number)
        {
            result = Optimiser_ApplyOperator(node, left, leftType, right, rightType);

            CollapseNode(&node, result);
        }
    }
}

void CollapseNode(astnode_t** nde, char* result)
{
    astnode_t* node = *nde;

    // COLLAPSE.

    // Change node value to new value from collapsed children.
    node->Value = result;
    node->Left = NULL;
    node->Right = NULL;
    node->Type = AST_Number;

    (*nde) = node;
}

char* Optimiser_ApplyOperator(astnode_t* node, astnode_t* left, enum ASTNodeValueType operandLeftType, astnode_t* right, enum ASTNodeValueType operandRightType)
{
    char* result = NULL;
    if (operandLeftType == AST_VALUE_INT)
    {
        int resultLeftInt = left->ValueInt;
        if (operandRightType == AST_VALUE_INT)
        {
            int resultRightInt = right->ValueInt;

            int resultInt = ApplyOperatorII(node, resultLeftInt, resultRightInt);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%d", resultInt);
            result = str;
        }
        else if (operandRightType == AST_VALUE_DOUBLE)
        {
            double resultRightDouble = right->ValueDouble;

            double resultDouble = ApplyOperatorID(node, resultLeftInt, resultRightDouble);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%f", resultDouble);
            result = str;
        }
        else if (operandRightType == AST_VALUE_FLOAT)
        {
            float resultRightFloat = right->ValueFloat;

            double resultFloat = ApplyOperatorIF(node, resultLeftInt, resultRightFloat);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%f", resultFloat);
            result = str;
        }
    }
    else if (operandLeftType == AST_VALUE_DOUBLE)
    {
        double resultLeftDouble = left->ValueDouble;
        if (operandRightType == AST_VALUE_INT)
        {
            int resultRightInt = right->ValueInt;

            double resultDouble = ApplyOperatorDI(node, resultLeftDouble, resultRightInt);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%f", resultDouble);
            result = str;
        }
        else if (operandRightType == AST_VALUE_DOUBLE)
        {
            double resultRightDouble = right->ValueDouble;

            double resultDouble = ApplyOperatorDD(node, resultLeftDouble, resultRightDouble);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%f", resultDouble);
            result = str;
        }
        else if (operandRightType == AST_VALUE_FLOAT)
        {
            float resultRightFloat = right->ValueFloat;

            double resultDouble = ApplyOperatorDF(node, resultLeftDouble, resultRightFloat);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%f", resultDouble);
            result = str;
        }
    }
    else if (operandLeftType == AST_VALUE_FLOAT)
    {
        float resultLeftFloat = left->ValueFloat;
        if (operandRightType == AST_VALUE_INT)
        {
            int resultRightInt = right->ValueInt;

            float resultFloat = ApplyOperatorFI(node, resultLeftFloat, resultRightInt);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%f", resultFloat);
            result = str;
        }
        else if (operandRightType == AST_VALUE_DOUBLE)
        {
            double resultRightDouble = right->ValueDouble;

            float resultFloat = ApplyOperatorFD(node, resultLeftFloat, resultRightDouble);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%f", resultFloat);
            result = str;
        }
        else if (operandRightType == AST_VALUE_FLOAT)
        {
            float resultRightFloat = right->ValueFloat;

            float resultFloat = ApplyOperatorFF(node, resultLeftFloat, resultRightFloat);

            char* str = (char*)malloc(sizeof(char) * 200);
            sprintf(str, "%f", resultFloat);
            result = str;
        }
    }
    return result;
}

void Optimiser_ParseNumber(astnode_t* node)
{
    if (node->ValueType == AST_VALUE_INT)
    {
        int num = atoi(node->Value);
        node->ValueInt = num;
    }
    else if (node->ValueType == AST_VALUE_FLOAT)
    {
        float num = (float)atof(node->Value);
        node->ValueFloat = num;
    }
    else if (node->ValueType == AST_VALUE_DOUBLE)
    {
        double num = atof(node->Value);
        node->ValueDouble = num;
    }
    else if (node->ValueType == AST_VALUE_STRING)
    {
        // Implicitly a string.
    }
    else if (node->ValueType == AST_VALUE_UNDEFINED)
    {
        LogError("Optimiser_ParseNumber(): AST_VALUE_UNDEFINED");
    }
}

int ApplyOperatorII(astnode_t* node, int operandLeft, int operandRight)
{
    char* c = node->Value;
    int result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else 
    {
        LogError("Optimiser.ApplyOperatorII(): Invalid operator");
    }
    return result;
}

double ApplyOperatorID(astnode_t* node, int operandLeft, double operandRight)
{
    char* c = node->Value;
    double result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else
    {
        LogError("Optimiser.ApplyOperatorID(): Invalid operator");
    }
    return result;
}

float ApplyOperatorIF(astnode_t* node, int operandLeft, float operandRight)
{
    char* c = node->Value;
    float result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else
    {
        LogError("Optimiser.ApplyOperatorIF(): Invalid operator");
    }
    return result;
}

double ApplyOperatorDI(astnode_t* node, double operandLeft, int operandRight)
{
    char* c = node->Value;
    double result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else
    {
        LogError("Optimiser.ApplyOperatorDI(): Invalid operator");
    }
    return result;
}

double ApplyOperatorDD(astnode_t* node, double operandLeft, double operandRight)
{
    char* c = node->Value;
    double result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else
    {
        LogError("Optimiser.ApplyOperatorDD(): Invalid operator");
    }
    return result;
}

double ApplyOperatorDF(astnode_t* node, double operandLeft, float operandRight)
{
    char* c = node->Value;
    double result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else
    {
        LogError("Optimiser.ApplyOperatorDF(): Invalid operator");
    }
    return result;
}

float ApplyOperatorFI(astnode_t* node, float operandLeft, int operandRight)
{
    char* c = node->Value;
    float result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else
    {
        LogError("Optimiser.ApplyOperatorFI(): Invalid operator");
    }
    return result;
}

float ApplyOperatorFD(astnode_t* node, float operandLeft, double operandRight)
{
    char* c = node->Value;
    float result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else
    {
        LogError("Optimiser.ApplyOperatorFD(): Invalid operator");
    }
    return result;
}

float ApplyOperatorFF(astnode_t* node, float operandLeft, float operandRight)
{
    char* c = node->Value;
    float result = 0;
    if (strcmp(c, "+") == 0)
    {
        result = operandLeft + operandRight;
    }
    else if (strcmp(c, "-") == 0)
    {
        result = operandLeft - operandRight;
    }
    else if (strcmp(c, "*") == 0)
    {
        result = operandLeft * operandRight;
    }
    else if (strcmp(c, "/") == 0)
    {
        result = operandLeft / operandRight;
    }
    else
    {
        LogError("Optimiser.ApplyOperatorFF(): Invalid operator");
    }
    return result;
}