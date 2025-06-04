#include "AST.h"

//typedef struct Assembly_s 
//{
//	char* operandLeft;
//	char* operandRight;
//	enum OperatorType operatorType;
//	AST* ast;
//	struct Assembly_s* leftChild;
//	struct Assembly_s* rightChild;
//} Assembly;

//AST* Expr(char* inProgramExpression);
//bool Compile(AST* programAST, ASMTree** programASM);

struct Assembler
{
    /// <summary>
    /// The expression tree output from a specified ExpressionCompiler.
    /// </summary>
    //AbstractSyntaxTree* AST;

    bool Is_macOS;
    bool Is_Linux;
    bool Is_Windows;
}

/// <summary>
/// Generates assembly language code given specified AST.
/// </summary>
/// <exception cref="Exception">
/// Underlying Assemble() method can throw exceptions as well as this method.
/// </exception>
void GenerateCode(ASTNode* astNode)
{
    if (astNode != NULL)
    {
        Logger._log.PrintEndStatistics();

        Logger.Log.LogInformation("************* Generating Code for specified Abstract Syntax Tree. '" + astNode.ToString() + "'");

        string outputAssembly = string.Empty;

        if (Is_Linux || Is_Windows || Is_macOS)
        {
            if (Is_macOS)
            {
                outputAssembly += "section .text\r\n    global  _main\n";
            }
            else
            {
                outputAssembly += "section .text\r\n    global  main\n";
            }
        }

        outputAssembly += Assemble(astNode);

        Logger._log.PrintEndStatistics();
        Logger.Log.LogInformation("************* Compiled Assembler Codegen Full Program Dump");

        if (Is_Linux)
        {
            outputAssembly += ".section .note.GNU-stack,\"\",@progbits\n";

            //Console.WriteLine(".section .note.GNU-stack,\"\",@progbits");
        }

        if (Is_Windows)
        {

        }

        Console.Write(outputAssembly);
    }
    else
    {
        Logger.Log.LogCritical("astNode must not be null for assembler to continue.");

        Logger._log.PrintEndStatistics();

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
string Assemble(ASTNode* node)
{
    //Console.WriteLine("AST: " + node.ToString());

    StringBuilder sb = new StringBuilder();

    if (node != NULL)
    {
        if (node.Type == ASTNodeType.FunctionReturn)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.FunctionReturn");

            sb.Append(Assemble(node.Left));
            sb.Append(Assemble(node.Right));

            sb.AppendLine("ret");
            Console.WriteLine("ret");

            if (node.FunctionArguments != null)
            {
                foreach(ASTNode arg in node.FunctionArguments)
                {
                    sb.Append(Assemble(arg));
                }
            }

            // Function definition has node.IsFunctionDefinition set to true
            // and has node.FunctionBody set to a list of ASTNode typed statements.
            if (node.IsFunctionDefinition == true
                && (node.FunctionBody != null && node.FunctionBody.Count > 0))
            {
                foreach(ASTNode bodyNode in node.FunctionBody)
                {
                    if (bodyNode.Type == ASTNodeType.FunctionReturn)
                    {
                        //Console.WriteLine("return");
                    }
                    Logger.Log.LogInformation("Assemble(): -* function body node: " + bodyNode.ToString());
                    sb.Append(Assemble(bodyNode));
                }
                //Console.WriteLine("ret back");
            }
        }
        else if (node.Type == ASTNodeType.VariableDefine)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.VariableDefine");

            // This assignment must occur last so it must be at the end of the expression evaluation
            // to store the result of the expression in an assigned variable.
            //Console.WriteLine("VariableDefine assembler not yet implemented.");
            //Console.WriteLine($"var {node.VarSymbol.Name}"); //TODO: Assembly equivalent
            //Console.WriteLine($"mov rax, [{node.VarSymbol.Name},{node.VarSymbol.Value}] ;{node.VarSymbol.VariableType.ToString()}");
            sb.Append(Assemble(node.Left));

            sb.AppendLine($"mov\trax, \t[{node.VarSymbol.Name}]");
            Console.WriteLine($"mov\trax, \t[{node.VarSymbol.Name}]");

            sb.Append(Assemble(node.Right));
            //Console.WriteLine($"{node.VarSymbol.Name}=(LEFT,RIGHT)");
            //Console.WriteLine($"mov rax, [{node.VarSymbol.Name}]");
            //Console.WriteLine("pop rbx");

            if (node.FunctionBody != null)
            {
                foreach(ASTNode nodeFunctionBodyChild in node.FunctionBody)
                {
                    sb.Append(Assemble(nodeFunctionBodyChild));
                }
            }
        }
        else if (node.Type == ASTNodeType.VariableAssignment)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.VariableAssignment");

            //Console.WriteLine(node.Right.Value + node.Value +  " " + node.Left.Left.Value 
            //    + " " + node.Left.Value + " " + node.Left.Right.Value);
            sb.Append(Assemble(node.Left));
            sb.Append(Assemble(node.Right));
            //Console.WriteLine("EO Var assignment");
        }
        else if (node.Type == ASTNodeType.Number)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.Number");

            // Load the number value into a register
            sb.AppendLine($"mov\trax,\t{node.Value}");
            Console.WriteLine($"mov\trax,\t{node.Value}");
        }
        else if (node.Type == ASTNodeType.Variable)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.Variable");

            // Load the variable value into a register
            sb.AppendLine($"mov\trax,\t[{node.VarSymbol.Name}]");
            Console.WriteLine($"mov\trax,\t[{node.VarSymbol.Name}]");
            //Console.WriteLine($"mov [{node.VarSymbol.Name}], rax");

            sb.Append(Assemble(node.Left));
            sb.Append(Assemble(node.Right));
        }
        else if (node.Type == ASTNodeType.FunctionCall && node.FunctionArguments != null)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.FunctionCall");

            // Generate code for function call arguments
            foreach(ASTNode arg in node.FunctionArguments)
            {
                sb.Append(Assemble(arg));
            }

            // Call the function
            if (Is_macOS)
            {
                sb.AppendLine($"call\t_{node.FunctSymbol.Name}");
                Console.WriteLine($"call\t_{node.FunctSymbol.Name}");
            }
            else
            {
                sb.AppendLine($"call\t{node.FunctSymbol.Name}");
                Console.WriteLine($"call\t{node.FunctSymbol.Name}");
            }
        }
        else if (node.Type == ASTNodeType.BIN_OP)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.BIN_OP");

            // Generate code for left and right operands
            sb.Append(Assemble(node.Left));

            sb.AppendLine("push\trax");
            Console.WriteLine("push\trax"); // Save value on the stack

            sb.Append(Assemble(node.Right));

            // Perform the operation (addition or subtraction in this example)
            sb.AppendLine("pop\trbx");
            Console.WriteLine("pop\trbx"); // Retrieve left operand from the stack
            if (node.Value == "+")
            {
                sb.AppendLine("add\trax,\trbx");
                Console.WriteLine("add\trax,\trbx");
            }
            else if (node.Value == "-")
            {
                sb.AppendLine("sub\trax,\trbx");
                Console.WriteLine("sub\trax,\trbx");
            }
            else if (node.Value == "*")
            {
                sb.AppendLine("mul\trax,\trbx");
                Console.WriteLine("mul\trax,\trbx");
            }
            else if (node.Value == "/")
            {
                sb.AppendLine("div\trax,\trbx");
                //TODO: idiv for signed division
                Console.WriteLine("div\trax,\trbx"); // TODO: confirm if this is the correct divide operator
            }
        }
        else if (node.Type == ASTNodeType.UNARY_OP)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.UNARY_OP");

            //Console.WriteLine("UNARY_OP assembler not yet implemented.");
            sb.Append(Assemble(node.Left));
            sb.Append(Assemble(node.Right));
        }
        else if (node.Type == ASTNodeType.FunctionArg)
        {
            Console.WriteLine("function arg: " + node.ArgumentType + " " + node.Value);

            sb.Append(Assemble(node.Left));
            sb.Append(Assemble(node.Right));

            if (node.FunctionArguments != null)
            {
                foreach(ASTNode arg in node.FunctionArguments)
                {
                    sb.Append(Assemble(arg));
                }
            }

            // Function definition has node.IsFunctionDefinition set to true
            // and has node.FunctionBody set to a list of ASTNode typed statements.
            if (node.IsFunctionDefinition == true
                && (node.FunctionBody != null && node.FunctionBody.Count > 0))
            {
                foreach(ASTNode bodyNode in node.FunctionBody)
                {
                    if (bodyNode.Type == ASTNodeType.FunctionReturn)
                    {
                        //Console.WriteLine("return");
                    }
                    Logger.Log.LogInformation("Assemble(): -* function body node: " + bodyNode.ToString());
                    sb.Append(Assemble(bodyNode));
                }
                //Console.WriteLine("ret back");
            }
        }
        else if (node.Type == ASTNodeType.FunctionDefinition)
        {
            Logger.Log.LogInformation("Assemble(): ASTNodeType.FunctionDefinition");

            Console.WriteLine("function definition: " + node.Value + "()");
            if (Is_macOS)
            {
                sb.Append("_" + node.Value + ": \n");
                Console.Write("_" + node.Value + ": \n");
            }
            else
            {
                sb.Append(node.Value + ": \n");
                Console.Write(node.Value + ": \n");
            }

            if (node.FunctionArguments != null)
            {
                foreach(ASTNode arg in node.FunctionArguments)
                {
                    sb.Append(Assemble(arg));
                }
            }

            // Function definition has node.IsFunctionDefinition set to true
            // and has node.FunctionBody set to a list of ASTNode typed statements.
            if (node.IsFunctionDefinition == true
                && (node.FunctionBody != null && node.FunctionBody.Count > 0))
            {
                foreach(ASTNode bodyNode in node.FunctionBody)
                {
                    if (bodyNode.Type == ASTNodeType.FunctionReturn)
                    {
                        //Console.WriteLine("return");
                    }
                    Logger.Log.LogInformation("Assemble(): -* function body node: " + bodyNode.ToString());
                    sb.Append(Assemble(bodyNode));
                }
                //Console.WriteLine("ret back");
            }
            else
            {
                Logger.Log.LogWarning("No function definition to enumerate maybe just a standard function return.");
            }
        }
        else
        {
            Logger.Log.LogError("Invalid ASTNode type sent to assembler.");

            throw new Exception("Invalid ASTNode type sent to assembler.");
        }
    }

    return sb.ToString();
}