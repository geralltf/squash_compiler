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

void OpModRM_regF0_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (GetBitness(encoder) != 64 && GetOpKind(instruction, operand) == OK_Register && GetOpRegister(instruction, operand) >= op->regLo + 8 && GetOpRegister(instruction, operand) <= op->regLo + 15)
	{
		encoder->EncoderFlags |= EncoderFlags_PF0;
		AddModRMRegister(encoder, instruction, operand, op->regLo + 8, op->regLo + 15);
	}
	else
	{
		AddModRMRegister(encoder, instruction, operand, op->regLo, op->regHi);
	}
}

void OpReg_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	Verify(encoder, operand, OK_Register, GetOpKind(instruction, operand));
	Verify(encoder, operand, op->_register, GetOpRegister(instruction, operand));
}

void OpRegSTi_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(encoder, operand, OK_Register, GetOpKind(instruction, operand)))
	{
		return;
	}
	enum Register reg = GetOpRegister(instruction, operand);
	if (!Verify(encoder, operand, reg, Register_ST0, Register_ST7))
	{
		return;
	}
	//Debug.Assert((encoder.OpCode & 7) == 0);
	encoder->OpCode |= (unsigned int)(reg - Register_ST0);
}

void OpIb_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	switch (encoder->ImmSize) 
	{
	case ImmSize_Size1:
		if (!Verify(operand, OK_Immediate8_2nd, GetOpKind(instruction, operand)))
		{
			return;
		}
		encoder->ImmSize = ImmSize_Size1_1;
		encoder->ImmediateHi = GetImmediate8_2nd(instruction);
		break;
	case ImmSize_Size2:
		if (!Verify(operand, OK_Immediate8_2nd, GetOpKind(instruction, operand)))
		{
			return;
		}
		encoder->ImmSize = ImmSize_Size2_1;
		encoder->ImmediateHi = GetImmediate8_2nd(instruction);
		break;
	default:
		enum OpCodeOperandKind opImmKind = GetOpKind(instruction, operand);
		if (!Verify(encoder, operand, op->opKind, opImmKind))
		{
			return;
		}
		encoder->ImmSize = ImmSize_Size1;
		encoder->Immediate = GetImmediate8(instruction);
		break;
	}
}

void OpImm_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(encoder, operand, OK_Immediate8, GetOpKind(instruction, operand)))
	{
		return;
	}
	if (GetImmediate8(instruction) != op->value)
	{
		//encoder.ErrorMessage = $"Operand {operand}: Expected 0x{value:X2}, actual: 0x{instruction.Immediate8:X2}";
		return;
	}
}

enum OpKind OpImm_GetImmediateOpKind(struct Op* op)
{
	return OK_Immediate8;
}

void OpIw_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(encoder, operand, OK_Immediate16, GetOpKind(instruction, operand)))
	{
		return;
	}
	encoder->ImmSize = ImmSize_Size2;
	encoder->Immediate = GetImmediate16(instruction);
}

enum OpKind OpIw_GetImmediateOpKind(struct Op* op)
{
	return OK_Immediate16;
}

void OpId_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	enum OpKind opImmKind = GetOpKind(instruction, operand);
	if (!Verify(encoder, operand, op->opKind, opImmKind))
	{
		return;
	}
	encoder->ImmSize = ImmSize_Size4;
	encoder->Immediate = GetImmediate32(instruction);
}

enum OpKind OpId_GetImmediateOpKind(struct Op* op)
{
	return op->opKind;
}

void OpIq_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(encoder, operand, OK_Immediate64, GetOpKind(instruction, operand)))
	{
		return;
	}
	encoder->ImmSize = ImmSize_Size8;
	unsigned long imm = GetImmediate64(instruction);
	encoder->Immediate = (unsigned int)imm;
	encoder->ImmediateHi = (unsigned int)(imm >> 32);
}

enum OpKind OpIq_GetImmediateOpKind(struct Op* op)
{
	return OK_Immediate64;
}
 
int GetXRegSize(enum OpKind opKind) 
{
	if (opKind == OK_MemorySegRSI)
	{
		return 8;
	}
	if (opKind == OK_MemorySegESI)
	{
		return 4;
	}
	if (opKind == OK_MemorySegSI)
	{
		return 2;
	}
	return 0;
}

int GetYRegSize(enum OpKind opKind) 
{
	if (opKind == OK_MemoryESRDI)
	{
		return 8;
	}
	if (opKind == OK_MemoryESEDI)
	{
		return 4;
	}
	if (opKind == OK_MemoryESDI)
	{
		return 2;
	}
	return 0;
}

void OpX_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	int regXSize = GetXRegSize(GetOpKind(instruction, operand));
	if (regXSize == 0) {
		//encoder.ErrorMessage = $"Operand {operand}: expected OpKind = {nameof(OpKind.MemorySegSI)}, {nameof(OpKind.MemorySegESI)} or {nameof(OpKind.MemorySegRSI)}";
		return;
	}
	switch (GetCode(instruction))
	{
	case Movsb_m8_m8:
	case Movsw_m16_m16:
	case Movsd_m32_m32:
	case Movsq_m64_m64:
		int regYSize = GetYRegSize(GetOp0Kind(instruction));
		if (regXSize != regYSize) 
		{
			//encoder.ErrorMessage = $"Same sized register must be used: reg #1 size = {regYSize * 8}, reg #2 size = {regXSize * 8}";
			return;
		}
		break;
	}
	SetAddrSize(encoder, regXSize);
}

void OpY_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	int regYSize = GetYRegSize(GetOpKind(instruction, operand));
	if (regYSize == 0) {
		//encoder.ErrorMessage = $"Operand {operand}: expected OpKind = {nameof(OpKind.MemoryESDI)}, {nameof(OpKind.MemoryESEDI)} or {nameof(OpKind.MemoryESRDI)}";
		return;
	}
	switch (GetCode(instruction)) 
	{
	case Cmpsb_m8_m8:
	case Cmpsw_m16_m16:
	case Cmpsd_m32_m32:
	case Cmpsq_m64_m64:
		int regXSize = GetXRegSize(GetOp0Kind(instruction));
		if (regXSize != regYSize) 
		{
			//encoder.ErrorMessage = $"Same sized register must be used: reg #1 size = {regXSize * 8}, reg #2 size = {regYSize * 8}";
			return;
		}
		break;
	}
	SetAddrSize(encoder, regYSize);
}

int GetRegSize(enum OpKind opKind) 
{
	if (opKind == OK_MemorySegRDI)
	{
		return 8;
	}
	if (opKind == OK_MemorySegEDI)
	{
		return 4;
	}
	if (opKind == OK_MemorySegDI)
	{
		return 2;
	}
	return 0;
}

void OprDI_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	int regSize = GetRegSize(GetOpKind(instruction, operand));
	if (regSize == 0) {
		//encoder.ErrorMessage = $"Operand {operand}: expected OpKind = {nameof(OpKind.MemorySegDI)}, {nameof(OpKind.MemorySegEDI)} or {nameof(OpKind.MemorySegRDI)}";
		return;
	}
	SetAddrSize(encoder, regSize);
}

void OpMRBX_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(encoder, operand, OK_Memory, GetOpKind(instruction, operand)))
	{
		return;
	}
	enum Register baseReg = GetMemoryBase(instruction);
	if (GetMemoryDisplSize(instruction) != 0 || GetMemoryDisplacement64(instruction) != 0 ||
		GetMemoryIndexScale(instruction) != 1 || GetMemoryIndex(instruction) != Register_AL ||
		(baseReg != Register_BX && baseReg != Register_EBX && baseReg != Register_RBX)) 
	{
		//encoder.ErrorMessage = $"Operand {operand}: Operand must be [bx+al], [ebx+al], or [rbx+al]";
		return;
	}
	int regSize;
	if (baseReg == Register_RBX)
	{
		regSize = 8;
	}
	else if (baseReg == Register_EBX)
	{
		regSize = 4;
	}
	else 
	{
		//Debug.Assert(baseReg == Register_BX);
		regSize = 2;
	}
	SetAddrSize(encoder, regSize);
}

void OpJ_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddBranch(encoder, op->opKind, op->immSize, instruction, operand);
}

enum OpKind OpJ_GetNearBranchOpKind(struct Op* op)
{
	return op->opKind;
}

void OpJx_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddBranchX(encoder, op->immSize, instruction, operand);
}

enum OpKind OpJx_GetNearBranchOpKind(struct Op* op)
{
	// xbegin is special and doesn't mask the target IP. We need to know the code size to return the correct value.
	// Instruction.CreateXbegin() should be used to create the instruction and this method should never be called.
	//Debug.Fail("Call Instruction.CreateXbegin()");
	return OpDefault_GetNearBranchOpKind(op);
}

void OpJdisp_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	AddBranchDisp(encoder, op->displSize, instruction, operand);
}

enum OpKind OpJdisp_GetNearBranchOpKind(struct Op* op)
{
	if (op->displSize == 2)
	{
		return OK_NearBranch16;
	}
	else
	{
		return OK_NearBranch32;
	}
}

struct Op* Op_new()
{
	struct Op* op = (struct Op*)malloc(sizeof(struct Op));
	op->operand_type = OT_UNDEFINED;
	op->size = 0;
	op->mustUseSib = false;
	op->regLo = (enum Register)0;
	op->regHi = (enum Register)0;
	op->_register = (enum Register)0;
	op->immSize = 0;
	op->displSize = 0;
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

struct Op* OpModRM_regF0_new(enum Register regLo, enum Register regHi)
{
	struct Op* op = Op_new();
	op->regLo = regLo;
	op->regHi = regHi;
	op->operand_type = OT_OpModRM_regF0;
	op->Encode = &OpModRM_regF0_Encode;

	return op;
}

struct Op* OpReg_new(enum Register _register)
{
	struct Op* op = Op_new();
	op->_register = _register;
	op->operand_type = OT_OpReg;
	op->Encode = &OpReg_Encode;

	return op;
}

struct Op* OpRegSTi_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpRegSTi;
	op->Encode = &OpRegSTi_Encode;

	return op;
}

struct Op* OpIb_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpIb;
	op->Encode = &OpIb_Encode;

	return op;
}

struct Op* OpImm_new(unsigned char value)
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpImm;
	op->value = value;
	op->Encode = &OpImm_Encode;
	op->GetImmediateOpKind = &OpImm_GetImmediateOpKind;
	return op;
}

struct Op* OpIw_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpIw;
	op->Encode = &OpIw_Encode;
	op->GetImmediateOpKind = &OpIw_GetImmediateOpKind;
	return op;
}

struct Op* OpId_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpId;
	op->Encode = &OpId_Encode;
	op->GetImmediateOpKind = &OpId_GetImmediateOpKind;
	return op;
}

struct Op* OpIq_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpIq;
	op->Encode = &OpIq_Encode;
	op->GetImmediateOpKind = &OpIq_GetImmediateOpKind;
	return op;
}

struct Op* OpX_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpX;
	op->Encode = &OpX_Encode;
	return op;
}

struct Op* OpY_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpY;
	op->Encode = &OpY_Encode;
	return op;
}

struct Op* OprDI_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OprDI;
	op->Encode = &OprDI_Encode;
	return op;
}

struct Op* OpMRBX_new()
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpMRBX;
	op->Encode = &OpMRBX_Encode;
	return op;
}

struct Op* OpJ_new(enum OpKind opKind, int immSize)
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpJ;
	op->opKind = opKind;
	op->immSize = immSize;
	op->Encode = &OpJ_Encode;
	op->GetNearBranchOpKind = &OpJ_GetNearBranchOpKind;
	return op;
}

struct Op* OpJx_new(int immSize)
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpJx;
	op->immSize = immSize;
	op->Encode = &OpJx_Encode;
	op->GetNearBranchOpKind = &OpJx_GetNearBranchOpKind;
	return op;
}

struct Op* OpJdisp_new(int displSize)
{
	struct Op* op = Op_new();
	op->operand_type = OT_OpJdisp;
	op->displSize = displSize;
	op->Encode = &OpJdisp_Encode;
	op->GetNearBranchOpKind = &OpJdisp_GetNearBranchOpKind;
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
	operands[27] = *OpModRM_rm_reg_only_new(Register_RAX, Register_R15);
	operands[28] = *OpRegEmbed8_new(Register_RAX, Register_R15);
	operands[29] = *OpModRM_reg_new(Register_ES, Register_GS);
	operands[30] = *OpModRM_reg_new(Register_MM0, Register_MM7);
	operands[31] = *OpModRM_rm_reg_only_new(Register_MM0, Register_MM7);
	operands[32] = *OpModRM_reg_new(Register_XMM0, Register_XMM15);
	operands[33] = *OpModRM_rm_reg_only_new(Register_XMM0, Register_XMM15);
	operands[34] = *OpModRM_regF0_new(Register_CR0, Register_CR15);
	operands[35] = *OpModRM_reg_new(Register_DR0, Register_DR15);
	operands[36] = *OpModRM_reg_new(Register_TR0, Register_TR7);
	operands[37] = *OpModRM_reg_new(Register_BND0, Register_BND3);
	operands[38] = *OpReg_new(Register_ES);
	operands[39] = *OpReg_new(Register_CS);
	operands[40] = *OpReg_new(Register_SS);
	operands[41] = *OpReg_new(Register_DS);
	operands[42] = *OpReg_new(Register_FS);
	operands[43] = *OpReg_new(Register_GS);
	operands[44] = *OpReg_new(Register_AL);
	operands[45] = *OpReg_new(Register_CL);
	operands[46] = *OpReg_new(Register_AX);
	operands[47] = *OpReg_new(Register_DX);
	operands[48] = *OpReg_new(Register_EAX);
	operands[49] = *OpReg_new(Register_RAX);
	operands[50] = *OpReg_new(Register_ST0);
	operands[51] = *OpRegSTi_new();
	operands[52] = *OpIb_new(OK_Immediate8);
	operands[53] = *OpImm_new(1);
	operands[54] = *OpIb_new(OK_Immediate8to16);
	operands[55] = *OpIb_new(OK_Immediate8to32);
	operands[56] = *OpIb_new(OK_Immediate8to64);
	operands[57] = *OpIw_new();
	operands[58] = *OpId_new(OK_Immediate32);
	operands[59] = *OpId_new(OK_Immediate32to64);
	operands[60] = *OpIq_new();
	operands[61] = *OpX_new();
	operands[62] = *OpY_new();
	operands[63] = *OprDI_new();
	operands[64] = *OpMRBX_new();
	operands[65] = *OpJ_new(OK_NearBranch16, 1);
	operands[66] = *OpJ_new(OK_NearBranch32, 1);
	operands[67] = *OpJ_new(OK_NearBranch64, 1);
	operands[68] = *OpJ_new(OK_NearBranch16, 2);
	operands[69] = *OpJ_new(OK_NearBranch32, 4);
	operands[70] = *OpJ_new(OK_NearBranch64, 4);
	operands[71] = *OpJx_new(2);
	operands[72] = *OpJx_new(4);
	operands[73] = *OpJdisp_new(2);
	operands[74] = *OpJdisp_new(4);

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

//void Encoder_WritePrefixes(struct Encoder* encoder, struct Instruction* instruction, bool canWriteF3)
//{
//	//TODO:
//}

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

enum CodeSize
{
	/// <summary>Unknown size</summary>
	CodeSize_Unknown = 0,
	/// <summary>16-bit code</summary>
	CodeSize_Code16 = 1,
	/// <summary>32-bit code</summary>
	CodeSize_Code32 = 2,
	/// <summary>64-bit code</summary>
	CodeSize_Code64 = 3,
};

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

bool Verify(int operand, enum Register expected, enum Register actual)
{
	if (expected == actual)
	{
		return true;
	}
	//ErrorMessage = $"Operand {operand}: Expected: {expected}, actual: {actual}";
	return false;
}

bool Verify(struct Encoder* encoder, int operand, enum Register registerValue, enum Register regLo, enum Register regHi)
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
bool TryEncode(struct Encoder* encoder, struct Instruction* instruction, unsigned long rip, unsigned int* encodedLength);

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
	if (!TryEncode(encoder, instruction, rip, &encoded_length))
	{
		//ThrowEncoderException(instruction, errorMessage);
	}
	return encoded_length;
}

#endif