#include "Encoder.h"
//#include "Instruction.Create.h"

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
	assembler->stream_bytes = (list_t*)malloc(sizeof(list_t));
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

struct AssemblerMemoryOperand* ToMemoryOperandFromLabel(struct Label* label)
{
	struct AssemblerMemoryOperand* memOp = AssemblerMemoryOperand_new(MOS_None, Register_None, Register_RIP, Register_None, 1, label->id, AF_None);
	return memOp;
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
		assembler->currentAnonLabel = create_label(assembler, "AnonLabel");
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
		assembler->nextAnonLabel = create_label(assembler, "AnonLabel");
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
void AddInstructionWithFlags(struct Assembler* assembler, struct Instruction* instruction, enum AssemblerOperandFlags flags)
{
	if (flags != AF_None)
	{
		if ((flags & AF_Broadcast) != 0)
		{
			SetIsBroadcast(instruction, true);
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

struct MemoryOperand* MemoryOperand_new(enum Register segmentPrefix, enum Register baseRegister, enum Register indexRegister, int scale, long displacement, int displSize, bool isBroadcast)
{
	struct MemoryOperand* o = (struct MemoryOperand*)malloc(sizeof(struct MemoryOperand));

	o->SegmentPrefix = segmentPrefix;
	o->Base = baseRegister;
	o->Index = indexRegister;
	o->Scale = scale;
	o->Displacement = displacement;
	o->DisplSize = displSize;
	o->IsBroadcast = isBroadcast;

	return o;
}

struct AssemblerMemoryOperand* AssemblerMemoryOperand_new(enum MemoryOperandSize size, enum Register segmentRegister, enum Register baseRegister, enum Register indexRegister, int scale, long displacement, enum AssemblerOperandFlags flags)
{
	struct AssemblerMemoryOperand* o = (struct AssemblerMemoryOperand*)malloc(sizeof(struct AssemblerMemoryOperand));
	o->Size = size;
	o->Segment = segmentRegister;
	o->Base = baseRegister;
	o->Index = indexRegister;
	o->Scale = scale;
	o->Flags = flags;
	return o;
}

/// <summary>
/// Gets a boolean indicating if this memory operand is a broadcast.
/// </summary>
bool IsBroadcast(struct AssemblerMemoryOperand* operand)
{
	return (operand->Flags & AF_Broadcast) != 0;
}

/// <summary>
/// Gets a boolean indicating if this memory operand is a memory access using displacement only (no base and index registers are used).
/// </summary>
bool IsDisplacementOnly(struct AssemblerMemoryOperand* operand)
{
	return operand->Base == Register_None && operand->Index == Register_None;
}

/// <summary>
/// Apply mask Register K1.
/// </summary>
struct AssemblerMemoryOperand* k1(struct AssemblerMemoryOperand* operand)
{
	struct AssemblerMemoryOperand* o = AssemblerMemoryOperand_new(operand->Size, operand->Segment, operand->Base, operand->Index, operand->Scale, operand->Displacement, (operand->Flags & ~AF_RegisterMask) | AF_K1);
	return o;
}

/// <summary>
/// Apply mask Register K2.
/// </summary>
struct AssemblerMemoryOperand* k2(struct AssemblerMemoryOperand* operand)
{
	struct AssemblerMemoryOperand* o = AssemblerMemoryOperand_new(operand->Size, operand->Segment, operand->Base, operand->Index, operand->Scale, operand->Displacement, (operand->Flags & ~AF_RegisterMask) | AF_K2);
	return o;
}

/// <summary>
/// Apply mask Register K3.
/// </summary>
struct AssemblerMemoryOperand* k3(struct AssemblerMemoryOperand* operand)
{
	struct AssemblerMemoryOperand* o = AssemblerMemoryOperand_new(operand->Size, operand->Segment, operand->Base, operand->Index, operand->Scale, operand->Displacement, (operand->Flags & ~AF_RegisterMask) | AF_K3);
	return o;
}

/// <summary>
/// Apply mask Register K4.
/// </summary>
struct AssemblerMemoryOperand* k4(struct AssemblerMemoryOperand* operand)
{
	struct AssemblerMemoryOperand* o = AssemblerMemoryOperand_new(operand->Size, operand->Segment, operand->Base, operand->Index, operand->Scale, operand->Displacement, (operand->Flags & ~AF_RegisterMask) | AF_K4);
	return o;
}

/// <summary>
/// Apply mask Register K5.
/// </summary>
struct AssemblerMemoryOperand* k5(struct AssemblerMemoryOperand* operand)
{
	struct AssemblerMemoryOperand* o = AssemblerMemoryOperand_new(operand->Size, operand->Segment, operand->Base, operand->Index, operand->Scale, operand->Displacement, (operand->Flags & ~AF_RegisterMask) | AF_K5);
	return o;
}

/// <summary>
/// Apply mask Register K6.
/// </summary>
struct AssemblerMemoryOperand* k6(struct AssemblerMemoryOperand* operand)
{
	struct AssemblerMemoryOperand* o = AssemblerMemoryOperand_new(operand->Size, operand->Segment, operand->Base, operand->Index, operand->Scale, operand->Displacement, (operand->Flags & ~AF_RegisterMask) | AF_K6);
	return o;
}

/// <summary>
/// Apply mask Register K7.
/// </summary>
struct AssemblerMemoryOperand* k7(struct AssemblerMemoryOperand* operand)
{
	struct AssemblerMemoryOperand* o = AssemblerMemoryOperand_new(operand->Size, operand->Segment, operand->Base, operand->Index, operand->Scale, operand->Displacement, (operand->Flags & ~AF_RegisterMask) | AF_K7);
	return o;
}

/// <summary>
/// Gets a memory operand for the specified bitness.
/// </summary>
/// <param name="bitness">The bitness</param>
struct MemoryOperand* ToMemoryOperand(struct AssemblerMemoryOperand* operand, int bitness)
{
	struct MemoryOperand* memoryOperand;
	int displSize = 1;
	bool flagsIsBroadcast;

	if (IsDisplacementOnly(operand))
	{
		displSize = bitness / 8;
	}
	else if (operand->Displacement == 0)
	{
		displSize = 0;
	}

	flagsIsBroadcast = (operand->Flags & AF_Broadcast) != 0;

	memoryOperand = MemoryOperand_new(operand->Segment, operand->Base, operand->Index, operand->Scale, operand->Displacement, displSize, flagsIsBroadcast);

	return memoryOperand;
}

struct AssemblerMemoryOperand* ToMemoryOperandFromRegister(enum Register baseRegister)
{
	struct AssemblerMemoryOperand* memOp = AssemblerMemoryOperand_new(MOS_None, Register_None, baseRegister, Register_None, 1, 0, AF_None);
	return memOp;
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

	int currentPosition = index;
	for (int i = 0; i < cycles; i++) 
	{
		AddInstruction(assembler, Instruction_CreateDeclareByte(array, currentPosition, maxLength));
		currentPosition += maxLength;
	}
	if (rest > 0)
	{
		AddInstruction(assembler, Instruction_CreateDeclareByte(array, currentPosition, rest));
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
	AddInstruction(assembler, Instruction_CreateBranch(assembler->Bitness >= 32 ? Call_ptr1632 : Call_ptr1616, selector, offset));
}


/// <summary>jmp selector:offset instruction.</summary>
void jmp(struct Assembler* assembler, unsigned short selector, unsigned int offset)
{
	AddInstruction(assembler, Instruction_CreateBranch(assembler->Bitness >= 32 ? Jmp_ptr1632 : Jmp_ptr1616, selector, offset));
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

	AddInstruction(assembler, Instruction_Create(Xlat_m8, memOp));
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
	return Instruction_Create(Inc_rm64, dst);
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
	AddInstruction(assembler, Instruction_Create(code));
}

/// <summary>zero_bytes instruction.<br/>
/// <br/>
/// A zero-sized instruction. Can be used as a label.</summary>
void zero_bytes(struct Assembler* assembler)
{
	AddInstruction(assembler, Instruction_Create(Zero_bytes));
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
	AddInstruction(assembler, Instruction_Create(Pop_r64, dst));
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
	AddInstruction(assembler, Instruction_Create(code));
}