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
	//unsigned int lastByte;

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

void Encoder_WriteByteInternal(struct Encoder* encoder, unsigned char byte_value);

void Encoder_WritePrefixes(struct Encoder* encoder, struct Instruction* instruction, bool canWriteF3);

void Encoder_AddAbsMem(struct Encoder* encoder, struct Instruction* instruction, int operand);

void Encoder_SetAddrSize(struct Encoder* encoder, int regSize);

void Encoder_AddRegOrMemAll(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi, enum Register vsibIndexRegLo, enum Register vsibIndexRegHi, bool allowMemOp, bool allowRegOp);

void Encoder_AddRegOrMem(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi, bool allowMemOp, bool allowRegOp);

void Encoder_AddReg(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi);

void Encoder_AddModRMRegister(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi);

void Encoder_AddMemOp16(struct Encoder* encoder, struct Instruction* instruction, int operand);

void Encoder_AddMemOp(struct Encoder* encoder, struct Instruction* instruction, int operand, int addrSize, enum Register vsibIndexRegLo, enum Register vsibIndexRegHi);

void Encoder_AddFarBranch(struct Encoder* encoder, struct Instruction* instruction, int operand, int size);

void Encoder_AddBranchX(struct Encoder* encoder, int immSize, struct Instruction* instruction, int operand);

void Encoder_AddBranchDisp(struct Encoder* encoder, int displSize, struct Instruction* instruction, int operand);

unsigned int OpCodeHandler_GetOpCode(struct OpCodeHandler* self, enum EncFlags2 encFlags2);

void InvalidHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

void DeclareDataHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

void ZeroBytesHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

void LegacyHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

void VEXHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

void EVEXHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

void XopHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

bool MvexHandler_TryConvertToDisp8N(struct Encoder* encoder, struct OpCodeHandler* handler, struct Instruction* instruction, int displ, signed char* compressedValue);

void MvexHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);

void D3nowHandler_Encode(struct OpCodeHandler* self, struct Encoder* encoder, struct Instruction* instruction);


unsigned int TupleTypeTable_GetDisp8N(enum TupleType tupleType, bool bcst);

bool EVEXHandler_TryConvertToDisp8N(struct Encoder* encoder, struct OpCodeHandler* handler, struct Instruction* instruction, int displ, signed char* compressedValue);

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

struct Encoder* Encoder_new();

void Encoder_init(struct Encoder* encoder, int bitness);

/// <summary>
/// Gets the bitness (16, 32 or 64)
/// </summary>
int GetBitness(struct Encoder* encoder);

/// <summary>
/// Creates an encoder
/// </summary>
/// <param name="bitness">16, 32 or 64</param>
/// <returns></returns>
struct Encoder* Create(int bitness);

bool Verify(int operand, enum OpKind expected, enum OpKind actual);

bool VerifyRegisters(int operand, enum Register expected, enum Register actual);

bool VerifyRegister(struct Encoder* encoder, int operand, enum Register registerValue, enum Register regLo, enum Register regHi);

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
unsigned int Encode(struct Encoder* encoder, struct Instruction* instruction, unsigned long rip);

#endif