#include "Instruction.Create.h"

struct OpCodeHandler* EncoderInternal_OpCodeHandlers_Handlers = (struct OpCodeHandler*)NULL; // CodeEnumCount
struct Op* ops_legacy = NULL;
struct Op* ops_vex = NULL;

struct OpCodeHandler* GetOpCodeHandlers()
{
	if (EncoderInternal_OpCodeHandlers_Handlers == NULL)
	{
		OpCodeHandlers_init();
	}

	return EncoderInternal_OpCodeHandlers_Handlers;
}

struct OpCodeHandler* GetOpCodeHandler(enum Code opcode)
{
	int index = (int)opcode;

	struct OpCodeHandler* handlers = GetOpCodeHandlers();

	struct OpCodeHandler handler =  handlers[index];

	return &handler;
}

enum EncodingKind GetEncodingKindByOpcode(enum Code opcode)
{
	int i = (int)opcode;

	enum EncFlags3 encFlags3 = (enum EncFlags3)EncoderData_EncFlags3[i];

	enum EncodingKind ekind = (enum EncodingKind)(((unsigned int)encFlags3 >> (int)EFLAGS3_EncodingShift) & (unsigned int)EFLAGS3_EncodingMask);

	return ekind;
}

struct Op* LegacyHandler_CreateOps(enum EncFlags1 encFlags1, int* operands_length) 
{
	if (ops_legacy == NULL)
	{
		ops_legacy = Operands_LegacyOps();
	}

	int op0 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op0Shift) & (unsigned int)EFLAGS_Legacy_OpMask);
	int op1 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op1Shift) & (unsigned int)EFLAGS_Legacy_OpMask);
	int op2 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op2Shift) & (unsigned int)EFLAGS_Legacy_OpMask);
	int op3 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_Legacy_Op3Shift) & (unsigned int)EFLAGS_Legacy_OpMask);
	if (op3 != 0) 
	{
		//Debug.Assert(op0 != 0 && op1 != 0 && op2 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 4);
		w[0] = ops_legacy[op0 - 1];
		w[1] = ops_legacy[op1 - 1];
		w[2] = ops_legacy[op2 - 1];
		w[3] = ops_legacy[op3 - 1];
		*operands_length = 4;
		return w;
	}
	if (op2 != 0) 
	{
		//Debug.Assert(op0 != 0 && op1 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 3);
		w[0] = ops_legacy[op0 - 1];
		w[1] = ops_legacy[op1 - 1];
		w[2] = ops_legacy[op2 - 1];
		*operands_length = 3;
		return w;
	}
	if (op1 != 0) 
	{
		//Debug.Assert(op0 != 0);
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 2);
		w[0] = ops_legacy[op0 - 1];
		w[1] = ops_legacy[op1 - 1];
		*operands_length = 2;
		return w;
	}
	if (op0 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 1);
		w[0] = ops_legacy[op0 - 1];
		*operands_length = 1;
		return w;
	}
		
	return NULL;
}

struct Op* VEXHandler_CreateOps(enum EncFlags1 encFlags1, int* operands_length)
{
	if (ops_vex == NULL)
	{
		ops_vex = Operands_VexOps();
	}

	int op0 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op0Shift) & (unsigned int)EFLAGS_VEX_OpMask);
	int op1 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op1Shift) & (unsigned int)EFLAGS_VEX_OpMask);
	int op2 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op2Shift) & (unsigned int)EFLAGS_VEX_OpMask);
	int op3 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op3Shift) & (unsigned int)EFLAGS_VEX_OpMask);
	int op4 = (int)(((unsigned int)encFlags1 >> (int)EFLAGS_VEX_Op4Shift) & (unsigned int)EFLAGS_VEX_OpMask);
	if (op4 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 5);
		w[0] = ops_vex[op0 - 1];
		w[1] = ops_vex[op1 - 1];
		w[2] = ops_vex[op2 - 1];
		w[3] = ops_vex[op3 - 1];
		w[4] = ops_vex[op4 - 1];
		*operands_length = 5;
		return w;
		//Debug.Assert(op0 != 0 && op1 != 0 && op2 != 0 && op3 != 0);
		//return new Op[]{ OpHandlerData.ops_vex[op0 - 1], OpHandlerData.ops_vex[op1 - 1], OpHandlerData.ops_vex[op2 - 1], OpHandlerData.ops_vex[op3 - 1], OpHandlerData.ops_vex[op4 - 1] };
	}
	if (op3 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 4);
		w[0] = ops_vex[op0 - 1];
		w[1] = ops_vex[op1 - 1];
		w[2] = ops_vex[op2 - 1];
		w[3] = ops_vex[op3 - 1];
		*operands_length = 4;
		return w;
		//Debug.Assert(op0 != 0 && op1 != 0 && op2 != 0);
		//return new Op[]{ OpHandlerData.ops_vex[op0 - 1], OpHandlerData.ops_vex[op1 - 1], OpHandlerData.ops_vex[op2 - 1], OpHandlerData.ops_vex[op3 - 1] };
	}
	if (op2 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 3);
		w[0] = ops_vex[op0 - 1];
		w[1] = ops_vex[op1 - 1];
		w[2] = ops_vex[op2 - 1];
		*operands_length = 3;
		return w;
		//Debug.Assert(op0 != 0 && op1 != 0);
		//return new Op[]{ OpHandlerData.ops_vex[op0 - 1], OpHandlerData.ops_vex[op1 - 1], OpHandlerData.ops_vex[op2 - 1] };
	}
	if (op1 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 2);
		w[0] = ops_vex[op0 - 1];
		w[1] = ops_vex[op1 - 1];
		*operands_length = 2;
		return w;
		//Debug.Assert(op0 != 0);
		//return new Op[]{ OpHandlerData.ops_vex[op0 - 1], OpHandlerData.ops_vex[op1 - 1] };
	}
	if (op0 != 0)
	{
		struct Op* w = (struct Op*)malloc(sizeof(struct Op) * 1);
		w[0] = ops_vex[op0 - 1];
		*operands_length = 1;
		return w;
		//return new Op[]{ OpHandlerData.VexOps[op0 - 1] };
	}
	return NULL;
}

void OpCodeHandlers_init()
{
	enum Code code;
	enum EncodingKind ekind;
	struct OpCodeHandler* handler;
	int i;
	enum LegacyOpCodeTable tbl_type;
	unsigned int encFlags1;
	unsigned int encFlags2;
	unsigned int encFlags3Data;
	enum MandatoryPrefixByte mpb_value;
	unsigned int operands_length = 0;
	struct Op* operands = NULL;

	//auto handlers = new OpCodeHandler[IcedConstants.CodeEnumCount];
	
	EncoderInternal_OpCodeHandlers_Handlers = (struct OpCodeHandler*)malloc(sizeof(struct OpCodeHandler) * CodeEnumCount);

	for (i = 0; i < CodeEnumCount; i++) // 4936.
	{
		encFlags1 = EncoderData_EncFlags1[i];
		encFlags2 = EncoderData_EncFlags2[i];
		encFlags3Data = EncoderData_EncFlags3[i];
		code = (enum Code)i;
		handler = (struct OpCodeHandler*)malloc(sizeof(struct OpCodeHandler));
		ekind = GetEncodingKindByOpcode(i);

		handler->GetOpCode = &OpCodeHandler_GetOpCode;

		switch (ekind)
		{
		case EncodingKind_Legacy:
			
			handler->Operands_Length = 75;

			if (code == INVALID)
			{
				OpCodeHandler_init(&handler, EFLAGS2_None, EFLAGS3_Bit16or32 | EFLAGS3_Bit64, false, NULL, 0, NULL, &OpCodeHandler_GetOpCode, &InvalidHandler_Encode);

				handler->handler_conf = InvalidHandler;
				handler->Encode = InvalidHandler_Encode;
				//handler = invalidHandler;
			}
			else if (code <= DeclareQword)
			{
				OpCodeHandler_init(&handler, EFLAGS2_None, EFLAGS3_Bit16or32 | EFLAGS3_Bit64, true, NULL, 0, NULL, &OpCodeHandler_GetOpCode, &DeclareDataHandler_Encode);

				handler->handler_conf = DeclareDataHandler;
				handler->Encode = DeclareDataHandler_Encode;
				//handler = new DeclareDataHandler(code);
			}
			else if (code == Zero_bytes)
			{
				OpCodeHandler_init(&handler, EFLAGS2_None, EFLAGS3_Bit16or32 | EFLAGS3_Bit64, true, NULL, 0, NULL, &OpCodeHandler_GetOpCode, &ZeroBytesHandler_Encode);

				handler->handler_conf = ZeroBytesHandler;
				//handler = new ZeroBytesHandler(code);
			}
			else
			{
				tbl_type = (enum LegacyOpCodeTable)(((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask);

				switch (tbl_type)
				{
				case LegacyOpCodeTable_MAP0:
					handler->tableByte1 = 0;
					handler->tableByte2 = 0;
					break;
				case LegacyOpCodeTable_MAP0F:
					handler->tableByte1 = 0x0F;
					handler->tableByte2 = 0;
					break;
				case LegacyOpCodeTable_MAP0F38:
					handler->tableByte1 = 0x0F;
					handler->tableByte2 = 0x38;
					break;
				case LegacyOpCodeTable_MAP0F3A:
					handler->tableByte1 = 0x0F;
					handler->tableByte2 = 0x3A;
					break;
				default:
					//throw new InvalidOperationException();
					break;
				}

				mpb_value = (enum MandatoryPrefixByte)(((unsigned int)encFlags2 >> (int)EFLAGS2_MandatoryPrefixShift) & (unsigned int)EFLAGS2_MandatoryPrefixMask);
				
				switch (mpb_value)
				{
				case MPB_None:
					handler->mandatoryPrefix = 0x00;
					break;
				case MPB_P66:
					handler->mandatoryPrefix = 0x66;
					break;
				case MPB_PF3:
					handler->mandatoryPrefix = 0xF3;
					break;
				case MPB_PF2:
					handler->mandatoryPrefix = 0xF2;
					break;
				default:
					// throw new InvalidOperationException()
					break;
				}
				
				operands = LegacyHandler_CreateOps(encFlags1, &operands_length);

				OpCodeHandler_init(&handler, encFlags2, encFlags3Data, false, operands, operands_length, NULL, &OpCodeHandler_GetOpCode, &LegacyHandler_Encode);

				handler->handler_conf = LegacyHandler;
				//handler = new LegacyHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			}
			break;
		case EncodingKind_VEX:
			handler->Operands_Length = 38;

			handler->table = ((unsigned int)encFlags2 >> (int)EFLAGS2_TableShift) & (unsigned int)EFLAGS2_TableMask;
			enum WBit wbit = (enum WBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_WBitShift) & (unsigned int)EFLAGS2_WBitMask);
			enum LBit lbit = (enum LBit)(((unsigned int)encFlags2 >> (int)EFLAGS2_LBitShift) & (int)EFLAGS2_LBitMask);

			if (wbit == WBit_W1)
			{
				handler->W1 = UINT_MAX;
			}
			else
			{
				handler->W1 = 0;
			}

			switch (lbit)
			{
			case LBit_L1:
			case LBit_L256:
				handler->lastByte = 4;
				break;
			}
			if (handler->W1 != 0)
			{
				handler->lastByte |= 0x80;
			}
			handler->lastByte |= ((unsigned int)encFlags2 >> (int)EFLAGS2_MandatoryPrefixShift) & (unsigned int)EFLAGS2_MandatoryPrefixMask;
			if (wbit == WBit_WIG)
			{
				handler->mask_W_L |= 0x80;
			}
			if (lbit == LBit_LIG)
			{
				handler->mask_W_L |= 4;
				handler->mask_L |= 4;
			}

			operands = VEXHandler_CreateOps(encFlags1, &operands_length);

			OpCodeHandler_init(&handler, encFlags2, encFlags3Data, false, operands, operands_length, NULL, &OpCodeHandler_GetOpCode, &VEXHandler_Encode);

			handler->handler_conf = VexHandler;
			//handler = new VexHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_EVEX:
			handler->Operands_Length = 31;

			handler->handler_conf = EvexHandler;
			//handler = new EvexHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_XOP:
			handler->Operands_Length = 18;

			handler->handler_conf = XopHandler;
			//handler = new XopHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_D3NOW:
			handler->Operands_Length = 0; //TODO: test if valid if it is fine to have zero operands.

			handler->handler_conf = D3nowHandler;
			//handler = new D3nowHandler((enum EncFlags2)encFlags2[i], encFlags3);
			break;

		case EncodingKind_MVEX:
			handler->Operands_Length = 8;

			handler->handler_conf = MvexHandler;
			//handler = new MvexHandler((enum EncFlags1)encFlags1[i], (enum EncFlags2)encFlags2[i], encFlags3);

			break;
		default:
			handler->handler_conf = UndefinedHandler;
			//throw new InvalidOperationException();
			break;
		}

		EncoderInternal_OpCodeHandlers_Handlers[i] = *handler;
	}
}

enum OpKind GetImmediateOpKind(enum Code code, int operand) 
{
	int index = (int)code;
	OpCodeHandlers_init(code);
	struct OpCodeHandler handler = EncoderInternal_OpCodeHandlers_Handlers[index];

	//auto handlers = EncoderInternal_OpCodeHandlers_Handlers;
	if ((unsigned int)code >= (unsigned int)4936)
	{
		//throw new ArgumentOutOfRangeException(nameof(code));
	}
	//var operands = handlers[(int)code].Operands;
	struct Op* operands = handler.Operands;
	unsigned int operands_length = handler.Operands_Length;

	if ((unsigned int)operand >= operands_length)
	{
		//throw new ArgumentOutOfRangeException(nameof(operand), $"{code} doesn't have at least {operand + 1} operands");
		return (enum OpKind)0;
	}
		
	struct Op op = operands[operand];
	enum OpKind opKind = op.GetImmediateOpKind(&op);
	enum OpKind opKindPrev = (enum OpKind)(-1);

	if (opKind == OK_Immediate8 &&
		operand > 0 &&
		operand + 1 == operands_length)
	{
		opKindPrev = operands[operand - 1].GetImmediateOpKind(&operands[operand - 1]);
	}

	if (opKind == OK_Immediate8 && operand > 0 && operand + 1 == operands_length && (opKindPrev == OK_Immediate8 || opKindPrev == OK_Immediate16)) 
	{
		opKind = OK_Immediate8_2nd;
	}
	if (opKind == (enum OpKind)(-1))
	{
		//throw new ArgumentException($"{code}'s op{operand} isn't an immediate operand");
	}
	return opKind;
}

enum OpKind GetNearBranchOpKind(enum Code code, int operand) 
{
	int index = (int)code;
	OpCodeHandlers_init(code);
	struct OpCodeHandler handler = EncoderInternal_OpCodeHandlers_Handlers[index];


	if ((unsigned int)code >= (unsigned int)4936)
	{
		
	}
	struct Op* operands = handler.Operands;
	unsigned int operands_length = handler.Operands_Length;
	if ((unsigned int)operand >= operands_length)
	{
		//throw new ArgumentOutOfRangeException(nameof(operand), $"{code} doesn't have at least {operand + 1} operands");
	}
	enum OpKind opKind = operands->GetNearBranchOpKind(operands);
	if (opKind == (enum OpKind)(-1))
	{
		//throw new ArgumentException($"{code}'s op{operand} isn't a near branch operand");
	}
	return opKind;
}

enum OpKind GetFarBranchOpKind(enum Code code, int operand) 
{
	int index = (int)code;
	OpCodeHandlers_init(code);
	struct OpCodeHandler handler = EncoderInternal_OpCodeHandlers_Handlers[index];


	if ((unsigned int)code >= (unsigned int)4936)
	{

	}
	struct Op* operands = handler.Operands;
	unsigned int operands_length = handler.Operands_Length;
	if ((unsigned int)operand >= operands_length)
	{
		//throw new ArgumentOutOfRangeException(nameof(operand), $"{code} doesn't have at least {operand + 1} operands");
	}
	enum OpKind opKind = operands->GetFarBranchOpKind(operands);
	if (opKind == (enum OpKind)(-1))
	{
		//throw new ArgumentException($"{code}'s op{operand} isn't a far branch operand");
	}

	return opKind;
}

void InitializeSignedImmediate(struct Instruction* instruction, int operand, long immediate) 
{
	enum OpKind opKind = GetImmediateOpKind(GetCode(instruction), operand);
	SetOpKind(instruction, operand, opKind);

	switch (opKind) 
	{
	case OK_Immediate8:
		// All sbyte and all byte values can be used
		//if (!(sbyte.MinValue <= immediate && immediate <= byte.MaxValue))
		//	throw new ArgumentOutOfRangeException(nameof(immediate));
		SetInternalImmediate8(instruction, (unsigned int)((unsigned char)immediate));
		break;
	case OK_Immediate8_2nd:
		// All sbyte and all byte values can be used
		//if (!(sbyte.MinValue <= immediate && immediate <= byte.MaxValue))
		//	throw new ArgumentOutOfRangeException(nameof(immediate));
		SetInternalImmediate8_2nd(instruction, (unsigned int)((unsigned char)immediate));
		break;
	case OK_Immediate8to16:
		//if (!(sbyte.MinValue <= immediate && immediate <= sbyte.MaxValue))
		//	throw new ArgumentOutOfRangeException(nameof(immediate));
		SetInternalImmediate8(instruction, (unsigned int)((unsigned char)immediate));
		break;
	case OK_Immediate8to32:
		//if (!(sbyte.MinValue <= immediate && immediate <= sbyte.MaxValue))
		//	throw new ArgumentOutOfRangeException(nameof(immediate));
		SetInternalImmediate8(instruction, (unsigned int)((unsigned char)immediate));
		break;
	case OK_Immediate8to64:
		//if (!(sbyte.MinValue <= immediate && immediate <= sbyte.MaxValue))
		//	throw new ArgumentOutOfRangeException(nameof(immediate));
		SetInternalImmediate8(instruction, (unsigned int)((unsigned char)immediate));
		break;
	case OK_Immediate16:
		// All short and all ushort values can be used
		//if (!(short.MinValue <= immediate && immediate <= ushort.MaxValue))
		//	throw new ArgumentOutOfRangeException(nameof(immediate));
		SetInternalImmediate16(instruction, (unsigned int)((unsigned short)immediate));
		break;
	case OK_Immediate32:
		// All int and all uint values can be used
		//if (!(int.MinValue <= immediate && immediate <= uint.MaxValue))
		//	throw new ArgumentOutOfRangeException(nameof(immediate));
		SetImmediate32(instruction, (unsigned int)immediate);
		break;
	case OK_Immediate32to64:
		//if (!(int.MinValue <= immediate && immediate <= int.MaxValue))
		//	throw new ArgumentOutOfRangeException(nameof(immediate));
		SetImmediate32(instruction, (unsigned int)immediate);
		break;
	case OK_Immediate64:
		SetImmediate64(instruction, (unsigned long)immediate);
		break;
	default:
		//throw new ArgumentOutOfRangeException(nameof(instruction));
		break;
	}
}

void InitMemoryOperand(struct Instruction* instruction, struct MemoryOperand* memory) 
{
	SetInternalMemoryBase(instruction, memory->Base);
	SetInternalMemoryIndex(instruction, memory->Index);
	SetMemoryIndexScale(instruction, memory->Scale);
	SetMemoryDisplSize(instruction, memory->DisplSize);
	SetMemoryDisplacement64(instruction, (unsigned long)memory->Displacement);
	SetIsBroadcast(instruction, memory->IsBroadcast);
	Set_SegmentPrefix(instruction, memory->SegmentPrefix);
}

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
struct Instruction* Instruction_CreateDeclareByte_imm1(unsigned char b0)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 1);
	SetDeclareByteValue(instruction, 0, b0);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
struct Instruction* Instruction_CreateDeclareByte_imm2(unsigned char b0, unsigned char b1)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 2);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
struct Instruction* Instruction_CreateDeclareByte_imm3(unsigned char b0, unsigned char b1, unsigned char b2)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 3);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
struct Instruction* Instruction_CreateDeclareByte_imm4(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 4);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
struct Instruction* Instruction_CreateDeclareByte_imm5(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 5);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

/// <summary>
/// Creates a <c>db</c>/<c>.byte</c> asm directive
/// </summary>
/// <param name="b0">Byte 0</param>
/// <param name="b1">Byte 1</param>
/// <param name="b2">Byte 2</param>
/// <param name="b3">Byte 3</param>
/// <param name="b4">Byte 4</param>
/// <param name="b5">Byte 5</param>
struct Instruction* Instruction_CreateDeclareByte_imm6(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 6);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm7(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 7);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm8(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 8);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm9(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 9);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);
	SetDeclareByteValue(instruction, 8, b8);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm10(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 10);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);
	SetDeclareByteValue(instruction, 8, b8);
	SetDeclareByteValue(instruction, 9, b9);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm11(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 11);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);
	SetDeclareByteValue(instruction, 8, b8);
	SetDeclareByteValue(instruction, 9, b9);
	SetDeclareByteValue(instruction, 10, b10);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm12(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 12);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);
	SetDeclareByteValue(instruction, 8, b8);
	SetDeclareByteValue(instruction, 9, b9);
	SetDeclareByteValue(instruction, 10, b10);
	SetDeclareByteValue(instruction, 11, b11);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm13(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11, unsigned char b12)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 13);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);
	SetDeclareByteValue(instruction, 8, b8);
	SetDeclareByteValue(instruction, 9, b9);
	SetDeclareByteValue(instruction, 10, b10);
	SetDeclareByteValue(instruction, 11, b11);
	SetDeclareByteValue(instruction, 12, b12);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm14(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11, unsigned char b12, unsigned char b13)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 14);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);
	SetDeclareByteValue(instruction, 8, b8);
	SetDeclareByteValue(instruction, 9, b9);
	SetDeclareByteValue(instruction, 10, b10);
	SetDeclareByteValue(instruction, 11, b11);
	SetDeclareByteValue(instruction, 12, b12);
	SetDeclareByteValue(instruction, 13, b13);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm15(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11, unsigned char b12, unsigned char b13, unsigned char b14)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 15);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);
	SetDeclareByteValue(instruction, 8, b8);
	SetDeclareByteValue(instruction, 9, b9);
	SetDeclareByteValue(instruction, 10, b10);
	SetDeclareByteValue(instruction, 11, b11);
	SetDeclareByteValue(instruction, 12, b12);
	SetDeclareByteValue(instruction, 13, b13);
	SetDeclareByteValue(instruction, 14, b14);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

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
struct Instruction* Instruction_CreateDeclareByte_imm16(unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4, unsigned char b5, unsigned char b6, unsigned char b7, unsigned char b8, unsigned char b9, unsigned char b10, unsigned char b11, unsigned char b12, unsigned char b13, unsigned char b14, unsigned char b15)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, DeclareByte);

	SetInternalDeclareDataCount(instruction, 16);

	SetDeclareByteValue(instruction, 0, b0);
	SetDeclareByteValue(instruction, 1, b1);
	SetDeclareByteValue(instruction, 2, b2);
	SetDeclareByteValue(instruction, 3, b3);
	SetDeclareByteValue(instruction, 4, b4);
	SetDeclareByteValue(instruction, 5, b5);
	SetDeclareByteValue(instruction, 6, b6);
	SetDeclareByteValue(instruction, 7, b7);
	SetDeclareByteValue(instruction, 8, b8);
	SetDeclareByteValue(instruction, 9, b9);
	SetDeclareByteValue(instruction, 10, b10);
	SetDeclareByteValue(instruction, 11, b11);
	SetDeclareByteValue(instruction, 12, b12);
	SetDeclareByteValue(instruction, 13, b13);
	SetDeclareByteValue(instruction, 14, b14);
	SetDeclareByteValue(instruction, 15, b15);

	//Debug.Assert(instruction.OpCount == 0);
	return instruction;
}

/// <summary>
/// Creates a new near/short branch instruction
/// </summary>
/// <param name="code">Code value</param>
/// <param name="target">Target address</param>
struct Instruction* Instruction_CreateBranch(enum Code code, unsigned long target)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);
	SetOp0Kind(instruction, GetNearBranchOpKind(code, 0));
	SetNearBranch64(instruction, target);

	//Debug.Assert(instruction.OpCount == 1);

	return instruction;
}

/// <summary>
/// Creates a new far branch instruction
/// </summary>
/// <param name="code">Code value</param>
/// <param name="selector">Selector/segment value</param>
/// <param name="offset">Offset</param>
struct Instruction* Instruction_CreateBranch(enum Code code, unsigned short selector, unsigned int offset)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);
	SetOp0Kind(instruction, GetFarBranchOpKind(code, 0));
	SetFarBranchSelector(instruction, selector);
	SetFarBranch32(instruction, offset);

	//Debug.Assert(instruction.OpCount == 1);

	return instruction;
}

/// <summary>
/// Creates an instruction with 2 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register">op0: Register</param>
/// <param name="immediate">op1: Immediate value</param>
struct Instruction* Instruction_Create(enum Code code, enum Register _register, int immediate)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op0Kind = OpKind.Register;
	SetOp0Kind(instruction, OK_Register);
	SetOp0Register(instruction, _register);

	InitializeSignedImmediate(&instruction, 1, immediate);

	//Debug.Assert(instruction.OpCount == 2);
	return instruction;
}

/// <summary>
/// Creates an instruction with 1 operand
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register">op0: Register</param>
struct Instruction* Instruction_Create(enum Code code, enum Register _register)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);
	
	SetCode(instruction, code);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op0Kind = OpKind.Register;
	SetOp0Kind(instruction, OK_Register);
	SetOp0Register(instruction, _register);

	//Debug.Assert(instruction.OpCount == 1);
	return instruction;
}

/// <summary>
/// Creates an instruction with 2 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register1">op0: Register</param>
/// <param name="register2">op1: Register</param>
struct Instruction* Instruction_Create(enum Code code, enum Register register1, enum Register register2)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op0Kind = OpKind.Register;
	SetOp0Kind(instruction, OK_Register);
	SetOp0Register(instruction, register1);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op1Kind = OpKind.Register;
	SetOp1Kind(instruction, OK_Register);
	SetOp1Register(instruction, register2);

	//Debug.Assert(instruction.OpCount == 2);
	return instruction;
}

/// <summary>
/// Creates an instruction with 2 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register">op0: Register</param>
/// <param name="memory">op1: Memory operand</param>
struct Instruction* Instruction_Create(enum Code code, enum Register register1, struct MemoryOperand* memory)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op0Kind = OpKind.Register;
	SetOp0Kind(instruction, OK_Register);
	SetOp0Register(instruction, register1);

	SetOp1Kind(instruction, OK_Memory);
	InitMemoryOperand(instruction, memory);

	//Debug.Assert(instruction.OpCount == 2);
	return instruction;
}

struct Instruction* CreateString_ESRDI_Reg(enum Code code, int addressSize, enum Register register1, enum RepPrefixKind repPrefix)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	if (repPrefix == RPK_Repe)
	{
		InternalSetHasRepePrefix(instruction);
	}
	else if (repPrefix == RPK_Repne)
	{
		InternalSetHasRepnePrefix(instruction);
	}
	else
	{
		//Debug.Assert(repPrefix == RepPrefixKind.None);
	}
		
	if (addressSize == 64)
	{
		SetOp0Kind(instruction, OK_MemoryESRDI);
	}
	else if (addressSize == 32)
	{
		SetOp0Kind(instruction, OK_MemoryESEDI);
	}
	else if (addressSize == 16)
	{
		SetOp0Kind(instruction, OK_MemoryESDI);
	}
	else
	{
		//throw new ArgumentOutOfRangeException(nameof(addressSize));
	}
		

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op1Kind = OpKind.Register;
	SetOp1Kind(instruction, OK_Register);
	SetOp1Register(instruction, register1);

	//Debug.Assert(instruction.OpCount == 2);
	return instruction;
}

/// <summary>
/// Creates a <c>STOSD</c> instruction
/// </summary>
/// <param name="addressSize">16, 32, or 64</param>
/// <param name="repPrefix">Rep prefix or <see cref="RepPrefixKind.None"/></param>
struct Instruction* Instruction_CreateStosd(int addressSize, enum RepPrefixKind repPrefix)
{
	return CreateString_ESRDI_Reg(Stosd_m32_EAX, addressSize, Register_EAX, repPrefix);
}

/// <summary>
/// Creates an instruction with 3 operands
/// </summary>
/// <param name="code">Code value</param>
/// <param name="register1">op0: Register</param>
/// <param name="register2">op1: Register</param>
/// <param name="memory">op2: Memory operand</param>
struct Instruction* Instruction_Create(enum Code code, enum Register register1, enum Register register2, struct MemoryOperand* memory)
{
	struct Instruction* instruction;

	instruction = instruction_new();
	instruction_init(&instruction);

	SetCode(instruction, code);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op0Kind = OpKind.Register;
	SetOp0Kind(instruction, OK_Register);
	SetOp0Register(instruction, register1);

	//Static.Assert(OpKind.Register == 0 ? 0 : -1);
	//instruction.Op1Kind = OpKind.Register;
	SetOp1Kind(instruction, OK_Register);
	SetOp1Register(instruction, register2);

	SetOp2Kind(instruction, OK_Memory);
	InitMemoryOperand(instruction, memory);

	//Debug.Assert(instruction.OpCount == 3);
	return instruction;
}

void Encoder_AddBranch(struct Encoder* encoder, struct Instruction* instruction, enum OpKind opKind, int immSize, int operand)
{
	if (!Verify(operand, opKind, Instruction_GetOpKind(instruction, operand)))
	{
		return;
	}
	unsigned long target;
	switch (immSize)
	{
	case 1:
		switch (opKind)
		{
		case OK_NearBranch16:
			encoder->EncoderFlags |= encoder->opSize16Flags;
			encoder->ImmSize = ImmSize_RipRelSize1_Target16;
			encoder->Immediate = GetNearBranch16(instruction);
			break;
		case OK_NearBranch32:
			encoder->EncoderFlags |= encoder->opSize32Flags;
			encoder->ImmSize = ImmSize_RipRelSize1_Target32;
			encoder->Immediate = GetNearBranch32(instruction);
			break;
		case OK_NearBranch64:
			encoder->ImmSize = ImmSize_RipRelSize1_Target64;
			target = GetNearBranch64(instruction);
			encoder->Immediate = (unsigned int)target;
			encoder->ImmediateHi = (unsigned int)(target >> 32);
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
		break;
	case 2:
		switch (opKind)
		{
		case OK_NearBranch16:
			encoder->EncoderFlags |= encoder->opSize16Flags;
			encoder->ImmSize = ImmSize_RipRelSize2_Target16;
			encoder->Immediate = GetNearBranch16(instruction);
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
		break;
	case 4:
		switch (opKind)
		{
		case OK_NearBranch32:
			encoder->EncoderFlags |= encoder->opSize32Flags;
			encoder->ImmSize = ImmSize_RipRelSize4_Target32;
			encoder->Immediate = GetNearBranch32(instruction);
			break;
		case OK_NearBranch64:
			encoder->ImmSize = ImmSize_RipRelSize4_Target64;
			target = GetNearBranch64(instruction);
			encoder->Immediate = (unsigned int)target;
			encoder->ImmediateHi = (unsigned int)(target >> 32);
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
		break;

	default:
		//throw new InvalidOperationException();
		break;
	}
}

void Encoder_AddBranchX(struct Encoder* encoder, int immSize, struct Instruction* instruction, int operand)
{
	if (encoder->bitness == 64)
	{
		if (!Verify(operand, OK_NearBranch64, Instruction_GetOpKind(instruction, operand)))
		{
			return;
		}

		unsigned long target = GetNearBranch64(instruction);
		switch (immSize)
		{
		case 2:
			encoder->EncoderFlags |= EncoderFlags_P66;
			encoder->ImmSize = ImmSize_RipRelSize2_Target64;
			encoder->Immediate = (unsigned int)target;
			encoder->ImmediateHi = (unsigned int)(target >> 32);
			break;
		case 4:
			encoder->ImmSize = ImmSize_RipRelSize4_Target64;
			encoder->Immediate = (unsigned int)target;
			encoder->ImmediateHi = (unsigned int)(target >> 32);
			break;
		default:
			//throw new InvalidOperationException();
			break;
		}
	}
	else
	{
		//Debug.Assert(encoder->bitness == 16 || encoder->bitness == 32);
		if (!Verify(operand, OK_NearBranch32, Instruction_GetOpKind(instruction, operand)))
		{
			return;
		}
		switch (immSize) {
		case 2:
			//Static.Assert((int)EncoderFlags.P66 == 0x80 ? 0 : -1);
			encoder->EncoderFlags |= (enum EncoderFlags)((encoder->bitness & 0x20) << 2);
			encoder->ImmSize = ImmSize_RipRelSize2_Target32;
			encoder->Immediate = GetNearBranch32(instruction);
			break;
		case 4:
			//Static.Assert((int)EncoderFlags.P66 == 0x80 ? 0 : -1);
			encoder->EncoderFlags |= (enum EncoderFlags)((encoder->bitness & 0x10) << 3);
			encoder->ImmSize = ImmSize_RipRelSize4_Target32;
			encoder->Immediate = GetNearBranch32(instruction);
			break;
		case 8:
		default:
			//throw new InvalidOperationException();
			break;
		}
	}
}

void Encoder_AddBranchDisp(struct Encoder* encoder, int displSize, struct Instruction* instruction, int operand)
{
	//Debug.Assert(displSize == 2 || displSize == 4);
	enum OpKind opKind;
	switch (displSize)
	{
	case 2:
		opKind = OK_NearBranch16;
		encoder->ImmSize = ImmSize_Size2;
		encoder->Immediate = GetNearBranch16(instruction);
		break;
	case 4:
		opKind = OK_NearBranch32;
		encoder->ImmSize = ImmSize_Size4;
		encoder->Immediate = GetNearBranch32(instruction);
		break;
	default:
		//throw new InvalidOperationException();
		break;
	}
	if (!Verify(operand, opKind, Instruction_GetOpKind(instruction, operand)))
	{
		return;
	}
}

void Encoder_AddFarBranch(struct Encoder* encoder, struct Instruction* instruction, int operand, int size)
{
	if (size == 2)
	{
		if (!Verify(operand, OK_FarBranch16, Instruction_GetOpKind(instruction, operand)))
		{
			return;
		}
		encoder->ImmSize = ImmSize_Size2_2;
		encoder->Immediate = GetFarBranch16(instruction);
		encoder->ImmediateHi = GetFarBranchSelector(instruction);
	}
	else
	{
		//Debug.Assert(size == 4);
		if (!Verify(operand, OK_FarBranch32, Instruction_GetOpKind(instruction, operand)))
		{
			return;
		}
		encoder->ImmSize = ImmSize_Size4_2;
		encoder->Immediate = GetFarBranch32(instruction);
		encoder->ImmediateHi = GetFarBranchSelector(instruction);
	}
	if (encoder->bitness != size * 8)
	{
		encoder->EncoderFlags |= EncoderFlags_P66;
	}
}

void Encoder_SetAddrSize(struct Encoder* encoder, int regSize)
{
	//Debug.Assert(regSize == 2 || regSize == 4 || regSize == 8);
	if (encoder->bitness == 64)
	{
		if (regSize == 2)
		{
			//ErrorMessage = $"Invalid register size: {regSize * 8}, must be 32-bit or 64-bit";
		}
		else if (regSize == 4)
		{
			encoder->EncoderFlags |= EncoderFlags_P67;
		}
	}
	else {
		if (regSize == 8)
		{
			//ErrorMessage = $"Invalid register size: {regSize * 8}, must be 16-bit or 32-bit";
		}
		else if (encoder->bitness == 16)
		{
			if (regSize == 4)
			{
				encoder->EncoderFlags |= EncoderFlags_P67;
			}
		}
		else {
			//Debug.Assert(bitness == 32);
			if (regSize == 2)
			{
				encoder->EncoderFlags |= EncoderFlags_P67;
			}
		}
	}
}

void Encoder_AddAbsMem(struct Encoder* encoder, struct Instruction* instruction, int operand)
{
	encoder->EncoderFlags |= EncoderFlags_Displ;
	enum OpKind opKind = Instruction_GetOpKind(instruction, operand);
	if (opKind == OK_Memory)
	{
		if (GetMemoryBase(instruction) != Register_None || GetMemoryIndex(instruction) != Register_None)
		{
			//ErrorMessage = $"Operand {operand}: Absolute addresses can't have base and/or index regs";
			return;
		}
		if (GetMemoryIndexScale(instruction) != 1)
		{
			//ErrorMessage = $"Operand {operand}: Absolute addresses must have scale == *1";
			return;
		}
		switch (GetMemoryDisplSize(instruction))
		{
		case 2:
			if (encoder->bitness == 64)
			{
				//ErrorMessage = $"Operand {operand}: 16-bit abs addresses can't be used in 64-bit mode";
				return;
			}
			if (encoder->bitness == 32)
			{
				encoder->EncoderFlags |= EncoderFlags_P67;
			}
			encoder->DisplSize = DisplSize_Size2;
			if (GetMemoryDisplacement64(instruction) > USHRT_MAX)
			{
				//ErrorMessage = $"Operand {operand}: Displacement must fit in a ushort";
				return;
			}
			encoder->Displ = GetMemoryDisplacement32(instruction);
			break;
		case 4:
			encoder->EncoderFlags |= encoder->adrSize32Flags;
			encoder->DisplSize = DisplSize_Size4;
			if (GetMemoryDisplacement64(instruction) > UINT_MAX)
			{
				//ErrorMessage = $"Operand {operand}: Displacement must fit in a uint";
				return;
			}
			encoder->Displ = GetMemoryDisplacement32(instruction);
			break;
		case 8:
			if (encoder->bitness != 64)
			{
				//ErrorMessage = $"Operand {operand}: 64-bit abs address is only available in 64-bit mode";
				return;
			}
			encoder->DisplSize = DisplSize_Size8;
			unsigned long addr = GetMemoryDisplacement64(instruction);
			encoder->Displ = (unsigned int)addr;
			encoder->DisplHi = (unsigned int)(addr >> 32);
			break;
		default:
			//ErrorMessage = $"Operand {operand}: {nameof(Instruction)}.{nameof(Instruction.MemoryDisplSize)} must be initialized to 2 (16-bit), 4 (32-bit) or 8 (64-bit)";
			break;
		}
	}
	else
	{
		//ErrorMessage = $"Operand {operand}: Expected OpKind {nameof(OpKind.Memory)}, actual: {opKind}";
	}
}

void Encoder_AddModRMRegister(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi)
{
	if (!Verify(operand, OK_Register, Instruction_GetOpKind(instruction, operand)))
	{
		return;
	}
	enum Register reg = GetOpRegister(instruction, operand);
	if (!Verify(operand, reg, regLo, regHi))
	{
		return;
	}
	unsigned int regNum = (unsigned int)(reg - regLo);
	if (regLo == Register_AL) 
	{
		if (reg >= Register_SPL)
		{
			regNum -= 4;
			encoder->EncoderFlags |= EncoderFlags_REX;
		}
		else if (reg >= Register_AH)
		{
			encoder->EncoderFlags |= EncoderFlags_HighLegacy8BitRegs;
		}
	}
	//Debug.Assert(regNum <= 31);
	encoder->ModRM |= (unsigned char)((regNum & 7) << 3);
	encoder->EncoderFlags |= EncoderFlags_ModRM;
	//Static.Assert((int)EncoderFlags.R == 4 ? 0 : -1);
	encoder->EncoderFlags |= (enum EncoderFlags)((regNum & 8) >> 1);
	//Static.Assert((int)EncoderFlags.R2 == 0x200 ? 0 : -1);
	encoder->EncoderFlags |= (enum EncoderFlags)((regNum & 0x10) << (9 - 4));
}

void Encoder_AddReg(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi)
{
	if (!Verify(operand, OK_Register, Instruction_GetOpKind(instruction, operand)))
	{
		return;
	}
	enum Register reg = GetOpRegister(instruction, operand);
	if (!Verify(operand, reg, regLo, regHi))
	{
		return;
	}
	unsigned int regNum = (unsigned int)(reg - regLo);
	if (regLo == Register_AL)
	{
		if (reg >= Register_SPL)
		{
			regNum -= 4;
			encoder->EncoderFlags |= EncoderFlags_REX;
		}
		else if (reg >= Register_AH)
		{
			encoder->EncoderFlags |= EncoderFlags_HighLegacy8BitRegs;
		}
	}
	//Debug.Assert(regNum <= 15);
	encoder->OpCode |= regNum & 7;
	//Static.Assert((int)EncoderFlags.B == 1 ? 0 : -1);
	//Debug.Assert(regNum <= 15);
	encoder->EncoderFlags |= (enum EncoderFlags)(regNum >> 3);// regNum <= 15, so no need to mask out anything
}

/// <summary>
/// Checks if <paramref name="memorySize"/> is a broadcast memory type
/// </summary>
/// <param name="memorySize">Memory size</param>
/// <returns></returns>
bool MemorySize_IsBroadcast(enum MemorySize memorySize)
{
	return memorySize >= FirstBroadcastMemorySize;
}

int GetAddressSizeInBytes(enum Register baseReg, enum Register indexReg, int displSize, enum CodeSize codeSize)
{
	if ((Register_RAX <= baseReg && baseReg <= Register_R15) || (Register_RAX <= indexReg && indexReg <= Register_R15) || baseReg == Register_RIP)
	{
		return 8;
	}
	if ((Register_EAX <= baseReg && baseReg <= Register_R15D) || (Register_EAX <= indexReg && indexReg <= Register_R15D) || baseReg == Register_EIP)
	{
		return 4;
	}
	if ((Register_AX <= baseReg && baseReg <= Register_DI) || (Register_AX <= indexReg && indexReg <= Register_DI))
	{
		return 2;
	}
	if (displSize == 2 || displSize == 4 || displSize == 8)
	{
		return displSize;
	}
	switch (codeSize)
	{
	case CodeSize_Code64:
		return 8;
	case CodeSize_Code32:
		return 4;
	case CodeSize_Code16:
		return 2;
	default:
		return 8;
	}
}

/// <summary>
/// Checks if it's a 16-bit general purpose register (<c>AX</c>-<c>R15W</c>)
/// </summary>
/// <param name="register">Register</param>
/// <returns></returns>
bool IsGPR16(enum Register register1)
{
	return Register_AX <= register1 && register1 <= Register_R15W;
}

/// <summary>
/// Checks if it's a 32-bit general purpose register (<c>EAX</c>-<c>R15D</c>)
/// </summary>
/// <param name="register">Register</param>
/// <returns></returns>
bool IsGPR32(enum Register register1)
{
	return Register_EAX <= register1 && register1 <= Register_R15D;
}

/// <summary>
/// Checks if it's a 64-bit general purpose register (<c>RAX</c>-<c>R15</c>)
/// </summary>
/// <param name="register">Register</param>
/// <returns></returns>
bool IsGPR64(enum Register register1)
{
	return Register_RAX <= register1 && register1 <= Register_R15;
}


int Encoder_GetRegisterOpSize(struct Instruction* instruction)
{
	//Debug.Assert(instruction.Op0Kind == OpKind.Register);
	if (GetOp0Kind(instruction) == OK_Register)
	{
		enum Register reg = GetOp0Register(instruction);
		if (IsGPR64(reg))
		{
			return 64;
		}
		if (IsGPR32(reg))
		{
			return 32;
		}
		if (IsGPR16(reg))
		{
			return 16;
		}
	}
	return 0;
}

void Encoder_AddRegOrMem(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi, enum Register vsibIndexRegLo, enum Register vsibIndexRegHi, bool allowMemOp, bool allowRegOp)
{
	enum OpKind opKind = Instruction_GetOpKind(instruction, operand);
	encoder->EncoderFlags |= EncoderFlags_ModRM;
	if (opKind == OK_Register)
	{
		if (!allowRegOp)
		{
			//ErrorMessage = $"Operand {operand}: register operand is not allowed";
			return;
		}
		enum Register reg = GetOpRegister(instruction, operand);
		if (!Verify(operand, reg, regLo, regHi))
		{
			return;
		}
		unsigned int regNum = (unsigned int)(reg - regLo);
		if (regLo == Register_AL)
		{
			if (reg >= Register_R8L)
			{
				regNum -= 4;
			}
			else if (reg >= Register_SPL)
			{
				regNum -= 4;
				encoder->EncoderFlags |= EncoderFlags_REX;
			}
			else if (reg >= Register_AH)
			{
				encoder->EncoderFlags |= EncoderFlags_HighLegacy8BitRegs;
			}
		}
		encoder->ModRM |= (unsigned char)(regNum & 7);
		encoder->ModRM |= 0xC0;
		//Static.Assert((int)EncoderFlags.B == 1 ? 0 : -1);
		//Static.Assert((int)EncoderFlags.X == 2 ? 0 : -1);
		encoder->EncoderFlags |= (enum EncoderFlags)((regNum >> 3) & 3);
		//Debug.Assert(regNum <= 31);
	}
	else if (opKind == OK_Memory)
	{
		if (!allowMemOp)
		{
			//ErrorMessage = $"Operand {operand}: memory operand is not allowed";
			return;
		}
		if (MemorySize_IsBroadcast(Instruction_GetMemorySize(instruction)))
		{
			encoder->EncoderFlags |= EncoderFlags_Broadcast;
		}

		enum CodeSize codeSize = GetCodeSize(instruction);
		if (codeSize == CodeSize_Unknown)
		{
			if (encoder->bitness == 64)
			{
				codeSize = CodeSize_Code64;
			}
			else if (encoder->bitness == 32)
			{
				codeSize = CodeSize_Code32;
			}
			else
			{
				//Debug.Assert(bitness == 16);
				codeSize = CodeSize_Code16;
			}
		}
		int addrSize = GetAddressSizeInBytes(GetMemoryBase(instruction), GetMemoryIndex(instruction), GetMemoryDisplSize(instruction), codeSize) * 8;
		if (addrSize != encoder->bitness)
		{
			encoder->EncoderFlags |= EncoderFlags_P67;
		}
		if ((encoder->EncoderFlags & EncoderFlags_RegIsMemory) != 0)
		{
			int regSize = Encoder_GetRegisterOpSize(instruction);
			if (regSize != addrSize)
			{
				//ErrorMessage = $"Operand {operand}: Register operand size must equal memory addressing mode (16/32/64)";
				return;
			}
		}
		if (addrSize == 16)
		{
			if (vsibIndexRegLo != Register_None)
			{
				//ErrorMessage = $"Operand {operand}: VSIB operands can't use 16-bit addressing. It must be 32-bit or 64-bit addressing";
				return;
			}
			Encoder_AddMemOp16(encoder, instruction, operand);
		}
		else
		{
			Encoder_AddMemOp(encoder, instruction, operand, addrSize, vsibIndexRegLo, vsibIndexRegHi);
		}
	}
	else
	{
		//ErrorMessage = $"Operand {operand}: Expected a register or memory operand, but opKind is {opKind}";
	}
}

void Encoder_AddRegOrMem(struct Encoder* encoder, struct Instruction* instruction, int operand, enum Register regLo, enum Register regHi, bool allowMemOp, bool allowRegOp)
{
	Encoder_AddRegOrMem(encoder, instruction, operand, regLo, regHi, Register_None, Register_None, allowMemOp, allowRegOp);
}

bool Encoder_TryConvertToDisp8N(struct Encoder* encoder, struct Instruction* instruction, int displ, signed char* compressedValue)
{
	bool(*tryConvertToDisp8N)(struct Encoder*, struct OpCodeHandler*, struct Instruction*, int, signed char*) = encoder->handler->TryConvertToDisp8N;
	if (tryConvertToDisp8N != NULL)
	{
		return tryConvertToDisp8N(encoder, encoder->handler, instruction, displ, compressedValue);
	}
	if (SCHAR_MIN <= displ && displ <= SCHAR_MAX)
	{
		(*compressedValue) = (signed char)displ;
		return true;
	}
	(*compressedValue) = 0;
	return false;
}

void Encoder_AddMemOp16(struct Encoder* encoder, struct Instruction* instruction, int operand)
{
	if (encoder->bitness == 64)
	{
		//ErrorMessage = $"Operand {operand}: 16-bit addressing can't be used by 64-bit code";
		return;
	}
	enum Register baseReg = GetMemoryBase(instruction);
	enum Register indexReg = GetMemoryIndex(instruction);
	int displSize = GetMemoryDisplSize(instruction);

	if (baseReg == Register_BX && indexReg == Register_SI)
	{
		// Nothing
	}
	else if (baseReg == Register_BX && indexReg == Register_DI)
	{
		encoder->ModRM |= 1;
	}
	else if (baseReg == Register_BP && indexReg == Register_SI)
	{
		encoder->ModRM |= 2;
	}
	else if (baseReg == Register_BP && indexReg == Register_DI)
	{
		encoder->ModRM |= 3;
	}
	else if (baseReg == Register_SI && indexReg == Register_None)
	{
		encoder->ModRM |= 4;
	}
	else if (baseReg == Register_DI && indexReg == Register_None)
	{
		encoder->ModRM |= 5;
	}
	else if (baseReg == Register_BP && indexReg == Register_None)
	{
		encoder->ModRM |= 6;
	}
	else if (baseReg == Register_BX && indexReg == Register_None)
	{
		encoder->ModRM |= 7;
	}
	else if (baseReg == Register_None && indexReg == Register_None)
	{
		encoder->ModRM |= 6;
		encoder->DisplSize = DisplSize_Size2;
		if (GetMemoryDisplacement64(instruction) > USHRT_MAX)
		{
			//ErrorMessage = $"Operand {operand}: Displacement must fit in a ushort";
			return;
		}
		encoder->Displ = GetMemoryDisplacement32(instruction);
	}
	else
	{
		//ErrorMessage = $"Operand {operand}: Invalid 16-bit base + index registers: base={baseReg}, index={indexReg}";
		return;
	}

	if (baseReg != Register_None || indexReg != Register_None) {
		if ((long)GetMemoryDisplacement64(instruction) < SHRT_MIN || (long)GetMemoryDisplacement64(instruction) > USHRT_MAX)
		{
			//ErrorMessage = $"Operand {operand}: Displacement must fit in a short or a ushort";
			return;
		}
		encoder->Displ = GetMemoryDisplacement32(instruction);
		// [bp] => [bp+00]
		if (displSize == 0 && baseReg == Register_BP && indexReg == Register_None)
		{
			displSize = 1;
			if (encoder->Displ != 0)
			{
				//ErrorMessage = $"Operand {operand}: Displacement must be 0 if displSize == 0";
				return;
			}
		}
		if (displSize == 1)
		{
			signed char compressedValue = 0;
			if (Encoder_TryConvertToDisp8N(encoder, instruction, (short)encoder->Displ, &compressedValue))
			{
				encoder->Displ = (unsigned int)compressedValue;
			}
			else
			{
				displSize = 2;
			}
		}
		if (displSize == 0)
		{
			if (encoder->Displ != 0)
			{
				//ErrorMessage = $"Operand {operand}: Displacement must be 0 if displSize == 0";
				return;
			}
		}
		else if (displSize == 1)
		{
			// This if check should never be true when we're here
			if ((int)encoder->Displ < SCHAR_MIN || (int)encoder->Displ > SCHAR_MAX)
			{
				//ErrorMessage = $"Operand {operand}: Displacement must fit in an sbyte";
				return;
			}
			encoder->ModRM |= 0x40;
			encoder->DisplSize = DisplSize_Size1;
		}
		else if (displSize == 2) {
			encoder->ModRM |= 0x80;
			encoder->DisplSize = DisplSize_Size2;
		}
		else
		{
			//ErrorMessage = $"Operand {operand}: Invalid displacement size: {displSize}, must be 0, 1, or 2";
			return;
		}
	}
}

void Encoder_AddMemOp(struct Encoder* encoder, struct Instruction* instruction, int operand, int addrSize, enum Register vsibIndexRegLo, enum Register vsibIndexRegHi)
{
	//Debug.Assert(addrSize == 32 || addrSize == 64);
	if (encoder->bitness != 64 && addrSize == 64)
	{
		//ErrorMessage = $"Operand {operand}: 64-bit addressing can only be used in 64-bit mode";
		return;
	}

	enum Register baseReg = GetMemoryBase(instruction);
	enum Register indexReg = GetMemoryIndex(instruction);
	enum Register displSize = GetMemoryDisplSize(instruction);

	enum Register baseRegLo, baseRegHi;
	enum Register indexRegLo, indexRegHi;
	if (addrSize == 64)
	{
		baseRegLo = Register_RAX;
		baseRegHi = Register_R15;
	}
	else
	{
		//Debug.Assert(addrSize == 32);
		baseRegLo = Register_EAX;
		baseRegHi = Register_R15D;
	}
	if (vsibIndexRegLo != Register_None)
	{
		indexRegLo = vsibIndexRegLo;
		indexRegHi = vsibIndexRegHi;
	}
	else
	{
		indexRegLo = baseRegLo;
		indexRegHi = baseRegHi;
	}
	if (baseReg != Register_None && baseReg != Register_RIP && baseReg != Register_EIP && !Verify(operand, baseReg, baseRegLo, baseRegHi))
	{
		return;
	}
	if (indexReg != Register_None && !Verify(operand, indexReg, indexRegLo, indexRegHi))
	{
		return;
	}

	if (displSize != 0 && displSize != 1 && displSize != 4 && displSize != 8)
	{
		//ErrorMessage = $"Operand {operand}: Invalid displ size: {displSize}, must be 0, 1, 4, 8";
		return;
	}
	if (baseReg == Register_RIP || baseReg == Register_EIP)
	{
		if (indexReg != Register_None)
		{
			//ErrorMessage = $"Operand {operand}: RIP relative addressing can't use an index register";
			return;
		}
		if (GetMemoryIndexScale(instruction) != 0)
		{
			//ErrorMessage = $"Operand {operand}: RIP relative addressing must use scale *1";
			return;
		}
		if (encoder->bitness != 64)
		{
			//ErrorMessage = $"Operand {operand}: RIP/EIP relative addressing is only available in 64-bit mode";
			return;
		}
		if ((encoder->EncoderFlags & EncoderFlags_MustUseSib) != 0)
		{
			//ErrorMessage = $"Operand {operand}: RIP/EIP relative addressing isn't supported";
			return;
		}
		encoder->ModRM |= 5;
		unsigned long target = GetMemoryDisplacement64(instruction);
		if (baseReg == Register_RIP)
		{
			encoder->DisplSize = DisplSize_RipRelSize4_Target64;
			encoder->Displ = (unsigned int)target;
			encoder->DisplHi = (unsigned int)(target >> 32);
		}
		else
		{
			encoder->DisplSize = DisplSize_RipRelSize4_Target32;
			if (target > UINT_MAX)
			{
				//ErrorMessage = $"Operand {operand}: Target address doesn't fit in 32 bits: 0x{target:X}";
				return;
			}
			encoder->Displ = (unsigned int)target;
		}
		return;
	}
	int scale = GetMemoryIndexScale(instruction);
	encoder->Displ = GetMemoryDisplacement32(instruction);
	if (addrSize == 64)
	{
		if ((long)GetMemoryDisplacement64(instruction) < INT_MIN || (long)GetMemoryDisplacement64(instruction) > INT_MAX)
		{
			//ErrorMessage = $"Operand {operand}: Displacement must fit in an int";
			return;
		}
	}
	else
	{
		//Debug.Assert(addrSize == 32);
		if ((long)GetMemoryDisplacement64(instruction) < INT_MIN || (long)GetMemoryDisplacement64(instruction) > UINT_MAX)
		{
			//ErrorMessage = $"Operand {operand}: Displacement must fit in an int or a uint";
			return;
		}
	}
	if (baseReg == Register_None && indexReg == Register_None)
	{
		if (vsibIndexRegLo != Register_None)
		{
			//ErrorMessage = $"Operand {operand}: VSIB addressing can't use an offset-only address";
			return;
		}
		if (encoder->bitness == 64 || scale != 0 || (encoder->EncoderFlags & EncoderFlags_MustUseSib) != 0)
		{
			encoder->ModRM |= 4;
			encoder->DisplSize = DisplSize_Size4;
			encoder->EncoderFlags |= EncoderFlags_Sib;
			encoder->Sib = (unsigned char)(0x25 | (scale << 6));
			return;
		}
		else
		{
			encoder->ModRM |= 5;
			encoder->DisplSize = DisplSize_Size4;
			return;
		}
	}

	int baseNum = baseReg == Register_None ? -1 : baseReg - baseRegLo;
	int indexNum = indexReg == Register_None ? -1 : indexReg - indexRegLo;

	// [ebp]/[ebp+index*scale] => [ebp+00]/[ebp+index*scale+00]
	if (displSize == 0 && (baseNum & 7) == 5)
	{
		displSize = 1;
		if (encoder->Displ != 0)
		{
			//ErrorMessage = $"Operand {operand}: Displacement must be 0 if displSize == 0";
			return;
		}
	}

	if (displSize == 1)
	{
		signed char compressedValue = 0;
		if (Encoder_TryConvertToDisp8N(encoder, instruction, (int)encoder->Displ, &compressedValue))
		{
			encoder->Displ = (unsigned int)compressedValue;
		}
		else
		{
			displSize = addrSize / 8;
		}
	}

	if (baseReg == Register_None)
	{
		// Tested earlier in the method
		//Debug.Assert(indexReg != Register.None);
		encoder->DisplSize = DisplSize_Size4;
	}
	else if (displSize == 1)
	{
		// This if check should never be true when we're here
		if ((int)encoder->Displ < SCHAR_MIN || (int)encoder->Displ > SCHAR_MAX)
		{
			//ErrorMessage = $"Operand {operand}: Displacement must fit in an sbyte";
			return;
		}
		encoder->ModRM |= 0x40;
		encoder->DisplSize = DisplSize_Size1;
	}
	else if (displSize == addrSize / 8) {
		encoder->ModRM |= 0x80;
		encoder->DisplSize = DisplSize_Size4;
	}
	else if (displSize == 0)
	{
		if (encoder->Displ != 0)
		{
			//ErrorMessage = $"Operand {operand}: Displacement must be 0 if displSize == 0";
			return;
		}
	}
	else
	{
		//ErrorMessage = $"Operand {operand}: Invalid {nameof(Instruction.MemoryDisplSize)} value";
		return;
	}

	if (indexReg == Register_None && (baseNum & 7) != 4 && scale == 0 && (encoder->EncoderFlags & EncoderFlags_MustUseSib) == 0)
	{
		// Tested earlier in the method
		//Debug.Assert(baseReg != Register.None);
		encoder->ModRM |= (unsigned char)(baseNum & 7);
	}
	else
	{
		encoder->EncoderFlags |= EncoderFlags_Sib;
		encoder->Sib = (unsigned char)(scale << 6);
		encoder->ModRM |= 4;
		if (indexReg == Register_RSP || indexReg == Register_ESP)
		{
			//ErrorMessage = $"Operand {operand}: ESP/RSP can't be used as an index register";
			return;
		}
		if (baseNum < 0)
		{
			encoder->Sib |= 5;
		}
		else
		{
			encoder->Sib |= (unsigned char)(baseNum & 7);
		}
		if (indexNum < 0)
		{
			encoder->Sib |= 0x20;
		}
		else
		{
			encoder->Sib |= (unsigned char)((indexNum & 7) << 3);
		}
	}

	if (baseNum >= 0)
	{
		//Static.Assert((int)EncoderFlags.B == 1 ? 0 : -1);
		//Debug.Assert(baseNum <= 15);// No '& 1' required below
		encoder->EncoderFlags |= (enum EncoderFlags)(baseNum >> 3);
	}
	if (indexNum >= 0)
	{
		//Static.Assert((int)EncoderFlags.X == 2 ? 0 : -1);
		encoder->EncoderFlags |= (enum EncoderFlags)((indexNum >> 2) & 2);
		encoder->EncoderFlags |= (enum EncoderFlags)((indexNum & 0x10) << (int)EncoderFlags_VvvvvShift);
		//Debug.Assert(indexNum <= 31);
	}
}

void Encoder_WritePrefixes(struct Encoder* encoder, struct Instruction* instruction, bool canWriteF3) // bool canWriteF3 = true
{
	//Debug.Assert(!handler.IsSpecialInstr);
	enum Register seg = Get_SegmentPrefix(instruction);
	if (seg != Register_None)
	{
		//Debug.Assert((uint)(seg - Register.ES) < (uint)SegmentOverrides.Length);
		Encoder_WriteByteInternal(encoder, SegmentOverrides[seg - Register_ES]);
	}
	if ((encoder->EncoderFlags & EncoderFlags_PF0) != 0 || Get_HasLockPrefix(instruction))
	{
		Encoder_WriteByteInternal(encoder, 0xF0);
	}
	if ((encoder->EncoderFlags & EncoderFlags_P66) != 0)
	{
		Encoder_WriteByteInternal(encoder, 0x66);
	}
	if ((encoder->EncoderFlags & EncoderFlags_P67) != 0)
	{
		Encoder_WriteByteInternal(encoder, 0x67);
	}
	if (canWriteF3 && Get_HasRepePrefix(instruction))
	{
		Encoder_WriteByteInternal(encoder, 0xF3);
	}
	if (Get_HasRepnePrefix(instruction))
	{
		Encoder_WriteByteInternal(encoder, 0xF2);
	}
}

void Encoder_WriteModRM(struct Encoder* encoder)
{
	//Debug.Assert(!handler.IsSpecialInstr);
	//Debug.Assert((EncoderFlags & (EncoderFlags.ModRM | EncoderFlags.Displ)) != 0);
	if ((encoder->EncoderFlags & EncoderFlags_ModRM) != 0)
	{
		Encoder_WriteByteInternal(encoder, encoder->ModRM);
		if ((encoder->EncoderFlags & EncoderFlags_Sib) != 0)
		{
			Encoder_WriteByteInternal(encoder, encoder->Sib);
		}
	}

	unsigned int diff4;
	encoder->displAddr = (unsigned int)encoder->currentRip;
	switch (encoder->DisplSize)
	{
	case DisplSize_None:
		break;

	case DisplSize_Size1:
		Encoder_WriteByteInternal(encoder, encoder->Displ);
		break;

	case DisplSize_Size2:
		diff4 = encoder->Displ;
		Encoder_WriteByteInternal(encoder, diff4);
		Encoder_WriteByteInternal(encoder, diff4 >> 8);
		break;

	case DisplSize_Size4:
		diff4 = encoder->Displ;
		Encoder_WriteByteInternal(encoder, diff4);
		Encoder_WriteByteInternal(encoder, diff4 >> 8);
		Encoder_WriteByteInternal(encoder, diff4 >> 16);
		Encoder_WriteByteInternal(encoder, diff4 >> 24);
		break;

	case DisplSize_Size8:
		diff4 = encoder->Displ;
		Encoder_WriteByteInternal(encoder, diff4);
		Encoder_WriteByteInternal(encoder, diff4 >> 8);
		Encoder_WriteByteInternal(encoder, diff4 >> 16);
		Encoder_WriteByteInternal(encoder, diff4 >> 24);
		diff4 = encoder->DisplHi;
		Encoder_WriteByteInternal(encoder, diff4);
		Encoder_WriteByteInternal(encoder, diff4 >> 8);
		Encoder_WriteByteInternal(encoder, diff4 >> 16);
		Encoder_WriteByteInternal(encoder, diff4 >> 24);
		break;

	case DisplSize_RipRelSize4_Target32:
		unsigned int eip = (unsigned int)encoder->currentRip + 4 + s_immSizes[(int)encoder->ImmSize];
		diff4 = encoder->Displ - eip;
		Encoder_WriteByteInternal(encoder, diff4);
		Encoder_WriteByteInternal(encoder, diff4 >> 8);
		Encoder_WriteByteInternal(encoder, diff4 >> 16);
		Encoder_WriteByteInternal(encoder, diff4 >> 24);
		break;

	case DisplSize_RipRelSize4_Target64:
		unsigned long rip = encoder->currentRip + 4 + s_immSizes[(int)encoder->ImmSize];
		long diff8 = (long)(((unsigned long)encoder->DisplHi << 32) | (unsigned long)encoder->Displ) - (long)rip;
		if (diff8 < INT_MIN || diff8 > INT_MAX)
		{
			//ErrorMessage = $"RIP relative distance is too far away: NextIP: 0x{rip:X16} target: 0x{DisplHi:X8}{Displ:X8}, diff = {diff8}, diff must fit in an Int32";
		}
		diff4 = (unsigned int)diff8;
		Encoder_WriteByteInternal(encoder, diff4);
		Encoder_WriteByteInternal(encoder, diff4 >> 8);
		Encoder_WriteByteInternal(encoder, diff4 >> 16);
		Encoder_WriteByteInternal(encoder, diff4 >> 24);
		break;

	default:
		//throw new InvalidOperationException();
		break;
	}
}

void Encoder_WriteImmediate(struct Encoder* encoder)
{
	//Debug.Assert(!handler.IsSpecialInstr);
	unsigned short ip;
	unsigned int eip;
	unsigned long rip;
	short diff2;
	int diff4;
	long diff8;
	unsigned int value;
	encoder->immAddr = (unsigned int)encoder->currentRip;
	switch (encoder->ImmSize)
	{
	case ImmSize_None:
		break;

	case ImmSize_Size1:
	case ImmSize_SizeIbReg:
	case ImmSize_Size1OpCode:
		Encoder_WriteByteInternal(encoder, encoder->Immediate);
		break;

	case ImmSize_Size2:
		value = encoder->Immediate;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		break;

	case ImmSize_Size4:
		value = encoder->Immediate;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		Encoder_WriteByteInternal(encoder, value >> 16);
		Encoder_WriteByteInternal(encoder, value >> 24);
		break;

	case ImmSize_Size8:
		value = encoder->Immediate;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		Encoder_WriteByteInternal(encoder, value >> 16);
		Encoder_WriteByteInternal(encoder, value >> 24);
		value = encoder->ImmediateHi;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		Encoder_WriteByteInternal(encoder, value >> 16);
		Encoder_WriteByteInternal(encoder, value >> 24);
		break;

	case ImmSize_Size2_1:
		value = encoder->Immediate;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		Encoder_WriteByteInternal(encoder, encoder->ImmediateHi);
		break;

	case ImmSize_Size1_1:
		Encoder_WriteByteInternal(encoder, encoder->Immediate);
		Encoder_WriteByteInternal(encoder, encoder->ImmediateHi);
		break;

	case ImmSize_Size2_2:
		value = encoder->Immediate;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		value = encoder->ImmediateHi;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		break;

	case ImmSize_Size4_2:
		value = encoder->Immediate;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		Encoder_WriteByteInternal(encoder, value >> 16);
		Encoder_WriteByteInternal(encoder, value >> 24);
		value = encoder->ImmediateHi;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		break;

	case ImmSize_RipRelSize1_Target16:
		ip = (unsigned short)((unsigned int)encoder->currentRip + 1);
		diff2 = (short)((short)encoder->Immediate - (short)ip);
		if (diff2 < SCHAR_MIN || diff2 > SCHAR_MAX)
		{
			//ErrorMessage = $"Branch distance is too far away: NextIP: 0x{ip:X4} target: 0x{(ushort)Immediate:X4}, diff = {diff2}, diff must fit in an Int8";
		}
		Encoder_WriteByteInternal(encoder, (unsigned int)diff2);
		break;

	case ImmSize_RipRelSize1_Target32:
		eip = (unsigned int)encoder->currentRip + 1;
		diff4 = (int)encoder->Immediate - (int)eip;
		if (diff4 < SCHAR_MIN || diff4 > SCHAR_MAX)
		{
			//ErrorMessage = $"Branch distance is too far away: NextIP: 0x{eip:X8} target: 0x{Immediate:X8}, diff = {diff4}, diff must fit in an Int8";
		}
		Encoder_WriteByteInternal(encoder, (unsigned int)diff4);
		break;

	case ImmSize_RipRelSize1_Target64:
		rip = encoder->currentRip + 1;
		diff8 = (long)(((unsigned long)encoder->ImmediateHi << 32) | (unsigned long)encoder->Immediate) - (long)rip;
		if (diff8 < SCHAR_MIN || diff8 > SCHAR_MAX)
		{
			//ErrorMessage = $"Branch distance is too far away: NextIP: 0x{rip:X16} target: 0x{ImmediateHi:X8}{Immediate:X8}, diff = {diff8}, diff must fit in an Int8";
		}
		Encoder_WriteByteInternal(encoder, (unsigned int)diff8);
		break;

	case ImmSize_RipRelSize2_Target16:
		eip = (unsigned int)encoder->currentRip + 2;
		value = encoder->Immediate - eip;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		break;

	case ImmSize_RipRelSize2_Target32:
		eip = (unsigned int)encoder->currentRip + 2;
		diff4 = (int)(encoder->Immediate - eip);
		if (diff4 < SCHAR_MIN || diff4 > SCHAR_MAX)
		{
			//ErrorMessage = $"Branch distance is too far away: NextIP: 0x{eip:X8} target: 0x{Immediate:X8}, diff = {diff4}, diff must fit in an Int16";
		}
		value = (unsigned int)diff4;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		break;

	case ImmSize_RipRelSize2_Target64:
		rip = encoder->currentRip + 2;
		diff8 = (long)(((unsigned long)encoder->ImmediateHi << 32) | (unsigned long)encoder->Immediate) - (long)rip;
		if (diff8 < SCHAR_MIN || diff8 > SCHAR_MAX)
		{
			//ErrorMessage = $"Branch distance is too far away: NextIP: 0x{rip:X16} target: 0x{ImmediateHi:X8}{Immediate:X8}, diff = {diff8}, diff must fit in an Int16";
		}
		value = (unsigned int)diff8;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		break;

	case ImmSize_RipRelSize4_Target32:
		eip = (unsigned int)encoder->currentRip + 4;
		value = encoder->Immediate - eip;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		Encoder_WriteByteInternal(encoder, value >> 16);
		Encoder_WriteByteInternal(encoder, value >> 24);
		break;

	case ImmSize_RipRelSize4_Target64:
		rip = encoder->currentRip + 4;
		diff8 = (long)(((unsigned long)encoder->ImmediateHi << 32) | (unsigned long)encoder->Immediate) - (long)rip;
		if (diff8 < INT_MIN || diff8 > INT_MAX)
		{
			//ErrorMessage = $"Branch distance is too far away: NextIP: 0x{rip:X16} target: 0x{ImmediateHi:X8}{Immediate:X8}, diff = {diff8}, diff must fit in an Int32";
		}
		value = (unsigned int)diff8;
		Encoder_WriteByteInternal(encoder, value);
		Encoder_WriteByteInternal(encoder, value >> 8);
		Encoder_WriteByteInternal(encoder, value >> 16);
		Encoder_WriteByteInternal(encoder, value >> 24);
		break;

	default:
		//throw new InvalidOperationException();
		break;
	}
}

/// <summary>
/// Encodes an instruction
/// </summary>
/// <param name="instruction">Instruction to encode</param>
/// <param name="rip"><c>RIP</c> of the encoded instruction</param>
/// <param name="encodedLength">Updated with length of encoded instruction if successful</param>
/// <param name="errorMessage">Set to the error message if we couldn't encode the instruction</param>
/// <returns></returns>
bool Encoder_TryEncode(struct Encoder* encoder, struct Instruction* instruction, unsigned long rip, unsigned int* encodedLength)
{
	encoder->currentRip = rip;
	encoder->eip = (unsigned int)rip;

	encoder->EncoderFlags = EncoderFlags_None;
	encoder->DisplSize = DisplSize_None;
	encoder->ImmSize = ImmSize_None;
	encoder->ModRM = 0;

	//var handler = handlers[(int)instruction.Code];
	//this.handler = handler;

	encoder->handler = GetOpCodeHandler(GetCode(instruction));
	enum OpCode code = encoder->handler->OpCode;
	encoder->OpCode = (unsigned int)code;

	if (encoder->handler->GroupIndex >= 0)
	{
		//Debug.Assert(EncoderFlags == 0);
		encoder->EncoderFlags = EncoderFlags_ModRM;
		encoder->ModRM = (unsigned char)(encoder->handler->GroupIndex << 3);
	}
	if (encoder->handler->RmGroupIndex >= 0)
	{
		//Debug.Assert(EncoderFlags == 0 || EncoderFlags == EncoderFlags.ModRM);
		encoder->EncoderFlags = EncoderFlags_ModRM;
		encoder->ModRM |= (unsigned char)(encoder->handler->RmGroupIndex | 0xC0);
	}

	switch (encoder->handler->EncFlags3 & (EFLAGS3_Bit16or32 | EFLAGS3_Bit64))
	{
	case EFLAGS3_Bit16or32 | EFLAGS3_Bit64:
		break;

	case EFLAGS3_Bit16or32:
		if (encoder->bitness == 64)
		{
			//ErrorMessage = ERROR_ONLY_1632_BIT_MODE;
		}
		break;

	case EFLAGS3_Bit64:
		if (encoder->bitness != 64)
		{
			//ErrorMessage = ERROR_ONLY_64_BIT_MODE;
		}
		break;

	default:
		//throw new InvalidOperationException();
		break;
	}

	switch (encoder->handler->OpSize)
	{
	case CodeSize_Unknown:
		break;

	case CodeSize_Code16:
		encoder->EncoderFlags |= encoder->opSize16Flags;
		break;

	case CodeSize_Code32:
		encoder->EncoderFlags |= encoder->opSize32Flags;
		break;

	case CodeSize_Code64:
		if ((encoder->handler->EncFlags3 & EFLAGS3_DefaultOpSize64) == 0)
			encoder->EncoderFlags |= EncoderFlags_W;
		break;

	default:
		//throw new InvalidOperationException();
		break;
	}

	switch (encoder->handler->AddrSize)
	{
	case CodeSize_Unknown:
		break;

	case CodeSize_Code16:
		encoder->EncoderFlags |= encoder->adrSize16Flags;
		break;

	case CodeSize_Code32:
		encoder->EncoderFlags |= encoder->adrSize32Flags;
		break;

	case CodeSize_Code64:
		break;
	default:
		//throw new InvalidOperationException();
		break;
	}

	if (!encoder->handler->IsSpecialInstr)
	{
		struct Op* ops = encoder->handler->Operands;
		for (int i = 0; i < encoder->handler->Operands_Length; i++)
		{
			ops[i].Encode(encoder, instruction, i, (ops + i));
		}

		if ((encoder->handler->EncFlags3 & EFLAGS3_Fwait) != 0)
		{
			WriteByteInternal(0x9B);
		}

		encoder->handler->Encode(encoder->handler, encoder, instruction);

		unsigned int opCode = encoder->OpCode;
		if (!encoder->handler->Is2ByteOpCode)
		{
			WriteByteInternal(opCode);
		}
		else
		{
			WriteByteInternal(opCode >> 8);
			WriteByteInternal(opCode);
		}

		if ((encoder->EncoderFlags & (EncoderFlags_ModRM | EncoderFlags_Displ)) != 0)
		{
			WriteModRM();
		}

		if (encoder->ImmSize != ImmSize_None)
		{
			WriteImmediate();
		}
	}
	else
	{
		//Debug.Assert(handler is DeclareDataHandler || handler is ZeroBytesHandler);
		encoder->handler->Encode(encoder->handler, encoder, instruction);
	}

	unsigned int instrLen = (unsigned int)encoder->currentRip - (unsigned int)rip;
	//if (instrLen > IcedConstants.MaxInstructionLength && !handler.IsSpecialInstr)
	//	ErrorMessage = $"Instruction length > {IcedConstants.MaxInstructionLength} bytes";
	//errorMessage = this.errorMessage;
	//if (errorMessage is not null) {
	//	encodedLength = 0;
	//	return false;
	//}
	*encodedLength = instrLen;
	return true;
}