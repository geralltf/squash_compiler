using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SquashC.Compiler
{
    /// <summary>
    /// Applies code optimisations to in memory expressions and statements 
    /// by compile-time evaluation of constants in many ASTNode's applying collapsing logic to Number nodes.
    /// </summary>
    public class Optimiser
    {
        public static void ParseNumber(string numberValue, out object val)
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

        public static void OptimiseNode(ref ASTNode node)
        {
            ASTNode left = null;
            ASTNode right = null;

            if (node == null)
            {
                return;
            }

            if(node.Type == ASTNodeType.BIN_OP)
            {
                object leftResult = null;
                bool leftSpecified = false;
                int leftResultInt = 0;
                double leftResultDouble = 0;
                float leftResultFloat = 0;

                object rightResult = null;
                bool rightSpecified = false;
                int rightResultInt = 0;
                double rightResultDouble = 0;
                float rightResultFloat = 0;

                if (node.Left != null)
                {
                    if(node.Left.Type == ASTNodeType.Number)
                    {
                        ParseNumber(node.Left.Value, out leftResult);
                        if(leftResult is int)
                        {
                            leftSpecified = true;
                            leftResultInt = (int)leftResult;
                        }
                        else if (leftResult is double)
                        {
                            leftSpecified = true;
                            leftResultDouble = (double)leftResult;
                        }
                        else if (leftResult is float)
                        {
                            leftSpecified = true;
                            leftResultFloat = (float)leftResult;
                        }
                    }
                }
                if (node.Right != null)
                {
                    if (node.Right.Type == ASTNodeType.Number)
                    {
                        ParseNumber(node.Right.Value, out rightResult);
                        if (rightResult is int)
                        {
                            rightSpecified = true;
                            rightResultInt = (int)rightResult;
                        }
                        else if (rightResult is double)
                        {
                            rightSpecified = true;
                            rightResultDouble = (double)rightResult;
                        }
                        else if (rightResult is float)
                        {
                            rightSpecified = true;
                            rightResultFloat = (float)rightResult;
                        }
                    }
                }

                if(leftSpecified && rightSpecified)
                {
                    // Collapse the child nodes into one immediate number value node type
                    // then for all other node types try and run the optimiser on each node.

                    // TODO: Run optimisers on const correct variables as well as constants
                    // in compiler time to optimise all code paths.

                    string output = string.Empty;

                    if (leftResult is int)
                    {
                        if (rightResult is int)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultInt + rightResultInt).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultInt - rightResultInt).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultInt * rightResultInt).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultInt / rightResultInt).ToString();
                            }
                        }
                        else if (rightResult is double)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultInt + rightResultDouble).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultInt - rightResultDouble).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultInt * rightResultDouble).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultInt / rightResultDouble).ToString();
                            }
                        }
                        else if (rightResult is float)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultInt + rightResultFloat).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultInt - rightResultFloat).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultInt * rightResultFloat).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultInt / rightResultFloat).ToString();
                            }
                        }
                    }
                    else if (leftResult is double)
                    {
                        if (rightResult is int)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultDouble + rightResultInt).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultDouble - rightResultInt).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultDouble * rightResultInt).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultDouble / rightResultInt).ToString();
                            }
                        }
                        else if (rightResult is double)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultDouble + rightResultDouble).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultDouble - rightResultDouble).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultDouble * rightResultDouble).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultDouble / rightResultDouble).ToString();
                            }
                        }
                        else if (rightResult is float)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultDouble + rightResultFloat).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultDouble - rightResultFloat).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultDouble * rightResultFloat).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultDouble / rightResultFloat).ToString();
                            }
                        }
                    }
                    else if (leftResult is float)
                    {
                        if (rightResult is int)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultFloat + rightResultInt).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultFloat - rightResultInt).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultFloat * rightResultInt).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultFloat / rightResultInt).ToString();
                            }
                        }
                        else if (rightResult is double)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultFloat + rightResultDouble).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultFloat - rightResultDouble).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultFloat * rightResultDouble).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultFloat / rightResultDouble).ToString();
                            }
                        }
                        else if (rightResult is float)
                        {
                            if (node.Value == "+")
                            {
                                output = (leftResultFloat + rightResultFloat).ToString();
                            }
                            else if (node.Value == "-")
                            {
                                output = (leftResultFloat - rightResultFloat).ToString();
                            }
                            else if (node.Value == "*")
                            {
                                output = (leftResultFloat * rightResultFloat).ToString();
                            }
                            else if (node.Value == "/")
                            {
                                output = (leftResultFloat / rightResultFloat).ToString();
                            }
                        }
                    }

                    // COLLAPSE.

                    // Change node value to new value from collapsed children.
                    node.Value = output;

                    // Detach node children and change node type to collapsed number.
                    node.Left = null;
                    node.Right = null;
                    node.Type = ASTNodeType.Number;
                }
                else
                {
                    left = node.Left;
                    right = node.Right;

                    if(left != null)
                    {
                        OptimiseNode(ref left);
                    }
                    if(right != null)
                    {
                        OptimiseNode(ref right);
                    }
                }
            }

            left = node.Left;
            right = node.Right;

            if (left != null)
            {
                OptimiseNode(ref left);
            }
            if (right != null)
            {
                OptimiseNode(ref right);
            }

            if (node.FunctionBody != null && node.FunctionBody.Count > 0)
            {
                for (int i = 0; i < node.FunctionBody.Count; i++)
                {
                    ASTNode statement = node.FunctionBody[i];

                    OptimiseNode(ref statement);
                }
            }
        }
    }
}
