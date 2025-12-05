#include "OpCodeInfo.h"
#include "InstructionMemorySizes.h"

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

struct OpCodeInfo* OpCodeInfo_new()
{
	struct OpCodeInfo* o = (struct OpCodeInfo*)malloc(sizeof(struct OpCodeInfo));
	return o;
}

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
	case CodeSize_Unknown:
		(*opcodeInfo)->operandSize = 0;
		break;
	case CodeSize_Code16:
		(*opcodeInfo)->operandSize = 16;
		break;
	case CodeSize_Code32:
		(*opcodeInfo)->operandSize = 32;
		break;
	case CodeSize_Code64:
		(*opcodeInfo)->operandSize = 64;
		break;
	default:
		// throw new InvalidOperationException()
		break;
	}

	switch ((enum CodeSize)(((unsigned int)encFlags3 >> (int)EFLAGS3_AddressSizeShift) & (unsigned int)EFLAGS3_AddressSizeMask))
	{
	case CodeSize_Unknown:
		(*opcodeInfo)->addressSize = 0;
		break;
	case CodeSize_Code16:
		(*opcodeInfo)->addressSize = 16;
		break;
	case CodeSize_Code32:
		(*opcodeInfo)->addressSize = 32;
		break;
	case CodeSize_Code64:
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
		(*opcodeInfo)->op0Kind = OpCodeOperandKinds_LegacyOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op0Shift) & (unsigned int)EFLAGS_Legacy_OpMask)];
		(*opcodeInfo)->op1Kind = OpCodeOperandKinds_LegacyOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op1Shift) & (unsigned int)EFLAGS_Legacy_OpMask)];
		(*opcodeInfo)->op2Kind = OpCodeOperandKinds_LegacyOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op2Shift) & (unsigned int)EFLAGS_Legacy_OpMask)];
		(*opcodeInfo)->op3Kind = OpCodeOperandKinds_LegacyOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op3Shift) & (unsigned int)EFLAGS_Legacy_OpMask)];

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
		(*opcodeInfo)->op0Kind = OpCodeOperandKinds_VexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op0Shift) & (unsigned int)EFLAGS_VEX_OpMask)];
		(*opcodeInfo)->op1Kind = OpCodeOperandKinds_VexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op1Shift) & (unsigned int)EFLAGS_VEX_OpMask)];
		(*opcodeInfo)->op2Kind = OpCodeOperandKinds_VexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op2Shift) & (unsigned int)EFLAGS_VEX_OpMask)];
		(*opcodeInfo)->op3Kind = OpCodeOperandKinds_VexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op3Shift) & (unsigned int)EFLAGS_VEX_OpMask)];
		(*opcodeInfo)->op4Kind = OpCodeOperandKinds_VexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op4Shift) & (unsigned int)EFLAGS_VEX_OpMask)];

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
		(*opcodeInfo)->op0Kind = OpCodeOperandKinds_EvexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op0Shift) & (unsigned int)EFLAGS_EVEX_OpMask)];
		(*opcodeInfo)->op1Kind = OpCodeOperandKinds_EvexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op1Shift) & (unsigned int)EFLAGS_EVEX_OpMask)];
		(*opcodeInfo)->op2Kind = OpCodeOperandKinds_EvexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op2Shift) & (unsigned int)EFLAGS_EVEX_OpMask)];
		(*opcodeInfo)->op3Kind = OpCodeOperandKinds_EvexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_EVEX_Op3Shift) & (unsigned int)EFLAGS_EVEX_OpMask)];

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
		(*opcodeInfo)->op0Kind = OpCodeOperandKinds_XopOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op0Shift) & (unsigned int)EFLAGS_XOP_OpMask)];
		(*opcodeInfo)->op1Kind = OpCodeOperandKinds_XopOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op1Shift) & (unsigned int)EFLAGS_XOP_OpMask)];
		(*opcodeInfo)->op2Kind = OpCodeOperandKinds_XopOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op2Shift) & (unsigned int)EFLAGS_XOP_OpMask)];
		(*opcodeInfo)->op3Kind = OpCodeOperandKinds_XopOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_XOP_Op3Shift) & (unsigned int)EFLAGS_XOP_OpMask)];

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
		(*opcodeInfo)->op0Kind = OpCodeOperandKinds_MvexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op0Shift) & (unsigned int)EFLAGS_MVEX_OpMask)];
		(*opcodeInfo)->op1Kind = OpCodeOperandKinds_MvexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op1Shift) & (unsigned int)EFLAGS_MVEX_OpMask)];
		(*opcodeInfo)->op2Kind = OpCodeOperandKinds_MvexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op2Shift) & (unsigned int)EFLAGS_MVEX_OpMask)];
		(*opcodeInfo)->op3Kind = OpCodeOperandKinds_MvexOpKinds[(int)(((unsigned int)encFlags1 >> (int)EFLAGS_MVEX_Op3Shift) & (unsigned int)EFLAGS_MVEX_OpMask)];

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

enum TupleType Instruction_GetTupleType(struct Instruction* i, int sss)
{
	return (enum TupleType)MvexTupleTypeLut_Data[(int)TupleTypeLutKind(i) * 8 + sss];
}

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
	(*o)->ehbit = (enum MvexEHBit)MvexInfoData_Data[(*o)->index * MvexInfoData_StructSize + MvexInfoData_EHBitIndex];
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
		struct MvexInfo* mvex_info = MvexInfo_new(code);

		return mvex_info->ehbit;
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

/// <summary>
/// Gets the decoder option that's needed to decode the instruction or <see cref="DecoderOptions.None"/>
/// </summary>
//enum DecoderOptions GetDecoderOption(struct OpCodeInfo* o)
//{
//	return toDecoderOptions[(int)(((unsigned int)o->opcFlags1 >> (int)OCIF1_DecOptionValueShift) & (unsigned int)OCIF1_DecOptionValueMask)];
//}

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