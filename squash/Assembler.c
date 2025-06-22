#include "Assembler.h"
#include "AST.h"
#include "sb.h"

assembler_t* assembler_new()
{
    assembler_t* assembler = (assembler_t*)malloc(sizeof(assembler_t));
    if (assembler)
    {
        assembler->Is_Windows = true;
        assembler->Is_macOS = false;
        assembler->Is_Linux = false;
    }
    return assembler;
}

/// <summary>
/// Generates assembly language code given specified AST.
/// </summary>
/// <exception cref="Exception">
/// Underlying Assemble() method can throw exceptions as well as this method.
/// </exception>
void GenerateCode(assembler_t* assembler, astnode_t* astNode)
{
    if (astNode != NULL)
    {
        //Logger._log.PrintEndStatistics();

        //Logger.Log.LogInformation("************* Generating Code for specified Abstract Syntax Tree. '" + astNode.ToString() + "'");
        StringBuilder* sb = sb_create();

        //string outputAssembly = string.Empty;

        if (assembler->Is_Linux || assembler->Is_Windows || assembler->Is_macOS)
        {
            if (assembler->Is_macOS)
            {
                sb_append(sb, "section .text\r\n    global  _main\n");
            }
            else
            {
                sb_append(sb, "section .text\r\n    global  main\n");
            }
        }

        sb_append(sb, Assemble(assembler, astNode));

        //Logger._log.PrintEndStatistics();
        LogInformation("************* Compiled Assembler Codegen Full Program Dump");

        if (assembler->Is_Linux)
        {
            sb_append(sb, ".section .note.GNU-stack,\"\",@progbits\n");
            //Console.WriteLine(".section .note.GNU-stack,\"\",@progbits");
        }

        if (assembler->Is_Windows)
        {

        }

        // Console.Write(outputAssembly);

        char* outputAssembly = sb_concat(sb);
        printf("%s", outputAssembly);
    }
    else
    {
        LogCritical("astNode must not be null for assembler to continue.");

        //Logger._log.PrintEndStatistics();

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
char* Assemble(assembler_t* assembler, astnode_t* node)
{
    StringBuilder* sb = sb_create();

    if (node != NULL)
    {
        if (node->Type == AST_FunctionReturn)
        {
            LogInformation("Assemble(): ASTNodeType.FunctionReturn");

            sb_append(sb, Assemble(assembler, node->Left));
            sb_append(sb, Assemble(assembler, node->Right));
            sb_append(sb, "ret");

            if (node->FunctionArguments != NULL)
            {
                list_t* n = node->FunctionArguments;
                astnode_t* ast_n_arg;
                while (n != NULL)
                {
                    ast_n_arg = (astnode_t*)n->data;

                    if (ast_n_arg != NULL)
                    {
                        sb_append(sb, Assemble(assembler, ast_n_arg));
                    }

                    n = n->next;
                }
            }

            // Function definition has node.IsFunctionDefinition set to true
            // and has node.FunctionBody set to a list of ASTNode typed statements.
            if (node->IsFunctionDefinition == true && (node->FunctionBody != NULL))
            {
                list_t* n = node->FunctionBody;
                astnode_t* body_node;

                while (n != NULL)
                {
                    body_node = (astnode_t*)n->data;

                    if (body_node != NULL)
                    {
                        if (body_node->Type == AST_FunctionReturn)
                        {
                            //Console.WriteLine("return");
                        }

                        char* body_node_str = ast_tostring(body_node);

                        LogInformation("Assemble(): -* function body node: %s", body_node_str);
                        sb_append(sb, Assemble(assembler, body_node));
                    }

                    n = n->next;
                }
                //Console.WriteLine("ret back");
            }
        }
        else if (node->Type == AST_VariableDefine)
        {
            LogInformation("Assemble(): ASTNodeType.VariableDefine");

            // This assignment must occur last so it must be at the end of the expression evaluation
            // to store the result of the expression in an assigned variable.
            //Console.WriteLine("VariableDefine assembler not yet implemented.");
            //Console.WriteLine($"var {node.VarSymbol.Name}"); //TODO: Assembly equivalent
            //Console.WriteLine($"mov rax, [{node.VarSymbol.Name},{node.VarSymbol.Value}] ;{node.VarSymbol.VariableType.ToString()}");
            sb_append(sb, Assemble(assembler, node->Left));
            sb_append(sb, "mov\trax, \t[");
            sb_append(sb, node->VarSymbol->Name);
            sb_append(sb, "]\n");

            //Console.WriteLine($"mov\trax, \t[{node.VarSymbol.Name}]");

            sb_append(sb, Assemble(assembler, node->Right));
            //Console.WriteLine($"{node.VarSymbol.Name}=(LEFT,RIGHT)");
            //Console.WriteLine($"mov rax, [{node.VarSymbol.Name}]");
            //Console.WriteLine("pop rbx");

            if (node->FunctionBody != NULL)
            {
                list_t* n = node->FunctionBody;
                astnode_t* nodeFunctionBodyChild;

                while (n != NULL)
                {
                    nodeFunctionBodyChild = (astnode_t*)n->data;
                    sb_append(sb, Assemble(assembler, nodeFunctionBodyChild));

                    n = n->next;
                }
            }
        }
        else if (node->Type == AST_VariableAssignment)
        {
            LogInformation("Assemble(): ASTNodeType.VariableAssignment");

            //Console.WriteLine(node.Right.Value + node.Value +  " " + node.Left.Left.Value 
            //    + " " + node.Left.Value + " " + node.Left.Right.Value);
            sb_append(sb, Assemble(assembler, node->Left));
            sb_append(sb, Assemble(assembler, node->Right));
            //Console.WriteLine("EO Var assignment");
        }
        else if (node->Type == AST_Number)
        {
            LogInformation("Assemble(): ASTNodeType.Number");

            // Load the number value into a register
            sb_append(sb, "mov\trax, \t");
            sb_append(sb, node->Value);
            sb_append(sb, "\n");

            //Console.WriteLine($"mov\trax,\t{node.Value}");
        }
        else if (node->Type == AST_Variable)
        {
            LogInformation("Assemble(): ASTNodeType.Variable");

            // Load the variable value into a register
            sb_append(sb, "mov\trax, \t[");
            sb_append(sb, node->VarSymbol->Name);
            sb_append(sb, "]\n");

            //Console.WriteLine($"mov\trax,\t[{node.VarSymbol.Name}]");
            //Console.WriteLine($"mov [{node.VarSymbol.Name}], rax");

            sb_append(sb, Assemble(assembler, node->Left));
            sb_append(sb, Assemble(assembler, node->Right));
        }
        else if (node->Type == AST_FunctionCall && node->FunctionArguments != NULL)
        {
            LogInformation("Assemble(): ASTNodeType.FunctionCall");

            // Generate code for function call arguments
            list_t* n = node->FunctionArguments;
            astnode_t* arg;

            while (n != NULL)
            {
                arg = (astnode_t*)n->data;
                sb_append(sb, Assemble(assembler, arg));

                n = n->next;
            }

            // Call the function
            if (assembler->Is_macOS)
            {
                sb_append(sb, "call\t_");
                sb_append(sb, node->FunctSymbol->Name);
                sb_append(sb, "\n");
                //Console.WriteLine($"call\t_{node.FunctSymbol.Name}");
            }
            else
            {
                sb_append(sb, "call\t");
                sb_append(sb, node->FunctSymbol->Name);
                sb_append(sb, "\n");
                //Console.WriteLine($"call\t{node.FunctSymbol.Name}");
            }
        }
        else if (node->Type == AST_BIN_OP)
        {
            LogInformation("Assemble(): ASTNodeType.BIN_OP");

            // Generate code for left and right operands
            sb_append(sb, Assemble(assembler, node->Left));

            sb_append(sb, "push\trax\n");
            //Console.WriteLine("push\trax"); // Save value on the stack

            sb_append(sb, Assemble(assembler, node->Right));

            // Perform the operation (addition or subtraction in this example)
            sb_append(sb, "pop\trbx\n");
            //Console.WriteLine("pop\trbx"); // Retrieve left operand from the stack
            if (node->Value == "+")
            {
                sb_append(sb, "add\trax,\trbx\n");
                //Console.WriteLine("add\trax,\trbx");
            }
            else if (node->Value == "-")
            {
                sb_append(sb, "sub\trax,\trbx\n");
                //Console.WriteLine("sub\trax,\trbx");
            }
            else if (node->Value == "*")
            {
                sb_append(sb, "mul\trax,\trbx\n");
                //Console.WriteLine("mul\trax,\trbx");
            }
            else if (node->Value == "/")
            {
                sb_append(sb, "div\trax,\trbx\n");
                //TODO: idiv for signed division
                //Console.WriteLine("div\trax,\trbx"); // TODO: confirm if this is the correct divide operator
            }
        }
        else if (node->Type == AST_UNARY_OP)
        {
            LogInformation("Assemble(): ASTNodeType.UNARY_OP");

            //Console.WriteLine("UNARY_OP assembler not yet implemented.");
            sb_append(sb, Assemble(assembler, node->Left));
            sb_append(sb, Assemble(assembler, node->Right));
        }
        else if (node->Type == AST_FunctionArg)
        {
            //Console.WriteLine("function arg: " + node.ArgumentType + " " + node.Value);
            sb_append(sb, Assemble(assembler, node->Left));
            sb_append(sb, Assemble(assembler, node->Right));

            if (node->FunctionArguments != NULL)
            {
                list_t* n = node->FunctionArguments;
                astnode_t* ast_funct_arg;
                while (n != NULL)
                {
                    ast_funct_arg = (astnode_t*)n->data;

                    if (ast_funct_arg != NULL)
                    {
                        sb_append(sb, Assemble(assembler, ast_funct_arg));
                    }

                    n = n->next;
                }
            }

            // Function definition has node.IsFunctionDefinition set to true
            // and has node.FunctionBody set to a list of ASTNode typed statements.
            if (node->IsFunctionDefinition == true && (node->FunctionBody != NULL))
            {
                list_t* n = node->FunctionBody;
                astnode_t* body_node;

                while (n != NULL)
                {
                    body_node = (astnode_t*)n->data;
                    if (body_node != NULL)
                    {
                        if (body_node->Type == AST_FunctionReturn)
                        {
                            //Console.WriteLine("return");
                        }

                        char* body_node_str = ast_tostring(body_node);

                        LogInformation("Assemble(): -* function body node: %s", body_node_str);
                        sb_append(sb, Assemble(assembler, body_node));
                    }

                    n = n->next;
                }
                //Console.WriteLine("ret back");
            }
        }
        else if (node->Type == AST_FunctionDefinition)
        {
            //Logger.Log.LogInformation("Assemble(): ASTNodeType.FunctionDefinition");

            //Console.WriteLine("function definition: " + node.Value + "()");
            if (assembler->Is_macOS)
            {
                sb_append(sb, "_");
                sb_append(sb, node->Value);
                sb_append(sb, "\n");
                //Console.Write("_" + node.Value + ": \n");
            }
            else
            {
                sb_append(sb, node->Value);
                sb_append(sb, "\n");
                //Console.Write(node.Value + ": \n");
            }

            if (node->FunctionArguments != NULL)
            {
                list_t* n = node->FunctionArguments;
                astnode_t* ast_funct_arg;
                while (n != NULL)
                {
                    ast_funct_arg = (astnode_t*)n->data;

                    if (ast_funct_arg != NULL)
                    {
                        sb_append(sb, Assemble(assembler, ast_funct_arg));
                    }

                    n = n->next;
                }
            }

            // Function definition has node.IsFunctionDefinition set to true
            // and has node.FunctionBody set to a list of ASTNode typed statements.
            if (node->IsFunctionDefinition == true && (node->FunctionBody != NULL))
            {
                list_t* n = node->FunctionBody;
                astnode_t* body_node;

                while (n != NULL)
                {
                    body_node = (astnode_t*)n->data;
                    if (body_node != NULL)
                    {
                        if (body_node->Type == AST_FunctionReturn)
                        {
                            //Console.WriteLine("return");
                        }

                        char* body_node_str = ast_tostring(body_node);

                        LogInformation("Assemble(): -* function body node: ", body_node_str);
                        sb_append(sb, Assemble(assembler, body_node));
                    }

                    n = n->next;
                }
                //Console.WriteLine("ret back");
            }
            else
            {
                LogWarning("No function definition to enumerate maybe just a standard function return.");
            }
        }
        else
        {
            LogError("Invalid ASTNode type sent to assembler.");

            //throw new Exception("Invalid ASTNode type sent to assembler.");
        }
    }
    return sb_concat(sb);
}