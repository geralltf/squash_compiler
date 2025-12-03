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
	//assembler->stream_bytes = (list_t*)malloc(sizeof(list_t));
	//assembler->instructions = (list_t*)malloc(sizeof(list_t));
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

