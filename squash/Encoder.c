#include "Encoder.h"

/// <summary>
/// Creates a new instance of this assembler
/// </summary>
/// <param name="bitness">The assembler instruction set bitness, either 16, 32 or 64 bit.</param>
void assembler(struct Assembler* assembler, int bitness)
{
	switch (bitness)
	{
	case 16:
	case 32:
	case 64:
		break;
	default:
		//throw new ArgumentOutOfRangeException(nameof(bitness));
		LogCritical("Bitness is out of range expected to be: 16, 32, or 64.");
	}
	assembler->Bitness = bitness;
	assembler->instructions = (list_t*)malloc(sizeof(list_t));
	assembler->currentLabelId = 0;
	assembler->currentLabel = NULL;
	assembler->currentAnonLabel = NULL;
	assembler->nextAnonLabel = NULL;
	assembler->definedAnonLabel = false;
	assembler->prefixFlags = PF_None;
	assembler->PreferVex = true;
	assembler->PreferShortBranch = true;
}

bool InstructionPreferVex(struct Assembler* assembler)
{
	if ((assembler->prefixFlags & (PF_PreferVex | PF_PreferEvex)) != 0)
	{
		return (assembler->prefixFlags & PF_PreferVex) != 0;
	}
	return assembler->PreferVex;
}

/// <summary>
/// Reset the current set of instructions and labels added to this instance.
/// </summary>
void Reset(struct Assembler* assembler)
{
	if (assembler->instructions != NULL)
	{
		free(assembler->instructions);
	}

	assembler->instructions = (list_t*)malloc(sizeof(list_t));//instructions.Clear();
	assembler->currentLabelId = 0;
	assembler->currentLabel = NULL;
	assembler->currentAnonLabel = NULL;
	assembler->nextAnonLabel = NULL;
	assembler->definedAnonLabel = false;
	assembler->prefixFlags = PF_None;
}

int instructions_count(struct Assembler* assembler)
{
	int count = 0;
	list_t* n = assembler->instructions;

	while (n != NULL)
	{
		n = n->next;
		count++;
	}
	return count;
}

/// <summary>
/// Creates a label.
/// </summary>
/// <param name="name">Optional name of the label.</param>
/// <returns></returns>
struct Label* create_label(struct Assembler* assembler, char* name)
{
	assembler->currentLabelId++;
	struct Label* label = (struct Label*)malloc(sizeof(struct Label));
	label->name = name;
	label->id = assembler->currentLabelId;
	return label;
}

/// <summary>
/// Use the specified label.
/// </summary>
/// <param name="label">Label to use</param>
/// <exception cref="ArgumentException"></exception>
void label(struct Assembler* assembler, struct Label* label)
{
	if (label == NULL)
	{
		LogCritical("Invalid label. Must be created via create_label()");
		return;
	}
	if (label->instruction_index >= 0)
	{
		LogCritical("Cannot reuse label. The specified label is already associated with an instruction at index {label.InstructionIndex}.");
		return;
	}
	if (assembler->currentLabel != NULL)
	{
		//LogCritical("At most one label per instruction is allowed");
		//return;
	}
	label->instruction_index = instructions_count(assembler);
	assembler->currentLabel = label;
}

/// <summary>
/// Creates an anonymous label that can be referenced by using <see cref="B"/> (backward anonymous label)
/// and <see cref="F"/> (forward anonymous label).
/// </summary>
void anonymous_label(struct Assembler* assembler)
{
	if (assembler->definedAnonLabel)
	{
		LogCritical("At most one anonymous label per instruction is allowed.");

		//throw new InvalidOperationException("At most one anonymous label per instruction is allowed");
	}
	if (assembler->nextAnonLabel == NULL) // assembler->nextAnonLabel.IsEmpty
	{
		assembler->currentAnonLabel = CreateLabel();
	}
	else 
	{
		assembler->currentAnonLabel = assembler->nextAnonLabel;
	}
	assembler->nextAnonLabel = NULL; // default
	assembler->definedAnonLabel = true;
}

/// <summary>
/// References the previous anonymous label created by <see cref="AnonymousLabel"/>
/// </summary>
struct Label* B(struct Assembler* assembler) // @B
{
	if (assembler->currentAnonLabel == NULL)
	{
		LogCritical("No anonymous label has been created yet");
	}
	return assembler->currentAnonLabel;
}

/// <summary>
/// References the next anonymous label created by a future call to <see cref="AnonymousLabel"/>
/// </summary>
struct Label* F(struct Assembler* assembler) // @F
{
	if (assembler->nextAnonLabel == NULL)
	{
		assembler->nextAnonLabel = CreateLabel();
	}
	return assembler->nextAnonLabel;
}

/// <summary>
/// Add an instruction directly to the flow of instructions.
/// </summary>
/// <param name="instruction"></param>
void AddInstruction(struct Assembler* assembler, struct Instruction* instruction)
{
	if (assembler->currentLabel != NULL && assembler->definedAnonLabel)
	{
		LogCritical("You can't create both an anonymous label and a normal label.");
		return;
		// throw new InvalidOperationException("You can't create both an anonymous label and a normal label");
	}
		
	if (assembler->currentLabel != NULL)
	{
		SetIP(assembler, assembler->currentLabel->id);
	}
	else if (assembler->definedAnonLabel)
	{
		SetIP(assembler, assembler->currentAnonLabel->id);
	}

	// Setup prefixes
	if (assembler->prefixFlags != PF_None)
	{
		if ((assembler->prefixFlags & PF_Lock) != 0)
		{
			Set_HasLockPrefix(instruction, true);
		}
		if ((assembler->prefixFlags & PF_Repe) != 0)
		{
			Set_HasRepePrefix(instruction, true);
		}
		else if ((assembler->prefixFlags & PF_Repne) != 0)
		{
			Set_HasRepnePrefix(instruction, true);
		}
		if ((assembler->prefixFlags & PF_Notrack) != 0)
		{
			Set_SegmentPrefix(instruction, Register_DS);
		}
	}

	list_enqueue(assembler->instructions, (void*)instruction); // instructions.Add(instruction);
	assembler->currentLabel = NULL; // default
	assembler->definedAnonLabel = false;
	assembler->prefixFlags = PF_None;
}

/// <summary>
/// Add an instruction directly to the flow of instructions.
/// </summary>
/// <param name="instruction"></param>
/// <param name="flags">Operand flags passed.</param>
void AddInstruction(struct Assembler* assembler, struct Instruction* instruction, enum AssemblerOperandFlags flags)
{
	if (flags != AF_None)
	{
		if ((flags & AF_Broadcast) != 0)
		{
			IsBroadcast(instruction, true);
		}
		if ((flags & AF_Zeroing) != 0)
		{
			SetZeroingMasking(instruction, true);
		}
		
		if ((flags & AF_RegisterMask) != 0)
		{
			// register mask is shift by 2 (starts at index 1 for K1)
			enum Register reg = (enum Register)((int)Register_K0 + (((int)(flags & AF_RegisterMask)) >> 6));

			SetOpMask(instruction, reg);
		}
		if ((flags & AF_SuppressAllExceptions) != 0) 
		{
			SetSuppressAllExceptions(instruction, true);
		}
		if ((flags & AF_RoundingControlMask) != 0)
		{
			enum RoundingControl rc = (enum RoundingControl)((((int)(flags & AF_RoundingControlMask)) >> 3));

			SetRoundingControl(instruction, rc);
		}
	}
	AddInstruction(assembler, instruction);
}

/// <summary>
/// Add lock prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* lock(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_Lock;
	return assembler;
}

/// <summary>
/// Add xacquire prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* xacquire(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_Repne;
	return assembler;
}

/// <summary>
/// Add xrelease prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* xrelease(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_Repe;
	return assembler;
}

/// <summary>
/// Add rep prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* rep(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_Repe;
	return assembler;
}

/// <summary>
/// Add repe/repz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repe(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_Repe;
	return assembler;
}

/// <summary>
/// Add repe/repz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repz(struct Assembler* assembler)
{
	return repe(assembler);
}

/// <summary>
/// Add repne/repnz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repne(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_Repne;
	return assembler;
}

/// <summary>
/// Add repne/repnz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repnz(struct Assembler* assembler)
{
	return repne(assembler);
}

/// <summary>
/// Add bnd prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* bnd(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_Repne;
	return assembler;
}

/// <summary>
/// Add notrack prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* notrack(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_Notrack;
	return assembler;
}

/// <summary>
/// Prefer VEX encoding if the next instruction can be VEX and EVEX encoded
/// </summary>
/// <returns></returns>
struct Assembler* vex(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_PreferVex;
	return assembler;
}

/// <summary>
/// Prefer EVEX encoding if the next instruction can be VEX and EVEX encoded
/// </summary>
/// <returns></returns>
struct Assembler* evex(struct Assembler* assembler)
{
	assembler->prefixFlags |= PF_PreferEvex;
	return assembler;
}

///// <summary>
///// Adds data
///// </summary>
///// <param name="array">Data</param>
//void db(byte[] array) 
//{
//	if (array is null)
//		ThrowHelper.ThrowArgumentNullException_array();
//	db(array, 0, array.Length);
//}

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

	int currentPosition = index;
	for (int i = 0; i < cycles; i++) 
	{
		AddInstruction(Instruction.CreateDeclareByte(array, currentPosition, maxLength));
		currentPosition += maxLength;
	}
	if (rest > 0)
	{
		AddInstruction(Instruction.CreateDeclareByte(array, currentPosition, rest));
	}
}

/// <summary>call selector:offset instruction.</summary>
void call(struct Assembler* assembler, unsigned short selector, unsigned int offset)
{
	AddInstruction(assembler, Instruction.CreateBranch(assembler->Bitness >= 32 ? Call_ptr1632 : Call_ptr1616, selector, offset));
}


/// <summary>jmp selector:offset instruction.</summary>
void jmp(struct Assembler* assembler, unsigned short selector, unsigned int offset)
{
	AddInstruction(assembler, Instruction.CreateBranch(assembler->Bitness >= 32 ? Jmp_ptr1632 : Jmp_ptr1616, selector, offset));
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

	AddInstruction(assembler, Instruction.Create(Xlat_m8, new MemoryOperand(baseReg, Register_AL)));
}

void AppendNop(struct Assembler* assembler, int amount) 
{
	switch (amount) 
	{
	case 1:
		db(assembler, 0x90); // NOP
		break;
	case 2:
		db(assembler, 0x66, 0x90); // 66 NOP
		break;
	case 3:
		db(assembler, 0x0F, 0x1F, 0x00); // NOP dword ptr [eax] or NOP word ptr [bx+si]
		break;
	case 4:
		db(assembler, 0x0F, 0x1F, 0x40, 0x00); // NOP dword ptr [eax + 00] or NOP word ptr [bx+si]
		break;
	case 5:
		if (assembler->Bitness != 16) 
		{
			db(assembler, 0x0F, 0x1F, 0x44, 0x00, 0x00); // NOP dword ptr [eax + eax*1 + 00]
		}
		else
		{
			db(assembler, 0x0F, 0x1F, 0x80, 0x00, 0x00); // NOP word ptr[bx + si]
		}
		break;
	case 6:
		if (assembler->Bitness != 16) 
		{
			db(assembler, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00); // 66 NOP dword ptr [eax + eax*1 + 00]
		}
		else
		{
			db(assembler, 0x66, 0x0F, 0x1F, 0x80, 0x00, 0x00); // NOP dword ptr [bx+si]
		}
		break;
	case 7:
		if (assembler->Bitness != 16)
		{
			db(assembler, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00); // NOP dword ptr [eax + 00000000]
		}
		else
		{
			db(assembler, 0x67, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00); // NOP dword ptr [eax+eax]
		}
		break;
	case 8:
		if (assembler->Bitness != 16)
		{
			db(assembler, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // NOP dword ptr [eax + eax*1 + 00000000]
		}
		else
		{
			db(assembler, 0x67, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00); // NOP word ptr [eax]
		}
		break;
	case 9:
		if (assembler->Bitness != 16)
		{
			db(assembler, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // 66 NOP dword ptr [eax + eax*1 + 00000000]
		}
		else
		{
			db(assembler, 0x67, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // NOP word ptr [eax+eax]
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
void WriteByte(unsigned char* buffer, unsigned char value)
{

}

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

	inst = Instruction_Create(code, dst, ToMemoryOperand(src, Bitness));
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
struct Instruction* cmp(struct AssemblerMemoryOperand* dst, int imm, int Bitness)
{
	struct Instruction* inst;
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
	//AddInstruction(Instruction.Create(code, dst.ToMemoryOperand(Bitness), imm));
	inst = Instruction_Create(code, ToMemoryOperand(dst, Bitness));
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
	inst = Instruction_Create(code, ToMemoryOperand(dst, Bitness), imm);
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
	struct Instruction* inst;
	inst = Instruction_Create(EVEX_Vaddpd_zmm_k1z_zmm_zmmm512b64_er, zmm_dst, zmm_src1, zmm_src2);

	AddInstruction(inst, dst_flags | src2_flags);

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
	AddInstruction(inst, xmm_dst_flags | src2_flags);
}

void test_assembler()
{
	const unsigned long RIP = 0x1234567810000000;
	int Bitness = 64;
	// The assembler supports all modes: 16-bit, 32-bit and 64-bit.
	struct Assembler* c = (struct Assembler*)malloc(sizeof(struct Assembler));
	assembler(c, Bitness);
	
	struct Label* label1 = CreateLabel(c);
	struct Label* data1 = CreateLabel(c);

	struct Instruction* inst;

	//inst = Instruction.Create(Code.Sub_rm64_imm32, rsp, 0x20); // Requires more size in encoder so an optimiser might select Code.Sub_rm64_imm8 path to favor min size.
	inst = Instruction_Create(Sub_rm64_imm8, Register_RSP, 0x20);
	AddInstruction(c, inst);

	inst = Instruction_Create(Push_r64, Register_R15); // c.push(r15);
	AddInstruction(c, inst);
	inst = Instruction_Create(Add_rm64_r64, Register_RAX, Register_R15); //c.add(rax, r15);
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

	inst = Instruction_Create(Inc_rm64, Register_RAX); // c.inc(rax);
	AddInstruction(c, inst);

	// Labels can be referenced by memory operands (64-bit only) and call/jmp/jcc/loopcc instructions
	inst = Instruction_Create(Lea_r64_m, Register_RCX, ToMemoryOperand(ToMemoryOperandFromLabel(data1), Bitness)); // c.lea(rcx, __[data1]);
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
	AnonymousLabel(c); // same as @@: in masm
	inc(rax);
	je(c.@B); // reference the previous anonymous label
	inc(rcx);
	je(c.@F); // reference the next anonymous label
	nop();
	AnonymousLabel();
	nop();

	// Emit label1:
	c.Label(&label1);
	// If needed, a zero-bytes instruction can be used as a label but this is optional
	c.zero_bytes();
	c.pop(Register_R15);
	c.ret();
	c.Label(&data1);
	c.db(0xF3, 0x90); // pause

	Assemble(c, new StreamCodeWriter(stream), RIP);
}