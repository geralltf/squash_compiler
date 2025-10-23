#ifndef INSTRUCTION_CREATE
#define INSTRUCTION_CREATE

#include "OpCodeInfo.h"
#include "Instruction.h"
#include "OpCodeInfoData.h"

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
};

struct Op 
{
	enum OperandType operand_type;

	int size;					// OpA
	bool mustUseSib;			// OpModRM_rm_mem_only
	enum Register regLo;		// OpModRM_rm
	enum Register regHi;		// OpModRM_rm

	void (*Encode)(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op);
	enum OpKind(*GetImmediateOpKind)(struct Op* op);
	enum OpKind(*GetNearBranchOpKind)(struct Op* op);
	enum OpKind(*GetFarBranchOpKind)(struct Op* op);
};

enum OpKind OpDefault_GetImmediateOpKind(struct Op* op)
{
	return (enum OpKind)(-1);
}

enum OpKind OpDefault_GetNearBranchOpKind(struct Op* op)
{
	return (enum OpKind)(-1);
}

enum OpKind OpDefault_GetFarBranchOpKind(struct Op* op)
{
	return (enum OpKind)(-1);
}

void OpA_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddFarBranch(encoder, instruction, operand, op->size);
}
enum OpKind OpA_GetFarBranchOpKind(struct Op* op)
{
	//Debug.Assert(size == 2 || size == 4);
	return op->size == 2 ? OK_FarBranch16 : OK_FarBranch32;
}

void OpO_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddAbsMem(encoder, instruction, operand);
}

void OpModRM_rm_mem_only_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (op->mustUseSib)
	{
		encoder->EncoderFlags |= EncoderFlags_MustUseSib;
	}

	AddRegOrMem(encoder, instruction, operand, Register_None, Register_None, true, false); // allowMemOp: true, allowRegOp : false
}

void OpModRM_rm_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddRegOrMem(encoder, instruction, operand, op->regLo, op->regHi, true, true); // allowMemOp: true, allowRegOp : true
}

void OpModRM_reg_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddModRMRegister(encoder, instruction, operand, op->regLo, op->regHi);
}

void OpRegEmbed8_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddReg(encoder, instruction, operand, op->regLo, op->regHi);
}

void OpModRM_reg_mem_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddModRMRegister(encoder, instruction, operand, op->regLo, op->regHi);
	encoder->EncoderFlags |= EncoderFlags_RegIsMemory;
}

void OpModRM_rm_reg_only_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddRegOrMem(encoder, instruction, operand, op->regLo, op->regHi, false, true); // allowMemOp: false, allowRegOp : true
}

struct Op* Op_new()
{
	struct Op* op = (struct Op*)malloc(sizeof(struct Op));
	op->operand_type = OT_UNDEFINED;
	op->size = 0;
	op->mustUseSib = false;
	op->regLo = (enum Register)0;
	op->regHi = (enum Register)0;
	op->Encode = NULL;
	op->GetImmediateOpKind = &OpDefault_GetImmediateOpKind;
	op->GetNearBranchOpKind = &OpDefault_GetNearBranchOpKind;
	op->GetFarBranchOpKind = &OpDefault_GetFarBranchOpKind;
	return op;
}

struct Op* OpA_new(int size)
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpA;
	op->size = size;
	op->Encode = &OpA_Encode;
	op->GetFarBranchOpKind = &OpA_GetFarBranchOpKind;
	return op;
}

struct Op* OpO_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpO;
	op->Encode = &OpO_Encode;
	
	return op;
}

struct Op* OpModRM_rm_mem_only_new(bool mustUseSib)
{
	struct Op* op = Op_new();
	op->mustUseSib = mustUseSib;
	op->operand_type = OT_OpModRM_rm_mem_only;
	op->Encode = &OpModRM_rm_mem_only_Encode;

	return op;
}

struct Op* OpModRM_rm_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpModRM_rm;
	op->Encode = &OpModRM_rm_Encode;

	return op;
}

struct Op* OpModRM_reg_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpModRM_reg;
	op->Encode = &OpModRM_reg_Encode;

	return op;
}

struct Op* OpRegEmbed8_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpRegEmbed8;
	op->Encode = &OpRegEmbed8_Encode;

	return op;
}

struct Op* OpModRM_reg_mem_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpModRM_reg_mem;
	op->Encode = &OpModRM_reg_mem_Encode;

	return op;
}

struct Op* OpModRM_rm_reg_only_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpModRM_rm_reg_only;
	op->Encode = &OpModRM_rm_reg_only_Encode;

	return op;
}

// Op Tables.
struct Op* Operands_LegacyOps()
{
	struct Op* operands = (struct Op*)malloc(sizeof(struct Op) * 75);
	operands[0] = *OpA_new(2);
	operands[1] = *OpA_new(4);
	operands[2] = *OpO_new();
	operands[3] = *OpModRM_rm_mem_only_new(false);
	operands[4] = *OpModRM_rm_mem_only_new(false);
	operands[5] = *OpModRM_rm_mem_only_new(false);
	operands[6] = *OpModRM_rm_new(Register_AL, Register_R15L);
	operands[7] = *OpModRM_rm_new(Register_AX, Register_R15W);
	operands[8] = *OpModRM_rm_new(Register_EAX, Register_R15D);
	operands[9] = *OpModRM_rm_new(Register_EAX, Register_R15D);
	operands[10] = *OpModRM_rm_new(Register_RAX, Register_R15);
	operands[11] = *OpModRM_rm_new(Register_RAX, Register_R15);
	operands[12] = *OpModRM_rm_new(Register_MM0, Register_MM7);
	operands[13] = *OpModRM_rm_new(Register_XMM0, Register_XMM15);
	operands[14] = *OpModRM_rm_new(Register_BND0, Register_BND3);
	operands[15] = *OpModRM_reg_new(Register_AL, Register_R15L);
	operands[16] = *OpRegEmbed8_new(Register_AL, Register_R15L);
	operands[17] = *OpModRM_reg_new(Register_AX, Register_R15W);
	operands[18] = *OpModRM_reg_mem_new(Register_AX, Register_R15W);
	operands[19] = *OpModRM_rm_reg_only_new(Register_AX, Register_R15W);
	operands[20] = *OpRegEmbed8_new(Register_AX, Register_R15W);
	operands[21] = *OpModRM_reg_new(Register_EAX, Register_R15D);
	operands[22] = *OpModRM_reg_mem_new(Register_EAX, Register_R15D);
	operands[23] = *OpModRM_rm_reg_only_new(Register_EAX, Register_R15D);
	operands[24] = *OpRegEmbed8_new(Register_EAX, Register_R15D);
	operands[25] = *OpModRM_reg_new(Register_RAX, Register_R15);
	operands[26] = *OpModRM_reg_mem_new(Register_RAX, Register_R15);
	return operands;
}

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
	//TryConvertToDisp8N ? TryConvertToDisp8N;
	struct Op* Operands;
	unsigned int Operands_Length;

	unsigned int (*GetOpCode)(struct OpCodeHandler* self, enum EncFlags2 encFlags2);
	void (*Encode)(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

	// DeclareDataHandler
	int elemLength;
	int maxLength;

	// LegacyHandler
	unsigned int tableByte1;
	unsigned int tableByte2;
	unsigned int mandatoryPrefix;
};

struct InvalidHandler
{
	struct OpCodeHandler* base;
};

struct DeclareDataHandler
{
	struct OpCodeHandler* base;

	int elemLength;
	int maxLength;
};

struct ZeroBytesHandler
{
	struct OpCodeHandler* base;
};

enum EncodingKind GetEncodingKindByOpcode(enum Code opcode);

void OpCodeHandler_init(struct OpCodeHandler** o, 
	enum EncFlags2 encFlags2, 
	enum EncFlags3 encFlags3, 
	bool isSpecialInstr, 
	/*TryConvertToDisp8N ? tryConvertToDisp8N, */ 
	struct Op* operands,
	unsigned int operands_length,
	unsigned int (*GetOpCode)(struct OpCodeHandler* self, enum EncFlags2 encFlags2),
	void (*Encode)(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction))
{
	(*o)->EncFlags2 = encFlags2;
	(*o)->EncFlags3 = encFlags3;
	(*o)->IsSpecialInstr = isSpecialInstr;
	(*o)->Operands = operands;
	(*o)->Operands_Length = operands_length;
	(*o)->GetOpCode = GetOpCode;
	(*o)->Encode = Encode;
}

void Encoder_WritePrefixes(struct Encoder* encoder, struct Instruction* instruction, bool canWriteF3)
{
	//TODO:
}

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

/// <summary>
/// Creates a new far branch instruction
/// </summary>
/// <param name="code">Code value</param>
/// <param name="selector">Selector/segment value</param>
/// <param name="offset">Offset</param>
struct Instruction* CreateBranch(enum Code code, unsigned short selector, unsigned int offset);

#endif