#include "Assembler.h"
#include "Instruction.Create.h"

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

        test_assembler();

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
    unsigned char stream_byte;
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
    sq_program_image = (unsigned char*)malloc(sizeof(unsigned char) * stream_length);

    while (n != NULL)
    {
        stream_byte = *(unsigned char*)n->data;

        if (sq_program_image != NULL)
        {
            sq_program_image[stream_index] = stream_byte;
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
    struct Assembler* c = (struct Assembler*)malloc(sizeof(struct Assembler));
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
    inst = cmp(qword_operand2, -1, Bitness); // c.cmp(__dword_ptr[rax + rcx * 8 + 0x10], -1);
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
    vunpcklps(assembler, Register_XMM2, dst_flags_xmm2_k5_z, Register_XMM6, dword_bcst_operand, src2_flags, Bitness);

    // You can create anonymous labels, just like in eg. masm, @@, @F and @B
    anonymous_label(c); // same as @@: in masm

    inst = inc(Register_RAX);
    AddInstruction(assembler, inst);

    je(c, B(c)); // reference the previous anonymous label

    inst = inc(Register_RCX);
    AddInstruction(assembler, inst);

    je(c, F(c)); // reference the next anonymous label

    nop2(c);
    anonymous_label(c);
    nop2(c);

    // Emit label1:
    label(c, &label1);
    // If needed, a zero-bytes instruction can be used as a label but this is optional
    zero_bytes(c);
    pop(c, Register_R15);
    ret(c);
    label(c, &data1);
    db_imm2(c, 0xF3, 0x90); // pause

    int encoded_length;
    unsigned char* sq_program_image = squash_assemble(c, RIP, &encoded_length);
    //Assemble(c, new StreamCodeWriter(stream), RIP);
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