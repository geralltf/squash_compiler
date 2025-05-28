using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml.Linq;

namespace SquashC.Compiler
{
    /// <summary>
    /// Applies code optimisations to in memory expressions and statements 
    /// by compile-time evaluation of constants in many ASTNode's applying collapsing logic to Number nodes.
    /// </summary>
    public class Optimiser
    {
        public static void OptimiseNode(ref ASTNode node)
        {
            ASTNode left = null;
            ASTNode right = null;

            if (node == null)
            {
                return;
            }

            // Collapse the child nodes into one immediate number value node type
            // then for all other node types try and run the optimiser on each node.

            // TODO: Run optimisers on const correct variables as well as constants
            // in compiler time to optimise all code paths.

            if (node.FunctionBody != null && node.FunctionBody.Count > 0)
            {
                for (int i = 0; i < node.FunctionBody.Count; i++)
                {
                    ASTNode statement = node.FunctionBody[i];

                    if (statement != null)
                    {
                        OptimiseNode(ref statement);
                    }
                }
            }

            string result = string.Empty;

            left = node.Left;
            right = node.Right;

            if (node.Type == ASTNodeType.BIN_OP || left != null || right != null)
            {
                object? operandLeft = null;
                object? operandRight = null;

                if (left != null)
                {
                    OptimiseNode(ref left);

                    if (left.Type == ASTNodeType.Number)
                    {
                        operandLeft = ParseNumber(left);
                    }
                }

                if (right != null)
                {
                    OptimiseNode(ref right);

                    if (right.Type == ASTNodeType.Number)
                    {
                        operandRight = ParseNumber(right);
                    }
                }

                if(operandLeft != null && operandRight != null)
                {
                    result = ApplyOperator(node, operandLeft, operandRight);

                    CollapseNode(ref node, result);
                }
            }
        }

        private static void CollapseNode(ref ASTNode node, string result)
        {
            // COLLAPSE.

            // Change node value to new value from collapsed children.
            node.Value = result;
            node.Left = null;
            node.Right = null;
            node.Type = ASTNodeType.Number;
        }

        private static string ApplyOperator(ASTNode node, object operandLeft, object operandRight)
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

        private static object? ParseNumber(ASTNode node)
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
    }
}