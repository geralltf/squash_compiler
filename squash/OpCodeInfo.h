#ifndef OPCODE_INFO_H
#define OPCODE_INFO_H

#include <stdio.h>

#include "sb.h"
#include "Instruction.h"
#include <stdlib.h>

enum Flags // : uint 
{
	FLAGS_None = 0,
	FLAGS_IgnoresRoundingControl = 0x00000001,
	FLAGS_AmdLockRegBit = 0x00000002,
	FLAGS_LIG = 0x00000004,
	FLAGS_W = 0x00000008,
	FLAGS_WIG = 0x00000010,
	FLAGS_WIG32 = 0x00000020,
	FLAGS_CPL0 = 0x00000040,
	FLAGS_CPL1 = 0x00000080,
	FLAGS_CPL2 = 0x00000100,
	FLAGS_CPL3 = 0x00000200,
};

enum OpCodeInfoFlags1 // : uint 
{
	OCIF1_None = 0x00000000,
	OCIF1_Cpl0Only = 0x00000001,
	OCIF1_Cpl3Only = 0x00000002,
	OCIF1_InputOutput = 0x00000004,
	OCIF1_Nop = 0x00000008,
	OCIF1_ReservedNop = 0x00000010,
	OCIF1_SerializingIntel = 0x00000020,
	OCIF1_SerializingAmd = 0x00000040,
	OCIF1_MayRequireCpl0 = 0x00000080,
	OCIF1_CetTracked = 0x00000100,
	OCIF1_NonTemporal = 0x00000200,
	OCIF1_FpuNoWait = 0x00000400,
	OCIF1_IgnoresModBits = 0x00000800,
	OCIF1_No66 = 0x00001000,
	OCIF1_NFx = 0x00002000,
	OCIF1_RequiresUniqueRegNums = 0x00004000,
	OCIF1_Privileged = 0x00008000,
	OCIF1_SaveRestore = 0x00010000,
	OCIF1_StackInstruction = 0x00020000,
	OCIF1_IgnoresSegment = 0x00040000,
	OCIF1_OpMaskReadWrite = 0x00080000,
	OCIF1_ModRegRmString = 0x00100000,
	OCIF1_DecOptionValueMask = 0x0000001F,
	OCIF1_DecOptionValueShift = 0x00000015,
	OCIF1_ForceOpSize64 = 0x40000000,
	OCIF1_RequiresUniqueDestRegNum = 0x80000000,
};

enum OpCodeInfoFlags2 // : uint
{
	OCIF2_None = 0x00000000,
	OCIF2_RealMode = 0x00000001,
	OCIF2_ProtectedMode = 0x00000002,
	OCIF2_Virtual8086Mode = 0x00000004,
	OCIF2_CompatibilityMode = 0x00000008,
	OCIF2_UseOutsideSmm = 0x00000010,
	OCIF2_UseInSmm = 0x00000020,
	OCIF2_UseOutsideEnclaveSgx = 0x00000040,
	OCIF2_UseInEnclaveSgx1 = 0x00000080,
	OCIF2_UseInEnclaveSgx2 = 0x00000100,
	OCIF2_UseOutsideVmxOp = 0x00000200,
	OCIF2_UseInVmxRootOp = 0x00000400,
	OCIF2_UseInVmxNonRootOp = 0x00000800,
	OCIF2_UseOutsideSeam = 0x00001000,
	OCIF2_UseInSeam = 0x00002000,
	OCIF2_TdxNonRootGenUd = 0x00004000,
	OCIF2_TdxNonRootGenVe = 0x00008000,
	OCIF2_TdxNonRootMayGenEx = 0x00010000,
	OCIF2_IntelVmExit = 0x00020000,
	OCIF2_IntelMayVmExit = 0x00040000,
	OCIF2_IntelSmmVmExit = 0x00080000,
	OCIF2_AmdVmExit = 0x00100000,
	OCIF2_AmdMayVmExit = 0x00200000,
	OCIF2_TsxAbort = 0x00400000,
	OCIF2_TsxImplAbort = 0x00800000,
	OCIF2_TsxMayAbort = 0x01000000,
	OCIF2_IntelDecoder16or32 = 0x02000000,
	OCIF2_IntelDecoder64 = 0x04000000,
	OCIF2_AmdDecoder16or32 = 0x08000000,
	OCIF2_AmdDecoder64 = 0x10000000,
	OCIF2_InstrStrFmtOptionMask = 0x00000007,
	OCIF2_InstrStrFmtOptionShift = 0x0000001D,
};

enum InstrStrFmtOption
{
	ISFO_None,
	ISFO_OpMaskIsK1_or_NoGprSuffix,
	ISFO_IncVecIndex,
	ISFO_NoVecIndex,
	ISFO_SwapVecIndex12,
	ISFO_SkipOp0,
	ISFO_VecIndexSameAsOpIndex,
};

enum EncFlags1 // : uint 
{
	EFLAGS_None = 0x00000000,
	EFLAGS_Legacy_OpMask = 0x0000007F,
	EFLAGS_Legacy_Op0Shift = 0x00000000,
	EFLAGS_Legacy_Op1Shift = 0x00000007,
	EFLAGS_Legacy_Op2Shift = 0x0000000E,
	EFLAGS_Legacy_Op3Shift = 0x00000015,
	EFLAGS_VEX_OpMask = 0x0000003F,
	EFLAGS_VEX_Op0Shift = 0x00000000,
	EFLAGS_VEX_Op1Shift = 0x00000006,
	EFLAGS_VEX_Op2Shift = 0x0000000C,
	EFLAGS_VEX_Op3Shift = 0x00000012,
	EFLAGS_VEX_Op4Shift = 0x00000018,
	EFLAGS_XOP_OpMask = 0x0000001F,
	EFLAGS_XOP_Op0Shift = 0x00000000,
	EFLAGS_XOP_Op1Shift = 0x00000005,
	EFLAGS_XOP_Op2Shift = 0x0000000A,
	EFLAGS_XOP_Op3Shift = 0x0000000F,
	EFLAGS_EVEX_OpMask = 0x0000001F,
	EFLAGS_EVEX_Op0Shift = 0x00000000,
	EFLAGS_EVEX_Op1Shift = 0x00000005,
	EFLAGS_EVEX_Op2Shift = 0x0000000A,
	EFLAGS_EVEX_Op3Shift = 0x0000000F,
	EFLAGS_MVEX_OpMask = 0x0000000F,
	EFLAGS_MVEX_Op0Shift = 0x00000000,
	EFLAGS_MVEX_Op1Shift = 0x00000004,
	EFLAGS_MVEX_Op2Shift = 0x00000008,
	EFLAGS_MVEX_Op3Shift = 0x0000000C,
	EFLAGS_IgnoresRoundingControl = 0x40000000,
	EFLAGS_AmdLockRegBit = 0x80000000,
};

enum EncFlags2 // : uint 
{
	EFLAGS2_None = 0x00000000,
	EFLAGS2_OpCodeShift = 0x00000000,
	EFLAGS2_OpCodeIs2Bytes = 0x00010000,
	EFLAGS2_TableShift = 0x00000011,
	EFLAGS2_TableMask = 0x00000007,
	EFLAGS2_MandatoryPrefixShift = 0x00000014,
	EFLAGS2_MandatoryPrefixMask = 0x00000003,
	EFLAGS2_WBitShift = 0x00000016,
	EFLAGS2_WBitMask = 0x00000003,
	EFLAGS2_LBitShift = 0x00000018,
	EFLAGS2_LBitMask = 0x00000007,
	EFLAGS2_GroupIndexShift = 0x0000001B,
	EFLAGS2_GroupIndexMask = 0x00000007,
	EFLAGS2_HasMandatoryPrefix = 0x40000000,
	EFLAGS2_HasGroupIndex = 0x80000000,
};

enum EncFlags3 // : uint 
{
	EFLAGS3_None = 0x00000000,
	EFLAGS3_EncodingShift = 0x00000000,
	EFLAGS3_EncodingMask = 0x00000007,
	EFLAGS3_OperandSizeShift = 0x00000003,
	EFLAGS3_OperandSizeMask = 0x00000003,
	EFLAGS3_AddressSizeShift = 0x00000005,
	EFLAGS3_AddressSizeMask = 0x00000003,
	EFLAGS3_TupleTypeShift = 0x00000007,
	EFLAGS3_TupleTypeMask = 0x0000001F,
	EFLAGS3_DefaultOpSize64 = 0x00001000,
	EFLAGS3_HasRmGroupIndex = 0x00002000,
	EFLAGS3_IntelForceOpSize64 = 0x00004000,
	EFLAGS3_Fwait = 0x00008000,
	EFLAGS3_Bit16or32 = 0x00010000,
	EFLAGS3_Bit64 = 0x00020000,
	EFLAGS3_Lock = 0x00040000,
	EFLAGS3_Xacquire = 0x00080000,
	EFLAGS3_Xrelease = 0x00100000,
	EFLAGS3_Rep = 0x00200000,
	EFLAGS3_Repne = 0x00400000,
	EFLAGS3_Bnd = 0x00800000,
	EFLAGS3_HintTaken = 0x01000000,
	EFLAGS3_Notrack = 0x02000000,
	EFLAGS3_Broadcast = 0x04000000,
	EFLAGS3_RoundingControl = 0x08000000,
	EFLAGS3_SuppressAllExceptions = 0x10000000,
	EFLAGS3_OpMaskRegister = 0x20000000,
	EFLAGS3_ZeroingMasking = 0x40000000,
	EFLAGS3_RequireOpMaskRegister = 0x80000000,
};

enum MandatoryPrefixByte // : uint
{
	MPB_None,
	MPB_P66,
	MPB_PF3,
	MPB_PF2,
};

enum MandatoryPrefix 
{
	/// <summary>No mandatory prefix (legacy and 3DNow! tables only)</summary>
	MP_None = 0,
	/// <summary>Empty mandatory prefix (no <c>66</c>, <c>F3</c> or <c>F2</c> prefix)</summary>
	MP_PNP = 1,
	/// <summary><c>66</c> prefix</summary>
	MP_P66 = 2,
	/// <summary><c>F3</c> prefix</summary>
	MP_PF3 = 3,
	/// <summary><c>F2</c> prefix</summary>
	MP_PF2 = 4,
};

enum CodeSize 
{
	/// <summary>Unknown size</summary>
	CSIZE_Unknown = 0,
	/// <summary>16-bit code</summary>
	CSIZE_Code16 = 1,
	/// <summary>32-bit code</summary>
	CSIZE_Code32 = 2,
	/// <summary>64-bit code</summary>
	CSIZE_Code64 = 3,
};

enum LKind // : byte 
{
	LK_None,
	/// <summary>.128, .256, .512</summary>
	LK_L128,
	/// <summary>.L0, .L1</summary>
	LK_L0,
	/// <summary>.LZ</summary>
	LK_LZ,
};

enum WBit // : uint 
{
	WBit_W0,
	WBit_W1,
	WBit_WIG,
	WBit_WIG32,
};

enum LBit // : uint 
{
	LBit_L0,
	LBit_L1,
	LBit_LIG,
	LBit_LZ,
	LBit_L128,
	LBit_L256,
	LBit_L512,
};

enum EncodingKind 
{
	/// <summary>Legacy encoding</summary>
	EncodingKind_Legacy = 0,
	/// <summary>VEX encoding</summary>
	EncodingKind_VEX = 1,
	/// <summary>EVEX encoding</summary>
	EncodingKind_EVEX = 2,
	/// <summary>XOP encoding</summary>
	EncodingKind_XOP = 3,
	/// <summary>3DNow! encoding</summary>
	EncodingKind_D3NOW = 4,
	/// <summary>MVEX encoding</summary>
	EncodingKind_MVEX = 5,
};

/// <summary>Operand kind</summary>
enum OpCodeOperandKind {
	/// <summary>No operand</summary>
	OpCodeOperandKind_None = 0,
	/// <summary>Far branch 16-bit offset, 16-bit segment/selector</summary>
	OpCodeOperandKind_farbr2_2 = 1,
	/// <summary>Far branch 32-bit offset, 16-bit segment/selector</summary>
	OpCodeOperandKind_farbr4_2 = 2,
	/// <summary>Memory offset without a modrm byte (eg. <c>MOV AL,[offset]</c>)</summary>
	OpCodeOperandKind_mem_offs = 3,
	/// <summary>Memory (modrm)</summary>
	OpCodeOperandKind_mem = 4,
	/// <summary>Memory (modrm), MPX:<br/>
	/// <br/>
	/// 16/32-bit mode: must be 32-bit addressing<br/>
	/// <br/>
	/// 64-bit mode: 64-bit addressing is forced and must not be RIP relative</summary>
	OpCodeOperandKind_mem_mpx = 5,
	/// <summary>Memory (modrm), MPX:<br/>
	/// <br/>
	/// 16/32-bit mode: must be 32-bit addressing<br/>
	/// <br/>
	/// 64-bit mode: 64-bit addressing is forced and must not be RIP relative</summary>
	OpCodeOperandKind_mem_mib = 6,
	/// <summary>Memory (modrm), vsib32, <c>XMM</c> registers</summary>
	OpCodeOperandKind_mem_vsib32x = 7,
	/// <summary>Memory (modrm), vsib64, <c>XMM</c> registers</summary>
	OpCodeOperandKind_mem_vsib64x = 8,
	/// <summary>Memory (modrm), vsib32, <c>YMM</c> registers</summary>
	OpCodeOperandKind_mem_vsib32y = 9,
	/// <summary>Memory (modrm), vsib64, <c>YMM</c> registers</summary>
	OpCodeOperandKind_mem_vsib64y = 10,
	/// <summary>Memory (modrm), vsib32, <c>ZMM</c> registers</summary>
	OpCodeOperandKind_mem_vsib32z = 11,
	/// <summary>Memory (modrm), vsib64, <c>ZMM</c> registers</summary>
	OpCodeOperandKind_mem_vsib64z = 12,
	/// <summary>8-bit GPR or memory</summary>
	OpCodeOperandKind_r8_or_mem = 13,
	/// <summary>16-bit GPR or memory</summary>
	OpCodeOperandKind_r16_or_mem = 14,
	/// <summary>32-bit GPR or memory</summary>
	OpCodeOperandKind_r32_or_mem = 15,
	/// <summary>32-bit GPR or memory, MPX: 16/32-bit mode: must be 32-bit addressing, 64-bit mode: 64-bit addressing is forced</summary>
	OpCodeOperandKind_r32_or_mem_mpx = 16,
	/// <summary>64-bit GPR or memory</summary>
	OpCodeOperandKind_r64_or_mem = 17,
	/// <summary>64-bit GPR or memory, MPX: 16/32-bit mode: must be 32-bit addressing, 64-bit mode: 64-bit addressing is forced</summary>
	OpCodeOperandKind_r64_or_mem_mpx = 18,
	/// <summary><c>MM</c> register or memory</summary>
	OpCodeOperandKind_mm_or_mem = 19,
	/// <summary><c>XMM</c> register or memory</summary>
	OpCodeOperandKind_xmm_or_mem = 20,
	/// <summary><c>YMM</c> register or memory</summary>
	OpCodeOperandKind_ymm_or_mem = 21,
	/// <summary><c>ZMM</c> register or memory</summary>
	OpCodeOperandKind_zmm_or_mem = 22,
	/// <summary><c>BND</c> register or memory, MPX: 16/32-bit mode: must be 32-bit addressing, 64-bit mode: 64-bit addressing is forced</summary>
	OpCodeOperandKind_bnd_or_mem_mpx = 23,
	/// <summary><c>K</c> register or memory</summary>
	OpCodeOperandKind_k_or_mem = 24,
	/// <summary>8-bit GPR encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_r8_reg = 25,
	/// <summary>8-bit GPR encoded in the low 3 bits of the opcode</summary>
	OpCodeOperandKind_r8_opcode = 26,
	/// <summary>16-bit GPR encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_r16_reg = 27,
	/// <summary>16-bit GPR encoded in the <c>reg</c> field of the modrm byte. This is a memory operand and it uses the address size prefix (<c>67h</c>) not the operand size prefix (<c>66h</c>).</summary>
	OpCodeOperandKind_r16_reg_mem = 28,
	/// <summary>16-bit GPR encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_r16_rm = 29,
	/// <summary>16-bit GPR encoded in the low 3 bits of the opcode</summary>
	OpCodeOperandKind_r16_opcode = 30,
	/// <summary>32-bit GPR encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_r32_reg = 31,
	/// <summary>32-bit GPR encoded in the <c>reg</c> field of the modrm byte. This is a memory operand and it uses the address size prefix (<c>67h</c>) not the operand size prefix (<c>66h</c>).</summary>
	OpCodeOperandKind_r32_reg_mem = 32,
	/// <summary>32-bit GPR encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_r32_rm = 33,
	/// <summary>32-bit GPR encoded in the low 3 bits of the opcode</summary>
	OpCodeOperandKind_r32_opcode = 34,
	/// <summary>32-bit GPR encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/XOP)</summary>
	OpCodeOperandKind_r32_vvvv = 35,
	/// <summary>64-bit GPR encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_r64_reg = 36,
	/// <summary>64-bit GPR encoded in the <c>reg</c> field of the modrm byte. This is a memory operand and it uses the address size prefix (<c>67h</c>) not the operand size prefix (<c>66h</c>).</summary>
	OpCodeOperandKind_r64_reg_mem = 37,
	/// <summary>64-bit GPR encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_r64_rm = 38,
	/// <summary>64-bit GPR encoded in the low 3 bits of the opcode</summary>
	OpCodeOperandKind_r64_opcode = 39,
	/// <summary>64-bit GPR encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/XOP)</summary>
	OpCodeOperandKind_r64_vvvv = 40,
	/// <summary>Segment register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_seg_reg = 41,
	/// <summary><c>K</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_k_reg = 42,
	/// <summary><c>K</c> register (+1) encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_kp1_reg = 43,
	/// <summary><c>K</c> register encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_k_rm = 44,
	/// <summary><c>K</c> register encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/MVEX/XOP)</summary>
	OpCodeOperandKind_k_vvvv = 45,
	/// <summary><c>MM</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_mm_reg = 46,
	/// <summary><c>MM</c> register encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_mm_rm = 47,
	/// <summary><c>XMM</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_xmm_reg = 48,
	/// <summary><c>XMM</c> register encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_xmm_rm = 49,
	/// <summary><c>XMM</c> register encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/XOP)</summary>
	OpCodeOperandKind_xmm_vvvv = 50,
	/// <summary><c>XMM</c> register (+3) encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/XOP)</summary>
	OpCodeOperandKind_xmmp3_vvvv = 51,
	/// <summary><c>XMM</c> register encoded in the the high 4 bits of the last 8-bit immediate (VEX/XOP only so only <c>XMM0</c>-<c>XMM15</c>)</summary>
	OpCodeOperandKind_xmm_is4 = 52,
	/// <summary><c>XMM</c> register encoded in the the high 4 bits of the last 8-bit immediate (VEX/XOP only so only <c>XMM0</c>-<c>XMM15</c>)</summary>
	OpCodeOperandKind_xmm_is5 = 53,
	/// <summary><c>YMM</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_ymm_reg = 54,
	/// <summary><c>YMM</c> register encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_ymm_rm = 55,
	/// <summary><c>YMM</c> register encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/XOP)</summary>
	OpCodeOperandKind_ymm_vvvv = 56,
	/// <summary><c>YMM</c> register encoded in the the high 4 bits of the last 8-bit immediate (VEX/XOP only so only <c>YMM0</c>-<c>YMM15</c>)</summary>
	OpCodeOperandKind_ymm_is4 = 57,
	/// <summary><c>YMM</c> register encoded in the the high 4 bits of the last 8-bit immediate (VEX/XOP only so only <c>YMM0</c>-<c>YMM15</c>)</summary>
	OpCodeOperandKind_ymm_is5 = 58,
	/// <summary><c>ZMM</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_zmm_reg = 59,
	/// <summary><c>ZMM</c> register encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_zmm_rm = 60,
	/// <summary><c>ZMM</c> register encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/MVEX/XOP)</summary>
	OpCodeOperandKind_zmm_vvvv = 61,
	/// <summary><c>ZMM</c> register (+3) encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/XOP)</summary>
	OpCodeOperandKind_zmmp3_vvvv = 62,
	/// <summary><c>CR</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_cr_reg = 63,
	/// <summary><c>DR</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_dr_reg = 64,
	/// <summary><c>TR</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_tr_reg = 65,
	/// <summary><c>BND</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_bnd_reg = 66,
	/// <summary><c>ES</c> register</summary>
	OpCodeOperandKind_es = 67,
	/// <summary><c>CS</c> register</summary>
	OpCodeOperandKind_cs = 68,
	/// <summary><c>SS</c> register</summary>
	OpCodeOperandKind_ss = 69,
	/// <summary><c>DS</c> register</summary>
	OpCodeOperandKind_ds = 70,
	/// <summary><c>FS</c> register</summary>
	OpCodeOperandKind_fs = 71,
	/// <summary><c>GS</c> register</summary>
	OpCodeOperandKind_gs = 72,
	/// <summary><c>AL</c> register</summary>
	OpCodeOperandKind_al = 73,
	/// <summary><c>CL</c> register</summary>
	OpCodeOperandKind_cl = 74,
	/// <summary><c>AX</c> register</summary>
	OpCodeOperandKind_ax = 75,
	/// <summary><c>DX</c> register</summary>
	OpCodeOperandKind_dx = 76,
	/// <summary><c>EAX</c> register</summary>
	OpCodeOperandKind_eax = 77,
	/// <summary><c>RAX</c> register</summary>
	OpCodeOperandKind_rax = 78,
	/// <summary><c>ST(0)</c> register</summary>
	OpCodeOperandKind_st0 = 79,
	/// <summary><c>ST(i)</c> register encoded in the low 3 bits of the opcode</summary>
	OpCodeOperandKind_sti_opcode = 80,
	/// <summary>4-bit immediate (m2z field, low 4 bits of the /is5 immediate, eg. <c>VPERMIL2PS</c>)</summary>
	OpCodeOperandKind_imm4_m2z = 81,
	/// <summary>8-bit immediate</summary>
	OpCodeOperandKind_imm8 = 82,
	/// <summary>Constant 1 (8-bit immediate)</summary>
	OpCodeOperandKind_imm8_const_1 = 83,
	/// <summary>8-bit immediate sign extended to 16 bits</summary>
	OpCodeOperandKind_imm8sex16 = 84,
	/// <summary>8-bit immediate sign extended to 32 bits</summary>
	OpCodeOperandKind_imm8sex32 = 85,
	/// <summary>8-bit immediate sign extended to 64 bits</summary>
	OpCodeOperandKind_imm8sex64 = 86,
	/// <summary>16-bit immediate</summary>
	OpCodeOperandKind_imm16 = 87,
	/// <summary>32-bit immediate</summary>
	OpCodeOperandKind_imm32 = 88,
	/// <summary>32-bit immediate sign extended to 64 bits</summary>
	OpCodeOperandKind_imm32sex64 = 89,
	/// <summary>64-bit immediate</summary>
	OpCodeOperandKind_imm64 = 90,
	/// <summary><c>seg:[rSI]</c> memory operand (string instructions)</summary>
	OpCodeOperandKind_seg_rSI = 91,
	/// <summary><c>es:[rDI]</c> memory operand (string instructions)</summary>
	OpCodeOperandKind_es_rDI = 92,
	/// <summary><c>seg:[rDI]</c> memory operand (<c>(V)MASKMOVQ</c> instructions)</summary>
	OpCodeOperandKind_seg_rDI = 93,
	/// <summary><c>seg:[rBX+al]</c> memory operand (<c>XLATB</c> instruction)</summary>
	OpCodeOperandKind_seg_rBX_al = 94,
	/// <summary>16-bit branch, 1-byte signed relative offset</summary>
	OpCodeOperandKind_br16_1 = 95,
	/// <summary>32-bit branch, 1-byte signed relative offset</summary>
	OpCodeOperandKind_br32_1 = 96,
	/// <summary>64-bit branch, 1-byte signed relative offset</summary>
	OpCodeOperandKind_br64_1 = 97,
	/// <summary>16-bit branch, 2-byte signed relative offset</summary>
	OpCodeOperandKind_br16_2 = 98,
	/// <summary>32-bit branch, 4-byte signed relative offset</summary>
	OpCodeOperandKind_br32_4 = 99,
	/// <summary>64-bit branch, 4-byte signed relative offset</summary>
	OpCodeOperandKind_br64_4 = 100,
	/// <summary><c>XBEGIN</c>, 2-byte signed relative offset</summary>
	OpCodeOperandKind_xbegin_2 = 101,
	/// <summary><c>XBEGIN</c>, 4-byte signed relative offset</summary>
	OpCodeOperandKind_xbegin_4 = 102,
	/// <summary>2-byte branch offset (<c>JMPE</c> instruction)</summary>
	OpCodeOperandKind_brdisp_2 = 103,
	/// <summary>4-byte branch offset (<c>JMPE</c> instruction)</summary>
	OpCodeOperandKind_brdisp_4 = 104,
	/// <summary>Memory (modrm) and the sib byte must be present</summary>
	OpCodeOperandKind_sibmem = 105,
	/// <summary><c>TMM</c> register encoded in the <c>reg</c> field of the modrm byte</summary>
	OpCodeOperandKind_tmm_reg = 106,
	/// <summary><c>TMM</c> register encoded in the <c>mod + r/m</c> fields of the modrm byte</summary>
	OpCodeOperandKind_tmm_rm = 107,
	/// <summary><c>TMM</c> register encoded in the the <c>V&apos;vvvv</c> field (VEX/EVEX/XOP)</summary>
	OpCodeOperandKind_tmm_vvvv = 108,
};

unsigned char OpCodeOperandKinds_LegacyOpKinds[76] =
{

	(unsigned char)OpCodeOperandKind_None,
	(unsigned char)OpCodeOperandKind_farbr2_2,
	(unsigned char)OpCodeOperandKind_farbr4_2,
	(unsigned char)OpCodeOperandKind_mem_offs,
	(unsigned char)OpCodeOperandKind_mem,
	(unsigned char)OpCodeOperandKind_mem_mpx,
	(unsigned char)OpCodeOperandKind_mem_mib,
	(unsigned char)OpCodeOperandKind_r8_or_mem,
	(unsigned char)OpCodeOperandKind_r16_or_mem,
	(unsigned char)OpCodeOperandKind_r32_or_mem,
	(unsigned char)OpCodeOperandKind_r32_or_mem_mpx,
	(unsigned char)OpCodeOperandKind_r64_or_mem,
	(unsigned char)OpCodeOperandKind_r64_or_mem_mpx,
	(unsigned char)OpCodeOperandKind_mm_or_mem,
	(unsigned char)OpCodeOperandKind_xmm_or_mem,
	(unsigned char)OpCodeOperandKind_bnd_or_mem_mpx,
	(unsigned char)OpCodeOperandKind_r8_reg,
	(unsigned char)OpCodeOperandKind_r8_opcode,
	(unsigned char)OpCodeOperandKind_r16_reg,
	(unsigned char)OpCodeOperandKind_r16_reg_mem,
	(unsigned char)OpCodeOperandKind_r16_rm,
	(unsigned char)OpCodeOperandKind_r16_opcode,
	(unsigned char)OpCodeOperandKind_r32_reg,
	(unsigned char)OpCodeOperandKind_r32_reg_mem,
	(unsigned char)OpCodeOperandKind_r32_rm,
	(unsigned char)OpCodeOperandKind_r32_opcode,
	(unsigned char)OpCodeOperandKind_r64_reg,
	(unsigned char)OpCodeOperandKind_r64_reg_mem,
	(unsigned char)OpCodeOperandKind_r64_rm,
	(unsigned char)OpCodeOperandKind_r64_opcode,
	(unsigned char)OpCodeOperandKind_seg_reg,
	(unsigned char)OpCodeOperandKind_mm_reg,
	(unsigned char)OpCodeOperandKind_mm_rm,
	(unsigned char)OpCodeOperandKind_xmm_reg,
	(unsigned char)OpCodeOperandKind_xmm_rm,
	(unsigned char)OpCodeOperandKind_cr_reg,
	(unsigned char)OpCodeOperandKind_dr_reg,
	(unsigned char)OpCodeOperandKind_tr_reg,
	(unsigned char)OpCodeOperandKind_bnd_reg,
	(unsigned char)OpCodeOperandKind_es,
	(unsigned char)OpCodeOperandKind_cs,
	(unsigned char)OpCodeOperandKind_ss,
	(unsigned char)OpCodeOperandKind_ds,
	(unsigned char)OpCodeOperandKind_fs,
	(unsigned char)OpCodeOperandKind_gs,
	(unsigned char)OpCodeOperandKind_al,
	(unsigned char)OpCodeOperandKind_cl,
	(unsigned char)OpCodeOperandKind_ax,
	(unsigned char)OpCodeOperandKind_dx,
	(unsigned char)OpCodeOperandKind_eax,
	(unsigned char)OpCodeOperandKind_rax,
	(unsigned char)OpCodeOperandKind_st0,
	(unsigned char)OpCodeOperandKind_sti_opcode,
	(unsigned char)OpCodeOperandKind_imm8,
	(unsigned char)OpCodeOperandKind_imm8_const_1,
	(unsigned char)OpCodeOperandKind_imm8sex16,
	(unsigned char)OpCodeOperandKind_imm8sex32,
	(unsigned char)OpCodeOperandKind_imm8sex64,
	(unsigned char)OpCodeOperandKind_imm16,
	(unsigned char)OpCodeOperandKind_imm32,
	(unsigned char)OpCodeOperandKind_imm32sex64,
	(unsigned char)OpCodeOperandKind_imm64,
	(unsigned char)OpCodeOperandKind_seg_rSI,
	(unsigned char)OpCodeOperandKind_es_rDI,
	(unsigned char)OpCodeOperandKind_seg_rDI,
	(unsigned char)OpCodeOperandKind_seg_rBX_al,
	(unsigned char)OpCodeOperandKind_br16_1,
	(unsigned char)OpCodeOperandKind_br32_1,
	(unsigned char)OpCodeOperandKind_br64_1,
	(unsigned char)OpCodeOperandKind_br16_2,
	(unsigned char)OpCodeOperandKind_br32_4,
	(unsigned char)OpCodeOperandKind_br64_4,
	(unsigned char)OpCodeOperandKind_xbegin_2,
	(unsigned char)OpCodeOperandKind_xbegin_4,
	(unsigned char)OpCodeOperandKind_brdisp_2,
	(unsigned char)OpCodeOperandKind_brdisp_4,
};

unsigned char OpCodeOperandKinds_VexOpKinds[39] =
{
	(unsigned char)OpCodeOperandKind_None,
	(unsigned char)OpCodeOperandKind_mem,
	(unsigned char)OpCodeOperandKind_mem_vsib32x,
	(unsigned char)OpCodeOperandKind_mem_vsib64x,
	(unsigned char)OpCodeOperandKind_mem_vsib32y,
	(unsigned char)OpCodeOperandKind_mem_vsib64y,
	(unsigned char)OpCodeOperandKind_r32_or_mem,
	(unsigned char)OpCodeOperandKind_r64_or_mem,
	(unsigned char)OpCodeOperandKind_xmm_or_mem,
	(unsigned char)OpCodeOperandKind_ymm_or_mem,
	(unsigned char)OpCodeOperandKind_k_or_mem,
	(unsigned char)OpCodeOperandKind_r32_reg,
	(unsigned char)OpCodeOperandKind_r32_rm,
	(unsigned char)OpCodeOperandKind_r32_vvvv,
	(unsigned char)OpCodeOperandKind_r64_reg,
	(unsigned char)OpCodeOperandKind_r64_rm,
	(unsigned char)OpCodeOperandKind_r64_vvvv,
	(unsigned char)OpCodeOperandKind_k_reg,
	(unsigned char)OpCodeOperandKind_k_rm,
	(unsigned char)OpCodeOperandKind_k_vvvv,
	(unsigned char)OpCodeOperandKind_xmm_reg,
	(unsigned char)OpCodeOperandKind_xmm_rm,
	(unsigned char)OpCodeOperandKind_xmm_vvvv,
	(unsigned char)OpCodeOperandKind_xmm_is4,
	(unsigned char)OpCodeOperandKind_xmm_is5,
	(unsigned char)OpCodeOperandKind_ymm_reg,
	(unsigned char)OpCodeOperandKind_ymm_rm,
	(unsigned char)OpCodeOperandKind_ymm_vvvv,
	(unsigned char)OpCodeOperandKind_ymm_is4,
	(unsigned char)OpCodeOperandKind_ymm_is5,
	(unsigned char)OpCodeOperandKind_imm4_m2z,
	(unsigned char)OpCodeOperandKind_imm8,
	(unsigned char)OpCodeOperandKind_seg_rDI,
	(unsigned char)OpCodeOperandKind_br64_1,
	(unsigned char)OpCodeOperandKind_br64_4,
	(unsigned char)OpCodeOperandKind_sibmem,
	(unsigned char)OpCodeOperandKind_tmm_reg,
	(unsigned char)OpCodeOperandKind_tmm_rm,
	(unsigned char)OpCodeOperandKind_tmm_vvvv,
};

unsigned char OpCodeOperandKinds_XopOpKinds[19] =
{
	(unsigned char)OpCodeOperandKind_None,
	(unsigned char)OpCodeOperandKind_r32_or_mem,
	(unsigned char)OpCodeOperandKind_r64_or_mem,
	(unsigned char)OpCodeOperandKind_xmm_or_mem,
	(unsigned char)OpCodeOperandKind_ymm_or_mem,
	(unsigned char)OpCodeOperandKind_r32_reg,
	(unsigned char)OpCodeOperandKind_r32_rm,
	(unsigned char)OpCodeOperandKind_r32_vvvv,
	(unsigned char)OpCodeOperandKind_r64_reg,
	(unsigned char)OpCodeOperandKind_r64_rm,
	(unsigned char)OpCodeOperandKind_r64_vvvv,
	(unsigned char)OpCodeOperandKind_xmm_reg,
	(unsigned char)OpCodeOperandKind_xmm_vvvv,
	(unsigned char)OpCodeOperandKind_xmm_is4,
	(unsigned char)OpCodeOperandKind_ymm_reg,
	(unsigned char)OpCodeOperandKind_ymm_vvvv,
	(unsigned char)OpCodeOperandKind_ymm_is4,
	(unsigned char)OpCodeOperandKind_imm8,
	(unsigned char)OpCodeOperandKind_imm32,
};

unsigned char OpCodeOperandKinds_EvexOpKinds[32] =
{
	(unsigned char)OpCodeOperandKind_None,
	(unsigned char)OpCodeOperandKind_mem,
	(unsigned char)OpCodeOperandKind_mem_vsib32x,
	(unsigned char)OpCodeOperandKind_mem_vsib64x,
	(unsigned char)OpCodeOperandKind_mem_vsib32y,
	(unsigned char)OpCodeOperandKind_mem_vsib64y,
	(unsigned char)OpCodeOperandKind_mem_vsib32z,
	(unsigned char)OpCodeOperandKind_mem_vsib64z,
	(unsigned char)OpCodeOperandKind_r32_or_mem,
	(unsigned char)OpCodeOperandKind_r64_or_mem,
	(unsigned char)OpCodeOperandKind_xmm_or_mem,
	(unsigned char)OpCodeOperandKind_ymm_or_mem,
	(unsigned char)OpCodeOperandKind_zmm_or_mem,
	(unsigned char)OpCodeOperandKind_r32_reg,
	(unsigned char)OpCodeOperandKind_r32_rm,
	(unsigned char)OpCodeOperandKind_r64_reg,
	(unsigned char)OpCodeOperandKind_r64_rm,
	(unsigned char)OpCodeOperandKind_k_reg,
	(unsigned char)OpCodeOperandKind_kp1_reg,
	(unsigned char)OpCodeOperandKind_k_rm,
	(unsigned char)OpCodeOperandKind_xmm_reg,
	(unsigned char)OpCodeOperandKind_xmm_rm,
	(unsigned char)OpCodeOperandKind_xmm_vvvv,
	(unsigned char)OpCodeOperandKind_xmmp3_vvvv,
	(unsigned char)OpCodeOperandKind_ymm_reg,
	(unsigned char)OpCodeOperandKind_ymm_rm,
	(unsigned char)OpCodeOperandKind_ymm_vvvv,
	(unsigned char)OpCodeOperandKind_zmm_reg,
	(unsigned char)OpCodeOperandKind_zmm_rm,
	(unsigned char)OpCodeOperandKind_zmm_vvvv,
	(unsigned char)OpCodeOperandKind_zmmp3_vvvv,
	(unsigned char)OpCodeOperandKind_imm8,
};

unsigned char OpCodeOperandKinds_MvexOpKinds[9] =
{
	(unsigned char)OpCodeOperandKind_None,
	(unsigned char)OpCodeOperandKind_mem,
	(unsigned char)OpCodeOperandKind_mem_vsib32z,
	(unsigned char)OpCodeOperandKind_zmm_or_mem,
	(unsigned char)OpCodeOperandKind_k_reg,
	(unsigned char)OpCodeOperandKind_k_vvvv,
	(unsigned char)OpCodeOperandKind_zmm_reg,
	(unsigned char)OpCodeOperandKind_zmm_vvvv,
	(unsigned char)OpCodeOperandKind_imm8,
};

enum DisplSize 
{
	DisplSize_None,
	DisplSize_Size1,
	DisplSize_Size2,
	DisplSize_Size4,
	DisplSize_Size8,
	DisplSize_RipRelSize4_Target32,
	DisplSize_RipRelSize4_Target64,
};

enum ImmSize 
{
	ImmSize_None,
	ImmSize_Size1,
	ImmSize_Size2,
	ImmSize_Size4,
	ImmSize_Size8,
	/// <summary><c>ENTER xxxx,yy</c></summary>
	ImmSize_Size2_1,
	/// <summary><c>EXTRQ/INSERTQ xx,yy</c></summary>
	ImmSize_Size1_1,
	/// <summary><c>CALL16 FAR x:y</c></summary>
	ImmSize_Size2_2,
	/// <summary><c>CALL32 FAR x:y</c></summary>
	ImmSize_Size4_2,
	ImmSize_RipRelSize1_Target16,
	ImmSize_RipRelSize1_Target32,
	ImmSize_RipRelSize1_Target64,
	ImmSize_RipRelSize2_Target16,
	ImmSize_RipRelSize2_Target32,
	ImmSize_RipRelSize2_Target64,
	ImmSize_RipRelSize4_Target32,
	ImmSize_RipRelSize4_Target64,
	ImmSize_SizeIbReg,
	ImmSize_Size1OpCode,
};

enum EncoderFlags //: uint 
{
	EncoderFlags_None = 0x00000000,
	EncoderFlags_B = 0x00000001,
	EncoderFlags_X = 0x00000002,
	EncoderFlags_R = 0x00000004,
	EncoderFlags_W = 0x00000008,
	EncoderFlags_ModRM = 0x00000010,
	EncoderFlags_Sib = 0x00000020,
	EncoderFlags_REX = 0x00000040,
	EncoderFlags_P66 = 0x00000080,
	EncoderFlags_P67 = 0x00000100,
	/// <summary><c>EVEX.R&apos;</c></summary>
	EncoderFlags_R2 = 0x00000200,
	EncoderFlags_Broadcast = 0x00000400,
	EncoderFlags_HighLegacy8BitRegs = 0x00000800,
	EncoderFlags_Displ = 0x00001000,
	EncoderFlags_PF0 = 0x00002000,
	EncoderFlags_RegIsMemory = 0x00004000,
	EncoderFlags_MustUseSib = 0x00008000,
	EncoderFlags_VvvvvShift = 0x0000001B,
	EncoderFlags_VvvvvMask = 0x0000001F,
};

enum LegacyOpCodeTable 
{
	LegacyOpCodeTable_MAP0,
	LegacyOpCodeTable_MAP0F,
	LegacyOpCodeTable_MAP0F38,
	LegacyOpCodeTable_MAP0F3A,
};

enum VexOpCodeTable 
{
	VexOpCodeTable_MAP0,
	VexOpCodeTable_MAP0F,
	VexOpCodeTable_MAP0F38,
	VexOpCodeTable_MAP0F3A,
};
enum XopOpCodeTable 
{
	XopOpCodeTable_MAP8,
	XopOpCodeTable_MAP9,
	XopOpCodeTable_MAP10,
};

enum EvexOpCodeTable 
{
	EvexOpCodeTable_MAP0F = 1,
	EvexOpCodeTable_MAP0F38,
	EvexOpCodeTable_MAP0F3A,
	EvexOpCodeTable_MAP5 = 5,
	EvexOpCodeTable_MAP6,
};

enum MvexOpCodeTable 
{
	MvexOpCodeTable_MAP0F = 1,
	MvexOpCodeTable_MAP0F38,
	MvexOpCodeTable_MAP0F3A,
};

enum OpCodeTableKind 
{
	/// <summary>Legacy/<c>MAP0</c> table</summary>
	OpCodeTableKind_Normal = 0,
	/// <summary><c>0F</c>/<c>MAP1</c> table (legacy, VEX, EVEX, MVEX)</summary>
	OpCodeTableKind_T0F = 1,
	/// <summary><c>0F38</c>/<c>MAP2</c> table (legacy, VEX, EVEX, MVEX)</summary>
	OpCodeTableKind_T0F38 = 2,
	/// <summary><c>0F3A</c>/<c>MAP3</c> table (legacy, VEX, EVEX, MVEX)</summary>
	OpCodeTableKind_T0F3A = 3,
	/// <summary><c>MAP5</c> table (EVEX)</summary>
	OpCodeTableKind_MAP5 = 4,
	/// <summary><c>MAP6</c> table (EVEX)</summary>
	OpCodeTableKind_MAP6 = 5,
	/// <summary><c>MAP8</c> table (XOP)</summary>
	OpCodeTableKind_MAP8 = 6,
	/// <summary><c>MAP9</c> table (XOP)</summary>
	OpCodeTableKind_MAP9 = 7,
	/// <summary><c>MAP10</c> table (XOP)</summary>
	OpCodeTableKind_MAP10 = 8,
};

struct OpCodeInfo 
{
	 //char* toOpCodeStringValue;
	 //char* toInstructionStringValue;
	 enum EncFlags2 encFlags2;
	 enum EncFlags3 encFlags3;
	 enum OpCodeInfoFlags1 opcFlags1;
	 enum OpCodeInfoFlags2 opcFlags2;
	 unsigned short code;
	 unsigned char encoding;
	 unsigned char operandSize;
	 unsigned char addressSize;
	 unsigned char l;
	 unsigned char tupleType;
	 unsigned char table;
	 unsigned char mandatoryPrefix;
	 signed char groupIndex;
	 signed char rmGroupIndex;
	 unsigned char op0Kind;
	 unsigned char op1Kind;
	 unsigned char op2Kind;
	 unsigned char op3Kind;
	 unsigned char op4Kind;
	 enum Flags flags;
};

void OpCodeInfo_init(struct OpCodeInfo** opcodeInfo, enum Code code, enum EncFlags1 encFlags1, enum EncFlags2 encFlags2, enum EncFlags3 encFlags3, enum OpCodeInfoFlags1 opcFlags1, enum OpCodeInfoFlags2 opcFlags2, StringBuilder* sb)
{
	//Debug.Assert((uint)code < (uint)IcedConstants.CodeEnumCount);
	//Debug.Assert((uint)code <= ushort.MaxValue);
	(*opcodeInfo)->code = (unsigned short)code;
	(*opcodeInfo)->encFlags2 = encFlags2;
	(*opcodeInfo)->encFlags3 = encFlags3;
	(*opcodeInfo)->opcFlags1 = opcFlags1;
	(*opcodeInfo)->opcFlags2 = opcFlags2;

	if ((encFlags1 & EFLAGS_IgnoresRoundingControl) != 0)
	{
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_IgnoresRoundingControl);
	}
	if ((encFlags1 & EFLAGS_AmdLockRegBit) != 0)
	{
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_AmdLockRegBit);
	}
	switch (opcFlags1 & (OCIF1_Cpl0Only | OCIF1_Cpl3Only))
	{
	case OCIF1_Cpl0Only: 
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_CPL0);
			break;
	case OCIF1_Cpl3Only:
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_CPL3);
		break;
	default:
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_CPL0 | FLAGS_CPL1 | FLAGS_CPL2 | FLAGS_CPL3);
		break;
	}

	(*opcodeInfo)->encoding = (unsigned char)(((unsigned int)encFlags3 >> (int)EFLAGS3_EncodingShift) & (unsigned int)EFLAGS3_EncodingMask);
	
	
	switch ((enum MandatoryPrefixByte)(((unsigned int)encFlags2 >> (int)EFLAGS2_MandatoryPrefixShift) & (unsigned int)EFLAGS2_MandatoryPrefixMask))
	{
	case MPB_None:
		(*opcodeInfo)->mandatoryPrefix = (unsigned char)((encFlags2 & EFLAGS2_HasMandatoryPrefix) != 0 ? MP_PNP : MP_None);
		break;
	case MPB_P66:
		(*opcodeInfo)->mandatoryPrefix = (unsigned char)MP_P66;
		break;
	case MPB_PF3:
		(*opcodeInfo)->mandatoryPrefix = (unsigned char)MP_PF3;
		break;
	case MPB_PF2:
		(*opcodeInfo)->mandatoryPrefix = (unsigned char)MP_PF2;
		break;
	default:
		// throw new InvalidOperationException()
		break;
	}

	switch ((enum CodeSize)(((unsigned int)encFlags3 >> (int)EFLAGS3_OperandSizeShift) & (unsigned int)EFLAGS3_OperandSizeMask))
	{
	case CSIZE_Unknown:
		(*opcodeInfo)->operandSize = 0;
		break;
	case CSIZE_Code16:
		(*opcodeInfo)->operandSize = 16;
		break;
	case CSIZE_Code32:
		(*opcodeInfo)->operandSize = 32;
		break;
	case CSIZE_Code64:
		(*opcodeInfo)->operandSize = 64;
		break;
	default:
		// throw new InvalidOperationException()
		break;
	}

	switch ((CodeSize)(((unsigned int)encFlags3 >> (int)EFLAGS3_AddressSizeShift) & (unsigned int)EFLAGS3_AddressSizeMask))
	{
	case CSIZE_Unknown:
		(*opcodeInfo)->addressSize = 0;
		break;
	case CSIZE_Code16:
		(*opcodeInfo)->addressSize = 16;
		break;
	case CSIZE_Code32:
		(*opcodeInfo)->addressSize = 32;
		break;
	case CSIZE_Code64:
		(*opcodeInfo)->addressSize = 64;
		break;
	default:
		// throw new InvalidOperationException()
		break;
	}
	(*opcodeInfo)->groupIndex = (signed char)((encFlags2 & EFLAGS2_HasGroupIndex) == 0 ? -1 : (int)(((unsigned int)encFlags2 >> (int)EFLAGS2_GroupIndexShift) & 7));
	(*opcodeInfo)->rmGroupIndex = (signed char)((encFlags3 & EFLAGS3_HasRmGroupIndex) == 0 ? -1 : (int)(((unsigned int)encFlags2 >> (int)EFLAGS2_GroupIndexShift) & 7));
	(*opcodeInfo)->tupleType = (unsigned char)(((unsigned int)encFlags3 >> (int)EFLAGS3_TupleTypeShift) & (unsigned int)EFLAGS3_TupleTypeMask);

	enum LKind lkind;
	switch ((enum LBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_LBitShift) & (int)EFLAGS2_LBitMask)) 
	{
	case LBit_LZ:
		lkind = LK_LZ;
		(*opcodeInfo)->l = 0;
		break;
	case LBit_L0:
		lkind = LK_L0;
		(*opcodeInfo)->l = 0;
		break;
	case LBit_L1:
		lkind = LK_L0;
		(*opcodeInfo)->l = 1;
		break;
	case LBit_L128:
		lkind = LK_L128;
		(*opcodeInfo)->l = 0;
		break;
	case LBit_L256:
		lkind = LK_L128;
		(*opcodeInfo)->l = 1;
		break;
	case LBit_L512:
		lkind = LK_L128;
		(*opcodeInfo)->l = 2;
		break;
	case LBit_LIG:
		lkind = LK_None;
		(*opcodeInfo)->l = 0;
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_LIG);
		break;
	default:
		//throw new InvalidOperationException();
		break;
	}

//#if !NO_VEX || !NO_EVEX || !NO_XOP
	switch ((enum WBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_WBitShift) & (unsigned int)EFLAGS2_WBitMask))
	{
	case WBit_W0:
		break;
	case WBit_W1:
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_W);
		break;
	case WBit_WIG:
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_WIG);
		break;
	case WBit_WIG32:
		(*opcodeInfo)->flags = (enum Flags)((*opcodeInfo)->flags | FLAGS_WIG32);
		break;
	default:
		//throw new InvalidOperationException();
		break;
	}
//#endif

	char* toOpCodeStringValue = NULL;
	char* toInstructionStringValue = NULL;
	//unsigned char opKinds[76];

	switch ((enum EncodingKind)(*opcodeInfo)->encoding) 
	{
	case EncodingKind_Legacy:

		auto opKinds = OpCodeOperandKinds_LegacyOpKinds;
		(*opcodeInfo)->op0Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op0Shift) & (unsigned int)EFLAGS_Legacy_OpMask)];
		(*opcodeInfo)->op1Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op1Shift) & (unsigned int)EFLAGS_Legacy_OpMask)];
		(*opcodeInfo)->op2Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op2Shift) & (unsigned int)EFLAGS_Legacy_OpMask)];
		(*opcodeInfo)->op3Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op3Shift) & (unsigned int)EFLAGS_Legacy_OpMask)];

		switch ((enum LegacyOpCodeTable)(((unsigned int)encFlags2 >> (int)(EFLAGS2_TableShift)) & (unsigned int)EFLAGS2_TableMask))
		{
		case LegacyOpCodeTable_MAP0:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_Normal;
			break;
		case LegacyOpCodeTable_MAP0F:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F;
			break;
		case LegacyOpCodeTable_MAP0F38:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F38;
			break;
		case LegacyOpCodeTable_MAP0F3A:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F3A;
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
		break;
	case EncodingKind_VEX:
//#if !NO_VEX
		auto opKinds = OpCodeOperandKinds_VexOpKinds;
		(*opcodeInfo)->op0Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op0Shift) & (unsigned int)EFLAGS_VEX_OpMask)];
		(*opcodeInfo)->op1Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op1Shift) & (unsigned int)EFLAGS_VEX_OpMask)];
		(*opcodeInfo)->op2Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op2Shift) & (unsigned int)EFLAGS_VEX_OpMask)];
		(*opcodeInfo)->op3Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op3Shift) & (unsigned int)EFLAGS_VEX_OpMask)];
		(*opcodeInfo)->op4Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op4Shift) & (unsigned int)EFLAGS_VEX_OpMask)];

		switch ((enum VexOpCodeTable)(((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask))
		{
		case VexOpCodeTable_MAP0:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_Normal;
			break;
		case VexOpCodeTable_MAP0F:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F;
			break;
		case VexOpCodeTable_MAP0F38:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F38;
			break;
		case VexOpCodeTable_MAP0F3A:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F3A;
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
		break;
//#else
//		op4Kind = (byte)OpCodeOperandKind.None;
//		toOpCodeStringValue = string.Empty;
//		toInstructionStringValue = string.Empty;
//		break;
//#endif

	case EncodingKind_EVEX:
//#if !NO_EVEX
		auto opKinds = OpCodeOperandKinds_EvexOpKinds;
		(*opcodeInfo)->op0Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op0Shift) & (unsigned int)EFLAGS_EVEX_OpMask)];
		(*opcodeInfo)->op1Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op1Shift) & (unsigned int)EFLAGS_EVEX_OpMask)];
		(*opcodeInfo)->op2Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op2Shift) & (unsigned int)EFLAGS_EVEX_OpMask)];
		(*opcodeInfo)->op3Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op3Shift) & (unsigned int)EFLAGS_EVEX_OpMask)];

		switch ((enum EvexOpCodeTable)(((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask))
		{
		case EvexOpCodeTable_MAP0F:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F;
			break;
		case EvexOpCodeTable_MAP0F38:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F38;
			break;
		case EvexOpCodeTable_MAP0F3A:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F3A;
			break;
		case EvexOpCodeTable_MAP5:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_MAP5;
			break;
		case EvexOpCodeTable_MAP6:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_MAP6;
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
		break;
//#else
//		toOpCodeStringValue = string.Empty;
//		toInstructionStringValue = string.Empty;
//		break;
//#endif

	case EncodingKind_XOP:
//#if !NO_XOP
		auto opKinds = OpCodeOperandKinds_XopOpKinds;
		(*opcodeInfo)->op0Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op0Shift) & (unsigned int)EFLAGS_XOP_OpMask)];
		(*opcodeInfo)->op1Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op1Shift) & (unsigned int)EFLAGS_XOP_OpMask)];
		(*opcodeInfo)->op2Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op2Shift) & (unsigned int)EFLAGS_XOP_OpMask)];
		(*opcodeInfo)->op3Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op3Shift) & (unsigned int)EFLAGS_XOP_OpMask)];

		switch ((enum XopOpCodeTable)(((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask))
		{
		case XopOpCodeTable_MAP8:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_MAP8;
			break;
		case XopOpCodeTable_MAP9:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_MAP9;
			break;
		case XopOpCodeTable_MAP10:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_MAP10;
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
		break;
//#else
//		toOpCodeStringValue = string.Empty;
//		toInstructionStringValue = string.Empty;
//		break;
//#endif

	case EncodingKind_D3NOW:
//#if !NO_D3NOW
		(*opcodeInfo)->op0Kind = (unsigned char)OpCodeOperandKind_mm_reg;
		(*opcodeInfo)->op1Kind = (unsigned char)OpCodeOperandKind_mm_or_mem;
		(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F;
		break;
//#else
//		toOpCodeStringValue = string.Empty;
//		toInstructionStringValue = string.Empty;
//		break;
//#endif

	case EncodingKind_MVEX:
//#if MVEX
		auto opKinds = OpCodeOperandKinds_MvexOpKinds;
		(*opcodeInfo)->op0Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op0Shift) & (unsigned int)EFLAGS_MVEX_OpMask)];
		(*opcodeInfo)->op1Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op1Shift) & (unsigned int)EFLAGS_MVEX_OpMask)];
		(*opcodeInfo)->op2Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op2Shift) & (unsigned int)EFLAGS_MVEX_OpMask)];
		(*opcodeInfo)->op3Kind = opKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op3Shift) & (unsigned int)EFLAGS_MVEX_OpMask)];

		switch ((enum MvexOpCodeTable)(((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask))
		{
		case MvexOpCodeTable_MAP0F:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F;
			break;
		case MvexOpCodeTable_MAP0F38:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F38;
			break;
		case MvexOpCodeTable_MAP0F3A:
			(*opcodeInfo)->table = (unsigned char)OpCodeTableKind_T0F3A;
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
		break;
//#else
		//toOpCodeStringValue = string.Empty;
		//toInstructionStringValue = string.Empty;
		break;
//#endif

	default:
		//throw new InvalidOperationException();
		break;
	}

	//this.toOpCodeStringValue = toOpCodeStringValue ? ? new OpCodeFormatter(this, sb, lkind, (opcFlags1 & OpCodeInfoFlags1.ModRegRmString) != 0).Format();
	//var fmtOption = (InstrStrFmtOption)(((uint)opcFlags2 >> (int)OpCodeInfoFlags2.InstrStrFmtOptionShift) & (uint)OpCodeInfoFlags2.InstrStrFmtOptionMask);
	//this.toInstructionStringValue = toInstructionStringValue ? ? new InstructionFormatter(this, fmtOption, sb).Format();
}

/// <summary>
/// Gets the code
/// </summary>
enum Code OpCodeInfo_GetCode(struct OpCodeInfo* o)
{
	return (enum Code)o->code;
}

/// <summary>
/// Gets the mnemonic
/// </summary>
enum Mnemonic OpCodeInfo_GetMnemonic(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;
	enum Mnemonic mnemonic = to_mnemonic(code);

	return mnemonic;
}

/// <summary>
/// Gets the encoding
/// </summary>
enum EncodingKind GetEncoding(struct OpCodeInfo* o)
{
	return (enum EncodingKind)o->encoding;
}

/// <summary>
/// <see langword="true"/> if it's an instruction, <see langword="false"/> if it's eg. <see cref="Code.INVALID"/>, <c>db</c>, <c>dw</c>, <c>dd</c>, <c>dq</c>, <c>zero_bytes</c>
/// </summary>
bool IsInstruction(struct OpCodeInfo* o)
{
	return !(o->code <= (unsigned short)DeclareQword || o->code == (unsigned short)Zero_bytes);
}

/// <summary>
/// <see langword="true"/> if it's an instruction available in 16-bit mode
/// </summary>
bool GetMode16(struct OpCodeInfo* o)
{
	return  (o->encFlags3 & EFLAGS3_Bit16or32) != 0;
}

/// <summary>
/// <see langword="true"/> if it's an instruction available in 32-bit mode
/// </summary>
bool GetMode32(struct OpCodeInfo* o)
{
	return  (o->encFlags3 & EFLAGS3_Bit16or32) != 0;
}

/// <summary>
/// <see langword="true"/> if it's an instruction available in 64-bit mode
/// </summary>
bool GetMode64(struct OpCodeInfo* o)
{
	return  (o->encFlags3 & EFLAGS3_Bit64) != 0;
}

/// <summary>
/// <see langword="true"/> if an <c>FWAIT</c> (<c>9B</c>) instruction is added before the instruction
/// </summary>
bool GetFwait(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Fwait) != 0;
}

/// <summary>
/// (Legacy encoding) Gets the required operand size (16,32,64) or 0
/// </summary>
int GetOperandSize(struct OpCodeInfo* o)
{
	return o->operandSize;
}

/// <summary>
/// (Legacy encoding) Gets the required address size (16,32,64) or 0
/// </summary>
int GetAddressSize(struct OpCodeInfo* o)
{
	return o->addressSize;
}

/// <summary>
/// (VEX/XOP/EVEX) <c>L</c> / <c>L'L</c> value or default value if <see cref="IsLIG"/> is <see langword="true"/>
/// </summary>
unsigned int GetL(struct OpCodeInfo* o)
{
	return o->l;
}

/// <summary>
/// (VEX/XOP/EVEX/MVEX) <c>W</c> value or default value if <see cref="IsWIG"/> or <see cref="IsWIG32"/> is <see langword="true"/>
/// </summary>
unsigned int GetW(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_W) != 0 ? 1U : 0;
}

/// <summary>
/// (VEX/XOP/EVEX) <see langword="true"/> if the <c>L</c> / <c>L'L</c> fields are ignored.
/// <br/>
/// EVEX: if reg-only ops and <c>{er}</c> (<c>EVEX.b</c> is set), <c>L'L</c> is the rounding control and not ignored.
/// </summary>
bool IsLIG(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_LIG) != 0;
}

/// <summary>
/// (VEX/XOP/EVEX/MVEX) <see langword="true"/> if the <c>W</c> field is ignored in 16/32/64-bit modes
/// </summary>
bool IsWIG(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_WIG) != 0;
}

/// <summary>
/// (VEX/XOP/EVEX/MVEX) <see langword="true"/> if the <c>W</c> field is ignored in 16/32-bit modes (but not 64-bit mode)
/// </summary>
bool IsWIG32(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_WIG32) != 0;
}

/// <summary>
/// (EVEX/MVEX) Gets the tuple type
/// </summary>
enum TupleType GetTupleType(struct OpCodeInfo* o)
{
	return (enum TupleType)o->tupleType;
}

struct MvexInfo
{
	int index;
	enum MvexTupleTypeLutKind TupleTypeLutKind;
	enum MvexEHBit EHBit;
	enum MvexConvFn ConvFn;
	unsigned int InvalidConvFns;
	unsigned int InvalidSwizzleFns;
	bool IsNDD;
	bool IsNDS;
	bool CanUseEvictionHint;
	bool CanUseImmRoundingControl;
	bool CanUseRoundingControl;
	bool CanUseSuppressAllExceptions;
	bool IgnoresOpMaskRegister;
	bool RequireOpMaskRegister;
	bool NoSaeRc;
	bool IsConvFn32;
	bool IgnoresEvictionHint;
};

struct MvexInfo* MvexInfo_new(enum Code code)
{
	struct MvexInfo* o = (struct MvexInfo*)malloc(sizeof(struct MvexInfo));

	o->index = (int)code - (int)MvexStart;
	//Debug.Assert((uint)index < MvexLength); // IcedConstants.MvexLength

	MvexInfo_init(&o);

	return o;
}

void MvexInfo_init(struct MvexInfo** o)
{
	(*o)->TupleTypeLutKind = (enum MvexTupleTypeLutKind)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_TupleTypeLutKindIndex];
	(*o)->EHBit = (enum MvexEHBit)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_EHBitIndex];
	(*o)->ConvFn = (enum MvexConvFn)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_ConvFnIndex];
	(*o)->InvalidConvFns = MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_InvalidConvFnsIndex];
	(*o)->InvalidSwizzleFns = MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_InvalidSwizzleFnsIndex];
	(*o)->IsNDD = ((enum MvexInfoFlags1)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_NDD) != 0;
	(*o)->IsNDS = ((enum MvexInfoFlags1)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_NDS) != 0;
	(*o)->CanUseEvictionHint = ((enum MvexInfoFlags1)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_EvictionHint) != 0;
	(*o)->CanUseImmRoundingControl = ((enum MvexInfoFlags1)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_ImmRoundingControl) != 0;
	(*o)->CanUseRoundingControl = ((enum MvexInfoFlags1)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_RoundingControl) != 0;
	(*o)->CanUseSuppressAllExceptions = ((enum MvexInfoFlags1)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_SuppressAllExceptions) != 0;
	(*o)->IgnoresOpMaskRegister = ((enum MvexInfoFlags1)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_IgnoresOpMaskRegister) != 0;
	(*o)->RequireOpMaskRegister = ((enum MvexInfoFlags1)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags1Index] & MIF1_RequireOpMaskRegister) != 0;
	(*o)->NoSaeRc = ((enum MvexInfoFlags2)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags2Index] & MIF2_NoSaeRoundingControl) != 0;
	(*o)->IsConvFn32 = ((enum MvexInfoFlags2)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags2Index] & MIF2_ConvFn32) != 0;
	(*o)->IgnoresEvictionHint = ((enum MvexInfoFlags2)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_Flags2Index] & MIF2_IgnoresEvictionHint) != 0;

}

enum TupleType  MvexInfo_GetTupleType(struct MvexInfo* o, int sss)
{
	return (enum TupleType)MvexTupleTypeLut_Data[(int)o->TupleTypeLutKind * 8 + sss];
}

/// <summary>
/// (MVEX) Gets the <c>EH</c> bit that's required to encode this instruction
/// </summary>
enum MvexEHBit GetMvexEHBit(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;

	if (GetEncoding(o) == EncodingKind_MVEX)
	{
		struct MvexInfo*  mvex_info = MvexInfo_new(code);

		return mvex_info->EHBit;
	}
	return  (enum MvexEHBit)MEHB_None;
}

/// <summary>
/// (MVEX) <see langword="true"/> if the instruction supports eviction hint (if it has a memory operand)
/// </summary>
bool GetMvexCanUseEvictionHint(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;
	struct MvexInfo* mvex_info = MvexInfo_new(code);

	return GetEncoding(o) == EncodingKind_MVEX && mvex_info->CanUseEvictionHint;
}

/// <summary>
/// (MVEX) <see langword="true"/> if the instruction's rounding control bits are stored in <c>imm8[1:0]</c>
/// </summary>
bool GetMvexCanUseImmRoundingControl(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;
	struct MvexInfo* mvex_info = MvexInfo_new(code);

	return GetEncoding(o) == EncodingKind_MVEX && mvex_info->CanUseImmRoundingControl;
}

/// <summary>
/// (MVEX) <see langword="true"/> if the instruction ignores op mask registers (eg. <c>{k1}</c>)
/// </summary>
bool GetMvexIgnoresOpMaskRegister(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;
	struct MvexInfo* mvex_info = MvexInfo_new(code);

	return GetEncoding(o) == EncodingKind_MVEX && mvex_info->IgnoresOpMaskRegister;
}

/// <summary>
/// (MVEX) <see langword="true"/> if the instruction must have <c>MVEX.SSS=000</c> if <c>MVEX.EH=1</c>
/// </summary>
bool GetMvexNoSaeRc(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;
	struct MvexInfo* mvex_info = MvexInfo_new(code);

	return GetEncoding(o) == EncodingKind_MVEX && mvex_info->NoSaeRc;
}

/// <summary>
/// (MVEX) Gets the tuple type / conv lut kind
/// </summary>
enum MvexTupleTypeLutKind GetMvexTupleTypeLutKind(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;

	if (GetEncoding(o) == EncodingKind_MVEX)
	{
		struct MvexInfo* mvex_info = MvexInfo_new(code);

		return mvex_info->TupleTypeLutKind;
	}
	return  (enum MvexTupleTypeLutKind)MTTLK_Int32;
}

/// <summary>
/// (MVEX) Gets the conversion function, eg. <c>Sf32</c>
/// </summary>
enum MvexConvFn GetMvexConversionFunc(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;

	if (GetEncoding(o) == EncodingKind_MVEX)
	{
		struct MvexInfo* mvex_info = MvexInfo_new(code);

		return mvex_info->ConvFn;
	}
	return  (enum MvexConvFn)MCF_None;
}

/// <summary>
/// (MVEX) Gets flags indicating which conversion functions are valid (bit 0 == func 0)
/// </summary>
unsigned char GetMvexValidConversionFuncsMask(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;

	if (GetEncoding(o) == EncodingKind_MVEX)
	{
		struct MvexInfo* mvex_info = MvexInfo_new(code);

		return (unsigned char)(~mvex_info->InvalidConvFns);
	}
	return 0;
}

/// <summary>
/// (MVEX) Gets flags indicating which swizzle functions are valid (bit 0 == func 0)
/// </summary>
unsigned char GetMvexValidSwizzleFuncsMask(struct OpCodeInfo* o)
{
	enum Code code = (enum Code)o->code;

	if (GetEncoding(o) == EncodingKind_MVEX)
	{
		struct MvexInfo* mvex_info = MvexInfo_new(code);

		return (unsigned char)(~mvex_info->InvalidSwizzleFns);
	}
	return 0;
}

/// <summary>
/// If it has a memory operand, gets the <see cref="MemorySize"/> (non-broadcast memory type)
/// </summary>
enum MemorySize GetMemorySize(struct OpCodeInfo* o)
{
	return (enum MemorySize)InstructionMemorySizes_SizesNormal[(int)o->code];
}

/// <summary>
/// If it has a memory operand, gets the <see cref="MemorySize"/> (broadcast memory type)
/// </summary>
enum MemorySize GetBroadcastMemorySize(struct OpCodeInfo* o)
{
	return (enum MemorySize)InstructionMemorySizes_SizesBcst[(int)o->code];
}

/// <summary>
/// (EVEX) <see langword="true"/> if the instruction supports broadcasting (<c>EVEX.b</c> bit) (if it has a memory operand)
/// </summary>
bool GetCanBroadcast(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Broadcast) != 0;
}

/// <summary>
/// (EVEX/MVEX) <see langword="true"/> if the instruction supports rounding control
/// </summary>
bool GetCanUseRoundingControl(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_RoundingControl) != 0;
}

/// <summary>
/// (EVEX/MVEX) <see langword="true"/> if the instruction supports suppress all exceptions
/// </summary>
bool GetCanSuppressAllExceptions(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_SuppressAllExceptions) != 0;
}

/// <summary>
/// (EVEX/MVEX) <see langword="true"/> if an opmask register can be used
/// </summary>
bool GetCanUseOpMaskRegister(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_OpMaskRegister) != 0;
}

/// <summary>
/// (EVEX/MVEX) <see langword="true"/> if a non-zero opmask register must be used
/// </summary>
bool GetRequireOpMaskRegister(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_RequireOpMaskRegister) != 0;
}

/// <summary>
/// (EVEX) <see langword="true"/> if the instruction supports zeroing masking (if one of the opmask registers <c>K1</c>-<c>K7</c> is used and destination operand is not a memory operand)
/// </summary>
bool GetCanUseZeroingMasking(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_ZeroingMasking) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>LOCK</c> (<c>F0</c>) prefix can be used
/// </summary>
bool GetCanUseLockPrefix(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Lock) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>XACQUIRE</c> (<c>F2</c>) prefix can be used
/// </summary>
bool GetCanUseXacquirePrefix(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Xacquire) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>XRELEASE</c> (<c>F3</c>) prefix can be used
/// </summary>
bool GetCanUseXreleasePrefix(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Xrelease) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>REP</c> / <c>REPE</c> (<c>F3</c>) prefixes can be used
/// </summary>
bool GetCanUseRepPrefix(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Rep) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>REPNE</c> (<c>F2</c>) prefix can be used
/// </summary>
bool GetCanUseRepnePrefix(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Repne) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>BND</c> (<c>F2</c>) prefix can be used
/// </summary>
bool GetCanUseBndPrefix(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Bnd) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>HINT-TAKEN</c> (<c>3E</c>) and <c>HINT-NOT-TAKEN</c> (<c>2E</c>) prefixes can be used
/// </summary>
bool GetCanUseHintTakenPrefix(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_HintTaken) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>NOTRACK</c> (<c>3E</c>) prefix can be used
/// </summary>
bool GetCanUseNotrackPrefix(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Notrack) != 0;
}

/// <summary>
/// <see langword="true"/> if rounding control is ignored (#UD is not generated)
/// </summary>
bool GetIgnoresRoundingControl(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_IgnoresRoundingControl) != 0;
}

/// <summary>
/// (AMD) <see langword="true"/> if the <c>LOCK</c> prefix can be used as an extra register bit (bit 3) to access registers 8-15 without a <c>REX</c> prefix (eg. in 32-bit mode)
/// </summary>
bool GetAmdLockRegBit(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_AmdLockRegBit) != 0;
}

/// <summary>
/// <see langword="true"/> if the default operand size is 64 in 64-bit mode. A <c>66</c> prefix can switch to 16-bit operand size.
/// </summary>
bool GetDefaultOpSize64(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_DefaultOpSize64) != 0;
}

/// <summary>
/// <see langword="true"/> if the operand size is always 64 in 64-bit mode. A <c>66</c> prefix is ignored.
/// </summary>
bool GetForceOpSize64(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_ForceOpSize64) != 0;
}

/// <summary>
/// <see langword="true"/> if the Intel decoder forces 64-bit operand size. A <c>66</c> prefix is ignored.
/// </summary>
bool GetIntelForceOpSize64(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_IntelForceOpSize64) != 0;
}

/// <summary>
/// <see langword="true"/> if it can only be executed when CPL=0
/// </summary>
bool GetMustBeCpl0(struct OpCodeInfo* o)
{
	return (o->flags & (FLAGS_CPL0 | FLAGS_CPL1 | FLAGS_CPL2 | FLAGS_CPL3)) == FLAGS_CPL0;
}

/// <summary>
/// <see langword="true"/> if it can be executed when CPL=0
/// </summary>
bool GetCpl0(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_CPL0) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be executed when CPL=1
/// </summary>
bool GetCpl1(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_CPL1) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be executed when CPL=2
/// </summary>
bool GetCpl2(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_CPL2) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be executed when CPL=3
/// </summary>
bool GetCpl3(struct OpCodeInfo* o)
{
	return (o->flags & FLAGS_CPL3) != 0;
}

/// <summary>
/// <see langword="true"/> if the instruction accesses the I/O address space (eg. <c>IN</c>, <c>OUT</c>, <c>INS</c>, <c>OUTS</c>)
/// </summary>
bool GetIsInputOutput(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_InputOutput) != 0;
}

/// <summary>
/// <see langword="true"/> if it's one of the many nop instructions (does not include FPU nop instructions, eg. <c>FNOP</c>)
/// </summary>
bool GetIsNop(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_Nop) != 0;
}

/// <summary>
/// <see langword="true"/> if it's one of the many reserved nop instructions (eg. <c>0F0D</c>, <c>0F18-0F1F</c>)
/// </summary>
bool GetIsReservedNop(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_ReservedNop) != 0;
}

/// <summary>
/// <see langword="true"/> if it's a serializing instruction (Intel CPUs)
/// </summary>
bool GetIsSerializingIntel(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_SerializingIntel) != 0;
}

/// <summary>
/// <see langword="true"/> if it's a serializing instruction (AMD CPUs)
/// </summary>
bool GetIsSerializingAmd(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_SerializingAmd) != 0;
}

/// <summary>
/// <see langword="true"/> if the instruction requires either CPL=0 or CPL&lt;=3 depending on some CPU option (eg. <c>CR4.TSD</c>, <c>CR4.PCE</c>, <c>CR4.UMIP</c>)
/// </summary>
bool GetMayRequireCpl0(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_MayRequireCpl0) != 0;
}

/// <summary>
/// <see langword="true"/> if it's a tracked <c>JMP</c>/<c>CALL</c> indirect instruction (CET)
/// </summary>
bool GetIsCetTracked(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_CetTracked) != 0;
}

/// <summary>
/// <see langword="true"/> if it's a non-temporal hint memory access (eg. <c>MOVNTDQ</c>)
/// </summary>
bool GetIsNonTemporal(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_NonTemporal) != 0;
}

/// <summary>
/// <see langword="true"/> if it's a no-wait FPU instruction, eg. <c>FNINIT</c>
/// </summary>
bool GetIsFpuNoWait(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_FpuNoWait) != 0;
}

/// <summary>
/// <see langword="true"/> if the mod bits are ignored and it's assumed <c>modrm[7:6] == 11b</c>
/// </summary>
bool GetIgnoresModBits(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_IgnoresModBits) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>66</c> prefix is not allowed (it will #UD)
/// </summary>
bool GetNo66(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_No66) != 0;
}

/// <summary>
/// <see langword="true"/> if the <c>F2</c>/<c>F3</c> prefixes aren't allowed
/// </summary>
bool GetNFx(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_NFx) != 0;
}

/// <summary>
/// <see langword="true"/> if the index reg's reg-num (vsib op) (if any) and register ops' reg-nums must be unique,
/// eg. <c>MNEMONIC XMM1,YMM1,[RAX+ZMM1*2]</c> is invalid. Registers = <c>XMM</c>/<c>YMM</c>/<c>ZMM</c>/<c>TMM</c>.
/// </summary>
bool GetRequiresUniqueRegNums(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_RequiresUniqueRegNums) != 0;
}

/// <summary>
/// <see langword="true"/> if the destination register's reg-num must not be present in any other operand, eg. <c>MNEMONIC XMM1,YMM1,[RAX+ZMM1*2]</c>
/// is invalid. Registers = <c>XMM</c>/<c>YMM</c>/<c>ZMM</c>/<c>TMM</c>.
/// </summary>
bool GetRequiresUniqueDestRegNum(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_RequiresUniqueDestRegNum) != 0;
}

/// <summary>
/// <see langword="true"/> if it's a privileged instruction (all CPL=0 instructions (except <c>VMCALL</c>) and IOPL instructions <c>IN</c>, <c>INS</c>, <c>OUT</c>, <c>OUTS</c>, <c>CLI</c>, <c>STI</c>)
/// </summary>
bool GetIsPrivileged(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_Privileged) != 0;
}

/// <summary>
/// <see langword="true"/> if it reads/writes too many registers
/// </summary>
bool GetIsSaveRestore(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_SaveRestore) != 0;
}

/// <summary>
/// <see langword="true"/> if it's an instruction that implicitly uses the stack register, eg. <c>CALL</c>, <c>POP</c>, etc
/// </summary>
bool GetIsStackInstruction(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_StackInstruction) != 0;
}

/// <summary>
/// <see langword="true"/> if the instruction doesn't read the segment register if it uses a memory operand
/// </summary>
bool GetIgnoresSegment(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_IgnoresSegment) != 0;
}

/// <summary>
/// <see langword="true"/> if the opmask register is read and written (instead of just read). This also implies that it can't be <c>K0</c>.
/// </summary>
bool GetIsOpMaskReadWrite(struct OpCodeInfo* o)
{
	return (o->opcFlags1 & OCIF1_OpMaskReadWrite) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be executed in real mode
/// </summary>
bool GetRealMode(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_RealMode) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be executed in protected mode
/// </summary>
bool GetProtectedMode(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_ProtectedMode) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be executed in virtual 8086 mode
/// </summary>
bool GetVirtual8086Mode(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_Virtual8086Mode) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be executed in compatibility mode
/// </summary>
bool GetCompatibilityMode(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_CompatibilityMode) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be executed in 64-bit mode
/// </summary>
bool GetLongMode(struct OpCodeInfo* o)
{
	return (o->encFlags3 & EFLAGS3_Bit64) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used outside SMM
/// </summary>
bool GetUseOutsideSmm(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseOutsideSmm) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used in SMM
/// </summary>
bool GetUseInSmm(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseInSmm) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used outside an enclave (SGX)
/// </summary>
bool GetUseOutsideEnclaveSgx(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseOutsideEnclaveSgx) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used inside an enclave (SGX1)
/// </summary>
bool GetUseInEnclaveSgx1(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseInEnclaveSgx1) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used inside an enclave (SGX2)
/// </summary>
bool GetUseInEnclaveSgx2(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseInEnclaveSgx2) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used outside VMX operation
/// </summary>
bool GetUseOutsideVmxOp(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseOutsideVmxOp) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used in VMX root operation
/// </summary>
bool GetUseInVmxRootOp(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseInVmxRootOp) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used in VMX non-root operation
/// </summary>
bool GetUseInVmxNonRootOp(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseInVmxNonRootOp) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used outside SEAM
/// </summary>
bool GetUseOutsideSeam(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseOutsideSeam) != 0;
}

/// <summary>
/// <see langword="true"/> if it can be used in SEAM
/// </summary>
bool GetUseInSeam(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_UseInSeam) != 0;
}

/// <summary>
/// <see langword="true"/> if #UD is generated in TDX non-root operation
/// </summary>
bool GetTdxNonRootGenUd(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_TdxNonRootGenUd) != 0;
}

/// <summary>
/// <see langword="true"/> if #VE is generated in TDX non-root operation
/// </summary>
bool GetTdxNonRootGenVe(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_TdxNonRootGenVe) != 0;
}

/// <summary>
/// <see langword="true"/> if an exception (eg. #GP(0), #VE) may be generated in TDX non-root operation
/// </summary>
bool GetTdxNonRootMayGenEx(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_TdxNonRootMayGenEx) != 0;
}

/// <summary>
/// (Intel VMX) <see langword="true"/> if it causes a VM exit in VMX non-root operation
/// </summary>
bool GetIntelVmExit(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_IntelVmExit) != 0;
}

/// <summary>
/// (Intel VMX) <see langword="true"/> if it may cause a VM exit in VMX non-root operation
/// </summary>
bool GetIntelMayVmExit(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_IntelMayVmExit) != 0;
}

/// <summary>
/// (Intel VMX) <see langword="true"/> if it causes an SMM VM exit in VMX root operation (if dual-monitor treatment is activated)
/// </summary>
bool GetIntelSmmVmExit(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_IntelSmmVmExit) != 0;
}

/// <summary>
/// (AMD SVM) <see langword="true"/> if it causes a #VMEXIT in guest mode
/// </summary>
bool GetAmdVmExit(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_AmdVmExit) != 0;
}

/// <summary>
/// (AMD SVM) <see langword="true"/> if it may cause a #VMEXIT in guest mode
/// </summary>
bool GetAmdMayVmExit(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_AmdMayVmExit) != 0;
}

/// <summary>
/// <see langword="true"/> if it causes a TSX abort inside a TSX transaction
/// </summary>
bool GetTsxAbort(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_TsxAbort) != 0;
}

/// <summary>
/// <see langword="true"/> if it causes a TSX abort inside a TSX transaction depending on the implementation
/// </summary>
bool GetTsxImplAbort(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_TsxImplAbort) != 0;
}

/// <summary>
/// <see langword="true"/> if it may cause a TSX abort inside a TSX transaction depending on some condition
/// </summary>
bool GetTsxMayAbort(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_TsxMayAbort) != 0;
}

/// <summary>
/// <see langword="true"/> if it's decoded by iced's 16-bit Intel decoder
/// </summary>
bool GetIntelDecoder16(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_IntelDecoder16or32) != 0;
}

/// <summary>
/// <see langword="true"/> if it's decoded by iced's 32-bit Intel decoder
/// </summary>
bool GetIntelDecoder32(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_IntelDecoder16or32) != 0;
}

/// <summary>
/// <see langword="true"/> if it's decoded by iced's 64-bit Intel decoder
/// </summary>
bool GetIntelDecoder64(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_IntelDecoder64) != 0;
}

/// <summary>
/// <see langword="true"/> if it's decoded by iced's 16-bit AMD decoder
/// </summary>
bool GetAmdDecoder16(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_AmdDecoder16or32) != 0;
}

/// <summary>
/// <see langword="true"/> if it's decoded by iced's 32-bit AMD decoder
/// </summary>
bool GetAmdDecoder32(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_AmdDecoder16or32) != 0;
}

/// <summary>
/// <see langword="true"/> if it's decoded by iced's 64-bit AMD decoder
/// </summary>
bool GetAmdDecoder64(struct OpCodeInfo* o)
{
	return (o->opcFlags2 & OCIF2_AmdDecoder64) != 0;
}

enum DecoderOptions // : uint 
{
	/// <summary>No option is enabled</summary>
	DecoderOptions_None = 0x00000000,
	/// <summary>Disable some checks for invalid encodings of instructions, eg. most instructions can&apos;t use a <c>LOCK</c> prefix so if one is found, they&apos;re decoded as <see cref="Code.INVALID"/> unless this option is enabled.</summary>
	DecoderOptions_NoInvalidCheck = 0x00000001,
	/// <summary>AMD decoder: allow 16-bit branch/ret instructions in 64-bit mode, no <c>o64 CALL/JMP FAR [mem], o64 LSS/LFS/LGS</c>, <c>UD0</c> has no modr/m byte, decode <c>LOCK MOV CR</c>. The AMD decoder can still decode Intel instructions.</summary>
	DecoderOptions_AMD = 0x00000002,
	/// <summary>Decode opcodes <c>0F0D</c> and <c>0F18-0F1F</c> as reserved-nop instructions (eg. <see cref="Code.Reservednop_rm32_r32_0F1D"/>)</summary>
	DecoderOptions_ForceReservedNop = 0x00000004,
	/// <summary>Decode <c>UMOV</c> instructions</summary>
	DecoderOptions_Umov = 0x00000008,
	/// <summary>Decode <c>XBTS</c>/<c>IBTS</c></summary>
	DecoderOptions_Xbts = 0x00000010,
	/// <summary>Decode <c>0FA6</c>/<c>0FA7</c> as <c>CMPXCHG</c></summary>
	DecoderOptions_Cmpxchg486A = 0x00000020,
	/// <summary>Decode some old removed FPU instructions (eg. <c>FRSTPM</c>)</summary>
	DecoderOptions_OldFpu = 0x00000040,
	/// <summary>Decode <c>PCOMMIT</c></summary>
	DecoderOptions_Pcommit = 0x00000080,
	/// <summary>Decode 286 <c>STOREALL</c>/<c>LOADALL</c> (<c>0F04</c> and <c>0F05</c>)</summary>
	DecoderOptions_Loadall286 = 0x00000100,
	/// <summary>Decode 386 <c>LOADALL</c></summary>
	DecoderOptions_Loadall386 = 0x00000200,
	/// <summary>Decode <c>CL1INVMB</c></summary>
	DecoderOptions_Cl1invmb = 0x00000400,
	/// <summary>Decode <c>MOV r32,tr</c> and <c>MOV tr,r32</c></summary>
	DecoderOptions_MovTr = 0x00000800,
	/// <summary>Decode <c>JMPE</c> instructions</summary>
	DecoderOptions_Jmpe = 0x00001000,
	/// <summary>Don&apos;t decode <c>PAUSE</c>, decode <c>NOP</c> instead</summary>
	DecoderOptions_NoPause = 0x00002000,
	/// <summary>Don&apos;t decode <c>WBNOINVD</c>, decode <c>WBINVD</c> instead</summary>
	DecoderOptions_NoWbnoinvd = 0x00004000,
	/// <summary>Decode undocumented Intel <c>RDUDBG</c> and <c>WRUDBG</c> instructions</summary>
	DecoderOptions_Udbg = 0x00008000,
	/// <summary>Don&apos;t decode <c>TZCNT</c>, decode <c>BSF</c> instead</summary>
	DecoderOptions_NoMPFX_0FBC = 0x00010000,
	/// <summary>Don&apos;t decode <c>LZCNT</c>, decode <c>BSR</c> instead</summary>
	DecoderOptions_NoMPFX_0FBD = 0x00020000,
	/// <summary>Don&apos;t decode <c>LAHF</c> and <c>SAHF</c> in 64-bit mode</summary>
	DecoderOptions_NoLahfSahf64 = 0x00040000,
	/// <summary>Decode <c>MPX</c> instructions</summary>
	DecoderOptions_MPX = 0x00080000,
	/// <summary>Decode most Cyrix instructions: <c>FPU</c>, <c>EMMI</c>, <c>SMM</c>, <c>DDI</c></summary>
	DecoderOptions_Cyrix = 0x00100000,
	/// <summary>Decode Cyrix <c>SMINT 0F7E</c> (Cyrix 6x86 or earlier)</summary>
	DecoderOptions_Cyrix_SMINT_0F7E = 0x00200000,
	/// <summary>Decode Cyrix <c>DMI</c> instructions (AMD Geode GX/LX)</summary>
	DecoderOptions_Cyrix_DMI = 0x00400000,
	/// <summary>Decode Centaur <c>ALTINST</c></summary>
	DecoderOptions_ALTINST = 0x00800000,
	/// <summary>Decode Intel Knights Corner instructions</summary>
	DecoderOptions_KNC = 0x01000000,
};

enum DecoderOptions toDecoderOptions[18] =
{
	DecoderOptions_None,
	DecoderOptions_ALTINST,
	DecoderOptions_Cl1invmb,
	DecoderOptions_Cmpxchg486A,
	DecoderOptions_Cyrix,
	DecoderOptions_Cyrix_DMI,
	DecoderOptions_Cyrix_SMINT_0F7E,
	DecoderOptions_Jmpe,
	DecoderOptions_Loadall286,
	DecoderOptions_Loadall386,
	DecoderOptions_MovTr,
	DecoderOptions_MPX,
	DecoderOptions_OldFpu,
	DecoderOptions_Pcommit,
	DecoderOptions_Umov,
	DecoderOptions_Xbts,
	DecoderOptions_Udbg,
	DecoderOptions_KNC,
};

/// <summary>
/// Gets the decoder option that's needed to decode the instruction or <see cref="DecoderOptions.None"/>
/// </summary>
enum DecoderOptions GetDecoderOption(struct OpCodeInfo* o)
{
	return toDecoderOptions[(int)(((unsigned int)o->opcFlags1 >> (int)OCIF1_DecOptionValueShift) & (unsigned int)OCIF1_DecOptionValueMask)];
}

/// <summary>
/// Gets the opcode table
/// </summary>
enum OpCodeTableKind GetTable(struct OpCodeInfo* o)
{
	return (enum OpCodeTableKind)o->table;
}

/// <summary>
/// Gets the mandatory prefix
/// </summary>
enum MandatoryPrefix GetMandatoryPrefix(struct OpCodeInfo* o)
{
	return (enum MandatoryPrefix)o->mandatoryPrefix;
}

/// <summary>
/// Gets the opcode byte(s). The low byte(s) of this value is the opcode. The length is in <see cref="OpCodeLength"/>.
/// It doesn't include the table value, see <see cref="Table"/>.
/// <br/>
/// Example values: <c>0xDFC0</c> (<see cref="Code.Ffreep_sti"/>), <c>0x01D8</c> (<see cref="Code.Vmrunw"/>), <c>0x2A</c> (<see cref="Code.Sub_r8_rm8"/>, <see cref="Code.Cvtpi2ps_xmm_mmm64"/>, etc).
/// </summary>
unsigned int OpCodeInfo_GetOpCode(struct OpCodeInfo* o)
{
	return (unsigned short)((unsigned int)o->encFlags2 >> (int)EFLAGS2_OpCodeShift);
}

/// <summary>
/// Gets the length of the opcode bytes (<see cref="OpCode"/>). The low bytes is the opcode value.
/// </summary>
int OpCodeInfo_OpCodeLength(struct OpCodeInfo* o)
{
	return (o->encFlags2 & EFLAGS2_OpCodeIs2Bytes) != 0 ? 2 : 1;
}

/// <summary>
/// Group index (0-7) or -1. If it's 0-7, it's stored in the <c>reg</c> field of the <c>modrm</c> byte.
/// </summary>
bool GetGroupIndex(struct OpCodeInfo* o)
{
	return o->groupIndex;
}

/// <summary>
/// <see langword="true"/> if it's part of a group
/// </summary>
bool GetIsGroup(struct OpCodeInfo* o)
{
	return GetGroupIndex(o) >= 0;
}

/// <summary>
/// modrm.rm group index (0-7) or -1. If it's 0-7, it's stored in the <c>rm</c> field of the <c>modrm</c> byte.
/// </summary>
int GetRmGroupIndex(struct OpCodeInfo* o)
{
	return o->rmGroupIndex;
}

/// <summary>
/// <see langword="true"/> if it's part of a modrm.rm group
/// </summary>
int GetIsRmGroup(struct OpCodeInfo* o)
{
	return GetRmGroupIndex(o) >= 0;
}

/// <summary>
/// Gets the number of operands
/// </summary>
int OpCount(struct OpCodeInfo* o)
{
	int code = (int)o->code;

	return InstructionOpCounts_OpCount[code];
}

/// <summary>
/// Gets operand #0's opkind
/// </summary>
enum OpCodeOperandKind OpCodeInfo_GetOp0Kind(struct OpCodeInfo* o)
{
	return (enum OpCodeOperandKind)o->op0Kind;
}

/// <summary>
/// Gets operand #1's opkind
/// </summary>
enum OpCodeOperandKind OpCodeInfo_GetOp1Kind(struct OpCodeInfo* o)
{
	return (enum OpCodeOperandKind)o->op1Kind;
}

/// <summary>
/// Gets operand #2's opkind
/// </summary>
enum OpCodeOperandKind OpCodeInfo_GetOp2Kind(struct OpCodeInfo* o)
{
	return (enum OpCodeOperandKind)o->op2Kind;
}

/// <summary>
/// Gets operand #3's opkind
/// </summary>
enum OpCodeOperandKind OpCodeInfo_GetOp3Kind(struct OpCodeInfo* o)
{
	return (enum OpCodeOperandKind)o->op3Kind;
}

/// <summary>
/// Gets operand #4's opkind
/// </summary>
enum OpCodeOperandKind OpCodeInfo_GetOp4Kind(struct OpCodeInfo* o)
{
	return (enum OpCodeOperandKind)o->op4Kind;
}



/// <summary>
/// Gets an operand's opkind
/// </summary>
/// <param name="operand">Operand number, 0-4</param>
/// <returns></returns>
enum OpCodeOperandKind GetOpKind(struct OpCodeInfo* o, int operand)
{
	switch (operand)
	{
	case 0:
		return OpCodeInfo_GetOp0Kind(o);
	case 1:
		return OpCodeInfo_GetOp1Kind(o);
	case 2:
		return OpCodeInfo_GetOp2Kind(o);
	case 3:
		return OpCodeInfo_GetOp3Kind(o);
	case 4:
		return OpCodeInfo_GetOp4Kind(o);
	default:
		// throw new ArgumentOutOfRangeException(nameof(operand));
		break;
	}
}

/// <summary>
/// Checks if the instruction is available in 16-bit mode, 32-bit mode or 64-bit mode
/// </summary>
/// <param name="bitness">16, 32 or 64</param>
/// <returns></returns>
bool IsAvailableInMode(struct OpCodeInfo* o, int bitness)
{
	switch (bitness)
	{
	case 16:
		return GetMode16(o);
	case 32:
		return GetMode32(o);
	case 64:
		return GetMode64(o);
	default:
		//throw new ArgumentOutOfRangeException(nameof(bitness))
		break;
	}
}

/// <summary>
/// Gets the opcode string, eg. <c>VEX.128.66.0F38.W0 78 /r</c>, see also <see cref="ToInstructionString()"/>
/// </summary>
/// <returns></returns>
//public string ToOpCodeString() = > toOpCodeStringValue;

/// <summary>
/// Gets the instruction string, eg. <c>VPBROADCASTB xmm1, xmm2/m8</c>, see also <see cref="ToOpCodeString()"/>
/// </summary>
/// <returns></returns>
//public string ToInstructionString() = > toInstructionStringValue;

/// <summary>
/// Gets the instruction string, eg. <c>VPBROADCASTB xmm1, xmm2/m8</c>, see also <see cref="ToOpCodeString()"/>
/// </summary>
/// <returns></returns>
//public override string ToString() = > ToInstructionString();

#endif