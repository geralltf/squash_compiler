#include "Assembler.h"
#include "Instruction.Create.h"

struct Assembler* assembler_new()
{
    struct Assembler* assembler = (struct Assembler*)malloc(sizeof(struct Assembler));
    if (assembler)
    {
        assembler->instructions = list_new();
        assembler->instructions->next = NULL;
        assembler->instructions->prev = NULL;
        assembler->instructions->data = NULL;

        assembler->stream_bytes = NULL;

        assembler->currentAnonLabel = NULL;
        assembler->currentLabel = NULL;
        assembler->definedAnonLabel = false;
        assembler->currentLabelId = -1;

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
            //LogCritical("*-*-*-*-*- output assembly (.s) failed to compile here '%s' -*-*-*-*-*", output_file_name);

            if (output_binary_file_name != NULL)
            {
                test_assembler();
            }
            else
            {
                LogCritical("*-*-*-*-*- output assembly (.s) failed to compile here '%s' -*-*-*-*-*", output_file_name);
            }
        }

        //unsigned long RIP_program_start_addr = 1400000; // TEST: Test address.
        //squash_assemble(assembler, RIP_program_start_addr);

        //squash_assembler(assembler, outputAssembly, source_length, output_binary_file_name);

        //unsigned char* squash_assemble(struct Assembler* assembler, unsigned long RIP_program_start_addr);
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

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="data">Data</param>
/// <param name="index">Start index</param>
/// <param name="length">Number of bytes</param>
struct Instruction* Instruction_CreateDeclareByte(unsigned char* data, int index, int length)
{
    struct Instruction* instruction;

    if (data == NULL)
    {
        return NULL;
    }
    if ((unsigned int)length - 1 > 16 - 1)
    {
        return NULL;
    }
    //if ((unsigned long)(unsigned int)index + (unsigned int)length > (unsigned int)length) // data.Length
    //	ThrowHelper.ThrowArgumentOutOfRangeException_index();

    instruction = instruction_new();
    instruction_init(&instruction);

    SetCode(instruction, DeclareByte);

    SetInternalDeclareDataCount(instruction, (unsigned int)length);

    for (int i = 0; i < length; i++)
    {
        Instruction_SetDeclareByteValue(instruction, i, data[index + i]);
    }

    //Debug.Assert(instruction.OpCount == 0);
    return instruction;
}

/// <summary>
/// Adds data
/// </summary>
/// <param name="array">Data</param>
/// <param name="index">Start index</param>
/// <param name="length">Length in bytes</param>
void db(struct Assembler* assembler, unsigned char* array, int index, int length)
{
    if (array == NULL)
    {
        return;
        //ThrowHelper.ThrowArgumentNullException_array();
    }

    if (index < 0)
    {
        return;
        //ThrowHelper.ThrowArgumentOutOfRangeException_index();
    }
    if (length < 0 || (unsigned int)(index + length) >(unsigned int)length)
    {
        return;
        //ThrowHelper.ThrowArgumentOutOfRangeException_length();
    }
    const int maxLength = 16;
    //int cycles = Math.DivRem(length, maxLength, out int rest);
    int cycles = length / maxLength;
    int rest = length % maxLength;

    struct Instruction* inst;

    int currentPosition = index;
    for (int i = 0; i < cycles; i++)
    {
        inst = Instruction_CreateDeclareByte(array, currentPosition, maxLength);
        AddInstruction(assembler, inst);
        currentPosition += maxLength;
    }
    if (rest > 0)
    {
        inst = Instruction_CreateDeclareByte(array, currentPosition, rest);
        AddInstruction(assembler, inst);
    }
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>byte</c>.</summary>
void db_imm1(struct Assembler* assembler, unsigned char imm0)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm1(imm0));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>byte</c>.</summary>
void db_imm2(struct Assembler* assembler, unsigned char imm0, unsigned char imm1)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm2(imm0, imm1));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>byte</c>.</summary>
void db_imm3(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm3(imm0, imm1, imm2));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>byte</c>.</summary>
void db_imm4(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm4(imm0, imm1, imm2, imm3));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm5(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm5(imm0, imm1, imm2, imm3, imm4));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm6(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm6(imm0, imm1, imm2, imm3, imm4, imm5));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm7(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm7(imm0, imm1, imm2, imm3, imm4, imm5, imm6));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm8(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm8(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm9(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7, unsigned char imm8)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm9(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7, imm8));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm10(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7, unsigned char imm8, unsigned char imm9)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm10(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7, imm8, imm9));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm11(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7, unsigned char imm8, unsigned char imm9, unsigned char imm10)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm11(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7, imm8, imm9, imm10));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm12(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7, unsigned char imm8, unsigned char imm9, unsigned char imm10, unsigned char imm11)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm12(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7, imm8, imm9, imm10, imm11));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm13(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7, unsigned char imm8, unsigned char imm9, unsigned char imm10, unsigned char imm11, unsigned char imm12)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm13(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7, imm8, imm9, imm10, imm11, imm12));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm14(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7, unsigned char imm8, unsigned char imm9, unsigned char imm10, unsigned char imm11, unsigned char imm12, unsigned char imm13)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm14(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7, imm8, imm9, imm10, imm11, imm12, imm13));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm15(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7, unsigned char imm8, unsigned char imm9, unsigned char imm10, unsigned char imm11, unsigned char imm12, unsigned char imm13, unsigned char imm14)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm15(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7, imm8, imm9, imm10, imm11, imm12, imm13, imm14));
}

/// <summary>Creates a <c>db</c> asm directive with the type <c>unsigned char</c>.</summary>
void db_imm16(struct Assembler* assembler, unsigned char imm0, unsigned char imm1, unsigned char imm2, unsigned char imm3, unsigned char imm4, unsigned char imm5, unsigned char imm6, unsigned char imm7, unsigned char imm8, unsigned char imm9, unsigned char imm10, unsigned char imm11, unsigned char imm12, unsigned char imm13, unsigned char imm14, unsigned char imm15)
{
    AddInstruction(assembler, Instruction_CreateDeclareByte_imm16(imm0, imm1, imm2, imm3, imm4, imm5, imm6, imm7, imm8, imm9, imm10, imm11, imm12, imm13, imm14, imm15));
}


/// <summary>call selector:offset instruction.</summary>
void call(struct Assembler* assembler, unsigned short selector, unsigned int offset)
{
    AddInstruction(assembler, Instruction_CreateBranchFar(assembler->Bitness >= 32 ? Call_ptr1632 : Call_ptr1616, selector, offset));
}


/// <summary>jmp selector:offset instruction.</summary>
void jmp(struct Assembler* assembler, unsigned short selector, unsigned int offset)
{
    AddInstruction(assembler, Instruction_CreateBranchFar(assembler->Bitness >= 32 ? Jmp_ptr1632 : Jmp_ptr1616, selector, offset));
}

/// <summary>xlatb instruction.</summary>
void xlatb(struct Assembler* assembler)
{
    enum Register baseReg;

    switch (assembler->Bitness)
    {
    case 64:
        baseReg = Register_RBX;
        break;
    case 32:
        baseReg = Register_EBX;
        break;
    default:
        baseReg = Register_BX;
        break;
    }

    struct AssemblerMemoryOperand* assMemOp = AssemblerMemoryOperand_new(MOS_None, Register_None, baseReg, Register_AL, 1, 0, AF_None);
    struct MemoryOperand* memOp = ToMemoryOperand(assMemOp, assembler->Bitness);

    AddInstruction(assembler, Instruction_Create1Mem(Xlat_m8, memOp));
}

void AppendNop(struct Assembler* assembler, int amount)
{
    switch (amount)
    {
    case 1:
        db_imm1(assembler, 0x90); // NOP
        break;
    case 2:
        db_imm2(assembler, 0x66, 0x90); // 66 NOP
        break;
    case 3:
        db_imm3(assembler, 0x0F, 0x1F, 0x00); // NOP dword ptr [eax] or NOP word ptr [bx+si]
        break;
    case 4:
        db_imm4(assembler, 0x0F, 0x1F, 0x40, 0x00); // NOP dword ptr [eax + 00] or NOP word ptr [bx+si]
        break;
    case 5:
        if (assembler->Bitness != 16)
        {
            db_imm5(assembler, 0x0F, 0x1F, 0x44, 0x00, 0x00); // NOP dword ptr [eax + eax*1 + 00]
        }
        else
        {
            db_imm5(assembler, 0x0F, 0x1F, 0x80, 0x00, 0x00); // NOP word ptr[bx + si]
        }
        break;
    case 6:
        if (assembler->Bitness != 16)
        {
            db_imm6(assembler, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00); // 66 NOP dword ptr [eax + eax*1 + 00]
        }
        else
        {
            db_imm6(assembler, 0x66, 0x0F, 0x1F, 0x80, 0x00, 0x00); // NOP dword ptr [bx+si]
        }
        break;
    case 7:
        if (assembler->Bitness != 16)
        {
            db_imm7(assembler, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00); // NOP dword ptr [eax + 00000000]
        }
        else
        {
            db_imm7(assembler, 0x67, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00); // NOP dword ptr [eax+eax]
        }
        break;
    case 8:
        if (assembler->Bitness != 16)
        {
            db_imm8(assembler, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // NOP dword ptr [eax + eax*1 + 00000000]
        }
        else
        {
            db_imm8(assembler, 0x67, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00); // NOP word ptr [eax]
        }
        break;
    case 9:
        if (assembler->Bitness != 16)
        {
            db_imm9(assembler, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // 66 NOP dword ptr [eax + eax*1 + 00000000]
        }
        else
        {
            db_imm9(assembler, 0x67, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // NOP word ptr [eax+eax]
        }
        break;
    default:
        //throw new InvalidOperationException();
        break;
    }
}

/// <summary>
/// Generates multibyte NOP instructions
/// </summary>
/// <param name="sizeInBytes">Size in bytes of all nops</param>
void nop(struct Assembler* assembler, int sizeInBytes)
{
    if (sizeInBytes < 0)
    {
        //throw new ArgumentOutOfRangeException(nameof(sizeInBytes));
    }
    if (assembler->prefixFlags != PF_None)
    {
        //throw new InvalidOperationException("No prefixes are allowed");
    }
    if (sizeInBytes == 0)
    {
        return;
    }
    const int maxMultibyteNopInstructionLength = 9;

    //int cycles = Math.DivRem(sizeInBytes, maxMultibyteNopInstructionLength, out int rest);
    int cycles = sizeInBytes / maxMultibyteNopInstructionLength;
    int rest = sizeInBytes % maxMultibyteNopInstructionLength;

    for (int i = 0; i < cycles; i++)
    {
        AppendNop(assembler, maxMultibyteNopInstructionLength);
    }
    if (rest > 0)
    {
        AppendNop(assembler, rest);
    }
}

/// <summary>
/// Encodes instructions. Any number of branches can be part of this block.
/// You can use this function to move instructions from one location to another location.
/// If the target of a branch is too far away, it'll be rewritten to a longer branch.
/// You can disable this by passing in <see cref="BlockEncoderOptions.DontFixBranches"/>.
/// If the block has any <c>RIP</c>-relative memory operands, make sure the data isn't too
/// far away from the new location of the encoded instructions. Every OS should have
/// some API to allocate memory close (+/-2GB) to the original code location.
/// </summary>
/// <param name="bitness">16, 32 or 64</param>
/// <param name="blocks">All instructions</param>
/// <param name="errorMessage">Updated with an error message if the method failed</param>
/// <param name="result">Result if this method returns <see langword="true"/></param>
/// <param name="options">Encoder options</param>
/// <returns></returns>
//public static bool TryEncode(int bitness, InstructionBlock[] blocks, [NotNullWhen(false)] out string ? errorMessage, [NotNullWhen(true)] out BlockEncoderResult[] ? result, BlockEncoderOptions options = BlockEncoderOptions.None) = >
//new BlockEncoder(bitness, blocks, options).Encode(out errorMessage, out result);

//bool Encode(char** errorMessage, struct BlockEncoderResult** result) 
//{
//	const int MAX_ITERS = 5;
//	for (int iter = 0; iter < MAX_ITERS; iter++) {
//		bool updated = false;
//		foreach(var block in blocks) {
//			ulong ip = block.RIP;
//			ulong gained = 0;
//			foreach(var instr in block.Instructions) {
//				instr.IP = ip;
//				if (!instr.Done) {
//					var oldSize = instr.Size;
//					if (instr.Optimize(gained)) {
//						if (instr.Size > oldSize) {
//							errorMessage = "Internal error: new size > old size";
//							result = null;
//							return false;
//						}
//						if (instr.Size < oldSize) {
//							gained += oldSize - instr.Size;
//							updated = true;
//						}
//					}
//					else if (instr.Size != oldSize) {
//						errorMessage = "Internal error: new size != old size";
//						result = null;
//						return false;
//					}
//				}
//				ip += instr.Size;
//			}
//		}
//		if (!updated)
//			break;
//	}
//
//	foreach(var block in blocks)
//		block.InitializeData();
//
//	var resultArray = new BlockEncoderResult[blocks.Length];
//	for (int i = 0; i < blocks.Length; i++) {
//		var block = blocks[i];
//		var encoder = Encoder.Create(bitness, block.CodeWriter);
//		ulong ip = block.RIP;
//		var newInstructionOffsets = ReturnNewInstructionOffsets ? new uint[block.Instructions.Length] : null;
//		var constantOffsets = ReturnConstantOffsets ? new ConstantOffsets[block.Instructions.Length] : null;
//		var instructions = block.Instructions;
//		for (int j = 0; j < instructions.Length; j++) {
//			var instr = instructions[j];
//			uint bytesWritten = block.CodeWriter.BytesWritten;
//			bool isOriginalInstruction;
//			if (constantOffsets is not null)
//				errorMessage = instr.TryEncode(encoder, out constantOffsets[j], out isOriginalInstruction);
//			else
//				errorMessage = instr.TryEncode(encoder, out _, out isOriginalInstruction);
//			if (errorMessage is not null) {
//				result = null;
//				return false;
//			}
//			uint size = block.CodeWriter.BytesWritten - bytesWritten;
//			if (size != instr.Size) {
//				errorMessage = "Internal error: didn't write all bytes";
//				result = null;
//				return false;
//			}
//			if (newInstructionOffsets is not null) {
//				if (isOriginalInstruction)
//					newInstructionOffsets[j] = (uint)(ip - block.RIP);
//				else
//					newInstructionOffsets[j] = uint.MaxValue;
//			}
//			ip += size;
//		}
//		resultArray[i] = new BlockEncoderResult(block.RIP, block.relocInfos, newInstructionOffsets, constantOffsets);
//		block.WriteData();
//	}
//
//	errorMessage = null;
//	result = resultArray;
//	return true;
//}

///// <summary>
///// Tries to assemble the instructions of this assembler with the specified options.
///// </summary>
///// <param name="writer">The code writer.</param>
///// <param name="rip">Base address.</param>
///// <param name="errorMessage">Error messages.</param>
///// <param name="assemblerResult">The assembler result if successful.</param>
///// <param name="options">Encoding options.</param>
///// <returns><c>true</c> if the encoding was successful; <c>false</c> otherwise.</returns>
//bool TryAssemble(struct Assembler* c, struct CodeWriter* writer, unsigned long rip, struct AssemblerResult* assemblerResult, enum BlockEncoderOptions options) // = BlockEncoderOptions.None
//{
//	if (writer is null)
//	{
//		//ThrowHelper.ThrowArgumentNullException_writer();
//	}
//
//	assemblerResult = default;
//
//	// Protect against using a prefix without actually using it
//	if (c->prefixFlags != PF_None) 
//	{
//		errorMessage = LogCritical("Unused prefixes {prefixFlags}. You must emit an instruction after using an instruction prefix.";
//		return false;
//	}
//
//	// Protect against a label emitted without being attached to an instruction
//	if (!currentLabel.IsEmpty) 
//	{
//		LogCritical("Unused label {currentLabel}. You must emit an instruction after emitting a label.");
//		return false;
//	}
//
//	if (definedAnonLabel) 
//	{
//		LogCritical("Unused anonymous label. You must emit an instruction after emitting a label.");
//		return false;
//	}
//
//	if (!nextAnonLabel.IsEmpty) 
//	{
//		LogCritical("Found an @F anonymous label reference but there was no call to "); // +nameof(AnonymousLabel);
//		return false;
//	}
//
//	if (BlockEncoder.TryEncode(writer, instructions, rip, Bitness, blocks, out var blockResults, options)) 
//	{
//		assemblerResult = new AssemblerResult(blockResults);
//		return true;
//	}
//	else 
//	{
//		assemblerResult = new AssemblerResult(Array2.Empty<BlockEncoderResult>());
//		return false;
//	}
//}

///// <summary>
///// Assembles the instructions of this assembler with the specified options.
///// </summary>
///// <param name="writer">The code writer.</param>
///// <param name="rip">Base address.</param>
///// <param name="options">Encoding options.</param>
///// <returns></returns>
///// <exception cref="InvalidOperationException"></exception>
//struct AssemblerResult* Assemble(struct Assembler* c, struct CodeWriter* writer, unsigned long rip, enum BlockEncoderOptions options)
//{ // = BlockEncoderOptions.None
//	char* errorMessage;
//	struct AssemblerResult* assemblerResult;
//
//	if (!TryAssemble(c, writer, rip, &errorMessage, &assemblerResult, options))
//	{
//		//LogCritical(errorMessage);
//		// throw new InvalidOperationException(errorMessage);
//	}
//
//	return assemblerResult;
//}

///// <summary>
///// Internal method used to throw an InvalidOperationException if it was not possible to encode an OpCode.
///// </summary>
///// <param name="mnemonic">The mnemonic of the instruction</param>
///// <param name="argNames">The argument values.</param>
///// <returns></returns>
//InvalidOperationException NoOpCodeFoundFor(Mnemonic mnemonic, params object[] argNames) 
//{
//	var builder = new StringBuilder();
//	builder.Append($"Unable to calculate an OpCode for `{mnemonic.ToString().ToLowerInvariant()}");
//	for (int i = 0; i < argNames.Length; i++) {
//		builder.Append(i == 0 ? " " : ", ");
//		builder.Append(argNames[i]); // TODO: add pretty print for arguments (registers, memory...)
//	}
//
//	builder.Append($"`. Combination of arguments and/or current bitness {Bitness} is not compatible with any existing OpCode encoding.");
//	return new InvalidOperationException(builder.ToString());
//}

/// <summary>
/// Writes the next byte
/// </summary>
/// <param name="value">Value</param>
//void WriteByte(unsigned char* buffer, unsigned char value)
//{
//
//}

/// <summary>mov instruction.<br/>
/// <br/>
/// <c>MOV RAX, moffs64</c><br/>
/// <br/>
/// <c>o64 A1 mo</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>MOV r64, r/m64</c><br/>
/// <br/>
/// <c>o64 8B /r</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c></summary>
struct Instruction* mov64(enum Register dst, struct AssemblerMemoryOperand* src, int Bitness) // AssemblerRegister64 dst, AssemblerMemoryOperand src
{
    enum Code code;
    struct Instruction* inst;

    if (dst == Register_RAX && Bitness == 64 && IsDisplacementOnly(src))
    {
        code = Mov_RAX_moffs64;
    }
    else
    {
        if (dst == Register_RAX && Bitness < 64 && IsDisplacementOnly(src))
        {
            code = Mov_RAX_moffs64;
        }
        else
        {
            code = Mov_r64_rm64;
        }
    }

    inst = Instruction_Create2Mem(code, dst, ToMemoryOperand(src, Bitness));
    //AddInstruction(inst);
    return inst;
}

/// <summary>cmp instruction.<br/>
/// <br/>
/// <c>CMP r/m64, imm32</c><br/>
/// <br/>
/// <c>o64 81 /7 id</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>CMP r/m64, imm8</c><br/>
/// <br/>
/// <c>o64 83 /7 ib</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>CMP r/m32, imm32</c><br/>
/// <br/>
/// <c>o32 81 /7 id</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>CMP r/m32, imm8</c><br/>
/// <br/>
/// <c>o32 83 /7 ib</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>CMP r/m16, imm16</c><br/>
/// <br/>
/// <c>o16 81 /7 iw</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>CMP r/m16, imm8</c><br/>
/// <br/>
/// <c>o16 83 /7 ib</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>CMP r/m8, imm8</c><br/>
/// <br/>
/// <c>80 /7 ib</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c></summary>
struct Instruction* cmp(struct Assembler* assembler, struct AssemblerMemoryOperand* dst, int imm)
{
    struct Instruction* inst = NULL;
    enum Code code;
    if (dst->Size == MOS_Qword)
    {
        code = imm >= SCHAR_MIN && imm <= SCHAR_MAX ? Cmp_rm64_imm8 : Cmp_rm64_imm32;
    }
    else if (dst->Size == MOS_Dword)
    {
        code = imm >= SCHAR_MIN && imm <= SCHAR_MAX ? Cmp_rm32_imm8 : Cmp_rm32_imm32;
    }
    else if (dst->Size == MOS_Word)
    {
        code = imm >= SCHAR_MIN && imm <= SCHAR_MAX ? Cmp_rm16_imm8 : Cmp_rm16_imm16;
    }
    else if (dst->Size == MOS_Byte)
    {
        code = Cmp_rm8_imm8;
    }
    else
    {
        //throw NoOpCodeFoundFor(Mnemonic.Cmp, dst, imm);
    }
    //AddInstruction(assembler, Instruction_Create2Mem1Imm(code, ToMemoryOperand(dst, assembler->Bitness), imm));
    inst = Instruction_Create2Mem1Imm(code, ToMemoryOperand(dst, assembler->Bitness), imm);
    AddInstruction(assembler, inst);
    return inst;
}

/// <summary>jne instruction.<br/>
/// <br/>
/// <c>JNE rel8</c><br/>
/// <br/>
/// <c>o64 75 cb</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>JNE rel32</c><br/>
/// <br/>
/// <c>o64 0F 85 cd</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>JNE rel8</c><br/>
/// <br/>
/// <c>o32 75 cb</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32-bit</c><br/>
/// <br/>
/// <c>JNE rel32</c><br/>
/// <br/>
/// <c>o32 0F 85 cd</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32-bit</c><br/>
/// <br/>
/// <c>JNE rel8</c><br/>
/// <br/>
/// <c>o16 75 cb</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>JNE rel16</c><br/>
/// <br/>
/// <c>o16 0F 85 cw</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c></summary>
struct Instruction* jne(struct Label* dst, bool PreferShortBranch, int Bitness)
{
    struct Instruction* inst;
    enum Code code;
    if (PreferShortBranch)
    {
        if (Bitness == 64)
        {
            code = Jne_rel8_64;
        }
        else code = Bitness >= 32 ? Jne_rel8_32 : Jne_rel8_16;
    }
    else if (Bitness == 64)
    {
        code = Jne_rel32_64;
    }
    else code = Bitness >= 32 ? Jne_rel32_32 : Jne_rel16;
    inst = Instruction_CreateBranch(code, dst->id);
    //AddInstruction(inst);
    return inst;
}

/// <summary>add instruction.<br/>
/// <br/>
/// <c>ADD r/m64, imm32</c><br/>
/// <br/>
/// <c>o64 81 /0 id</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>ADD r/m64, imm8</c><br/>
/// <br/>
/// <c>o64 83 /0 ib</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>ADD r/m32, imm32</c><br/>
/// <br/>
/// <c>o32 81 /0 id</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>ADD r/m32, imm8</c><br/>
/// <br/>
/// <c>o32 83 /0 ib</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>ADD r/m16, imm16</c><br/>
/// <br/>
/// <c>o16 81 /0 iw</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>ADD r/m16, imm8</c><br/>
/// <br/>
/// <c>o16 83 /0 ib</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>ADD r/m8, imm8</c><br/>
/// <br/>
/// <c>80 /0 ib</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c></summary>
struct Instruction* add(struct AssemblerMemoryOperand* dst, int imm, int Bitness)
{
    struct Instruction* inst;
    enum Code code;
    if (dst->Size == MOS_Qword)
    {
        code = imm >= SCHAR_MIN && imm <= SCHAR_MAX ? Add_rm64_imm8 : Add_rm64_imm32;
    }
    else if (dst->Size == MOS_Dword)
    {
        code = imm >= SCHAR_MIN && imm <= SCHAR_MAX ? Add_rm32_imm8 : Add_rm32_imm32;
    }
    else if (dst->Size == MOS_Word)
    {
        code = imm >= SCHAR_MIN && imm <= SCHAR_MAX ? Add_rm16_imm8 : Add_rm16_imm16;
    }
    else if (dst->Size == MOS_Byte)
    {
        code = Add_rm8_imm8;
    }
    else {
        //throw NoOpCodeFoundFor(Mnemonic.Add, dst, imm);
    }
    inst = Instruction_Create2Mem1Imm(code, ToMemoryOperand(dst, Bitness), imm);
    //AddInstruction();
    return inst;
}

/// <summary>vaddpd instruction.<br/>
/// <br/>
/// <c>VADDPD zmm1 {k1}{z}, zmm2, zmm3/m512/m64bcst{er}</c><br/>
/// <br/>
/// <c>EVEX.512.66.0F.W1 58 /r</c><br/>
/// <br/>
/// <c>AVX512F</c><br/>
/// <br/>
/// <c>16/32/64-bit</c></summary>
struct Instruction* vaddpd(struct Assembler* assembler,
    enum Register zmm_dst,
    enum AssemblerOperandFlags dst_flags,
    enum Register zmm_src1,
    enum Register zmm_src2,
    enum AssemblerOperandFlags src2_flags)
{
    struct Instruction* inst = NULL;
    inst = Instruction_Create3Reg(EVEX_Vaddpd_zmm_k1z_zmm_zmmm512b64_er, zmm_dst, zmm_src1, zmm_src2);

    AddInstructionWithFlags(assembler, inst, dst_flags | src2_flags);

    return inst;
}

/// <summary>vunpcklps instruction.<br/>
/// <br/>
/// <c>VUNPCKLPS xmm1, xmm2, xmm3/m128</c><br/>
/// <br/>
/// <c>VEX.128.0F.WIG 14 /r</c><br/>
/// <br/>
/// <c>AVX</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>VUNPCKLPS xmm1 {k1}{z}, xmm2, xmm3/m128/m32bcst</c><br/>
/// <br/>
/// <c>EVEX.128.0F.W0 14 /r</c><br/>
/// <br/>
/// <c>AVX512VL and AVX512F</c><br/>
/// <br/>
/// <c>16/32/64-bit</c></summary>
struct Instruction* vunpcklps(
    struct Assembler* assembler,
    enum Register xmm_dst,
    enum AssemblerOperandFlags xmm_dst_flags,
    enum Register xmm_src1,
    struct AssemblerMemoryOperand* src2,
    enum AssemblerOperandFlags src2_flags,
    int Bitness)
{
    struct Instruction* inst;
    enum Code code;

    if (IsBroadcast(src2))
    {
        code = EVEX_Vunpcklps_xmm_k1z_xmm_xmmm128b32;
    }
    else
    {
        if (InstructionPreferVex(assembler))
        {
            code = VEX_Vunpcklps_xmm_xmm_xmmm128;
        }
        else
        {
            code = EVEX_Vunpcklps_xmm_k1z_xmm_xmmm128b32;
        }
    }
    inst = Instruction_Create(code, xmm_dst, xmm_src1, ToMemoryOperand(src2, Bitness));
    AddInstructionWithFlags(assembler, inst, xmm_dst_flags | src2_flags);
}

/// <summary>inc instruction.<br/>
/// <br/>
/// <c>INC r/m64</c><br/>
/// <br/>
/// <c>o64 FF /0</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c></summary>
struct Instruction* inc(enum Register dst)
{
    return Instruction_Create1(Inc_rm64, dst);
}


/// <summary>je instruction.<br/>
/// <br/>
/// <c>JE rel8</c><br/>
/// <br/>
/// <c>o64 74 cb</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>JE rel32</c><br/>
/// <br/>
/// <c>o64 0F 84 cd</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>JE rel8</c><br/>
/// <br/>
/// <c>o32 74 cb</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32-bit</c><br/>
/// <br/>
/// <c>JE rel32</c><br/>
/// <br/>
/// <c>o32 0F 84 cd</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32-bit</c><br/>
/// <br/>
/// <c>JE rel8</c><br/>
/// <br/>
/// <c>o16 74 cb</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>JE rel16</c><br/>
/// <br/>
/// <c>o16 0F 84 cw</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c></summary>
void je(struct Assembler* assembler, struct Label* dst)
{
    enum Code code;
    if (assembler->PreferShortBranch)
    {
        if (assembler->Bitness == 64)
        {
            code = Je_rel8_64;
        }
        else
        {
            if (assembler->Bitness >= 32)
            {
                code = Je_rel8_32;
            }
            else
            {
                code = Je_rel8_16;
            }
        }
    }
    else if (assembler->Bitness == 64)
    {
        code = Je_rel32_64;
    }
    else
    {
        if (assembler->Bitness >= 32)
        {
            code = Je_rel32_32;
        }
        else
        {
            code = Je_rel16;
        }
    }
    AddInstruction(assembler, Instruction_CreateBranch(code, dst->id));
}

/// <summary>nop instruction.<br/>
/// <br/>
/// <c>NOP</c><br/>
/// <br/>
/// <c>o32 90</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c><br/>
/// <br/>
/// <c>NOP</c><br/>
/// <br/>
/// <c>o16 90</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c></summary>
void nop2(struct Assembler* assembler)
{
    enum Code code;
    if (assembler->Bitness >= 32)
    {
        code = Nopd;
    }
    else
    {
        code = Nopw;
    }
    AddInstruction(assembler, Instruction_CreateNoOperands(code));
}

/// <summary>zero_bytes instruction.<br/>
/// <br/>
/// A zero-sized instruction. Can be used as a label.</summary>
void zero_bytes(struct Assembler* assembler)
{
    AddInstruction(assembler, Instruction_CreateNoOperands(Zero_bytes));
}

/// <summary>pop instruction.<br/>
/// <br/>
/// <c>POP r64</c><br/>
/// <br/>
/// <c>o64 58+ro</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c></summary>
void pop(struct Assembler* assembler, enum Register dst)
{
    AddInstruction(assembler, Instruction_Create1(Pop_r64, dst));
}

/// <summary>ret instruction.<br/>
/// <br/>
/// <c>RET</c><br/>
/// <br/>
/// <c>o64 C3</c><br/>
/// <br/>
/// <c>X64</c><br/>
/// <br/>
/// <c>64-bit</c><br/>
/// <br/>
/// <c>RET</c><br/>
/// <br/>
/// <c>o32 C3</c><br/>
/// <br/>
/// <c>386+</c><br/>
/// <br/>
/// <c>16/32-bit</c><br/>
/// <br/>
/// <c>RET</c><br/>
/// <br/>
/// <c>o16 C3</c><br/>
/// <br/>
/// <c>8086+</c><br/>
/// <br/>
/// <c>16/32/64-bit</c></summary>
void ret(struct Assembler* assembler)
{
    enum Code code;
    if (assembler->Bitness == 64)
    {
        code = Retnq;
    }
    else
    {
        if (assembler->Bitness >= 32)
        {
            code = Retnd;
        }
        else
        {
            code = Retnw;
        }
    }
    AddInstruction(assembler, Instruction_CreateNoOperands(code));
}

/// <summary>
/// Assemble the program image which is generated by the assembler into a stream of bytes.
/// </summary>
/// <param name="assembler">
/// The assembler instance.
/// </param>
/// <param name="RIP_program_start_addr">
/// The start address of the program where all operations begin executing at the specified RIP address.
/// </param>
/// <returns>
/// A stream of bytes of the effective machine code generated by the assembler.
/// </returns>
unsigned char* squash_assemble(struct Assembler* assembler, unsigned long RIP_program_start_addr, int* encoder_length)
{
    unsigned char* sq_program_image = NULL;
    struct Encoder* encoder;

    //TODO:

    list_t* instructions_list = assembler->instructions;
    list_t* stream_bytes;
    list_t* n;
    struct Instruction* inst;
    int encoded_length;
    unsigned char* stream_byte;
    long stream_length = 0;
    long stream_index;

    encoder = Create(assembler, assembler->Bitness);

    n = instructions_list;
    while (n != NULL)
    {
        inst = (struct Instruction*)n->data;

        if (inst != NULL)
        {
            encoded_length = Encode(encoder, inst, RIP_program_start_addr);
        }

        n = n->next;
    }

    stream_bytes = assembler->stream_bytes;
    n = stream_bytes;
    stream_length = 0;

    // Get the size of the stream in bytes.
    while (n != NULL)
    {
        stream_length++;
        n = n->next;
    }

    // Convert linked-list of bytes to a byte buffer.
    n = stream_bytes;
    stream_index = 0;
    *encoder_length = stream_length;
    sq_program_image = (unsigned char*)malloc(sizeof(unsigned char) * stream_length + 1);

    for (int a = 0; a < stream_length + 1; a++)
    {
        sq_program_image[a] = 0x00;
    }

    while (n != NULL)
    {
        stream_byte = (unsigned char*)n->data;

        if (sq_program_image != NULL)
        {
            *(sq_program_image + stream_index) = *stream_byte;
        }

        stream_index++;
        n = n->next;
    }

    return sq_program_image;
}

void test_assembler()
{
    const unsigned long RIP = 0x1234567810000000;
    int Bitness = 64;
    // The assembler supports all modes: 16-bit, 32-bit and 64-bit.
    struct Assembler* c = assembler_new();
    assembler(c, Bitness);

    struct Label* label1 = create_label(c, "label1");
    struct Label* data1 = create_label(c, "data1");

    struct Instruction* inst;

    //inst = Instruction.Create(Code.Sub_rm64_imm32, rsp, 0x20); // Requires more size in encoder so an optimiser might select Code.Sub_rm64_imm8 path to favor min size.
    inst = Instruction_Create2(Sub_rm64_imm8, Register_RSP, 0x20);
    AddInstruction(c, inst);

    inst = Instruction_Create1(Push_r64, Register_R15); // c.push(r15);
    AddInstruction(c, inst);
    inst = Instruction_Create2Reg(Add_rm64_r64, Register_RAX, Register_R15); //c.add(rax, r15);
    AddInstruction(c, inst);

    // If the memory operand can only have one size, __[] can be used. The assembler ignores
    // the memory size unless it's an ambiguous instruction, eg. 'add [mem],123'
    inst = mov64(Register_RAX, ToMemoryOperandFromRegister(Register_RAX), Bitness);  // c.mov(rax, __[rax]);
    AddInstruction(c, inst);

    struct AssemblerMemoryOperand* qword_operand2;
    qword_operand2 = AssemblerMemoryOperand_new(MOS_Qword, Register_None, Register_RAX, Register_None, 1, 0, AF_None);
    inst = mov64(Register_RAX, qword_operand2, Bitness); //inst = mov(rax, __qword_ptr[rax], Bitness); // c.mov(rax, __qword_ptr[rax]);
    AddInstruction(c, inst);

    // The assembler must know the memory size to pick the correct instruction
    // (Base + Index * Scale + Displacement)
    qword_operand2 = AssemblerMemoryOperand_new(MOS_Dword, Register_None, Register_RAX, Register_RCX, 8, 0x10, AF_None);
    inst = cmp(c, qword_operand2, -1); // c.cmp(__dword_ptr[rax + rcx * 8 + 0x10], -1);
    AddInstruction(c, inst);

    inst = jne(label1, c->PreferShortBranch, Bitness); // c.jne(label1); // Jump to Label1
    AddInstruction(c, inst);

    inst = Instruction_Create1(Inc_rm64, Register_RAX); // c.inc(rax);
    AddInstruction(c, inst);

    // Labels can be referenced by memory operands (64-bit only) and call/jmp/jcc/loopcc instructions
    inst = Instruction_Create2Mem(Lea_r64_m, Register_RCX, ToMemoryOperand(ToMemoryOperandFromLabel(data1), Bitness)); // c.lea(rcx, __[data1]);
    AddInstruction(c, inst);

    // The assembler has prefix properties that will be added to the following instruction
    c->prefixFlags |= PF_Repe; // c.rep
    inst = Instruction_CreateStosd(Bitness, RPK_None); //c.rep.stosd();
    AddInstruction(c, inst);

    //c.xacquire.@lock.add(__qword_ptr[rax + rcx], 123); // f0f24883487b = xacquire lock add qword ptr [rax+rcx],7Bh
    c->prefixFlags |= PF_Repne; // apply xacquire to flags
    c->prefixFlags |= PF_Lock; // apply @lock
    //c.add(__qword_ptr[rax + rcx], 123);

    struct AssemblerMemoryOperand* qword_operand;
    qword_operand = AssemblerMemoryOperand_new(MOS_Qword, Register_None, Register_RAX, Register_RCX, 1, 0, AF_None);
    inst = add(qword_operand, 123, Bitness);
    AddInstruction(c, inst);

    // The assembler defaults to VEX instructions. If you need EVEX instructions, set PreferVex=false
    c->PreferVex = false;
    // or call `c.vex` or `c.evex` prefixes to override the default encoding.
    // AVX-512 decorators are properties on the memory and register operands
    //vaddpd(c, zmm1.k3.z, zmm2, zmm3.rz_sae);

    enum AssemblerOperandFlags dst_flags_zmm1_k3_z = AF_Zeroing;
    enum AssemblerOperandFlags src2_flags_zmm3_rz_sae = AF_SuppressAllExceptions;

    dst_flags_zmm1_k3_z = (dst_flags_zmm1_k3_z & ~AF_RegisterMask) | AF_K3;
    src2_flags_zmm3_rz_sae = (src2_flags_zmm3_rz_sae & ~AF_RoundingControlMask) | AF_RoundTowardZero;

    vaddpd(c, Register_ZMM1, dst_flags_zmm1_k3_z, Register_ZMM2, Register_ZMM3, src2_flags_zmm3_rz_sae);



    // To broadcast memory, use the __dword_bcst/__qword_bcst memory types
    //c.vunpcklps(xmm2.k5.z, xmm6, __dword_bcst[rax]);

    struct AssemblerMemoryOperand* dword_bcst_operand;
    dword_bcst_operand = AssemblerMemoryOperand_new(MOS_Dword, Register_None, Register_RAX, Register_None, 1, 0, AF_Broadcast);
    enum AssemblerOperandFlags dst_flags_xmm2_k5_z = AF_Zeroing;
    enum AssemblerOperandFlags src2_flags = AF_Broadcast;

    dst_flags_xmm2_k5_z = (dst_flags_xmm2_k5_z & ~AF_RegisterMask) | AF_K5;

    //c.vunpcklps(xmm2.k5.z, xmm6, dword_operand);
    vunpcklps(c, Register_XMM2, dst_flags_xmm2_k5_z, Register_XMM6, dword_bcst_operand, src2_flags, Bitness);

    // You can create anonymous labels, just like in eg. masm, @@, @F and @B
    anonymous_label(c); // same as @@: in masm

    inst = inc(Register_RAX);
    AddInstruction(c, inst);

    je(c, B(c)); // reference the previous anonymous label

    inst = inc(Register_RCX);
    AddInstruction(c, inst);

    je(c, F(c)); // reference the next anonymous label

    nop2(c);
    anonymous_label(c);
    nop2(c);

    // Emit label1:
    label(c, label1);
    // If needed, a zero-bytes instruction can be used as a label but this is optional
    zero_bytes(c);
    pop(c, Register_R15);
    ret(c);
    label(c, data1);
    db_imm2(c, 0xF3, 0x90); // pause

    int encoded_length;
    unsigned char* sq_program_image = squash_assemble(c, RIP, &encoded_length);
    //Assemble(c, new StreamCodeWriter(stream), RIP);

    // Print out program buffer machine code:
    printf("Machine code by assembler:\n");
    //printf("\n\n");
    for (int buffer_index = 0; buffer_index < encoded_length; buffer_index++)
    {
        unsigned char* p_byte = (sq_program_image + buffer_index);
        unsigned char byte_value = *p_byte;

        //printf("%02X ", (unsigned int)byte_value);

        printf("%02X ", (unsigned int)sq_program_image[buffer_index]);
    }

    //printf("\n");
}

//void asm_encode_instruction(unsigned char* mnumonic, unsigned char* register_name, unsigned char* operands)
//{
//   
//
//}

//void asm_parse_instruction(char* carr_line)
//{
//    char* token;
//    const char* delimiter = ", ";
//
//    token = strtok(carr_line, delimiter);
//    printf("%s\n", token);
//
//    while (token != NULL)
//    {
//        printf("%s\n", token);
//        token = strtok(NULL, delimiter);
//    }
//
//    unsigned char* mnumonic = "SUB";
//    unsigned char* operands = "RSP, 0x28"; // right operand is of type 'O_IMM' because it is a constant value obtained immediately after the register.
//    unsigned char* register_name = "RSP"; // left operand is of type 'O_REG'
//    
//    asm_encode_instruction(mnumonic, register_name, operands); // should encode to the following hex sequence: 4883ec28
//}

//void asm_parse_line(char* carr_line)
//{
//    if (strcmp(carr_line, "section .text") == 0)
//    {
//
//    }
//    else if (strcmp(carr_line, "global main") == 0)
//    {
//
//    }
//    else
//    {
//        // instruction.
//        asm_parse_instruction(carr_line);
//    }
//}

//void squash_assembler(struct Assembler* assembler, char* source_asm, int source_size, char* output_binary_file_name)
//{
//    int index;
//    char currentChar = '\0';
//    index = 0;
//    StringBuilder* sb = sb_create();
//    char* carr_line;
//    char* mem = (char*)malloc(sizeof(char) * 2);
//
//    while (index < source_size)
//    {
//        currentChar = (source_asm + index)[0];
//
//        if (currentChar == '\n' || currentChar == '\r')
//        {
//            carr_line = sb_concat(sb);
//            
//            asm_parse_line(carr_line);
//
//            sb_reset(sb);
//        }
//        else
//        {
//            mem[0] = currentChar;
//            mem[1] = '\0';
//
//            sb_append(sb, mem);
//        }
//        index++;
//    }
//
//    LogInformation("*-*-*-*-*- output assemble portable executable (.exe) created '%s' -*-*-*-*-*", output_binary_file_name);
//}