#ifndef INSTRUCTION_CREATE
#define INSTRUCTION_CREATE

#include "OpCodeInfo.h"
#include "Instruction.h"
#include "OpCodeInfoData.h"
#include "Encoder.h"

enum HandlerTypeConfig
{
	UndefinedHandler,
	InvalidHandler,
	DeclareDataHandler,
	ZeroBytesHandler,
	LegacyHandler,
	VexHandler,
	EvexHandler,
	XopHandler,
	D3nowHandler,
	MvexHandler,
};

enum OperandType 
{
	OT_UNDEFINED,
	OT_OpA,
	OT_OpO,
	OT_OpModRM_rm_mem_only,
	OT_OpModRM_rm,
	OT_OpRegEmbed8,
	OT_OpModRM_reg,
	OT_OpModRM_reg_mem,
	OT_OpModRM_rm_reg_only,
	OT_OpModRM_regF0,
	OT_OpReg,
	OT_OpRegSTi,
	OT_OpIb,
	OT_OpImm,
	OT_OpIw,
	OT_OpId,
	OT_OpIq,
	OT_OpX,
	OT_OpY,
	OT_OprDI,
	OT_OpMRBX,
	OT_OpJ,
	OT_OpJx,
	OT_OpJdisp,
	OT_OpVsib,
	OT_OpHx,
	OT_OpIsX,
	OP_OpMRBX,
	OP_OpI4,
	OP_OprDI
};

struct Op 
{
	enum OperandType operand_type;

	int size;					// OpA
	bool mustUseSib;			// OpModRM_rm_mem_only
	enum Register regLo;		// OpModRM_rm
	enum Register regHi;		// OpModRM_rm
	enum Register _register;	// OpReg
	unsigned char value;		// OpImm
	enum OpKind opKind;			// OpId
	int immSize;				// OpJ
	int displSize;				// OpJdisp

	void (*Encode)(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);
	enum OpKind(*GetImmediateOpKind)(struct Op* op);
	enum OpKind(*GetNearBranchOpKind)(struct Op* op);
	enum OpKind(*GetFarBranchOpKind)(struct Op* op);
};

enum OpKind OpDefault_GetImmediateOpKind(struct Op* op);

enum OpKind OpDefault_GetNearBranchOpKind(struct Op* op);

enum OpKind OpDefault_GetFarBranchOpKind(struct Op* op);

void OpA_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);
enum OpKind OpA_GetFarBranchOpKind(struct Op* op);

void OpO_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpModRM_rm_mem_only_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpModRM_rm_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpModRM_reg_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpRegEmbed8_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpModRM_reg_mem_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpModRM_rm_reg_only_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpModRM_regF0_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpReg_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpRegSTi_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpIb_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpImm_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

enum OpKind OpImm_GetImmediateOpKind(struct Op* op);

void OpIw_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

enum OpKind OpIw_GetImmediateOpKind(struct Op* op);

void OpId_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

enum OpKind OpId_GetImmediateOpKind(struct Op* op);

void OpIq_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

enum OpKind OpIq_GetImmediateOpKind(struct Op* op);
 
int GetXRegSize(enum OpKind opKind);

int GetYRegSize(enum OpKind opKind);

void OpX_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpY_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

int GetRegSize(enum OpKind opKind);

void OprDI_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpMRBX_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpJ_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

enum OpKind OpJ_GetNearBranchOpKind(struct Op* op);

void OpJx_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

enum OpKind OpJx_GetNearBranchOpKind(struct Op* op);

void OpJdisp_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

enum OpKind OpJdisp_GetNearBranchOpKind(struct Op* op);

void OpVsib_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpHx_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

void OpIsX_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

enum OpKind OpI4_GetImmediateOpKind(struct Op* op);

void OpI4_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);

struct Op* Op_new();

struct Op* OpA_new(int size);

struct Op* OpO_new();

struct Op* OpModRM_rm_mem_only_new(bool mustUseSib);

struct Op* OpModRM_rm_new(enum Register regLo, enum Register regHi);

struct Op* OpModRM_reg_new(enum Register regLo, enum Register regHi);

struct Op* OpRegEmbed8_new(enum Register regLo, enum Register regHi);

struct Op* OpModRM_reg_mem_new(enum Register regLo, enum Register regHi);

struct Op* OpModRM_rm_reg_only_new(enum Register regLo, enum Register regHi);

struct Op* OpModRM_regF0_new(enum Register regLo, enum Register regHi);

struct Op* OpReg_new(enum Register _register);

struct Op* OpRegSTi_new();

struct Op* OpIb_new(enum OpKind opKind);

struct Op* OpImm_new(unsigned char value);

struct Op* OpIw_new();

struct Op* OpId_new();

struct Op* OpIq_new();

struct Op* OpX_new();

struct Op* OpY_new();

struct Op* OprDI_new();

struct Op* OpMRBX_new();

struct Op* OpJ_new(enum OpKind opKind, int immSize);

struct Op* OpJx_new(int immSize);

struct Op* OpJdisp_new(int displSize);

struct Op* OpVsib_new(enum Register regLo, enum Register regHi);

struct Op* OpHx_new(enum Register regLo, enum Register regHi);

struct Op* OpIsX_new(enum Register regLo, enum Register regHi);

struct Op* OpI4_new();

// Op Tables.
struct Op* Operands_LegacyOps();

// Op Tables.
struct Op* Operands_VexOps();

// Op Tables.
struct Op* Operands_EVexOps();

// Op Tables.
struct Op* Operands_XopOps();

// Op Tables.
struct Op* Operands_MVEXOps();

// Op Tables.
struct Op* Operands_D3NowOps();

struct OpCodeHandler
{
	enum HandlerTypeConfig handler_conf;
	unsigned int OpCode;
	bool Is2ByteOpCode;
	int GroupIndex;
	int RmGroupIndex;
	bool IsSpecialInstr;
	enum EncFlags2 EncFlags2;
	enum EncFlags3 EncFlags3;
	enum CodeSize OpSize;
	enum CodeSize AddrSize;
	struct Op* Operands;
	unsigned int Operands_Length;

	bool(*TryConvertToDisp8N)(struct Encoder* encoder, struct OpCodeHandler* handler, struct Instruction* instruction, int displ, signed char* compressedValue);
	unsigned int (*GetOpCode)(struct OpCodeHandler* self, enum EncFlags2 encFlags2);
	void (*Encode)(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

	// DeclareDataHandler
	int elemLength;
	int maxLength;

	// LegacyHandler
	unsigned int tableByte1;
	unsigned int tableByte2;
	unsigned int mandatoryPrefix;

	// VexHandler
	unsigned int table;
	unsigned int lastByte;
	unsigned int mask_W_L;
	unsigned int mask_L;
	unsigned int W1;

	// EvexHandler
	enum WBit wbit;
	enum TupleType tupleType;
	unsigned int p1Bits;
	unsigned int llBits;
	unsigned int mask_W;
	unsigned int mask_LL;

	// XopHandler
	unsigned int lastByte;

	// MvexHandler
	//enum WBit wbit;
	//unsigned int table;
	//unsigned int p1Bits;
	//unsigned int mask_W;

	// D3nowHandler
	unsigned int immediate;
};

unsigned char SegmentOverrides[6] =
{
	0x26,
	0x2E,
	0x36,
	0x3E,
	0x64,
	0x65
};

unsigned int s_immSizes[19] =
{
	0,// None
	1,// Size1
	2,// Size2
	4,// Size4
	8,// Size8
	3,// Size2_1
	2,// Size1_1
	4,// Size2_2
	6,// Size4_2
	1,// RipRelSize1_Target16
	1,// RipRelSize1_Target32
	1,// RipRelSize1_Target64
	2,// RipRelSize2_Target16
	2,// RipRelSize2_Target32
	2,// RipRelSize2_Target64
	4,// RipRelSize4_Target32
	4,// RipRelSize4_Target64
	1,// SizeIbReg
	1,// Size1OpCode
};

const char* ERROR_ONLY_1632_BIT_MODE = "The instruction can only be used in 16/32-bit mode";
const char* ERROR_ONLY_64_BIT_MODE = "The instruction can only be used in 64-bit mode";

struct Encoder
{
	unsigned int Internal_PreventVEX2;
	unsigned int Internal_VEX_WIG_LIG;
	unsigned int Internal_VEX_LIG;
	unsigned int Internal_EVEX_WIG;
	unsigned int Internal_EVEX_LIG;
	unsigned int Internal_MVEX_WIG;

	//readonly CodeWriter writer;
	int bitness;
	//OpCodeHandler[] handlers;
	//unsigned int immSizes[19];
	unsigned long currentRip;
	char* errorMessage;
	struct OpCodeHandler* handler;
	unsigned int eip;
	unsigned int displAddr;
	unsigned int immAddr;
	unsigned int Immediate;
	// high 32 bits if it's a 64-bit immediate
	// high 32 bits if it's an IP relative immediate (jcc,call target)
	// high 32 bits if it's a 64-bit absolute address
	unsigned int ImmediateHi;
	unsigned int Displ;
	// high 32 bits if it's an IP relative mem displ (target)
	unsigned int DisplHi;
	enum EncoderFlags opSize16Flags;
	enum EncoderFlags opSize32Flags;
	enum EncoderFlags adrSize16Flags;
	enum EncoderFlags adrSize32Flags;
	unsigned int OpCode;
	enum EncoderFlags EncoderFlags;
	enum DisplSize DisplSize;
	enum ImmSize ImmSize;
	unsigned char ModRM;
	unsigned char Sib;
};

enum EncodingKind GetEncodingKindByOpcode(enum Code opcode);

void OpCodeHandler_init(struct OpCodeHandler** o,
	enum EncFlags2 encFlags2,
	enum EncFlags3 encFlags3,
	bool isSpecialInstr,
	struct Op* operands,
	unsigned int operands_length,
	bool(*TryConvertToDisp8N)(struct Encoder* encoder, struct OpCodeHandler* handler, struct Instruction* instruction, int displ, signed char* compressedValue),
	unsigned int (*GetOpCode)(struct OpCodeHandler* self, enum EncFlags2 encFlags2),
	void (*Encode)(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction));

void Encoder_WriteByteInternal(struct Encoder* encoder, unsigned char byte_value)
{
	//TODO:
}

unsigned int OpCodeHandler_GetOpCode(struct OpCodeHandler* self, enum EncFlags2 encFlags2)
{
	return (unsigned short)((unsigned int)encFlags2 >> (int)EFLAGS2_OpCodeShift);
}

void InvalidHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	//const string ERROR_MESSAGE = "Can't encode an invalid instruction";
}

void DeclareDataHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	enum Code opcode = (enum Code)instruction->code;


	int declDataCount = GetDeclareDataCount(instruction);

	switch (opcode)
	{
	case DeclareByte:
		self->elemLength = 1;
		break;
	case DeclareWord:
		self->elemLength = 2;
		break;
	case DeclareDword:
		self->elemLength = 4;
		break;
	case DeclareQword:
		self->elemLength = 8;
		break;
	default:
		// throw new InvalidOperationException();
		break;
	}

	self->maxLength = 16 / self->elemLength;

	if (declDataCount < 1 || declDataCount > self->maxLength)
	{
		//encoder.ErrorMessage = $"Invalid db/dw/dd/dq data count. Count = {declDataCount}, max count = {maxLength}";
		return;
	}
	int length = declDataCount * self->elemLength;
	for (int i = 0; i < length; i++)
	{
		unsigned char b = GetDeclareByteValue(instruction, i);

		Encoder_WriteByteInternal(encoder, b);
	}
}

void ZeroBytesHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	// Do nothing.
}

void LegacyHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	unsigned int b = self->mandatoryPrefix;
	Encoder_WritePrefixes(encoder, instruction, b != 0xF3);
	if (b != 0)
	{
		Encoder_WriteByteInternal(encoder, b);
	}
	//Static.Assert((int)EncoderFlags.B == 0x01 ? 0 : -1);
	//Static.Assert((int)EncoderFlags.X == 0x02 ? 0 : -1);
	//Static.Assert((int)EncoderFlags.R == 0x04 ? 0 : -1);
	//Static.Assert((int)EncoderFlags.W == 0x08 ? 0 : -1);
	//Static.Assert((int)EncoderFlags.REX == 0x40 ? 0 : -1);
	b = (unsigned int)encoder->EncoderFlags;
	b &= 0x4F;
	if (b != 0) {
		if ((encoder->EncoderFlags & EncoderFlags_HighLegacy8BitRegs) != 0)
		{
			//encoder.ErrorMessage = "Registers AH, CH, DH, BH can't be used if there's a REX prefix. Use AL, CL, DL, BL, SPL, BPL, SIL, DIL, R8L-R15L instead.";
		}
		b |= 0x40;
		
		Encoder_WriteByteInternal(encoder, b);
	}

	if ((b = self->tableByte1) != 0)
	{
		Encoder_WriteByteInternal(encoder, b);
		if ((b = self->tableByte2) != 0)
		{
			Encoder_WriteByteInternal(encoder, b);
		}
	}
}

void VEXHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	bool canWriteF3 = true;
	Encoder_WritePrefixes(encoder, instruction, canWriteF3);

	unsigned int encoderFlags = (unsigned int)encoder->EncoderFlags;

	//Static.Assert((int)MandatoryPrefixByte.None == 0 ? 0 : -1);
	//Static.Assert((int)MandatoryPrefixByte.P66 == 1 ? 0 : -1);
	//Static.Assert((int)MandatoryPrefixByte.PF3 == 2 ? 0 : -1);
	//Static.Assert((int)MandatoryPrefixByte.PF2 == 3 ? 0 : -1);
	unsigned int b = self->lastByte;
	b |= (~encoderFlags >> ((int)EncoderFlags_VvvvvShift - 3)) & 0x78;

	unsigned int XBW = (unsigned int)(EncoderFlags_X | EncoderFlags_B | EncoderFlags_W);

	if ((encoder->Internal_PreventVEX2 | self->W1 | (self->table - (unsigned int)VexOpCodeTable_MAP0F) | (encoderFlags & XBW)) != 0)
	{
		Encoder_WriteByteInternal(encoder, 0xC4);
		//Static.Assert((int)VexOpCodeTable.MAP0F == 1 ? 0 : -1);
		//Static.Assert((int)VexOpCodeTable.MAP0F38 == 2 ? 0 : -1);
		//Static.Assert((int)VexOpCodeTable.MAP0F3A == 3 ? 0 : -1);
		unsigned int b2 = self->table;
		//Static.Assert((int)EncoderFlags.B == 1 ? 0 : -1);
		//Static.Assert((int)EncoderFlags.X == 2 ? 0 : -1);
		//Static.Assert((int)EncoderFlags.R == 4 ? 0 : -1);
		b2 |= (~encoderFlags & 7) << 5;
		Encoder_WriteByteInternal(encoder, b2);
		b |= self->mask_W_L & encoder->Internal_VEX_WIG_LIG;
		Encoder_WriteByteInternal(encoder, b);
	}
	else
	{
		Encoder_WriteByteInternal(encoder, 0xC5);
		//Static.Assert((int)EncoderFlags.R == 4 ? 0 : -1);
		b |= (~encoderFlags & 4) << 5;
		b |= self->mask_L & encoder->Internal_VEX_LIG;
		Encoder_WriteByteInternal(encoder, b);
	}
}

void EVEXHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	unsigned int encoderFlags = (unsigned int)encoder->EncoderFlags;

	Encoder_WriteByteInternal(encoder, 0x62);

	//Static.Assert((int)EvexOpCodeTable_MAP0F == 1 ? 0 : -1);
	//Static.Assert((int)EvexOpCodeTable_MAP0F38 == 2 ? 0 : -1);
	//Static.Assert((int)EvexOpCodeTable_MAP0F3A == 3 ? 0 : -1);
	//Static.Assert((int)EvexOpCodeTable_MAP5 == 5 ? 0 : -1);
	//Static.Assert((int)EvexOpCodeTable_MAP6 == 6 ? 0 : -1);
	unsigned int b = self->table;
	//Static.Assert((int)EncoderFlags_B == 1 ? 0 : -1);
	//Static.Assert((int)EncoderFlags_X == 2 ? 0 : -1);
	//Static.Assert((int)EncoderFlags_R == 4 ? 0 : -1);
	b |= (encoderFlags & 7) << 5;
	//Static.Assert((int)EncoderFlags_R2 == 0x00000200 ? 0 : -1);
	b |= (encoderFlags >> (9 - 4)) & 0x10;
	b ^= ~0xFU;
	Encoder_WriteByteInternal(encoder, b);

	b = self->p1Bits;
	b |= (~encoderFlags >> ((int)EncoderFlags_VvvvvShift - 3)) & 0x78;
	b |= self->mask_W & encoder->Internal_EVEX_WIG;
	Encoder_WriteByteInternal(encoder, b);

	b = GetInternalOpMask(instruction);
	if (b != 0) {
		if ((self->EncFlags3 & EFLAGS3_OpMaskRegister) == 0)
		{
			//encoder.ErrorMessage = "The instruction doesn't support opmask registers";
		}
	}
	else {
		if ((self->EncFlags3 & EFLAGS3_RequireOpMaskRegister) != 0)
		{
			//encoder.ErrorMessage = "The instruction must use an opmask register";
		}
	}
	b |= (encoderFlags >> ((int)EncoderFlags_VvvvvShift + 4 - 3)) & 8;
	if (GetSuppressAllExceptions(instruction))
	{
		if ((self->EncFlags3 & EFLAGS3_SuppressAllExceptions) == 0)
		{
			//encoder.ErrorMessage = "The instruction doesn't support suppress-all-exceptions";
		}
		b |= 0x10;
	}
	enum RoundingControl rc = GetRoundingControl(instruction);
	if (rc != RC_None)
	{
		if ((self->EncFlags3 & EFLAGS3_RoundingControl) == 0)
		{
			//encoder.ErrorMessage = "The instruction doesn't support rounding control";
		}
		b |= 0x10;
		//Static.Assert((int)RoundingControl.RoundToNearest == 1 ? 0 : -1);
		//Static.Assert((int)RoundingControl.RoundDown == 2 ? 0 : -1);
		//Static.Assert((int)RoundingControl.RoundUp == 3 ? 0 : -1);
		//Static.Assert((int)RoundingControl.RoundTowardZero == 4 ? 0 : -1);
		b |= (unsigned int)(rc - RC_RoundToNearest) << 5;
	}
	else if ((self->EncFlags3 & EFLAGS3_SuppressAllExceptions) == 0 || !GetSuppressAllExceptions(instruction))
	{
		b |= self->llBits;
	}
	if ((encoderFlags & (unsigned int)EncoderFlags_Broadcast) != 0)
	{
		b |= 0x10;
	}
	else if (IsBroadcast2(instruction))
	{
		//encoder.ErrorMessage = "The instruction doesn't support broadcasting";
	}
	if (GetZeroingMasking(instruction))
	{
		if ((self->EncFlags3 & EFLAGS3_ZeroingMasking) == 0)
		{
			//encoder.ErrorMessage = "The instruction doesn't support zeroing masking";
		}
		b |= 0x80;
	}
	b ^= 8;
	b |= self->mask_LL & encoder->Internal_EVEX_LIG;
	Encoder_WriteByteInternal(encoder, b);
}

void XopHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	Encoder_WritePrefixes(encoder, instruction, true);

	Encoder_WriteByteInternal(encoder, 0x8F);

	unsigned int encoderFlags = (unsigned int)encoder->EncoderFlags;
	//Static.Assert((int)MandatoryPrefixByte.None == 0 ? 0 : -1);
	//Static.Assert((int)MandatoryPrefixByte.P66 == 1 ? 0 : -1);
	//Static.Assert((int)MandatoryPrefixByte.PF3 == 2 ? 0 : -1);
	//Static.Assert((int)MandatoryPrefixByte.PF2 == 3 ? 0 : -1);

	unsigned int b = self->table;
	//Static.Assert((int)EncoderFlags.B == 1 ? 0 : -1);
	//Static.Assert((int)EncoderFlags.X == 2 ? 0 : -1);
	//Static.Assert((int)EncoderFlags.R == 4 ? 0 : -1);
	b |= (~encoderFlags & 7) << 5;
	Encoder_WriteByteInternal(encoder, b);
	b = self->lastByte;
	b |= (~encoderFlags >> ((int)EncoderFlags_VvvvvShift - 3)) & 0x78;
	Encoder_WriteByteInternal(encoder, b);
}

bool MvexHandler_TryConvertToDisp8N(struct Encoder* encoder, struct OpCodeHandler* handler, struct Instruction* instruction, int displ, signed char* compressedValue)
{
	struct MvexInfo* mvex = MvexInfo_new(GetCode(instruction));

	int sss = ((int)GetMvexRegMemConv(instruction) - (int)MRMC_MemConvNone) & 7;
	enum TupleType tupleType = (enum TupleType)MvexTupleTypeLut_Data[(int)mvex->TupleTypeLutKind * 8 + sss];

	int n = (int)TupleTypeTable_GetDisp8N(tupleType, false);
	int res = displ / n;
	if (res * n == displ && SCHAR_MIN <= res && res <= SCHAR_MAX)
	{
		compressedValue = (signed char)res;
		return true;
	}

	compressedValue = 0;
	return false;
}

void MvexHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	Encoder_WritePrefixes(encoder, instruction, true);

	unsigned int encoderFlags = (unsigned int)encoder->EncoderFlags;

	Encoder_WriteByteInternal(encoder, 0x62);

	//Static.Assert((int)MvexOpCodeTable.MAP0F == 1 ? 0 : -1);
	//Static.Assert((int)MvexOpCodeTable.MAP0F38 == 2 ? 0 : -1);
	//Static.Assert((int)MvexOpCodeTable.MAP0F3A == 3 ? 0 : -1);
	unsigned int b = self->table;
	//Static.Assert((int)EncoderFlags.B == 1 ? 0 : -1);
	//Static.Assert((int)EncoderFlags.X == 2 ? 0 : -1);
	//Static.Assert((int)EncoderFlags.R == 4 ? 0 : -1);
	b |= (encoderFlags & 7) << 5;
	//Static.Assert((int)EncoderFlags.R2 == 0x00000200 ? 0 : -1);
	b |= (encoderFlags >> (9 - 4)) & 0x10;
	b ^= ~0xFU;
	Encoder_WriteByteInternal(encoder, b);

	b = self->p1Bits;
	b |= (~encoderFlags >> ((int)EncoderFlags_VvvvvShift - 3)) & 0x78;
	b |= self->mask_W & encoder->Internal_MVEX_WIG;
	Encoder_WriteByteInternal(encoder, b);

	b = GetInternalOpMask(instruction);
	if (b != 0)
	{
		if ((self->EncFlags3 & EFLAGS3_OpMaskRegister) == 0)
		{
			//encoder.ErrorMessage = "The instruction doesn't support opmask registers";
		}
	}
	else
	{
		if ((self->EncFlags3 & EFLAGS3_RequireOpMaskRegister) != 0)
		{
			//encoder.ErrorMessage = "The instruction must use an opmask register";
		}
	}
	b |= (encoderFlags >> ((int)EncoderFlags_VvvvvShift + 4 - 3)) & 8;

	struct MvexInfo* mvex = MvexInfo_new(GetCode(instruction));

	enum MvexRegMemConv conv = GetMvexRegMemConv(instruction);
	// Memory ops can only be op0-op2, never op3 (imm8)
	if (GetOp0Kind(instruction) == OK_Memory || GetOp1Kind(instruction) == OK_Memory || GetOp2Kind(instruction) == OK_Memory)
	{
		if (conv >= MRMC_MemConvNone && conv <= MRMC_MemConvSint16)
		{
			b |= ((unsigned int)conv - (unsigned int)MRMC_MemConvNone) << 4;
		}
		else if (conv == MRMC_None)
		{
			// Nothing, treat it as MvexRegMemConv.MemConvNone
		}
		else
		{
			//encoder.ErrorMessage = "Memory operands must use a valid MvexRegMemConv variant, eg. MvexRegMemConv.MemConvNone";
		}
		if (GetIsMvexEvictionHint(instruction))
		{
			if (mvex->CanUseEvictionHint)
			{
				b |= 0x80;
			}
			else
			{
				//encoder.ErrorMessage = "This instruction doesn't support eviction hint (`{eh}`)";
			}
		}
	}
	else
	{
		if (GetIsMvexEvictionHint(instruction))
		{
			//encoder.ErrorMessage = "Only memory operands can enable eviction hint (`{eh}`)";
		}
		if (conv == MRMC_None)
		{
			b |= 0x80;
			if (GetSuppressAllExceptions(instruction))
			{
				b |= 0x40;
				if ((self->EncFlags3 & EFLAGS3_SuppressAllExceptions) == 0)
				{
					//encoder.ErrorMessage = "The instruction doesn't support suppress-all-exceptions";
				}
			}
			enum RoundingCongtrol rc = GetRoundingControl(instruction);
			if (rc == RC_None) {
				// Nothing
			}
			else {
				if ((self->EncFlags3 & EFLAGS3_RoundingControl) == 0)
				{
					//encoder.ErrorMessage = "The instruction doesn't support rounding control";
				}
				else
				{
					//Static.Assert((int)RoundingControl.RoundToNearest == 1 ? 0 : -1);
					//Static.Assert((int)RoundingControl.RoundDown == 2 ? 0 : -1);
					//Static.Assert((int)RoundingControl.RoundUp == 3 ? 0 : -1);
					//Static.Assert((int)RoundingControl.RoundTowardZero == 4 ? 0 : -1);
					b |= ((unsigned int)rc - (unsigned int)RC_RoundToNearest) << 4;
				}
			}
		}
		else if (conv >= MRMC_RegSwizzleNone && conv <= MRMC_RegSwizzleDddd)
		{
			if (GetSuppressAllExceptions(instruction))
			{
				//encoder.ErrorMessage = "Can't use {sae} with register swizzles";
			}
			else if (GetRoundingControl(instruction) != RC_None)
			{
				//encoder.ErrorMessage = "Can't use rounding control with register swizzles";
			}
			b |= (((unsigned int)conv - (unsigned int)MRMC_RegSwizzleNone) & 7) << 4;
		}
		else
		{
			//encoder.ErrorMessage = "Register operands can't use memory up/down conversions";
		}
	}
	if (mvex->EHBit == MEHB_EH1)
	{
		b |= 0x80;
	}
	b ^= 8;
	Encoder_WriteByteInternal(encoder, b);
}

void D3nowHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction)
{
	Encoder_WritePrefixes(encoder, instruction, true);
	Encoder_WriteByteInternal(encoder, 0x0F);
	encoder->ImmSize = ImmSize_Size1OpCode;
	encoder->Immediate = self->immediate;
}


unsigned int TupleTypeTable_GetDisp8N(enum TupleType tupleType, bool bcst)
{
	unsigned char tupleTypeData[38] =
	{
		// TupleType.N1
		0x01,// N
		0x01,// Nbcst
		// TupleType.N2
		0x02,// N
		0x02,// Nbcst
		// TupleType.N4
		0x04,// N
		0x04,// Nbcst
		// TupleType.N8
		0x08,// N
		0x08,// Nbcst
		// TupleType.N16
		0x10,// N
		0x10,// Nbcst
		// TupleType.N32
		0x20,// N
		0x20,// Nbcst
		// TupleType.N64
		0x40,// N
		0x40,// Nbcst
		// TupleType.N8b4
		0x08,// N
		0x04,// Nbcst
		// TupleType.N16b4
		0x10,// N
		0x04,// Nbcst
		// TupleType.N32b4
		0x20,// N
		0x04,// Nbcst
		// TupleType.N64b4
		0x40,// N
		0x04,// Nbcst
		// TupleType.N16b8
		0x10,// N
		0x08,// Nbcst
		// TupleType.N32b8
		0x20,// N
		0x08,// Nbcst
		// TupleType.N64b8
		0x40,// N
		0x08,// Nbcst
		// TupleType.N4b2
		0x04,// N
		0x02,// Nbcst
		// TupleType.N8b2
		0x08,// N
		0x02,// Nbcst
		// TupleType.N16b2
		0x10,// N
		0x02,// Nbcst
		// TupleType.N32b2
		0x20,// N
		0x02,// Nbcst
		// TupleType.N64b2
		0x40,// N
		0x02,// Nbcst
		// GENERATOR-END: TupleTypeTable
	};

	int index = ((int)tupleType << 1) | (bcst ? 1 : 0);
	//Debug.Assert((uint)index < (uint)tupleTypeData.Length);
	return tupleTypeData[index];
}

bool EVEXHandler_TryConvertToDisp8N(struct Encoder* encoder, struct OpCodeHandler* handler, struct Instruction* instruction, int displ, signed char* compressedValue)
{
	int n = (int)TupleTypeTable_GetDisp8N(handler->tupleType, (encoder->EncoderFlags & EncoderFlags_Broadcast) != 0);
	int res = displ / n;
	if (res * n == displ && SCHAR_MIN <= res && res <= SCHAR_MAX)
	{
		compressedValue = (signed char)res;
		return true;
	}

	compressedValue = 0;
	return false;
}

void OpCodeHandlers_init();
struct OpCodeHandler* GetOpCodeHandlers();
struct OpCodeHandler* GetOpCodeHandler(enum Code opcode);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
struct Instruction* Instruction_CreateDeclareByte_imm1(unsigned char b0);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
struct Instruction* Instruction_CreateDeclareByte_imm2(unsigned char b0, unsigned char b1);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
struct Instruction* Instruction_CreateDeclareByte_imm3(unsigned char b0, unsigned char b1, unsigned char b2);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
struct Instruction* Instruction_CreateDeclareByte_imm4(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
struct Instruction* Instruction_CreateDeclareByte_imm5(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
struct Instruction* Instruction_CreateDeclareByte_imm6(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
struct Instruction* Instruction_CreateDeclareByte_imm7(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
struct Instruction* Instruction_CreateDeclareByte_imm8(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
/// <param name="b8">Byte 8</param>
struct Instruction* Instruction_CreateDeclareByte_imm9(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
/// <param name="b8">Byte 8</param>
/// <param name="b9">Byte 9</param>
struct Instruction* Instruction_CreateDeclareByte_imm10(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
/// <param name="b8">Byte 8</param>
/// <param name="b9">Byte 9</param>
/// <param name="b10">Byte 10</param>
struct Instruction* Instruction_CreateDeclareByte_imm11(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
/// <param name="b8">Byte 8</param>
/// <param name="b9">Byte 9</param>
/// <param name="b10">Byte 10</param>
/// <param name="b11">Byte 11</param>
struct Instruction* Instruction_CreateDeclareByte_imm12(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
/// <param name="b8">Byte 8</param>
/// <param name="b9">Byte 9</param>
/// <param name="b10">Byte 10</param>
/// <param name="b11">Byte 11</param>
/// <param name="b12">Byte 12</param>
struct Instruction* Instruction_CreateDeclareByte_imm13(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11, unsigned char b12);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
/// <param name="b8">Byte 8</param>
/// <param name="b9">Byte 9</param>
/// <param name="b10">Byte 10</param>
/// <param name="b11">Byte 11</param>
/// <param name="b12">Byte 12</param>
/// <param name="b13">Byte 13</param>
struct Instruction* Instruction_CreateDeclareByte_imm14(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11, unsigned char b12, unsigned char b13);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
/// <param name="b8">Byte 8</param>
/// <param name="b9">Byte 9</param>
/// <param name="b10">Byte 10</param>
/// <param name="b11">Byte 11</param>
/// <param name="b12">Byte 12</param>
/// <param name="b13">Byte 13</param>
/// <param name="b14">Byte 14</param>
struct Instruction* Instruction_CreateDeclareByte_imm15(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11, unsigned char b12, unsigned char b13, unsigned char b14);

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
/// <param name="b6">Byte 6</param>
/// <param name="b7">Byte 7</param>
/// <param name="b8">Byte 8</param>
/// <param name="b9">Byte 9</param>
/// <param name="b10">Byte 10</param>
/// <param name="b11">Byte 11</param>
/// <param name="b12">Byte 12</param>
/// <param name="b13">Byte 13</param>
/// <param name="b14">Byte 14</param>
struct Instruction* Instruction_CreateDeclareByte_imm16(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11, unsigned char b12, unsigned char b13, unsigned char b14, unsigned char b15);

/// <summary>
/// Creates a new near/short branch instruction
/// </summary>
/// <param name="code">Code value</param>
/// <param name="target">Target address</param>
struct Instruction* Instruction_CreateBranch(enum Code code, unsigned long target);

/// <summary>
/// Creates a new far branch instruction
/// </summary>
/// <param name="code">Code value</param>
/// <param name="selector">Selector/segment value</param>
/// <param name="offset">Offset</param>
struct Instruction* Instruction_CreateBranch(enum Code code, unsigned short selector, unsigned int offset);

/// <summary>
/// Creates an instruction with 2 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register">op0: Register</param>
/// <param name="immediate">op1: Immediate value</param>
struct Instruction* Instruction_Create(enum Code code, enum Register _register, int immediate);

/// <summary>
/// Creates an instruction with 1 operand
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register">op0: Register</param>
struct Instruction* Instruction_Create(enum Code code, enum Register _register);

/// <summary>
/// Creates an instruction with 2 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register1">op0: Register</param>
/// <param name="register2">op1: Register</param>
struct Instruction* Instruction_Create(enum Code code, enum Register register1, enum Register register2);

/// <summary>
/// Creates an instruction with 2 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register">op0: Register</param>
/// <param name="memory">op1: Memory operand</param>
struct Instruction* Instruction_Create(enum Code code, enum Register register1, struct MemoryOperand* memory);

struct Instruction* CreateString_ESRDI_Reg(enum Code code, int addressSize, enum Register register1, enum RepPrefixKind repPrefix);

/// <summary>
/// Creates a <c>STOSD</c> instruction
/// </summary>
/// <param name="addressSize">16, 32, or 64</param>
/// <param name="repPrefix">Rep prefix or <see cref="RepPrefixKind.None"/></param>
struct Instruction* Instruction_CreateStosd(int addressSize, enum RepPrefixKind repPrefix);

/// <summary>
/// Creates an instruction with 3 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register1">op0: Register</param>
/// <param name="register2">op1: Register</param>
/// <param name="memory">op2: Memory operand</param>
struct Instruction* Instruction_Create(enum Code code, enum Register register1, enum Register register2, struct MemoryOperand* memory);

struct Encoder* Encoder_new()
{
	struct Encoder* encoder = (struct Encoder*)malloc(sizeof(struct Encoder));
	encoder->Internal_PreventVEX2 = 0;
	encoder->Internal_VEX_WIG_LIG = 0;
	encoder->Internal_VEX_LIG = 0;
	encoder->Internal_EVEX_WIG = 0;
	encoder->Internal_EVEX_LIG = 0;
	encoder->Internal_MVEX_WIG = 0;
	encoder->bitness = 0;
	encoder->currentRip = 0;
	encoder->errorMessage = NULL;
	encoder->handler = NULL;
	encoder->eip = 0;
	encoder->displAddr = 0;
	encoder->immAddr = 0;
	encoder->Immediate = 0;
	encoder->ImmediateHi = 0;
	encoder->Displ = 0;
	encoder->DisplHi = 0;
	encoder->opSize16Flags = EncoderFlags_None;
	encoder->opSize32Flags = EncoderFlags_None;
	encoder->adrSize16Flags = EncoderFlags_None;
	encoder->adrSize32Flags = EncoderFlags_None;
	encoder->OpCode = 0;
	encoder->EncoderFlags = EncoderFlags_None;
	encoder->DisplSize = 0;
	encoder->ImmSize = 0;
	encoder->ModRM = 0;
	encoder->Sib = 0;
	return encoder;
}

void Encoder_init(struct Encoder* encoder, int bitness)
{
	//Debug.Assert(bitness == 16 || bitness == 32 || bitness == 64);
	//encoder->immSizes = s_immSizes;
	//encoder->writer = writer;
	encoder->bitness = bitness;
	//encoder->handlers = OpCodeHandlers.Handlers;
	encoder->handler = NULL;// It's initialized by TryEncode

	if (bitness != 16)
	{
		encoder->opSize16Flags = EncoderFlags_P66;
	}
	else
	{
		encoder->opSize16Flags = 0;
	}

	if (bitness == 16)
	{
		encoder->opSize32Flags = EncoderFlags_P66;
	}
	else
	{
		encoder->opSize32Flags = 0;
	}

	if (bitness != 16)
	{
		encoder->adrSize16Flags = EncoderFlags_P67;
	}
	else
	{
		encoder->adrSize16Flags = 0;
	}

	if (bitness != 32)
	{
		encoder->adrSize32Flags = EncoderFlags_P67;
	}
	else
	{
		encoder->adrSize32Flags = 0;
	}
}

/// <summary>
/// Gets the bitness (16, 32 or 64)
/// </summary>
int GetBitness(struct Encoder* encoder)
{
	return encoder->bitness;
}

/// <summary>
/// Creates an encoder
/// </summary>
/// <param name="bitness">16, 32 or 64</param>
/// <returns></returns>
struct Encoder* Create(int bitness)
{
	struct Encoder* encoder = NULL;
	switch (bitness)
	{
	case 16:
	case 32:
	case 64:
		encoder = Encoder_new();
		Encoder_init(encoder, bitness);
		break;
	default:
		// throw new ArgumentOutOfRangeException(nameof(bitness))
		break;
	}
	return encoder;
}

bool Verify(int operand, enum OpKind expected, enum OpKind actual)
{
	if (expected == actual)
	{
		return true;
	}
	//ErrorMessage = $"Operand {operand}: Expected: {expected}, actual: {actual}";
	return false;
}

bool VerifyRegisters(int operand, enum Register expected, enum Register actual)
{
	if (expected == actual)
	{
		return true;
	}
	//ErrorMessage = $"Operand {operand}: Expected: {expected}, actual: {actual}";
	return false;
}

bool VerifyRegister(struct Encoder* encoder, int operand, enum Register registerValue, enum Register regLo, enum Register regHi)
{
	if (encoder->bitness != 64 && regHi > regLo + 7)
	{
		regHi = regLo + 7;
	}
	if (regLo <= registerValue && registerValue <= regHi)
	{
		return true;
	}
	//ErrorMessage = $"Operand {operand}: Register {register} is not between {regLo} and {regHi} (inclusive)";
	return false;
}

void Encoder_AddBranch(struct Encoder* encoder, struct Instruction* instruction, enum OpKind opKind, int immSize, int operand);

/// <summary>
/// Encodes an instruction
/// </summary>
/// <param name="instruction">Instruction to encode</param>
/// <param name="rip"><c>RIP</c> of the encoded instruction</param>
/// <param name="encodedLength">Updated with length of encoded instruction if successful</param>
/// <param name="errorMessage">Set to the error message if we couldn't encode the instruction</param>
/// <returns></returns>
bool Encoder_TryEncode(struct Encoder* encoder, struct Instruction* instruction, unsigned long rip, unsigned int* encodedLength);

/// <summary>
/// Encodes an instruction and returns the size of the encoded instruction.
/// A <see cref="EncoderException"/> is thrown if it failed to encode the instruction.
/// </summary>
/// <param name="instruction">Instruction to encode</param>
/// <param name="rip">RIP of the encoded instruction</param>
/// <returns>
/// Encoded length.
/// </returns>
unsigned int Encode(struct Encoder* encoder, struct Instruction* instruction, unsigned long rip)
{
	unsigned int encoded_length = 0;
	if (!Encoder_TryEncode(encoder, instruction, rip, &encoded_length))
	{
		//ThrowEncoderException(instruction, errorMessage);
	}
	return encoded_length;
}

#endif