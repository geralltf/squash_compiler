#include "Assembler.h"

struct Assembler* assembler_new()
{
    struct Assembler* assembler = (struct Assembler*)malloc(sizeof(struct Assembler));
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
void GenerateCode(struct Assembler* assembler, astnode_t* astNode, char* output_file_name, char* output_binary_file_name, bool enable_tracing)
{
    char* astnode_str;
    if (astNode != NULL)
    {
        PrintEndStatistics();

        astnode_str = ast_tostring(astNode);
        LogInformation("************* Generating Code for specified Abstract Syntax Tree. '%s'", astnode_str);

        StringBuilder* sb = sb_create();

        //string outputAssembly = string.Empty;

        if (assembler->Is_Linux || assembler->Is_Windows || assembler->Is_macOS)
        {
            if (assembler->Is_macOS)
            {
                sb_append(sb, "section .text\r\nglobal _main\n");
            }
            else
            {
                sb_append(sb, "section .text\r\nglobal main\n");
            }
        }

        sb_append(sb, Assemble(assembler, astNode));

        //Logger._log.PrintEndStatistics();

        if (enable_tracing)
        {
            LogInformation("************* Compiled Assembler Codegen Full Program Dump");
        }

        if (assembler->Is_Linux)
        {
            sb_append(sb, ".section .note.GNU-stack,\"\",@progbits\n");
            //Console.WriteLine(".section .note.GNU-stack,\"\",@progbits");
        }

        if (assembler->Is_Windows)
        {

        }

        // Assembly .s file generation.
        char* outputAssembly = sb_concat(sb);
        int source_length = strlen(outputAssembly);

        if (enable_tracing)
        {
            printf("%s", outputAssembly);
        }

        if (output_file_name != NULL)
        {
            if (FileWriteString(output_file_name, outputAssembly)) 
            {
                LogInformation("*-*-*-*-*- output assembly (.s) created '%s' -*-*-*-*-*", output_file_name);
            }
            else 
            {
                LogCritical("*-*-*-*-*- output assembly (.s) failed to compile here '%s' -*-*-*-*-*", output_file_name);
            }
        }
        else 
        {
            LogCritical("*-*-*-*-*- output assembly (.s) failed to compile here '%s' -*-*-*-*-*", output_file_name);
        }

        squash_assembler(assembler, outputAssembly, source_length, output_binary_file_name);
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
char* Assemble(struct Assembler* assembler, astnode_t* node)
{
    StringBuilder* sb = sb_create();

    if (node != NULL)
    {
        if (node->Type == AST_FunctionReturn)
        {
            LogInformation("Assemble(): ASTNodeType.FunctionReturn");

            sb_append(sb, Assemble(assembler, node->Left));
            sb_append(sb, Assemble(assembler, node->Right));
            sb_append(sb, "ret\n");

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
            sb_append(sb, "mov rax, [");
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
            sb_append(sb, "mov rax, ");
            sb_append(sb, node->Value);
            sb_append(sb, "\n");

            //Console.WriteLine($"mov\trax,\t{node.Value}");
        }
        else if (node->Type == AST_Variable)
        {
            LogInformation("Assemble(): ASTNodeType.Variable");

            // Load the variable value into a register
            sb_append(sb, "mov rax, [");
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

            sb_append(sb, "push rax\n");
            //Console.WriteLine("push\trax"); // Save value on the stack

            sb_append(sb, Assemble(assembler, node->Right));

            // Perform the operation (addition or subtraction in this example)
            sb_append(sb, "pop rbx\n");
            //Console.WriteLine("pop\trbx"); // Retrieve left operand from the stack
            if (strcmp(node->Value, "+") == 0)
            {
                sb_append(sb, "add rax, rbx\n");
                //Console.WriteLine("add\trax,\trbx");
            }
            else if (strcmp(node->Value, "-") == 0)
            {
                sb_append(sb, "sub rax, rbx\n");
                //Console.WriteLine("sub\trax,\trbx");
            }
            else if (strcmp(node->Value, "*") == 0)
            {
                sb_append(sb, "mul rax, rbx\n");
                //Console.WriteLine("mul\trax,\trbx");
            }
            else if (strcmp(node->Value, "/") == 0)
            {
                sb_append(sb, "div rax, rbx\n");
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
            LogInformation("Assemble(): ASTNodeType.FunctionDefinition");

            //Console.WriteLine("function definition: " + node.Value + "()");
            if (assembler->Is_macOS)
            {
                sb_append(sb, "_");
                sb_append(sb, node->Value);
                sb_append(sb, ": \n");
                //Console.Write("_" + node.Value + ": \n");
            }
            else
            {
                sb_append(sb, node->Value);
                sb_append(sb, ": \n");
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

                        LogInformation("Assemble(): -* function body node: %s", body_node_str);
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

void asm_encode_instruction(unsigned char* mnumonic, unsigned char* register_name, unsigned char* operands)
{
   

}

void asm_parse_instruction(char* carr_line)
{
    char* token;
    const char* delimiter = ", ";

    token = strtok(carr_line, delimiter);
    printf("%s\n", token);

    while (token != NULL)
    {
        printf("%s\n", token);
        token = strtok(NULL, delimiter);
    }

    unsigned char* mnumonic = "SUB";
    unsigned char* operands = "RSP, 0x28"; // right operand is of type 'O_IMM' because it is a constant value obtained immediately after the register.
    unsigned char* register_name = "RSP"; // left operand is of type 'O_REG'
    
    asm_encode_instruction(mnumonic, register_name, operands); // should encode to the following hex sequence: 4883ec28
}

void asm_parse_line(char* carr_line)
{
    if (strcmp(carr_line, "section .text") == 0)
    {

    }
    else if (strcmp(carr_line, "global main") == 0)
    {

    }
    else
    {
        // instruction.
        asm_parse_instruction(carr_line);
    }
}

void squash_assembler(struct Assembler* assembler, char* source_asm, int source_size, char* output_binary_file_name)
{
    int index;
    char currentChar = '\0';
    index = 0;
    StringBuilder* sb = sb_create();
    char* carr_line;
    char* mem = (char*)malloc(sizeof(char) * 2);

    while (index < source_size)
    {
        currentChar = (source_asm + index)[0];

        if (currentChar == '\n' || currentChar == '\r')
        {
            carr_line = sb_concat(sb);
            
            asm_parse_line(carr_line);

            sb_reset(sb);
        }
        else
        {
            mem[0] = currentChar;
            mem[1] = '\0';

            sb_append(sb, mem);
        }
        index++;
    }

    LogInformation("*-*-*-*-*- output assemble portable executable (.exe) created '%s' -*-*-*-*-*", output_binary_file_name);
}