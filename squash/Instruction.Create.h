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
	if (encoder->Bitness != 64 && GetOpKind(instruction, operand) == OK_Register && GetOpRegister(instruction, operand) >= op->regLo + 8 && GetOpRegister(instruction, operand) <= op->regLo + 15)
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
		if (!encoder.Verify(operand, OK_Immediate8_2nd, GetOpKind(instruction, operand)))
		{
			return;
		}
		encoder.ImmSize = ImmSize_Size1_1;
		encoder.ImmediateHi = instruction->Immediate8_2nd;
		break;
	case ImmSize_Size2:
		if (!encoder.Verify(operand, OK_Immediate8_2nd, GetOpKind(instruction, operand)))
		{
			return;
		}
		encoder.ImmSize = ImmSize.Size2_1;
		encoder.ImmediateHi = instruction.Immediate8_2nd;
		break;
	default:
		var opImmKind = GetOpKind(instruction, operand);
		if (!Verify(encoder, operand, opKind, opImmKind))
		{
			return;
		}
		encoder.ImmSize = ImmSize_Size1;
		encoder.Immediate = instruction->Immediate8;
		break;
	}
}

void OpImm_Encode(struct Encoder* encoder, struct Instruction* instruction, int operand, struct Op* op)
{
	if (!Verify(encoder, operand, OK_Immediate8, GetOpKind(instruction, operand)))
	{
		return;
	}
	if (instruction.Immediate8 != op->value) 
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
	encoder.ImmSize = ImmSize_Size2;
	encoder.Immediate = instruction.Immediate16;
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
	encoder.ImmSize = ImmSize_Size4;
	encoder.Immediate = instruction.Immediate32;
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
	encoder.ImmSize = ImmSize_Size8;
	unsigned long imm = instruction.Immediate64;
	encoder.Immediate = (unsigned int)imm;
	encoder.ImmediateHi = (unsigned int)(imm >> 32);
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
	enum Register baseReg = instruction.MemoryBase;
	if (instruction.MemoryDisplSize != 0 || instruction.MemoryDisplacement64 != 0 ||
		instruction.MemoryIndexScale != 1 || instruction.MemoryIndex != Register_AL ||
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
	encoder.SetAddrSize(regSize);
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