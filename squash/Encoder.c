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
	if (label->InstructionIndex >= 0)
	{
		LogCritical("Cannot reuse label. The specified label is already associated with an instruction at index {label.InstructionIndex}.");
		return;
	}
	if (assembler->currentLabel != NULL)
	{
		//LogCritical("At most one label per instruction is allowed");
		//return;
	}
	label->InstructionIndex = instructions_count(assembler);
	assembler->currentLabel = label;
}

/// <summary>
/// Creates an anonymous label that can be referenced by using <see cref="B"/> (backward anonymous label)
/// and <see cref="F"/> (forward anonymous label).
/// </summary>
void anonymous_label(struct Assembler* assembler)
{
	if (definedAnonLabel)
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
		instruction->IP = assembler->currentLabel->id;
	}
	else if (definedAnonLabel) 
	{
		instruction->IP = assembler->currentAnonLabel->id;
	}

	// Setup prefixes
	if (assembler->prefixFlags != PF_None)
	{
		if ((assembler->prefixFlags & PF_Lock) != 0)
		{
			instruction->HasLockPrefix = true;
		}
		if ((assembler->prefixFlags & PF_Repe) != 0)
		{
			instruction->HasRepePrefix = true;
		}
		else if ((assembler->prefixFlags & PF_Repne) != 0)
		{
			instruction->HasRepnePrefix = true;
		}
		if ((assembler->prefixFlags & PF_Notrack) != 0)
		{
			instruction->SegmentPrefix = Register.DS;
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
	if (flags != AssemblerOperandFlags.None)
	{
		if ((flags & AssemblerOperandFlags.Broadcast) != 0)
		{
			instruction.IsBroadcast = true;
		}
		if ((flags & AssemblerOperandFlags.Zeroing) != 0)
		{
			instruction.ZeroingMasking = true;
		}
		
		if ((flags & AssemblerOperandFlags.RegisterMask) != 0) 
		{
			// register mask is shift by 2 (starts at index 1 for K1)
			instruction.OpMask = (Register)((int)Register.K0 + (((int)(flags & AssemblerOperandFlags.RegisterMask)) >> 6));
		}
		if ((flags & AssemblerOperandFlags.SuppressAllExceptions) != 0) {
			instruction.SuppressAllExceptions = true;
		}
		if ((flags & AssemblerOperandFlags.RoundingControlMask) != 0)
		{
			instruction.RoundingControl = (RoundingControl)((((int)(flags & AssemblerOperandFlags.RoundingControlMask)) >> 3));
		}
	}
	AddInstruction(instruction);
}

/// <summary>
/// Add lock prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* lock(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.Lock;
	return assembler;
}

/// <summary>
/// Add xacquire prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* xacquire(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.Repne;
	return assembler;
}

/// <summary>
/// Add xrelease prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* xrelease(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.Repe;
	return assembler;
}

/// <summary>
/// Add rep prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* rep(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.Repe;
	return assembler;
}

/// <summary>
/// Add repe/repz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repe(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.Repe;
	return assembler;
}

/// <summary>
/// Add repe/repz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repz(struct Assembler* assembler)
{
	return repe();
}

/// <summary>
/// Add repne/repnz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repne(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.Repne;
	return assembler;
}

/// <summary>
/// Add repne/repnz prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* repnz(struct Assembler* assembler)
{
	return repne();
}

/// <summary>
/// Add bnd prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* bnd(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.Repne;
	return assembler;
}

/// <summary>
/// Add notrack prefix before the next instruction.
/// </summary>
/// <returns></returns>
struct Assembler* notrack(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.Notrack;
	return assembler;
}

/// <summary>
/// Prefer VEX encoding if the next instruction can be VEX and EVEX encoded
/// </summary>
/// <returns></returns>
struct Assembler* vex(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.PreferVex;
	return assembler;
}

/// <summary>
/// Prefer EVEX encoding if the next instruction can be VEX and EVEX encoded
/// </summary>
/// <returns></returns>
struct Assembler* evex(struct Assembler* assembler)
{
	prefixFlags |= PrefixFlags.PreferEvex;
	return assembler;
}

/// <summary>
/// Adds data
/// </summary>
/// <param name="array">Data</param>
void db(byte[] array) 
{
	if (array is null)
		ThrowHelper.ThrowArgumentNullException_array();
	db(array, 0, array.Length);
}

/// <summary>
/// Adds data
/// </summary>
/// <param name="array">Data</param>
/// <param name="index">Start index</param>
/// <param name="length">Length in bytes</param>
void db(byte[] array, int index, int length) 
{
	if (array is null)
		ThrowHelper.ThrowArgumentNullException_array();
	if (index < 0)
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
	if (length < 0 || (uint)(index + length) >(uint)array.Length)
		ThrowHelper.ThrowArgumentOutOfRangeException_length();
	const int maxLength = 16;
	int cycles = Math.DivRem(length, maxLength, out int rest);
	int currentPosition = index;
	for (int i = 0; i < cycles; i++) {
		AddInstruction(Instruction.CreateDeclareByte(array, currentPosition, maxLength));
		currentPosition += maxLength;
	}
	if (rest > 0)
		AddInstruction(Instruction.CreateDeclareByte(array, currentPosition, rest));
}

/// <summary>call selector:offset instruction.</summary>
void call(unsigned short selector, unsigned int offset)
{
	AddInstruction(Instruction.CreateBranch(Bitness >= 32 ? Code.Call_ptr1632 : Code.Call_ptr1616, selector, offset));
}


/// <summary>jmp selector:offset instruction.</summary>
void jmp(unsigned short selector, unsigned int offset)
{
	AddInstruction(Instruction.CreateBranch(Bitness >= 32 ? Code.Jmp_ptr1632 : Code.Jmp_ptr1616, selector, offset));
}

/// <summary>xlatb instruction.</summary>
void xlatb() 
{
	var baseReg = Bitness switch {
		64 = > Register.RBX,
			32 = > Register.EBX,
			_ = > Register.BX,
	};
	AddInstruction(Instruction.Create(Code.Xlat_m8, new MemoryOperand(baseReg, Register.AL)));
}

/// <summary>
/// Generates multibyte NOP instructions
/// </summary>
/// <param name="sizeInBytes">Size in bytes of all nops</param>
void nop(int sizeInBytes) 
{
	if (sizeInBytes < 0)
		throw new ArgumentOutOfRangeException(nameof(sizeInBytes));
	if (this.prefixFlags != PrefixFlags.None)
		throw new InvalidOperationException("No prefixes are allowed");
	if (sizeInBytes == 0)
		return;

	const int maxMultibyteNopInstructionLength = 9;

	int cycles = Math.DivRem(sizeInBytes, maxMultibyteNopInstructionLength, out int rest);

	for (int i = 0; i < cycles; i++)
		AppendNop(maxMultibyteNopInstructionLength);
	if (rest > 0)
		AppendNop(rest);

	void AppendNop(int amount) {
		switch (amount) {
		case 1:
			db(0x90); // NOP
			break;
		case 2:
			db(0x66, 0x90); // 66 NOP
			break;
		case 3:
			db(0x0F, 0x1F, 0x00); // NOP dword ptr [eax] or NOP word ptr [bx+si]
			break;
		case 4:
			db(0x0F, 0x1F, 0x40, 0x00); // NOP dword ptr [eax + 00] or NOP word ptr [bx+si]
			break;
		case 5:
			if (Bitness != 16)
				db(0x0F, 0x1F, 0x44, 0x00, 0x00); // NOP dword ptr [eax + eax*1 + 00]
			else
				db(0x0F, 0x1F, 0x80, 0x00, 0x00); // NOP word ptr[bx + si]
			break;
		case 6:
			if (Bitness != 16)
				db(0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00); // 66 NOP dword ptr [eax + eax*1 + 00]
			else
				db(0x66, 0x0F, 0x1F, 0x80, 0x00, 0x00); // NOP dword ptr [bx+si]
			break;
		case 7:
			if (Bitness != 16)
				db(0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00); // NOP dword ptr [eax + 00000000]
			else
				db(0x67, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00); // NOP dword ptr [eax+eax]
			break;
		case 8:
			if (Bitness != 16)
				db(0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // NOP dword ptr [eax + eax*1 + 00000000]
			else
				db(0x67, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00); // NOP word ptr [eax]
			break;
		case 9:
			if (Bitness != 16)
				db(0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // 66 NOP dword ptr [eax + eax*1 + 00000000]
			else
				db(0x67, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00); // NOP word ptr [eax+eax]
			break;
		default:
			throw new InvalidOperationException();
		}
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
public static bool TryEncode(int bitness, InstructionBlock[] blocks, [NotNullWhen(false)] out string ? errorMessage, [NotNullWhen(true)] out BlockEncoderResult[] ? result, BlockEncoderOptions options = BlockEncoderOptions.None) = >
new BlockEncoder(bitness, blocks, options).Encode(out errorMessage, out result);

bool Encode([NotNullWhen(false)] out string ? errorMessage, [NotNullWhen(true)] out BlockEncoderResult[] ? result) {
	const int MAX_ITERS = 5;
	for (int iter = 0; iter < MAX_ITERS; iter++) {
		bool updated = false;
		foreach(var block in blocks) {
			ulong ip = block.RIP;
			ulong gained = 0;
			foreach(var instr in block.Instructions) {
				instr.IP = ip;
				if (!instr.Done) {
					var oldSize = instr.Size;
					if (instr.Optimize(gained)) {
						if (instr.Size > oldSize) {
							errorMessage = "Internal error: new size > old size";
							result = null;
							return false;
						}
						if (instr.Size < oldSize) {
							gained += oldSize - instr.Size;
							updated = true;
						}
					}
					else if (instr.Size != oldSize) {
						errorMessage = "Internal error: new size != old size";
						result = null;
						return false;
					}
				}
				ip += instr.Size;
			}
		}
		if (!updated)
			break;
	}

	foreach(var block in blocks)
		block.InitializeData();

	var resultArray = new BlockEncoderResult[blocks.Length];
	for (int i = 0; i < blocks.Length; i++) {
		var block = blocks[i];
		var encoder = Encoder.Create(bitness, block.CodeWriter);
		ulong ip = block.RIP;
		var newInstructionOffsets = ReturnNewInstructionOffsets ? new uint[block.Instructions.Length] : null;
		var constantOffsets = ReturnConstantOffsets ? new ConstantOffsets[block.Instructions.Length] : null;
		var instructions = block.Instructions;
		for (int j = 0; j < instructions.Length; j++) {
			var instr = instructions[j];
			uint bytesWritten = block.CodeWriter.BytesWritten;
			bool isOriginalInstruction;
			if (constantOffsets is not null)
				errorMessage = instr.TryEncode(encoder, out constantOffsets[j], out isOriginalInstruction);
			else
				errorMessage = instr.TryEncode(encoder, out _, out isOriginalInstruction);
			if (errorMessage is not null) {
				result = null;
				return false;
			}
			uint size = block.CodeWriter.BytesWritten - bytesWritten;
			if (size != instr.Size) {
				errorMessage = "Internal error: didn't write all bytes";
				result = null;
				return false;
			}
			if (newInstructionOffsets is not null) {
				if (isOriginalInstruction)
					newInstructionOffsets[j] = (uint)(ip - block.RIP);
				else
					newInstructionOffsets[j] = uint.MaxValue;
			}
			ip += size;
		}
		resultArray[i] = new BlockEncoderResult(block.RIP, block.relocInfos, newInstructionOffsets, constantOffsets);
		block.WriteData();
	}

	errorMessage = null;
	result = resultArray;
	return true;
}

/// <summary>
/// Assembles the instructions of this assembler with the specified options.
/// </summary>
/// <param name="writer">The code writer.</param>
/// <param name="rip">Base address.</param>
/// <param name="options">Encoding options.</param>
/// <returns></returns>
/// <exception cref="InvalidOperationException"></exception>
struct AssemblerResult* Assemble(struct CodeWriter* writer, unsigned long rip, enum BlockEncoderOptions options)
{ // = BlockEncoderOptions.None
	char* errorMessage;

	if (!TryAssemble(writer, rip, out var errorMessage, out var assemblerResult, options))
	{
		//LogCritical(errorMessage);
		// throw new InvalidOperationException(errorMessage);
	}
		
	return assemblerResult;
}

/// <summary>
/// Tries to assemble the instructions of this assembler with the specified options.
/// </summary>
/// <param name="writer">The code writer.</param>
/// <param name="rip">Base address.</param>
/// <param name="errorMessage">Error messages.</param>
/// <param name="assemblerResult">The assembler result if successful.</param>
/// <param name="options">Encoding options.</param>
/// <returns><c>true</c> if the encoding was successful; <c>false</c> otherwise.</returns>
bool TryAssemble(CodeWriter writer, unsigned long rip, struct AssemblerResult* assemblerResult, enum BlockEncoderOptions options) // = BlockEncoderOptions.None
{
	if (writer is null)
		ThrowHelper.ThrowArgumentNullException_writer();

	assemblerResult = default;

	// Protect against using a prefix without actually using it
	if (prefixFlags != PF_None) 
	{
		errorMessage = LogCritical("Unused prefixes {prefixFlags}. You must emit an instruction after using an instruction prefix.";
		return false;
	}

	// Protect against a label emitted without being attached to an instruction
	if (!currentLabel.IsEmpty) 
	{
		LogCritical("Unused label {currentLabel}. You must emit an instruction after emitting a label.");
		return false;
	}

	if (definedAnonLabel) 
	{
		LogCritical("Unused anonymous label. You must emit an instruction after emitting a label.");
		return false;
	}

	if (!nextAnonLabel.IsEmpty) 
	{
		LogCritical("Found an @F anonymous label reference but there was no call to "); // +nameof(AnonymousLabel);
		return false;
	}

	if (BlockEncoder.TryEncode(writer, instructions, rip, Bitness, blocks, out var blockResults, options)) {
		assemblerResult = new AssemblerResult(blockResults);
		return true;
	}
	else {
		assemblerResult = new AssemblerResult(Array2.Empty<BlockEncoderResult>());
		return false;
	}
}

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

void test_assembler()
{
	const unsigned long RIP = 0x1234567810000000;
	int Bitness = 64;
	// The assembler supports all modes: 16-bit, 32-bit and 64-bit.
	struct Assembler* c = (struct Assembler*)malloc(sizeof(struct Assembler));
	assembler(c, Bitness);

	var label1 = c.CreateLabel();
	var data1 = c.CreateLabel();

	Instruction inst;

	//inst = Instruction.Create(Code.Sub_rm64_imm32, rsp, 0x20); // Requires more size in encoder so an optimiser might select Code.Sub_rm64_imm8 path to favor min size.
	inst = Instruction.Create(Code.Sub_rm64_imm8, rsp, 0x20);
	c.AddInstruction(inst);

	inst = Instruction.Create(Code.Push_r64, r15); // c.push(r15);
	c.AddInstruction(inst);
	inst = Instruction.Create(Code.Add_rm64_r64, rax, r15); //c.add(rax, r15);
	c.AddInstruction(inst);

	// If the memory operand can only have one size, __[] can be used. The assembler ignores
	// the memory size unless it's an ambiguous instruction, eg. 'add [mem],123'
	inst = mov(rax, __[rax], Bitness);  // c.mov(rax, __[rax]);
	c.AddInstruction(inst);

	AssemblerMemoryOperand qword_operand2 = new AssemblerMemoryOperand(MemoryOperandSize.Qword, Register.None, rax, Register.None, 1, 0, AssemblerOperandFlags.None);
	inst = mov(rax, qword_operand2, Bitness); //inst = mov(rax, __qword_ptr[rax], Bitness); // c.mov(rax, __qword_ptr[rax]);
	c.AddInstruction(inst);

	// The assembler must know the memory size to pick the correct instruction
	inst = cmp(__dword_ptr[rax + rcx * 8 + 0x10], -1, Bitness); // c.cmp(__dword_ptr[rax + rcx * 8 + 0x10], -1);
	c.AddInstruction(inst);

	inst = jne(label1, c.PreferShortBranch, Bitness); // c.jne(label1); // Jump to Label1
	c.AddInstruction(inst);

	inst = Instruction.Create(Code.Inc_rm64, rax); // c.inc(rax);
	c.AddInstruction(inst);

	// Labels can be referenced by memory operands (64-bit only) and call/jmp/jcc/loopcc instructions
	inst = Instruction.Create(Code.Lea_r64_m, rcx, __[data1].ToMemoryOperand(Bitness)); // c.lea(rcx, __[data1]);
	c.AddInstruction(inst);

	// The assembler has prefix properties that will be added to the following instruction
	c.prefixFlags |= Iced.Intel.Assembler.PrefixFlags.Repe; // c.rep
	inst = Instruction.CreateStosd(Bitness); //c.rep.stosd();
	c.AddInstruction(inst);

	//c.xacquire.@lock.add(__qword_ptr[rax + rcx], 123); // f0f24883487b = xacquire lock add qword ptr [rax+rcx],7Bh
	c.prefixFlags |= Assembler.PrefixFlags.Repne; // apply xacquire to flags
	c.prefixFlags |= Assembler.PrefixFlags.Lock; // apply @lock
	//c.add(__qword_ptr[rax + rcx], 123);
	AssemblerMemoryOperand qword_operand = new AssemblerMemoryOperand(MemoryOperandSize.Qword, Register.None, rax, rcx, 1, 0, AssemblerOperandFlags.None);
	AssemblerMemoryOperand operand = new AssemblerMemoryOperand(qword_operand.Size, qword_operand.Segment, qword_operand.Base, qword_operand.Index, qword_operand.Scale, qword_operand.Displacement, qword_operand.Flags);
	c.add(operand, 123);

	// The assembler defaults to VEX instructions. If you need EVEX instructions, set PreferVex=false
	c.PreferVex = false;
	// or call `c.vex` or `c.evex` prefixes to override the default encoding.
	// AVX-512 decorators are properties on the memory and register operands
	c.vaddpd(zmm1.k3.z, zmm2, zmm3.rz_sae);

	// To broadcast memory, use the __dword_bcst/__qword_bcst memory types
	//c.vunpcklps(xmm2.k5.z, xmm6, __dword_bcst[rax]);
	AssemblerMemoryOperand dword_operand = new AssemblerMemoryOperand(MemoryOperandSize.Dword, Register.None, rax, Register.None, 1, 0, AssemblerOperandFlags.Broadcast);
	c.vunpcklps(xmm2.k5.z, xmm6, dword_operand);

	// You can create anonymous labels, just like in eg. masm, @@, @F and @B
	c.AnonymousLabel(); // same as @@: in masm
	c.inc(rax);
	c.je(c.@B); // reference the previous anonymous label
	c.inc(rcx);
	c.je(c.@F); // reference the next anonymous label
	c.nop();
	c.AnonymousLabel();
	c.nop();

	// Emit label1:
	c.Label(ref label1);
	// If needed, a zero-bytes instruction can be used as a label but this is optional
	c.zero_bytes();
	c.pop(r15);
	c.ret();
	c.Label(ref data1);
	c.db(0xF3, 0x90); // pause

	c.Assemble(new StreamCodeWriter(stream), RIP);
}