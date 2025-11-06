#include "Instruction.h"
#include "sb.h"
#include "OpCodeInfoData.h"

struct Instruction
{
	unsigned long nextRip;
	unsigned long memDispl;
	unsigned int flags1;// InstrFlags1
	unsigned int immediate;
	unsigned short code;
	unsigned char memBaseReg;// Register
	unsigned char memIndexReg;// Register
	unsigned char reg0, reg1, reg2, reg3;// Register
	unsigned char opKind0, opKind1, opKind2, opKind3;// OpKind
	unsigned char scale;
	unsigned char displSize;
	unsigned char len;
	unsigned char pad;
};

struct Instruction* instruction_new()
{
	struct Instruction* o = (struct Instruction*)malloc(sizeof(struct Instruction));
	return o;
}

void instruction_init(struct Instruction** o)
{
	(*o)->nextRip = 0;
	(*o)->memDispl = 0;
	(*o)->flags1 = 0;
	(*o)->immediate = 0;
	(*o)->code = 0;
	(*o)->memBaseReg = 0;
	(*o)->memIndexReg = 0;
	(*o)->reg0 = 0;
	(*o)->reg1 = 0;
	(*o)->reg2 = 0;
	(*o)->reg3 = 0;
	(*o)->opKind0 = 0;
	(*o)->opKind1 = 0;
	(*o)->opKind2 = 0;
	(*o)->opKind3 = 0;
	(*o)->scale = 0;
	(*o)->displSize = 0;
	(*o)->len = 0;
	(*o)->pad = 0;
}

bool instruction_equals(struct Instruction* a, struct Instruction* b)
{
	return a->memDispl == b->memDispl &&
		((a->flags1 ^ b->flags1) & ~(unsigned int)IF_EqualsIgnoreMask) == 0 &&
		a->immediate == b->immediate &&
		a->code == b->code &&
		a->memBaseReg == b->memBaseReg &&
		a->memIndexReg == b->memIndexReg &&
		a->reg0 == b->reg0 &&
		a->reg1 == b->reg1 &&
		a->reg2 == b->reg2 &&
		a->reg3 == b->reg3 &&
		a->opKind0 == b->opKind0 &&
		a->opKind1 == b->opKind1 &&
		a->opKind2 == b->opKind2 &&
		a->opKind3 == b->opKind3 &&
		a->scale == b->scale &&
		a->displSize == b->displSize &&
		a->pad == b->pad;
}

bool instruction_equals_allbits(struct Instruction* a, struct Instruction* b)
{
	return a->nextRip == b->nextRip &&
		a->memDispl == b->memDispl &&
		a->flags1 == b->flags1 &&
		a->immediate == b->immediate &&
		a->code == b->code &&
		a->memBaseReg == b->memBaseReg &&
		a->memIndexReg == b->memIndexReg &&
		a->reg0 == b->reg0 &&
		a->reg1 == b->reg1 &&
		a->reg2 == b->reg2 &&
		a->reg3 == b->reg3 &&
		a->opKind0 == b->opKind0 &&
		a->opKind1 == b->opKind1 &&
		a->opKind2 == b->opKind2 &&
		a->opKind3 == b->opKind3 &&
		a->scale == b->scale &&
		a->displSize == b->displSize &&
		a->len == b->len &&
		a->pad == b->pad;
}

int gethashcode(struct Instruction* i)
{
	unsigned int c = (unsigned int)i->memDispl;
	c ^= (unsigned int)(i->memDispl >> 32);
	c ^= i->flags1 & ~(unsigned int)IF_EqualsIgnoreMask;
	c ^= i->immediate;
	c ^= (unsigned int)i->code << 8;
	c ^= (unsigned int)i->memBaseReg << 16;
	c ^= (unsigned int)i->memIndexReg << 24;
	c ^= i->reg3;
	c ^= (unsigned int)i->reg2 << 8;
	c ^= (unsigned int)i->reg1 << 16;
	c ^= (unsigned int)i->reg0 << 24;
	c ^= i->opKind3;
	c ^= (unsigned int)i->opKind2 << 8;
	c ^= (unsigned int)i->opKind1 << 16;
	c ^= (unsigned int)i->opKind0 << 24;
	c ^= i->scale;
	c ^= (unsigned int)i->displSize << 8;
	c ^= (unsigned int)i->pad << 16;
	return (int)c;
}
/// <summary>
/// 16-bit IP of the instruction
/// </summary>
unsigned short GetIP16(struct Instruction* i)
{
	return (unsigned short)((unsigned int)i->nextRip - (unsigned int)GetLength(i));
}

/// <summary>
/// 16-bit IP of the instruction
/// </summary>
void SetIP16(struct Instruction* i, unsigned short value)
{
	i->nextRip = value + (unsigned int)GetLength(i);
}

/// <summary>
/// 32-bit IP of the instruction
/// </summary>
unsigned int GetIP32(struct Instruction* i)
{
	return (unsigned int)((unsigned int)i->nextRip - (unsigned int)GetLength(i));
}

/// <summary>
/// 32-bit IP of the instruction
/// </summary>
void SetIP32(struct Instruction* i, unsigned int value)
{
	i->nextRip = value + (unsigned int)GetLength(i);
}

/// <summary>
/// 64-bit IP of the instruction
/// </summary>
unsigned long GetIP(struct Instruction* i)
{
	return i->nextRip - (unsigned int)GetLength(i);
}

/// <summary>
/// 64-bit IP of the instruction
/// </summary>
void SetIP(struct Instruction* i, unsigned long value)
{
	i->nextRip = value + (unsigned int)GetLength(i);
}

/// <summary>
/// 16-bit IP of the next instruction
/// </summary>
unsigned short GetNextIP16(struct Instruction* i)
{
	return (unsigned short)i->nextRip;
}

/// <summary>
/// 16-bit IP of the next instruction
/// </summary>
void SetNextIP16(struct Instruction* i, unsigned short value)
{
	i->nextRip = value;
}

/// <summary>
/// 32-bit IP of the next instruction
/// </summary>
unsigned int GetNextIP32(struct Instruction* i)
{
	return (unsigned int)i->nextRip;
}

/// <summary>
/// 32-bit IP of the next instruction
/// </summary>
void SetNextIP32(struct Instruction* i, unsigned int value)
{
	i->nextRip = value;
}

/// <summary>
/// 64-bit IP of the next instruction
/// </summary>
unsigned long GetNextIP64(struct Instruction* i)
{
	return i->nextRip;
}

/// <summary>
/// 64-bit IP of the next instruction
/// </summary>
void SetNextIP64(struct Instruction* i, unsigned long value)
{
	i->nextRip = value;
}

/// <summary>
/// Gets the code size when the instruction was decoded. This value is informational and can
/// be used by a formatter.
/// </summary>
enum CodeSize GetCodeSize(struct Instruction* i)
{
	enum CodeSize codeSize;
	codeSize = (enum CodeSize)((i->flags1 >> (int)IF_CodeSizeShift)& (unsigned int)IF_CodeSizeMask);
	return codeSize;
}

/// <summary>
/// Sets the code size when the instruction was decoded. This value is informational and can
/// be used by a formatter.
/// </summary>
void SetCodeSize(struct Instruction* i, enum CodeSize value)
{
	i->flags1 = ((i->flags1 & ~((unsigned int)IF_CodeSizeMask << (int)IF_CodeSizeShift)) 
		| (((unsigned int)value & (unsigned int)IF_CodeSizeMask) << (int)IF_CodeSizeShift));
}

void SetInternalCodeSize(struct Instruction* i, enum CodeSize value)
{
	i->flags1 |= ((unsigned int)value << (int)IF_CodeSizeShift);
}

/// <summary>
/// Checks if it's an invalid instruction (<see cref="Code"/> == <see cref="Code.INVALID"/>)
/// </summary>
bool GetIsInvalid(struct Instruction* i)
{
	return i->code == (unsigned short)INVALID;
}

/// <summary>
/// Instruction code, see also <see cref="Mnemonic"/>
/// </summary>
enum Code GetCode(struct Instruction* i)
{
	return (enum Code)i->code;
}

/// <summary>
/// Instruction code, see also <see cref="Mnemonic"/>
/// </summary>
void SetCode(struct Instruction* i, enum Code value)
{
	if ((unsigned int)value >= (unsigned int)CodeEnumCount)
	{
		//ThrowArgumentOutOfRangeException_value();
		return;
	}
	i->code = (unsigned short)value;
}

/// <summary>
/// Gets the mnemonic
/// </summary>
/// <param name="code">Code value</param>
/// <returns></returns>
enum Mnemonic to_mnemonic(enum Code code) 
{
	return (enum Mnemonic)toMnemonic[(int)code];
}

void InternalSetCodeNoCheck(struct Instruction* i, enum Code code)
{
	i->code = (unsigned short)code;
}

/// <summary>
/// Gets the mnemonic, see also <see cref="Code"/>
/// </summary>
enum Mnemonic GetMnemonic(struct Instruction* i)
{
	enum Code code = GetCode(i);
	enum Mnemonic mnemonic = to_mnemonic(code);
	return mnemonic;
}

/// <summary>
/// Gets the operand count. An instruction can have 0-5 operands.
/// </summary>
int GetOpCount(struct Instruction* i)
{
	return InstructionOpCounts_OpCount[(int)i->code];
}

/// <summary>
/// Gets the length of the instruction, 0-15 bytes. This is just informational. If you modify the instruction
/// or create a new one, this property could return the wrong value.
/// </summary>
int GetLength(struct Instruction* i)
{
	return i->len;
}

/// <summary>
/// Sets the length of the instruction, 0-15 bytes. This is just informational. If you modify the instruction
/// or create a new one, this property could return the wrong value.
/// </summary>
void SetLength(struct Instruction* i, int value)
{
	i->len = (unsigned char)value;
}

bool Get_Internal_HasRepeOrRepnePrefix(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)(IF_RepePrefix | IF_RepnePrefix)) != 0;
}

unsigned int Get_HasAnyOf_Lock_Rep_Repne_Prefix(struct Instruction* i)
{
	return i->flags1 & (unsigned int)(IF_LockPrefix | IF_RepePrefix | IF_RepnePrefix);
}

/// <summary>
/// Gets operand #0's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
enum OpKind GetOp0Kind(struct Instruction* i)
{
	return (enum OpKind)i->opKind0;
}
/// <summary>
/// Sets operand #0's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
void SetOp0Kind(struct Instruction* i, enum OpKind value)
{
	i->opKind0 = (unsigned char)value;
}

bool Get_Internal_Op0IsNotReg_or_Op1IsNotReg(struct Instruction* i)
{
	return (i->opKind0 | i->opKind1) != 0;
}

/// <summary>
/// Gets operand #1's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
enum OpKind GetOp1Kind(struct Instruction* i)
{
	return (enum OpKind)i->opKind1;
}
/// <summary>
/// Sets operand #1's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
void SetOp1Kind(struct Instruction* i, enum OpKind value)
{
	i->opKind1 = (unsigned char)value;
}

/// <summary>
/// Gets operand #2's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
enum OpKind GetOp2Kind(struct Instruction* i)
{
	return (enum OpKind)i->opKind2;
}
/// <summary>
/// Sets operand #2's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
void SetOp2Kind(struct Instruction* i, enum OpKind value)
{
	i->opKind2 = (unsigned char)value;
}

/// <summary>
/// Gets operand #3's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
enum OpKind GetOp3Kind(struct Instruction* i)
{
	return (enum OpKind)i->opKind3;
}
/// <summary>
/// Sets operand #3's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
void SetOp3Kind(struct Instruction* i, enum OpKind value)
{
	i->opKind3 = (unsigned char)value;
}

/// <summary>
/// Gets operand #4's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
enum OpKind GetOp4Kind(struct Instruction* i)
{
	return OK_Immediate8;
}
/// <summary>
/// Sets operand #4's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
void SetOp4Kind(struct Instruction* i, enum OpKind value)
{
	if (value != OK_Immediate8)
	{
		//ThrowHelper.ThrowArgumentOutOfRangeException_value();
	}
}

/// <summary>
/// Gets an operand's kind if it exists (see <see cref="OpCount"/>)
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <returns></returns>
enum OpKind Instruction_GetOpKind(struct Instruction* i, int operand)
{
	switch (operand) 
	{
	case 0: return GetOp0Kind(i);
	case 1: return GetOp1Kind(i);
	case 2: return GetOp2Kind(i);
	case 3: return GetOp3Kind(i);
	case 4: return GetOp4Kind(i);
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_operand();
		return (enum OpKind)0;
	}
}

/// <summary>
/// Gets whether a specific operand's kind exists
/// </summary>
/// <param name="opKind">Operand kind</param>
/// <returns></returns>
bool HasOpKind(struct Instruction* ins, enum OpKind opKind)
{
	for (int i = 0; i < GetOpCount(i); i++) 
	{
		if (GetOpKind(ins, i) == opKind)
		{
			return true;
		}
	}
	return false;
}

/// <summary>
/// Sets an operand's kind
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="opKind">Operand kind</param>
void SetOpKind(struct Instruction* i, int operand, enum OpKind opKind)
{
	switch (operand) 
	{
	case 0: SetOp0Kind(i, opKind); break;
	case 1: SetOp1Kind(i, opKind); break;
	case 2: SetOp2Kind(i, opKind); break;
	case 3: SetOp3Kind(i, opKind); break;
	case 4: SetOp4Kind(i, opKind); break;
	default: 
		//ThrowHelper.ThrowArgumentOutOfRangeException_operand(); 
		break;
	}
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>LOCK</c> prefix (<c>F0</c>)
/// </summary>
bool Get_HasLockPrefix(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_LockPrefix) != 0;
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>LOCK</c> prefix (<c>F0</c>)
/// </summary>
void Set_HasLockPrefix(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 |= (unsigned int)IF_LockPrefix;
	}
	else
	{
		i->flags1 &= ~(unsigned int)IF_LockPrefix;
	}
}

void InternalSetHasLockPrefix(struct Instruction* i)
{
	i->flags1 |= (unsigned int)IF_LockPrefix;
}

void InternalClearHasLockPrefix(struct Instruction* i) 
{
	i->flags1 &= ~(unsigned int)IF_LockPrefix;
}

bool IsXacquireInstr(struct Instruction* i) 
{
	if (GetOp0Kind(i) != OK_Memory)
	{
		return false;
	}
		
	if (Get_HasLockPrefix(i))
	{
		return GetCode(i) != Cmpxchg16b_m128;
	}
	return GetMnemonic(i) == M_Xchg;
}

bool IsXreleaseInstr(struct Instruction* i)
{
	if (GetOp0Kind(i) != OK_Memory)
	{
		return false;
	}
	if (GetHasLockPrefix(i))
	{
		return GetCode(i) != Cmpxchg16b_m128;
	}
		
	switch (GetCode(i)) 
	{
	case Xchg_rm8_r8:
	case Xchg_rm16_r16:
	case Xchg_rm32_r32:
	case Xchg_rm64_r64:
	case Mov_rm8_r8:
	case Mov_rm16_r16:
	case Mov_rm32_r32:
	case Mov_rm64_r64:
	case Mov_rm8_imm8:
	case Mov_rm16_imm16:
	case Mov_rm32_imm32:
	case Mov_rm64_imm32:
		return true;
	default:
		return false;
	}
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>XACQUIRE</c> prefix (<c>F2</c>)
/// </summary>
bool Get_HasXacquirePrefix(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_RepnePrefix) != 0 && IsXacquireInstr(i);
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>XACQUIRE</c> prefix (<c>F2</c>)
/// </summary>
void Set_HasXacquirePrefix(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 |= (unsigned int)IF_RepnePrefix;
	}
	else
	{
		i->flags1 &= ~(unsigned int)IF_RepnePrefix;
	}
}

void InternalSetHasXacquirePrefix(struct Instruction* i)
{
	i->flags1 |= (unsigned int)IF_RepnePrefix;
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>XRELEASE</c> prefix (<c>F3</c>)
/// </summary>
bool HasXreleasePrefix(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_RepePrefix) != 0 && IsXreleaseInstr(i);
}

void Set_HasXreleasePrefix(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 |= (unsigned int)IF_RepePrefix;
	}
	else
	{
		i->flags1 &= ~(unsigned int)IF_RepePrefix;
	}
}

void InternalSetHasXreleasePrefix(struct Instruction* i)
{
	i->flags1 |= (unsigned int)IF_RepePrefix;
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>REPE</c> or <c>REP</c> prefix (<c>F3</c>)
/// </summary>
bool Get_HasRepPrefix(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_RepePrefix) != 0;
}

void Set_HasRepPrefix(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 |= (unsigned int)IF_RepePrefix;
	}
	else 
	{
		i->flags1 &= ~(unsigned int)IF_RepePrefix;
	}
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>REPE</c> or <c>REP</c> prefix (<c>F3</c>)
/// </summary>
bool Get_HasRepePrefix(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_RepePrefix) != 0;
}

void Set_HasRepePrefix(struct Instruction* i, bool value)
{
	if (value) 
	{
		i->flags1 |= (unsigned int)IF_RepePrefix;
	}
	else 
	{
		i->flags1 &= ~(unsigned int)IF_RepePrefix;
	}
}

void InternalSetHasRepePrefix(struct Instruction* i)
{
	i->flags1 = (i->flags1 & ~(unsigned int)IF_RepnePrefix) | (unsigned int)IF_RepePrefix;
}

void InternalClearHasRepePrefix(struct Instruction* i)
{
	i->flags1 &= ~(unsigned int)IF_RepePrefix;
}

void InternalClearHasRepeRepnePrefix(struct Instruction* i)
{
	i->flags1 &= ~((unsigned int)IF_RepePrefix | (unsigned int)IF_RepnePrefix);
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>REPNE</c> prefix (<c>F2</c>)
/// </summary>
bool Get_HasRepnePrefix(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_RepnePrefix) != 0;
}

void Set_HasRepnePrefix(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 |= (unsigned int)IF_RepnePrefix;
	}
	else
	{
		i->flags1 &= ~(unsigned int)IF_RepnePrefix;
	}
}

void InternalSetHasRepnePrefix(struct Instruction* i)
{
	i->flags1 = (i->flags1 & ~(unsigned int)IF_RepePrefix) | (unsigned int)IF_RepnePrefix;
}

void InternalClearHasRepnePrefix(struct Instruction* i)
{
	i->flags1 &= ~(unsigned int)IF_RepnePrefix;
}

/// <summary>
/// Checks if the instruction has a segment override prefix, see <see cref="SegmentPrefix"/>
/// </summary>
bool HasSegmentPrefix(struct Instruction* i)
{
	return (((i->flags1 >> (int)IF_SegmentPrefixShift) & (unsigned int)IF_SegmentPrefixMask) - 1) < 6;
}

/// <summary>
/// Gets the segment override prefix or <see cref="Register.None"/> if none. See also <see cref="MemorySegment"/>.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>,
/// <see cref="OpKind.MemorySegSI"/>, <see cref="OpKind.MemorySegESI"/>, <see cref="OpKind.MemorySegRSI"/>
/// </summary>
enum Register Get_SegmentPrefix(struct Instruction* i)
{
	unsigned int index = ((i->flags1 >> (int)IF_SegmentPrefixShift) & (unsigned int)IF_SegmentPrefixMask) - 1;
	return index < 6 ? Register_ES + (int)index : Register_None;
}

void Set_SegmentPrefix(struct Instruction* i, enum Register value)
{
	unsigned int encValue;
	if (value == Register_None)
	{
		encValue = 0;
	}
	else
	{
		encValue = (((unsigned int)value - (unsigned int)Register_ES) + 1) & (unsigned int)IF_SegmentPrefixMask;
	}
	i->flags1 = (i->flags1 & ~((unsigned int)IF_SegmentPrefixMask << (int)IF_SegmentPrefixShift)) |
		(encValue << (int)IF_SegmentPrefixShift);
}

/// <summary>
/// Gets the memory operand's base register or <see cref="Register.None"/> if none. Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
enum Register GetMemoryBase(struct Instruction* i)
{
	return (enum Register)i->memBaseReg;
}

void SetMemoryBase(struct Instruction* i, enum Register value)
{
	i->memBaseReg = (unsigned char)value;
}

void SetInternalMemoryBase(struct Instruction* i, enum Register value)
{
	i->memBaseReg = (unsigned char)value;
}

/// <summary>
/// Gets the effective segment register used to reference the memory location.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>,
/// <see cref="OpKind.MemorySegSI"/>, <see cref="OpKind.MemorySegESI"/>, <see cref="OpKind.MemorySegRSI"/>
/// </summary>
enum Register MemorySegment(struct Instruction* i)
{
	enum Register segReg = Get_SegmentPrefix(i);
	if (segReg != Register_None)
	{
		return segReg;
	}
	enum Register baseReg = GetMemoryBase(i);
	if (baseReg == Register_BP || baseReg == Register_EBP || baseReg == Register_ESP || baseReg == Register_RBP || baseReg == Register_RSP)
	{
		return Register_SS;
	}
	return Register_DS;
}

/// <summary>
/// Gets the size of the memory displacement in bytes. Valid values are <c>0</c>, <c>1</c> (16/32/64-bit), <c>2</c> (16-bit), <c>4</c> (32-bit), <c>8</c> (64-bit).
/// Note that the return value can be 1 and <see cref="MemoryDisplacement64"/> may still not fit in
/// a signed byte if it's an EVEX/MVEX encoded instruction.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
int GetMemoryDisplSize(struct Instruction* i)
{
	switch (i->displSize)
	{
	case 0: return 0;
	case 1: return 1;
	case 2: return 2;
	case 3: return 4;
	default: return 8;
	}
}

void SetMemoryDisplSize(struct Instruction* i, int value)
{
	switch (value)
	{
	case 0:
		i->displSize = 0;
		break;
	case 1:
		i->displSize = 1;
		break;
	case 2:
		i->displSize = 2;
		break;
	case 4:
		i->displSize = 3;
		break;
	default:
		i->displSize = 4;
		break;
	}
}

void InternalSetMemoryDisplSize(struct Instruction* i, unsigned int scale) 
{
	//Debug.Assert(0 <= scale && scale <= 4);
	i->displSize = (unsigned char)scale;
}

/// <summary>
/// <see langword="true"/> if the data is broadcast (EVEX instructions only)
/// </summary>
bool IsBroadcast2(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_Broadcast) != 0;
}

void SetIsBroadcast(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 |= (unsigned int)IF_Broadcast;
	}	
	else
	{
		i->flags1 &= ~(unsigned int)IF_Broadcast;
	}
}

void InternalSetIsBroadcast(struct Instruction* i)
{
	i->flags1 |= (unsigned int)IF_Broadcast;
}

bool IsMvex(struct Instruction* i, enum Code code)
{
	return ((unsigned int)code - MvexStart) < MvexLength;
}

bool IsMvexEvictionHint(struct Instruction* i)
{
	return IsMvex(i, GetCode(i)) && (i->immediate & (unsigned int)MIF_EvictionHint) != 0;
}

/// <summary>
/// <see langword="true"/> if eviction hint bit is set (<c>{eh}</c>) (MVEX instructions only)
/// </summary>
bool GetIsMvexEvictionHint(struct Instruction* i)
{
	return IsMvex(i, GetCode(i)) && (i->immediate & (unsigned int)MIF_EvictionHint) != 0;
}

void SetIsMvexEvictionHint(struct Instruction* i, bool value)
{
	if (value) 
	{
		i->immediate |= (unsigned int)MIF_EvictionHint;
	}
	else
	{
		i->immediate &= ~(unsigned int)MIF_EvictionHint;
	}
}

void InternalSetIsMvexEvictionHint(struct Instruction* i) 
{
	i->immediate |= (unsigned int)MIF_EvictionHint;
}

/// <summary>
/// (MVEX) Register/memory operand conversion function
/// </summary>
enum MvexRegMemConv GetMvexRegMemConv(struct Instruction* i)
{
	if (!IsMvex(i, GetCode(i)))
	{
		return MRMC_None;
	}
		
	return (enum MvexRegMemConv)((i->immediate >> (int)MIF_MvexRegMemConvShift) & (unsigned int)MIF_MvexRegMemConvMask);
}

void SetMvexRegMemConv(struct Instruction* i, enum MvexRegMemConv value)
{
	i->immediate = (i->immediate & ~((unsigned int)MIF_MvexRegMemConvMask << (int)MIF_MvexRegMemConvShift)) |
		((unsigned int)value << (int)MIF_MvexRegMemConvShift);
}

void InternalSetMvexRegMemConv(struct Instruction* i, enum MvexRegMemConv newValue)
{
	i->immediate |= ((unsigned int)newValue << (int)MIF_MvexRegMemConvShift);
}

int GetIndexFromCode(enum Code code)
{
	int index = (int)code - (int)MvexStart;
	//Debug.Assert((uint)index < IcedConstants.MvexLength);
	return index;
}

int GetIndexFromInstruction(struct Instruction* i)
{
	return GetIndexFromCode((enum Code)i->code);
}

enum MvexTupleTypeLutKind TupleTypeLutKind(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return (enum MvexTupleTypeLutKind)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_TupleTypeLutKindIndex];
}
enum MvexEHBit EHBit(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return (enum MvexEHBit)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_EHBitIndex];
}
enum MvexConvFn ConvFn(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return (enum MvexConvFn)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_ConvFnIndex];
}
unsigned int InvalidConvFns(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_InvalidConvFnsIndex];
}
unsigned int InvalidSwizzleFns(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_InvalidSwizzleFnsIndex];
}
bool IsNDD(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags1)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_NDD) != 0;
}
bool IsNDS(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags1)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_NDS) != 0;
}
bool CanUseEvictionHint(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags1)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_EvictionHint) != 0;
}
bool CanUseImmRoundingControl(struct Instruction* i) 
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags1)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_ImmRoundingControl) != 0;
}
bool CanUseRoundingControl(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags1)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_RoundingControl) != 0;
}
bool CanUseSuppressAllExceptions(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags1)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_SuppressAllExceptions) != 0;
}
bool IgnoresOpMaskRegister(struct Instruction* i) 
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags1)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_IgnoresOpMaskRegister) != 0;
}
bool RequireOpMaskRegister(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags1)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_RequireOpMaskRegister) != 0;
}
bool NoSaeRc(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags2)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags2Index] & MIF2_NoSaeRoundingControl) != 0;
}
bool IsConvFn32(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags2)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags2Index] & MIF2_ConvFn32) != 0;
}
bool IgnoresEvictionHint(struct Instruction* i)
{
	int index = GetIndexFromInstruction(i);
	return ((enum MvexInfoFlags2)MvexInfoData_Data[index * MvexInfoData_StructSize + MvexInfoData_Flags2Index] & MIF2_IgnoresEvictionHint) != 0;
}

//public MvexInfo(Code code) {
//	index = (int)code - (int)IcedConstants.MvexStart;
//	Debug.Assert((uint)index < IcedConstants.MvexLength);
//}

enum TupleType GetTupleType(struct Instruction* i, int sss)
{
	return (enum TupleType)MvexTupleTypeLut_Data[(int)TupleTypeLutKind(i) * 8 + sss];
}


/// <summary>
/// Gets the size of the memory location that is referenced by the operand. See also <see cref="IsBroadcast"/>.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>,
/// <see cref="OpKind.MemorySegSI"/>, <see cref="OpKind.MemorySegESI"/>, <see cref="OpKind.MemorySegRSI"/>,
/// <see cref="OpKind.MemoryESDI"/>, <see cref="OpKind.MemoryESEDI"/>, <see cref="OpKind.MemoryESRDI"/>
/// </summary>
enum MemorySize GetMemorySize(struct Instruction* i)
{
	int index = (int)GetCode(i);
	if (IsMvex(i, (enum Code)index)) 
	{
		//var mvex = new MvexInfo((enum Code)index);
		int sss = ((int)GetMvexRegMemConv(i) - (int)MRMC_MemConvNone) & 7;
		return (enum MemorySize)MvexMemorySizeLut_Data[(int)TupleTypeLutKind(i) * 8 + sss];
	}
	if (IsBroadcast(i))
	{
		return (enum MemorySize)InstructionMemorySizes_SizesBcst[index];
	}
	else
	{
		return (enum MemorySize)InstructionMemorySizes_SizesNormal[index];
	}
}

/// <summary>
/// Gets the index register scale value, valid values are <c>*1</c>, <c>*2</c>, <c>*4</c>, <c>*8</c>. Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
int GetMemoryIndexScale(struct Instruction* i)
{
	return 1 << (int)i->scale;
}

void SetMemoryIndexScale(struct Instruction* i, int value)
{
	if (value == 1)
	{
		i->scale = 0;
	}
	else if (value == 2)
	{
		i->scale = 1;
	}
	else if (value == 4)
	{
		i->scale = 2;
	}
	else {
		//Debug.Assert(value == 8);
		i->scale = 3;
	}
}

int GetInternalMemoryIndexScale(struct Instruction* i)
{
	return i->scale;
}

void SetInternalMemoryIndexScale(struct Instruction* i, int value)
{
	i->scale = (unsigned char)value;
}

/// <summary>
/// Gets the memory operand's displacement or the 32-bit absolute address if it's
/// an <c>EIP</c> or <c>RIP</c> relative memory operand.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
unsigned int GetMemoryDisplacement32(struct Instruction* i)
{
	return (unsigned int)i->memDispl;
}

void SetMemoryDisplacement32(struct Instruction* i, unsigned int value)
{
	i->memDispl = value;
}

/// <summary>
/// Gets the memory operand's displacement or the 64-bit absolute address if it's
/// an <c>EIP</c> or <c>RIP</c> relative memory operand.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
unsigned long GetMemoryDisplacement64(struct Instruction* i)
{
	return i->memDispl;
}

void SetMemoryDisplacement64(struct Instruction* i, unsigned long value)
{
	i->memDispl = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8"/>
/// </summary>
unsigned char GetImmediate8(struct Instruction* i)
{
	return (unsigned char)i->immediate;
}

void SetImmediate8(struct Instruction* i, unsigned char value)
{
//#if MVEX
	i->immediate = (i->immediate & 0xFFFFFF00) | (unsigned int)value;
//#else
//	i->immediate = value;
//#endif
}

void SetInternalImmediate8(struct Instruction* i, unsigned int value)
{
	i->immediate = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8_2nd"/>
/// </summary>
unsigned char GetImmediate8_2nd(struct Instruction* i)
{
	return (unsigned char)i->memDispl;
}

void SetImmediate8_2nd(struct Instruction* i, unsigned char value)
{
	i->memDispl = value;
}

void SetInternalImmediate8_2nd(struct Instruction* i, unsigned int value)
{
	i->memDispl = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate16"/>
/// </summary>
unsigned short GetImmediate16(struct Instruction* i)
{
	return (unsigned short)i->immediate;
}

void SetImmediate16(struct Instruction* i, unsigned short value)
{
	i->immediate = value;
}

void SetInternalImmediate16(struct Instruction* i, unsigned int value)
{
	i->immediate = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate32"/>
/// </summary>
unsigned int GetImmediate32(struct Instruction* i)
{
	return i->immediate;
}

void SetImmediate32(struct Instruction* i, unsigned int value)
{
	i->immediate = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate64"/>
/// </summary>
unsigned long GetImmediate64(struct Instruction* i)
{
	return (i->memDispl << 32) | i->immediate;
}

void SetImmediate64(struct Instruction* i, unsigned long value)
{
	i->immediate = (unsigned int)value;
	i->memDispl = (unsigned int)(value >> 32);
}

void SetInternalImmediate64_lo(struct Instruction* i, unsigned int value)
{
	i->immediate = value;
}

void SetInternalImmediate64_hi(struct Instruction* i, unsigned int value)
{
	i->memDispl = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8to16"/>
/// </summary>
short GetImmediate8to16(struct Instruction* i)
{
	return (short)(signed char)i->immediate;
}

void SetImmediate8to16(struct Instruction* i, short value)
{
	i->immediate = (unsigned int)(signed char)value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8to32"/>
/// </summary>
int GetImmediate8to32(struct Instruction* i)
{
	return (int)(signed char)i->immediate;
}

void SetImmediate8to32(struct Instruction* i, int value)
{
	i->immediate = (unsigned int)(signed char)value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8to64"/>
/// </summary>
long GetImmediate8to64(struct Instruction* i)
{
	return (long)(signed char)i->immediate;
}

void SetImmediate8to64(struct Instruction* i, long value)
{
	i->immediate = (unsigned int)(signed char)value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate32to64"/>
/// </summary>
long GetImmediate32to64(struct Instruction* i)
{
	return (int)i->immediate;
}

void SetImmediate32to64(struct Instruction* i, long value)
{
	i->immediate = (unsigned int)value;
}

/// <summary>
/// Gets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <returns></returns>
unsigned long GetImmediate(struct Instruction* i, int operand)
{
	switch (GetOpKind(i, operand))
	{
	case OK_Immediate8:
		return GetImmediate8(i);
	case OK_Immediate8_2nd:
		return GetImmediate8_2nd(i);
	case OK_Immediate16:
		return GetImmediate16(i);
	case OK_Immediate32:
		return GetImmediate32(i);
	case OK_Immediate64:
		return GetImmediate64(i);
	case OK_Immediate8to16:
		return (unsigned long)GetImmediate8to16(i);
	case OK_Immediate8to32:
		return (unsigned long)GetImmediate8to32(i);
	case OK_Immediate8to64:
		return (unsigned long)GetImmediate8to64(i);
	default:
		// Op{operand} isn't an immediate operand
		return 0;
	}
}

/// <summary>
/// Sets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="immediate">New immediate</param>
/// <returns></returns>
void SetImmediate(struct Instruction* i, int operand, int immediate)
{
	SetImmediate(i, operand, (unsigned long)immediate);
}

/// <summary>
/// Sets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="immediate">New immediate</param>
/// <returns></returns>
void SetImmediate(struct Instruction* i, int operand, unsigned int immediate)
{
	SetImmediate(i, operand, (unsigned long)immediate);
}

/// <summary>
/// Sets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="immediate">New immediate</param>
/// <returns></returns>
void SetImmediate(struct Instruction* i, int operand, long immediate)
{
	SetImmediate(i, operand, (unsigned long)immediate);
}

/// <summary>
/// Sets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="immediate">New immediate</param>
/// <returns></returns>
void SetImmediate(struct Instruction* i, int operand, unsigned long immediate) 
{
	switch (GetOpKind(i, operand)) 
	{
	case OK_Immediate8:
		SetImmediate8(i, (unsigned char)immediate);
		break;
	case OK_Immediate8to16:
		SetImmediate8to16(i, (short)immediate);
		break;
	case OK_Immediate8to32:
		SetImmediate8to32(i, (int)immediate);
		break;
	case OK_Immediate8to64:
		SetImmediate8to64(i, (long)immediate);
		break;
	case OK_Immediate8_2nd:
		SetImmediate8_2nd(i, (unsigned char)immediate);
		break;
	case OK_Immediate16:
		SetImmediate16(i, (unsigned short)immediate);
		break;
	case OK_Immediate32to64:
		SetImmediate32to64(i, (long)immediate);
		break;
	case OK_Immediate32:
		SetImmediate32(i, (unsigned int)immediate);
		break;
	case OK_Immediate64:
		SetImmediate64(i, immediate);
		break;
	default:
		// Op{operand} isn't an immediate operand
		break;
	}
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.NearBranch16"/>
/// </summary>
unsigned short GetNearBranch16(struct Instruction* i)
{
	return (unsigned short)i->memDispl;
}

void SetNearBranch16(struct Instruction* i, unsigned short value)
{
	i->memDispl = value;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.NearBranch32"/>
/// </summary>
unsigned int GetInternalNearBranch16(struct Instruction* i)
{
	return (unsigned int)i->memDispl;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.NearBranch32"/>
/// </summary>
unsigned int GetNearBranch32(struct Instruction* i)
{
	return (unsigned int)i->memDispl;
}

void SetNearBranch32(struct Instruction* i, unsigned int value)
{
	i->memDispl = value;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.NearBranch64"/>
/// </summary>
unsigned long GetNearBranch64(struct Instruction* i)
{
	return i->memDispl;
}

void SetNearBranch64(struct Instruction* i, unsigned long value)
{
	i->memDispl = value;
}

/// <summary>
/// Gets the near branch target if it's a <c>CALL</c>/<c>JMP</c>/<c>Jcc</c> near branch instruction
/// (i.e., if <see cref="Op0Kind"/> is <see cref="OpKind.NearBranch16"/>, <see cref="OpKind.NearBranch32"/> or <see cref="OpKind.NearBranch64"/>)
/// </summary>
unsigned long GetNearBranchTarget(struct Instruction* i)
{
	enum OpKind opKind = GetOp0Kind(i);
//#if MVEX
	// Check if JKZD/JKNZD
	if (GetOpCount(i) == 2)
	{
		opKind = GetOp1Kind(i);
	}
//#endif

	switch (opKind)
	{
	case OK_NearBranch16:
		return GetNearBranch16(i);
	case OK_NearBranch32:
		return GetNearBranch32(i);
	case OK_NearBranch64:
		return GetNearBranch64(i);
	default:
		return 0;
	}
	return 0;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.FarBranch16"/>
/// </summary>
unsigned short GetFarBranch16(struct Instruction* i)
{
	return (unsigned short)i->immediate;
}

void SetFarBranch16(struct Instruction* i, unsigned short value)
{
	i->immediate = value;
}

void SetInternalFarBranch16(struct Instruction* i, unsigned int value)
{
	i->immediate = value;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.FarBranch32"/>
/// </summary>
unsigned int GetFarBranch32(struct Instruction* i)
{
	return i->immediate;
}

void SetFarBranch32(struct Instruction* i, unsigned int value)
{
	i->immediate = value;
}

/// <summary>
/// Gets the operand's branch target selector. Use this property if the operand has kind <see cref="OpKind.FarBranch16"/> or <see cref="OpKind.FarBranch32"/>
/// </summary>
unsigned short GetFarBranchSelector(struct Instruction* i)
{
	return (unsigned short)i->memDispl;
}

void SetFarBranchSelector(struct Instruction* i, unsigned short value)
{
	i->memDispl = value;
}

void SetInternalFarBranchSelector(struct Instruction* i, unsigned int value)
{
	i->memDispl = value;
}

/// <summary>
/// Gets the memory operand's index register or <see cref="Register.None"/> if none. Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
enum Register GetMemoryIndex(struct Instruction* i)
{
	return (enum Register)i->memIndexReg;
}

void SetMemoryIndex(struct Instruction* i, enum Register value)
{
	i->memIndexReg = (unsigned char)value;
}

void SetInternalMemoryIndex(struct Instruction* i, enum Register value)
{
	i->memIndexReg = (unsigned char)value;
}

/// <summary>
/// Gets operand #0's register value. Use this property if operand #0 (<see cref="Op0Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
enum Register GetOp0Register(struct Instruction* i)
{
	return (enum Register)i->reg0;
}

void SetOp0Register(struct Instruction* i, enum Register value)
{
	i->reg0 = (unsigned char)value;
}

void SetInternalOp0Register(struct Instruction* i, enum Register value)
{
	i->reg0 = (unsigned char)value;
}

/// <summary>
/// Gets operand #1's register value. Use this property if operand #1 (<see cref="Op1Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
enum Register GetOp1Register(struct Instruction* i)
{
	return (enum Register)i->reg1;
}

void SetOp1Register(struct Instruction* i, enum Register value)
{
	i->reg1 = (unsigned char)value;
}

void SetInternalOp1Register(struct Instruction* i, enum Register value)
{
	i->reg1 = (unsigned char)value;
}

/// <summary>
/// Gets operand #2's register value. Use this property if operand #2 (<see cref="Op2Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
enum Register GetOp2Register(struct Instruction* i)
{
	return (enum Register)i->reg2;
}

void SetOp2Register(struct Instruction* i, enum Register value)
{
	i->reg2 = (unsigned char)value;
}

void SetInternalOp2Register(struct Instruction* i, enum Register value)
{
	i->reg2 = (unsigned char)value;
}

/// <summary>
/// Gets operand #3's register value. Use this property if operand #3 (<see cref="Op3Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
enum Register GetOp3Register(struct Instruction* i)
{
	return (enum Register)i->reg3;
}

void SetOp3Register(struct Instruction* i, enum Register value)
{
	i->reg3 = (unsigned char)value;
}

void SetInternalOp3Register(struct Instruction* i, enum Register value)
{
	i->reg3 = (unsigned char)value;
}

/// <summary>
/// Gets operand #4's register value. Use this property if operand #4 (<see cref="Op4Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
enum Register GetOp4Register(struct Instruction* i)
{
	return Register_None;
}

void SetOp4Register(struct Instruction* i, enum Register value)
{
	if (value != Register_None)
	{
		// ThrowHelper.ThrowArgumentOutOfRangeException_value();
	}
}

/// <summary>
/// Gets the operand's register value. Use this property if the operand has kind <see cref="OpKind.Register"/>
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <returns></returns>
enum Register GetOpRegister(struct Instruction* i, int operand)
{
	switch (operand) 
	{
	case 0: 
		return GetOp0Register(i);
	case 1: 
		return GetOp1Register(i);
	case 2: 
		return GetOp2Register(i);
	case 3: 
		return GetOp3Register(i);
	case 4: 
		return GetOp4Register(i);
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_operand();
		return 0;
	}
}

/// <summary>
/// Sets the operand's register value. Use this property if the operand has kind <see cref="OpKind.Register"/>
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="_register">Register</param>
void SetOpRegister(struct Instruction* i, int operand, enum Register _register) 
{
	switch (operand) 
	{
	case 0: 
		SetOp0Register(i, _register); 
		break;
	case 1: 
		SetOp1Register(i, _register); 
		break;
	case 2: 
		SetOp2Register(i, _register); 
		break;
	case 3: 
		SetOp3Register(i, _register); 
		break;
	case 4: 
		SetOp4Register(i, _register); 
		break;
	default: 
		//ThrowHelper.ThrowArgumentOutOfRangeException_operand(); 
		break;
	}
}

/// <summary>
/// Gets the opmask register (<see cref="Register.K1"/> - <see cref="Register.K7"/>) or <see cref="Register.None"/> if none
/// </summary>
enum Register GetOpMask(struct Instruction* i)
{
	int r = (int)(i->flags1 >> (int)IF_OpMaskShift) & (int)IF_OpMaskMask;
	return r == 0 ? Register_None : r + Register_K0;
}

void SetOpMask(struct Instruction* i, enum Register value)
{
	unsigned int r;
	if (value == Register_None)
	{
		r = 0;
	}
	else
	{
		r = (unsigned int)(value - Register_K0) & (unsigned int)IF_OpMaskMask;
	}
	i->flags1 = (i->flags1 & ~((unsigned int)IF_OpMaskMask << (int)IF_OpMaskShift)) 
		| (r << (int)IF_OpMaskShift);
}

unsigned int GetInternalOpMask(struct Instruction* i)
{
	return (i->flags1 >> (int)IF_OpMaskShift) & (unsigned int)IF_OpMaskMask;
}

void SetInternalOpMask(struct Instruction* i, unsigned int value)
{
	i->flags1 |= value << (int)IF_OpMaskShift;
}

/// <summary>
/// <see langword="true"/> if there's an opmask register (<see cref="OpMask"/>)
/// </summary>
bool HasOpMask(struct Instruction* i)
{
	return (i->flags1 & ((unsigned int)IF_OpMaskMask << (int)IF_OpMaskShift)) != 0;
}

bool HasOpMask_or_ZeroingMasking(struct Instruction* i)
{
	return (i->flags1 & (((unsigned int)IF_OpMaskMask << (int)IF_OpMaskShift) 
		| (unsigned int)IF_ZeroingMasking)) != 0;
}

/// <summary>
/// <see langword="true"/> if zeroing-masking, <see langword="false"/> if merging-masking.
/// Only used by most EVEX encoded instructions that use opmask registers.
/// </summary>
bool GetZeroingMasking(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_ZeroingMasking) != 0;
}

void SetZeroingMasking(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 |= (unsigned int)IF_ZeroingMasking;
	}
	else
	{
		i->flags1 &= ~(unsigned int)IF_ZeroingMasking;
	}
}

void InternalSetZeroingMasking(struct Instruction* i)
{
	i->flags1 |= (unsigned int)IF_ZeroingMasking;
}

/// <summary>
/// <see langword="true"/> if merging-masking, <see langword="false"/> if zeroing-masking.
/// Only used by most EVEX encoded instructions that use opmask registers.
/// </summary>
bool GetMergingMasking(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_ZeroingMasking) == 0;
}


void SetMergingMasking(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 &= ~(unsigned int)IF_ZeroingMasking;
	}
	else
	{
		i->flags1 |= (unsigned int)IF_ZeroingMasking;
	}
}

/// <summary>
/// Rounding control (SAE is implied but <see cref="SuppressAllExceptions"/> still returns <see langword="false"/>)
/// or <see cref="RoundingControl.None"/> if the instruction doesn't use it.
/// </summary>
enum RoundingControl GetRoundingControl(struct Instruction* i)
{
	return (enum RoundingControl)((i->flags1 >> (int)IF_RoundingControlShift) & (int)IF_RoundingControlMask);
}

void SetRoundingControl(struct Instruction* i, enum RoundingControl value)
{
	i->flags1 = (i->flags1 & ~((unsigned int)IF_RoundingControlMask << (int)IF_RoundingControlShift)) 
		| ((unsigned int)value << (int)IF_RoundingControlShift);
}

unsigned int SetInternalRoundingControl(struct Instruction* i, unsigned int value)
{
	i->flags1 |= value << (int)IF_RoundingControlShift;
}

bool HasRoundingControlOrSae(struct Instruction* i)
{
	return (i->flags1 & (((unsigned int)IF_RoundingControlMask << (int)IF_RoundingControlShift) | (unsigned int)IF_SuppressAllExceptions)) != 0;
}

/// <summary>
/// Number of elements in a db/dw/dd/dq directive: <c>db</c>: 1-16; <c>dw</c>: 1-8; <c>dd</c>: 1-4; <c>dq</c>: 1-2.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareByte"/>, <see cref="Code.DeclareWord"/>, <see cref="Code.DeclareDword"/>, <see cref="Code.DeclareQword"/>
/// </summary>
int GetDeclareDataCount(struct Instruction* i)
{
	return (int)((i->flags1 >> (int)IF_DataLengthShift) & (unsigned int)IF_DataLengthMask) + 1;
}

void SetDeclareDataCount(struct Instruction* i, int value)
{
	i->flags1 = (i->flags1 & ~((unsigned int)IF_DataLengthMask << (int)IF_DataLengthShift)) 
		| (((unsigned int)(value - 1) & (unsigned int)IF_DataLengthMask) << (int)IF_DataLengthShift);
}

void SetInternalDeclareDataCount(struct Instruction* i, unsigned int value)
{
	i->flags1 |= (value - 1) << (int)IF_DataLengthShift;
}

/// <summary>
/// Sets a new 'db' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareByte"/>
/// </summary>
/// <param name="index">Index (0-15)</param>
/// <param name="value">New value</param>
void SetDeclareByteValue(struct Instruction* i, int index, signed char value)
{
	SetDeclareByteValue(i, index, (unsigned char)value);
}

/// <summary>
/// Sets a new 'db' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareByte"/>
/// </summary>
/// <param name="index">Index (0-15)</param>
/// <param name="value">New value</param>
void SetDeclareByteValue(struct Instruction* i, int index, unsigned char value)
{
	switch (index) {
	case 0:
		i->reg0 = value;
		break;
	case 1:
		i->reg1 = value;
		break;
	case 2:
		i->reg2 = value;
		break;
	case 3:
		i->reg3 = value;
		break;
	case 4:
		i->immediate = (i->immediate & 0xFFFFFF00) | value;
		break;
	case 5:
		i->immediate = (i->immediate & 0xFFFF00FF) | ((unsigned int)value << 8);
		break;
	case 6:
		i->immediate = (i->immediate & 0xFF00FFFF) | ((unsigned int)value << 16);
		break;
	case 7:
		i->immediate = (i->immediate & 0x00FFFFFF) | ((unsigned int)value << 24);
		break;
	case 8:
		i->memDispl = (i->memDispl & 0xFFFFFFFFFFFFFF00) | (unsigned long)value;
		break;
	case 9:
		i->memDispl = (i->memDispl & 0xFFFFFFFFFFFF00FF) | ((unsigned long)value << 8);
		break;
	case 10:
		i->memDispl = (i->memDispl & 0xFFFFFFFFFF00FFFF) | ((unsigned long)value << 16);
		break;
	case 11:
		i->memDispl = (i->memDispl & 0xFFFFFFFF00FFFFFF) | ((unsigned long)value << 24);
		break;
	case 12:
		i->memDispl = (i->memDispl & 0xFFFFFF00FFFFFFFF) | ((unsigned long)value << 32);
		break;
	case 13:
		i->memDispl = (i->memDispl & 0xFFFF00FFFFFFFFFF) | ((unsigned long)value << 40);
		break;
	case 14:
		i->memDispl = (i->memDispl & 0xFF00FFFFFFFFFFFF) | ((unsigned long)value << 48);
		break;
	case 15:
		i->memDispl = (i->memDispl & 0x00FFFFFFFFFFFFFF) | ((unsigned long)value << 56);
		break;
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_index();
		break;
	}
}

/// <summary>
/// Gets a 'db' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareByte"/>
/// </summary>
/// <param name="index">Index (0-15)</param>
/// <returns></returns>
unsigned char GetDeclareByteValue(struct Instruction* i, int index)
{
	switch (index) 
	{
	case 0:		return i->reg0;
	case 1:		return i->reg1;
	case 2:		return i->reg2;
	case 3:		return i->reg3;
	case 4:		return (unsigned char)i->immediate;
	case 5:		return (unsigned char)(i->immediate >> 8);
	case 6:		return (unsigned char)(i->immediate >> 16);
	case 7:		return (unsigned char)(i->immediate >> 24);
	case 8:		return (unsigned char)i->memDispl;
	case 9:		return (unsigned char)((unsigned int)i->memDispl >> 8);
	case 10:	return (unsigned char)((unsigned int)i->memDispl >> 16);
	case 11:	return (unsigned char)((unsigned int)i->memDispl >> 24);
	case 12:	return (unsigned char)(i->memDispl >> 32);
	case 13:	return (unsigned char)(i->memDispl >> 40);
	case 14:	return (unsigned char)(i->memDispl >> 48);
	case 15:	return (unsigned char)(i->memDispl >> 56);
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_index();
		return 0;
	}
}

/// <summary>
/// Sets a new 'dw' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareWord"/>
/// </summary>
/// <param name="index">Index (0-7)</param>
/// <param name="value">New value</param>
void SetDeclareWordValue(struct Instruction* i, int index, short value)
{
	SetDeclareWordValue(i, index, (unsigned short)value);
}

/// <summary>
/// Sets a new 'dw' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareWord"/>
/// </summary>
/// <param name="index">Index (0-7)</param>
/// <param name="value">New value</param>
void SetDeclareWordValue(struct Instruction* i, int index, unsigned short value) 
{
	switch (index) 
	{
	case 0:
		i->reg0 = (unsigned char)value;
		i->reg1 = (unsigned char)(value >> 8);
		break;
	case 1:
		i->reg2 = (unsigned char)value;
		i->reg3 = (unsigned char)(value >> 8);
		break;
	case 2:
		i->immediate = (i->immediate & 0xFFFF0000) | value;
		break;
	case 3:
		i->immediate = (unsigned int)(unsigned short)i->immediate | ((unsigned int)value << 16);
		break;
	case 4:
		i->memDispl = (i->memDispl & 0xFFFFFFFFFFFF0000) | (unsigned long)value;
		break;
	case 5:
		i->memDispl = (i->memDispl & 0xFFFFFFFF0000FFFF) | ((unsigned long)value << 16);
		break;
	case 6:
		i->memDispl = (i->memDispl & 0xFFFF0000FFFFFFFF) | ((unsigned long)value << 32);
		break;
	case 7:
		i->memDispl = (i->memDispl & 0x0000FFFFFFFFFFFF) | ((unsigned long)value << 48);
		break;
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_index();
		break;
	}
}

/// <summary>
/// Gets a 'dw' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareWord"/>
/// </summary>
/// <param name="index">Index (0-7)</param>
/// <returns></returns>
unsigned short GetDeclareWordValue(struct Instruction* i, int index) 
{
	switch (index) 
	{
	case 0:	return (unsigned short)((unsigned int)i->reg0 | (unsigned int)(i->reg1 << 8));
	case 1:	return (unsigned short)((unsigned int)i->reg2 | (unsigned int)(i->reg3 << 8));
	case 2:	return (unsigned short)i->immediate;
	case 3:	return (unsigned short)(i->immediate >> 16);
	case 4:	return (unsigned short)i->memDispl;
	case 5:	return (unsigned short)((unsigned int)i->memDispl >> 16);
	case 6:	return (unsigned short)(i->memDispl >> 32);
	case 7:	return (unsigned short)(i->memDispl >> 48);
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_index();
		return 0;
	}
}

/// <summary>
/// Sets a new 'dd' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareDword"/>
/// </summary>
/// <param name="index">Index (0-3)</param>
/// <param name="value">New value</param>
void SetDeclareDwordValue(struct Instruction* i, int index, int value)
{
	SetDeclareDwordValue(i, index, (unsigned int)value);
}

/// <summary>
/// Sets a new 'dd' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareDword"/>
/// </summary>
/// <param name="index">Index (0-3)</param>
/// <param name="value">New value</param>
void SetDeclareDwordValue(struct Instruction* i, int index, unsigned int value) 
{
	switch (index) 
	{
	case 0:
		i->reg0 = (unsigned char)value;
		i->reg1 = (unsigned char)(value >> 8);
		i->reg2 = (unsigned char)(value >> 16);
		i->reg3 = (unsigned char)(value >> 24);
		break;
	case 1:
		i->immediate = value;
		break;
	case 2:
		i->memDispl = (i->memDispl & 0xFFFFFFFF00000000) | (unsigned long)value;
		break;
	case 3:
		i->memDispl = (i->memDispl & 0x00000000FFFFFFFF) | ((unsigned long)value << 32);
		break;
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_index();
		break;
	}
}

/// <summary>
/// Gets a 'dd' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareDword"/>
/// </summary>
/// <param name="index">Index (0-3)</param>
/// <returns></returns>
unsigned int GetDeclareDwordValue(struct Instruction* i, int index) 
{
	switch (index) 
	{
	case 0:	return (unsigned int)i->reg0 | (unsigned int)(i->reg1 << 8) | (unsigned int)(i->reg2 << 16) | (unsigned int)(i->reg3 << 24);
	case 1:	return i->immediate;
	case 2:	return (unsigned int)i->memDispl;
	case 3:	return (unsigned int)(i->memDispl >> 32);
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_index();
		return 0;
	}
}

/// <summary>
/// Sets a new 'dq' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareQword"/>
/// </summary>
/// <param name="index">Index (0-1)</param>
/// <param name="value">New value</param>
void SetDeclareQwordValue(struct Instruction* i, int index, long value)
{
	SetDeclareQwordValue(i, index, (unsigned long)value);
}

/// <summary>
/// Sets a new 'dq' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareQword"/>
/// </summary>
/// <param name="index">Index (0-1)</param>
/// <param name="value">New value</param>
void SetDeclareQwordValue(struct Instruction* i, int index, unsigned long value) 
{
	unsigned int v;
	switch (index) 
	{
	case 0:
		v = (unsigned int)value;
		i->reg0 = (unsigned char)v;
		i->reg1 = (unsigned char)(v >> 8);
		i->reg2 = (unsigned char)(v >> 16);
		i->reg3 = (unsigned char)(v >> 24);
		i->immediate = (unsigned int)(value >> 32);
		break;
	case 1:
		i->memDispl = value;
		break;
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_index();
		break;
	}
}

/// <summary>
/// Gets a 'dq' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareQword"/>
/// </summary>
/// <param name="index">Index (0-1)</param>
/// <returns></returns>
unsigned long GetDeclareQwordValue(struct Instruction* i, int index) 
{
	switch (index) 
	{
	case 0:	return (unsigned long)i->reg0 | (unsigned long)((unsigned int)i->reg1 << 8) | (unsigned long)((unsigned int)i->reg2 << 16) | (unsigned long)((unsigned int)i->reg3 << 24) | ((unsigned long)i->immediate << 32);
	case 1:	return i->memDispl;
	default:
		//ThrowHelper.ThrowArgumentOutOfRangeException_index();
		return 0;
	}
}

/// <summary>
/// Checks if this is a VSIB instruction, see also <see cref="IsVsib32"/>, <see cref="IsVsib64"/>
/// </summary>
bool IsVsib(struct Instruction* i) 
{
	bool vsib64 = false;
	return TryGetVsib64(i, &vsib64);
}

/// <summary>
/// VSIB instructions only (<see cref="IsVsib"/>): <see langword="true"/> if it's using 32-bit indexes, <see langword="false"/> if it's using 64-bit indexes
/// </summary>
bool IsVsib32(struct Instruction* i)
{
	bool vsib64 = false;
	return TryGetVsib64(i, &vsib64) && !vsib64;
}

/// <summary>
/// VSIB instructions only (<see cref="IsVsib"/>): <see langword="true"/> if it's using 64-bit indexes, <see langword="false"/> if it's using 32-bit indexes
/// </summary>
bool IsVsib64(struct Instruction* i) 
{
	bool vsib64 = false;
	return TryGetVsib64(i, &vsib64) && vsib64;
}

/// <summary>
/// Checks if it's a VSIB instruction. If it's a VSIB instruction, it sets <paramref name="vsib64"/> to <see langword="true"/> if it's
/// a VSIB instruction with 64-bit indexes, and clears it if it's using 32-bit indexes.
/// </summary>
/// <param name="vsib64">If it's a VSIB instruction, set to <see langword="true"/> if it's using 64-bit indexes, set to <see langword="false"/> if it's using 32-bit indexes</param>
/// <returns></returns>
bool TryGetVsib64(struct Instruction* i, bool* vsib64)
{
	switch (GetCode(i)) 
	{
		// GENERATOR-BEGIN: Vsib32
		// ⚠️This was generated by GENERATOR!🦹‍♂️
	case VEX_Vpgatherdd_xmm_vm32x_xmm:
	case VEX_Vpgatherdd_ymm_vm32y_ymm:
	case VEX_Vpgatherdq_xmm_vm32x_xmm:
	case VEX_Vpgatherdq_ymm_vm32x_ymm:
	case EVEX_Vpgatherdd_xmm_k1_vm32x:
	case EVEX_Vpgatherdd_ymm_k1_vm32y:
	case EVEX_Vpgatherdd_zmm_k1_vm32z:
	case EVEX_Vpgatherdq_xmm_k1_vm32x:
	case EVEX_Vpgatherdq_ymm_k1_vm32x:
	case EVEX_Vpgatherdq_zmm_k1_vm32y:
	case VEX_Vgatherdps_xmm_vm32x_xmm:
	case VEX_Vgatherdps_ymm_vm32y_ymm:
	case VEX_Vgatherdpd_xmm_vm32x_xmm:
	case VEX_Vgatherdpd_ymm_vm32x_ymm:
	case EVEX_Vgatherdps_xmm_k1_vm32x:
	case EVEX_Vgatherdps_ymm_k1_vm32y:
	case EVEX_Vgatherdps_zmm_k1_vm32z:
	case EVEX_Vgatherdpd_xmm_k1_vm32x:
	case EVEX_Vgatherdpd_ymm_k1_vm32x:
	case EVEX_Vgatherdpd_zmm_k1_vm32y:
	case EVEX_Vpscatterdd_vm32x_k1_xmm:
	case EVEX_Vpscatterdd_vm32y_k1_ymm:
	case EVEX_Vpscatterdd_vm32z_k1_zmm:
	case EVEX_Vpscatterdq_vm32x_k1_xmm:
	case EVEX_Vpscatterdq_vm32x_k1_ymm:
	case EVEX_Vpscatterdq_vm32y_k1_zmm:
	case EVEX_Vscatterdps_vm32x_k1_xmm:
	case EVEX_Vscatterdps_vm32y_k1_ymm:
	case EVEX_Vscatterdps_vm32z_k1_zmm:
	case EVEX_Vscatterdpd_vm32x_k1_xmm:
	case EVEX_Vscatterdpd_vm32x_k1_ymm:
	case EVEX_Vscatterdpd_vm32y_k1_zmm:
	case EVEX_Vgatherpf0dps_vm32z_k1:
	case EVEX_Vgatherpf0dpd_vm32y_k1:
	case EVEX_Vgatherpf1dps_vm32z_k1:
	case EVEX_Vgatherpf1dpd_vm32y_k1:
	case EVEX_Vscatterpf0dps_vm32z_k1:
	case EVEX_Vscatterpf0dpd_vm32y_k1:
	case EVEX_Vscatterpf1dps_vm32z_k1:
	case EVEX_Vscatterpf1dpd_vm32y_k1:
	case MVEX_Vpgatherdd_zmm_k1_mvt:
	case MVEX_Vpgatherdq_zmm_k1_mvt:
	case MVEX_Vgatherdps_zmm_k1_mvt:
	case MVEX_Vgatherdpd_zmm_k1_mvt:
	case MVEX_Vpscatterdd_mvt_k1_zmm:
	case MVEX_Vpscatterdq_mvt_k1_zmm:
	case MVEX_Vscatterdps_mvt_k1_zmm:
	case MVEX_Vscatterdpd_mvt_k1_zmm:
	case MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_B0:
	case MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_B2:
	case MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_C0:
	case MVEX_Vgatherpf0hintdps_mvt_k1:
	case MVEX_Vgatherpf0hintdpd_mvt_k1:
	case MVEX_Vgatherpf0dps_mvt_k1:
	case MVEX_Vgatherpf1dps_mvt_k1:
	case MVEX_Vscatterpf0hintdps_mvt_k1:
	case MVEX_Vscatterpf0hintdpd_mvt_k1:
	case MVEX_Vscatterpf0dps_mvt_k1:
	case MVEX_Vscatterpf1dps_mvt_k1:
		vsib64 = false;
		return true;
		// GENERATOR-END: Vsib32

		// GENERATOR-BEGIN: Vsib64
		// ⚠️This was generated by GENERATOR!🦹‍♂️
	case VEX_Vpgatherqd_xmm_vm64x_xmm:
	case VEX_Vpgatherqd_xmm_vm64y_xmm:
	case VEX_Vpgatherqq_xmm_vm64x_xmm:
	case VEX_Vpgatherqq_ymm_vm64y_ymm:
	case EVEX_Vpgatherqd_xmm_k1_vm64x:
	case EVEX_Vpgatherqd_xmm_k1_vm64y:
	case EVEX_Vpgatherqd_ymm_k1_vm64z:
	case EVEX_Vpgatherqq_xmm_k1_vm64x:
	case EVEX_Vpgatherqq_ymm_k1_vm64y:
	case EVEX_Vpgatherqq_zmm_k1_vm64z:
	case VEX_Vgatherqps_xmm_vm64x_xmm:
	case VEX_Vgatherqps_xmm_vm64y_xmm:
	case VEX_Vgatherqpd_xmm_vm64x_xmm:
	case VEX_Vgatherqpd_ymm_vm64y_ymm:
	case EVEX_Vgatherqps_xmm_k1_vm64x:
	case EVEX_Vgatherqps_xmm_k1_vm64y:
	case EVEX_Vgatherqps_ymm_k1_vm64z:
	case EVEX_Vgatherqpd_xmm_k1_vm64x:
	case EVEX_Vgatherqpd_ymm_k1_vm64y:
	case EVEX_Vgatherqpd_zmm_k1_vm64z:
	case EVEX_Vpscatterqd_vm64x_k1_xmm:
	case EVEX_Vpscatterqd_vm64y_k1_xmm:
	case EVEX_Vpscatterqd_vm64z_k1_ymm:
	case EVEX_Vpscatterqq_vm64x_k1_xmm:
	case EVEX_Vpscatterqq_vm64y_k1_ymm:
	case EVEX_Vpscatterqq_vm64z_k1_zmm:
	case EVEX_Vscatterqps_vm64x_k1_xmm:
	case EVEX_Vscatterqps_vm64y_k1_xmm:
	case EVEX_Vscatterqps_vm64z_k1_ymm:
	case EVEX_Vscatterqpd_vm64x_k1_xmm:
	case EVEX_Vscatterqpd_vm64y_k1_ymm:
	case EVEX_Vscatterqpd_vm64z_k1_zmm:
	case EVEX_Vgatherpf0qps_vm64z_k1:
	case EVEX_Vgatherpf0qpd_vm64z_k1:
	case EVEX_Vgatherpf1qps_vm64z_k1:
	case EVEX_Vgatherpf1qpd_vm64z_k1:
	case EVEX_Vscatterpf0qps_vm64z_k1:
	case EVEX_Vscatterpf0qpd_vm64z_k1:
	case EVEX_Vscatterpf1qps_vm64z_k1:
	case EVEX_Vscatterpf1qpd_vm64z_k1:
		vsib64 = true;
		return true;
		// GENERATOR-END: Vsib64

	default:
		vsib64 = false;
		return false;
	}
}

/// <summary>
/// Suppress all exceptions (EVEX/MVEX encoded instructions). Note that if <see cref="RoundingControl"/> is
/// not <see cref="RoundingControl.None"/>, SAE is implied but this property will still return <see langword="false"/>.
/// </summary>
bool GetSuppressAllExceptions(struct Instruction* i)
{
	return (i->flags1 & (unsigned int)IF_SuppressAllExceptions) != 0;
}

void SetSuppressAllExceptions(struct Instruction* i, bool value)
{
	if (value)
	{
		i->flags1 |= (unsigned int)IF_SuppressAllExceptions;
	}
	else
	{
		i->flags1 &= ~(unsigned int)IF_SuppressAllExceptions;
	}
}

void InternalSetSuppressAllExceptions(struct Instruction* i)
{
	i->flags1 |= (unsigned int)IF_SuppressAllExceptions;
}

/// <summary>
/// Checks if the memory operand is RIP/EIP relative
/// </summary>
bool IsIPRelativeMemoryOperand(struct Instruction* i)
{
	return GetMemoryBase(i) == Register_RIP || GetMemoryBase(i) == Register_EIP;
}

/// <summary>
/// Gets the <c>RIP</c>/<c>EIP</c> releative address (<see cref="MemoryDisplacement32"/> or <see cref="MemoryDisplacement64"/>).
/// This property is only valid if there's a memory operand with <c>RIP</c>/<c>EIP</c> relative addressing, see <see cref="IsIPRelativeMemoryOperand"/>
/// </summary>
unsigned long IPRelativeMemoryAddress(struct Instruction* i)
{
	return GetMemoryBase(i) == Register_EIP ? GetMemoryDisplacement32(i) : GetMemoryDisplacement64(i);
}

struct OpCodeInfo* ToOpCode(enum Code code) 
{
	//var infos = OpCodeInfos.Infos;
	//if ((unsigned int)code >= (unsigned int)infos.Length)
	//{
		//ThrowHelper.ThrowArgumentOutOfRangeException_code();
	//}

	int i = (int)code;

	StringBuilder* sb = sb_create();

	auto encFlags1 = EncoderData_EncFlags1;
	auto encFlags2 = EncoderData_EncFlags2;
	auto encFlags3 = EncoderData_EncFlags3;
	auto opcFlags1 = OpCodeInfoData_OpcFlags1;
	auto opcFlags2 = OpCodeInfoData_OpcFlags2;

	struct OpCodeInfo* o = OpCodeInfo_new();

	OpCodeInfo_init(o, code, (enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], (enum EncFlags3)encFlags3[i], (enum OpCodeInfoFlags1)opcFlags1[i], (enum OpCodeInfoFlags2)opcFlags2[i], sb);


	return o;
}

//#if ENCODER && OPCODE_INFO
/// <summary>
/// Gets the <see cref="OpCodeInfo"/>
/// </summary>
/// <returns></returns>
struct OpCodeInfo* OpCode(struct Instruction* i)
{
	return ToOpCode(GetCode(i));
}
//#endif

///// <summary>
///// Formats the instruction using the default formatter with default formatter options
///// </summary>
///// <returns></returns>
//public override readonly string ToString() {
//	// If the order of #if/elif checks gets updated, also update the `Instruction_ToString()` test method
//#if MASM
//	var output = new StringOutput();
//	new MasmFormatter().Format(this, output);
//	return output.ToString();
//#elif NASM
//	var output = new StringOutput();
//	new NasmFormatter().Format(this, output);
//	return output.ToString();
//#elif INTEL
//	var output = new StringOutput();
//	new IntelFormatter().Format(this, output);
//	return output.ToString();
//#elif GAS
//	var output = new StringOutput();
//	new GasFormatter().Format(this, output);
//	return output.ToString();
//#elif FAST_FMT
//	var output = new FastStringOutput();
//	new FastFormatter().Format(this, output);
//	return output.ToString();
//#else
//	return base.ToString() ? ? string.Empty;
//#endif
//}