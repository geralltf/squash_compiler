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
        private static Dictionary<string, int> precedence = new Dictionary<string, int>
        {
            {"*", 2},
            {"/", 2},
            {"+", 1},
            {"-", 1}
        };

        private static Dictionary<string, bool> associativity = new Dictionary<string, bool>
        {
            {"*", true},
            {"/", true},
            {"+", true},
            {"-", true}
        };


        private static void ParseNumber(string numberValue, out object val)
        { 
            int resultInt;
            double resultDouble;
            float resultFloat;

            if (int.TryParse(numberValue, out resultInt))
            {
                val = resultInt;
                return;
            }
            if (double.TryParse(numberValue, out resultDouble))
            {
                val = resultDouble;
                return;
            }
            if (float.TryParse(numberValue, out resultFloat))
            {
                val = resultFloat;
                return;
            }
            val = null;
            Logger.Log.LogError("Optimiser.ParseNumber(): Unexpected type specified of number.");
        }

        private static int GetPrecedence(string op)
        {
            switch (op)
            {
                case "+":
                case "-":
                    return 1;
                case "*":
                case "/":
                    return 2;
                default:
                    return 0;
            }
        }

        private static bool ShouldEvaluateFirst(string op1, string op2)
        {
            if (!precedence.ContainsKey(op1) || !precedence.ContainsKey(op2))
            {
                return false;
            }

            if (precedence[op1] > precedence[op2])
            {
                return true;
            }
            else if (precedence[op1] == precedence[op2])
            {
                return associativity[op1];
            }
            else
            {
                return false;
            }
        }

        private static void CollapseNode(ref ASTNode node, string result)
        {
            // COLLAPSE.

            // Change node value to new value from collapsed children.

            node.Value = result;

            // Detach node children and change node type to collapsed number.
            node.Left = null;
            node.Right = null;
            node.Type = ASTNodeType.Number;
        }


        public static int OptimiseNode(ref ASTNode node)
        {
            ASTNode left = null;
            ASTNode right = null;

            if (node == null)
            {
                return 0;
            }

            // Collapse the child nodes into one immediate number value node type
            // then for all other node types try and run the optimiser on each node.

            // TODO: Run optimisers on const correct variables as well as constants
            // in compiler time to optimise all code paths.

            if (node.Type == ASTNodeType.Number)
            {
                return int.Parse(node.Value);
            }

            int operandLeft = 0;
            int operandRight = 0;

            left = node.Left;
            right = node.Right;

            if (left != null)
            {
                operandLeft = OptimiseNode(ref left);
            }

            if (right != null)
            {
                operandRight = OptimiseNode(ref right);
            }

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

            int result = 0;

            if (node.Type == ASTNodeType.BIN_OP)
            {
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

                // COLLAPSE.

                // Change node value to new value from collapsed children.
                node.Value = result.ToString();
                node.Left = null;
                node.Right = null;
                node.Type = ASTNodeType.Number;
            }

            return result;
        }
    }
}