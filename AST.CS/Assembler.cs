using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Squash.Compiler
{
    public class Assembler
    {
        /// <summary>
        /// The expression tree output from a specified ExpressionCompiler.
        /// </summary>
        public AbstractSyntaxTree AST { get; set; }

        public Assembler(AbstractSyntaxTree expressionTree)
        {
            AST = expressionTree;
        }

        /// <summary>
        /// Generates assembly language code given specified AST.
        /// </summary>
        /// <exception cref="Exception">
        /// Underlying Assemble() method can throw exceptions as well as this method.
        /// </exception>
        public void GenerateCode()
        {
            if (AST != null && AST.Root != null)
            {
                string outputAssembly = Assemble(AST.Root);
                Console.WriteLine(outputAssembly);
            }
            else
            {
                throw new Exception("Root node must not be null for assembler to continue.");
            }
        }

        /// <summary>
        /// Assembles the specified ASTNode into assembly language instructions.
        /// This is the compiler backend.
        /// </summary>
        /// <param name="node">
        /// The specified ASTNode to compile into assembly instructions.
        /// </param>
        /// <returns>
        /// A string comprising a set of assembly language instructions to be further assembled to machine code.
        /// </returns>
        /// <exception cref="Exception">
        /// Can throw exceptions related to invalid ASTNodeType types.
        /// </exception>
        public string Assemble(ASTNode node)
        {
            //Console.WriteLine("AST: " + node.ToString());

            StringBuilder sb = new StringBuilder();

            if(node != null)
            {
                if(node.Type == ASTNodeType.VariableDefine)
                {
                    Console.WriteLine("VariableDefine assembler not yet implemented.");
                }
                else if (node.Type == ASTNodeType.Number)
                {
                    // Load the number value into a register
                    Console.WriteLine($"mov rax, {node.Value}");

                }
                else if (node.Type == ASTNodeType.Variable)
                {
                    // Load the variable value into a register
                    Console.WriteLine($"mov rax, [{node.VarSymbol.Name}]");

                }
                else if (node.Type == ASTNodeType.FunctionCall && node.FunctionArguments != null)
                {
                    // Generate code for function call arguments
                    foreach (ASTNode arg in node.FunctionArguments)
                    {
                        Assemble(arg);
                    }

                    // Call the function
                    Console.WriteLine($"call {node.FunctSymbol.Name}");

                }
                else if (node.Type == ASTNodeType.BIN_OP)
                {
                    // Generate code for left and right operands
                    Assemble(node.Left);
                    Console.WriteLine("push rax"); // Save value on the stack
                    Assemble(node.Right);

                    // Perform the operation (addition or subtraction in this example)
                    Console.WriteLine("pop rbx"); // Retrieve left operand from the stack
                    if (node.Value == "+")
                    {
                        Console.WriteLine("add rax, rbx");
                    }
                    else if (node.Value == "-")
                    {
                        Console.WriteLine("sub rax, rbx");
                    }
                    else if (node.Value == "*")
                    {
                        Console.WriteLine("mul rax, rbx");
                    }
                    else if (node.Value == "/")
                    {
                        Console.WriteLine("div rax, rbx"); // TODO: confirm if this is the correct divide operator
                    }
                }
                else if (node.Type == ASTNodeType.UNARY_OP)
                {
                    Console.WriteLine("UNARY_OP assembler not yet implemented.");
                }
                else
                {
                    throw new Exception("Invalid ASTNode type sent to assembler.");
                }
            }

            return sb.ToString();
        }
    }
}
