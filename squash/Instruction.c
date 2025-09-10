#include "Instruction.h"

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
		ThrowArgumentOutOfRangeException_value();
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
	return OpCount[(int)i->code];
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
void GetOp2Kind(struct Instruction* i, enum OpKind value)
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
void GetOp3Kind(struct Instruction* i, enum OpKind value)
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
void GetOp4Kind(struct Instruction* i, enum OpKind value)
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
enum OpKind GetOpKind(struct Instruction* i, int operand)
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

bool IsXacquireInstr(struct Instruction* i) 
{
	if (GetOp0Kind(i) != OK_Memory)
	{
		return false;
	}
		
	if (HasLockPrefix)
		return Code != Code.Cmpxchg16b_m128;
	return Mnemonic == Mnemonic.Xchg;
}

readonly bool IsXreleaseInstr() {
	if (Op0Kind != OpKind.Memory)
		return false;
	if (HasLockPrefix)
		return Code != Code.Cmpxchg16b_m128;
	switch (Code) {
	case Code.Xchg_rm8_r8:
	case Code.Xchg_rm16_r16:
	case Code.Xchg_rm32_r32:
	case Code.Xchg_rm64_r64:
	case Code.Mov_rm8_r8:
	case Code.Mov_rm16_r16:
	case Code.Mov_rm32_r32:
	case Code.Mov_rm64_r64:
	case Code.Mov_rm8_imm8:
	case Code.Mov_rm16_imm16:
	case Code.Mov_rm32_imm32:
	case Code.Mov_rm64_imm32:
		return true;
	default:
		return false;
	}
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>XACQUIRE</c> prefix (<c>F2</c>)
/// </summary>
public bool HasXacquirePrefix{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.RepnePrefix) != 0 && IsXacquireInstr();
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.RepnePrefix;
		else
			flags1 &= ~(uint)InstrFlags1.RepnePrefix;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetHasXacquirePrefix() = > flags1 |= (uint)InstrFlags1.RepnePrefix;

/// <summary>
/// <see langword="true"/> if the instruction has the <c>XRELEASE</c> prefix (<c>F3</c>)
/// </summary>
public bool HasXreleasePrefix{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.RepePrefix) != 0 && IsXreleaseInstr();
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.RepePrefix;
		else
			flags1 &= ~(uint)InstrFlags1.RepePrefix;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetHasXreleasePrefix() = > flags1 |= (uint)InstrFlags1.RepePrefix;

/// <summary>
/// <see langword="true"/> if the instruction has the <c>REPE</c> or <c>REP</c> prefix (<c>F3</c>)
/// </summary>
public bool HasRepPrefix{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.RepePrefix) != 0;
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.RepePrefix;
		else
			flags1 &= ~(uint)InstrFlags1.RepePrefix;
	}
}

/// <summary>
/// <see langword="true"/> if the instruction has the <c>REPE</c> or <c>REP</c> prefix (<c>F3</c>)
/// </summary>
public bool HasRepePrefix{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.RepePrefix) != 0;
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.RepePrefix;
		else
			flags1 &= ~(uint)InstrFlags1.RepePrefix;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetHasRepePrefix() = > flags1 = (flags1 & ~(uint)InstrFlags1.RepnePrefix) | (uint)InstrFlags1.RepePrefix;
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalClearHasRepePrefix() = > flags1 &= ~(uint)InstrFlags1.RepePrefix;
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalClearHasRepeRepnePrefix() = > flags1 &= ~((uint)InstrFlags1.RepePrefix | (uint)InstrFlags1.RepnePrefix);

/// <summary>
/// <see langword="true"/> if the instruction has the <c>REPNE</c> prefix (<c>F2</c>)
/// </summary>
public bool HasRepnePrefix{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.RepnePrefix) != 0;
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.RepnePrefix;
		else
			flags1 &= ~(uint)InstrFlags1.RepnePrefix;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetHasRepnePrefix() = > flags1 = (flags1 & ~(uint)InstrFlags1.RepePrefix) | (uint)InstrFlags1.RepnePrefix;
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalClearHasRepnePrefix() = > flags1 &= ~(uint)InstrFlags1.RepnePrefix;

/// <summary>
/// <see langword="true"/> if the instruction has the <c>LOCK</c> prefix (<c>F0</c>)
/// </summary>
public bool HasLockPrefix{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.LockPrefix) != 0;
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.LockPrefix;
		else
			flags1 &= ~(uint)InstrFlags1.LockPrefix;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetHasLockPrefix() = > flags1 |= (uint)InstrFlags1.LockPrefix;
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalClearHasLockPrefix() = > flags1 &= ~(uint)InstrFlags1.LockPrefix;



/// <summary>
/// Checks if the instruction has a segment override prefix, see <see cref="SegmentPrefix"/>
/// </summary>
public readonly bool HasSegmentPrefix{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	get = > (((flags1 >> (int)InstrFlags1.SegmentPrefixShift) & (uint)InstrFlags1.SegmentPrefixMask) - 1) < 6;
}

/// <summary>
/// Gets the segment override prefix or <see cref="Register.None"/> if none. See also <see cref="MemorySegment"/>.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>,
/// <see cref="OpKind.MemorySegSI"/>, <see cref="OpKind.MemorySegESI"/>, <see cref="OpKind.MemorySegRSI"/>
/// </summary>
public Register SegmentPrefix{
	readonly get {
		uint index = ((flags1 >> (int)InstrFlags1.SegmentPrefixShift) & (uint)InstrFlags1.SegmentPrefixMask) - 1;
		return index < 6 ? Register.ES + (int)index : Register.None;
	}
	set {
		uint encValue;
		if (value == Register.None)
			encValue = 0;
		else
			encValue = (((uint)value - (uint)Register.ES) + 1) & (uint)InstrFlags1.SegmentPrefixMask;
		flags1 = (flags1 & ~((uint)InstrFlags1.SegmentPrefixMask << (int)InstrFlags1.SegmentPrefixShift)) |
			(encValue << (int)InstrFlags1.SegmentPrefixShift);
	}
}

/// <summary>
/// Gets the effective segment register used to reference the memory location.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>,
/// <see cref="OpKind.MemorySegSI"/>, <see cref="OpKind.MemorySegESI"/>, <see cref="OpKind.MemorySegRSI"/>
/// </summary>
public readonly Register MemorySegment{
	get {
		var segReg = SegmentPrefix;
		if (segReg != Register.None)
			return segReg;
		var baseReg = MemoryBase;
		if (baseReg == Register.BP || baseReg == Register.EBP || baseReg == Register.ESP || baseReg == Register.RBP || baseReg == Register.RSP)
			return Register.SS;
		return Register.DS;
	}
}

/// <summary>
/// Gets the size of the memory displacement in bytes. Valid values are <c>0</c>, <c>1</c> (16/32/64-bit), <c>2</c> (16-bit), <c>4</c> (32-bit), <c>8</c> (64-bit).
/// Note that the return value can be 1 and <see cref="MemoryDisplacement64"/> may still not fit in
/// a signed byte if it's an EVEX/MVEX encoded instruction.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
public int MemoryDisplSize{
	readonly get = >
		displSize switch {
			0 = > 0,
			1 = > 1,
			2 = > 2,
			3 = > 4,
			_ = > 8,
		};
	set {
		displSize = value switch {
			0 = > 0,
			1 = > 1,
			2 = > 2,
			4 = > 3,
			_ = > 4,
		};
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetMemoryDisplSize(uint scale) {
	Debug.Assert(0 <= scale && scale <= 4);
	displSize = (byte)scale;
}

/// <summary>
/// <see langword="true"/> if the data is broadcast (EVEX instructions only)
/// </summary>
public bool IsBroadcast{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.Broadcast) != 0;
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.Broadcast;
		else
			flags1 &= ~(uint)InstrFlags1.Broadcast;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetIsBroadcast() = > flags1 |= (uint)InstrFlags1.Broadcast;

#if MVEX
/// <summary>
/// <see langword="true"/> if eviction hint bit is set (<c>{eh}</c>) (MVEX instructions only)
/// </summary>
public bool IsMvexEvictionHint{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > IcedConstants.IsMvex(Code) && (immediate & (uint)MvexInstrFlags.EvictionHint) != 0;
	set {
		if (value)
			immediate |= (uint)MvexInstrFlags.EvictionHint;
		else
			immediate &= ~(uint)MvexInstrFlags.EvictionHint;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetIsMvexEvictionHint() = > immediate |= (uint)MvexInstrFlags.EvictionHint;
#endif

#if MVEX
/// <summary>
/// (MVEX) Register/memory operand conversion function
/// </summary>
public MvexRegMemConv MvexRegMemConv{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get {
		if (!IcedConstants.IsMvex(Code))
			return MvexRegMemConv.None;
		return (MvexRegMemConv)((immediate >> (int)MvexInstrFlags.MvexRegMemConvShift) & (uint)MvexInstrFlags.MvexRegMemConvMask);
	}
	set {
		immediate = (immediate & ~((uint)MvexInstrFlags.MvexRegMemConvMask << (int)MvexInstrFlags.MvexRegMemConvShift)) |
			((uint)value << (int)MvexInstrFlags.MvexRegMemConvShift);
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetMvexRegMemConv(MvexRegMemConv newValue) = >
immediate |= ((uint)newValue << (int)MvexInstrFlags.MvexRegMemConvShift);
#endif

/// <summary>
/// Gets the size of the memory location that is referenced by the operand. See also <see cref="IsBroadcast"/>.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>,
/// <see cref="OpKind.MemorySegSI"/>, <see cref="OpKind.MemorySegESI"/>, <see cref="OpKind.MemorySegRSI"/>,
/// <see cref="OpKind.MemoryESDI"/>, <see cref="OpKind.MemoryESEDI"/>, <see cref="OpKind.MemoryESRDI"/>
/// </summary>
public readonly MemorySize MemorySize{
	get {
		int index = (int)Code;
#if MVEX
				if (IcedConstants.IsMvex((Code)index)) {
					var mvex = new MvexInfo((Code)index);
					int sss = ((int)MvexRegMemConv - (int)MvexRegMemConv.MemConvNone) & 7;
					return (MemorySize)MvexMemorySizeLut.Data[(int)mvex.TupleTypeLutKind * 8 + sss];
				}
#endif
				if (IsBroadcast)
					return (MemorySize)InstructionMemorySizes.SizesBcst[index];
				else
					return (MemorySize)InstructionMemorySizes.SizesNormal[index];
			}
}

/// <summary>
/// Gets the index register scale value, valid values are <c>*1</c>, <c>*2</c>, <c>*4</c>, <c>*8</c>. Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
public int MemoryIndexScale{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > 1 << (int)scale;
	set {
		if (value == 1)
			scale = 0;
		else if (value == 2)
			scale = 1;
		else if (value == 4)
			scale = 2;
		else {
			Debug.Assert(value == 8);
			scale = 3;
		}
	}
}
internal int InternalMemoryIndexScale{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > scale;
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	set = > scale = (byte)value;
}

/// <summary>
/// Gets the memory operand's displacement or the 32-bit absolute address if it's
/// an <c>EIP</c> or <c>RIP</c> relative memory operand.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
public uint MemoryDisplacement32{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (uint)memDispl;
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	set = > memDispl = value;
}

/// <summary>
/// Gets the memory operand's displacement or the 64-bit absolute address if it's
/// an <c>EIP</c> or <c>RIP</c> relative memory operand.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
public ulong MemoryDisplacement64{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > memDispl;
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	set = > memDispl = value;
}

/// <summary>
/// Gets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <returns></returns>
public readonly ulong GetImmediate(int operand) = >
GetOpKind(operand) switch {
	OpKind.Immediate8 = > Immediate8,
		OpKind.Immediate8_2nd = > Immediate8_2nd,
		OpKind.Immediate16 = > Immediate16,
		OpKind.Immediate32 = > Immediate32,
		OpKind.Immediate64 = > Immediate64,
		OpKind.Immediate8to16 = > (ulong)Immediate8to16,
		OpKind.Immediate8to32 = > (ulong)Immediate8to32,
		OpKind.Immediate8to64 = > (ulong)Immediate8to64,
		OpKind.Immediate32to64 = > (ulong)Immediate32to64,
		_ = > throw new ArgumentException($"Op{operand} isn't an immediate operand", nameof(operand)),
};

/// <summary>
/// Sets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="immediate">New immediate</param>
/// <returns></returns>
public void SetImmediate(int operand, int immediate) = > SetImmediate(operand, (ulong)immediate);

/// <summary>
/// Sets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="immediate">New immediate</param>
/// <returns></returns>
public void SetImmediate(int operand, uint immediate) = > SetImmediate(operand, (ulong)immediate);

/// <summary>
/// Sets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="immediate">New immediate</param>
/// <returns></returns>
public void SetImmediate(int operand, long immediate) = > SetImmediate(operand, (ulong)immediate);

/// <summary>
/// Sets an operand's immediate value
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="immediate">New immediate</param>
/// <returns></returns>
public void SetImmediate(int operand, ulong immediate) {
	switch (GetOpKind(operand)) {
	case OpKind.Immediate8:
		Immediate8 = (byte)immediate;
		break;
	case OpKind.Immediate8to16:
		Immediate8to16 = (short)immediate;
		break;
	case OpKind.Immediate8to32:
		Immediate8to32 = (int)immediate;
		break;
	case OpKind.Immediate8to64:
		Immediate8to64 = (long)immediate;
		break;
	case OpKind.Immediate8_2nd:
		Immediate8_2nd = (byte)immediate;
		break;
	case OpKind.Immediate16:
		Immediate16 = (ushort)immediate;
		break;
	case OpKind.Immediate32to64:
		Immediate32to64 = (long)immediate;
		break;
	case OpKind.Immediate32:
		Immediate32 = (uint)immediate;
		break;
	case OpKind.Immediate64:
		Immediate64 = immediate;
		break;
	default:
		throw new ArgumentException($"Op{operand} isn't an immediate operand", nameof(operand));
	}
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8"/>
/// </summary>
public byte Immediate8{
	readonly get = > (byte)immediate;
#if MVEX
			set = > immediate = (immediate & 0xFFFF_FF00) | (uint)value;
#else
			set = > immediate = value;
#endif
}
internal uint InternalImmediate8{
	set = > immediate = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8_2nd"/>
/// </summary>
public byte Immediate8_2nd{
	readonly get = > (byte)memDispl;
	set = > memDispl = value;
}
internal uint InternalImmediate8_2nd{
	set = > memDispl = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate16"/>
/// </summary>
public ushort Immediate16{
	readonly get = > (ushort)immediate;
	set = > immediate = value;
}
internal uint InternalImmediate16{
	set = > immediate = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate32"/>
/// </summary>
public uint Immediate32{
	readonly get = > immediate;
	set = > immediate = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate64"/>
/// </summary>
public ulong Immediate64{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (memDispl << 32) | immediate;
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	set {
		immediate = (uint)value;
		memDispl = (uint)(value >> 32);
	}
}
internal uint InternalImmediate64_lo{
	set = > immediate = value;
}
internal uint InternalImmediate64_hi{
	set = > memDispl = value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8to16"/>
/// </summary>
public short Immediate8to16{
	readonly get = > (sbyte)immediate;
	set = > immediate = (uint)(sbyte)value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8to32"/>
/// </summary>
public int Immediate8to32{
	readonly get = > (sbyte)immediate;
	set = > immediate = (uint)(sbyte)value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8to64"/>
/// </summary>
public long Immediate8to64{
	readonly get = > (sbyte)immediate;
	set = > immediate = (uint)(sbyte)value;
}

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate32to64"/>
/// </summary>
public long Immediate32to64{
	readonly get = > (int)immediate;
	set = > immediate = (uint)value;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.NearBranch16"/>
/// </summary>
public ushort NearBranch16{
	readonly get = > (ushort)memDispl;
	set = > memDispl = value;
}
internal uint InternalNearBranch16{
	set = > memDispl = value;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.NearBranch32"/>
/// </summary>
public uint NearBranch32{
	readonly get = > (uint)memDispl;
	set = > memDispl = value;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.NearBranch64"/>
/// </summary>
public ulong NearBranch64{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > memDispl;
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	set = > memDispl = value;
}

/// <summary>
/// Gets the near branch target if it's a <c>CALL</c>/<c>JMP</c>/<c>Jcc</c> near branch instruction
/// (i.e., if <see cref="Op0Kind"/> is <see cref="OpKind.NearBranch16"/>, <see cref="OpKind.NearBranch32"/> or <see cref="OpKind.NearBranch64"/>)
/// </summary>
public readonly ulong NearBranchTarget{
	get {
		var opKind = Op0Kind;
#if MVEX
// Check if JKZD/JKNZD
if (OpCount == 2)
	opKind = Op1Kind;
#endif
				return opKind switch {
					OpKind.NearBranch16 = > NearBranch16,
					OpKind.NearBranch32 = > NearBranch32,
					OpKind.NearBranch64 = > NearBranch64,
					_ = > 0,
				};
			}
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.FarBranch16"/>
/// </summary>
public ushort FarBranch16{
	readonly get = > (ushort)immediate;
	set = > immediate = value;
}
internal uint InternalFarBranch16{
	set = > immediate = value;
}

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.FarBranch32"/>
/// </summary>
public uint FarBranch32{
	readonly get = > immediate;
	set = > immediate = value;
}

/// <summary>
/// Gets the operand's branch target selector. Use this property if the operand has kind <see cref="OpKind.FarBranch16"/> or <see cref="OpKind.FarBranch32"/>
/// </summary>
public ushort FarBranchSelector{
	readonly get = > (ushort)memDispl;
	set = > memDispl = value;
}
internal uint InternalFarBranchSelector{
	set = > memDispl = value;
}

/// <summary>
/// Gets the memory operand's base register or <see cref="Register.None"/> if none. Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
public Register MemoryBase{
	readonly get = > (Register)memBaseReg;
	set = > memBaseReg = (byte)value;
}
internal Register InternalMemoryBase{
	set = > memBaseReg = (byte)value;
}

/// <summary>
/// Gets the memory operand's index register or <see cref="Register.None"/> if none. Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
public Register MemoryIndex{
	readonly get = > (Register)memIndexReg;
	set = > memIndexReg = (byte)value;
}
internal Register InternalMemoryIndex{
	set = > memIndexReg = (byte)value;
}

/// <summary>
/// Gets operand #0's register value. Use this property if operand #0 (<see cref="Op0Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
public Register Op0Register{
	readonly get = > (Register)reg0;
	set = > reg0 = (byte)value;
}
internal Register InternalOp0Register{
	set = > reg0 = (byte)value;
}

/// <summary>
/// Gets operand #1's register value. Use this property if operand #1 (<see cref="Op1Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
public Register Op1Register{
	readonly get = > (Register)reg1;
	set = > reg1 = (byte)value;
}
internal Register InternalOp1Register{
	set = > reg1 = (byte)value;
}

/// <summary>
/// Gets operand #2's register value. Use this property if operand #2 (<see cref="Op2Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
public Register Op2Register{
	readonly get = > (Register)reg2;
	set = > reg2 = (byte)value;
}
internal Register InternalOp2Register{
	set = > reg2 = (byte)value;
}

/// <summary>
/// Gets operand #3's register value. Use this property if operand #3 (<see cref="Op3Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
public Register Op3Register{
	readonly get = > (Register)reg3;
	set = > reg3 = (byte)value;
}
internal Register InternalOp3Register{
	set = > reg3 = (byte)value;
}

/// <summary>
/// Gets operand #4's register value. Use this property if operand #4 (<see cref="Op4Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
public Register Op4Register{
	readonly get = > Register.None;
	set {
		if (value != Register.None)
			ThrowHelper.ThrowArgumentOutOfRangeException_value();
	}
}

/// <summary>
/// Gets the operand's register value. Use this property if the operand has kind <see cref="OpKind.Register"/>
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <returns></returns>
public readonly Register GetOpRegister(int operand) {
	switch (operand) {
	case 0: return Op0Register;
	case 1: return Op1Register;
	case 2: return Op2Register;
	case 3: return Op3Register;
	case 4: return Op4Register;
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_operand();
		return 0;
	}
}

/// <summary>
/// Sets the operand's register value. Use this property if the operand has kind <see cref="OpKind.Register"/>
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="register">Register</param>
public void SetOpRegister(int operand, Register register) {
	switch (operand) {
	case 0: Op0Register = register; break;
	case 1: Op1Register = register; break;
	case 2: Op2Register = register; break;
	case 3: Op3Register = register; break;
	case 4: Op4Register = register; break;
	default: ThrowHelper.ThrowArgumentOutOfRangeException_operand(); break;
	}
}

/// <summary>
/// Gets the opmask register (<see cref="Register.K1"/> - <see cref="Register.K7"/>) or <see cref="Register.None"/> if none
/// </summary>
public Register OpMask{
	readonly get {
		int r = (int)(flags1 >> (int)InstrFlags1.OpMaskShift) & (int)InstrFlags1.OpMaskMask;
		return r == 0 ? Register.None : r + Register.K0;
	}
	set {
		uint r;
		if (value == Register.None)
			r = 0;
		else
			r = (uint)(value - Register.K0) & (uint)InstrFlags1.OpMaskMask;
		flags1 = (flags1 & ~((uint)InstrFlags1.OpMaskMask << (int)InstrFlags1.OpMaskShift)) |
				(r << (int)InstrFlags1.OpMaskShift);
	}
}
internal uint InternalOpMask{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 >> (int)InstrFlags1.OpMaskShift) & (uint)InstrFlags1.OpMaskMask;
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	set = > flags1 |= value << (int)InstrFlags1.OpMaskShift;
}

/// <summary>
/// <see langword="true"/> if there's an opmask register (<see cref="OpMask"/>)
/// </summary>
public readonly bool HasOpMask{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	get = > (flags1 & ((uint)InstrFlags1.OpMaskMask << (int)InstrFlags1.OpMaskShift)) != 0;
}

internal readonly bool HasOpMask_or_ZeroingMasking{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	get = > (flags1 & (((uint)InstrFlags1.OpMaskMask << (int)InstrFlags1.OpMaskShift) | (uint)InstrFlags1.ZeroingMasking)) != 0;
}

/// <summary>
/// <see langword="true"/> if zeroing-masking, <see langword="false"/> if merging-masking.
/// Only used by most EVEX encoded instructions that use opmask registers.
/// </summary>
public bool ZeroingMasking{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.ZeroingMasking) != 0;
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.ZeroingMasking;
		else
			flags1 &= ~(uint)InstrFlags1.ZeroingMasking;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetZeroingMasking() = > flags1 |= (uint)InstrFlags1.ZeroingMasking;

/// <summary>
/// <see langword="true"/> if merging-masking, <see langword="false"/> if zeroing-masking.
/// Only used by most EVEX encoded instructions that use opmask registers.
/// </summary>
public bool MergingMasking{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.ZeroingMasking) == 0;
	set {
		if (value)
			flags1 &= ~(uint)InstrFlags1.ZeroingMasking;
		else
			flags1 |= (uint)InstrFlags1.ZeroingMasking;
	}
}

/// <summary>
/// Rounding control (SAE is implied but <see cref="SuppressAllExceptions"/> still returns <see langword="false"/>)
/// or <see cref="RoundingControl.None"/> if the instruction doesn't use it.
/// </summary>
public RoundingControl RoundingControl{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (RoundingControl)((flags1 >> (int)InstrFlags1.RoundingControlShift) & (int)InstrFlags1.RoundingControlMask);
	set = > flags1 = (flags1 & ~((uint)InstrFlags1.RoundingControlMask << (int)InstrFlags1.RoundingControlShift)) |
		((uint)value << (int)InstrFlags1.RoundingControlShift);
}
internal uint InternalRoundingControl{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	set = > flags1 |= value << (int)InstrFlags1.RoundingControlShift;
}

internal readonly bool HasRoundingControlOrSae{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	get = > (flags1 & (((uint)InstrFlags1.RoundingControlMask << (int)InstrFlags1.RoundingControlShift) | (uint)InstrFlags1.SuppressAllExceptions)) != 0;
}

/// <summary>
/// Number of elements in a db/dw/dd/dq directive: <c>db</c>: 1-16; <c>dw</c>: 1-8; <c>dd</c>: 1-4; <c>dq</c>: 1-2.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareByte"/>, <see cref="Code.DeclareWord"/>, <see cref="Code.DeclareDword"/>, <see cref="Code.DeclareQword"/>
/// </summary>
public int DeclareDataCount{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (int)((flags1 >> (int)InstrFlags1.DataLengthShift) & (uint)InstrFlags1.DataLengthMask) + 1;
	set = > flags1 = (flags1 & ~((uint)InstrFlags1.DataLengthMask << (int)InstrFlags1.DataLengthShift)) |
			(((uint)(value - 1) & (uint)InstrFlags1.DataLengthMask) << (int)InstrFlags1.DataLengthShift);
}
internal uint InternalDeclareDataCount{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	set = > flags1 |= (value - 1) << (int)InstrFlags1.DataLengthShift;
}

/// <summary>
/// Sets a new 'db' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareByte"/>
/// </summary>
/// <param name="index">Index (0-15)</param>
/// <param name="value">New value</param>
public void SetDeclareByteValue(int index, sbyte value) = > SetDeclareByteValue(index, (byte)value);

/// <summary>
/// Sets a new 'db' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareByte"/>
/// </summary>
/// <param name="index">Index (0-15)</param>
/// <param name="value">New value</param>
public void SetDeclareByteValue(int index, byte value) {
	switch (index) {
	case 0:
		reg0 = value;
		break;
	case 1:
		reg1 = value;
		break;
	case 2:
		reg2 = value;
		break;
	case 3:
		reg3 = value;
		break;
	case 4:
		immediate = (immediate & 0xFFFFFF00) | value;
		break;
	case 5:
		immediate = (immediate & 0xFFFF00FF) | ((uint)value << 8);
		break;
	case 6:
		immediate = (immediate & 0xFF00FFFF) | ((uint)value << 16);
		break;
	case 7:
		immediate = (immediate & 0x00FFFFFF) | ((uint)value << 24);
		break;
	case 8:
		memDispl = (memDispl & 0xFFFF_FFFF_FFFF_FF00) | (ulong)value;
		break;
	case 9:
		memDispl = (memDispl & 0xFFFF_FFFF_FFFF_00FF) | ((ulong)value << 8);
		break;
	case 10:
		memDispl = (memDispl & 0xFFFF_FFFF_FF00_FFFF) | ((ulong)value << 16);
		break;
	case 11:
		memDispl = (memDispl & 0xFFFF_FFFF_00FF_FFFF) | ((ulong)value << 24);
		break;
	case 12:
		memDispl = (memDispl & 0xFFFF_FF00_FFFF_FFFF) | ((ulong)value << 32);
		break;
	case 13:
		memDispl = (memDispl & 0xFFFF_00FF_FFFF_FFFF) | ((ulong)value << 40);
		break;
	case 14:
		memDispl = (memDispl & 0xFF00_FFFF_FFFF_FFFF) | ((ulong)value << 48);
		break;
	case 15:
		memDispl = (memDispl & 0x00FF_FFFF_FFFF_FFFF) | ((ulong)value << 56);
		break;
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
		break;
	}
}

/// <summary>
/// Gets a 'db' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareByte"/>
/// </summary>
/// <param name="index">Index (0-15)</param>
/// <returns></returns>
public readonly byte GetDeclareByteValue(int index) {
	switch (index) {
	case 0:		return reg0;
	case 1:		return reg1;
	case 2:		return reg2;
	case 3:		return reg3;
	case 4:		return (byte)immediate;
	case 5:		return (byte)(immediate >> 8);
	case 6:		return (byte)(immediate >> 16);
	case 7:		return (byte)(immediate >> 24);
	case 8:		return (byte)memDispl;
	case 9:		return (byte)((uint)memDispl >> 8);
	case 10:	return (byte)((uint)memDispl >> 16);
	case 11:	return (byte)((uint)memDispl >> 24);
	case 12:	return (byte)(memDispl >> 32);
	case 13:	return (byte)(memDispl >> 40);
	case 14:	return (byte)(memDispl >> 48);
	case 15:	return (byte)(memDispl >> 56);
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
		return 0;
	}
}

/// <summary>
/// Sets a new 'dw' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareWord"/>
/// </summary>
/// <param name="index">Index (0-7)</param>
/// <param name="value">New value</param>
public void SetDeclareWordValue(int index, short value) = > SetDeclareWordValue(index, (ushort)value);

/// <summary>
/// Sets a new 'dw' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareWord"/>
/// </summary>
/// <param name="index">Index (0-7)</param>
/// <param name="value">New value</param>
public void SetDeclareWordValue(int index, ushort value) {
	switch (index) {
	case 0:
		reg0 = (byte)value;
		reg1 = (byte)(value >> 8);
		break;
	case 1:
		reg2 = (byte)value;
		reg3 = (byte)(value >> 8);
		break;
	case 2:
		immediate = (immediate & 0xFFFF0000) | value;
		break;
	case 3:
		immediate = (uint)(ushort)immediate | ((uint)value << 16);
		break;
	case 4:
		memDispl = (memDispl & 0xFFFF_FFFF_FFFF_0000) | (ulong)value;
		break;
	case 5:
		memDispl = (memDispl & 0xFFFF_FFFF_0000_FFFF) | ((ulong)value << 16);
		break;
	case 6:
		memDispl = (memDispl & 0xFFFF_0000_FFFF_FFFF) | ((ulong)value << 32);
		break;
	case 7:
		memDispl = (memDispl & 0x0000_FFFF_FFFF_FFFF) | ((ulong)value << 48);
		break;
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
		break;
	}
}

/// <summary>
/// Gets a 'dw' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareWord"/>
/// </summary>
/// <param name="index">Index (0-7)</param>
/// <returns></returns>
public readonly ushort GetDeclareWordValue(int index) {
	switch (index) {
	case 0:	return (ushort)((uint)reg0 | (uint)(reg1 << 8));
	case 1:	return (ushort)((uint)reg2 | (uint)(reg3 << 8));
	case 2:	return (ushort)immediate;
	case 3:	return (ushort)(immediate >> 16);
	case 4:	return (ushort)memDispl;
	case 5:	return (ushort)((uint)memDispl >> 16);
	case 6:	return (ushort)(memDispl >> 32);
	case 7:	return (ushort)(memDispl >> 48);
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
		return 0;
	}
}

/// <summary>
/// Sets a new 'dd' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareDword"/>
/// </summary>
/// <param name="index">Index (0-3)</param>
/// <param name="value">New value</param>
public void SetDeclareDwordValue(int index, int value) = > SetDeclareDwordValue(index, (uint)value);

/// <summary>
/// Sets a new 'dd' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareDword"/>
/// </summary>
/// <param name="index">Index (0-3)</param>
/// <param name="value">New value</param>
public void SetDeclareDwordValue(int index, uint value) {
	switch (index) {
	case 0:
		reg0 = (byte)value;
		reg1 = (byte)(value >> 8);
		reg2 = (byte)(value >> 16);
		reg3 = (byte)(value >> 24);
		break;
	case 1:
		immediate = value;
		break;
	case 2:
		memDispl = (memDispl & 0xFFFF_FFFF_0000_0000) | (ulong)value;
		break;
	case 3:
		memDispl = (memDispl & 0x0000_0000_FFFF_FFFF) | ((ulong)value << 32);
		break;
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
		break;
	}
}

/// <summary>
/// Gets a 'dd' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareDword"/>
/// </summary>
/// <param name="index">Index (0-3)</param>
/// <returns></returns>
public readonly uint GetDeclareDwordValue(int index) {
	switch (index) {
	case 0:	return (uint)reg0 | (uint)(reg1 << 8) | (uint)(reg2 << 16) | (uint)(reg3 << 24);
	case 1:	return immediate;
	case 2:	return (uint)memDispl;
	case 3:	return (uint)(memDispl >> 32);
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
		return 0;
	}
}

/// <summary>
/// Sets a new 'dq' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareQword"/>
/// </summary>
/// <param name="index">Index (0-1)</param>
/// <param name="value">New value</param>
public void SetDeclareQwordValue(int index, long value) = > SetDeclareQwordValue(index, (ulong)value);

/// <summary>
/// Sets a new 'dq' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareQword"/>
/// </summary>
/// <param name="index">Index (0-1)</param>
/// <param name="value">New value</param>
public void SetDeclareQwordValue(int index, ulong value) {
	uint v;
	switch (index) {
	case 0:
		v = (uint)value;
		reg0 = (byte)v;
		reg1 = (byte)(v >> 8);
		reg2 = (byte)(v >> 16);
		reg3 = (byte)(v >> 24);
		immediate = (uint)(value >> 32);
		break;
	case 1:
		memDispl = value;
		break;
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
		break;
	}
}

/// <summary>
/// Gets a 'dq' value, see also <see cref="DeclareDataCount"/>.
/// Can only be called if <see cref="Code"/> is <see cref="Code.DeclareQword"/>
/// </summary>
/// <param name="index">Index (0-1)</param>
/// <returns></returns>
public readonly ulong GetDeclareQwordValue(int index) {
	switch (index) {
	case 0:	return (ulong)reg0 | (ulong)((uint)reg1 << 8) | (ulong)((uint)reg2 << 16) | (ulong)((uint)reg3 << 24) | ((ulong)immediate << 32);
	case 1:	return memDispl;
	default:
		ThrowHelper.ThrowArgumentOutOfRangeException_index();
		return 0;
	}
}

/// <summary>
/// Checks if this is a VSIB instruction, see also <see cref="IsVsib32"/>, <see cref="IsVsib64"/>
/// </summary>
public readonly bool IsVsib = > TryGetVsib64(out _);

/// <summary>
/// VSIB instructions only (<see cref="IsVsib"/>): <see langword="true"/> if it's using 32-bit indexes, <see langword="false"/> if it's using 64-bit indexes
/// </summary>
public readonly bool IsVsib32 = > TryGetVsib64(out bool vsib64) && !vsib64;

/// <summary>
/// VSIB instructions only (<see cref="IsVsib"/>): <see langword="true"/> if it's using 64-bit indexes, <see langword="false"/> if it's using 32-bit indexes
/// </summary>
public readonly bool IsVsib64 = > TryGetVsib64(out bool vsib64) && vsib64;

/// <summary>
/// Checks if it's a VSIB instruction. If it's a VSIB instruction, it sets <paramref name="vsib64"/> to <see langword="true"/> if it's
/// a VSIB instruction with 64-bit indexes, and clears it if it's using 32-bit indexes.
/// </summary>
/// <param name="vsib64">If it's a VSIB instruction, set to <see langword="true"/> if it's using 64-bit indexes, set to <see langword="false"/> if it's using 32-bit indexes</param>
/// <returns></returns>
public readonly bool TryGetVsib64(out bool vsib64) {
	switch (Code) {
		// GENERATOR-BEGIN: Vsib32
		// ⚠️This was generated by GENERATOR!🦹‍♂️
	case Code.VEX_Vpgatherdd_xmm_vm32x_xmm:
	case Code.VEX_Vpgatherdd_ymm_vm32y_ymm:
	case Code.VEX_Vpgatherdq_xmm_vm32x_xmm:
	case Code.VEX_Vpgatherdq_ymm_vm32x_ymm:
	case Code.EVEX_Vpgatherdd_xmm_k1_vm32x:
	case Code.EVEX_Vpgatherdd_ymm_k1_vm32y:
	case Code.EVEX_Vpgatherdd_zmm_k1_vm32z:
	case Code.EVEX_Vpgatherdq_xmm_k1_vm32x:
	case Code.EVEX_Vpgatherdq_ymm_k1_vm32x:
	case Code.EVEX_Vpgatherdq_zmm_k1_vm32y:
	case Code.VEX_Vgatherdps_xmm_vm32x_xmm:
	case Code.VEX_Vgatherdps_ymm_vm32y_ymm:
	case Code.VEX_Vgatherdpd_xmm_vm32x_xmm:
	case Code.VEX_Vgatherdpd_ymm_vm32x_ymm:
	case Code.EVEX_Vgatherdps_xmm_k1_vm32x:
	case Code.EVEX_Vgatherdps_ymm_k1_vm32y:
	case Code.EVEX_Vgatherdps_zmm_k1_vm32z:
	case Code.EVEX_Vgatherdpd_xmm_k1_vm32x:
	case Code.EVEX_Vgatherdpd_ymm_k1_vm32x:
	case Code.EVEX_Vgatherdpd_zmm_k1_vm32y:
	case Code.EVEX_Vpscatterdd_vm32x_k1_xmm:
	case Code.EVEX_Vpscatterdd_vm32y_k1_ymm:
	case Code.EVEX_Vpscatterdd_vm32z_k1_zmm:
	case Code.EVEX_Vpscatterdq_vm32x_k1_xmm:
	case Code.EVEX_Vpscatterdq_vm32x_k1_ymm:
	case Code.EVEX_Vpscatterdq_vm32y_k1_zmm:
	case Code.EVEX_Vscatterdps_vm32x_k1_xmm:
	case Code.EVEX_Vscatterdps_vm32y_k1_ymm:
	case Code.EVEX_Vscatterdps_vm32z_k1_zmm:
	case Code.EVEX_Vscatterdpd_vm32x_k1_xmm:
	case Code.EVEX_Vscatterdpd_vm32x_k1_ymm:
	case Code.EVEX_Vscatterdpd_vm32y_k1_zmm:
	case Code.EVEX_Vgatherpf0dps_vm32z_k1:
	case Code.EVEX_Vgatherpf0dpd_vm32y_k1:
	case Code.EVEX_Vgatherpf1dps_vm32z_k1:
	case Code.EVEX_Vgatherpf1dpd_vm32y_k1:
	case Code.EVEX_Vscatterpf0dps_vm32z_k1:
	case Code.EVEX_Vscatterpf0dpd_vm32y_k1:
	case Code.EVEX_Vscatterpf1dps_vm32z_k1:
	case Code.EVEX_Vscatterpf1dpd_vm32y_k1:
	case Code.MVEX_Vpgatherdd_zmm_k1_mvt:
	case Code.MVEX_Vpgatherdq_zmm_k1_mvt:
	case Code.MVEX_Vgatherdps_zmm_k1_mvt:
	case Code.MVEX_Vgatherdpd_zmm_k1_mvt:
	case Code.MVEX_Vpscatterdd_mvt_k1_zmm:
	case Code.MVEX_Vpscatterdq_mvt_k1_zmm:
	case Code.MVEX_Vscatterdps_mvt_k1_zmm:
	case Code.MVEX_Vscatterdpd_mvt_k1_zmm:
	case Code.MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_B0:
	case Code.MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_B2:
	case Code.MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_C0:
	case Code.MVEX_Vgatherpf0hintdps_mvt_k1:
	case Code.MVEX_Vgatherpf0hintdpd_mvt_k1:
	case Code.MVEX_Vgatherpf0dps_mvt_k1:
	case Code.MVEX_Vgatherpf1dps_mvt_k1:
	case Code.MVEX_Vscatterpf0hintdps_mvt_k1:
	case Code.MVEX_Vscatterpf0hintdpd_mvt_k1:
	case Code.MVEX_Vscatterpf0dps_mvt_k1:
	case Code.MVEX_Vscatterpf1dps_mvt_k1:
		vsib64 = false;
		return true;
		// GENERATOR-END: Vsib32

		// GENERATOR-BEGIN: Vsib64
		// ⚠️This was generated by GENERATOR!🦹‍♂️
	case Code.VEX_Vpgatherqd_xmm_vm64x_xmm:
	case Code.VEX_Vpgatherqd_xmm_vm64y_xmm:
	case Code.VEX_Vpgatherqq_xmm_vm64x_xmm:
	case Code.VEX_Vpgatherqq_ymm_vm64y_ymm:
	case Code.EVEX_Vpgatherqd_xmm_k1_vm64x:
	case Code.EVEX_Vpgatherqd_xmm_k1_vm64y:
	case Code.EVEX_Vpgatherqd_ymm_k1_vm64z:
	case Code.EVEX_Vpgatherqq_xmm_k1_vm64x:
	case Code.EVEX_Vpgatherqq_ymm_k1_vm64y:
	case Code.EVEX_Vpgatherqq_zmm_k1_vm64z:
	case Code.VEX_Vgatherqps_xmm_vm64x_xmm:
	case Code.VEX_Vgatherqps_xmm_vm64y_xmm:
	case Code.VEX_Vgatherqpd_xmm_vm64x_xmm:
	case Code.VEX_Vgatherqpd_ymm_vm64y_ymm:
	case Code.EVEX_Vgatherqps_xmm_k1_vm64x:
	case Code.EVEX_Vgatherqps_xmm_k1_vm64y:
	case Code.EVEX_Vgatherqps_ymm_k1_vm64z:
	case Code.EVEX_Vgatherqpd_xmm_k1_vm64x:
	case Code.EVEX_Vgatherqpd_ymm_k1_vm64y:
	case Code.EVEX_Vgatherqpd_zmm_k1_vm64z:
	case Code.EVEX_Vpscatterqd_vm64x_k1_xmm:
	case Code.EVEX_Vpscatterqd_vm64y_k1_xmm:
	case Code.EVEX_Vpscatterqd_vm64z_k1_ymm:
	case Code.EVEX_Vpscatterqq_vm64x_k1_xmm:
	case Code.EVEX_Vpscatterqq_vm64y_k1_ymm:
	case Code.EVEX_Vpscatterqq_vm64z_k1_zmm:
	case Code.EVEX_Vscatterqps_vm64x_k1_xmm:
	case Code.EVEX_Vscatterqps_vm64y_k1_xmm:
	case Code.EVEX_Vscatterqps_vm64z_k1_ymm:
	case Code.EVEX_Vscatterqpd_vm64x_k1_xmm:
	case Code.EVEX_Vscatterqpd_vm64y_k1_ymm:
	case Code.EVEX_Vscatterqpd_vm64z_k1_zmm:
	case Code.EVEX_Vgatherpf0qps_vm64z_k1:
	case Code.EVEX_Vgatherpf0qpd_vm64z_k1:
	case Code.EVEX_Vgatherpf1qps_vm64z_k1:
	case Code.EVEX_Vgatherpf1qpd_vm64z_k1:
	case Code.EVEX_Vscatterpf0qps_vm64z_k1:
	case Code.EVEX_Vscatterpf0qpd_vm64z_k1:
	case Code.EVEX_Vscatterpf1qps_vm64z_k1:
	case Code.EVEX_Vscatterpf1qpd_vm64z_k1:
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
public bool SuppressAllExceptions{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	readonly get = > (flags1 & (uint)InstrFlags1.SuppressAllExceptions) != 0;
	set {
		if (value)
			flags1 |= (uint)InstrFlags1.SuppressAllExceptions;
		else
			flags1 &= ~(uint)InstrFlags1.SuppressAllExceptions;
	}
}
[MethodImpl(MethodImplOptions.AggressiveInlining)]
internal void InternalSetSuppressAllExceptions() = > flags1 |= (uint)InstrFlags1.SuppressAllExceptions;

/// <summary>
/// Checks if the memory operand is RIP/EIP relative
/// </summary>
public readonly bool IsIPRelativeMemoryOperand{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	get = > MemoryBase == Register.RIP || MemoryBase == Register.EIP;
}

/// <summary>
/// Gets the <c>RIP</c>/<c>EIP</c> releative address (<see cref="MemoryDisplacement32"/> or <see cref="MemoryDisplacement64"/>).
/// This property is only valid if there's a memory operand with <c>RIP</c>/<c>EIP</c> relative addressing, see <see cref="IsIPRelativeMemoryOperand"/>
/// </summary>
public readonly ulong IPRelativeMemoryAddress = >
MemoryBase == Register.EIP ? MemoryDisplacement32 : MemoryDisplacement64;

#if ENCODER && OPCODE_INFO
/// <summary>
/// Gets the <see cref="OpCodeInfo"/>
/// </summary>
/// <returns></returns>
public readonly OpCodeInfo OpCode{
	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	get = > Code.ToOpCode();
}
#endif

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