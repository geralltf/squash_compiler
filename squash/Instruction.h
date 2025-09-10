#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <stdbool.h>

#include "Enums.h"
#include "Mnemonic.h"
#include "InstructionOpCounts.h"

enum InstrFlags1 // : uint
{
	IF_SegmentPrefixMask = 0x00000007,
	IF_SegmentPrefixShift = 0x00000005,
	IF_DataLengthMask = 0x0000000F,
	IF_DataLengthShift = 0x00000008,
	IF_RoundingControlMask = 0x00000007,
	IF_RoundingControlShift = 0x0000000C,
	IF_OpMaskMask = 0x00000007,
	IF_OpMaskShift = 0x0000000F,
	IF_CodeSizeMask = 0x00000003,
	IF_CodeSizeShift = 0x00000012,
	IF_Broadcast = 0x04000000,
	IF_SuppressAllExceptions = 0x08000000,
	IF_ZeroingMasking = 0x10000000,
	IF_RepePrefix = 0x20000000,
	IF_RepnePrefix = 0x40000000,
	IF_LockPrefix = 0x80000000,
	IF_EqualsIgnoreMask = 0x000C0000,
};

enum MvexInstrFlags // : uint
{
	MIF_MvexRegMemConvShift = 0x00000010,
	MIF_MvexRegMemConvMask = 0x0000001F,
	MIF_EvictionHint = 0x80000000,
};

enum CodeSize 
{
	/// <summary>Unknown size</summary>
	CS_Unknown = 0,
	/// <summary>16-bit code</summary>
	CS_Code16 = 1,
	/// <summary>32-bit code</summary>
	CS_Code32 = 2,
	/// <summary>64-bit code</summary>
	CS_Code64 = 3,
};

enum OpKind {
	/// <summary>A register (<see cref="Iced.Intel.Register"/>).<br/>
	/// <br/>
	/// This operand kind uses <see cref="Instruction.Op0Register"/>, <see cref="Instruction.Op1Register"/>, <see cref="Instruction.Op2Register"/>, <see cref="Instruction.Op3Register"/> or <see cref="Instruction.Op4Register"/> depending on operand number. See also <see cref="Instruction.GetOpRegister"/>.</summary>
	OK_Register = 0,
	/// <summary>Near 16-bit branch. This operand kind uses <see cref="Instruction.NearBranch16"/></summary>
	OK_NearBranch16 = 1,
	/// <summary>Near 32-bit branch. This operand kind uses <see cref="Instruction.NearBranch32"/></summary>
	OK_NearBranch32 = 2,
	/// <summary>Near 64-bit branch. This operand kind uses <see cref="Instruction.NearBranch64"/></summary>
	OK_NearBranch64 = 3,
	/// <summary>Far 16-bit branch. This operand kind uses <see cref="Instruction.FarBranch16"/> and <see cref="Instruction.FarBranchSelector"/></summary>
	OK_FarBranch16 = 4,
	/// <summary>Far 32-bit branch. This operand kind uses <see cref="Instruction.FarBranch32"/> and <see cref="Instruction.FarBranchSelector"/></summary>
	OK_FarBranch32 = 5,
	/// <summary>8-bit constant. This operand kind uses <see cref="Instruction.Immediate8"/></summary>
	OK_Immediate8 = 6,
	/// <summary>8-bit constant used by the <c>ENTER</c>, <c>EXTRQ</c>, <c>INSERTQ</c> instructions. This operand kind uses <see cref="Instruction.Immediate8_2nd"/></summary>
	OK_Immediate8_2nd = 7,
	/// <summary>16-bit constant. This operand kind uses <see cref="Instruction.Immediate16"/></summary>
	OK_Immediate16 = 8,
	/// <summary>32-bit constant. This operand kind uses <see cref="Instruction.Immediate32"/></summary>
	OK_Immediate32 = 9,
	/// <summary>64-bit constant. This operand kind uses <see cref="Instruction.Immediate64"/></summary>
	OK_Immediate64 = 10,
	/// <summary>An 8-bit value sign extended to 16 bits. This operand kind uses <see cref="Instruction.Immediate8to16"/></summary>
	OK_Immediate8to16 = 11,
	/// <summary>An 8-bit value sign extended to 32 bits. This operand kind uses <see cref="Instruction.Immediate8to32"/></summary>
	OK_Immediate8to32 = 12,
	/// <summary>An 8-bit value sign extended to 64 bits. This operand kind uses <see cref="Instruction.Immediate8to64"/></summary>
	OK_Immediate8to64 = 13,
	/// <summary>A 32-bit value sign extended to 64 bits. This operand kind uses <see cref="Instruction.Immediate32to64"/></summary>
	OK_Immediate32to64 = 14,
	/// <summary><c>seg:[SI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/>, <see cref="Instruction.MemorySegment"/>, <see cref="Instruction.SegmentPrefix"/></summary>
	OK_MemorySegSI = 15,
	/// <summary><c>seg:[ESI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/>, <see cref="Instruction.MemorySegment"/>, <see cref="Instruction.SegmentPrefix"/></summary>
	OK_MemorySegESI = 16,
	/// <summary><c>seg:[RSI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/>, <see cref="Instruction.MemorySegment"/>, <see cref="Instruction.SegmentPrefix"/></summary>
	OK_MemorySegRSI = 17,
	/// <summary><c>seg:[DI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/>, <see cref="Instruction.MemorySegment"/>, <see cref="Instruction.SegmentPrefix"/></summary>
	OK_MemorySegDI = 18,
	/// <summary><c>seg:[EDI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/>, <see cref="Instruction.MemorySegment"/>, <see cref="Instruction.SegmentPrefix"/></summary>
	OK_MemorySegEDI = 19,
	/// <summary><c>seg:[RDI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/>, <see cref="Instruction.MemorySegment"/>, <see cref="Instruction.SegmentPrefix"/></summary>
	OK_MemorySegRDI = 20,
	/// <summary><c>ES:[DI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/></summary>
	OK_MemoryESDI = 21,
	/// <summary><c>ES:[EDI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/></summary>
	OK_MemoryESEDI = 22,
	/// <summary><c>ES:[RDI]</c>. This operand kind uses <see cref="Instruction.MemorySize"/></summary>
	OK_MemoryESRDI = 23,
	/// <summary>Memory operand.<br/>
	/// <br/>
	/// This operand kind uses <see cref="Instruction.MemoryDisplSize"/>, <see cref="Instruction.MemorySize"/>, <see cref="Instruction.MemoryIndexScale"/>, <see cref="Instruction.MemoryDisplacement64"/>, <see cref="Instruction.MemoryBase"/>, <see cref="Instruction.MemoryIndex"/>, <see cref="Instruction.MemorySegment"/>, <see cref="Instruction.SegmentPrefix"/></summary>
	OK_Memory = 24,
};

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

const int TOTAL_SIZE = 40;

const int MaxOpCount = 5;
const int MaxInstructionLength = 15;
const int RegisterBits = 8;
const Register VMM_first = Register.ZMM0;
const Register VMM_last = Register.ZMM31;
const int VMM_count = 32;
const Register XMM_last = Register.XMM31;
const Register YMM_last = Register.YMM31;
const Register ZMM_last = Register.ZMM31;
const Register TMM_last = Register.TMM7;
const int MaxCpuidFeatureInternalValues = 199;
const MemorySize FirstBroadcastMemorySize = MemorySize.Broadcast32_Float16;
const unsigned int MvexStart = 4611;
const unsigned int MvexLength = 207;
const int CC_a_EnumCount = 2;
const int CC_ae_EnumCount = 3;
const int CC_b_EnumCount = 3;
const int CC_be_EnumCount = 2;
const int CC_e_EnumCount = 2;
const int CC_g_EnumCount = 2;
const int CC_ge_EnumCount = 2;
const int CC_l_EnumCount = 2;
const int CC_le_EnumCount = 2;
const int CC_ne_EnumCount = 2;
const int CC_np_EnumCount = 2;
const int CC_p_EnumCount = 2;
const int CodeEnumCount = 4936;
const int CodeSizeEnumCount = 4;
const int ConditionCodeEnumCount = 17;
const int CpuidFeatureEnumCount = 178;
const int DecoderErrorEnumCount = 3;
const int DecoratorKindEnumCount = 6;
const int EncodingKindEnumCount = 6;
const int FlowControlEnumCount = 10;
const int FormatterSyntaxEnumCount = 4;
const int FormatterTextKindEnumCount = 16;
const int MandatoryPrefixEnumCount = 5;
const int MemorySizeEnumCount = 162;
const int MemorySizeOptionsEnumCount = 4;
const int MnemonicEnumCount = 1894;
const int MvexConvFnEnumCount = 13;
const int MvexEHBitEnumCount = 3;
const int MvexRegMemConvEnumCount = 17;
const int MvexTupleTypeLutKindEnumCount = 14;
const int NumberBaseEnumCount = 4;
const int NumberKindEnumCount = 8;
const int OpAccessEnumCount = 8;
const int OpCodeOperandKindEnumCount = 109;
const int OpCodeTableKindEnumCount = 9;
const int OpKindEnumCount = 25;
const int PrefixKindEnumCount = 18;
const int RegisterEnumCount = 256;
const int RelocKindEnumCount = 1;
const int RepPrefixKindEnumCount = 3;
const int RoundingControlEnumCount = 5;
const int TupleTypeEnumCount = 19;

bool instruction_equals(struct Instruction* a, struct Instruction* b);
bool instruction_equals_allbits(struct Instruction* a, struct Instruction* b);
int gethashcode(struct Instruction* i);

#endif