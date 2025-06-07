#include "AST.h"
#include "Token.h"


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

    OptimiseFunctionBody(node);

    OptimiseBinaryOperator(node, optimiseConstVariables);
}

void OptimiseFunctionBody(astnode_t** nde, bool optimiseConstVariables)
{
    astnode_t* node = *nde;
    list_t* n;
    if (node->FunctionBody != NULL)
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

    astnode_t* left = node->Left;
    astnode_t* right = node->Right;

    char* result = NULL;

    if (node->Type == AST_BIN_OP || left != NULL || right != NULL)
    {
        object ? operandLeft = null;
        object ? operandRight = null;

        if (left != null)
        {
            OptimiseNode(&left, optimiseConstVariables);

            if (left->Type == AST_Number)
            {
                operandLeft = ParseNumber(left);
            }
            if (left->Type == AST_Variable && optimiseConstVariables)
            {
                string identifierName = left.Value;
                if (left.VarSymbol != null)
                {
                    VariableSymbol variableSymbol = left.VarSymbol;

                    if (variableSymbol.Value != null)// && (int)variableSymbol.Value != 0)
                    {
                        operandLeft = variableSymbol.Value;
                    }
                }
            }
        }

        if (right != NULL)
        {
            OptimiseNode(&right, optimiseConstVariables);

            if (right->Type == AST_Number)
            {
                operandRight = ParseNumber(right);
            }
            if (right->Type == AST_Variable && optimiseConstVariables)
            {
                string identifierName = right.Value;
                if (right.VarSymbol != null)
                {
                    VariableSymbol variableSymbol = right.VarSymbol;

                    if (variableSymbol.Value != null)// (int)variableSymbol.Value != 0)
                    {
                        operandRight = variableSymbol.Value;
                    }
                }
            }
        }

        if (operandLeft != NULL && operandRight != NULL)
        {
            result = ApplyOperator(node, operandLeft, operandRight);

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

string ApplyOperator(astnode_t* node, object operandLeft, object operandRight)
{
    string result = string.Empty;
    if (operandLeft is int)
    {
        int resultLeftInt = (int)operandLeft;
        if (operandRight is int)
        {
            int resultRightInt = (int)operandRight;

            result = ApplyOperator(node, resultLeftInt, resultRightInt).ToString();
        }
        else if (operandRight is double)
        {
            double resultRightDouble = (double)operandRight;

            result = ApplyOperator(node, resultLeftInt, resultRightDouble).ToString();
        }
        else if (operandRight is float)
        {
            float resultRightFloat = (float)operandRight;

            result = ApplyOperator(node, resultLeftInt, resultRightFloat).ToString();
        }
    }
    else if (operandLeft is double)
    {
        double resultLeftDouble = (double)operandLeft;
        if (operandRight is int)
        {
            int resultRightInt = (int)operandRight;

            result = ApplyOperator(node, resultLeftDouble, resultRightInt).ToString();
        }
        else if (operandRight is double)
        {
            double resultRightDouble = (double)operandRight;

            result = ApplyOperator(node, resultLeftDouble, resultRightDouble).ToString();
        }
        else if (operandRight is float)
        {
            float resultRightFloat = (float)operandRight;

            result = ApplyOperator(node, resultLeftDouble, resultRightFloat).ToString();
        }
    }
    else if (operandLeft is float)
    {
        double resultLeftFloat = (float)operandLeft;
        if (operandRight is int)
        {
            int resultRightInt = (int)operandRight;

            result = ApplyOperator(node, resultLeftFloat, resultRightInt).ToString();
        }
        else if (operandRight is double)
        {
            double resultRightDouble = (double)operandRight;

            result = ApplyOperator(node, resultLeftFloat, resultRightDouble).ToString();
        }
        else if (operandRight is float)
        {
            float resultRightFloat = (float)operandRight;

            result = ApplyOperator(node, resultLeftFloat, resultRightFloat).ToString();
        }
    }
    return result;
}

object ? Optimiser_ParseNumber(ASTNode node)
{
    int resultInt;
    double resultDouble;
    float resultFloat;

    if (int.TryParse(node.Value, out resultInt))
    {
        return resultInt;
    }
    else
    {
        if (double.TryParse(node.Value, out resultDouble))
        {
            return resultDouble;
        }
        else
        {
            if (float.TryParse(node.Value, out resultFloat))
            {
                return resultFloat;
            }
        }
    }
    return null;
}

private static int ApplyOperator(ASTNode node, int operandLeft, int operandRight)
{
    int result = 0;
    switch (node.Value)
    {
    case "+":
        result = operandLeft + operandRight;
        break;
    case "-":
        result = operandLeft - operandRight;
        break;
    case "*":
        result = operandLeft * operandRight;
        break;
    case "/":
        result = operandLeft / operandRight;
        break;
    default:
        Logger.Log.LogError("Optimiser.OptimiseNode(): Invalid operator");
        break;
    }
    return result;
}

private static double ApplyOperator(ASTNode node, double operandLeft, int operandRight)
{
    double result = 0;
    switch (node.Value)
    {
    case "+":
        result = operandLeft + operandRight;
        break;
    case "-":
        result = operandLeft - operandRight;
        break;
    case "*":
        result = operandLeft * operandRight;
        break;
    case "/":
        result = operandLeft / operandRight;
        break;
    default:
        Logger.Log.LogError("Optimiser.OptimiseNode(): Invalid operator");
        break;
    }
    return result;
}

private static double ApplyOperator(ASTNode node, double operandLeft, double operandRight)
{
    double result = 0;
    switch (node.Value)
    {
    case "+":
        result = operandLeft + operandRight;
        break;
    case "-":
        result = operandLeft - operandRight;
        break;
    case "*":
        result = operandLeft * operandRight;
        break;
    case "/":
        result = operandLeft / operandRight;
        break;
    default:
        Logger.Log.LogError("Optimiser.OptimiseNode(): Invalid operator");
        break;
    }
    return result;
}

private static double ApplyOperator(ASTNode node, double operandLeft, float operandRight)
{
    double result = 0;
    switch (node.Value)
    {
    case "+":
        result = operandLeft + operandRight;
        break;
    case "-":
        result = operandLeft - operandRight;
        break;
    case "*":
        result = operandLeft * operandRight;
        break;
    case "/":
        result = operandLeft / operandRight;
        break;
    default:
        Logger.Log.LogError("Optimiser.OptimiseNode(): Invalid operator");
        break;
    }
    return result;
}

private static float ApplyOperator(ASTNode node, float operandLeft, float operandRight)
{
    float result = 0;
    switch (node.Value)
    {
    case "+":
        result = operandLeft + operandRight;
        break;
    case "-":
        result = operandLeft - operandRight;
        break;
    case "*":
        result = operandLeft * operandRight;
        break;
    case "/":
        result = operandLeft / operandRight;
        break;
    default:
        Logger.Log.LogError("Optimiser.OptimiseNode(): Invalid operator");
        break;
    }
    return result;
}