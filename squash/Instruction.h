#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <stdbool.h>

#include "Enums.h"
#include "Mnemonic.h"
#include "InstructionOpCounts.h"
//#include "InstructionMemorySizes.h"

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

enum MvexInfoFlags1 
{
	MIF1_None = 0x00000000,
	MIF1_NDD = 0x00000001,
	MIF1_NDS = 0x00000002,
	MIF1_EvictionHint = 0x00000004,
	MIF1_ImmRoundingControl = 0x00000008,
	MIF1_RoundingControl = 0x00000010,
	MIF1_SuppressAllExceptions = 0x00000020,
	MIF1_IgnoresOpMaskRegister = 0x00000040,
	MIF1_RequireOpMaskRegister = 0x00000080,
};

enum MvexInfoFlags2 
{
	MIF2_None = 0x00000000,
	MIF2_NoSaeRoundingControl = 0x00000001,
	MIF2_ConvFn32 = 0x00000002,
	MIF2_IgnoresEvictionHint = 0x00000004,
};

enum TupleType 
{
	/// <summary><c>N = 1</c></summary>
	TT_N1 = 0,
	/// <summary><c>N = 2</c></summary>
	TT_N2 = 1,
	/// <summary><c>N = 4</c></summary>
	TT_N4 = 2,
	/// <summary><c>N = 8</c></summary>
	TT_N8 = 3,
	/// <summary><c>N = 16</c></summary>
	TT_N16 = 4,
	/// <summary><c>N = 32</c></summary>
	TT_N32 = 5,
	/// <summary><c>N = 64</c></summary>
	TT_N64 = 6,
	/// <summary><c>N = b ? 4 : 8</c></summary>
	TT_N8b4 = 7,
	/// <summary><c>N = b ? 4 : 16</c></summary>
	TT_N16b4 = 8,
	/// <summary><c>N = b ? 4 : 32</c></summary>
	TT_N32b4 = 9,
	/// <summary><c>N = b ? 4 : 64</c></summary>
	TT_N64b4 = 10,
	/// <summary><c>N = b ? 8 : 16</c></summary>
	TT_N16b8 = 11,
	/// <summary><c>N = b ? 8 : 32</c></summary>
	TT_N32b8 = 12,
	/// <summary><c>N = b ? 8 : 64</c></summary>
	TT_N64b8 = 13,
	/// <summary><c>N = b ? 2 : 4</c></summary>
	TT_N4b2 = 14,
	/// <summary><c>N = b ? 2 : 8</c></summary>
	TT_N8b2 = 15,
	/// <summary><c>N = b ? 2 : 16</c></summary>
	TT_N16b2 = 16,
	/// <summary><c>N = b ? 2 : 32</c></summary>
	TT_N32b2 = 17,
	/// <summary><c>N = b ? 2 : 64</c></summary>
	TT_N64b2 = 18,
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

enum Register
{
	Register_None = 0,
	Register_AL = 1,
	Register_CL = 2,
	Register_DL = 3,
	Register_BL = 4,
	Register_AH = 5,
	Register_CH = 6,
	Register_DH = 7,
	Register_BH = 8,
	Register_SPL = 9,
	Register_BPL = 10,
	Register_SIL = 11,
	Register_DIL = 12,
	Register_R8L = 13,
	Register_R9L = 14,
	Register_R10L = 15,
	Register_R11L = 16,
	Register_R12L = 17,
	Register_R13L = 18,
	Register_R14L = 19,
	Register_R15L = 20,
	Register_AX = 21,
	Register_CX = 22,
	Register_DX = 23,
	Register_BX = 24,
	Register_SP = 25,
	Register_BP = 26,
	Register_SI = 27,
	Register_DI = 28,
	Register_R8W = 29,
	Register_R9W = 30,
	Register_R10W = 31,
	Register_R11W = 32,
	Register_R12W = 33,
	Register_R13W = 34,
	Register_R14W = 35,
	Register_R15W = 36,
	Register_EAX = 37,
	Register_ECX = 38,
	Register_EDX = 39,
	Register_EBX = 40,
	Register_ESP = 41,
	Register_EBP = 42,
	Register_ESI = 43,
	Register_EDI = 44,
	Register_R8D = 45,
	Register_R9D = 46,
	Register_R10D = 47,
	Register_R11D = 48,
	Register_R12D = 49,
	Register_R13D = 50,
	Register_R14D = 51,
	Register_R15D = 52,
	Register_RAX = 53,
	Register_RCX = 54,
	Register_RDX = 55,
	Register_RBX = 56,
	Register_RSP = 57,
	Register_RBP = 58,
	Register_RSI = 59,
	Register_RDI = 60,
	Register_R8 = 61,
	Register_R9 = 62,
	Register_R10 = 63,
	Register_R11 = 64,
	Register_R12 = 65,
	Register_R13 = 66,
	Register_R14 = 67,
	Register_R15 = 68,
	Register_EIP = 69,
	Register_RIP = 70,
	Register_ES = 71,
	Register_CS = 72,
	Register_SS = 73,
	Register_DS = 74,
	Register_FS = 75,
	Register_GS = 76,
	Register_XMM0 = 77,
	Register_XMM1 = 78,
	Register_XMM2 = 79,
	Register_XMM3 = 80,
	Register_XMM4 = 81,
	Register_XMM5 = 82,
	Register_XMM6 = 83,
	Register_XMM7 = 84,
	Register_XMM8 = 85,
	Register_XMM9 = 86,
	Register_XMM10 = 87,
	Register_XMM11 = 88,
	Register_XMM12 = 89,
	Register_XMM13 = 90,
	Register_XMM14 = 91,
	Register_XMM15 = 92,
	Register_XMM16 = 93,
	Register_XMM17 = 94,
	Register_XMM18 = 95,
	Register_XMM19 = 96,
	Register_XMM20 = 97,
	Register_XMM21 = 98,
	Register_XMM22 = 99,
	Register_XMM23 = 100,
	Register_XMM24 = 101,
	Register_XMM25 = 102,
	Register_XMM26 = 103,
	Register_XMM27 = 104,
	Register_XMM28 = 105,
	Register_XMM29 = 106,
	Register_XMM30 = 107,
	Register_XMM31 = 108,
	Register_YMM0 = 109,
	Register_YMM1 = 110,
	Register_YMM2 = 111,
	Register_YMM3 = 112,
	Register_YMM4 = 113,
	Register_YMM5 = 114,
	Register_YMM6 = 115,
	Register_YMM7 = 116,
	Register_YMM8 = 117,
	Register_YMM9 = 118,
	Register_YMM10 = 119,
	Register_YMM11 = 120,
	Register_YMM12 = 121,
	Register_YMM13 = 122,
	Register_YMM14 = 123,
	Register_YMM15 = 124,
	Register_YMM16 = 125,
	Register_YMM17 = 126,
	Register_YMM18 = 127,
	Register_YMM19 = 128,
	Register_YMM20 = 129,
	Register_YMM21 = 130,
	Register_YMM22 = 131,
	Register_YMM23 = 132,
	Register_YMM24 = 133,
	Register_YMM25 = 134,
	Register_YMM26 = 135,
	Register_YMM27 = 136,
	Register_YMM28 = 137,
	Register_YMM29 = 138,
	Register_YMM30 = 139,
	Register_YMM31 = 140,
	Register_ZMM0 = 141,
	Register_ZMM1 = 142,
	Register_ZMM2 = 143,
	Register_ZMM3 = 144,
	Register_ZMM4 = 145,
	Register_ZMM5 = 146,
	Register_ZMM6 = 147,
	Register_ZMM7 = 148,
	Register_ZMM8 = 149,
	Register_ZMM9 = 150,
	Register_ZMM10 = 151,
	Register_ZMM11 = 152,
	Register_ZMM12 = 153,
	Register_ZMM13 = 154,
	Register_ZMM14 = 155,
	Register_ZMM15 = 156,
	Register_ZMM16 = 157,
	Register_ZMM17 = 158,
	Register_ZMM18 = 159,
	Register_ZMM19 = 160,
	Register_ZMM20 = 161,
	Register_ZMM21 = 162,
	Register_ZMM22 = 163,
	Register_ZMM23 = 164,
	Register_ZMM24 = 165,
	Register_ZMM25 = 166,
	Register_ZMM26 = 167,
	Register_ZMM27 = 168,
	Register_ZMM28 = 169,
	Register_ZMM29 = 170,
	Register_ZMM30 = 171,
	Register_ZMM31 = 172,
	Register_K0 = 173,
	Register_K1 = 174,
	Register_K2 = 175,
	Register_K3 = 176,
	Register_K4 = 177,
	Register_K5 = 178,
	Register_K6 = 179,
	Register_K7 = 180,
	Register_BND0 = 181,
	Register_BND1 = 182,
	Register_BND2 = 183,
	Register_BND3 = 184,
	Register_CR0 = 185,
	Register_CR1 = 186,
	Register_CR2 = 187,
	Register_CR3 = 188,
	Register_CR4 = 189,
	Register_CR5 = 190,
	Register_CR6 = 191,
	Register_CR7 = 192,
	Register_CR8 = 193,
	Register_CR9 = 194,
	Register_CR10 = 195,
	Register_CR11 = 196,
	Register_CR12 = 197,
	Register_CR13 = 198,
	Register_CR14 = 199,
	Register_CR15 = 200,
	Register_DR0 = 201,
	Register_DR1 = 202,
	Register_DR2 = 203,
	Register_DR3 = 204,
	Register_DR4 = 205,
	Register_DR5 = 206,
	Register_DR6 = 207,
	Register_DR7 = 208,
	Register_DR8 = 209,
	Register_DR9 = 210,
	Register_DR10 = 211,
	Register_DR11 = 212,
	Register_DR12 = 213,
	Register_DR13 = 214,
	Register_DR14 = 215,
	Register_DR15 = 216,
	Register_ST0 = 217,
	Register_ST1 = 218,
	Register_ST2 = 219,
	Register_ST3 = 220,
	Register_ST4 = 221,
	Register_ST5 = 222,
	Register_ST6 = 223,
	Register_ST7 = 224,
	Register_MM0 = 225,
	Register_MM1 = 226,
	Register_MM2 = 227,
	Register_MM3 = 228,
	Register_MM4 = 229,
	Register_MM5 = 230,
	Register_MM6 = 231,
	Register_MM7 = 232,
	Register_TR0 = 233,
	Register_TR1 = 234,
	Register_TR2 = 235,
	Register_TR3 = 236,
	Register_TR4 = 237,
	Register_TR5 = 238,
	Register_TR6 = 239,
	Register_TR7 = 240,
	Register_TMM0 = 241,
	Register_TMM1 = 242,
	Register_TMM2 = 243,
	Register_TMM3 = 244,
	Register_TMM4 = 245,
	Register_TMM5 = 246,
	Register_TMM6 = 247,
	Register_TMM7 = 248,
	/// <summary>Don&apos;t use it!</summary>
	Register_DontUse0 = 249,
	/// <summary>Don&apos;t use it!</summary>
	Register_DontUseFA = 250,
	/// <summary>Don&apos;t use it!</summary>
	Register_DontUseFB = 251,
	/// <summary>Don&apos;t use it!</summary>
	Register_DontUseFC = 252,
	/// <summary>Don&apos;t use it!</summary>
	Register_DontUseFD = 253,
	/// <summary>Don&apos;t use it!</summary>
	Register_DontUseFE = 254,
	/// <summary>Don&apos;t use it!</summary>
	Register_DontUseFF = 255,
};

enum MemorySize
{
	/// <summary>Unknown size or the instruction doesn&apos;t reference any memory (eg. <c>LEA</c>)</summary>
	MS_Unknown = 0,
	/// <summary>Memory location contains a <see cref="byte"/></summary>
	MS_UInt8 = 1,
	/// <summary>Memory location contains a <see cref="ushort"/></summary>
	MS_UInt16 = 2,
	/// <summary>Memory location contains a <see cref="uint"/></summary>
	MS_UInt32 = 3,
	/// <summary>Memory location contains a <c>uint52</c></summary>
	MS_UInt52 = 4,
	/// <summary>Memory location contains a <see cref="ulong"/></summary>
	MS_UInt64 = 5,
	/// <summary>Memory location contains a <c>uint128</c></summary>
	MS_UInt128 = 6,
	/// <summary>Memory location contains a <c>uint256</c></summary>
	MS_UInt256 = 7,
	/// <summary>Memory location contains a <c>uint512</c></summary>
	MS_UInt512 = 8,
	/// <summary>Memory location contains a <see cref="sbyte"/></summary>
	MS_Int8 = 9,
	/// <summary>Memory location contains a <see cref="short"/></summary>
	MS_Int16 = 10,
	/// <summary>Memory location contains a <see cref="int"/></summary>
	MS_Int32 = 11,
	/// <summary>Memory location contains a <see cref="long"/></summary>
	MS_Int64 = 12,
	/// <summary>Memory location contains a <c>int128</c></summary>
	MS_Int128 = 13,
	/// <summary>Memory location contains a <c>int256</c></summary>
	MS_Int256 = 14,
	/// <summary>Memory location contains a <c>int512</c></summary>
	MS_Int512 = 15,
	/// <summary>Memory location contains a seg:ptr pair, <see cref="ushort"/> (offset) + <see cref="ushort"/> (segment/selector)</summary>
	MS_SegPtr16 = 16,
	/// <summary>Memory location contains a seg:ptr pair, <see cref="uint"/> (offset) + <see cref="ushort"/> (segment/selector)</summary>
	MS_SegPtr32 = 17,
	/// <summary>Memory location contains a seg:ptr pair, <see cref="ulong"/> (offset) + <see cref="ushort"/> (segment/selector)</summary>
	MS_SegPtr64 = 18,
	/// <summary>Memory location contains a 16-bit offset (<c>JMP/CALL WORD PTR [mem]</c>)</summary>
	MS_WordOffset = 19,
	/// <summary>Memory location contains a 32-bit offset (<c>JMP/CALL DWORD PTR [mem]</c>)</summary>
	MS_DwordOffset = 20,
	/// <summary>Memory location contains a 64-bit offset (<c>JMP/CALL QWORD PTR [mem]</c>)</summary>
	MS_QwordOffset = 21,
	/// <summary>Memory location contains two <see cref="ushort"/>s (16-bit <c>BOUND</c>)</summary>
	MS_Bound16_WordWord = 22,
	/// <summary>Memory location contains two <see cref="uint"/>s (32-bit <c>BOUND</c>)</summary>
	MS_Bound32_DwordDword = 23,
	/// <summary>32-bit <c>BNDMOV</c>, 2 x <see cref="uint"/></summary>
	MS_Bnd32 = 24,
	/// <summary>64-bit <c>BNDMOV</c>, 2 x <see cref="ulong"/></summary>
	MS_Bnd64 = 25,
	/// <summary>Memory location contains a 16-bit limit and a 32-bit address (eg. <c>LGDTW</c>, <c>LGDTD</c>)</summary>
	MS_Fword6 = 26,
	/// <summary>Memory location contains a 16-bit limit and a 64-bit address (eg. <c>LGDTQ</c>)</summary>
	MS_Fword10 = 27,
	/// <summary>Memory location contains a <c>float16</c></summary>
	MS_Float16 = 28,
	/// <summary>Memory location contains a <see cref="float"/></summary>
	MS_Float32 = 29,
	/// <summary>Memory location contains a <see cref="double"/></summary>
	MS_Float64 = 30,
	/// <summary>Memory location contains a <c>float80</c></summary>
	MS_Float80 = 31,
	/// <summary>Memory location contains a <c>float128</c></summary>
	MS_Float128 = 32,
	/// <summary>Memory location contains a <c>bfloat16</c></summary>
	MS_BFloat16 = 33,
	/// <summary>Memory location contains a 14-byte FPU environment (16-bit <c>FLDENV</c>/<c>FSTENV</c>)</summary>
	MS_FpuEnv14 = 34,
	/// <summary>Memory location contains a 28-byte FPU environment (32/64-bit <c>FLDENV</c>/<c>FSTENV</c>)</summary>
	MS_FpuEnv28 = 35,
	/// <summary>Memory location contains a 94-byte FPU environment (16-bit <c>FSAVE</c>/<c>FRSTOR</c>)</summary>
	MS_FpuState94 = 36,
	/// <summary>Memory location contains a 108-byte FPU environment (32/64-bit <c>FSAVE</c>/<c>FRSTOR</c>)</summary>
	MS_FpuState108 = 37,
	/// <summary>Memory location contains 512-bytes of <c>FXSAVE</c>/<c>FXRSTOR</c> data</summary>
	MS_Fxsave_512Byte = 38,
	/// <summary>Memory location contains 512-bytes of <c>FXSAVE64</c>/<c>FXRSTOR64</c> data</summary>
	MS_Fxsave64_512Byte = 39,
	/// <summary>32-bit <c>XSAVE</c> area</summary>
	MS_Xsave = 40,
	/// <summary>64-bit <c>XSAVE</c> area</summary>
	MS_Xsave64 = 41,
	/// <summary>Memory location contains a 10-byte <c>bcd</c> value (<c>FBLD</c>/<c>FBSTP</c>)</summary>
	MS_Bcd = 42,
	/// <summary>64-bit location: TILECFG (<c>LDTILECFG</c>/<c>STTILECFG</c>)</summary>
	MS_Tilecfg = 43,
	/// <summary>Tile data</summary>
	MS_Tile = 44,
	/// <summary>80-bit segment descriptor and selector: 0-7 = descriptor, 8-9 = selector</summary>
	MS_SegmentDescSelector = 45,
	/// <summary>384-bit AES 128 handle (Key Locker)</summary>
	MS_KLHandleAes128 = 46,
	/// <summary>512-bit AES 256 handle (Key Locker)</summary>
	MS_KLHandleAes256 = 47,
	/// <summary>16-bit location: 2 x <see cref="byte"/></summary>
	MS_Packed16_UInt8 = 48,
	/// <summary>16-bit location: 2 x <see cref="sbyte"/></summary>
	MS_Packed16_Int8 = 49,
	/// <summary>32-bit location: 4 x <see cref="byte"/></summary>
	MS_Packed32_UInt8 = 50,
	/// <summary>32-bit location: 4 x <see cref="sbyte"/></summary>
	MS_Packed32_Int8 = 51,
	/// <summary>32-bit location: 2 x <see cref="ushort"/></summary>
	MS_Packed32_UInt16 = 52,
	/// <summary>32-bit location: 2 x <see cref="short"/></summary>
	MS_Packed32_Int16 = 53,
	/// <summary>32-bit location: 2 x <c>float16</c></summary>
	MS_Packed32_Float16 = 54,
	/// <summary>32-bit location: 2 x <c>bfloat16</c></summary>
	MS_Packed32_BFloat16 = 55,
	/// <summary>64-bit location: 8 x <see cref="byte"/></summary>
	MS_Packed64_UInt8 = 56,
	/// <summary>64-bit location: 8 x <see cref="sbyte"/></summary>
	MS_Packed64_Int8 = 57,
	/// <summary>64-bit location: 4 x <see cref="ushort"/></summary>
	MS_Packed64_UInt16 = 58,
	/// <summary>64-bit location: 4 x <see cref="short"/></summary>
	MS_Packed64_Int16 = 59,
	/// <summary>64-bit location: 2 x <see cref="uint"/></summary>
	MS_Packed64_UInt32 = 60,
	/// <summary>64-bit location: 2 x <see cref="int"/></summary>
	MS_Packed64_Int32 = 61,
	/// <summary>64-bit location: 4 x <c>float16</c></summary>
	MS_Packed64_Float16 = 62,
	/// <summary>64-bit location: 2 x <see cref="float"/></summary>
	MS_Packed64_Float32 = 63,
	/// <summary>128-bit location: 16 x <see cref="byte"/></summary>
	MS_Packed128_UInt8 = 64,
	/// <summary>128-bit location: 16 x <see cref="sbyte"/></summary>
	MS_Packed128_Int8 = 65,
	/// <summary>128-bit location: 8 x <see cref="ushort"/></summary>
	MS_Packed128_UInt16 = 66,
	/// <summary>128-bit location: 8 x <see cref="short"/></summary>
	MS_Packed128_Int16 = 67,
	/// <summary>128-bit location: 4 x <see cref="uint"/></summary>
	MS_Packed128_UInt32 = 68,
	/// <summary>128-bit location: 4 x <see cref="int"/></summary>
	MS_Packed128_Int32 = 69,
	/// <summary>128-bit location: 2 x <c>uint52</c></summary>
	MS_Packed128_UInt52 = 70,
	/// <summary>128-bit location: 2 x <see cref="ulong"/></summary>
	MS_Packed128_UInt64 = 71,
	/// <summary>128-bit location: 2 x <see cref="long"/></summary>
	MS_Packed128_Int64 = 72,
	/// <summary>128-bit location: 8 x <c>float16</c></summary>
	MS_Packed128_Float16 = 73,
	/// <summary>128-bit location: 4 x <see cref="float"/></summary>
	MS_Packed128_Float32 = 74,
	/// <summary>128-bit location: 2 x <see cref="double"/></summary>
	MS_Packed128_Float64 = 75,
	/// <summary>128-bit location: 8 x <c>bfloat16</c></summary>
	MS_Packed128_BFloat16 = 76,
	/// <summary>128-bit location: 4 x (2 x <c>float16</c>)</summary>
	MS_Packed128_2xFloat16 = 77,
	/// <summary>128-bit location: 4 x (2 x <c>bfloat16</c>)</summary>
	MS_Packed128_2xBFloat16 = 78,
	/// <summary>256-bit location: 32 x <see cref="byte"/></summary>
	MS_Packed256_UInt8 = 79,
	/// <summary>256-bit location: 32 x <see cref="sbyte"/></summary>
	MS_Packed256_Int8 = 80,
	/// <summary>256-bit location: 16 x <see cref="ushort"/></summary>
	MS_Packed256_UInt16 = 81,
	/// <summary>256-bit location: 16 x <see cref="short"/></summary>
	MS_Packed256_Int16 = 82,
	/// <summary>256-bit location: 8 x <see cref="uint"/></summary>
	MS_Packed256_UInt32 = 83,
	/// <summary>256-bit location: 8 x <see cref="int"/></summary>
	MS_Packed256_Int32 = 84,
	/// <summary>256-bit location: 4 x <c>uint52</c></summary>
	MS_Packed256_UInt52 = 85,
	/// <summary>256-bit location: 4 x <see cref="ulong"/></summary>
	MS_Packed256_UInt64 = 86,
	/// <summary>256-bit location: 4 x <see cref="long"/></summary>
	MS_Packed256_Int64 = 87,
	/// <summary>256-bit location: 2 x <c>uint128</c></summary>
	MS_Packed256_UInt128 = 88,
	/// <summary>256-bit location: 2 x <c>int128</c></summary>
	MS_Packed256_Int128 = 89,
	/// <summary>256-bit location: 16 x <c>float16</c></summary>
	MS_Packed256_Float16 = 90,
	/// <summary>256-bit location: 8 x <see cref="float"/></summary>
	MS_Packed256_Float32 = 91,
	/// <summary>256-bit location: 4 x <see cref="double"/></summary>
	MS_Packed256_Float64 = 92,
	/// <summary>256-bit location: 2 x <c>float128</c></summary>
	MS_Packed256_Float128 = 93,
	/// <summary>256-bit location: 16 x <c>bfloat16</c></summary>
	MS_Packed256_BFloat16 = 94,
	/// <summary>256-bit location: 8 x (2 x <c>float16</c>)</summary>
	MS_Packed256_2xFloat16 = 95,
	/// <summary>256-bit location: 8 x (2 x <c>bfloat16</c>)</summary>
	MS_Packed256_2xBFloat16 = 96,
	/// <summary>512-bit location: 64 x <see cref="byte"/></summary>
	MS_Packed512_UInt8 = 97,
	/// <summary>512-bit location: 64 x <see cref="sbyte"/></summary>
	MS_Packed512_Int8 = 98,
	/// <summary>512-bit location: 32 x <see cref="ushort"/></summary>
	MS_Packed512_UInt16 = 99,
	/// <summary>512-bit location: 32 x <see cref="short"/></summary>
	MS_Packed512_Int16 = 100,
	/// <summary>512-bit location: 16 x <see cref="uint"/></summary>
	MS_Packed512_UInt32 = 101,
	/// <summary>512-bit location: 16 x <see cref="int"/></summary>
	MS_Packed512_Int32 = 102,
	/// <summary>512-bit location: 8 x <c>uint52</c></summary>
	MS_Packed512_UInt52 = 103,
	/// <summary>512-bit location: 8 x <see cref="ulong"/></summary>
	MS_Packed512_UInt64 = 104,
	/// <summary>512-bit location: 8 x <see cref="long"/></summary>
	MS_Packed512_Int64 = 105,
	/// <summary>256-bit location: 4 x <c>uint128</c></summary>
	MS_Packed512_UInt128 = 106,
	/// <summary>512-bit location: 32 x <c>float16</c></summary>
	MS_Packed512_Float16 = 107,
	/// <summary>512-bit location: 16 x <see cref="float"/></summary>
	MS_Packed512_Float32 = 108,
	/// <summary>512-bit location: 8 x <see cref="double"/></summary>
	MS_Packed512_Float64 = 109,
	/// <summary>512-bit location: 16 x (2 x <c>float16</c>)</summary>
	MS_Packed512_2xFloat16 = 110,
	/// <summary>512-bit location: 16 x (2 x <c>bfloat16</c>)</summary>
	MS_Packed512_2xBFloat16 = 111,
	/// <summary>Broadcast <c>float16</c> to 32-bits</summary>
	MS_Broadcast32_Float16 = 112,
	/// <summary>Broadcast <see cref="uint"/> to 64-bits</summary>
	MS_Broadcast64_UInt32 = 113,
	/// <summary>Broadcast <see cref="int"/> to 64-bits</summary>
	MS_Broadcast64_Int32 = 114,
	/// <summary>Broadcast <c>float16</c> to 64-bits</summary>
	MS_Broadcast64_Float16 = 115,
	/// <summary>Broadcast <see cref="float"/> to 64-bits</summary>
	MS_Broadcast64_Float32 = 116,
	/// <summary>Broadcast <see cref="short"/> to 128-bits</summary>
	MS_Broadcast128_Int16 = 117,
	/// <summary>Broadcast <see cref="ushort"/> to 128-bits</summary>
	MS_Broadcast128_UInt16 = 118,
	/// <summary>Broadcast <see cref="uint"/> to 128-bits</summary>
	MS_Broadcast128_UInt32 = 119,
	/// <summary>Broadcast <see cref="int"/> to 128-bits</summary>
	MS_Broadcast128_Int32 = 120,
	/// <summary>Broadcast <c>uint52</c> to 128-bits</summary>
	MS_Broadcast128_UInt52 = 121,
	/// <summary>Broadcast <see cref="ulong"/> to 128-bits</summary>
	MS_Broadcast128_UInt64 = 122,
	/// <summary>Broadcast <see cref="long"/> to 128-bits</summary>
	MS_Broadcast128_Int64 = 123,
	/// <summary>Broadcast <c>float16</c> to 128-bits</summary>
	MS_Broadcast128_Float16 = 124,
	/// <summary>Broadcast <see cref="float"/> to 128-bits</summary>
	MS_Broadcast128_Float32 = 125,
	/// <summary>Broadcast <see cref="double"/> to 128-bits</summary>
	MS_Broadcast128_Float64 = 126,
	/// <summary>Broadcast 2 x <see cref="short"/> to 128-bits</summary>
	MS_Broadcast128_2xInt16 = 127,
	/// <summary>Broadcast 2 x <see cref="int"/> to 128-bits</summary>
	MS_Broadcast128_2xInt32 = 128,
	/// <summary>Broadcast 2 x <see cref="uint"/> to 128-bits</summary>
	MS_Broadcast128_2xUInt32 = 129,
	/// <summary>Broadcast 2 x <c>float16</c> to 128-bits</summary>
	MS_Broadcast128_2xFloat16 = 130,
	/// <summary>Broadcast 2 x <c>bfloat16</c> to 128-bits</summary>
	MS_Broadcast128_2xBFloat16 = 131,
	/// <summary>Broadcast <see cref="short"/> to 256-bits</summary>
	MS_Broadcast256_Int16 = 132,
	/// <summary>Broadcast <see cref="ushort"/> to 256-bits</summary>
	MS_Broadcast256_UInt16 = 133,
	/// <summary>Broadcast <see cref="uint"/> to 256-bits</summary>
	MS_Broadcast256_UInt32 = 134,
	/// <summary>Broadcast <see cref="int"/> to 256-bits</summary>
	MS_Broadcast256_Int32 = 135,
	/// <summary>Broadcast <c>uint52</c> to 256-bits</summary>
	MS_Broadcast256_UInt52 = 136,
	/// <summary>Broadcast <see cref="ulong"/> to 256-bits</summary>
	MS_Broadcast256_UInt64 = 137,
	/// <summary>Broadcast <see cref="long"/> to 256-bits</summary>
	MS_Broadcast256_Int64 = 138,
	/// <summary>Broadcast <c>float16</c> to 256-bits</summary>
	MS_Broadcast256_Float16 = 139,
	/// <summary>Broadcast <see cref="float"/> to 256-bits</summary>
	MS_Broadcast256_Float32 = 140,
	/// <summary>Broadcast <see cref="double"/> to 256-bits</summary>
	MS_Broadcast256_Float64 = 141,
	/// <summary>Broadcast 2 x <see cref="short"/> to 256-bits</summary>
	MS_Broadcast256_2xInt16 = 142,
	/// <summary>Broadcast 2 x <see cref="int"/> to 256-bits</summary>
	MS_Broadcast256_2xInt32 = 143,
	/// <summary>Broadcast 2 x <see cref="uint"/> to 256-bits</summary>
	MS_Broadcast256_2xUInt32 = 144,
	/// <summary>Broadcast 2 x <c>float16</c> to 256-bits</summary>
	MS_Broadcast256_2xFloat16 = 145,
	/// <summary>Broadcast 2 x <c>bfloat16</c> to 256-bits</summary>
	MS_Broadcast256_2xBFloat16 = 146,
	/// <summary>Broadcast <see cref="short"/> to 512-bits</summary>
	MS_Broadcast512_Int16 = 147,
	/// <summary>Broadcast <see cref="ushort"/> to 512-bits</summary>
	MS_Broadcast512_UInt16 = 148,
	/// <summary>Broadcast <see cref="uint"/> to 512-bits</summary>
	MS_Broadcast512_UInt32 = 149,
	/// <summary>Broadcast <see cref="int"/> to 512-bits</summary>
	MS_Broadcast512_Int32 = 150,
	/// <summary>Broadcast <c>uint52</c> to 512-bits</summary>
	MS_Broadcast512_UInt52 = 151,
	/// <summary>Broadcast <see cref="ulong"/> to 512-bits</summary>
	MS_Broadcast512_UInt64 = 152,
	/// <summary>Broadcast <see cref="long"/> to 512-bits</summary>
	MS_Broadcast512_Int64 = 153,
	/// <summary>Broadcast <c>float16</c> to 512-bits</summary>
	MS_Broadcast512_Float16 = 154,
	/// <summary>Broadcast <see cref="float"/> to 512-bits</summary>
	MS_Broadcast512_Float32 = 155,
	/// <summary>Broadcast <see cref="double"/> to 512-bits</summary>
	MS_Broadcast512_Float64 = 156,
	/// <summary>Broadcast 2 x <c>float16</c> to 512-bits</summary>
	MS_Broadcast512_2xFloat16 = 157,
	/// <summary>Broadcast 2 x <see cref="short"/> to 512-bits</summary>
	MS_Broadcast512_2xInt16 = 158,
	/// <summary>Broadcast 2 x <see cref="uint"/> to 512-bits</summary>
	MS_Broadcast512_2xUInt32 = 159,
	/// <summary>Broadcast 2 x <see cref="int"/> to 512-bits</summary>
	MS_Broadcast512_2xInt32 = 160,
	/// <summary>Broadcast 2 x <c>bfloat16</c> to 512-bits</summary>
	MS_Broadcast512_2xBFloat16 = 161,
};

enum MvexRegMemConv 
{
	/// <summary>No operand conversion</summary>
	MRMC_None = 0,
	/// <summary>Register swizzle: <c>zmm0</c> or <c>zmm0 {dcba}</c></summary>
	MRMC_RegSwizzleNone = 1,
	/// <summary>Register swizzle: <c>zmm0 {cdab}</c></summary>
	MRMC_RegSwizzleCdab = 2,
	/// <summary>Register swizzle: <c>zmm0 {badc}</c></summary>
	MRMC_RegSwizzleBadc = 3,
	/// <summary>Register swizzle: <c>zmm0 {dacb}</c></summary>
	MRMC_RegSwizzleDacb = 4,
	/// <summary>Register swizzle: <c>zmm0 {aaaa}</c></summary>
	MRMC_RegSwizzleAaaa = 5,
	/// <summary>Register swizzle: <c>zmm0 {bbbb}</c></summary>
	MRMC_RegSwizzleBbbb = 6,
	/// <summary>Register swizzle: <c>zmm0 {cccc}</c></summary>
	MRMC_RegSwizzleCccc = 7,
	/// <summary>Register swizzle: <c>zmm0 {dddd}</c></summary>
	MRMC_RegSwizzleDddd = 8,
	/// <summary>Memory Up/DownConv: <c>[rax]</c> / <c>zmm0</c></summary>
	MRMC_MemConvNone = 9,
	/// <summary>Memory UpConv: <c>[rax] {1to16}</c> or <c>[rax] {1to8}</c></summary>
	MRMC_MemConvBroadcast1 = 10,
	/// <summary>Memory UpConv: <c>[rax] {4to16}</c> or <c>[rax] {4to8}</c></summary>
	MRMC_MemConvBroadcast4 = 11,
	/// <summary>Memory Up/DownConv: <c>[rax] {float16}</c> / <c>zmm0 {float16}</c></summary>
	MRMC_MemConvFloat16 = 12,
	/// <summary>Memory Up/DownConv: <c>[rax] {uint8}</c> / <c>zmm0 {uint8}</c></summary>
	MRMC_MemConvUint8 = 13,
	/// <summary>Memory Up/DownConv: <c>[rax] {sint8}</c> / <c>zmm0 {sint8}</c></summary>
	MRMC_MemConvSint8 = 14,
	/// <summary>Memory Up/DownConv: <c>[rax] {uint16}</c> / <c>zmm0 {uint16}</c></summary>
	MRMC_MemConvUint16 = 15,
	/// <summary>Memory Up/DownConv: <c>[rax] {sint16}</c> / <c>zmm0 {sint16}</c></summary>
	MRMC_MemConvSint16 = 16,
};

const int MvexInfoData_StructSize = 8;
const int MvexInfoData_TupleTypeLutKindIndex = 0;
const int MvexInfoData_EHBitIndex = 1;
const int MvexInfoData_ConvFnIndex = 2;
const int MvexInfoData_InvalidConvFnsIndex = 3;
const int MvexInfoData_InvalidSwizzleFnsIndex = 4;
const int MvexInfoData_Flags1Index = 5;
const int MvexInfoData_Flags2Index = 6;

unsigned char MvexInfoData_Data[207 * 8] =
{
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Vprefetchnta_m
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Vprefetch0_m
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Vprefetch1_m
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Vprefetch2_m
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Vprefetchenta_m
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Vprefetche0_m
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Vprefetche1_m
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Vprefetche2_m
	0x07, 0x00, 0x01, 0x06, 0x00, 0x04, 0x03, 0x00,// Code.MVEX_Vmovaps_zmm_k1_zmmmt
	0x0B, 0x00, 0x02, 0xFE, 0x00, 0x04, 0x01, 0x00,// Code.MVEX_Vmovapd_zmm_k1_zmmmt
	0x07, 0x00, 0x09, 0x06, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vmovaps_mt_k1_zmm
	0x0B, 0x00, 0x0A, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vmovapd_mt_k1_zmm
	0x0B, 0x01, 0x0A, 0xFE, 0xFF, 0x00, 0x00, 0x00,// Code.MVEX_Vmovnrapd_m_k1_zmm
	0x0B, 0x02, 0x0A, 0xFE, 0xFF, 0x00, 0x00, 0x00,// Code.MVEX_Vmovnrngoapd_m_k1_zmm
	0x07, 0x01, 0x09, 0x06, 0xFF, 0x00, 0x02, 0x00,// Code.MVEX_Vmovnraps_m_k1_zmm
	0x07, 0x02, 0x09, 0x06, 0xFF, 0x00, 0x02, 0x00,// Code.MVEX_Vmovnrngoaps_m_k1_zmm
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vaddps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vaddpd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vmulps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vmulpd_zmm_k1_zmm_zmmmt
	0x08, 0x00, 0x01, 0xF8, 0x00, 0x24, 0x02, 0x00,// Code.MVEX_Vcvtps2pd_zmm_k1_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x34, 0x00, 0x00,// Code.MVEX_Vcvtpd2ps_zmm_k1_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vsubps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vsubpd_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpcmpgtd_kr_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x0E, 0x00, 0x04, 0x03, 0x00,// Code.MVEX_Vmovdqa32_zmm_k1_zmmmt
	0x04, 0x00, 0x04, 0xFE, 0x00, 0x04, 0x01, 0x00,// Code.MVEX_Vmovdqa64_zmm_k1_zmmmt
	0x00, 0x00, 0x00, 0xFE, 0xFE, 0x04, 0x03, 0x00,// Code.MVEX_Vpshufd_zmm_k1_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x05, 0x03, 0x00,// Code.MVEX_Vpsrld_zmm_k1_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x05, 0x03, 0x00,// Code.MVEX_Vpsrad_zmm_k1_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x05, 0x03, 0x00,// Code.MVEX_Vpslld_zmm_k1_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpcmpeqd_kr_k1_zmm_zmmmt
	0x01, 0x00, 0x03, 0xF8, 0x00, 0x04, 0x02, 0x00,// Code.MVEX_Vcvtudq2pd_zmm_k1_zmmmt
	0x00, 0x00, 0x0B, 0x0E, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vmovdqa32_mt_k1_zmm
	0x04, 0x00, 0x0C, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vmovdqa64_mt_k1_zmm
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Clevict1_m
	0x00, 0x00, 0x00, 0xFE, 0xFF, 0x40, 0x06, 0x00,// Code.MVEX_Clevict0_m
	0x07, 0x00, 0x01, 0x00, 0x00, 0x26, 0x02, 0x00,// Code.MVEX_Vcmpps_kr_k1_zmm_zmmmt_imm8
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x26, 0x00, 0x00,// Code.MVEX_Vcmppd_kr_k1_zmm_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpandd_zmm_k1_zmm_zmmmt
	0x04, 0x00, 0x04, 0xF8, 0x00, 0x06, 0x00, 0x00,// Code.MVEX_Vpandq_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpandnd_zmm_k1_zmm_zmmmt
	0x04, 0x00, 0x04, 0xF8, 0x00, 0x06, 0x00, 0x00,// Code.MVEX_Vpandnq_zmm_k1_zmm_zmmmt
	0x01, 0x00, 0x03, 0xF8, 0x00, 0x04, 0x02, 0x00,// Code.MVEX_Vcvtdq2pd_zmm_k1_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpord_zmm_k1_zmm_zmmmt
	0x04, 0x00, 0x04, 0xF8, 0x00, 0x06, 0x00, 0x00,// Code.MVEX_Vporq_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpxord_zmm_k1_zmm_zmmmt
	0x04, 0x00, 0x04, 0xF8, 0x00, 0x06, 0x00, 0x00,// Code.MVEX_Vpxorq_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpsubd_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpaddd_zmm_k1_zmm_zmmmt
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vbroadcastss_zmm_k1_mt
	0x0D, 0x00, 0x06, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vbroadcastsd_zmm_k1_mt
	0x09, 0x00, 0x05, 0x06, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vbroadcastf32x4_zmm_k1_mt
	0x0C, 0x00, 0x06, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vbroadcastf64x4_zmm_k1_mt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vptestmd_kr_k1_zmm_zmmmt
	0x00, 0x00, 0x00, 0xFE, 0xFE, 0x06, 0x03, 0x00,// Code.MVEX_Vpermd_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpminsd_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpminud_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpmaxsd_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpmaxud_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpmulld_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x24, 0x02, 0x00,// Code.MVEX_Vgetexpps_zmm_k1_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x24, 0x00, 0x00,// Code.MVEX_Vgetexppd_zmm_k1_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpsrlvd_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpsravd_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpsllvd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_48
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_49
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_4A
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_4B
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vaddnps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vaddnpd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x26, 0x02, 0x00,// Code.MVEX_Vgmaxabsps_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x26, 0x02, 0x00,// Code.MVEX_Vgminps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x26, 0x00, 0x00,// Code.MVEX_Vgminpd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x26, 0x02, 0x00,// Code.MVEX_Vgmaxps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x26, 0x00, 0x00,// Code.MVEX_Vgmaxpd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_54
	0x00, 0x00, 0x03, 0x08, 0x00, 0x26, 0x02, 0x00,// Code.MVEX_Vfixupnanps_zmm_k1_zmm_zmmmt
	0x04, 0x00, 0x04, 0xF8, 0x00, 0x26, 0x00, 0x00,// Code.MVEX_Vfixupnanpd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_56
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_57
	0x03, 0x00, 0x07, 0x0E, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vpbroadcastd_zmm_k1_mt
	0x06, 0x00, 0x08, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vpbroadcastq_zmm_k1_mt
	0x02, 0x00, 0x07, 0x0E, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vbroadcasti32x4_zmm_k1_mt
	0x05, 0x00, 0x08, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vbroadcasti64x4_zmm_k1_mt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpadcd_zmm_k1_kr_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpaddsetcd_zmm_k1_kr_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpsbbd_zmm_k1_kr_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpsubsetbd_zmm_k1_kr_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpblendmd_zmm_k1_zmm_zmmmt
	0x04, 0x00, 0x04, 0xF8, 0x00, 0x06, 0x00, 0x00,// Code.MVEX_Vpblendmq_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vblendmps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x06, 0x00, 0x00,// Code.MVEX_Vblendmpd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_67
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_68
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_69
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_6A
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_6B
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpsubrd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vsubrps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vsubrpd_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpsbbrd_zmm_k1_kr_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpsubrsetbd_zmm_k1_kr_zmmmt
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_70
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_71
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_72
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_73
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpcmpltd_kr_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vscaleps_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpmulhud_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpmulhd_zmm_k1_zmm_zmmmt
	0x03, 0x00, 0x07, 0x0E, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vpgatherdd_zmm_k1_mvt
	0x06, 0x00, 0x08, 0xFE, 0xFF, 0x84, 0x00, 0x00,// Code.MVEX_Vpgatherdq_zmm_k1_mvt
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vgatherdps_zmm_k1_mvt
	0x0D, 0x00, 0x06, 0xFE, 0xFF, 0x84, 0x00, 0x00,// Code.MVEX_Vgatherdpd_zmm_k1_mvt
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_94
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W1_94
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfmadd132ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfmadd132pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfmsub132ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfmsub132pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfnmadd132ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfnmadd132pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfnmsub132ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfnmsub132pd_zmm_k1_zmm_zmmmt
	0x03, 0x00, 0x0B, 0x0E, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vpscatterdd_mvt_k1_zmm
	0x06, 0x00, 0x0C, 0xFE, 0xFF, 0x84, 0x00, 0x00,// Code.MVEX_Vpscatterdq_mvt_k1_zmm
	0x0A, 0x00, 0x09, 0x06, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vscatterdps_mvt_k1_zmm
	0x0D, 0x00, 0x0A, 0xFE, 0xFF, 0x84, 0x00, 0x00,// Code.MVEX_Vscatterdpd_mvt_k1_zmm
	0x07, 0x00, 0x01, 0xFA, 0xFE, 0x36, 0x02, 0x00,// Code.MVEX_Vfmadd233ps_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfmadd213ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfmadd213pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfmsub213ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfmsub213pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfnmadd213ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfnmadd213pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfnmsub213ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfnmsub213pd_zmm_k1_zmm_zmmmt
	0x0A, 0x00, 0x00, 0x00, 0xFF, 0xB4, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_B0
	0x0A, 0x00, 0x00, 0x00, 0xFF, 0xB4, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_B2
	0x00, 0x00, 0x03, 0xFA, 0xFE, 0x06, 0x02, 0x00,// Code.MVEX_Vpmadd233d_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x02, 0x00,// Code.MVEX_Vpmadd231d_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfmadd231ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfmadd231pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfmsub231ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfmsub231pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfnmadd231ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfnmadd231pd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Vfnmsub231ps_zmm_k1_zmm_zmmmt
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x36, 0x00, 0x00,// Code.MVEX_Vfnmsub231pd_zmm_k1_zmm_zmmmt
	0x0A, 0x00, 0x00, 0x00, 0xFF, 0xB4, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_mvt_512_66_0F38_W0_C0
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vgatherpf0hintdps_mvt_k1
	0x0D, 0x00, 0x06, 0xFE, 0xFF, 0x84, 0x00, 0x00,// Code.MVEX_Vgatherpf0hintdpd_mvt_k1
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vgatherpf0dps_mvt_k1
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vgatherpf1dps_mvt_k1
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vscatterpf0hintdps_mvt_k1
	0x0D, 0x00, 0x06, 0xFE, 0xFF, 0x84, 0x00, 0x00,// Code.MVEX_Vscatterpf0hintdpd_mvt_k1
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vscatterpf0dps_mvt_k1
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x84, 0x02, 0x00,// Code.MVEX_Vscatterpf1dps_mvt_k1
	0x00, 0x00, 0x00, 0xFE, 0xFE, 0x24, 0x02, 0x00,// Code.MVEX_Vexp223ps_zmm_k1_zmmmt
	0x07, 0x00, 0x00, 0xFE, 0xFE, 0x24, 0x02, 0x00,// Code.MVEX_Vlog2ps_zmm_k1_zmmmt
	0x07, 0x00, 0x00, 0xFE, 0xFE, 0x24, 0x02, 0x00,// Code.MVEX_Vrcp23ps_zmm_k1_zmmmt
	0x07, 0x00, 0x00, 0xFE, 0xFE, 0x24, 0x02, 0x00,// Code.MVEX_Vrsqrt23ps_zmm_k1_zmmmt
	0x07, 0x00, 0x01, 0x00, 0x00, 0xB6, 0x02, 0x00,// Code.MVEX_Vaddsetsps_zmm_k1_zmm_zmmmt
	0x00, 0x00, 0x03, 0x08, 0x00, 0x86, 0x02, 0x00,// Code.MVEX_Vpaddsetsd_zmm_k1_zmm_zmmmt
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_CE
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W1_CE
	0x07, 0x00, 0x00, 0x00, 0x00, 0x36, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmm_zmmmt_512_66_0F38_W0_CF
	0x03, 0x00, 0x07, 0x0E, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vloadunpackld_zmm_k1_mt
	0x06, 0x00, 0x08, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vloadunpacklq_zmm_k1_mt
	0x03, 0x00, 0x0B, 0x0E, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vpackstoreld_mt_k1_zmm
	0x06, 0x00, 0x0C, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vpackstorelq_mt_k1_zmm
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vloadunpacklps_zmm_k1_mt
	0x0D, 0x00, 0x06, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vloadunpacklpd_zmm_k1_mt
	0x0A, 0x00, 0x09, 0x06, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vpackstorelps_mt_k1_zmm
	0x0D, 0x00, 0x0A, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vpackstorelpd_mt_k1_zmm
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_0F38_W0_D2
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_D2
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_0F38_W0_D3
	0x03, 0x00, 0x07, 0x0E, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vloadunpackhd_zmm_k1_mt
	0x06, 0x00, 0x08, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vloadunpackhq_zmm_k1_mt
	0x03, 0x00, 0x0B, 0x0E, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vpackstorehd_mt_k1_zmm
	0x06, 0x00, 0x0C, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vpackstorehq_mt_k1_zmm
	0x0A, 0x00, 0x05, 0x06, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vloadunpackhps_zmm_k1_mt
	0x0D, 0x00, 0x06, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vloadunpackhpd_zmm_k1_mt
	0x0A, 0x00, 0x09, 0x06, 0xFF, 0x04, 0x02, 0x00,// Code.MVEX_Vpackstorehps_mt_k1_zmm
	0x0D, 0x00, 0x0A, 0xFE, 0xFF, 0x04, 0x00, 0x00,// Code.MVEX_Vpackstorehpd_mt_k1_zmm
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_0F38_W0_D6
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_66_0F38_W0_D6
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_512_0F38_W0_D7
	0x00, 0x00, 0x00, 0xFE, 0xFE, 0x06, 0x03, 0x00,// Code.MVEX_Valignd_zmm_k1_zmm_zmmmt_imm8
	0x07, 0x00, 0x00, 0xFE, 0xFE, 0x04, 0x03, 0x00,// Code.MVEX_Vpermf32x4_zmm_k1_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpcmpud_kr_k1_zmm_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x06, 0x03, 0x00,// Code.MVEX_Vpcmpd_kr_k1_zmm_zmmmt_imm8
	0x07, 0x00, 0x01, 0x00, 0x00, 0x24, 0x02, 0x00,// Code.MVEX_Vgetmantps_zmm_k1_zmmmt_imm8
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x24, 0x00, 0x00,// Code.MVEX_Vgetmantpd_zmm_k1_zmmmt_imm8
	0x07, 0x00, 0x01, 0x00, 0x00, 0x2C, 0x02, 0x00,// Code.MVEX_Vrndfxpntps_zmm_k1_zmmmt_imm8
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x2C, 0x00, 0x00,// Code.MVEX_Vrndfxpntpd_zmm_k1_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x2C, 0x02, 0x00,// Code.MVEX_Vcvtfxpntudq2ps_zmm_k1_zmmmt_imm8
	0x07, 0x00, 0x01, 0x00, 0x00, 0x2C, 0x02, 0x00,// Code.MVEX_Vcvtfxpntps2udq_zmm_k1_zmmmt_imm8
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x2C, 0x00, 0x00,// Code.MVEX_Vcvtfxpntpd2udq_zmm_k1_zmmmt_imm8
	0x00, 0x00, 0x03, 0x08, 0x00, 0x2C, 0x02, 0x00,// Code.MVEX_Vcvtfxpntdq2ps_zmm_k1_zmmmt_imm8
	0x07, 0x00, 0x01, 0x00, 0x00, 0x2C, 0x02, 0x00,// Code.MVEX_Vcvtfxpntps2dq_zmm_k1_zmmmt_imm8
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_imm8_512_66_0F3A_W0_D0
	0x07, 0x00, 0x00, 0x00, 0x00, 0x34, 0x02, 0x00,// Code.MVEX_Undoc_zmm_k1_zmmmt_imm8_512_66_0F3A_W0_D1
	0x0B, 0x00, 0x02, 0xF8, 0x00, 0x2C, 0x00, 0x00,// Code.MVEX_Vcvtfxpntpd2dq_zmm_k1_zmmmt_imm8
};

unsigned char MvexTupleTypeLut_Data[112] =
{
	// MvexTupleTypeLutKind.Int32
	(unsigned char)TT_N64,// 0
	(unsigned char)TT_N4,// 1
	(unsigned char)TT_N16,// 2
	(unsigned char)TT_N32,// 3
	(unsigned char)TT_N16,// 4
	(unsigned char)TT_N16,// 5
	(unsigned char)TT_N32,// 6
	(unsigned char)TT_N32,// 7
	// MvexTupleTypeLutKind.Int32_Half
	(unsigned char)TT_N32,// 0
	(unsigned char)TT_N4,// 1
	(unsigned char)TT_N16,// 2
	(unsigned char)TT_N16,// 3
	(unsigned char)TT_N8,// 4
	(unsigned char)TT_N8,// 5
	(unsigned char)TT_N16,// 6
	(unsigned char)TT_N16,// 7
	// MvexTupleTypeLutKind.Int32_4to16
	(unsigned char)TT_N16,// 0
	(unsigned char)TT_N1,// 1
	(unsigned char)TT_N1,// 2
	(unsigned char)TT_N8,// 3
	(unsigned char)TT_N4,// 4
	(unsigned char)TT_N4,// 5
	(unsigned char)TT_N8,// 6
	(unsigned char)TT_N8,// 7
	// MvexTupleTypeLutKind.Int32_1to16_or_elem
	(unsigned char)TT_N4,// 0
	(unsigned char)TT_N1,// 1
	(unsigned char)TT_N1,// 2
	(unsigned char)TT_N2,// 3
	(unsigned char)TT_N1,// 4
	(unsigned char)TT_N1,// 5
	(unsigned char)TT_N2,// 6
	(unsigned char)TT_N2,// 7
	// MvexTupleTypeLutKind.Int64
	(unsigned char)TT_N64,// 0
	(unsigned char)TT_N8,// 1
	(unsigned char)TT_N32,// 2
	(unsigned char)TT_N16,// 3
	(unsigned char)TT_N8,// 4
	(unsigned char)TT_N8,// 5
	(unsigned char)TT_N16,// 6
	(unsigned char)TT_N16,// 7
	// MvexTupleTypeLutKind.Int64_4to8
	(unsigned char)TT_N32,// 0
	(unsigned char)TT_N1,// 1
	(unsigned char)TT_N1,// 2
	(unsigned char)TT_N8,// 3
	(unsigned char)TT_N4,// 4
	(unsigned char)TT_N4,// 5
	(unsigned char)TT_N8,// 6
	(unsigned char)TT_N8,// 7
	// MvexTupleTypeLutKind.Int64_1to8_or_elem
	(unsigned char)TT_N8,// 0
	(unsigned char)TT_N1,// 1
	(unsigned char)TT_N1,// 2
	(unsigned char)TT_N2,// 3
	(unsigned char)TT_N1,// 4
	(unsigned char)TT_N1,// 5
	(unsigned char)TT_N2,// 6
	(unsigned char)TT_N2,// 7
	// MvexTupleTypeLutKind.Float32
	(unsigned char)TT_N64,// 0
	(unsigned char)TT_N4,// 1
	(unsigned char)TT_N16,// 2
	(unsigned char)TT_N32,// 3
	(unsigned char)TT_N16,// 4
	(unsigned char)TT_N16,// 5
	(unsigned char)TT_N32,// 6
	(unsigned char)TT_N32,// 7
	// MvexTupleTypeLutKind.Float32_Half
	(unsigned char)TT_N32,// 0
	(unsigned char)TT_N4,// 1
	(unsigned char)TT_N16,// 2
	(unsigned char)TT_N16,// 3
	(unsigned char)TT_N8,// 4
	(unsigned char)TT_N8,// 5
	(unsigned char)TT_N16,// 6
	(unsigned char)TT_N16,// 7
	// MvexTupleTypeLutKind.Float32_4to16
	(unsigned char)TT_N16,// 0
	(unsigned char)TT_N1,// 1
	(unsigned char)TT_N1,// 2
	(unsigned char)TT_N8,// 3
	(unsigned char)TT_N4,// 4
	(unsigned char)TT_N4,// 5
	(unsigned char)TT_N8,// 6
	(unsigned char)TT_N8,// 7
	// MvexTupleTypeLutKind.Float32_1to16_or_elem
	(unsigned char)TT_N4,// 0
	(unsigned char)TT_N1,// 1
	(unsigned char)TT_N1,// 2
	(unsigned char)TT_N2,// 3
	(unsigned char)TT_N1,// 4
	(unsigned char)TT_N1,// 5
	(unsigned char)TT_N2,// 6
	(unsigned char)TT_N2,// 7
	// MvexTupleTypeLutKind.Float64
	(unsigned char)TT_N64,// 0
	(unsigned char)TT_N8,// 1
	(unsigned char)TT_N32,// 2
	(unsigned char)TT_N16,// 3
	(unsigned char)TT_N8,// 4
	(unsigned char)TT_N8,// 5
	(unsigned char)TT_N16,// 6
	(unsigned char)TT_N16,// 7
	// MvexTupleTypeLutKind.Float64_4to8
	(unsigned char)TT_N32,// 0
	(unsigned char)TT_N1,// 1
	(unsigned char)TT_N1,// 2
	(unsigned char)TT_N8,// 3
	(unsigned char)TT_N4,// 4
	(unsigned char)TT_N4,// 5
	(unsigned char)TT_N8,// 6
	(unsigned char)TT_N8,// 7
	// MvexTupleTypeLutKind.Float64_1to8_or_elem
	(unsigned char)TT_N8,// 0
	(unsigned char)TT_N1,// 1
	(unsigned char)TT_N1,// 2
	(unsigned char)TT_N2,// 3
	(unsigned char)TT_N1,// 4
	(unsigned char)TT_N1,// 5
	(unsigned char)TT_N2,// 6
	(unsigned char)TT_N2,// 7
};

unsigned char MvexMemorySizeLut_Data[112] =
{
	// MvexTupleTypeLutKind.Int32
	(unsigned char)MS_Packed512_Int32,// 0
	(unsigned char)MS_Int32,// 1
	(unsigned char)MS_Packed128_Int32,// 2
	(unsigned char)MS_Packed256_Float16,// 3
	(unsigned char)MS_Packed128_UInt8,// 4
	(unsigned char)MS_Packed128_Int8,// 5
	(unsigned char)MS_Packed256_UInt16,// 6
	(unsigned char)MS_Packed256_Int16,// 7
	// MvexTupleTypeLutKind.Int32_Half
	(unsigned char)MS_Packed256_Int32,// 0
	(unsigned char)MS_Int32,// 1
	(unsigned char)MS_Packed128_Int32,// 2
	(unsigned char)MS_Packed128_Float16,// 3
	(unsigned char)MS_Packed64_UInt8,// 4
	(unsigned char)MS_Packed64_Int8,// 5
	(unsigned char)MS_Packed128_UInt16,// 6
	(unsigned char)MS_Packed128_Int16,// 7
	// MvexTupleTypeLutKind.Int32_4to16
	(unsigned char)MS_Packed128_Int32,// 0
	(unsigned char)MS_Unknown,// 1
	(unsigned char)MS_Unknown,// 2
	(unsigned char)MS_Packed64_Float16,// 3
	(unsigned char)MS_Packed32_UInt8,// 4
	(unsigned char)MS_Packed32_Int8,// 5
	(unsigned char)MS_Packed64_UInt16,// 6
	(unsigned char)MS_Packed64_Int16,// 7
	// MvexTupleTypeLutKind.Int32_1to16_or_elem
	(unsigned char)MS_Int32,// 0
	(unsigned char)MS_Unknown,// 1
	(unsigned char)MS_Unknown,// 2
	(unsigned char)MS_Float16,// 3
	(unsigned char)MS_UInt8,// 4
	(unsigned char)MS_Int8,// 5
	(unsigned char)MS_UInt16,// 6
	(unsigned char)MS_Int16,// 7
	// MvexTupleTypeLutKind.Int64
	(unsigned char)MS_Packed512_Int64,// 0
	(unsigned char)MS_Int64,// 1
	(unsigned char)MS_Packed256_Int64,// 2
	(unsigned char)MS_Packed128_Float16,// 3
	(unsigned char)MS_Packed64_UInt8,// 4
	(unsigned char)MS_Packed64_Int8,// 5
	(unsigned char)MS_Packed128_UInt16,// 6
	(unsigned char)MS_Packed128_Int16,// 7
	// MvexTupleTypeLutKind.Int64_4to8
	(unsigned char)MS_Packed256_Int64,// 0
	(unsigned char)MS_Unknown,// 1
	(unsigned char)MS_Unknown,// 2
	(unsigned char)MS_Packed64_Float16,// 3
	(unsigned char)MS_Packed32_UInt8,// 4
	(unsigned char)MS_Packed32_Int8,// 5
	(unsigned char)MS_Packed64_UInt16,// 6
	(unsigned char)MS_Packed64_Int16,// 7
	// MvexTupleTypeLutKind.Int64_1to8_or_elem
	(unsigned char)MS_Int64,// 0
	(unsigned char)MS_Unknown,// 1
	(unsigned char)MS_Unknown,// 2
	(unsigned char)MS_Float16,// 3
	(unsigned char)MS_UInt8,// 4
	(unsigned char)MS_Int8,// 5
	(unsigned char)MS_UInt16,// 6
	(unsigned char)MS_Int16,// 7
	// MvexTupleTypeLutKind.Float32
	(unsigned char)MS_Packed512_Float32,// 0
	(unsigned char)MS_Float32,// 1
	(unsigned char)MS_Packed128_Float32,// 2
	(unsigned char)MS_Packed256_Float16,// 3
	(unsigned char)MS_Packed128_UInt8,// 4
	(unsigned char)MS_Packed128_Int8,// 5
	(unsigned char)MS_Packed256_UInt16,// 6
	(unsigned char)MS_Packed256_Int16,// 7
	// MvexTupleTypeLutKind.Float32_Half
	(unsigned char)MS_Packed256_Float32,// 0
	(unsigned char)MS_Float32,// 1
	(unsigned char)MS_Packed128_Float32,// 2
	(unsigned char)MS_Packed128_Float16,// 3
	(unsigned char)MS_Packed64_UInt8,// 4
	(unsigned char)MS_Packed64_Int8,// 5
	(unsigned char)MS_Packed128_UInt16,// 6
	(unsigned char)MS_Packed128_Int16,// 7
	// MvexTupleTypeLutKind.Float32_4to16
	(unsigned char)MS_Packed128_Float32,// 0
	(unsigned char)MS_Unknown,// 1
	(unsigned char)MS_Unknown,// 2
	(unsigned char)MS_Packed64_Float16,// 3
	(unsigned char)MS_Packed32_UInt8,// 4
	(unsigned char)MS_Packed32_Int8,// 5
	(unsigned char)MS_Packed64_UInt16,// 6
	(unsigned char)MS_Packed64_Int16,// 7
	// MvexTupleTypeLutKind.Float32_1to16_or_elem
	(unsigned char)MS_Float32,// 0
	(unsigned char)MS_Unknown,// 1
	(unsigned char)MS_Unknown,// 2
	(unsigned char)MS_Float16,// 3
	(unsigned char)MS_UInt8,// 4
	(unsigned char)MS_Int8,// 5
	(unsigned char)MS_UInt16,// 6
	(unsigned char)MS_Int16,// 7
	// MvexTupleTypeLutKind.Float64
	(unsigned char)MS_Packed512_Float64,// 0
	(unsigned char)MS_Float64,// 1
	(unsigned char)MS_Packed256_Float64,// 2
	(unsigned char)MS_Packed128_Float16,// 3
	(unsigned char)MS_Packed64_UInt8,// 4
	(unsigned char)MS_Packed64_Int8,// 5
	(unsigned char)MS_Packed128_UInt16,// 6
	(unsigned char)MS_Packed128_Int16,// 7
	// MvexTupleTypeLutKind.Float64_4to8
	(unsigned char)MS_Packed256_Float64,// 0
	(unsigned char)MS_Unknown,// 1
	(unsigned char)MS_Unknown,// 2
	(unsigned char)MS_Packed64_Float16,// 3
	(unsigned char)MS_Packed32_UInt8,// 4
	(unsigned char)MS_Packed32_Int8,// 5
	(unsigned char)MS_Packed64_UInt16,// 6
	(unsigned char)MS_Packed64_Int16,// 7
	// MvexTupleTypeLutKind.Float64_1to8_or_elem
	(unsigned char)MS_Float64,// 0
	(unsigned char)MS_Unknown,// 1
	(unsigned char)MS_Unknown,// 2
	(unsigned char)MS_Float16,// 3
	(unsigned char)MS_UInt8,// 4
	(unsigned char)MS_Int8,// 5
	(unsigned char)MS_UInt16,// 6
	(unsigned char)MS_Int16,// 7
};

enum MvexEHBit
{
	/// <summary>Not hard coded to 0 or 1 so can be used for other purposes</summary>
	MEHB_None = 0,
	/// <summary>EH bit must be 0</summary>
	MEHB_EH0 = 1,
	/// <summary>EH bit must be 1</summary>
	MEHB_EH1 = 2,
};

enum MvexConvFn {
	/// <summary>No conversion function</summary>
	MCF_None = 0,
	/// <summary>Sf32(xxx)</summary>
	MCF_Sf32 = 1,
	/// <summary>Sf64(xxx)</summary>
	MCF_Sf64 = 2,
	/// <summary>Si32(xxx)</summary>
	MCF_Si32 = 3,
	/// <summary>Si64(xxx)</summary>
	MCF_Si64 = 4,
	/// <summary>Uf32(xxx)</summary>
	MCF_Uf32 = 5,
	/// <summary>Uf64(xxx)</summary>
	MCF_Uf64 = 6,
	/// <summary>Ui32(xxx)</summary>
	MCF_Ui32 = 7,
	/// <summary>Ui64(xxx)</summary>
	MCF_Ui64 = 8,
	/// <summary>Df32(xxx)</summary>
	MCF_Df32 = 9,
	/// <summary>Df64(xxx)</summary>
	MCF_Df64 = 10,
	/// <summary>Di32(xxx)</summary>
	MCF_Di32 = 11,
	/// <summary>Di64(xxx)</summary>
	MCF_Di64 = 12,
};

enum RoundingControl 
{
	/// <summary>No rounding mode</summary>
	RC_None = 0,
	/// <summary>Round to nearest (even)</summary>
	RC_RoundToNearest = 1,
	/// <summary>Round down (toward -inf)</summary>
	RC_RoundDown = 2,
	/// <summary>Round up (toward +inf)</summary>
	RC_RoundUp = 3,
	/// <summary>Round toward zero (truncate)</summary>
	RC_RoundTowardZero = 4,
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
const enum Register VMM_first = Register_ZMM0;
const enum Register VMM_last = Register_ZMM31;
const int VMM_count = 32;
const enum Register XMM_last = Register_XMM31;
const enum Register YMM_last = Register_YMM31;
const enum Register ZMM_last = Register_ZMM31;
const enum Register TMM_last = Register_TMM7;
const int MaxCpuidFeatureInternalValues = 199;
const enum MemorySize FirstBroadcastMemorySize = MS_Broadcast32_Float16;
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

struct Instruction* instruction_new();
void instruction_init(struct Instruction** o);
bool instruction_equals(struct Instruction* a, struct Instruction* b);
bool instruction_equals_allbits(struct Instruction* a, struct Instruction* b);
int gethashcode(struct Instruction* i);

enum Mnemonic to_mnemonic(enum Code code);

/// <summary>
/// Sets an operand's kind
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <param name="opKind">Operand kind</param>
void SetOpKind(struct Instruction* i, int operand, enum OpKind opKind);

void SetIP(struct Instruction* i, unsigned long value);
void Set_HasLockPrefix(struct Instruction* i, bool value);
void Set_HasRepePrefix(struct Instruction* i, bool value);
void Set_HasRepnePrefix(struct Instruction* i, bool value);
void Set_SegmentPrefix(struct Instruction* i, enum Register value);
void SetZeroingMasking(struct Instruction* i, bool value);
void SetOpMask(struct Instruction* i, enum Register value);
void SetSuppressAllExceptions(struct Instruction* i, bool value);
void SetRoundingControl(struct Instruction* i, enum RoundingControl value);
void SetIsBroadcast(struct Instruction* i, bool value);
void SetCode(struct Instruction* i, enum Code value);
void SetOp0Kind(struct Instruction* i, enum OpKind value);
void SetOp1Kind(struct Instruction* i, enum OpKind value);
void SetOp2Kind(struct Instruction* i, enum OpKind value);
void SetOp3Kind(struct Instruction* i, enum OpKind value);
void SetFarBranchSelector(struct Instruction* i, unsigned short value);
void SetFarBranch32(struct Instruction* i, unsigned int value);
void SetNearBranch64(struct Instruction* i, unsigned long value);
void SetOp0Register(struct Instruction* i, enum Register value);
void SetOp1Register(struct Instruction* i, enum Register value);
void SetInternalImmediate8(struct Instruction* i, unsigned int value);
void SetInternalImmediate8_2nd(struct Instruction* i, unsigned int value);
void SetInternalImmediate16(struct Instruction* i, unsigned int value);
void SetImmediate32(struct Instruction* i, unsigned int value);
void SetImmediate64(struct Instruction* i, unsigned long value);

void SetInternalMemoryBase(struct Instruction* i, enum Register value);
void SetInternalMemoryIndex(struct Instruction* i, enum Register value);
void SetMemoryIndexScale(struct Instruction* i, int value);
void SetMemoryDisplSize(struct Instruction* i, int value);
void SetMemoryDisplacement64(struct Instruction* i, unsigned long value);

void InternalSetHasRepePrefix(struct Instruction* i);
void InternalSetHasRepnePrefix(struct Instruction* i);

void SetInternalDeclareDataCount(struct Instruction* i, unsigned int value);
void Instruction_SetDeclareByteValue(struct Instruction* i, int index, unsigned char value);
void Instruction_SetDeclareDwordValue(struct Instruction* i, int index, unsigned int value);
void Instruction_SetDeclareQwordValue(struct Instruction* i, int index, unsigned long value);
void Instruction_SetDeclareWordValue(struct Instruction* i, int index, unsigned short value);

void Instruction_SetImmediate(struct Instruction* i, int operand, unsigned long immediate);
void SetImmediateLong(struct Instruction* i, int operand, long immediate);
void SetImmediateUInt(struct Instruction* i, int operand, unsigned int immediate);
void SetImmediateInt(struct Instruction* i, int operand, int immediate);

int GetDeclareDataCount(struct Instruction* i);
unsigned char GetDeclareByteValue(struct Instruction* i, int index);
enum Register GetMemoryBase(struct Instruction* i);
int GetMemoryDisplSize(struct Instruction* i);
unsigned long GetMemoryDisplacement64(struct Instruction* i);
int GetMemoryIndexScale(struct Instruction* i);

unsigned long GetNearBranch64(struct Instruction* i);
unsigned int GetNearBranch32(struct Instruction* i);
unsigned short GetNearBranch16(struct Instruction* i);

/// <summary>
/// Instruction code, see also <see cref="Mnemonic"/>
/// </summary>
enum Code GetCode(struct Instruction* i);

enum Register GetMemoryIndex(struct Instruction* i);
unsigned char GetImmediate8(struct Instruction* i);
unsigned short GetImmediate16(struct Instruction* i);

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate32"/>
/// </summary>
unsigned int GetImmediate32(struct Instruction* i);

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate64"/>
/// </summary>
unsigned long GetImmediate64(struct Instruction* i);

/// <summary>
/// Gets the operand's immediate value. Use this property if the operand has kind <see cref="OpKind.Immediate8_2nd"/>
/// </summary>
unsigned char GetImmediate8_2nd(struct Instruction* i);

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.FarBranch16"/>
/// </summary>
unsigned short GetFarBranch16(struct Instruction* i);

/// <summary>
/// Gets the operand's branch target. Use this property if the operand has kind <see cref="OpKind.FarBranch32"/>
/// </summary>
unsigned int GetFarBranch32(struct Instruction* i);

/// <summary>
/// Gets the operand's branch target selector. Use this property if the operand has kind <see cref="OpKind.FarBranch16"/> or <see cref="OpKind.FarBranch32"/>
/// </summary>
unsigned short GetFarBranchSelector(struct Instruction* i);

/// <summary>
/// Gets the memory operand's displacement or the 32-bit absolute address if it's
/// an <c>EIP</c> or <c>RIP</c> relative memory operand.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>
/// </summary>
unsigned int GetMemoryDisplacement32(struct Instruction* i);

/// <summary>
/// Gets the operand's register value. Use this property if the operand has kind <see cref="OpKind.Register"/>
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <returns></returns>
enum Register GetOpRegister(struct Instruction* i, int operand);

enum OpKind Instruction_GetOpKind(struct Instruction* i, int operand);

/// <summary>
/// Gets the size of the memory location that is referenced by the operand. See also <see cref="IsBroadcast"/>.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>,
/// <see cref="OpKind.MemorySegSI"/>, <see cref="OpKind.MemorySegESI"/>, <see cref="OpKind.MemorySegRSI"/>,
/// <see cref="OpKind.MemoryESDI"/>, <see cref="OpKind.MemoryESEDI"/>, <see cref="OpKind.MemoryESRDI"/>
/// </summary>
enum MemorySize Instruction_GetMemorySize(struct Instruction* i);

/// <summary>
/// Gets the code size when the instruction was decoded. This value is informational and can
/// be used by a formatter.
/// </summary>
enum CodeSize GetCodeSize(struct Instruction* i);

/// <summary>
/// Gets operand #0's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
enum OpKind GetOp0Kind(struct Instruction* i);

/// <summary>
/// Gets operand #0's register value. Use this property if operand #0 (<see cref="Op0Kind"/>) has kind <see cref="OpKind.Register"/>, see <see cref="OpCount"/> and <see cref="GetOpRegister(int)"/>
/// </summary>
enum Register GetOp0Register(struct Instruction* i);

unsigned int GetInternalOpMask(struct Instruction* i);

/// <summary>
/// <see langword="true"/> if the data is broadcast (EVEX instructions only)
/// </summary>
bool IsBroadcast2(struct Instruction* i);

/// <summary>
/// <see langword="true"/> if zeroing-masking, <see langword="false"/> if merging-masking.
/// Only used by most EVEX encoded instructions that use opmask registers.
/// </summary>
bool GetZeroingMasking(struct Instruction* i);

/// <summary>
/// Rounding control (SAE is implied but <see cref="SuppressAllExceptions"/> still returns <see langword="false"/>)
/// or <see cref="RoundingControl.None"/> if the instruction doesn't use it.
/// </summary>
enum RoundingControl GetRoundingControl(struct Instruction* i);

/// <summary>
/// Suppress all exceptions (EVEX/MVEX encoded instructions). Note that if <see cref="RoundingControl"/> is
/// not <see cref="RoundingControl.None"/>, SAE is implied but this property will still return <see langword="false"/>.
/// </summary>
bool GetSuppressAllExceptions(struct Instruction* i);

/// <summary>
/// (MVEX) Register/memory operand conversion function
/// </summary>
enum MvexRegMemConv GetMvexRegMemConv(struct Instruction* i);

/// <summary>
/// Gets the segment override prefix or <see cref="Register.None"/> if none. See also <see cref="MemorySegment"/>.
/// Use this property if the operand has kind <see cref="OpKind.Memory"/>,
/// <see cref="OpKind.MemorySegSI"/>, <see cref="OpKind.MemorySegESI"/>, <see cref="OpKind.MemorySegRSI"/>
/// </summary>
enum Register Get_SegmentPrefix(struct Instruction* i);

/// <summary>
/// <see langword="true"/> if the instruction has the <c>REPE</c> or <c>REP</c> prefix (<c>F3</c>)
/// </summary>
bool Get_HasRepePrefix(struct Instruction* i);

/// <summary>
/// <see langword="true"/> if the instruction has the <c>LOCK</c> prefix (<c>F0</c>)
/// </summary>
bool Get_HasLockPrefix(struct Instruction* i);

/// <summary>
/// <see langword="true"/> if the instruction has the <c>REPNE</c> prefix (<c>F2</c>)
/// </summary>
bool Get_HasRepnePrefix(struct Instruction* i);

/// <summary>
/// Gets operand #1's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
enum OpKind GetOp1Kind(struct Instruction* i);

/// <summary>
/// Gets operand #2's kind if the operand exists (see <see cref="OpCount"/> and <see cref="GetOpKind(int)"/>)
/// </summary>
enum OpKind GetOp2Kind(struct Instruction* i);

/// <summary>
/// <see langword="true"/> if eviction hint bit is set (<c>{eh}</c>) (MVEX instructions only)
/// </summary>
bool GetIsMvexEvictionHint(struct Instruction* i);

bool IsMvexEvictionHint(struct Instruction* i);

bool TryGetVsib64(struct Instruction* i, bool* vsib64);

#endif