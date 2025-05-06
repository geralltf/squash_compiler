using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SquashC.Compiler
{
    public enum ASTNodeType
    {
        BIN_OP,
        UNARY_OP,
        Number,
        VariableDefine,
        VariableAssignment,
        VariableDeclaration,
        Variable,
        FunctionCall,
        FunctionDefinition,
        FunctionReturn,
        FunctionArg
    }

    public class ASTNode
    {
        public ASTNodeType Type { get; set; }
        public string Value { get; set; }
        public ASTNode? Left { get; set; }
        public ASTNode? Right { get; set; }

        public bool IsFunctionCall { get; set; }
        public FunctionSymbol? FunctSymbol { get; set; }
        public List<ASTNode>? FunctionArguments { get; set; }

        public bool IsVariable { get; set; }
        public VarType? VariableType { get; set; }
        public VariableSymbol? VarSymbol { get; set; }

        public bool IsFunctionDefinition { get; set; }
        public string? FunctionName { get; set; }
        public List<ASTNode>? FunctionBody { get; set; }
        public VarType? FunctionReturnType { get; set; }
        public string ArgumentType { get; set; }
        public ASTNode(ASTNodeType type, string value, ASTNode? left, ASTNode? right)
        {
            Type = type;
            Value = value;
            Left = left;
            Right = right;
            FunctSymbol = null;
            FunctionArguments = null;
            VarSymbol = null;
            IsFunctionCall = false;
            IsVariable = false;
            IsFunctionDefinition = false;
        }

        public ASTNode(ASTNodeType type, string argumentType, string value, ASTNode? left, ASTNode? right)
        {
            Type = type;
            ArgumentType = argumentType;
            Value = value;
            Left = left;
            Right = right;
            FunctSymbol = null;
            FunctionArguments = null;
            VarSymbol = null;
            IsFunctionCall = false;
            IsVariable = false;
            IsFunctionDefinition = false;
        }

        public ASTNode(ASTNodeType type, string value, FunctionSymbol functionSymbol, List<ASTNode> arguments)
        {
            Type = type;
            Value = value;
            Left = null;
            Right = null;
            FunctSymbol = functionSymbol;
            FunctionArguments = arguments;
            VarSymbol = null;
            IsFunctionCall = true;
            IsVariable = false;
            IsFunctionDefinition = false;
        }

        public ASTNode(ASTNodeType type, string value, VariableSymbol variableSymbol)
        {
            Type = type;
            Value = value;
            Left = null;
            Right = null;
            FunctSymbol = null;
            FunctionArguments = null;
            VarSymbol = variableSymbol;
            IsVariable = true;
            IsFunctionDefinition = false;
        }

        public override string ToString()
        {
            return "Type: " + Type.ToString() + ", Value: '" + Value + "', IsVariable: " + IsVariable.ToString()
                + " IsFunctionCall: " + IsFunctionCall.ToString()
                + " IsFunctionDefinition: " + IsFunctionDefinition.ToString()
                + " Function Args: " + ((FunctionArguments != null && FunctionArguments.Count > 0) ? " Has them." : "Doesn't have them.");
        }
    }

    public class AbstractSyntaxTree
    {
        public ASTNode? Root { get; set; }

        public AbstractSyntaxTree()
        {
            Root = null;
        }

        public void AddBinaryOperator(string op, ASTNode? left, ASTNode? right)
        {
            ASTNode node = new ASTNode(ASTNodeType.BIN_OP, op, left, right); // Create a new node for the binary operator
            Root = node; // Assign the new node as the root of the tree
        }

        public void AddUnaryOperator(string op, ASTNode? operand)
        {
            ASTNode node = new ASTNode(ASTNodeType.UNARY_OP, op, operand, null); // Create a new node for the unary operator
            Root = node; // Assign the new node as the root of the tree
        }
    }
}
